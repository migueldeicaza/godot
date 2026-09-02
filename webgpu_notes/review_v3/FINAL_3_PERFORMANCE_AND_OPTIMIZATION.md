# Godot WebGPU Backend — Performance & Optimization

## The Core Problem

WebGPU on the web runs inside a browser sandbox where every GPU API call crosses an inter-process communication (IPC) boundary. The cost structure:

| Environment | Cost per GPU call | Mechanism |
|------------|-------------------|-----------|
| Native (Vulkan/Metal/D3D12) | ~5 ns | Direct memory-mapped command buffer write |
| WebGPU (Chromium, desktop) | ~200-500 ns | WASM→JS context switch → Mojo IPC → GPU process |
| WebGPU (Chromium, mobile) | ~500-1500 ns | Same + slower IPC on mobile SoCs |

This 40-250x per-call overhead means Godot's forward mobile renderer — designed around "commands are free" — becomes **IPC-bound, not GPU-bound**. The GPU sits idle waiting for commands to arrive through the serialization pipeline.

---

## The Optimization Philosophy

A clear hierarchy drives all performance work:

1. **Eliminate calls entirely** — batching, merging, deduplication
2. **Reduce call frequency** — state caching, dirty tracking
3. **Reduce per-call cost** — command buffering (explored, parked)

All optimizations are gated behind `ApiTrait` enums queried at initialization, making them **WebGPU-only with zero impact on Vulkan/Metal/D3D12** code paths. This is a clean extension mechanism that avoids polluting the core renderer.

---

## The Journey from 3.25x Slower to Parity

### Benchmark: Shadow Stress Scene (20k instances, 32 omni lights)

| Phase | Key Changes | Frame Time | vs Native |
|-------|-------------|-----------|-----------|
| Baseline | None | 133 ms | 3.25x slower |
| +Staging fixes | Dirty tracking, re-dirty fix | ~120 ms | ~2.9x |
| +Dual-paraboloid + pass merging | Force DP, merge same-FB | 76 ms | ~1.86x |
| +Shadow instance batching | Merge same-mesh shadow draws | 57 ms | ~1.39x |
| +Skeleton atlas | Single writeBuffer per frame | ~48 ms | ~1.17x |
| +firstInstance dedup | Skip push constant IPC | ~46 ms | ~1.12x |
| +Color pass batching | Merge color draws | ~36 ms | **Parity** |

### IPC Crossings Per Frame (3D Platformer)

| Stage | Crossings/Frame | Cost at 0.3us each |
|-------|----------------|-------------------|
| Baseline | ~23,837 | 7.15 ms (23.6% of frame) |
| After all optimizations | ~8,700 | 2.6 ms (11% of frame) |

---

## Layer 0: Staging Buffer Architecture

### Problem

Godot's `RenderingDevice` uses staging buffers for all CPU-to-GPU transfers. On Vulkan/Metal, these are GPU-mapped memory — idle blocks cost nothing. On WebGPU, the driver emulates staging via "shadow buffers" (CPU-side `memalloc` allocations). Three pathologies emerged:

1. **Unbounded pool growth**: Default 128MB cap allowed 69 blocks × 256KB = 17.7MB to accumulate during loading
2. **Full-buffer flush on unmap**: `buffer_unmap()` copied the entire shadow buffer (up to 32MB) to GPU via `wgpuQueueWriteBuffer` on every call, even if only a few bytes changed
3. **Re-dirtying after flush**: End-of-frame flush loop re-mapped buffers, unconditionally setting `map_dirty=true`, causing the next frame to flush ALL staging blocks

### Solution 1: 16MB Pool Cap

`API_TRAIT_STAGING_BUFFER_MAX_SIZE_MB` returns 16 for WebGPU. The `RenderingDevice::initialize()` method clamps `upload_staging_buffers.max_size` to this value. Overflow is handled by the existing stall-and-reuse path.

**Why 16MB**: Generous for per-frame dynamic updates (uniforms, instance data, bones). Large texture uploads go through the direct-write path (see below).

### Solution 2: Dirty Range Tracking

Added `dirty_offset`/`dirty_end` fields to `WGBuffer`:

```cpp
uint64_t dirty_offset = 0;
uint64_t dirty_end = 0; // 0 means "no explicit range set"
```

Three key changes:
- `buffer_unmap()`/`buffer_flush()` only flush the dirty range
- `buffer_persistent_map_advance()` sets dirty range to current frame's slice only
- Copy operations clear `map_dirty = false` (their transfer handles the flush)

**Impact**: Texture creation dropped from 10-25ms to 1-4.6ms. Per-frame uniform flushes write only the active frame slice.

### Solution 3: Eliminate End-of-Frame Re-Dirtying

The `_end_frame()` staging flush loop previously called `buffer_map()` after each `buffer_unmap()` to keep `data_ptr` valid. This unconditionally set `map_dirty = true`, causing the next frame to flush ALL 69 blocks (17.7MB) even if none were written.

The fix removes the re-map call entirely — shadow buffers and `data_ptr` persist after unmap (they're CPU allocations, not GPU mappings).

**Impact**: Max frame time dropped from 83-100ms to 32ms. The recurring ~82ms spike was eliminated. Staging flush cost: 2-54ms → 0.25ms (200x improvement).

### Solution 4: Mapped-at-Creation for Initial Buffer Data

New `buffer_create_with_data()` creates buffers with `mappedAtCreation = true`, memcpys data directly into the mapped range, then unmaps. This eliminates:
- Transfer worker staging buffer allocation
- `wgpuQueueWriteBuffer` calls (each ~9ms WASM-to-JS overhead)
- Command encoder copy from staging to destination

Applied to all five buffer creation paths: vertex, index, storage, texture, uniform.

---

## Layer 1: Shadow Pass Merging + Forced Dual-Paraboloid

### Problem

Each OmniLight3D with cubemap shadows generates 6 render pass encoder cycles + 2 copy-to-atlas operations. With 32 omni lights: ~192 cubemap passes + 64 copies + 4 directional splits = ~260 render pass operations per frame.

### Solution Part 1: Force Dual-Paraboloid Shadows

`API_TRAIT_FORCE_OMNI_DUAL_PARABOLOID` overrides `light_omni_get_shadow_mode()` to always return `SHADOW_DUAL_PARABOLOID`. 2 passes per light instead of 6+2.

**Quality trade-off**: Dual-paraboloid shadows have a seam at the hemisphere equator and slightly different sampling distribution. Acceptable for web where performance is the bottleneck.

### Solution Part 2: Merge Same-Framebuffer Shadow Passes

Pre-clear the positional shadow atlas once upfront, then render all positional shadow passes with `clear_region = false`. In `_render_shadow_end()`, consecutive passes sharing the same framebuffer are batched into a single render pass with viewport/scissor changes.

```
Before: 196 render pass begin/end cycles per frame
After:  4 render pass begin/end cycles per frame
```

**Impact**: Frame time: 133ms → 76ms (-43%).

---

## Layer 2: Instance Batching

### Shadow Pass Instance Batching

20,000 instances sharing the same mesh surface and shadow material previously issued 20,000 individual draw calls (40,000 IPC crossings: push constant + draw each).

The batching lookahead in `_render_list_template` detects consecutive draws sharing:
- Same mesh surface (shadow variant)
- Same material uniform set
- Same LOD index
- Same cull variant

When detected, they merge into a single instanced draw: `drawIndexed(indexCount, batchCount, ...)`.

The shader computes the correct per-instance data index:
```glsl
batch_instance_index = draw_call.instance_index + uint(gl_InstanceIndex);
```

**Impact**: 20,000 draws → 1 instanced draw. Frame time: 76ms → 57ms (-25%).

### Color Pass Instance Batching

Extended the same lookahead to opaque color passes with additional state checks:
- Same mesh surface (color variant)
- Same material uniform set
- Same cull mode, lightmap usage
- Same pipeline specialization (light counts, projector, soft shadow flags)
- Same transforms uniform set

**Safety exclusions** (zero-cost compile-time template checks):
- Transparent pass excluded (alpha-sorted draws must preserve back-to-front order)
- Skinned meshes excluded (per-instance vertex buffers)
- Multimesh excluded (uses gl_InstanceIndex for its own indexing)
- Particle instances excluded

**Impact (60k shared-material instances)**: Draw calls: 32,190 → 14 (-99.96%). FPS: 20.5 → 27.6 (+34.6%).

---

## Layer 3: firstInstance + Push Constant Deduplication

### Problem

Even after batching, non-batchable draws (different meshes/materials) still require per-draw `SetBindGroup` for the push constant dynamic offset — this is the remaining per-draw IPC that instance batching cannot eliminate.

### Solution

Encode the per-draw `base_index` in `drawIndexed`'s `firstInstance` parameter instead of the push constant:

```glsl
// Shader recovers: 0 + firstInstance = correct base_index
batch_instance_index = draw_call.instance_index + uint(gl_InstanceIndex);
// draw_call.instance_index = 0 (in push constant)
// gl_InstanceIndex = firstInstance value from draw call
```

After moving `base_index` out, compare remaining push constant fields via `memcmp`. If unchanged from previous draw, skip the `SetBindGroup` call entirely:

```cpp
push_constant.base_index = 0; // Move to firstInstance
bool need_pc = !pc_set_for_current_pipeline;
if (!need_pc && have_prev_fi_push_constant) {
    need_pc = memcmp(&push_constant, &prev_push_constant, size) != 0;
}
```

**Key insight**: Consecutive draws often share the same ubershader specialization (quantized light counts, feature flags). With `base_index` moved out, their push constants become identical.

**Impact (20k instances)**: Push constant writes: 11,610 → 5 per frame (-99.96%). FPS: 30.2 → 31.3 (+3.6%).

**Interaction with batching**: These are complementary, not conflicting:
- When draws CAN be batched (same mesh/material): merge into instanced draw (1 IPC total)
- When draws CANNOT be batched: firstInstance dedup skips the PC SetBindGroup (1 IPC instead of 2)
- `batch_count == 1` is required for the firstInstance path (prevents conflict)

---

## Skeleton Atlas: Single-Buffer Bone Upload

### Problem

Per-skeleton GPU buffer updates generate one `wgpuQueueWriteBuffer` call per skeleton per frame. In animation-heavy scenes: ~4000 IPC crossings.

### Solution

Single shared GPU buffer ("skeleton atlas") holds all bone data. Per-frame:
1. Walk dirty skeleton list, memcpy each skeleton's data into CPU mirror at its offset
2. Track `atlas_dirty_min`/`atlas_dirty_max` across all dirty skeletons
3. Single `buffer_update_direct()` covering the dirty range

Shader gains `bone_offset` push constant (repurposed padding field):
```glsl
uvec2 bones_01 = uvec2(bones.x & 0xFFFF, bones.x >> 16) * 2 + params.bone_offset;
```

**Memory**: Power-of-two growth (minimum 64KB, doubling on overflow). Bump allocator only — no deallocation (acceptable for typical fixed-skeleton-count scenes).

**Impact**: ~4000 IPC crossings → 1. Frame time: -17%.

---

## Texture Upload Optimization

### Batched Layered Texture Uploads

New `command_copy_buffer_to_texture_layered` collapses N per-layer `wgpuQueueWriteTexture` calls into one call with `extent.depthOrArrayLayers = N`. WebGPU supports writing multiple array layers in a single call when consecutively laid out in memory.

### Direct CPU→GPU Texture Write

`API_TRAIT_TEXTURE_INITIALIZE_DIRECT_WRITE` + `texture_initialize_direct_layered` eliminates the transfer worker entirely for Texture2DArray uploads:

```
Old: memalloc → pack → transfer worker staging buffer (GPU) → command encoder copy → GPU texture
New: memalloc → pack → single wgpuQueueWriteTexture from CPU pointer → memfree
```

No transfer worker, no GPU staging buffer, no command encoder, no pipeline barriers.

**Impact**: `create_from_images` total: 2877ms → 240ms (-92%). Peak VRAM per upload: -300MB.

---

## Redundant State Caching

The `WGCommandBuffer::RenderState` tracks currently-bound state:

```cpp
WGPURenderPipeline *current_pipeline = nullptr;
WGPUBuffer current_index_buffer = nullptr;
uint64_t current_index_offset = 0;
WGPUBuffer current_vertex_buffers[8] = {};
uint64_t current_vertex_offsets[8] = {};
```

Each bind call compares against cached state and skips the `wgpu*` call when unchanged. State resets at render pass begin and subpass transitions.

Impact is minimal with unique materials but meaningful with shared-material scenes where consecutive draws bind the same pipeline/vertex buffers.

---

## Command Buffering (Explored, Parked)

### Concept

Record all render pass encoder commands into a flat buffer (48 bytes/command, 16k capacity = 768KB) in WASM linear memory. Replay via single `EM_JS` call per render pass:

```javascript
for (var i = 0; i < count; i++) {
    switch (heap[base]) {
        case 0: pass.setPipeline(obj[heap[base + 1]]); break;
        case 1: pass.setBindGroup(...); break;
        // 12 command types total
    }
    base += CMD_U32;
}
```

### Results

- **M3 Ultra desktop**: Neutral (trampoline savings ~40ns/call offset by JS replay loop ~40ns/iteration)
- **Expected benefit on mobile/Chromebook**: 3-5x where trampoline is proportionally more expensive

### Decision

Parked for future use. Not merged because it adds complexity without measurable benefit on current target hardware. The optimization layers above (batching, firstInstance) already reduced per-frame crossings from ~23,837 to ~8,700, making the remaining trampoline overhead a smaller fraction.

---

## How the Optimizations Interact

```
Layer 0: Staging fixes + Direct Write     — eliminates loading-time and per-frame overhead
Layer 1: Shadow Pass Merging + DP mode    — reduces render pass count (additive with Layer 0)
Layer 2: Instance Batching                — reduces draw count within passes (builds on Layer 1)
Layer 3: firstInstance + PC dedup         — reduces per-draw overhead for non-batchable draws
```

**Layer 1 enables Layer 2**: Shadow pass merging puts all shadow draws into a single render pass, making consecutive same-mesh draws sort-adjacent — a prerequisite for effective batching.

**Layer 2 and Layer 3 are complementary**: Instance batching handles same-mesh draws; firstInstance handles different-mesh draws. They never conflict (`batch_count == 1` required for firstInstance).

**Layer 0 is orthogonal**: Staging fixes address loading-time and per-frame data transfer overhead, independent of draw call optimizations.

---

## API Trait Summary

| Trait | Value | Purpose |
|-------|-------|---------|
| `STAGING_BUFFER_MAX_SIZE_MB` | 16 | Cap staging pool heap waste |
| `BUFFER_CREATE_MAPPED_AT_CREATION` | 1 | Zero-copy initial buffer data |
| `TEXTURE_INITIALIZE_DIRECT_WRITE` | 1 | Bypass transfer worker for textures |
| `SKELETON_BUFFER_DIRECT_WRITE` | 1 | Direct queue write for bone atlas |
| `FORCE_OMNI_DUAL_PARABOLOID` | 1 | Reduce shadow pass count |
| `BATCH_INSTANCE_DRAWS` | 1 | Merge same-state draws |
| `FIRST_INSTANCE_INDEX` | 1 | Encode base_index via firstInstance |

All traits default to 0 in the base class. Non-WebGPU backends are completely unaffected.

---

## Performance Diagnostics

### Built-in Counters (logged every second)

- `draws`: Total draw calls issued
- `set_bind_group` / `set_bind_group_skipped`: Bind group calls issued vs deduplicated
- `push_constant_writes` / `push_constant_skipped`: PC ring writes vs skipped via dirty-check
- `render_passes`: Total encoder begin/end cycles
- `bg_cache_miss`: BGL rebind cache misses
- `set_pipeline` / `set_vertex_buffer`: Pipeline/VB bind calls
- `gap_bind_group`: Empty bind group fills for Firefox compatibility
- `first_instance_draws`: Draws using firstInstance encoding

### SPIR-V Cache Diagnostics

- Hit/miss counters per conversion
- Total conversions logged during startup

---

## Remaining Optimization Opportunities

1. **Command buffering on mobile**: Re-evaluate when mobile WebGPU is a primary target. Expected 3-5x trampoline savings on Chromebook/mobile.

2. **Skeleton atlas compaction**: Add free-list or generational compaction for dynamic skeleton scenes.

3. **Material sort hints**: Optimize render list sort key to prefer same-mesh grouping within materials, increasing batch hit rates.

4. **Batch size cap**: Add configurable maximum (e.g., 4096 instances) for mobile GPU scheduling.

5. **Shader pre-compilation**: Run naga at export time and ship WGSL directly. Runtime conversion only for specialization constants that differ from defaults. Would eliminate the sometimes up to ~15s startup cost.

6. **Push constant ring sizing**: Monitor peak usage and right-size the 256KB ring buffer. Add runtime warning when approaching the 1024-slot limit.

7. **Texture upload streaming**: Split large atlas uploads across multiple frames to avoid frame spikes during scene transitions.

---

## Key Files

| File | Role |
|------|------|
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | All WebGPU-specific fast paths |
| `drivers/webgpu/webgpu_objects.h` | WGBuffer dirty tracking, RenderState |
| `servers/rendering/rendering_device.cpp` | Staging cap, direct write, firstInstance plumbing |
| `servers/rendering/rendering_device_driver.h` | API trait definitions |
| `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.cpp` | Batching, firstInstance, shadow merging |
| `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp` | Skeleton atlas |
| `servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl` | batch_instance_index |
| `servers/rendering/renderer_rd/shaders/skeleton.glsl` | bone_offset |
