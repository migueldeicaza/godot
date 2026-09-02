# Rendering Server Integration Review

## Overview

This review covers all modifications to `servers/rendering/` that integrate the WebGPU
backend with Godot's rendering server layer. The changes span 42 files with 1224 insertions
and 198 deletions across the RenderingDeviceDriver interface, RenderingDevice core,
renderer_rd subsystems, and GPU shaders.

The integration strategy is trait-driven: the WebGPU driver advertises capabilities via
API traits, and the rendering server conditionally activates code paths based on those
traits. This avoids #ifdef pollution in most places (though some WebGPU-specific paths
use `#ifdef WEB_ENABLED` or `#ifdef WEBGPU_ENABLED`).

---

## 1. Driver API Trait Modifications (rendering_device_driver.h)

### 1.1 New API Traits Added

Ten new API traits were added to the `ApiTrait` enum:

| Trait | Purpose |
|-------|---------|
| `API_TRAIT_TEXTURE_GET_DATA_VIA_DRIVER` | Routes texture readback through `driver->texture_get_data()` instead of synchronous buffer-map (needed because WebGPU mapAsync requires an event-loop tick) |
| `API_TRAIT_TEXTURE_INITIALIZE_DIRECT_WRITE` | Enables CPU-only staging path via `texture_initialize_direct_layered()`, bypassing transfer workers |
| `API_TRAIT_BUFFER_CREATE_MAPPED_AT_CREATION` | Uses `buffer_create_with_data()` instead of buffer_create + staging upload (leverages WebGPU's mappedAtCreation) |
| `API_TRAIT_STAGING_BUFFER_MAX_SIZE_MB` | Caps staging buffer pool size (WebGPU shadow memory optimization) |
| `API_TRAIT_SKELETON_BUFFER_DIRECT_WRITE` | Skeleton bone updates use direct queue writes instead of staging buffers |
| `API_TRAIT_FORCE_OMNI_DUAL_PARABOLOID` | Forces dual-paraboloid shadows for omni lights (avoids expensive cubemap 6-pass rendering) |
| `API_TRAIT_BATCH_INSTANCE_DRAWS` | Enables batching consecutive same-state draws into instanced draw calls |
| `API_TRAIT_FIRST_INSTANCE_INDEX` | Encodes instance base index in firstInstance parameter to eliminate per-draw push constant IPC |

### 1.2 New Virtual Methods on RenderingDeviceDriver

**Buffer Operations:**
- `buffer_create_with_data()` - Creates buffer with initial data in one operation (mappedAtCreation)
- `buffer_initiate_async_map()` - Start async map for download staging buffers (WebGPU)
- `buffer_write_direct()` - Direct queue write bypassing staging buffers
- `buffer_get_data_direct()` - Driver-managed readback with persistent staging buffers
- `buffer_flush()` - Flush shadow memory to GPU (already existed as empty virtual)

**Texture Operations:**
- `texture_get_gpu_pixel_size()` - Returns actual GPU pixel size when format is promoted (e.g. R8 -> R32Float)
- `texture_readback_convert()` - Converts readback data from GPU format to engine format
- `texture_upload_convert()` - Converts upload data from engine format to GPU format
- `texture_initialize_direct_layered()` - Direct CPU->GPU layered texture init without transfer workers

**Command Operations:**
- `command_copy_buffer_to_texture_layered()` - Multi-layer buffer-to-texture copy (default fans out to per-layer calls)

### 1.3 Render Pass Attachment Extension

The `Attachment` struct in `SubpassInfo` gained a `usage_flags` field:
```cpp
uint32_t usage_flags = 0;
```
This propagates the original TextureUsageBits from AttachmentFormat, enabling the WebGPU
driver to determine when a texture format needs promotion (e.g. when STORAGE_BIT is set
on formats that WebGPU requires promotion for).

### 1.4 Design Assessment

The trait-based approach is well-designed:
- All new methods have safe default implementations (return invalid/false/empty)
- Non-WebGPU drivers are completely unaffected
- The rendering server queries traits once at startup and caches the results
- No virtual dispatch overhead on hot paths (traits are checked at initialization)

**Potential concern:** The trait enum is growing significantly (10 new entries). If more
backends adopt this pattern, a capability flags bitfield might be more appropriate for
boolean traits, with integer traits kept separate.

---

## 2. RenderingDevice Core Changes (rendering_device.cpp / .h)

### 2.1 New Public API Methods

```cpp
void buffer_update_direct(RID p_buffer, uint32_t p_offset, uint32_t p_size, const void *p_data);
bool supports_buffer_direct_write();
bool force_omni_dual_paraboloid_shadows();
bool supports_batch_instance_draws();
bool supports_first_instance_index();
```

These are thin wrappers around driver trait queries, exposed to the renderer_rd layer.

### 2.2 Buffer Creation with Initial Data

All buffer creation paths (vertex, index, uniform, texture) now check
`API_TRAIT_BUFFER_CREATE_MAPPED_AT_CREATION` and route through `buffer_create_with_data()`
when the trait is active. The staging-based `_buffer_initialize()` is skipped in this case.

Affected paths:
- `vertex_buffer_create()`
- `index_buffer_create()`
- `uniform_buffer_create()`
- `texture_buffer_create()`

### 2.3 Layered Texture Initialization

A new method `_texture_initialize_layered()` coalesces N per-layer uploads into:
1. A single staging buffer allocation (or direct CPU->GPU write if `API_TRAIT_TEXTURE_INITIALIZE_DIRECT_WRITE`)
2. A single pipeline barrier
3. A single layered copy command via `command_copy_buffer_to_texture_layered()`

This applies when: `array_layers > 1 && mipmaps == 1 && texture_type == TEXTURE_TYPE_2D_ARRAY`

The direct-write path (`API_TRAIT_TEXTURE_INITIALIZE_DIRECT_WRITE`) skips transfer workers
entirely, using CPU memory + `texture_initialize_direct_layered()` which maps to WebGPU's
`wgpuQueueWriteTexture`.

### 2.4 Format Promotion Support

Throughout `_texture_initialize`, `texture_update`, `texture_get_data_async`, and the
stall-frame readback path, the code now queries `texture_get_gpu_pixel_size()` to handle
format promotion transparently:

- **Upload:** staging buffers are sized using `staging_pixel_size` (GPU format), and
  `texture_upload_convert()` transforms engine data to GPU format
- **Readback:** `texture_readback_convert()` transforms GPU data back to engine format
- **Async readback:** `GetDataRequest` stores `gpu_pixel_size` and `driver_texture_id`

### 2.5 Staging Buffer Pool Management

- Pool max_size is now capped by `API_TRAIT_STAGING_BUFFER_MAX_SIZE_MB` (WebGPU sets 16MB)
- End-of-frame: all upload staging blocks are unmapped via `buffer_unmap()` to flush
  shadow memory (no-op on Vulkan/Metal where map returns GPU-visible memory)
- Post-submit: async buffer maps are initiated for download staging buffers

### 2.6 Buffer Readback

`buffer_get_data()` now first tries `buffer_get_data_direct()` on the driver. If the
driver handles it (returns true), the staging-buffer-based path is skipped entirely.

### 2.7 Texture Readback

`texture_get_data()` routes through `driver->texture_get_data()` when
`API_TRAIT_TEXTURE_GET_DATA_VIA_DRIVER` is set. The comment notes that the first call
may return empty (async not yet resolved), and subsequent calls return cached data.

### 2.8 Draw List API Extension

```cpp
void draw_list_draw(DrawListID p_list, bool p_use_indices, uint32_t p_instances = 1,
                    uint32_t p_procedural_vertices = 0, uint32_t p_first_instance = 0);
```

The `p_first_instance` parameter propagates through to `RenderingDeviceGraph` instructions
(`DrawListDrawInstruction`, `DrawListDrawIndexedInstruction`) and ultimately to
`command_render_draw` / `command_render_draw_indexed` on the driver.

---

## 3. Rendering Pipeline Compatibility

### 3.1 Forward Mobile Renderer (Primary Target)

The WebGPU backend targets the **Forward Mobile** renderer. Key modifications:

#### Shadow Pass Optimization
- **Pre-clearing:** When `force_omni_dual_paraboloid` is active, the positional shadow
  atlas is pre-cleared once, then individual passes use LOAD instead of CLEAR. This
  enables merging same-framebuffer passes into one render pass encoder.
- **Shadow pass merging:** `_render_shadow_end()` was rewritten to batch consecutive
  shadow passes sharing the same framebuffer into a single render pass with
  viewport/scissor changes. This eliminates N-1 encoder begin/end cycles.

#### Subpass Disabling
```cpp
#ifdef WEB_ENABLED
using_subpass_post_process = false;
#endif
```
WebGPU does not support input attachments, so subpass-based post-processing is disabled.

#### Instance Batching
The `_render_list_template` function gained a batching system that merges consecutive
draws with identical state (mesh surface, material, pipeline, cull mode) into single
instanced draw calls. Criteria for batching:
- Non-multimesh, non-particle, instance_count == 1
- Same LOD level, same mesh surface, same material uniform set
- Same pipeline specialization (light counts, projector, soft shadow)
- Not transparent pass (order matters for transparency)

#### FirstInstance Optimization
When `API_TRAIT_FIRST_INSTANCE_INDEX` is active, the instance base index is encoded in
the `firstInstance` draw parameter. The push constant's `base_index` is set to 0, and
a check determines whether the push constant actually changed vs the previous draw. If
unchanged, the `draw_list_set_push_constant` call is skipped entirely — saving one IPC
crossing per draw on WebGPU.

The shader uses: `draw_call.instance_index + gl_InstanceIndex` to recover the actual
instance index (where `draw_call.instance_index` is 0 and `gl_InstanceIndex` is the
firstInstance value).

### 3.2 Forward Clustered Renderer

Minimal changes:
- Added `IS_MULTIVIEW` shader rename (`"OUTPUT_IS_MULTIVIEW"`)

This is a shared shader language feature, not a WebGPU-specific change.

### 3.3 Canvas 2D Renderer

**Binding layout shift:** Canvas texture uniforms shifted from bindings 0-3 to bindings
1-4. Binding 0 in set 3 is reserved for the WebGPU push-constant ring buffer (emulated
push constants via UBO).

**Performance fix:** Instance data now goes through an `intermediary_instance_data` buffer
to avoid reading from write-combined memory pages (huge perf impact on web).

**LCD/use_lcd field initialization:** Several batch creation points now explicitly
initialize `use_lcd = false` to avoid uninitialized data.

### 3.4 Sky Rendering

- Fixed combined_reprojection to use `sky_scene_state.cam_projection` instead of
  `p_render_data->scene_data->cam_projection` (bug fix)
- Added `uv_border_size` parameter to `_render_sky()` calls for octahedral map border
- These changes support the radiance octahedral map system used for sky reflections

### 3.5 Tone Mapping

```cpp
#ifdef WEB_ENABLED
tonemap_mobile.shader.set_variant_enabled(TONEMAP_MOBILE_MODE_SUBPASS, false);
tonemap_mobile.shader.set_variant_enabled(TONEMAP_MOBILE_MODE_SUBPASS_1D_LUT, false);
tonemap_mobile.shader.set_variant_enabled(TONEMAP_MOBILE_MODE_SUBPASS_MULTIVIEW, false);
tonemap_mobile.shader.set_variant_enabled(TONEMAP_MOBILE_MODE_SUBPASS_1D_LUT_MULTIVIEW, false);
#endif
```

All subpass-based tonemap variants are disabled for WebGPU since input attachments
are not available.

### 3.6 Debanding Fix

The debanding logic in `renderer_scene_render_rd.cpp` was corrected:
- `_render_buffers_post_process_and_tonemap`: Restructured to properly handle the
  SMAA + debanding interaction
- `_post_process_subpass`: Only enables debanding when not in HDR mode

### 3.7 Compositor (Blit to Screen)

- `draw_list_begin_for_screen` now passes `Color(0, 0, 0, 1)` as clear color (was default)
- The blit shader forces `color.a = 1.0` to prevent transparent canvas output on Chrome
  (workaround for Chrome ignoring CompositeAlphaMode_Opaque)

---

## 4. Format & Feature Negotiation

### 4.1 Texture Format Promotion

The driver can promote texture formats when the GPU lacks certain capabilities:
- R8 -> R32Float (for storage textures on WebGPU)
- The `texture_get_gpu_pixel_size()` mechanism enables transparent format promotion

The rendering server handles this by:
1. Sizing staging buffers using GPU pixel size
2. Calling `texture_upload_convert()` during upload
3. Calling `texture_readback_convert()` during readback
4. Storing the GPU pixel size in async readback requests

### 4.2 Texture Swizzle Workarounds (WEBGPU_ENABLED)

WebGPU lacks texture component swizzle. Formats that rely on swizzle are converted:
- **L8 (Luminance):** Converted to RGBA8 with luminance baked into all channels
- **LA8 (Luminance-Alpha):** Manually expanded to RGBA8 with (L,L,L,A) pattern

These conversions happen in `_validate_texture_format()` with appropriate suppression
of "format not supported" warnings.

### 4.3 Storage Texture Usage Flags

**Render target color:** On WebGPU, `TEXTURE_USAGE_STORAGE_BIT` is omitted from render
target color textures because Dawn rejects sRGB viewFormats on storage textures, causing
washed-out linear rendering. FSR (which requires storage) is not available on WebGPU
Forward Mobile anyway.

**VRS fallback texture:** `TEXTURE_USAGE_STORAGE_BIT` is only added when VRS is actually
supported, since WebGPU does not support storage for R8_UINT.

### 4.4 Staging Buffer Pool Cap

`API_TRAIT_STAGING_BUFFER_MAX_SIZE_MB` allows the driver to override the project setting
for max staging pool size. WebGPU sets this to 16MB to avoid wasting CPU heap memory
(shadow buffers are pure CPU-side on web).

---

## 5. Skeleton Atlas System

### 5.1 Architecture

A major new system: the **skeleton atlas** consolidates all skeleton bone data into a
single GPU storage buffer. This replaces per-skeleton buffers when `use_skeleton_atlas`
is true (enabled when `supports_buffer_direct_write()` returns true).

**Key components in mesh_storage:**
- `skeleton_atlas_buffer` - Single shared GPU buffer
- `skeleton_atlas_data` - CPU mirror (LocalVector<float>)
- `skeleton_atlas_used / skeleton_atlas_capacity` - Simple bump allocator
- `skeleton_atlas_uniform_set` - For compute skinning shader
- `skeleton_atlas_uniform_set_3d` - For scene draw shader (lazily created)

### 5.2 Update Path

`_update_dirty_skeletons()` now has two paths:
1. **Atlas path:** Copies all dirty skeleton data into the CPU mirror, computes the
   dirty byte range, then performs ONE `buffer_update_direct()` call
2. **Legacy path:** Individual `buffer_update()` per skeleton (original behavior)

### 5.3 Shader Integration

The skeleton compute shader (`skeleton.glsl`) adds `bone_offset` to bone indices:
```glsl
uvec2 bones_01 = uvec2(bones.x & 0xFFFF, bones.x >> 16) * 2 + params.bone_offset;
```

The push constant's `pad1` field was repurposed as `bone_offset` (vec4 offset into atlas).

### 5.4 Assessment

This is a significant memory management optimization:
- Reduces N per-skeleton GPU buffer allocations to 1
- Reduces N WASM->JS->GPU bridge crossings to 1 per frame (dirty range upload)
- The bump allocator does not handle deallocation (skeleton lifetime management may
  fragment the atlas over time in dynamic scenes)

**Concern:** No compaction/defragmentation strategy is visible. Long-running scenes
that create and destroy many skeletons may waste atlas space.

---

## 6. GPU Shader Changes

### 6.1 Forward Mobile Shader (scene_forward_mobile.glsl)

**Instance batching support:** All `draw_call.instance_index` references replaced with
`batch_instance_index`:
```glsl
layout(location = 10) flat out uint batch_instance_index;
// ...
batch_instance_index = sc_multimesh() ? draw_call.instance_index :
    (draw_call.instance_index + uint(gl_InstanceIndex));
```

This allows the shader to work with both single-draw and batched-instanced-draw paths.

**Radiance octmap modf replacement:**
```glsl
// Before: blend = modf(mip_level * MAX_ROUGHNESS_LOD, roughness_lod);
// After:
roughness_lod = floor(mip_level * MAX_ROUGHNESS_LOD);
blend = mip_level * MAX_ROUGHNESS_LOD - roughness_lod;
```
`modf` is not available in WGSL; replaced with floor + subtraction.

**Fog variable scoping:** `fog_highp` declaration moved outside `#ifndef FOG_DISABLED`
to avoid NAGA SPIR-V scoping issues.

**IS_MULTIVIEW constant:** `OUTPUT_IS_MULTIVIEW` defined based on `#ifdef USE_MULTIVIEW`.

### 6.2 Forward Mobile Include (scene_forward_mobile_inc.glsl)

`option_to_count()` rewritten from switch-statement to if/else chain to avoid SPIR-V
phi/emit scoping issues in NAGA (the WGSL transpiler).

### 6.3 SMAA Shaders

Varying arrays `offset[3]` decomposed into individual `offset0`, `offset1`, `offset2`
variables. WGSL/NAGA does not support arrays as shader interface variables.

### 6.4 Effects Shaders

- `isinf(color)` replaced with `greaterThan(abs(color), vec4(3.0e+10))`
- `isnan(color)` replaced with `notEqual(color, color)`
- `modf(x, y)` replaced with `floor()` + subtraction throughout

These are WGSL compatibility fixes (isinf/isnan/modf with out-param not in WGSL).

### 6.5 Resolve Shader

- `1e20` reduced to `1e6` for `best_depth` (likely f16 precision concern)
- MSAA resolve sample selection changed from XOR pattern to depth comparison

### 6.6 Blit Shader

Forces `color.a = 1.0` for opaque output on WebGPU (Chrome compositing workaround).

### 6.7 Shader Compiler Changes

The shader compiler now:
- Converts `texture()` calls on radiance textures to `textureLod()` with explicit LOD
  (avoids mipmap artifacts from octahedral discontinuity + NAGA gradient issues)
- Adds `IS_MULTIVIEW` as a built-in shader constant
- Shader preprocessor gains `completion_show_defines` for IDE code completion in `#ifdef`

---

## 7. Canvas Uniform Binding Layout

The canvas uniform set 3 bindings shifted:
```
Before: binding 0=color_tex, 1=normal_tex, 2=specular_tex, 3=sampler
After:  binding 1=color_tex, 2=normal_tex, 3=specular_tex, 4=sampler
```

Binding 0 is reserved for the WebGPU push-constant emulation ring buffer (UBO-based
push constant emulation since WebGPU has no native push constants).

The C++ uniform creation code mirrors this shift (uniforms created at bindings 1-4).

---

## 8. Other Rendering Server Changes

### 8.1 Rendering Server Configuration

Sky reflections roughness_layers default changed from 7 to 8. This is likely related
to the octahedral radiance map needing one more mip level for the border texels.

### 8.2 Viewport Diagnostic Stub

A no-op diagnostic block was added to `renderer_viewport.cpp` (empty braces). This
appears to be a remnant of debugging that should be cleaned up.

### 8.3 Draw Graph firstInstance Support

`RenderingDeviceGraph` instructions now carry `first_instance` and pass it through to
the driver's `command_render_draw` / `command_render_draw_indexed` calls.

---

## 9. Features Supported vs Limited vs Stubbed

### Fully Supported
- Forward Mobile 3D rendering (primary path)
- 2D Canvas rendering (with binding layout adaptation)
- Sky rendering with octahedral radiance maps
- Shadow mapping (directional + positional with dual-paraboloid)
- Skeleton/bone animation (atlas-based)
- Tone mapping (non-subpass variants)
- SMAA anti-aliasing
- Texture compression readback/upload with format promotion
- Instance batching and firstInstance optimization
- Debanding (8-bit and 10-bit modes)

### Limited / Degraded
- **Omni shadows:** Forced to dual-paraboloid (no cubemap option) - quality tradeoff
  for performance
- **Texture readback:** Async with one-frame delay (first call returns empty)
- **Storage textures:** Limited format support requiring promotion (R8 -> R32Float)
- **Render target color:** No storage bit (FSR unavailable)
- **Staging buffer pool:** Capped at 16MB (may stall more on large asset loads)

### Not Supported / Stubbed
- **Subpass/input attachments:** Disabled entirely (WebGPU limitation)
- **VRS (Variable Rate Shading):** Storage bit removed from fallback texture
- **Forward Clustered renderer:** Not targeted (only IS_MULTIVIEW added)
- **Push constants:** Emulated via UBO ring buffer (transparent to rendering server)
- **Texture component swizzle:** Emulated via data conversion (L8, LA8)

---

## 10. Compatibility Concerns

### 10.1 Skeleton Atlas Fragmentation
The bump allocator for the skeleton atlas has no deallocation or compaction. Scenes
that dynamically create/destroy many skeletons will grow the atlas monotonically. A
frame-based or generational allocator would be more robust for dynamic workloads.

### 10.2 Canvas Binding Layout Divergence
The shift from binding 0-3 to 1-4 for canvas textures creates a divergence between
WebGPU and Vulkan/Metal. Custom canvas shaders that hardcode bindings may break on
WebGPU. This should be documented for shader authors.

### 10.3 Async Readback Semantics
The `texture_get_data()` path now silently returns empty data on first call (WebGPU).
Callers must handle this gracefully. The texture_storage change does handle it
(returns null Image without error), but other callers throughout the engine should be
audited.

### 10.4 modf Replacement Precision
The `floor() + subtraction` pattern for modf replacement may have different rounding
behavior at boundary values compared to hardware modf. This is likely acceptable for
the visual uses (roughness LOD selection) but worth noting.

### 10.5 Viewport Diagnostic Stub
The empty block in `renderer_viewport.cpp` should be removed before merge.

### 10.6 firstInstance Correctness
The shader formula `draw_call.instance_index + gl_InstanceIndex` requires that
`gl_InstanceIndex` equals `firstInstance` (which is the Vulkan/WebGPU semantics). On
some older APIs, `gl_InstanceID` starts from 0 regardless of firstInstance. This
appears correct for the target APIs.

---

## 11. Summary and Recommendations

### Architecture Quality: Strong
The trait-based integration is clean and maintainable. The rendering server remains
driver-agnostic with the WebGPU driver expressing its capabilities declaratively.

### Performance Optimizations: Impressive
The IPC reduction strategy (batching, firstInstance, skeleton atlas, direct writes,
shadow pass merging) addresses the core performance challenge of WebGPU's WASM->JS
boundary systematically.

### Recommendations

1. **Skeleton atlas defragmentation:** Add a periodic compaction pass or use a pool
   allocator with free-list to prevent unbounded growth.

2. **Remove diagnostic stub:** Clean up the empty block in renderer_viewport.cpp.

3. **Document canvas binding divergence:** Shader authors need to know that binding 0
   in set 3 is reserved on WebGPU for push constant emulation.

4. **Audit texture readback callers:** Ensure all code paths that call
   `texture_get_data()` handle empty returns gracefully (not just texture_storage).

5. **Consider trait compression:** As traits grow, consider splitting into a bitfield
   for boolean traits and keeping integer traits separate, to reduce enum sprawl.

6. **Forward Clustered testing:** While not the primary target, the IS_MULTIVIEW
   addition suggests some awareness of this path. Clarify whether Forward Clustered
   is intended to work at all on WebGPU, or if it should be explicitly disabled.

7. **Subpass fallback documentation:** The disabling of subpass post-processing should
   be noted in user-facing docs (affects perceived quality of certain effects).
