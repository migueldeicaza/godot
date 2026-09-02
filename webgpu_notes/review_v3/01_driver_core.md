# WebGPU Driver Core Review (drivers/webgpu/)

## Overview

The WebGPU backend implements Godot's `RenderingDeviceDriver` interface to target the
browser's WebGPU API via emdawnwebgpu (Emscripten Dawn bindings). The implementation
comprises ~7500 lines of C++ across a single `.cpp` file, with object definitions in
`webgpu_objects.h` and the class declaration in `rendering_device_driver_webgpu.h`.

Key architectural choice: the driver runs entirely on the main thread in a WASM
environment, with all GPU operations going through the browser's WebGPU implementation.
The single-threaded model significantly constrains the design, particularly for buffer
readback (async-only mapping) and shader compilation (serialized on main thread).

---

## 1. Object Model & Resource Lifecycle

### Resource Structs

All GPU resources are wrapped in heap-allocated structs (`WGBuffer`, `WGTexture`,
`WGShader`, `WGPipelineWrapper`, `WGUniformSet`, etc.) and returned as opaque IDs.
The pattern is consistent: `new WGFoo()` -> configure -> return `FooID(ptr)`.

**WGBuffer**: Stores the `WGPUBuffer` handle, size, usage flags, and a CPU-side
`shadow_map` for staging. Notable additions for the web platform:
- `map_pending`/`map_complete`/`freed` flags for async lifecycle management
- `dirty_offset`/`dirty_end` for dirty-range tracking (avoids flushing 32MB staging buffers)
- Dynamic persistent buffer rotation (`frame_idx`, `per_frame_size`) for Task 7.5

**WGTexture**: Stores the GPU texture, a `view_source` pointer (always the owning
`WGPUTexture`), default view, and full metadata. The `gpu_handle()` helper centralizes
the common pattern of resolving shared/sliced views to their parent texture.

**WGShader**: The most complex object. Contains:
- Per-stage shader modules (6 slots)
- Per-stage raw SPIR-V bytes for deferred specialization constant patching
- Pipeline layout and per-set bind group layouts
- Push constant emulation metadata (bind group index, binding, stage mask)
- Merged PC group layout (when push constants share a group with material uniforms)
- Reflection data (`bind_group_infos`) for uniform set creation
- Depth alias binding map for NAGA-split depth textures

**WGPipelineWrapper**: Union of render/compute pipeline handles. For strip topologies,
stores both Uint16 and Uint32 variants (WebGPU bakes index format into the pipeline).
Owns any specialized shader modules created for this pipeline.

### Ownership Model

Ownership is straightforward: the struct that holds a `WGPUFoo handle` owns it and is
responsible for `wgpuFooRelease()` on destruction. Shared textures explicitly set
`handle = nullptr` to indicate they do NOT own the underlying `WGPUTexture`.

**Leak Prevention (delete-on-failure)**:
- `buffer_create`: On `wgpuDeviceCreateBuffer` failure, deletes the `WGBuffer*` and
  returns empty ID. Correct.
- `texture_create`: On view creation failure, releases the texture and deletes struct.
- `texture_create_shared`/`_from_slice`: On view failure, deletes struct.
- `shader_create_from_container`: Uses `goto cleanup` to release partially-created
  modules and layouts. This pattern works but is fragile (block scope needed for
  local variables with destructors).

**Async Lifecycle (freed-while-pending pattern)**:
The driver uses a consistent "deferred deletion" pattern for objects with async callbacks:
- `WGBuffer::freed` / `WGFence::freed` / `WGQueryPool::freed`
- `ReadbackEntry::cancelled`
When `foo_free()` is called while an async map/work-done callback is in flight, the
object is marked `freed=true` and the callback handles final cleanup. This correctly
prevents use-after-free.

### Observations

1. **No reference counting**: Objects use a simple new/delete model. This is adequate
   given the single-threaded WASM environment.
2. **Readback cache uses pointer-as-key**: `_readback_cache` keys on `(uintptr_t)buf`.
   This is safe only because the cache entry is removed in `buffer_free()` before the
   pointer can be recycled. For textures, a layer index is XOR'd into the upper 16 bits.
3. **Destructor cleanup is thorough**: The `~RenderingDeviceDriverWebGPU` destructor
   releases all global resources (ring buffer, fallback textures, dummy samplers,
   aliasing stub, readback cache entries).

---

## 2. Command Encoding & Submission

### Command Buffer Recording

`WGCommandBuffer` wraps a `WGPUCommandEncoder` (created per `command_buffer_begin()`)
plus active pass encoder state. The `ActiveEncoder` enum tracks whether we're currently
in a render pass, compute pass, or idle.

**Pass Lifecycle**:
- `command_begin_render_pass`: Ends any active encoder, builds `WGPURenderPassDescriptor`
  from the `WGRenderPass` metadata + `WGFramebuffer` attachment views, then begins a
  new render pass encoder.
- `command_end_render_pass`: Ends the render pass encoder, releases it, resets state.
- `command_next_render_subpass`: Ends current render pass, begins a new one for the
  next subpass (WebGPU has no native subpass concept).
- Compute passes are lazily begun in `command_bind_compute_pipeline`.

**Encoder Splitting for Sync Isolation**:
The driver proactively splits the command encoder when a render pass attachment has both
`TextureBinding` and `RenderAttachment` usage flags. This submits all prior work and
starts a fresh encoder, preventing WebGPU intra-pass sync scope violations. The split
includes flushing the push constant shadow buffer.

### Draw Call Encoding

- `command_render_draw`: Flushes push constants, issues `wgpuRenderPassEncoderDraw`.
- `command_render_draw_indexed`: Same, plus handles strip-topology pipeline variant
  selection based on current index format.
- Indirect draws loop over `p_draw_count` (WebGPU has no multi-draw-indirect).
- `draw_indexed_indirect_count` falls back to `p_max_draw_count` (no async count readback).

### Redundant State Elimination

Extensive tracking to skip redundant WebGPU calls:
- Pipeline bind (skip if same `WGPipelineWrapper*`)
- Index buffer bind (skip if same handle + offset + format)
- Vertex buffer bind (per-slot tracking, 8 slots max)
- Bind group bind (per-slot tracking, 4 slots max; invalidated on shader change)

### Submission

`command_queue_execute_and_present`:
1. Flushes batched push constant data to GPU via `wgpuQueueWriteBuffer`
2. Submits all command buffers via `wgpuQueueSubmit`
3. Registers a work-done callback on the fence
4. Presents swap chains (releases texture/view for browser compositing)
5. Initiates async readback for timestamp query pools

### Barrier/Sync Model

**All barrier calls are no-ops.** WebGPU provides implicit synchronization:
- Within a command encoder, passes execute sequentially with automatic barriers
- Between command encoders in the same submit, execution is ordered
- The driver relies on encoder splitting (above) for cases where Godot would emit
  Vulkan barriers between passes that read/write the same texture

---

## 3. Bind Groups, Uniforms & Push Constants

### Bind Group Layout Creation

BGLs are derived from a combination of:
1. SPIR-V reflection data (Godot's `ShaderReflection`)
2. WGSL text scanning (for information lost in SPIR-V -> WGSL conversion)

The WGSL text is scanned for:
- Texture view dimensions (`texture_2d`, `texture_cube`, etc.)
- Sampler types (`sampler` vs `sampler_comparison`)
- Storage buffer access modes (`read` vs `read_write`)
- Storage texture formats and access modes
- Multisampled textures
- Depth alias bindings (NAGA split)
- Per-stage buffer usage metadata (`//SSBO_USED:` annotations from naga-converter)

**Binding Numbering**: All bindings are doubled (`binding * 2`) because NAGA's
combined-image-sampler splitting places the sampler at `binding*2` and texture at
`binding*2+1`. For standalone types, the binding is `binding * 2`.

### Uniform Set Creation

`uniform_set_create` builds `WGPUBindGroupEntry` arrays with extensive fixup logic:
- **Depth/Float mismatch**: Substitutes fallback float textures when a depth-format
  texture is bound to a Float-sample-type BGL entry
- **Float32 filterability**: Substitutes fallback textures when float32-filterable
  is unavailable
- **Dimension mismatch**: Creates corrected texture views (e.g., 2DArray -> 2D)
  using slice base offsets
- **MSAA mismatch**: Uses fallback multisampled texture
- **Buffer aliasing**: Redirects duplicate writable storage buffer bindings to a
  stub buffer (particle system workaround)
- **Dynamic buffers**: Binds one per-frame slice, records buffer for dynamic offset
  calculation

### BGL Rebinding (`_get_compatible_bind_group`)

When a uniform set created with shader A's BGL needs to be used with shader B's
pipeline, the driver:
1. Checks if BGLs are pointer-equal (fast path)
2. Checks the rebind cache
3. On miss: adapts entries (sampler type, texture sample type, view dimension),
   filters to target BGL's bindings, creates a new bind group, caches it

### Push Constant Emulation

WebGPU has no native push constants. The driver emulates them via:

**Architecture**:
- 256KB ring buffer (`push_constant_ring_buffer`) with `ReadOnlyStorage` usage
- Universal BGL with `hasDynamicOffset=true` at binding 120
- CPU shadow buffer for batched writes (flushed once at submit time)
- 256-byte slot alignment (matching `minUniformBufferOffsetAlignment`)

**Flow**:
1. `command_bind_push_constants`: Copies data to `WGCommandBuffer::push_constant_data`,
   marks dirty
2. `_flush_push_constants` (called before every draw/dispatch):
   - Writes data to CPU shadow buffer at current ring offset
   - Updates dirty range tracking
   - Binds the PC bind group with dynamic offset = current ring offset
   - Advances ring offset
   - Handles wraparound with immediate flush
3. At submit time, the accumulated shadow buffer dirty range is flushed to GPU

**Merged PC Group**: When push constants share a descriptor set with material uniforms,
a merged BGL is created containing both material entries and the PC ring buffer entry.
The `_flush_push_constants` function preserves material dynamic offsets while updating
the PC ring offset.

---

## 4. Surface, Swapchain & Presentation

### Surface Configuration

- Swap chain format is always `BGRA8Unorm` (browser canvas standard)
- `swap_chain_resize` unconfigures and reconfigures the `WGPUSurface`
- `alphaMode = Opaque`, `presentMode = Fifo` (browser always vsyncs)
- Actual texture dimensions are queried from the surface texture (handles HiDPI mismatch)

### Frame Acquisition

`swap_chain_acquire_framebuffer`:
1. Checks if surface needs resize
2. Releases previous frame's framebuffer/view/texture
3. Calls `wgpuSurfaceGetCurrentTexture`
4. Handles status codes (Lost, Outdated, SubOptimal)
5. Creates a texture view and wraps in a `WGFramebuffer`

### Presentation

In `command_queue_execute_and_present`, after submit:
- Releases `current_view` and `current_texture` so the browser can composite
- On non-Emscripten targets, calls `wgpuSurfacePresent`

### Alpha Handling

Chrome ignores `CompositeAlphaMode_Opaque` and composites alpha=0 against background.
The driver strips alpha writes from ALL pipelines targeting `BGRA8Unorm` format and
forces `LoadOp_Clear` with alpha=1.0 for swap chain render passes.

### Device-Lost Recovery

The driver registers a `device.lost` handler via JS during initialization that logs
the reason. No automatic recovery is attempted.

---

## 5. Texture & Buffer Operations

### Format Promotion/Demotion

**Storage format promotion** (`_promote_storage_format`):
- R8/RG8 -> R32Float/RG32Float (or native with texture-formats-tier1)
- R16/RG16 -> R32/RG32 equivalents
- Applied at texture creation when `StorageBinding` usage is present
- Matching promotion applied in WGSL text (8-bit and 16-bit format name replacement)
- Pipeline creation also promotes render target formats when storage usage is present

**Float32 demotion** (no `float32-filterable`):
- RGBA32Float -> RGBA16Float, RG32Float -> RG16Float, R32Float -> R16Float
- Only for sampling-only textures (not storage/render/MSAA)
- Bidirectional conversion in `texture_upload_convert`/`texture_readback_convert`

### Texture Views and Shared Textures

- `texture_create_shared`: Creates a new `WGTexture` with `handle=nullptr`,
  inherits `view_source` from parent, creates a potentially re-formatted view
- `texture_create_shared_from_slice`: Same pattern but with mipmap/layer subset.
  Updates `base_mipmap`, `base_layer`, `view_dimension`, and mip-level dimensions.
  Critical fix: updates `view_dimension` so downstream dimension fixups work correctly.

### Buffer Mapping and Readback

**Upload (non-readback buffers)**:
- `buffer_map`: Returns CPU shadow buffer pointer, marks dirty
- `buffer_unmap`: Flushes dirty range via `wgpuQueueWriteBuffer`
- `buffer_flush`: Same as unmap but callable independently

**Readback (async)**:
- `buffer_map` for readback buffers: Processes events, checks map state, returns
  shadow buffer (may contain stale data)
- `buffer_initiate_async_map`: Called after GPU submit, starts async map for next frame
- One-frame latency pattern: data available on subsequent call

**Direct readback** (`buffer_get_data_direct`):
- Maintains a persistent `_readback_cache` of staging buffers per source
- First call: creates staging buffer, copies GPU->staging, initiates async map, returns false
- Subsequent calls: returns previous frame's data, initiates fresh copy

### Copy Operations

- `command_copy_buffer`: Special handling when source has shadow_map (flushes shadow
  to GPU first via queue write, then uses encoder copy for ordering guarantees)
- `command_copy_buffer_to_texture`: Optimized path using `wgpuQueueWriteTexture` when
  source has shadow_map (bypasses GPU staging entirely)
- `command_copy_buffer_to_texture_layered`: Collapses N per-layer copies into one
  multi-layer `wgpuQueueWriteTexture` (saves N-1 wasm-JS bridge crossings)
- `texture_initialize_direct_layered`: Direct CPU->GPU write bypassing transfer worker
  entirely (saves staging buffer VRAM allocation)

### 256-Byte Row Alignment

WebGPU requires 256-byte row alignment for buffer<->texture copies. Applied in
`texture_get_copyable_layout` and all copy operations.

---

## 6. Shader Compilation Pipeline

### SPIR-V to WGSL

1. Raw SPIR-V stored in shader container (no compression)
2. Converted to WGSL at runtime via naga (Rust->WASM, called through `EM_ASM`)
3. Process-lifetime cache keyed on 64-bit hash of SPIR-V bytes
4. Multiple text-replacement passes on WGSL output:
   - 8-bit storage format names -> 32-bit equivalents
   - 16-bit storage format names -> 32-bit equivalents  
   - `read_write` storage -> `read` for vertex/fragment stages
   - `binding_array<T, N>` -> `T` (Chrome lacks sized_binding_array)

### Specialization Constants

Implemented via SPIR-V binary patching:
1. Scan `OpDecorate` for SpecId -> result_id mapping
2. Patch `OpSpecConstant`/`OpSpecConstantTrue`/`OpSpecConstantFalse` values in place
3. Re-convert patched SPIR-V to WGSL
4. All WGSL text fixup passes re-applied

Specialized modules are owned by the `WGPipelineWrapper` and released with it.

---

## 7. Performance Optimizations

### Tracked in PerfCounters:
- Draw calls, set_bind_group calls (issued vs skipped), push constant writes (issued vs
  skipped), render passes, bind group cache misses, set_pipeline/set_vertex_buffer calls,
  gap bind group calls, first_instance draws

### Key Optimizations:
1. **Push constant batching**: CPU shadow buffer accumulates all PC writes per frame,
   single `wgpuQueueWriteBuffer` at submit time
2. **Redundant state elimination**: Pipeline, bind group, vertex buffer, index buffer
3. **Multi-layer texture upload**: Single wgpuQueueWriteTexture for N layers
4. **Direct CPU->GPU texture init**: Bypasses transfer worker staging buffers
5. **Dirty range tracking**: buffer_flush only writes modified region
6. **SPIR-V->WGSL cache**: Avoids redundant naga conversions for shared SPIR-V

---

## 8. Issues, Concerns & Recommendations

### Bugs / Potential Issues

1. **`command_resolve_texture` is unimplemented** (line 4858): Prints a warning and
   does nothing. MSAA resolve is handled via render pass resolve targets, but explicit
   resolve calls from the engine will silently fail.

2. **`command_render_clear_attachments` is unimplemented** (line 6089): Mid-pass
   clears are a no-op. This may cause visual artifacts in scenes that rely on partial
   attachment clears (e.g., split-screen rendering).

3. **`draw_indexed_indirect_count` ignores the count buffer** (line 6311): Always
   draws `p_max_draw_count` times. This wastes GPU cycles for culling-based renderers
   that use indirect count to skip invisible objects.

4. **`texture_can_make_shared_with_format` always returns true** (line 2113): No
   actual format compatibility check. Could allow invalid view creation attempts.

5. **Ring buffer wraparound submits mid-frame** (line 5487-5493): When the 256KB push
   constant ring wraps, it flushes the shadow buffer and resets the offset. This
   implicit mid-frame `wgpuQueueWriteBuffer` is safe but may cause subtle timing
   issues if the browser coalesces writes differently than expected.

6. **`shader_get_layout_hash` uses pointer as hash** (line 4067): 
   `(uint32_t)(uint64_t)(void *)(shader->pipeline_layout)` truncates to 32 bits.
   On 64-bit systems the low 32 bits of heap pointers may have poor distribution.
   This is used for pipeline cache keying and could cause spurious misses.

7. **WGSL format replacement ordering** (lines 3119-3186): The r8/rg8 replacement
   uses `String::replace()` which replaces ALL occurrences. A format name appearing
   in a comment or variable name would be incorrectly modified. The in-place char*
   scanning (lines 3149-3159) is safer but both approaches coexist.

8. **Duplicate format remapping passes**: The 16-bit format remap (lines 3148-3159)
   and the separate 16-bit->32-bit pass (lines 3167-3186) appear to do overlapping
   work. The first replaces `r16snorm`->`r16float`, then the second replaces
   `r16float`->`r32float`. While the end result is correct, this double-pass is
   confusing and could be simplified to a single pass.

### Design Strengths

1. **Thorough fallback texture system**: The driver handles every mismatch between
   Godot's Vulkan-centric texture usage and WebGPU's stricter type system. Fallback
   textures (float, cube, multisampled) prevent validation errors gracefully.

2. **Robust async lifecycle management**: The freed-while-pending pattern is
   consistently applied across buffers, fences, query pools, and readback entries.
   No use-after-free possible.

3. **Comprehensive format support**: Format promotion/demotion handles the gap between
   Vulkan's format richness and WebGPU's limited set, with bidirectional conversion
   for upload and readback.

4. **Push constant emulation is well-designed**: The ring buffer + dynamic offset
   approach minimizes bind group creation (one bind group reused for all draws)
   and batches GPU writes.

5. **BGL rebinding cache**: Gracefully handles the common case where a uniform set
   created with one shader needs to work with a compatible but different pipeline.
   The cache prevents repeated bind group creation.

6. **Encoder splitting for sync isolation**: Proactively splits command encoders
   when dual-usage textures are detected, preventing WebGPU validation errors
   that would invalidate the entire command buffer.

### Code Quality

1. **Documentation**: Extensive inline comments explain the "why" behind design
   decisions, WebGPU spec limitations, and workarounds. The comments are genuinely
   helpful for understanding the code.

2. **File structure**: The `rendering_device_driver_webgpu.cpp` implementation matches
   the Vulkan driver pattern (also a single ~6600-line `.cpp`). Clear `/* SECTION */`
   markers make it navigable. Object types and format tables are factored out into
   `webgpu_objects.h` and `pixel_formats_webgpu.h`.

3. **Diagnostic infrastructure**: The `WEBGPU_VERBOSE` conditional and JS-side
   monkey-patching provide excellent debugging capabilities without production overhead.

4. **Consistent error handling**: `ERR_FAIL_*` macros used throughout. Delete-on-failure
   pattern is consistently applied.

5. **Static helper functions**: Format detection (`_is_depth_format`, `_is_float32_format`,
   `_is_valid_storage_texture_format`) are clean and well-placed.

### Recommendations

1. **Extract shader WGSL processing**: The ~1000-line `shader_create_from_container`
   function contains multiple distinct phases (WGSL generation, text fixups, BGL
   building, pipeline layout creation). Each could be a separate method.

2. **Consolidate format remapping**: The multiple overlapping format replacement passes
   in shader creation and `_create_module_with_spec_constants` should be unified into
   a single reusable function.

3. **Implement `command_resolve_texture`**: Even a minimal render-pass-based
   implementation would prevent silent failures.

4. **Consider ring buffer sizing**: 256KB = 1024 draws at 256B/slot. Complex scenes
   with many materials could exhaust this within a frame, triggering the mid-frame
   flush path. Monitoring via perf counters would help right-size this.

5. **Fix `shader_get_layout_hash`**: Use a proper hash of the layout's configuration
   rather than truncating a pointer value.

---

## 9. Summary of Findings

### Critical Issues (0)
None found. The driver is production-functional.

### Moderate Issues (4)
- `command_resolve_texture` unimplemented (may cause MSAA issues)
- `command_render_clear_attachments` unimplemented (potential visual artifacts)
- `draw_indexed_indirect_count` ignores count buffer (wastes GPU work)
- `shader_get_layout_hash` uses truncated pointer (cache efficacy concern)

### Minor Issues (4)
- String::replace on WGSL could match non-format-name occurrences
- Duplicate/overlapping format remapping passes
- `texture_can_make_shared_with_format` lacks validation
- Ring buffer wrap triggers synchronous GPU write

### Design Strengths (6)
- Fallback texture system for type mismatches
- Async lifecycle management (freed-while-pending)
- Format promotion/demotion with bidirectional conversion
- Push constant ring buffer emulation
- BGL rebinding cache for cross-shader compatibility
- Proactive encoder splitting for sync isolation

### Architecture Note
The driver successfully bridges Godot's Vulkan-centric RDD interface to WebGPU's
significantly different API surface. The major challenges addressed are:
1. No push constants -> ring buffer emulation
2. No combined image-samplers -> SPIR-V preprocessing + binding doubling
3. Strict type matching -> fallback textures + format promotion
4. Async-only buffer mapping -> shadow buffers + deferred readback
5. No pipeline barriers -> implicit sync + encoder splitting
6. No secondary command buffers -> single encoder model
7. Limited storage texture formats -> format promotion in both textures and WGSL
8. No multi-draw-indirect -> loop emulation
9. Single queue -> semaphores as no-ops

The implementation handles these constraints with careful engineering and extensive
documentation. The code is maintainable and debuggable despite its complexity.
