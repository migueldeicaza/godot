# Godot WebGPU Backend — Technical Reference

## 1. Driver Core: Object Model & Resource Management

### 1.1 Object Lifecycle Pattern

All GPU resources follow a uniform pattern: heap-allocate a wrapper struct, configure it, return the pointer cast to an opaque ID. The pattern mirrors Vulkan and Metal backends exactly:

```
WGFoo *foo = new WGFoo();
foo->handle = wgpuDeviceCreateFoo(device, &desc);
if (foo->handle == nullptr) { delete foo; return FooID(); }
// ... configure additional fields ...
return FooID(foo);
```

Destruction is the reverse: cast ID back to pointer, release GPU handles, delete struct.

### 1.2 Resource Types

**WGBuffer**
- GPU handle (`WGPUBuffer`)
- CPU shadow buffer (`shadow_map`) for staging writes
- Dirty range tracking (`dirty_offset`, `dirty_end`)
- Async lifecycle flags (`map_pending`, `map_complete`, `freed`)
- Dynamic buffer support (`frame_idx`, `per_frame_size`) for multi-frame uniform rotation

**WGTexture**
- GPU handle and default view
- `view_source` pointer (always the owning `WGPUTexture`, even for shared/sliced views)
- Format metadata (original vs promoted format)
- Usage flags (propagated from creation)
- `gpu_handle()` helper resolves shared/sliced views to their parent texture

**WGShader**
- Per-stage shader modules (6 slots: vertex, fragment, compute, etc.)
- Per-stage raw SPIR-V bytes (for deferred specialization constant patching)
- Pipeline layout and per-set bind group layouts
- Push constant emulation metadata (bind group index, binding, stage mask)
- Merged PC group layout (when push constants share a group with material uniforms)
- Reflection data (`bind_group_infos`) for uniform set creation
- Depth alias binding map (for Naga-split depth textures)

**WGPipelineWrapper**
- Union of render/compute pipeline handles
- For strip topologies: both Uint16 and Uint32 pipeline variants
- Owns specialized shader modules (released with pipeline)
- Stored stencil reference (applied dynamically at bind time)

**WGUniformSet**
- Bind group handle
- Temporary views (created for dimension/format fixups, released with set)
- Rebind cache (`HashMap<WGPUBindGroupLayout, WGPUBindGroup>`) for cross-shader compatibility
- Dynamic buffer tracking for offset calculation

**WGCommandBuffer**
- Command encoder (created per `command_buffer_begin`)
- Active encoder state machine (NONE / RENDER / COMPUTE)
- Render state cache (current pipeline, index buffer, vertex buffers per slot, bind groups per slot)
- Push constant data (128 bytes) and dirty flag

### 1.3 Ownership & Leak Prevention

Ownership is simple: the struct holding a `WGPUFoo` handle owns it and calls `wgpuFooRelease()` on destruction. Shared textures explicitly null their handle (don't own the underlying GPU texture).

Every resource creation function follows a delete-on-failure pattern:
- `buffer_create`: deletes WGBuffer on `wgpuDeviceCreateBuffer` failure
- `texture_create`: releases texture AND deletes struct on view creation failure
- `shader_create_from_container`: goto-based cleanup releases all partially-created modules and layouts
- `render_pipeline_create`: releases specialized modules on failure
- `uniform_set_create`: deletes WGUniformSet on `wgpuDeviceCreateBindGroup` failure

### 1.4 Async Lifetime (Freed-While-Pending Pattern)

For objects with async callbacks (buffers, fences, query pools, readback entries):

```cpp
void buffer_free(BufferID p_buffer) {
    WGBuffer *buf = ...;
    if (buf->map_pending) {
        buf->freed = true;  // Callback will handle cleanup
        return;
    }
    // Normal cleanup
    wgpuBufferRelease(buf->handle);
    memfree(buf->shadow_map);
    delete buf;
}

static void _buffer_deferred_map_cb(WGPUMapAsyncStatus status, void *userdata) {
    WGBuffer *buf = (WGBuffer *)userdata;
    buf->map_pending = false;
    if (buf->freed) {
        if (buf->handle) { wgpuBufferUnmap(buf->handle); wgpuBufferRelease(buf->handle); }
        if (buf->shadow_map) memfree(buf->shadow_map);
        delete buf;
        return;
    }
    // Normal handling
}
```

This pattern is consistently applied across all 4 async callback sites, preventing use-after-free in the single-threaded WASM environment.

---

## 2. Command Recording & Submission

### 2.1 Command Buffer Model

Each `command_buffer_begin()` creates a fresh `WGPUCommandEncoder`. The command buffer tracks which encoder type is active (render pass, compute pass, or none) and automatically ends the active encoder when switching.

### 2.2 Render Pass Lifecycle

1. `command_begin_render_pass`: Ends any active encoder, builds `WGPURenderPassDescriptor` from `WGRenderPass` metadata + `WGFramebuffer` attachment views, begins render pass encoder
2. `command_next_render_subpass`: Ends current render pass, begins new one for next subpass (WebGPU has no native subpass concept)
3. `command_end_render_pass`: Ends encoder, releases it, resets state

### 2.3 Compute Pass Lifecycle

Compute passes are lazily begun in `command_bind_compute_pipeline` (only creates the encoder when actually needed). This avoids creating empty compute passes.

### 2.4 Draw Call Encoding

- `command_render_draw`: Flushes push constants, issues `wgpuRenderPassEncoderDraw`
- `command_render_draw_indexed`: Same, plus selects strip-topology pipeline variant based on current index format
- Indirect draws: loop over `p_draw_count` (WebGPU has no multi-draw-indirect)
- `draw_indexed_indirect_count`: uses `p_max_draw_count` (no async count readback implemented)

### 2.5 Redundant State Elimination

The `WGCommandBuffer::RenderState` tracks currently-bound state to skip redundant WebGPU calls:
- Pipeline bind (skip if same `WGPipelineWrapper*`)
- Index buffer (skip if same handle + offset + format)
- Vertex buffers (per-slot tracking, 8 slots)
- Bind groups (per-slot tracking, 4 slots; invalidated on shader change)

State is reset at render pass begin and subpass transitions.

### 2.6 Encoder Splitting for Sync Isolation

When a render pass attachment has both `TextureBinding` and `RenderAttachment` usage flags, the driver:
1. Flushes push constant shadow buffer
2. Finishes and submits current command encoder
3. Creates a fresh encoder

This prevents WebGPU validation errors for intra-pass sync scope conflicts. It's conservative but safe.

### 2.7 Submission

`command_queue_execute_and_present`:
1. Flushes batched push constant data via single `wgpuQueueWriteBuffer`
2. Submits all command buffers via `wgpuQueueSubmit`
3. Registers work-done callback on fence
4. Presents swap chain (releases texture/view for browser compositing)
5. Initiates async readback for timestamp query pools

---

## 3. Bind Groups, Uniforms & Push Constants

### 3.1 Bind Group Layout Creation

BGLs are derived from two sources:
1. **SPIR-V reflection** (Godot's ShaderReflection): binding types, counts
2. **WGSL text scanning**: texture dimensions, sampler types, storage access modes, depth alias bindings, per-stage SSBO metadata

The WGSL scanning handles information lost in the SPIR-V→WGSL conversion (WebGPU's type system is stricter than Vulkan's).

**Binding numbering**: All bindings are doubled (`binding * 2`) because the combined-sampler split places sampler at `binding*2` and texture at `binding*2+1`.

### 3.2 Uniform Set Creation

`uniform_set_create` builds bind group entries with extensive fixup logic:
- **Depth/Float mismatch**: Substitutes fallback float textures for depth textures bound to float sample-type entries
- **Float32 filterability**: Substitutes fallback when float32-filterable unavailable
- **Dimension mismatch**: Creates corrected texture views (2DArray → 2D) using slice base offsets
- **MSAA mismatch**: Uses fallback multisampled texture
- **Buffer aliasing**: Redirects duplicate writable storage buffer bindings to a stub buffer (particle system workaround)
- **Dynamic buffers**: Records per-frame offset for dynamic offset calculation

### 3.3 BGL Rebinding Cache

When a uniform set created with shader A's BGL needs to be used with shader B's pipeline:
1. Check if BGLs are pointer-equal (fast path)
2. Check rebind cache
3. On miss: adapt entries (sampler type, texture sample type, view dimension), filter to target BGL bindings, create new bind group, cache it

### 3.4 Push Constant Emulation (Detailed)

```
1. command_bind_push_constants():
   → Copy data into WGCommandBuffer::push_constant_data[128]
   → Set push_constants_dirty = true

2. _flush_push_constants (before every draw/dispatch):
   → Check dirty flag (skip if clean)
   → Copy data to shadow buffer at push_constant_ring_offset
   → Track dirty range [start, end]
   → SetBindGroup(group3, pc_bind_group, dynamic_offset=ring_offset)
   → Advance ring offset by 256
   → Clear dirty flag

3. Submit time:
   → Single wgpuQueueWriteBuffer for entire dirty range of shadow buffer
   → Reset ring offset for next frame
```

**Merged PC Group**: When shader group 3 has both material uniforms AND push constants, a merged BGL contains both. The flush path preserves material dynamic offsets while patching in the PC ring offset.

---

## 4. Shader Pipeline Details

### 4.1 SPIR-V Pre-Processing (naga-converter)

Five binary-level passes before Naga parsing:

1. **freeze_spec_constant_ops**: Evaluates `OpSpecConstantOp` instructions (integer arithmetic, logical ops, bitwise, select, comparisons, type conversions). Naga doesn't support these.

2. **infer_readonly_storage**: Multi-pass analysis finds SSBOs never written to, injects `OpDecorate NonWritable`. Prevents writable-aliasing validation errors when Godot binds placeholder buffers.

3. **convert_push_constants_to_uniforms**: Rewrites push-constant variables to storage buffers at `@group(3) @binding(120)`. Changes storage class, injects decorations.

4. **split_combined_samplers**: Most complex pass (~500 lines). Splits combined image-sampler variables, doubles all binding numbers, handles function parameters and call sites, updates entry point interfaces.

5. **fix_depth2_images**: Rewrites SPIR-V `depth=2` (unknown comparison) to `depth=1`.

### 4.2 Post-Parse Module Fixups

Applied to the Naga Module before validation:
- `fix_writeonly_storage`: WGSL requires STORE-only buffers to also have LOAD
- `fix_nonfinite_literals`: Inf/NaN → f32::MAX/MIN (WGSL has no literal infinity)
- `strip_point_size`: No `@builtin(point_size)` in WGSL
- `flatten_binding_arrays`: All BindingArray sizes reduced to 1 (Chrome limitation)

### 4.3 WGSL Post-Processing

- Replace f32::MAX decimal representations with hex float `0x1.fffffep+127f`
- Strip `enable f16;` (Chrome doesn't support shader-f16)
- Prepend `diagnostic(off, derivative_uniformity);` (suppress non-uniform control flow errors)
- Prepend SSBO_USED metadata comments for per-stage BGL visibility

### 4.4 Specialization Constants

Implemented via SPIR-V binary patching:
1. Scan `OpDecorate` for SpecId → result_id mapping
2. Patch `OpSpecConstant`/`OpSpecConstantTrue`/`OpSpecConstantFalse` values in place
3. Re-convert patched SPIR-V through full pipeline (different hash = separate cache entry)
4. Specialized modules owned by WGPipelineWrapper, released with it

### 4.5 Caching

- Static process-lifetime `HashMap<uint64_t, String>`
- 64-bit key: two MurmurHash3 passes with different seeds
- ~1k entries typical, never evicted (process lifetime = page session)
- Hit/miss counters for diagnostics

---

## 5. Texture & Buffer Operations

### 5.1 Format Promotion/Demotion

**Storage format promotion** (when `StorageBinding` usage present):
- R8/RG8 → R32Float/RG32Float (or native with texture-formats-tier1)
- R16/RG16 → R32/RG32 equivalents
- Applied at texture creation + matching promotion in WGSL text

**Float32 demotion** (when `float32-filterable` unavailable):
- RGBA32Float → RGBA16Float, RG32Float → RG16Float, R32Float → R16Float
- Only for sampling-only textures (not storage/render/MSAA)
- Bidirectional conversion in upload and readback paths

**Texture swizzle emulation** (WebGPU has no swizzle):
- L8 (Luminance) → RGBA8 with luminance baked into RGB channels
- LA8 (Luminance-Alpha) → RGBA8 with (L,L,L,A) pattern

### 5.2 Buffer Operations

**Upload (non-readback)**:
- `buffer_map`: Returns CPU shadow buffer pointer, marks dirty
- `buffer_unmap`/`buffer_flush`: Flushes dirty range via `wgpuQueueWriteBuffer`
- `buffer_create_with_data`: Uses `mappedAtCreation` (zero-copy initial data)

**Readback (async)**:
- `buffer_get_data_direct`: Maintains `_readback_cache` of per-source staging buffers
- First call: create staging buffer, copy GPU→staging, initiate async map, return false
- Subsequent: return previous frame's data, initiate fresh copy
- 1-frame latency pattern

**Direct write** (bypasses staging):
- `buffer_write_direct`: Single `wgpuQueueWriteBuffer` call
- Used by skeleton atlas (single buffer covers all bone data)

### 5.3 Texture Copy Operations

- `command_copy_buffer_to_texture`: Uses `wgpuQueueWriteTexture` when source has shadow map (bypasses GPU staging entirely)
- `command_copy_buffer_to_texture_layered`: Collapses N per-layer copies into one multi-layer call (saves N-1 IPC crossings)
- `texture_initialize_direct_layered`: Direct CPU→GPU write from malloc'd buffer (no transfer worker, no staging VRAM)
- All copies respect WebGPU's 256-byte row alignment requirement

### 5.4 Texture Views & Shared Textures

- `texture_create_shared`: New WGTexture with `handle=nullptr` (doesn't own GPU texture), inherits `view_source`, creates potentially re-formatted view
- `texture_create_shared_from_slice`: Same but with mipmap/layer subset, updates `view_dimension` for downstream fixups

---

## 6. Surface & Presentation

### 6.1 Swap Chain Configuration

- Format: always `BGRA8Unorm` (browser canvas standard)
- Alpha mode: `Opaque` (with alpha strip workaround for Chrome)
- Present mode: `Fifo` (browser always vsyncs via requestAnimationFrame)
- Canvas selector: `#canvas` (hardcoded, standard for Godot web exports)

### 6.2 Frame Acquisition

1. Check if surface needs resize
2. Release previous frame's framebuffer/view/texture
3. `wgpuSurfaceGetCurrentTexture`
4. Handle status (Lost → unrecoverable, Outdated/SubOptimal → request resize)
5. Create texture view, wrap in WGFramebuffer

### 6.3 Alpha Handling (Chrome Workaround)

Chrome ignores `CompositeAlphaMode_Opaque` and composites alpha against background. The driver:
- Strips `WGPUColorWriteMask_Alpha` from ALL pipelines targeting BGRA8Unorm
- Forces `LoadOp_Clear` with alpha=1.0 for swap chain render passes
- Blit shader forces `color.a = 1.0`

---

## 7. Rendering Server Integration

### 7.1 Forward Mobile Renderer Modifications

**Shadow pass optimization**:
- Force dual-paraboloid for omni lights (2 passes vs 6+2 for cubemap)
- Pre-clear positional shadow atlas once, individual passes use LOAD
- Merge same-framebuffer shadow passes into single render pass encoder

**Instance batching** (opaque passes):
- Lookahead detects consecutive draws with identical state
- Merges into instanced draws (`drawIndexed(indexCount, batchCount, ...)`)
- Shader uses: `draw_call.instance_index + gl_InstanceIndex`
- Exclusions: transparent pass, skinned meshes, multimesh, particle instances

**firstInstance optimization**:
- Encodes instance base index in `firstInstance` draw parameter
- Compare remaining push constant fields via memcmp
- Skip SetBindGroup when unchanged (saves 1 IPC per draw)

**Subpass disabling**:
- All subpass-based post-processing disabled (WebGPU has no input attachments)
- Tonemap subpass variants disabled

### 7.2 Skeleton Atlas

All bone data consolidated into a single GPU storage buffer:
- Bump allocator assigns contiguous slots per skeleton
- Per-frame: walk dirty list, memcpy to CPU mirror, single `buffer_update_direct`
- Shader adds `bone_offset` to bone indices for atlas lookup
- Reduces N per-skeleton IPC crossings to 1 per frame

### 7.3 Canvas 2D Renderer

- Binding layout shifted (0-3 → 1-4) to reserve binding 0 for push constant ring buffer
- Instance data goes through intermediary buffer to avoid reading from write-combined memory
- `use_lcd` field explicitly initialized in all batch creation points

### 7.4 GPU Shader Compatibility Changes

| Change | Reason |
|--------|--------|
| `modf(x, y)` → `floor() + subtraction` | WGSL has no `modf` with out-param |
| `isinf(x)` → `abs(x) > 3.0e+10` | WGSL has no `isinf` |
| `isnan(x)` → `x != x` | WGSL has no `isnan` |
| Array varyings → individual vars | WGSL/Naga doesn't support array interface variables |
| switch → if/else chains | Naga SPIR-V scoping issues with phi nodes |
| `texture()` → `textureLod()` on radiance | Avoid mipmap artifacts from octahedral discontinuity |

---

## 8. Build System

### 8.1 Build Command

```bash
# WebGPU-only release template:
scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no

# Debug with both renderers:
scons platform=web target=template_debug dlink_enabled=yes webgpu=yes opengl3=yes
```

### 8.2 Build Pipeline

1. SCons reads `platform/web/detect.py` → sets `WEBGPU_ENABLED`, `RD_ENABLED`, links emdawnwebgpu
2. `modules/glslang/config.py` → enables glslang for SPIR-V compilation
3. `drivers/SCsub` → includes `drivers/webgpu/SCsub` → compiles 3 .cpp files
4. Emscripten (emcc) compiles C++ with `--use-port=emdawnwebgpu` headers
5. Emscripten links with emdawnwebgpu JS glue
6. `emscripten_helpers.py` packages: .js + .wasm + naga_wasm_bg.wasm into template zip

### 8.3 Naga Converter Build (Separate)

```bash
cd drivers/webgpu/naga-converter
wasm-pack build --target web --release
cp pkg/naga_converter_bg.wasm prebuilt/naga_wasm_bg.wasm
```

Prebuilt binary committed to repo. No Rust toolchain required for engine contributors.

### 8.4 Runtime Loading

1. `engine.js` detects `renderingDriver === 'webgpu'`
2. Parallel: `requestWebGPUDevice()` + `loadNagaSpirvToWgsl()`
3. Both complete → `Godot(moduleConfig)` called with preinitializedWebGPUDevice
4. C++ imports device, rendering begins

---

## 9. Platform/Web Integration

### 9.1 Device Request (Feature Negotiation)

The JS engine requests maximum capabilities from the adapter:
- All supported optional features (texture tiers, compression families, timestamp-query, etc.)
- Maximum limits for key resources (storage buffers, buffer size, bind groups)
- `powerPreference: 'high-performance'`
- Graceful: never requests unsupported features (gated by adapter.features.has())

### 9.2 Async Safety on WASM

WebGPU callbacks fire during `wgpuInstanceProcessEvents` which can interleave with object destruction. The freed-while-pending pattern (Section 1.4) prevents all UAF scenarios.

Additional safety:
- `ABORTING_MALLOC=0`: malloc returns NULL on OOM instead of aborting
- `ALLOW_MEMORY_GROWTH`: WASM linear memory grows on demand
- Force-signal in `fence_wait`: prevents deadlock in single-threaded environment

### 9.3 Threading

- WebGPU operates entirely on the main thread (single queue)
- Works with both threaded and non-threaded WASM builds
- Naga conversion always on main thread (JS APIs only accessible from main thread)
- No SharedArrayBuffer requirement for WebGPU itself (broader hosting compatibility)

### 9.4 WorkerThreadPool Nothreads Fix

On nothreads builds, `wait_for_group_task_completion` previously leaked Group allocations. Fixed to properly increment finished counter and free via `group_allocator.free()`.

### 9.5 Assertions Configuration

Release templates no longer accidentally enable assertions (was adding ~2MB and reducing performance). Now a multi-value option: `auto`/`no`/`yes`/`extra`.

---

## 10. Cross-Browser Compatibility

### Firefox/wgpu
| Issue | Solution |
|-------|----------|
| All bind group slots must be bound | Empty bind group pre-bound at gap indices |
| 8 storage buffers per stage (Metal limit) | Per-stage SSBO visibility via metadata comments |
| read_write storage in vertex shaders | Demote to read-only via string replacement |

### Chrome/Dawn
| Issue | Solution |
|-------|----------|
| No sized_binding_array | Flatten binding_array<T,N> → T at WGSL text level |
| CompositeAlphaMode_Opaque ignored | Strip alpha writes from BGRA8Unorm pipelines |
| shader-f16 not supported | Strip `enable f16;` directive |
| float32-blendable may be missing | Skip blend state when unavailable |

### Adreno GPUs (Android Chrome)
| Issue | Solution |
|-------|----------|
| No float32-filterable | Downgrade Float32 → Float16 for sample-only textures |
| No float32-blendable | Disable blending on float32 render targets |

### All Browsers
| Issue | Solution |
|-------|----------|
| No derivative uniformity guarantee | Prepend `diagnostic(off, derivative_uniformity)` |
| f32::MAX decimal overflow | Replace with hex float `0x1.fffffep+127f` |
| Limited storage texture formats | Promote R8/RG8/R16/RG16 to 32-bit |
| No texture component swizzle | Convert L8/LA8 to RGBA8 on CPU |
| No push constants | Ring buffer emulation |
| No combined image-samplers | SPIR-V-level split |
| No subpasses | Flatten to separate render passes |
| 256-byte row alignment | Applied in all buffer↔texture copies |

---

## 11. Diagnostics & Debugging

### Performance Counters

The driver maintains per-frame counters (reset each frame):
- Draw calls, set_bind_group calls (issued vs skipped)
- Push constant writes (issued vs skipped via firstInstance dedup)
- Render passes, bind group cache misses
- Pipeline/vertex buffer bind calls
- Gap bind group binds, first_instance draws

Logged to console every second via `EM_ASM` (always-on in current build).

### Shader Debugging

- JS monkey-patch on `device.createShaderModule` logs WGSL compilation errors
- `WEBGPU_VERBOSE` flag enables detailed WGSL dumps and bind group layout logging
- `dump_spirv_around_error`: hex dumps SPIR-V on parse failures
- SPIR-V cache hit/miss counters

### Device Lost Handler

Attached at initialization via JS `device.lost.then(...)`. Logs reason and message. No automatic recovery (page reload required).

---

## 12. Codebase Size & Statistics

155 commits on top of Godot 4.6.2-stable. All numbers below reflect the diff against that base.

### Original Code We Wrote (~20K lines, 24 new files)

24 new files added + modifications to 50 existing Godot engine files (including 15 .glsl shaders).

The implementation is concentrated: just 3 new .cpp files (the largest being
`rendering_device_driver_webgpu.cpp` at 8,783 lines) plus 5 headers. No new shader
files — instead, 15 existing engine `.glsl` shaders are patched for WGSL compatibility.

| Area | Lines Added | Lines Removed | Description |
|------|------------:|--------------:|-------------|
| drivers/webgpu (C++ driver) | 12,345 | 0 | WebGPU rendering device driver |
| naga-converter/src (Rust) | 5,146 | 0 | Custom SPIR-V → WGSL translator |
| servers/ (renderer fixes) | 1,170 | 179 | Forward Mobile renderer adaptations |
| .github/workflows (CI) | 456 | 0 | WebGPU test workflow |
| platform/web/ (web platform) | 391 | 12 | Display server, export, build integration |
| misc/ (export templates) | 325 | 0 | HTML template for WebGPU exports |
| other engine (main, core, scene) | 297 | 70 | Worker thread fix, assertions, etc. |
| naga-converter/fuzz | 110 | 0 | Fuzz targets for translator |
| **TOTAL** | **20,295** | **261** | **~20,034 net new lines** |

### Original Code by File Type

| Extension | Lines | What |
|-----------|------:|------|
| .cpp | 10,095 | WebGPU driver + renderer fixes |
| .rs | 5,214 | naga-converter custom code + fuzz targets |
| .h | 2,220 | Driver headers |
| .py | 928 | Build scripts (detect.py, SCsub, wgsl_precompile) |
| .yml | 456 | CI workflow |
| .html | 325 | Web export template |
| .js/.mjs | 436 | Engine JS glue (engine.js, config.js) |
| .glsl | 124 | Shader compatibility fixes |
| .wgsl | 144 | Prebuilt WGSL shaders |
| other | 353 | Config, .toml, .gitignore, etc. |

### Full Commit Breakdown (all lines added, including vendored)

| Extension | Total | Engine | Original | Non-Engine |
|-----------|------:|-------:|---------:|-----------:|
| .rs | 115,495 | 115,495 | 5,214 | 0 |
| .md | 22,079 | 1,450 | 409 | 20,629 |
| .cpp | 12,164 | 10,095 | 10,095 | 2,069 |
| .mjs | 8,479 | 172 | 172 | 8,307 |
| .h | 3,731 | 2,220 | 2,220 | 1,511 |
| .gd | 2,159 | 0 | 0 | 2,159 |
| .html | 1,670 | 325 | 325 | 1,345 |
| .js | 1,414 | 264 | 264 | 1,150 |
| .py | 1,364 | 928 | 928 | 436 |
| .glsl | 124 | 124 | 124 | 0 |
| .wgsl | 144 | 144 | 144 | 0 |
| .yml | 456 | 456 | 456 | 0 |
| other | ~3,700 | ~2,400 | ~1,300 | ~1,300 |
| **TOTAL** | **172,956** | **133,030** | **20,295** | **39,926** |

**Column definitions:**
- **Total** — all lines added across the 155 commits
- **Engine** — code in upstreamable paths (drivers/, platform/, servers/, etc.)
- **Original** — engine code we wrote, excluding vendored naga-patched (~112K lines of upstream Rust)
- **Non-Engine** — notes, benchmarks, demos, test harnesses (webgpu_notes/, webgpu_tests/, webgpu_site/)

### Vendored Dependencies

| Dependency | Lines | Purpose |
|------------|------:|---------|
| naga-patched (Rust) | 111,932 | Patched copy of wgpu-naga v28.0.0 for SPIR-V→WGSL |
| Cargo.lock files | 803 | Generated lockfiles |

The naga-patched crate is a vendored+patched copy of the upstream [naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga) shader translator. It is compiled to WASM separately and the prebuilt binary (`naga_wasm_bg.wasm`) is committed to the repo — no Rust toolchain needed for engine builds.
