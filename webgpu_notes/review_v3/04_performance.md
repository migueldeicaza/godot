# Performance Optimizations Review

## Overall Architecture and Philosophy

The godot-webgpu performance work addresses a fundamental structural problem: WebGPU on the web runs inside a browser sandbox where every GPU API call crosses an inter-process communication (IPC) boundary between WASM and the browser's GPU process. On native APIs (Vulkan, Metal, D3D12), a draw call is a ~5ns pointer write into a command buffer. On WebGPU in Chromium, the same operation costs ~0.2-0.5 microseconds due to Mojo IPC serialization. This 40-250x per-call overhead means Godot's forward mobile renderer -- designed around "commands are free" -- becomes IPC-bound rather than GPU-bound.

The optimization philosophy follows a clear hierarchy:

1. **Eliminate calls entirely** (batching, merging, deduplication)
2. **Reduce call frequency** (state caching, dirty tracking)
3. **Reduce per-call cost** (command buffering -- explored but parked)

All optimizations are gated behind `ApiTrait` enums queried at initialization, making them WebGPU-only with zero impact on Vulkan/Metal/D3D12 code paths. This is a clean extension mechanism that avoids polluting the core renderer with platform-specific branches.

---

## Staging Buffer Architecture

### The Problem

Godot's `RenderingDevice` uses staging buffers as intermediaries for all CPU-to-GPU data transfers. On Vulkan/Metal, staging buffers are GPU-mapped memory -- idle blocks cost nothing. On WebGPU, the driver emulates this via "shadow buffers" (CPU-side `memalloc` allocations) that persist in the JS heap after the initial loading spike subsides.

The staging system had three distinct pathologies on WebGPU:

1. **Unbounded pool growth**: Default 128 MB cap allowed 69 blocks x 256 KB = 17.7 MB to accumulate during loading and never shrink.
2. **Full-buffer flush on unmap**: `buffer_unmap()` copied the *entire* shadow buffer (often 32 MB) to the GPU via `wgpuQueueWriteBuffer` on every call, even if only a few bytes were written.
3. **Re-dirtying after flush**: The end-of-frame flush loop re-mapped buffers after unmapping them, which unconditionally set `map_dirty=true`, causing the *next frame* to redundantly flush all staging blocks.

### Solutions

#### 16 MB Pool Cap (`API_TRAIT_STAGING_BUFFER_MAX_SIZE_MB`)

**Commit:** `05279d146f`

**File:** `servers/rendering/rendering_device.cpp` (8 lines), `servers/rendering/rendering_device_driver.h` (6 lines), `drivers/webgpu/rendering_device_driver_webgpu.cpp` (5 lines)

The WebGPU driver returns 16 from the new trait. The `RenderingDevice::initialize()` method clamps `upload_staging_buffers.max_size` to this value before converting to bytes. Overflow is handled by the existing stall-and-reuse path (blocks that exceed the cap are reclaimed from the least-recently-used end).

**Design decision:** 16 MB is generous for per-frame dynamic updates (uniforms, instance data, skeleton bones). The initial loading spike still handles large textures via the direct-write paths below.

#### Dirty Range Tracking for Shadow Buffer Flushes

**Commit:** `4a04eaac78`

**Files:** `drivers/webgpu/webgpu_objects.h` (added `dirty_offset`/`dirty_end` fields to `WGBuffer`), `drivers/webgpu/rendering_device_driver_webgpu.cpp` (three flush paths updated)

The fix introduces range tracking to `WGBuffer`:

```cpp
uint64_t dirty_offset = 0;
uint64_t dirty_end = 0; // Exclusive end. 0 means "no explicit range set."
```

Three changes:
1. `buffer_unmap()` and `buffer_flush()` check `dirty_end > dirty_offset` -- if set, only that range is flushed via `wgpuQueueWriteBuffer`.
2. `buffer_persistent_map_advance()` sets the dirty range to the current frame's slice (`frame_idx * per_frame_size` to `+ per_frame_size`), so dynamic uniform buffers flush only their active frame slice instead of the entire multi-frame allocation.
3. `command_copy_buffer_to_texture`, `command_copy_buffer_to_texture_layered`, and `command_copy_buffer` clear `map_dirty = false` after they handle their own transfer, making the subsequent `buffer_unmap()` a no-op.

**Impact:** Texture creation dropped from 10-25ms to 1-4.6ms. Per-frame uniform flushes write only `per_frame_size` bytes instead of the entire multi-frame buffer.

#### Eliminating End-of-Frame Re-Dirtying

**Commit:** `f4382a3855`

**File:** `servers/rendering/rendering_device.cpp` (9 lines added, 2 removed)

The `_end_frame()` staging flush loop previously called `buffer_map()` after each `buffer_unmap()` to keep `data_ptr` valid. On WebGPU, `buffer_map()` unconditionally sets `map_dirty = true`. This caused the next frame to flush ALL 69 staging blocks (17.7 MB) even if none were written to.

The fix removes the re-map call entirely. The shadow buffer and `data_ptr` persist after unmap (they're CPU allocations, not GPU mappings). The comment explains why:

```cpp
// Note: we do NOT re-map after unmapping. The shadow buffer persists and
// data_ptr remains valid. Re-mapping would unconditionally set map_dirty,
// causing the next frame to redundantly flush ALL staging blocks.
```

**Impact:** Max frame time dropped from 83-100ms to 32ms. The recurring ~82ms spike at t=22s was eliminated. Staging flush cost went from 2-54ms to 0.25ms (200x improvement).

#### Mapped-at-Creation for Initial Buffer Data

**Commit:** `0e760adb2b`

**Files:** `servers/rendering/rendering_device_driver.h` (new virtual + trait), `drivers/webgpu/rendering_device_driver_webgpu.cpp` (38 lines), `servers/rendering/rendering_device.cpp` (40 lines across 5 buffer creation functions)

New `buffer_create_with_data()` virtual creates a buffer with `mappedAtCreation = true`, memcpys data directly into the mapped range, then unmaps. This eliminates:
- Transfer worker staging buffer allocation
- `wgpuQueueWriteBuffer` calls (each ~9ms WASM-to-JS overhead)
- Command encoder copy from staging to destination

Applied to all five buffer creation paths: `vertex_buffer_create`, `index_buffer_create`, `storage_buffer_create`, `texture_buffer_create`, `uniform_buffer_create`. Each checks `API_TRAIT_BUFFER_CREATE_MAPPED_AT_CREATION` and either takes the fast path or falls through to the existing staging path.

**Correctness note:** The implementation correctly zero-fills padding between `p_data_size` and the 4-byte-aligned buffer size, preventing uninitialized memory reads.

---

## Instance Batching and Draw Call Optimization

### Shadow Pass Merging and Forced Dual-Paraboloid

**Commit:** `63119b0a4f` (Optimization #3)

**Problem:** Each OmniLight3D with cubemap shadows generates 6 render pass encoder cycles + 2 copy-to-atlas operations. With 32 omni lights: ~192 cubemap passes + 64 copies + 4 directional splits = ~260 render pass operations per frame.

**Solution (two parts):**

1. **Force dual-paraboloid mode** (`API_TRAIT_FORCE_OMNI_DUAL_PARABOLOID`): Overrides `light_omni_get_shadow_mode()` in `LightStorage` to always return `LIGHT_OMNI_SHADOW_DUAL_PARABOLOID`. 2 passes per light instead of 6+2.

2. **Merge same-framebuffer shadow passes**: Pre-clear the positional shadow atlas once upfront, then render all positional shadow passes with `clear_region = false`. In `_render_shadow_end()`, consecutive passes sharing the same framebuffer are batched into a single render pass with viewport/scissor changes:

```cpp
while (i < scene_state.shadow_passes.size()) {
    // Find how many consecutive passes share this framebuffer
    uint32_t batch_end = i + 1;
    if (!first_pass.clear_depth) {
        while (batch_end < passes.size() &&
               passes[batch_end].framebuffer == first_pass.framebuffer &&
               !passes[batch_end].clear_depth) {
            batch_end++;
        }
    }
    // One render pass, multiple viewport-scoped draws
    ...
}
```

**Result:** Render passes per frame: 196 to 4. Frame time: 133ms to 76ms (-43%).

**Quality trade-off:** Dual-paraboloid shadows have slightly lower quality than cubemaps (seam at the equator, slightly different sampling characteristics). This is acceptable for web where performance is the bottleneck.

### Shadow Pass Instance Batching

**Commit:** `63119b0a4f` (Optimization #4)

**Problem:** 20,000 instances sharing the same mesh surface and shadow material issue 20,000 individual draw calls: 20,000 x 2 IPC (push constant + draw) = 40,000 crossings.

**Solution:** Lookahead in `_render_list_template` detects consecutive shadow draws sharing:
- Same mesh surface (shadow variant)
- Same material uniform set (shadow)
- Same LOD index
- Same cull variant (double-sided flag + mirror)

When detected, they merge into a single instanced draw: `drawIndexed(indexCount, batchCount, ...)`. The shader computes the correct per-instance data index:

```glsl
batch_instance_index = draw_call.instance_index + uint(gl_InstanceIndex);
```

This formula works because `draw_call.instance_index` (from the push constant) gives the base, and `gl_InstanceIndex` (0..N-1 within the instanced draw) provides the offset.

**Result:** 20,000 draws merged to 1 instanced draw. Frame time: 76ms to 57ms (-25%).

### Color Pass Instance Batching Extension

**Commit:** `e609353f14`

**Problem:** After the shadow optimizations, color pass draws became the dominant IPC source. Same-mesh, same-material instances are already sort-adjacent in Godot's render list (sorted by shader, material, geometry).

**Solution:** Extended the shadow batching lookahead to also run in opaque color passes. Additional state checks required for color passes:
- Same mesh surface (color variant, not shadow variant)
- Same material uniform set (color)
- Same cull mode (`mirror` flag)
- Same lightmap usage (`uses_lightmap`)
- Same pipeline specialization: `use_projector`, `use_soft_shadow`, quantized light/probe/decal counts
- Same transforms uniform set

**Safety exclusions:**
- `PASS_MODE_COLOR_TRANSPARENT` is excluded -- alpha-sorted draws must preserve back-to-front order. This check is a compile-time template parameter, so zero runtime cost.
- `mesh_instance.is_valid()` excluded -- skinned/blend-shape meshes have per-instance vertex buffers that cannot share an instanced draw.

**Result (scene_h, 60k shared-material instances):** Draw calls per frame: 32,190 to 14 (-99.96%). Mean FPS: 20.5 to 27.6 (+34.6%).

### Redundant Draw State Caching

**Commit:** `63119b0a4f` (Optimization #2)

**Implementation:** The `WGCommandBuffer::RenderState` struct tracks currently-bound state:

```cpp
WGPURenderPipeline *current_pipeline = nullptr;
WGPUBuffer current_index_buffer = nullptr;
uint64_t current_index_offset = 0;
static constexpr uint32_t MAX_VERTEX_BINDINGS = 8;
WGPUBuffer current_vertex_buffers[MAX_VERTEX_BINDINGS] = {};
uint64_t current_vertex_offsets[MAX_VERTEX_BINDINGS] = {};
```

`command_bind_render_pipeline`, `command_render_bind_vertex_buffers`, and `command_render_bind_index_buffer` compare against cached state and skip the `wgpu*` call when unchanged. State is reset at render pass begin and subpass transitions.

**Impact:** Negligible gain with unique materials (state changes every draw), but meaningful with shared-material scenes where multiple consecutive draws bind the same pipeline and vertex buffers.

---

## IPC and Data Transfer Optimization

### Texture2DArray Batched Uploads

**Commit:** `343970bf08`

**Two stacked optimizations:**

#### Patch 1: `command_copy_buffer_to_texture_layered`

New `RenderingDeviceDriver` virtual with a default per-layer fan-out implementation (preserves behavior on all backends). The WebGPU override collapses N per-layer `wgpuQueueWriteTexture` calls into one call with `extent.depthOrArrayLayers = N`.

**Key insight:** WebGPU's `queue.writeTexture` supports writing multiple array layers in a single call when they are laid out consecutively in memory at `rowsPerImage * bytesPerRow` stride. The engine's `_texture_initialize_layered` helper packs all N layers into a contiguous staging allocation matching this layout.

The WebGPU override validates the stride matches expectations and falls back to per-layer behavior if not (defensive correctness).

#### Patch 2: `API_TRAIT_TEXTURE_INITIALIZE_DIRECT_WRITE` + `texture_initialize_direct_layered`

Eliminates the transfer worker entirely for Texture2DArray uploads. The problem with Patch 1 alone: the transfer worker allocates a same-size GPU staging buffer that `wgpuQueueWriteTexture` never reads from (it uses the shadow map directly). For a 314 MB tier, this wasted allocation forces GPU queue serialization, adding ~440ms of fixed cost.

The new path: `memalloc` -> pack layers -> single `wgpuQueueWriteTexture` from CPU pointer -> `memfree`. No transfer worker, no GPU staging buffer, no command encoder, no pipeline barriers.

**Result:** `create_from_images` total: 2877ms to 240ms (-92%). Peak VRAM per upload: -300 MB.

### Push Constant IPC Elimination via firstInstance

**Commit:** `34394dce72`

**Problem:** WebGPU emulates Vulkan's push constants via a ring buffer with dynamic offsets. Each draw requires a `SetBindGroup` call to update the dynamic offset -- this is the per-draw IPC that cannot be avoided by instance batching (when meshes differ).

**Solution:** Encode the per-draw `base_index` in `drawIndexed`'s `firstInstance` parameter. The shader reads it via `@builtin(instance_index)`:

```glsl
batch_instance_index = draw_call.instance_index + uint(gl_InstanceIndex);
```

When `draw_call.instance_index` (the push constant field) is set to 0 and `firstInstance` carries the real base_index, the shader formula gives `0 + base_index = correct_index`.

**Deduplication logic:** After moving `base_index` out, compare the remaining push constant fields via `memcmp` against the previous draw's push constant. If unchanged, skip the `SetBindGroup` call entirely:

```cpp
push_constant.base_index = 0; // Move to firstInstance
bool need_pc = !pc_set_for_current_pipeline;
if (!need_pc && have_prev_fi_push_constant && prev_fi_push_constant_size == push_constant_size) {
    need_pc = memcmp(&push_constant, &prev_fi_push_constant, push_constant_size) != 0;
}
```

**Key insight:** Consecutive draws often share the same ubershader specialization (quantized light counts, feature flags). With `base_index` moved out, their push constants become identical -- memcmp succeeds and the SetBindGroup IPC is skipped.

**Result (20k instances):** Push constant writes: 11,610 to 5 per frame (-99.96%). FPS: 30.2 to 31.3 (+3.6%).

The `first_instance` parameter was plumbed through the entire rendering pipeline:
- `RenderingDevice::draw_list_draw()` gained `p_first_instance` parameter
- `RenderingDeviceGraph` instructions (`DrawListDrawInstruction`, `DrawListDrawIndexedInstruction`) gained `first_instance` field
- Graph replay passes `first_instance` to `driver->command_render_draw_indexed()`
- `API_TRAIT_FIRST_INSTANCE_INDEX` gates the optimization (WebGPU only)

### Skeleton Atlas Buffer

**Commit:** `63119b0a4f` (Optimization #1)

**Problem:** Per-skeleton GPU buffer updates (one `wgpuQueueWriteBuffer` call per skeleton per frame) generate ~4000 IPC crossings for skeletal animation scenes.

**Solution:** A single shared GPU buffer ("skeleton atlas") holds all bone data. All skeletons are allocated contiguous slots via a bump allocator. Per-frame updates:

1. Walk the dirty skeleton list, memcpy each skeleton's data into the atlas CPU mirror at its offset.
2. Track `atlas_dirty_min`/`atlas_dirty_max` across all dirty skeletons.
3. Issue a single `buffer_update_direct()` covering the dirty range.

The `buffer_write_direct()` virtual calls `wgpuQueueWriteBuffer` directly (bypassing staging buffers entirely), which is safe because skeleton data is fully updated before any draw commands.

The skeleton shader gains a `bone_offset` push constant (replaces `pad1`) that indexes into the atlas buffer:

```glsl
uvec2 bones_01 = uvec2(bones.x & 0xFFFF, bones.x >> 16) * 2 + params.bone_offset;
```

**Memory management:** The atlas uses a power-of-two growth strategy (minimum 64KB, doubling on overflow). The uniform set is rebuilt lazily when the buffer is reallocated.

**Limitation:** The current allocator is bump-only with no freeing. Skeleton deallocation does not reclaim atlas space. For long-running scenes with dynamic skeleton creation/destruction, the atlas would grow monotonically. This is acceptable for the target use case (fixed number of skeletons per scene).

**Result:** ~4000 IPC crossings per frame reduced to 1. Frame time: -17%.

### Command Buffering (Explored, Parked)

**Commit:** `4c03562baf` (not merged to release)

Records all render pass encoder commands into a flat buffer (48 bytes per command, 16,384-entry capacity = 768KB) in WASM linear memory, then replays them via a single `EM_JS` call per render pass. This reduces per-frame WASM-to-JS boundary crossings from ~8,700 to 9 (one per render pass).

The replay function is a tight JS loop reading from `HEAPU32`:
```javascript
for (var i = 0; i < count; i++) {
    switch (heap[base]) {
        case 0: pass.setPipeline(obj[heap[base + 1]]); break;
        case 1: pass.setBindGroup(...); break;
        // ... 12 command types
    }
    base += CMD_U32;
}
```

**Result:** Tested neutral on M3 Ultra desktop -- trampoline savings (~40ns/call) are offset by JS replay loop overhead (~40ns/iteration). Expected to be beneficial on mobile/Chromebook where the trampoline is 3-5x more expensive relative to pure JS.

**Decision:** Parked for potential future use. Not merged because it adds complexity without measurable benefit on current target hardware.

---

## How the Optimizations Interact and Compound

The optimizations form a layered stack where each level builds on the previous:

```
Layer 4: Command Buffering (parked)     -- reduces cost per remaining call
Layer 3: firstInstance + push constant dedup -- eliminates per-draw SetBindGroup
Layer 2: Instance Batching (shadow + color)  -- merges N draws into 1
Layer 1: Shadow Pass Merging + DP mode       -- reduces render pass count
Layer 0: Staging Buffer fixes + Direct Write -- eliminates loading/per-frame overhead
```

**Interaction patterns:**

1. **Layer 1 enables Layer 2:** Shadow pass merging puts all shadow draws into a single render pass, making consecutive same-mesh draws sort-adjacent -- a prerequisite for effective batching.

2. **Layer 2 and Layer 3 are complementary, not conflicting:** Instance batching (batch_count > 1) takes priority -- when draws CAN be merged, they are merged (one draw = one IPC). When they CANNOT be merged (different mesh/material), firstInstance dedup kicks in (skip the push constant SetBindGroup). The code explicitly gates: `batch_count == 1` is required for the firstInstance path.

3. **Layer 0 is orthogonal:** Staging buffer fixes and direct write paths address loading-time and per-frame uniform update overhead, which is independent of the draw call optimizations. They stack additively.

4. **State caching (Optimization #2) stacks with all above:** Even after batching reduces draw count, the remaining draws still bind pipelines and vertex buffers. Caching prevents redundant calls when consecutive draws share state but could not be batched (e.g., different instances with different LOD levels).

---

## The Journey from 3.25x Slower to Parity

### Timeline and Cumulative Impact

| Phase | Key Changes | Frame Time (Shadow Stress) | vs Native |
|-------|-------------|---------------------------|-----------|
| Baseline | None | 133 ms | 3.25x slower |
| +Staging fixes | Dirty tracking, re-dirty fix | ~120 ms (loading fixed) | ~2.9x |
| +Dual-paraboloid + pass merging | Force DP, merge same-FB | 76 ms | ~1.86x |
| +Shadow instance batching | Merge same-mesh shadow draws | 57 ms | ~1.39x |
| +Skeleton atlas | Single writeBuffer/frame | ~48 ms | ~1.17x |
| +firstInstance dedup | Skip push constant IPC | ~46 ms | ~1.12x |
| +Color pass batching | Merge color draws | ~36 ms | ~0.88x (parity) |

### IPC Crossings Per Frame (3D Platformer)

| Stage | Crossings/Frame | Cost at 0.3us each |
|-------|----------------|-------------------|
| Baseline | ~23,837 | 7.15 ms (23.6% of frame) |
| After batching + firstInstance (A+B) | ~8,700 | 2.6 ms (11% of frame) |
| After command buffering (A+B+C, parked) | 9 | ~0.003 ms |

### Key Insight: IPC as the Dominant Cost

The critical realization was that on WebGPU, the engine was **IPC-bound**, not GPU-bound or CPU-logic-bound. The GPU was idle waiting for commands to arrive through the serialization pipeline. Native APIs have zero per-call overhead because commands are written directly into memory-mapped command buffers. WebGPU must serialize every command into an IPC message, crossing the WASM-to-JS boundary (V8 register save/restore, value conversion, JS context setup) and then the browser-to-GPU-process boundary (Mojo IPC on Chromium).

This insight drove the entire optimization strategy: **reduce the number of IPC messages** rather than optimizing GPU utilization or CPU-side logic.

---

## Correctness Concerns

### Instance Batching Safety

1. **Transparent pass exclusion:** Correctly excludes `PASS_MODE_COLOR_TRANSPARENT` from batching. Alpha-blended draws must preserve back-to-front order for correct compositing. This is a compile-time template check with zero runtime cost.

2. **Skinned mesh exclusion:** `mesh_instance.is_valid()` check prevents batching of meshes with per-instance vertex buffers (blend shapes, skeleton-deformed vertices). The commit message notes this also fixes a latent correctness issue in the pre-existing shadow pass batching.

3. **Pipeline specialization matching:** The color pass batching verifies all fields that affect the pipeline: light counts, probe counts, decal presence, lightmap usage, projector, soft shadows. A mismatch would render with the wrong specialization constants.

4. **Multimesh exclusion:** Correctly excludes multimesh instances (which use `gl_InstanceIndex` for their own per-instance data indexing, incompatible with the batching formula).

### firstInstance Encoding Safety

1. **Pass mode exclusion:** `PASS_MODE_DEPTH_MATERIAL` is excluded (it has different push constant semantics).

2. **Pipeline change invalidation:** When a new pipeline is bound, `pc_set_for_current_pipeline` and `have_prev_fi_push_constant` are reset, forcing a fresh push constant write. This prevents stale state from a previous pipeline being assumed valid.

3. **Batch count interaction:** The firstInstance path requires `batch_count == 1` -- ensuring it does not conflict with instanced draw batching (which needs `firstInstance = 0` so `gl_InstanceIndex` indexes from 0..N-1 for batch lookup).

### Skeleton Atlas Concerns

1. **No freeing:** The bump allocator never reclaims space. In a scene that creates and destroys skeletons dynamically, the atlas grows monotonically. For fixed-skeleton scenes (the common case), this is not a problem.

2. **Single uniform set:** All compute dispatches for skeleton processing bind the same atlas uniform set. If the atlas buffer is reallocated mid-frame (due to new skeleton allocation forcing growth), the old uniform set becomes invalid. The current code rebuilds it lazily in `_skeleton_atlas_rebuild_uniform_set()`, but there could be a race if skeleton allocation happens between compute dispatches. In practice, skeletons are allocated during scene load, not mid-frame.

### Shadow Quality

Forcing dual-paraboloid shadows eliminates cubemap shadows entirely on WebGPU. While dual-paraboloid provides acceptable shadow quality for most games, it has known limitations:
- Seam artifacts at the hemisphere equator
- Slightly different sampling distribution
- May be noticeable on large-radius omni lights with high shadow resolution

This is an acceptable trade-off for the 43% frame time reduction, but should be documented for users who notice shadow quality differences between native and web builds.

### Staging Buffer Dirty Range Edge Cases

The dirty range tracking assumes `dirty_end > dirty_offset` indicates a valid range, with `0, 0` meaning "no explicit range." If code writes at offset 0 with size 0, the condition `dirty_end > dirty_offset` evaluates to `0 > 0 = false`, which correctly falls back to the full-buffer flush. However, if multiple write regions are scattered across the buffer (not contiguous), the dirty range encompasses them all -- potentially flushing unmodified data between regions. This is safe (writes unmodified data are idempotent) but suboptimal for pathological scatter patterns. In practice, per-frame dynamic buffers are written linearly within their frame slice.

---

## Remaining Optimization Opportunities

### 1. Command Buffering on Mobile

The parked command buffering optimization (`4c03562baf`) showed neutral results on M3 Ultra but is expected to show 3-5x more savings on mobile/Chromebook where the WASM-to-JS trampoline is proportionally more expensive. When mobile WebGPU becomes a primary target, re-evaluating this optimization with proper mobile benchmarking would be valuable.

### 2. Skeleton Atlas Compaction

The current bump allocator wastes space when skeletons are freed. Implementing a simple free-list or generational compaction would allow the atlas to reclaim space in dynamic skeleton scenes.

### 3. Material Sort Optimization for Batching

Instance batching requires draws to be sort-adjacent with matching state. Godot's sort key already groups by shader-material-geometry, but additional hints (e.g., preferring same-mesh grouping within a material) could increase batch hit rates for scenes with multiple mesh types sharing materials.

### 4. Vertex Buffer Deduplication

The state caching currently tracks vertex buffers per-slot. In scenes where many meshes share the same vertex format but differ in which buffer is bound, the cache miss rate is high. A more aggressive approach might deduplicate at the buffer level rather than per-draw.

### 5. Batch Size Limits

There is no upper bound on batch size. An extremely large batch (e.g., 60,000 instances in a single draw) could potentially cause GPU workgroup scheduling issues or exceed hardware limits on some mobile GPUs. Adding a configurable maximum batch size would be prudent.

### 6. Push Constant Ring Buffer Pool

The push constant ring buffer grows to accommodate peak usage, but never shrinks. For scenes that transition from high draw count (loading/cutscene) to low draw count (gameplay), the ring buffer wastes memory. A shrink-on-idle strategy similar to the staging buffer cap could help.

### 7. Texture Upload Streaming

The direct-write texture path blocks the main thread during the upload. For very large atlases, this could be split across multiple frames (progressive loading) to avoid frame spikes during scene transitions.

---

## API Trait Summary

All performance optimizations are gated behind a clean API trait system:

| Trait | Value (WebGPU) | Purpose |
|-------|---------------|---------|
| `API_TRAIT_STAGING_BUFFER_MAX_SIZE_MB` | 16 | Cap staging pool to avoid heap waste |
| `API_TRAIT_BUFFER_CREATE_MAPPED_AT_CREATION` | 1 | Use `mappedAtCreation` for initial data |
| `API_TRAIT_TEXTURE_INITIALIZE_DIRECT_WRITE` | 1 | Bypass transfer worker for texture uploads |
| `API_TRAIT_SKELETON_BUFFER_DIRECT_WRITE` | 1 | Direct queue write for skeleton atlas |
| `API_TRAIT_FORCE_OMNI_DUAL_PARABOLOID` | 1 | Force DP shadows to reduce pass count |
| `API_TRAIT_BATCH_INSTANCE_DRAWS` | 1 | Enable instanced draw batching |
| `API_TRAIT_FIRST_INSTANCE_INDEX` | 1 | Encode base_index via firstInstance |

All traits default to 0 in the base `RenderingDeviceDriver` class. Non-WebGPU backends are completely unaffected.

---

## Summary of Findings

### Strengths

1. **Clean architecture:** The API trait system provides a clean, composable extension point that keeps platform-specific optimizations isolated.

2. **Correct safety exclusions:** Transparent pass ordering, skinned mesh exclusion, and multimesh exclusion are all properly handled, preventing rendering corruption.

3. **Measured-driven approach:** Every optimization includes before/after metrics from real benchmarks, demonstrating clear causality.

4. **Layered design:** Each optimization works independently and stacks additively. They can be individually disabled via traits for debugging.

5. **Minimal code footprint:** Most changes are 5-50 lines each. The largest (Texture2DArray batching) is well-structured with clear fast-path/fallback separation.

### Concerns

1. **Skeleton atlas lacks deallocation:** Monotonic growth in dynamic skeleton scenes.

2. **Dual-paraboloid shadow quality:** Forced globally with no per-light override. Users may want high-quality shadows on specific important lights.

3. **`memcmp` for push constant dedup:** Relies on struct layout with no padding issues. If the compiler introduces padding between fields, `memcmp` could give false negatives (thinking push constants differ when they don't due to uninitialized padding bytes). The struct is `std430`-aligned and tightly packed, so this is likely fine in practice, but a field-by-field comparison would be more robust.

4. **Performance counter logging is always-on:** The `EM_ASM` performance logging runs every second unconditionally. While the overhead is negligible, it emits console output in production builds. Consider gating behind a runtime flag or build configuration.

### Recommendations

1. Add a configurable option to re-enable cubemap shadows for specific lights on WebGPU (e.g., the "main" light in a scene) for users who prioritize quality over performance.

2. Consider adding a maximum batch size cap (e.g., 4096 instances) to prevent pathological GPU scheduling on mobile hardware.

3. Gate performance counter logging behind a project setting or compile flag rather than always-on.

4. Document the shadow quality trade-off in user-facing documentation so developers understand what to expect from WebGPU builds vs native.

5. For the skeleton atlas, add basic metrics logging when the atlas grows (size, skeleton count) to help diagnose memory issues in long-running applications.

---

## File Reference

Key files implementing performance optimizations:

- `drivers/webgpu/rendering_device_driver_webgpu.cpp` -- All WebGPU-specific fast paths
- `drivers/webgpu/rendering_device_driver_webgpu.h` -- Perf counters, command buffer state
- `drivers/webgpu/webgpu_objects.h` -- `WGBuffer` dirty tracking, `WGCommandBuffer::RenderState`
- `servers/rendering/rendering_device.cpp` -- Engine-level plumbing (staging cap, direct write, firstInstance)
- `servers/rendering/rendering_device.h` -- API surface additions
- `servers/rendering/rendering_device_driver.h` -- API trait definitions, new virtuals
- `servers/rendering/rendering_device_graph.cpp` -- firstInstance in draw instructions
- `servers/rendering/rendering_device_graph.h` -- Instruction struct additions
- `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.cpp` -- Batching, firstInstance, shadow merging
- `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.h` -- Cached trait flags
- `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp` -- Skeleton atlas implementation
- `servers/rendering/renderer_rd/storage_rd/mesh_storage.h` -- Atlas data structures
- `servers/rendering/renderer_rd/storage_rd/light_storage.cpp` -- Force DP mode
- `servers/rendering/renderer_rd/shaders/skeleton.glsl` -- bone_offset push constant
- `servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl` -- batch_instance_index
