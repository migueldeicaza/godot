# WebGPU Driver — Design & Method-by-Method API Mapping

> **Purpose**: Exact implementation guide for every method in `RenderingDeviceDriverWebGPU` and `RenderingContextDriverWebGPU`. Agents should use this as a lookup table when implementing each method.
>
> **Convention**: "→ no-op" means the method body should be empty or return a safe default. "→ stub" means return a dummy value but log a warning.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [RenderingContextDriverWebGPU Methods](#2-renderingcontextdriverwebgpu-methods)
3. [RenderingDeviceDriverWebGPU Methods](#3-renderingdevicedriverwebgpu-methods)
4. [Internal Object Design](#4-internal-object-design)
5. [Push Constant Emulation Design](#5-push-constant-emulation-design)
6. [Subpass Flattening Design](#6-subpass-flattening-design)
7. [Shader Translation Design](#7-shader-translation-design)

---

## 1. Architecture Overview

### Classes to Implement

```
RenderingContextDriverWebGPU : public RenderingContextDriver
  - Manages WGPUInstance, WGPUAdapter, WGPUDevice
  - Creates WGPUSurface from canvas
  - Factory for RenderingDeviceDriverWebGPU

RenderingDeviceDriverWebGPU : public RenderingDeviceDriver
  - Implements ~80 pure virtual methods
  - Manages all GPU resources via WGPUDevice
  - Records commands via WGPUCommandEncoder

RenderingShaderContainerWebGPU : public RenderingShaderContainer
  - Cross-compiles SPIR-V → WGSL
  - Handles push constant → uniform buffer rewriting
  - Stores WGSL source + reflection metadata

RenderingShaderContainerFormatWebGPU : public RenderingShaderContainerFormat
  - Factory for shader containers
  - Reports shader language/SPIRV version
```

### Key Internal Types

Identifiers use opaque `uint64_t` IDs. The standard pattern is to allocate an object on the heap, cast the pointer to `uint64_t`, and return it as the ID. To retrieve the object: cast the ID back to a pointer.

```cpp
// Pattern used by Metal and Vulkan drivers:
BufferID buffer_create(...) {
    WGBuffer *buf = new WGBuffer();
    // ... fill buf ...
    return BufferID((uint64_t)buf);
}
void buffer_free(BufferID p_buffer) {
    WGBuffer *buf = (WGBuffer *)(uint64_t)p_buffer;
    wgpuBufferRelease(buf->handle);
    delete buf;
}
```

---

## 2. RenderingContextDriverWebGPU Methods

Source interface: `servers/rendering/rendering_context_driver.h`

| Method | WebGPU Implementation | Notes |
|--------|----------------------|-------|
| `initialize()` | Call `emscripten_webgpu_get_device()` to get `WGPUDevice`. Call `wgpuDeviceGetQueue()` to get `WGPUQueue`. Store both. | Device must be pre-initialized in JS shell. See `webgpu_notes/stubs/` for JS template. |
| `device_get_count()` | Return `1` | Browser exposes a single GPU context |
| `device_get(index)` | Return a `Device` struct with name="WebGPU Device", vendor=VENDOR_UNKNOWN, type=DEVICE_TYPE_INTEGRATED_GPU | No reliable way to query GPU vendor from WebGPU in browser |
| `device_supports_present(index, surface)` | Return `true` | Single device always supports the canvas surface |
| `driver_create()` | `return memnew(RenderingDeviceDriverWebGPU(device, queue))` | Pass device and queue to the driver |
| `driver_free(driver)` | `memdelete(driver)` | |
| `surface_create(platform_data)` | Create `WGPUSurface` from HTML canvas. Use `WGPUSurfaceDescriptorFromCanvasHTMLSelector` with canvas selector `"#canvas"`. Call `wgpuInstanceCreateSurface()`. | The `platform_data` will need to carry the canvas ID. May need a struct. |
| `surface_set_size(surface, w, h)` | Store dimensions, set `needs_resize = true` | Actual resize happens when swap chain reconfigures |
| `surface_set_vsync_mode(surface, mode)` | Store mode | WebGPU in browser always syncs to requestAnimationFrame |
| `surface_get_vsync_mode(surface)` | Return stored mode | |
| `surface_get_width(surface)` | Return stored width | |
| `surface_get_height(surface)` | Return stored height | |
| `surface_set_needs_resize(surface, b)` | Store flag | |
| `surface_get_needs_resize(surface)` | Return stored flag | |
| `surface_destroy(surface)` | `wgpuSurfaceRelease(surface_handle)` | |
| `is_debug_utils_enabled()` | Return `false` | No debug utils in browser WebGPU |

---

## 3. RenderingDeviceDriverWebGPU Methods

Source interface: `servers/rendering/rendering_device_driver.h` (858 lines)

### 3.1 Initialization

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `initialize` | `Error initialize(uint32_t p_device_index, uint32_t p_frame_count)` | Store frame count. Query device limits via `wgpuDeviceGetLimits()` → `WGPUSupportedLimits`. Configure surface format (typically `WGPUTextureFormat_BGRA8Unorm`). Return OK. |

### 3.2 Buffers

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `buffer_create` | `BufferID buffer_create(uint64_t p_size, BitField<BufferUsageBits> p_usage, MemoryAllocationType p_type, uint64_t p_frames_drawn)` | **Create WGPUBuffer**: Map usage bits: `TRANSFER_FROM→CopySrc`, `TRANSFER_TO→CopyDst`, `UNIFORM→Uniform`, `STORAGE→Storage`, `INDEX→Index`, `VERTEX→Vertex`, `INDIRECT→Indirect`. Always add `CopyDst` for initial data upload. Size must be multiple of 4 (round up). Call `wgpuDeviceCreateBuffer()`. |
| `buffer_set_texel_format` | `bool buffer_set_texel_format(BufferID, DataFormat)` | → stub (return true). WebGPU has no texel buffer views. May need to emulate with storage buffers later. |
| `buffer_free` | `void buffer_free(BufferID)` | `wgpuBufferRelease(handle); delete internal_obj;` |
| `buffer_get_allocation_size` | `uint64_t buffer_get_allocation_size(BufferID)` | Return stored size (from creation). |
| `buffer_map` | `uint8_t *buffer_map(BufferID)` | **PROBLEMATIC**: WebGPU mapping is async. For buffers with `MapRead`/`MapWrite`: would need sync workaround. For others: return nullptr. Best approach: maintain a shadow CPU buffer for mapped data. |
| `buffer_unmap` | `void buffer_unmap(BufferID)` | If shadow buffer used, call `wgpuQueueWriteBuffer()` to flush. |
| `buffer_persistent_map_advance` | `uint8_t *buffer_persistent_map_advance(BufferID, uint64_t)` | → return nullptr. No persistent mapping in WebGPU. |
| `buffer_get_dynamic_offsets` | `uint64_t buffer_get_dynamic_offsets(Span<BufferID>)` | Not used for WebGPU (dynamic offsets handled differently). Return 0. |
| `buffer_flush` | `void buffer_flush(BufferID)` | No-op (or `wgpuQueueWriteBuffer` if shadow buffer dirty). |
| `buffer_get_device_address` | `uint64_t buffer_get_device_address(BufferID)` | → return 0. No device addresses in WebGPU. |

**Buffer usage mapping table**:
```
Godot BUFFER_USAGE_TRANSFER_FROM_BIT  → WGPUBufferUsage_CopySrc
Godot BUFFER_USAGE_TRANSFER_TO_BIT    → WGPUBufferUsage_CopyDst
Godot BUFFER_USAGE_TEXEL_BIT          → WGPUBufferUsage_Storage (emulation)
Godot BUFFER_USAGE_UNIFORM_BIT        → WGPUBufferUsage_Uniform
Godot BUFFER_USAGE_STORAGE_BIT        → WGPUBufferUsage_Storage
Godot BUFFER_USAGE_INDEX_BIT          → WGPUBufferUsage_Index
Godot BUFFER_USAGE_VERTEX_BIT         → WGPUBufferUsage_Vertex
Godot BUFFER_USAGE_INDIRECT_BIT       → WGPUBufferUsage_Indirect
```

### 3.3 Textures

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `texture_create` | `TextureID texture_create(const TextureFormat &p_format, const TextureView &p_view)` | Map DataFormat → WGPUTextureFormat (see pixel_formats). Map TextureType → WGPUTextureDimension. Map usage bits → WGPUTextureUsage. Handle 3-component → 4-component upgrade. For MSAA: set sampleCount. Create `WGPUTexture` + default `WGPUTextureView`. |
| `texture_create_from_extension` | `TextureID texture_create_from_extension(uint64_t, TextureType, DataFormat, uint32_t, bool, uint32_t)` | → stub (return null). Not needed for web. |
| `texture_create_shared` | `TextureID texture_create_shared(TextureID, const TextureView &)` | Create new `WGPUTextureView` from existing texture with different format mapping. Use `wgpuTextureCreateView()`. |
| `texture_create_shared_from_slice` | `TextureID texture_create_shared_from_slice(TextureID, const TextureView &, TextureSliceType, uint32_t, uint32_t, uint32_t, uint32_t)` | Create `WGPUTextureView` with specific layer range and mip range. Set `baseMipLevel`, `mipLevelCount`, `baseArrayLayer`, `arrayLayerCount`. |
| `texture_free` | `void texture_free(TextureID)` | Release all views, then `wgpuTextureRelease()`. Free internal object. |
| `texture_get_allocation_size` | `uint64_t texture_get_allocation_size(TextureID)` | Calculate: width × height × depth × layers × bpp × mipmaps. Approximate. |
| `texture_get_copyable_layout` | `void texture_get_copyable_layout(TextureID, const TextureSubresource &, TextureCopyableLayout *)` | Calculate `row_pitch` = align_up(width × bpp, 256). Calculate `size` = row_pitch × height. WebGPU requires 256-byte row alignment for buffer↔texture copies. |
| `texture_get_data` | `Vector<uint8_t> texture_get_data(TextureID, uint32_t)` | Copy texture → staging buffer (`wgpuCommandEncoderCopyTextureToBuffer`). Map staging buffer to read. This is async — may need to block with `wgpuDeviceTick()` polling. |
| `texture_get_usages_supported_by_format` | `BitField<TextureUsageBits> texture_get_usages_supported_by_format(DataFormat, bool)` | Check WebGPU format capabilities. Most formats support sampling. Depth formats support depth/stencil. Some formats don't support storage. |
| `texture_can_make_shared_with_format` | `bool texture_can_make_shared_with_format(TextureID, DataFormat, bool &)` | Check if formats are view-compatible. In WebGPU, view format must be compatible with the original (same block size, reinterpretation rules). |

**Texture usage mapping table**:
```
Godot TEXTURE_USAGE_SAMPLING_BIT              → WGPUTextureUsage_TextureBinding
Godot TEXTURE_USAGE_COLOR_ATTACHMENT_BIT      → WGPUTextureUsage_RenderAttachment
Godot TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT  → WGPUTextureUsage_RenderAttachment
Godot TEXTURE_USAGE_STORAGE_BIT              → WGPUTextureUsage_StorageBinding
Godot TEXTURE_USAGE_STORAGE_ATOMIC_BIT       → WGPUTextureUsage_StorageBinding
Godot TEXTURE_USAGE_CPU_READ_BIT             → WGPUTextureUsage_CopySrc
Godot TEXTURE_USAGE_CAN_UPDATE_BIT           → WGPUTextureUsage_CopyDst
Godot TEXTURE_USAGE_CAN_COPY_FROM_BIT        → WGPUTextureUsage_CopySrc
Godot TEXTURE_USAGE_CAN_COPY_TO_BIT          → WGPUTextureUsage_CopyDst
```

**TextureType → WGPUTextureDimension + WGPUTextureViewDimension**:
```
TEXTURE_TYPE_1D       → WGPUTextureDimension_1D, WGPUTextureViewDimension_1D
TEXTURE_TYPE_2D       → WGPUTextureDimension_2D, WGPUTextureViewDimension_2D
TEXTURE_TYPE_3D       → WGPUTextureDimension_3D, WGPUTextureViewDimension_3D
TEXTURE_TYPE_CUBE     → WGPUTextureDimension_2D, WGPUTextureViewDimension_Cube (depth=6)
TEXTURE_TYPE_2D_ARRAY → WGPUTextureDimension_2D, WGPUTextureViewDimension_2DArray
TEXTURE_TYPE_CUBE_ARRAY → WGPUTextureDimension_2D, WGPUTextureViewDimension_CubeArray
```

### 3.4 Samplers

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `sampler_create` | `SamplerID sampler_create(const SamplerState &)` | Map: `FILTER_NEAREST→WGPUFilterMode_Nearest`, `FILTER_LINEAR→WGPUFilterMode_Linear`. Map: `REPEAT_MODE_REPEAT→WGPUAddressMode_Repeat`, `REPEAT_MODE_MIRRORED_REPEAT→WGPUAddressMode_MirrorRepeat`, `REPEAT_MODE_CLAMP_TO_EDGE→WGPUAddressMode_ClampToEdge`. Map CompareOperator → WGPUCompareFunction. Set `maxAnisotropy`. Call `wgpuDeviceCreateSampler()`. |
| `sampler_free` | `void sampler_free(SamplerID)` | `wgpuSamplerRelease()` |
| `sampler_is_format_supported_for_filter` | `bool sampler_is_format_supported_for_filter(DataFormat, SamplerFilter)` | Check if format supports linear filtering. Depth formats and integer formats typically don't support linear filtering. `float32-filterable` is an optional WebGPU feature. |

**SamplerFilter mapping**:
```
SAMPLER_FILTER_NEAREST → WGPUFilterMode_Nearest
SAMPLER_FILTER_LINEAR  → WGPUFilterMode_Linear
```

**SamplerRepeatMode mapping**:
```
SAMPLER_REPEAT_MODE_REPEAT          → WGPUAddressMode_Repeat
SAMPLER_REPEAT_MODE_MIRRORED_REPEAT → WGPUAddressMode_MirrorRepeat
SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE   → WGPUAddressMode_ClampToEdge
SAMPLER_REPEAT_MODE_CLAMP_TO_BORDER → WGPUAddressMode_ClampToEdge (no border in WebGPU)
SAMPLER_REPEAT_MODE_MIRROR_CLAMP_TO_EDGE → WGPUAddressMode_MirrorRepeat (approximate)
```

**CompareOperator mapping**:
```
COMPARE_OP_NEVER            → WGPUCompareFunction_Never
COMPARE_OP_LESS             → WGPUCompareFunction_Less
COMPARE_OP_EQUAL            → WGPUCompareFunction_Equal
COMPARE_OP_LESS_OR_EQUAL    → WGPUCompareFunction_LessEqual
COMPARE_OP_GREATER          → WGPUCompareFunction_Greater
COMPARE_OP_NOT_EQUAL        → WGPUCompareFunction_NotEqual
COMPARE_OP_GREATER_OR_EQUAL → WGPUCompareFunction_GreaterEqual
COMPARE_OP_ALWAYS           → WGPUCompareFunction_Always
```

### 3.5 Vertex Formats

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `vertex_format_create` | `VertexFormatID vertex_format_create(Span<VertexAttribute>, const VertexAttributeBindingsMap &)` | Store vertex attribute descriptions. Map Godot formats to `WGPUVertexFormat`: e.g., `DATA_FORMAT_R32G32B32_SFLOAT → WGPUVertexFormat_Float32x3`. Build `WGPUVertexBufferLayout` array with strides and step modes (`WGPUVertexStepMode_Vertex` or `WGPUVertexStepMode_Instance`). Store for pipeline creation. |
| `vertex_format_free` | `void vertex_format_free(VertexFormatID)` | Free stored layout description. |

### 3.6 Barriers (ALL NO-OPS)

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `command_pipeline_barrier` | `void command_pipeline_barrier(CommandBufferID, BitField<PipelineStageBits>, BitField<PipelineStageBits>, VectorView<MemoryAccessBarrier>, VectorView<BufferBarrier>, VectorView<TextureBarrier>)` | **→ no-op**. WebGPU handles synchronization automatically. |

### 3.7 Fences & Semaphores

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `fence_create` | `FenceID fence_create()` | Allocate a `WGFence` with a submission counter and completion flag. Return its ID. |
| `fence_wait` | `Error fence_wait(FenceID)` | Poll with `wgpuDeviceTick()` until the tracked submission is complete. In Emscripten, this may need `emscripten_sleep()` or just check a callback flag. Consider: use `wgpuQueueOnSubmittedWorkDone()` to set a flag, then check it. |
| `fence_free` | `void fence_free(FenceID)` | Free the `WGFence` object. |
| `semaphore_create` | `SemaphoreID semaphore_create()` | Allocate minimal object. Return ID. WebGPU is single-queue — semaphores are mostly no-ops. |
| `semaphore_free` | `void semaphore_free(SemaphoreID)` | Free object. |

### 3.8 Command Buffers

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `command_queue_family_get` | `CommandQueueFamilyID command_queue_family_get(BitField<CommandQueueFamilyBits>, SurfaceID)` | Return a single family ID (e.g., `CommandQueueFamilyID(1)`). WebGPU has one queue that supports everything. |
| `command_queue_create` | `CommandQueueID command_queue_create(CommandQueueFamilyID, bool)` | Wrap the existing `WGPUQueue`. Return its ID. Single queue. |
| `command_queue_execute_and_present` | `Error command_queue_execute_and_present(CommandQueueID, VectorView<SemaphoreID>, VectorView<CommandBufferID>, VectorView<SemaphoreID>, FenceID, VectorView<SwapChainID>)` | For each command buffer: get the `WGPUCommandBuffer` (already finished). Call `wgpuQueueSubmit(queue, count, cmdBuffers)`. If fence specified: call `wgpuQueueOnSubmittedWorkDone()` to set fence completion flag. For each swap chain: call `wgpuSurfacePresent()`. Ignore semaphores (single queue). |
| `command_queue_free` | `void command_queue_free(CommandQueueID)` | Free wrapper object. Don't release queue (owned by device). |
| `command_pool_create` | `CommandPoolID command_pool_create(CommandQueueFamilyID, CommandBufferType)` | Allocate pool object. Store buffer type. Return ID. No WebGPU equivalent — purely bookkeeping. |
| `command_pool_reset` | `bool command_pool_reset(CommandPoolID)` | Mark all command buffers in pool as available. Return true. |
| `command_pool_free` | `void command_pool_free(CommandPoolID)` | Free pool and all contained command buffers. |
| `command_buffer_create` | `CommandBufferID command_buffer_create(CommandPoolID)` | Allocate `WGCommandBuffer` with space for push constant data, state tracking. Return ID. |
| `command_buffer_begin` | `bool command_buffer_begin(CommandBufferID)` | Create `WGPUCommandEncoder` via `wgpuDeviceCreateCommandEncoder()`. Store in command buffer. Reset state. Return true. |
| `command_buffer_begin_secondary` | `bool command_buffer_begin_secondary(CommandBufferID, RenderPassID, uint32_t, FramebufferID)` | → Same as `command_buffer_begin()`. WebGPU has no secondary command buffers. |
| `command_buffer_end` | `void command_buffer_end(CommandBufferID)` | Call `wgpuCommandEncoderFinish()` → store resulting `WGPUCommandBuffer`. |
| `command_buffer_execute_secondary` | `void command_buffer_execute_secondary(CommandBufferID, VectorView<CommandBufferID>)` | → no-op. No secondary command buffers in WebGPU. |

### 3.9 Swap Chain

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `swap_chain_create` | `SwapChainID swap_chain_create(SurfaceID)` | Allocate `WGSwapChain` storing surface reference. Don't configure yet (done in resize). |
| `swap_chain_resize` | `Error swap_chain_resize(CommandQueueID, SwapChainID, uint32_t)` | Call `wgpuSurfaceConfigure()` with: `device`, format=`BGRA8Unorm`, usage=`RenderAttachment`, alphaMode=`Opaque`, width/height from surface. Create internal render pass for the swap chain. |
| `swap_chain_acquire_framebuffer` | `FramebufferID swap_chain_acquire_framebuffer(CommandQueueID, SwapChainID, bool &r_resize)` | Call `wgpuSurfaceGetCurrentTexture()`. If status != Success: set `r_resize=true`, return null. Create `WGPUTextureView` from surface texture. Build framebuffer wrapper. Return it. |
| `swap_chain_get_render_pass` | `RenderPassID swap_chain_get_render_pass(SwapChainID)` | Return the render pass created during `swap_chain_resize()`. |
| `swap_chain_get_format` | `DataFormat swap_chain_get_format(SwapChainID)` | Return `DATA_FORMAT_B8G8R8A8_UNORM` (or the configured format). |
| `swap_chain_free` | `void swap_chain_free(SwapChainID)` | `wgpuSurfaceUnconfigure()`. Free internal objects. |

### 3.10 Framebuffers

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `framebuffer_create` | `FramebufferID framebuffer_create(RenderPassID, VectorView<TextureID>, uint32_t, uint32_t)` | Store render pass reference, texture IDs (attachments), and dimensions. No WebGPU object to create — framebuffers are implicit in WebGPU (render pass descriptors directly reference texture views). |
| `framebuffer_free` | `void framebuffer_free(FramebufferID)` | Free bookkeeping object. Don't release texture views (owned by textures). |

### 3.11 Shaders

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `shader_create_from_container` | `ShaderID shader_create_from_container(const Ref<RenderingShaderContainer> &, const Vector<ImmutableSampler> &)` | Extract WGSL from container. Call `wgpuDeviceCreateShaderModule()` with `WGPUShaderModuleWGSLDescriptor`. Build `WGPUBindGroupLayout` array from reflection data. Build `WGPUPipelineLayout`. Store shader metadata including push constant binding info. |
| `shader_get_layout_hash` | `uint32_t shader_get_layout_hash(ShaderID)` | Return hash of bind group layouts. |
| `shader_free` | `void shader_free(ShaderID)` | Release shader module, bind group layouts, pipeline layout. |
| `shader_destroy_modules` | `void shader_destroy_modules(ShaderID)` | Release `WGPUShaderModule` only (keep layouts for pipeline use). |

### 3.12 Uniform Sets (Bind Groups)

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `uniform_set_create` | `UniformSetID uniform_set_create(VectorView<BoundUniform>, ShaderID, uint32_t, int)` | For each `BoundUniform`, create a `WGPUBindGroupEntry`. Map uniform types: `UNIFORM_TYPE_SAMPLER → sampler binding`, `UNIFORM_TYPE_TEXTURE → textureView binding`, `UNIFORM_TYPE_SAMPLER_WITH_TEXTURE → sampler + textureView (two entries)`, `UNIFORM_TYPE_UNIFORM_BUFFER → buffer binding with offset+size`, `UNIFORM_TYPE_STORAGE_BUFFER → buffer binding`, `UNIFORM_TYPE_IMAGE → storage texture view`, `UNIFORM_TYPE_INPUT_ATTACHMENT → texture view`. Get bind group layout from shader at set_index. Call `wgpuDeviceCreateBindGroup()`. |
| `uniform_set_free` | `void uniform_set_free(UniformSetID)` | `wgpuBindGroupRelease()`, free object. |
| `uniform_sets_get_dynamic_offsets` | `uint32_t uniform_sets_get_dynamic_offsets(VectorView<UniformSetID>, ShaderID, uint32_t, uint32_t)` | Return dynamic offsets for dynamic uniform/storage buffers. These are passed to `wgpuRenderPassEncoderSetBindGroup()` as the `dynamicOffsets` parameter. |
| `command_uniform_set_prepare_for_use` | `void command_uniform_set_prepare_for_use(CommandBufferID, UniformSetID, ShaderID, uint32_t)` | → no-op. WebGPU doesn't need explicit preparation. |

### 3.13 Transfer Commands

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `command_clear_buffer` | `void command_clear_buffer(CommandBufferID, BufferID, uint64_t, uint64_t)` | `wgpuCommandEncoderClearBuffer(encoder, buffer, offset, size)`. Size must be multiple of 4. |
| `command_copy_buffer` | `void command_copy_buffer(CommandBufferID, BufferID src, BufferID dst, VectorView<BufferCopyRegion>)` | For each region: `wgpuCommandEncoderCopyBufferToBuffer(encoder, src, srcOff, dst, dstOff, size)`. Size must be multiple of 4. |
| `command_copy_texture` | `void command_copy_texture(CommandBufferID, TextureID src, TextureLayout, TextureID dst, TextureLayout, VectorView<TextureCopyRegion>)` | For each region: `wgpuCommandEncoderCopyTextureToTexture(encoder, &srcCopy, &dstCopy, &extent)`. Ignore layouts (WebGPU handles transitions). |
| `command_resolve_texture` | `void command_resolve_texture(CommandBufferID, TextureID src, TextureLayout, uint32_t layer, uint32_t mip, TextureID dst, TextureLayout, uint32_t, uint32_t)` | Create a minimal render pass with the MSAA texture as color attachment and the resolve target as resolveTarget. Begin and immediately end the pass. |
| `command_clear_color_texture` | `void command_clear_color_texture(CommandBufferID, TextureID, TextureLayout, const Color &, const TextureSubresourceRange &)` | Create a render pass with loadOp=Clear and the clear color. Begin and immediately end it. Iterate over layers/mips in the subresource range. |
| `command_clear_depth_stencil_texture` | `void command_clear_depth_stencil_texture(CommandBufferID, TextureID, TextureLayout, float depth, uint8_t stencil, const TextureSubresourceRange &)` | Same as above but with depth/stencil attachment and depthClearValue/stencilClearValue. |
| `command_copy_buffer_to_texture` | `void command_copy_buffer_to_texture(CommandBufferID, BufferID, TextureID, TextureLayout, VectorView<BufferTextureCopyRegion>)` | For each region: `wgpuCommandEncoderCopyBufferToTexture()`. Set `bytesPerRow` = align_up(row_pitch, 256). Set `rowsPerImage`. |
| `command_copy_texture_to_buffer` | `void command_copy_texture_to_buffer(CommandBufferID, TextureID, TextureLayout, BufferID, VectorView<BufferTextureCopyRegion>)` | For each region: `wgpuCommandEncoderCopyTextureToBuffer()`. Same alignment rules. |

### 3.14 Pipelines & Push Constants

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `pipeline_free` | `void pipeline_free(PipelineID)` | `wgpuRenderPipelineRelease()` or `wgpuComputePipelineRelease()`. Free object. |
| `command_bind_push_constants` | `void command_bind_push_constants(CommandBufferID, ShaderID, uint32_t first_index, VectorView<uint32_t> data)` | **See Section 5 (Push Constant Emulation)**. Copy data into command buffer's push constant staging area. Mark dirty. |
| `pipeline_cache_create` | `bool pipeline_cache_create(const Vector<uint8_t> &)` | → return true (no-op). WebGPU has no explicit pipeline cache. |
| `pipeline_cache_free` | `void pipeline_cache_free()` | → no-op |
| `pipeline_cache_query_size` | `size_t pipeline_cache_query_size()` | → return 0 |
| `pipeline_cache_serialize` | `Vector<uint8_t> pipeline_cache_serialize()` | → return empty vector |

### 3.15 Render Passes

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `render_pass_create` | `RenderPassID render_pass_create(VectorView<Attachment>, VectorView<Subpass>, VectorView<SubpassDependency>, uint32_t view_count, AttachmentReference fdm_attachment)` | Store attachments, subpasses, and dependencies in a `WGRenderPass` object. **See Section 6 (Subpass Flattening)**. Dependencies are no-ops (automatic sync). |
| `render_pass_free` | `void render_pass_free(RenderPassID)` | Free `WGRenderPass` object. |

### 3.16 Render Commands

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `command_begin_render_pass` | `void command_begin_render_pass(CommandBufferID, RenderPassID, FramebufferID, CommandBufferType, const Rect2i &, VectorView<RenderPassClearValue>)` | Build `WGPURenderPassDescriptor` from first subpass's color/depth references + framebuffer textures. Set load/store ops from attachment descriptions. Set clear values. Call `wgpuCommandEncoderBeginRenderPass()`. Store the `WGPURenderPassEncoder`. |
| `command_end_render_pass` | `void command_end_render_pass(CommandBufferID)` | `wgpuRenderPassEncoderEnd(encoder)`. Release encoder. |
| `command_next_render_subpass` | `void command_next_render_subpass(CommandBufferID, CommandBufferType)` | **End current pass**. Increment subpass index. Build new `WGPURenderPassDescriptor` for next subpass. Begin new render pass encoder. Reset all render state (dirty flags). |
| `command_render_set_viewport` | `void command_render_set_viewport(CommandBufferID, VectorView<Rect2i>)` | `wgpuRenderPassEncoderSetViewport(encoder, x, y, w, h, 0.0, 1.0)`. Only first viewport used (WebGPU supports one viewport). |
| `command_render_set_scissor` | `void command_render_set_scissor(CommandBufferID, VectorView<Rect2i>)` | `wgpuRenderPassEncoderSetScissorRect(encoder, x, y, w, h)`. Only first scissor. |
| `command_render_clear_attachments` | `void command_render_clear_attachments(CommandBufferID, VectorView<AttachmentClear>, VectorView<Rect2i>)` | WebGPU has no mid-pass clear. Options: (a) end pass, do a clear pass, restart; or (b) draw a full-screen quad with the clear color. Option (a) is simpler. |
| `command_bind_render_pipeline` | `void command_bind_render_pipeline(CommandBufferID, PipelineID)` | `wgpuRenderPassEncoderSetPipeline(encoder, pipeline)`. Store current pipeline in state. |
| `command_bind_render_uniform_sets` | `void command_bind_render_uniform_sets(CommandBufferID, VectorView<UniformSetID>, ShaderID, uint32_t first, uint32_t count, uint32_t dynamic_offsets)` | For set_index in [first, first+count): `wgpuRenderPassEncoderSetBindGroup(encoder, set_index, bindGroup, dynamicOffsetCount, dynamicOffsets)`. |
| `command_render_draw` | `void command_render_draw(CommandBufferID, uint32_t vertex_count, uint32_t instance_count, uint32_t base_vertex, uint32_t first_instance)` | Flush push constants (see Section 5). `wgpuRenderPassEncoderDraw(encoder, vertexCount, instanceCount, firstVertex, firstInstance)`. |
| `command_render_draw_indexed` | `void command_render_draw_indexed(CommandBufferID, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)` | Flush push constants. `wgpuRenderPassEncoderDrawIndexed(encoder, indexCount, instanceCount, firstIndex, baseVertex, firstInstance)`. |
| `command_render_draw_indexed_indirect` | `void command_render_draw_indexed_indirect(CommandBufferID, BufferID, uint64_t offset, uint32_t draw_count, uint32_t stride)` | Flush push constants. For each draw: `wgpuRenderPassEncoderDrawIndexedIndirect(encoder, buffer, offset + i*stride)`. WebGPU has no multi-draw-indirect — must loop. |
| `command_render_draw_indexed_indirect_count` | `...` | Same as above but read count from buffer. May need to copy count buffer to CPU first. |
| `command_render_draw_indirect` | `void command_render_draw_indirect(CommandBufferID, BufferID, uint64_t, uint32_t, uint32_t)` | Flush push constants. Loop: `wgpuRenderPassEncoderDrawIndirect(encoder, buffer, offset + i*stride)`. |
| `command_render_draw_indirect_count` | `...` | Same with count from buffer. |
| `command_render_bind_vertex_buffers` | `void command_render_bind_vertex_buffers(CommandBufferID, uint32_t count, const BufferID *, const uint64_t *, uint64_t)` | For each buffer: `wgpuRenderPassEncoderSetVertexBuffer(encoder, slot, buffer, offset, WGPU_WHOLE_SIZE)`. |
| `command_render_bind_index_buffer` | `void command_render_bind_index_buffer(CommandBufferID, BufferID, IndexBufferFormat, uint64_t)` | Map format: `INDEX_BUFFER_FORMAT_UINT16 → WGPUIndexFormat_Uint16`, `INDEX_BUFFER_FORMAT_UINT32 → WGPUIndexFormat_Uint32`. `wgpuRenderPassEncoderSetIndexBuffer(encoder, buffer, format, offset, WGPU_WHOLE_SIZE)`. |
| `command_render_set_blend_constants` | `void command_render_set_blend_constants(CommandBufferID, const Color &)` | `wgpuRenderPassEncoderSetBlendConstant(encoder, &color)`. |
| `command_render_set_line_width` | `void command_render_set_line_width(CommandBufferID, float)` | → no-op. WebGPU doesn't support line width > 1.0. |

### 3.17 Render Pipeline Creation

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `render_pipeline_create` | `PipelineID render_pipeline_create(ShaderID, VertexFormatID, RenderPrimitive, PipelineRasterizationState, PipelineMultisampleState, PipelineDepthStencilState, PipelineColorBlendState, VectorView<int32_t>, BitField<PipelineDynamicStateFlags>, RenderPassID, uint32_t, VectorView<PipelineSpecializationConstant>)` | Build `WGPURenderPipelineDescriptor`. See mapping tables below. Call `wgpuDeviceCreateRenderPipeline()`. |

**RenderPrimitive mapping**:
```
RENDER_PRIMITIVE_POINTS         → WGPUPrimitiveTopology_PointList
RENDER_PRIMITIVE_LINES          → WGPUPrimitiveTopology_LineList
RENDER_PRIMITIVE_LINE_STRIPS    → WGPUPrimitiveTopology_LineStrip
RENDER_PRIMITIVE_TRIANGLES      → WGPUPrimitiveTopology_TriangleList
RENDER_PRIMITIVE_TRIANGLE_STRIPS → WGPUPrimitiveTopology_TriangleStrip
RENDER_PRIMITIVE_LINES_WITH_ADJACENCY → WGPUPrimitiveTopology_LineList (no adjacency in WebGPU)
RENDER_PRIMITIVE_TRIANGLES_WITH_ADJACENCY → WGPUPrimitiveTopology_TriangleList (no adjacency)
```

**CullMode mapping**:
```
POLYGON_CULL_DISABLED → WGPUCullMode_None
POLYGON_CULL_FRONT    → WGPUCullMode_Front
POLYGON_CULL_BACK     → WGPUCullMode_Back
```

**BlendFactor mapping**:
```
BLEND_FACTOR_ZERO                → WGPUBlendFactor_Zero
BLEND_FACTOR_ONE                 → WGPUBlendFactor_One
BLEND_FACTOR_SRC_COLOR           → WGPUBlendFactor_Src
BLEND_FACTOR_ONE_MINUS_SRC_COLOR → WGPUBlendFactor_OneMinusSrc
BLEND_FACTOR_DST_COLOR           → WGPUBlendFactor_Dst
BLEND_FACTOR_ONE_MINUS_DST_COLOR → WGPUBlendFactor_OneMinusDst
BLEND_FACTOR_SRC_ALPHA           → WGPUBlendFactor_SrcAlpha
BLEND_FACTOR_ONE_MINUS_SRC_ALPHA → WGPUBlendFactor_OneMinusSrcAlpha
BLEND_FACTOR_DST_ALPHA           → WGPUBlendFactor_DstAlpha
BLEND_FACTOR_ONE_MINUS_DST_ALPHA → WGPUBlendFactor_OneMinusDstAlpha
BLEND_FACTOR_CONSTANT_COLOR      → WGPUBlendFactor_Constant
BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR → WGPUBlendFactor_OneMinusConstant
BLEND_FACTOR_SRC_ALPHA_SATURATE  → WGPUBlendFactor_SrcAlphaSaturated
BLEND_FACTOR_SRC1_COLOR          → WGPUBlendFactor_Src1 (if dual-source blending supported)
BLEND_FACTOR_ONE_MINUS_SRC1_COLOR → WGPUBlendFactor_OneMinusSrc1
BLEND_FACTOR_SRC1_ALPHA          → WGPUBlendFactor_Src1Alpha
BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA → WGPUBlendFactor_OneMinusSrc1Alpha
```

**BlendOperation mapping**:
```
BLEND_OP_ADD              → WGPUBlendOperation_Add
BLEND_OP_SUBTRACT         → WGPUBlendOperation_Subtract
BLEND_OP_REVERSE_SUBTRACT → WGPUBlendOperation_ReverseSubtract
BLEND_OP_MINIMUM          → WGPUBlendOperation_Min
BLEND_OP_MAXIMUM          → WGPUBlendOperation_Max
```

**StencilOperation mapping**:
```
STENCIL_OP_KEEP                → WGPUStencilOperation_Keep
STENCIL_OP_ZERO                → WGPUStencilOperation_Zero
STENCIL_OP_REPLACE             → WGPUStencilOperation_Replace
STENCIL_OP_INCREMENT_AND_CLAMP → WGPUStencilOperation_IncrementClamp
STENCIL_OP_DECREMENT_AND_CLAMP → WGPUStencilOperation_DecrementClamp
STENCIL_OP_INVERT              → WGPUStencilOperation_Invert
STENCIL_OP_INCREMENT_AND_WRAP  → WGPUStencilOperation_IncrementWrap
STENCIL_OP_DECREMENT_AND_WRAP  → WGPUStencilOperation_DecrementWrap
```

### 3.18 Compute

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `command_bind_compute_pipeline` | `void command_bind_compute_pipeline(CommandBufferID, PipelineID)` | `wgpuComputePassEncoderSetPipeline(encoder, pipeline)` |
| `command_bind_compute_uniform_sets` | `void command_bind_compute_uniform_sets(CommandBufferID, VectorView<UniformSetID>, ShaderID, uint32_t, uint32_t, uint32_t)` | For each set: `wgpuComputePassEncoderSetBindGroup(encoder, setIndex, bindGroup, dynOffsets...)` |
| `command_compute_dispatch` | `void command_compute_dispatch(CommandBufferID, uint32_t x, uint32_t y, uint32_t z)` | Flush push constants. `wgpuComputePassEncoderDispatchWorkgroups(encoder, x, y, z)` |
| `command_compute_dispatch_indirect` | `void command_compute_dispatch_indirect(CommandBufferID, BufferID, uint64_t)` | Flush push constants. `wgpuComputePassEncoderDispatchWorkgroupsIndirect(encoder, buffer, offset)` |
| `compute_pipeline_create` | `PipelineID compute_pipeline_create(ShaderID, VectorView<PipelineSpecializationConstant>)` | Build `WGPUComputePipelineDescriptor` with shader module and entry point `"main"`. Map specialization constants → `WGPUConstantEntry` array. Call `wgpuDeviceCreateComputePipeline()`. |

**Note on compute passes**: Unlike render passes, compute passes must be explicitly created and ended. When the command buffer encounters compute commands:
1. If inside a render pass → end the render pass first
2. Begin compute pass: `wgpuCommandEncoderBeginComputePass(encoder, &desc)`
3. Bind pipeline, bind groups, dispatch
4. End compute pass: `wgpuComputePassEncoderEnd(encoder)`

The tricky part: Godot may interleave compute and render commands within a command buffer. The driver must track whether it's in a render or compute pass and switch between them by ending one and beginning the other.

### 3.19 Queries

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `timestamp_query_pool_create` | `QueryPoolID timestamp_query_pool_create(uint32_t count)` | If `timestamp-query` feature available: `wgpuDeviceCreateQuerySet()` with type=Timestamp. Else: return null ID. |
| `timestamp_query_pool_free` | `void timestamp_query_pool_free(QueryPoolID)` | `wgpuQuerySetRelease()` if valid. |
| `timestamp_query_pool_get_results` | `void timestamp_query_pool_get_results(QueryPoolID, uint32_t count, uint64_t *results)` | Resolve query set → buffer (`wgpuCommandEncoderResolveQuerySet()`), map buffer, copy results. |
| `timestamp_query_result_to_time` | `uint64_t timestamp_query_result_to_time(uint64_t)` | Return value in nanoseconds. WebGPU timestamps are already in nanoseconds. |
| `command_timestamp_query_pool_reset` | `void command_timestamp_query_pool_reset(CommandBufferID, QueryPoolID, uint32_t)` | → no-op (WebGPU query sets don't need reset). |
| `command_timestamp_write` | `void command_timestamp_write(CommandBufferID, QueryPoolID, uint32_t index)` | `wgpuCommandEncoderWriteTimestamp(encoder, querySet, index)` if available. |

### 3.20 Labels & Debug

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `command_begin_label` | `void command_begin_label(CommandBufferID, const char *, const Color &)` | `wgpuCommandEncoderPushDebugGroup(encoder, label)`. Or on render/compute pass encoder if active. |
| `command_end_label` | `void command_end_label(CommandBufferID)` | `wgpuCommandEncoderPopDebugGroup(encoder)`. |
| `command_insert_breadcrumb` | `void command_insert_breadcrumb(CommandBufferID, uint32_t)` | `wgpuCommandEncoderInsertDebugMarker(encoder, marker_string)`. |

### 3.21 Submission

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `begin_segment` | `void begin_segment(uint32_t frame_index, uint32_t frames_drawn)` | Store frame index. Any per-frame reset logic (ring buffer advancement). |
| `end_segment` | `void end_segment()` | Cleanup any per-frame temporaries. |

### 3.22 Capabilities & Misc

| Method | Signature | WebGPU Implementation |
|--------|-----------|----------------------|
| `set_object_name` | `void set_object_name(ObjectType, ID, const String &)` | `wgpuBufferSetLabel()` / `wgpuTextureSetLabel()` etc. based on object type. |
| `get_resource_native_handle` | `uint64_t get_resource_native_handle(DriverResource, ID)` | Return the `WGPUBuffer`/`WGPUTexture` handle cast to uint64_t. |
| `get_total_memory_used` | `uint64_t get_total_memory_used()` | Track internally (sum of buffer + texture allocations). |
| `get_lazily_memory_used` | `uint64_t get_lazily_memory_used()` | Return 0 (no lazy allocation in WebGPU). |
| `limit_get` | `uint64_t limit_get(Limit)` | Query from `WGPUSupportedLimits`. Full mapping in RESEARCH.md Appendix C. |
| `api_trait_get` | `uint64_t api_trait_get(ApiTrait)` | `HONORS_PIPELINE_BARRIERS → 0 (false)`, `SHADER_CHANGE_INVALIDATION → ALL_BOUND_UNIFORM_SETS`, `TEXTURE_TRANSFER_ALIGNMENT → 256`, `TEXTURE_DATA_ROW_PITCH_STEP → 256`, `SECONDARY_VIEWPORT_SCISSOR → 0 (false)`, `CLEARS_WITH_COPY_ENGINE → 0 (false)`, `USE_GENERAL_IN_COPY_QUEUES → 0`, `BUFFERS_REQUIRE_TRANSITIONS → 0 (false)`, `TEXTURE_OUTPUTS_REQUIRE_CLEARS → 0 (false)`. |
| `has_feature` | `bool has_feature(Features)` | Check WebGPU device features. Most features: false. |
| `get_multiview_capabilities` | `const MultiviewCapabilities &get_multiview_capabilities()` | Return default (is_supported=false). WebGPU has no multiview. |
| `get_fragment_shading_rate_capabilities` | `...` | Return default (all false/zero). Not in WebGPU. |
| `get_fragment_density_map_capabilities` | `...` | Return default. Not in WebGPU. |
| `get_api_name` | `String get_api_name() const` | Return `"WebGPU"`. |
| `get_api_version` | `String get_api_version() const` | Return `"1.0"`. |
| `get_pipeline_cache_uuid` | `String get_pipeline_cache_uuid() const` | Return empty or generated UUID. |
| `get_capabilities` | `const Capabilities &get_capabilities() const` | Return `{DEVICE_UNKNOWN, 1, 0}` or define `DEVICE_WEBGPU` family. |
| `get_shader_container_format` | `const RenderingShaderContainerFormat &get_shader_container_format() const` | Return the `RenderingShaderContainerFormatWebGPU` instance. |

---

## 4. Internal Object Design

File: `drivers/webgpu/webgpu_objects.h`

```cpp
// Wrapper for WebGPU buffer
struct WGBuffer {
    WGPUBuffer handle = nullptr;
    uint64_t size = 0;
    WGPUBufferUsageFlags usage = 0;
    uint8_t *shadow_map = nullptr;  // For CPU-side mapping emulation
    bool map_dirty = false;
};

// Wrapper for WebGPU texture
struct WGTexture {
    WGPUTexture handle = nullptr;
    WGPUTextureView default_view = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUTextureDimension dimension = WGPUTextureDimension_2D;
    WGPUTextureViewDimension view_dimension = WGPUTextureViewDimension_2D;
    uint32_t width = 0, height = 0, depth = 0;
    uint32_t mipmaps = 1;
    uint32_t layers = 1;
    uint32_t sample_count = 1;
    WGPUTextureUsageFlags usage = 0;
    bool is_from_swap_chain = false;
};

// Wrapper for shader
struct WGShader {
    WGPUShaderModule module = nullptr;
    WGPUPipelineLayout pipeline_layout = nullptr;
    LocalVector<WGPUBindGroupLayout> bind_group_layouts;

    // Push constant emulation
    uint32_t push_constant_size = 0;
    uint32_t push_constant_bind_group = 0;  // Which bind group index
    uint32_t push_constant_binding = 0;     // Which binding within that group
    BitField<RDD::ShaderStage> push_constant_stages;

    // Reflection data for uniform set creation
    struct BindGroupLayoutInfo {
        struct Entry {
            WGPUBindGroupLayoutEntry layout_entry;
            RDD::UniformType godot_type;
        };
        LocalVector<Entry> entries;
    };
    LocalVector<BindGroupLayoutInfo> bind_group_infos;

    String name;
};

// Wrapper for render pass metadata
struct WGRenderPass {
    struct SubpassInfo {
        LocalVector<RDD::AttachmentReference> input_references;
        LocalVector<RDD::AttachmentReference> color_references;
        RDD::AttachmentReference depth_stencil_reference;
        LocalVector<RDD::AttachmentReference> resolve_references;
    };
    LocalVector<RDD::Attachment> attachments;
    LocalVector<SubpassInfo> subpasses;
    uint32_t view_count = 0;
};

// Wrapper for framebuffer
struct WGFramebuffer {
    LocalVector<WGTexture *> attachments;
    LocalVector<WGPUTextureView> attachment_views;
    uint32_t width = 0, height = 0;
    WGRenderPass *render_pass = nullptr;
};

// Wrapper for pipeline
struct WGPipelineRender {
    WGPURenderPipeline handle = nullptr;
    WGShader *shader = nullptr;
};
struct WGPipelineCompute {
    WGPUComputePipeline handle = nullptr;
    WGShader *shader = nullptr;
};
struct WGPipelineWrapper {
    enum Type { RENDER, COMPUTE };
    Type type = RENDER;
    union {
        WGPURenderPipeline render_handle;
        WGPUComputePipeline compute_handle;
    };
    WGShader *shader = nullptr;
};

// Wrapper for uniform set (bind group)
struct WGUniformSet {
    WGPUBindGroup handle = nullptr;
    uint32_t set_index = 0;
};

// Wrapper for swap chain
struct WGSwapChain {
    WGPUSurface surface = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;
    uint32_t width = 0, height = 0;
    WGRenderPass *render_pass = nullptr;  // Created during resize
    bool configured = false;
};

// Wrapper for command buffer
struct WGCommandBuffer {
    WGPUCommandEncoder encoder = nullptr;
    WGPUCommandBuffer finished_buffer = nullptr;

    // Current active encoder states
    WGPURenderPassEncoder render_encoder = nullptr;
    WGPUComputePassEncoder compute_encoder = nullptr;

    enum ActiveEncoder { NONE, RENDER, COMPUTE };
    ActiveEncoder active_encoder = NONE;

    // Push constant emulation state
    static constexpr uint32_t MAX_PUSH_CONSTANT_SIZE = 128;
    uint8_t push_constant_data[MAX_PUSH_CONSTANT_SIZE] = {};
    uint32_t push_constant_data_len = 0;
    bool push_constants_dirty = false;

    // Render state tracking
    struct RenderState {
        WGRenderPass *render_pass = nullptr;
        WGFramebuffer *framebuffer = nullptr;
        uint32_t current_subpass = 0;
        WGPipelineWrapper *current_pipeline = nullptr;
    } render_state;

    // Helpers
    void end_active_encoder() {
        if (render_encoder) {
            wgpuRenderPassEncoderEnd(render_encoder);
            render_encoder = nullptr;
        }
        if (compute_encoder) {
            wgpuComputePassEncoderEnd(compute_encoder);
            compute_encoder = nullptr;
        }
        active_encoder = NONE;
    }
};

// Wrapper for fence
struct WGFence {
    bool signaled = false;
    uint64_t submission_id = 0;
};

// Minimal wrapper for semaphores (no-op on WebGPU)
struct WGSemaphore {};

// Wrapper for query pool
struct WGQueryPool {
    WGPUQuerySet handle = nullptr;
    WGPUBuffer resolve_buffer = nullptr;
    uint32_t count = 0;
    bool available = false;  // Is timestamp-query feature available?
};
```

---

## 5. Push Constant Emulation Design

### Problem
WebGPU has no push constants. Godot uses push constants in 67/100 shaders, with up to 96 bytes of data that changes per draw call.

### Design (based on Metal driver pattern)

**Shader side**: The SPIR-V → WGSL translator rewrites:
```glsl
layout(push_constant, std430) uniform PushConstants {
    uint instance_index;
    uint uv_offset;
    ...
} push_constants;
```
To WGSL:
```wgsl
@group(3) @binding(0) var<uniform> push_constants : PushConstants;
```

**Convention**: Push constants always use **bind group 3, binding 0**. This is safe because Godot shaders use sets 0-3 and set 3 is only used for material uniforms (binding 0). We can either:
- (A) Move material uniforms to a different binding in set 3 and use binding 0 for push constants
- (B) Use a separate bind group at index 3 and remap material uniforms

**Option A is simpler**. Or even simpler: use **the last used bind group + 1** for push constants. Since most shaders use 0-2 sets, push constants go in group 3. The shader container records which group/binding is used.

**Driver side**:

```
Per-frame ring buffer:
┌──────────────────────────────────────────────────────────────┐
│ PC data 0 (256B) │ PC data 1 (256B) │ PC data 2 (256B) │...│
└──────────────────────────────────────────────────────────────┘
                   ↑ current offset

- Ring buffer: 64KB WGPUBuffer with Uniform | CopyDst usage
- Each push constant update writes to next 256-byte aligned slot
- Bind group created with dynamic offset at slot 0
- Before draw: wgpuRenderPassEncoderSetBindGroup(encoder, pcGroup, pcBindGroup, 1, &dynamicOffset)
```

**Flow**:
1. `command_bind_push_constants()`: Copy data into `WGCommandBuffer::push_constant_data`. Mark `push_constants_dirty = true`.
2. Before each draw/dispatch: If `push_constants_dirty`:
   a. Write push constant data to next ring buffer slot via `wgpuQueueWriteBuffer(queue, pcBuffer, ringOffset, data, size)`
   b. Set bind group with dynamic offset: `wgpuRenderPassEncoderSetBindGroup(encoder, pcGroupIndex, pcBindGroup, 1, &ringOffset)`
   c. Advance ring offset by 256 (alignment requirement)
   d. Clear dirty flag
3. Per-frame: Reset ring offset to 0 at `begin_segment()`.

**Ring buffer sizing**: 64KB / 256 bytes per slot = 256 draw calls per frame before wrapping. If exceeded, allocate a second buffer or increase size. 256 draw calls is likely sufficient for most 2D scenes. For complex 3D scenes, may need 1024+ slots (256KB buffer).

---

## 6. Subpass Flattening Design

### Problem
WebGPU has no subpasses within render passes. Vulkan render passes can have multiple subpasses with input attachments.

### Design (based on Metal driver pattern)

**Storage**: `WGRenderPass` stores a vector of `SubpassInfo`, each with its own set of color/depth/input/resolve references.

**Execution**:
1. `command_begin_render_pass()`: Begin encoder for subpass 0.
   - Color attachments from subpass 0's `color_references`
   - Depth attachment from subpass 0's `depth_stencil_reference`
   - Load ops from the `Attachment` description
   - Resolve targets from subpass 0's `resolve_references`

2. `command_next_render_subpass()`:
   - End current render pass encoder
   - Increment subpass index
   - Build new render pass descriptor:
     - Color attachments from new subpass's `color_references`
     - For `input_references`: The referenced attachments become texture views that need to be bound as textures (they were written to in the previous subpass)
     - Load op for previously-written attachments = `Load` (preserve previous content)
     - Store op for intermediate attachments = `Store` (needed for next subpass)
   - Begin new render pass encoder
   - All render state is invalidated (pipeline, bind groups, vertex buffers, etc.)

3. `command_end_render_pass()`: End current encoder.

**Input attachment handling**: When a subpass reads from a previous subpass's output (via `input_references`), the WebGPU driver must:
- Ensure the attachment was stored (not discarded) in the previous subpass
- Create a `WGPUTextureView` for the attachment if not already created
- The shader must sample from the texture instead of using `subpassInput` (WGSL has no `subpassInput`)
- This means the shader compilation must rewrite `subpassInput` + `subpassLoad()` to `texture2D` + `textureSample()`

---

## 7. Shader Translation Design

### Pipeline: GLSL → SPIR-V → WGSL

```
Build/Export time:
  GLSL source → [glslang] → SPIR-V bytecode → [SPIRV-Reflect] → ReflectionData
                                              → [Tint] → WGSL source

Runtime (browser):
  WGSL source → wgpuDeviceCreateShaderModule() → WGPUShaderModule
```

### RenderingShaderContainerWebGPU._set_code_from_spirv()

```
Input: ReflectShader with SPIR-V bytecode per stage + reflection data

Processing:
1. For each shader stage (vertex, fragment, compute):
   a. Take SPIR-V bytecode
   b. Run through Tint to produce WGSL
   c. During translation, configure:
      - Push constant block → uniform buffer at group(N) binding(M)
      - Descriptor set indices → bind group indices (1:1 mapping)
      - Specialization constants → override constants
      - subpassInput → texture sampling (if applicable)
   d. Validate WGSL output
   e. Store WGSL string

2. Store metadata:
   - Push constant bind group index + binding index + size
   - Per-bind-group layout info (for WGPUBindGroupLayout creation)
   - Entry point names (typically "main" for both vertex and fragment)

Output: Container with WGSL source + metadata
```

### GLSL → WGSL Gotchas

| GLSL Feature | WGSL Equivalent | Notes |
|--------------|----------------|-------|
| `gl_VertexIndex` | `@builtin(vertex_index)` | |
| `gl_InstanceIndex` | `@builtin(instance_index)` | |
| `gl_FragCoord` | `@builtin(position)` | In fragment shader |
| `gl_Position` | `@builtin(position)` | In vertex output |
| `gl_FrontFacing` | `@builtin(front_facing)` | |
| `gl_FragDepth` | `@builtin(frag_depth)` | |
| `gl_GlobalInvocationID` | `@builtin(global_invocation_id)` | Compute |
| `gl_LocalInvocationID` | `@builtin(local_invocation_id)` | Compute |
| `gl_WorkGroupID` | `@builtin(workgroup_id)` | Compute |
| `gl_NumWorkGroups` | `@builtin(num_workgroups)` | Compute |
| `layout(push_constant)` | `var<uniform>` at group/binding | See Section 5 |
| `layout(set=N, binding=M)` | `@group(N) @binding(M)` | Direct mapping |
| `layout(constant_id=N)` | `@id(N) override` | Specialization constants → pipeline-overridable constants |
| `bool` in uniform blocks | `u32` | WGSL doesn't allow `bool` in uniform/storage |
| `vec3` in uniform blocks | Check alignment | WGSL `vec3<f32>` has 16-byte alignment in uniform buffers |
| `mat3` in uniform blocks | Use `mat3x4` or `array<vec3<f32>, 3>` | std140 vs WGSL layout differences |
| `sampler2D` (combined) | Separate `texture_2d` + `sampler` | Already separated in Godot |
| `texture()` | `textureSample()` | |
| `textureLod()` | `textureSampleLevel()` | |
| `textureSize()` | `textureDimensions()` | |
| `imageLoad()` | `textureLoad()` | |
| `imageStore()` | `textureStore()` | |
| `bitfieldExtract()` | `extractBits()` | |
| `bitfieldInsert()` | `insertBits()` | |
| `packHalf2x16()` | `pack2x16float()` | |
| `unpackHalf2x16()` | `unpack2x16float()` | |
| `packUnorm4x8()` | `pack4x8unorm()` | |
| `unpackUnorm4x8()` | `unpack4x8unorm()` | |
| `mix(a, b, boolVec)` | `select(a, b, boolVec)` | When using bool vector |
| `lessThan(a, b)` | `a < b` (component-wise on vectors) | |
| `floatBitsToInt()` | `bitcast<i32>()` | |
| `intBitsToFloat()` | `bitcast<f32>()` | |
| `invariant gl_Position` | `@invariant @builtin(position)` | |
| `barrier()` | `workgroupBarrier()` | Compute |
| `memoryBarrierShared()` | `workgroupBarrier()` | |
| `subgroupBallot()` | `subgroupBallot()` | Requires `enable subgroups;` |
| `discard` | `discard` | Same in WGSL |
| `dFdx()` / `dFdy()` | `dpdx()` / `dpdy()` | |
| `fwidth()` | `fwidth()` | Same |
