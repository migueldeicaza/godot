# Phase 7 Tail — Systematic Re-application Plan

**Starting branch:** `webgpu_bak_phase_7_fix`

**Context:** A previous autonomous attempt to apply the Phase 7 tail (commits `8d48436801`, `942618f8a2`, `f38667134b`) got the 3D platformer to ~99.99% working but left the 2D platformer consistently glitchy while moving. Rather than try to debug the combined commit, we're going to re-apply each Phase 7 sub-task atomically onto `webgpu_bak_phase_7_fix`, rebuilding and testing both the 3D and 2D platformers after each step, so we can isolate exactly which change breaks canvas rendering.

The branch already has:

- Task 7.1 — fence signaling via GPU callback
- Task 7.4 — buffer_unmap flush
- Canvas UBO overwrite fix (2D camera follow)

---

## Phase 7 Tasks to Re-apply

### Task 7.7 — Texture bytes-per-pixel format awareness

**What it does:** Replaces hardcoded `bpp = 4` in three functions with format-aware lookups. Adds `rd_format` field to `WGTexture` to remember the Godot `DataFormat` at creation time.

- `texture_get_allocation_size()` → `get_image_format_required_size(rd_format, w, h, d, mips)` with fallback.
- `texture_get_copyable_layout()` → branches on compressed vs uncompressed: uses `get_compressed_image_format_block_dimensions` / `_block_byte_size` for BCn/ETC2/ASTC, and `get_image_format_pixel_size` otherwise. Still applies the 256-byte row alignment WebGPU requires.
- `texture_get_data()` → uses `get_image_format_pixel_size(rd_format)` with fallback to 4.

**Why it needs to be added:** Hardcoded `bpp = 4` is wrong for L8/LA8/R8/R16/BCn/etc. It causes wrong `row_pitch`, wrong staging buffer size, and wrong memory accounting, which can manifest as corrupt texture readback or validation errors on non-RGBA8 formats.

**Risk:** **LOW.** Only touches readback/reporting paths, not per-frame draw. Doesn't touch any uniform set or bind group code.

**Files:**
- `drivers/webgpu/rendering_device_driver_webgpu.cpp` — `texture_create`, `texture_get_allocation_size`, `texture_get_copyable_layout`, `texture_get_data`
- `drivers/webgpu/webgpu_objects.h` — add `rd_format` to `WGTexture`

---

### Task 7.12 — float32-filterable feature detection + filter validation hardening

**What it does:**

1. Requests `"float32-filterable"` in `wantedFeatures` in `webgpu-full-size.html`.
2. Adds `float32_filterable_supported` bool to the driver, set via `wgpuDeviceHasFeature(device, (WGPUFeatureName)13)` in `_check_capabilities()`. Logs a warning if absent.
3. Replaces `sampler_is_format_supported_for_filter()`'s "always return true" with a real switch:
   - `R/RG/RGB/RGBA_32_SFLOAT` → `float32_filterable_supported`
   - All integer formats (UINT/SINT) → `false` (never filterable in any API)
   - Others → `true`

**Why it needs to be added:** Forward Mobile's HDR post-processing samples 32F render targets with linear samplers. Without the feature flag + validation, WebGPU either silently falls back to nearest or throws a validation error. With the flag declared, Chrome enables real linear float32 filtering.

**Risk:** **LOW.** Only affects sampler creation validation for specific formats; gated entirely on the feature bit. Safe no-op for codepaths that don't hit those formats.

**Files:**
- `drivers/webgpu/rendering_device_driver_webgpu.cpp` — `_check_capabilities`, `sampler_is_format_supported_for_filter`
- `drivers/webgpu/rendering_device_driver_webgpu.h` — add flag
- `misc/dist/html/webgpu-full-size.html` — add to `wantedFeatures`

---

### Task 7.8 — Activate async GPU→CPU readback path

**What it does:** In `buffer_create()`, when the buffer is `MEMORY_ALLOCATION_TYPE_CPU` with `TRANSFER_TO_BIT` set and `TRANSFER_FROM_BIT` *not* set, marks it `is_readback = true` and ORs `WGPUBufferUsage_MapRead` into `buf->usage`. This lets `buffer_map()`'s existing `wgpuBufferMapAsync(MapMode::Read)` codepath actually be valid for those buffers.

**Why it needs to be added:** This staging-buffer-readback path is currently dead code on WebGPU. The original commit `8d48436801` activated it without adding `MapRead` usage, which caused `"BufferUsage::CopyDst does not contain BufferUsage::MapRead"` validation errors. The Phase-7-fix commit `942618f8a2` added the MapRead fix.

**Risk:** **LOW.** Only affects the narrow pattern "CPU + CopyDst + no CopySrc" staging buffers. Compute readback today goes through `buffer_get_data_direct()` which creates its own fresh buffer, so this change doesn't alter that path. The MapRead/CopySrc conflict warning noted in the code doesn't apply because these readback buffers explicitly do not have CopySrc.

**Files:**
- `drivers/webgpu/rendering_device_driver_webgpu.cpp` — `buffer_create` only

---

### Task 7.5 — Full dynamic buffer offset pipeline (HIGHEST RISK — prime 2D regression suspect)

**What it does:** Implements the Vulkan-style dynamic uniform/storage buffer offset machinery for WebGPU. Forward Mobile uses `UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC` / `STORAGE_BUFFER_DYNAMIC` bindings fed from `MultiUmaBuffer<1u>` with `frame_count = 2` rotation. Each frame, the CPU writes to a different slice of the same physical buffer, and the GPU binding uses a dynamic offset to select the slice.

Sub-parts:

1. **`WGBuffer`** gets `frame_idx` (default `UINT32_MAX` = not dynamic), `per_frame_size`, and `is_dynamic()` helper.
2. **`WGUniformSet`** gets `LocalVector<WGBuffer*> dynamic_buffers` (binding-order list).
3. **`buffer_create()`**: if `BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT`, aligns `aligned_size` **up to `device_limits.minUniformBufferOffsetAlignment` (256)** — this is the critical fix vs. the original `8d48436801` — then stores it as `per_frame_size`, then multiplies `aligned_size` by `frame_count` for the physical allocation.
4. **`buffer_persistent_map_advance()`**: rotates `frame_idx = (frame_idx + 1) % frame_count`, returns `shadow_map + frame_idx * per_frame_size`.
5. **`buffer_get_dynamic_offsets()`** (standalone path): packs frame_idx for each dynamic buffer into 2-bit shifted slots. Returns mask.
6. **`uniform_set_create()`**: when binding a dynamic buffer, sets BG entry `size = buf->per_frame_size` (not full `buf->size`) and appends `buf` to `us->dynamic_buffers`.
7. **`uniform_sets_get_dynamic_offsets()`**: walks `us->dynamic_buffers` across passed sets, packs `frame_idx & 0xF` in 4-bit shifted slots. Returns mask.
8. **Shader BGL entries**: for `UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC` and `UNIFORM_TYPE_STORAGE_BUFFER(_DYNAMIC)` — `entry.buffer.hasDynamicOffset = true`.
9. **`command_bind_render_uniform_sets()` + compute variant**: unpacks mask into `set_dyn_offsets[]` array (byte offsets = `frame_idx * per_frame_size`), passes to `wgpuRenderPassEncoderSetBindGroup(..., num_dyn_offsets, set_dyn_offsets)`. Importantly, sets with dynamic offsets are **forced to rebind every time** (bypasses redundant-bind optimization) and invalidate their cached bind group entry.

**Why it needs to be added:** Without it, Forward Mobile reads stale UBO data from previous frames, causing flicker, stale camera transforms, and "wrong one frame" rendering. On single-threaded WebGPU, the browser's event-loop submission serialization *usually* hides this, which is why the code "worked" for the 3D platformer most of the time before Phase 7. But it's still correctness-wrong and can glitch under load.

**Why it's the prime suspect for the 2D glitch:** The canvas renderer uses `MaterialUniformSet` / `CanvasShaderData` uniform binding patterns that may route through the same `UniformBufferPool` with `DYNAMIC_PERSISTENT_BIT`. If either (a) the mask packing breaks for canvas's particular binding layout, (b) the forced-rebind path interacts badly with the canvas's own per-draw push-constant bind group, or (c) canvas uniform sets don't actually use `_DYNAMIC` types and something in this change trips their static path — we'd see per-frame canvas glitching like the user describes.

**Risk:** **HIGH.** Touches every per-frame bind group in Forward Mobile and potentially the canvas path.

**Files:**
- `drivers/webgpu/rendering_device_driver_webgpu.cpp` (large diff)
- `drivers/webgpu/webgpu_objects.h`

---

## Proposed Order (lowest-risk first)

| Step | Task | Risk | Test after |
|------|------|------|------------|
| 1 | 7.7 texture bpp | LOW | 3d + 2d platformer |
| 2 | 7.12 float32-filterable | LOW | 3d + 2d platformer |
| 3 | 7.8 async readback activation | LOW | 3d + 2d platformer |
| 4 | 7.5 dynamic buffer offsets (with alignment fix) | HIGH | 3d + 2d platformer — prime debug target |

If 2D is still glitchy at step 4, we'll have isolated it to the dynamic-offset change and can dig in there specifically. Suspects to investigate first:

- Canvas render path interaction with forced rebind on dynamic sets
- Mask packing off-by-one in `dyn_shift` across sets with mixed dynamic/static bindings
- Canvas uniform sets unexpectedly using `_DYNAMIC` types
- Canvas `MaterialUniformSet` binding slot 0 vs Forward Mobile's assumption

---

## Progress Log

- **Step 1 (Task 7.7):** DONE — 3d_platformer + 2d_platformer PASS (gpuErrors=0, allErrors=0). Committed: `6d4b9397a9` (godot-webgpu), `6a5d0661d` (shiny_gen_1).
- **Step 2 (Task 7.12):** DONE — 3d_platformer + 2d_platformer PASS (gpuErrors=0, allErrors=0). No "float32-filterable NOT available" warning — feature granted by adapter. Committed: `63d0d10f4e` (godot-webgpu). No shiny_gen_1 change (only gitignored wasm updated).
- **Step 3 (Task 7.8):** DONE — 3d_platformer + 2d_platformer PASS (gpuErrors=0, allErrors=0). Committed: `c1594deb3a` (godot-webgpu). No shiny_gen_1 change (only gitignored wasm updated).
- **Step 4 (Task 7.5):** DONE — 3d_platformer + 2d_platformer PASS (gpuErrors=0, allErrors=0). Manual 2D platformer verified fixed by user after initial sprite-flicker regression.
  - Uniform/storage dynamic offsets: wired up in `command_bind_render_uniform_sets` / `command_bind_compute_uniform_sets`, with PC-merged-group offset plumbing via new `pc_group_material_dyn_offsets[]` state in `WGCommandBuffer` and `_flush_push_constants` reconstruction.
  - **Vertex buffer dynamic offsets (the missing piece that caused 2D sprite flicker):** Canvas renderer binds per-batch instance data as a VERTEX buffer via `state.instance_buffers.set_vertex_size(0, ...)`, which routes through `command_render_bind_vertex_buffers`. That function was ignoring `p_dynamic_offsets`, so the CPU rotated writes to slices 0/1/2 but the GPU always read slice 0 — stale data → displaced sprites and wrong spritesheet frames, visible only while values were changing (character moving). Fix mirrors Vulkan: unpack 2-bit `frame_idx` from the mask and offset into `frame_idx * buf->per_frame_size`. Note `per_frame_size`, not `buf->size` — WebGPU's `buf->size` is total physical allocation, unlike Vulkan where `buf_info->size` is already the per-frame size.
