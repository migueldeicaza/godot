/**************************************************************************/
/*  rendering_device_driver_webgpu.cpp                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifdef WEBGPU_ENABLED

#include "rendering_device_driver_webgpu.h"
#include "rendering_context_driver_webgpu.h"
#include "rendering_shader_container_webgpu.h"

#include <webgpu/webgpu.h>

// =============================================================================
// Constructor / Destructor
// =============================================================================

RenderingDeviceDriverWebGPU::RenderingDeviceDriverWebGPU(RenderingContextDriverWebGPU *p_context_driver) {
	context_driver = p_context_driver;
}

RenderingDeviceDriverWebGPU::~RenderingDeviceDriverWebGPU() {
	if (push_constant_ring_buffer) {
		wgpuBufferRelease(push_constant_ring_buffer);
		push_constant_ring_buffer = nullptr;
	}
	if (shader_container_format) {
		memdelete(shader_container_format);
		shader_container_format = nullptr;
	}
}

// =============================================================================
// GENERIC
// =============================================================================

Error RenderingDeviceDriverWebGPU::initialize(uint32_t p_device_index, uint32_t p_frame_count) {
	device = context_driver->get_device();
	queue = context_driver->get_queue();
	ERR_FAIL_COND_V(device == nullptr, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(queue == nullptr, ERR_CANT_CREATE);

	frame_count = p_frame_count;

	// Query device limits.
	_check_capabilities();

	// Create push constant ring buffer.
	{
		WGPUBufferDescriptor desc = {};
		desc.size = PUSH_CONSTANT_RING_SIZE;
		desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
		desc.mappedAtCreation = false;
		push_constant_ring_buffer = wgpuDeviceCreateBuffer(device, &desc);
		ERR_FAIL_COND_V(push_constant_ring_buffer == nullptr, ERR_CANT_CREATE);
	}

	// Create shader container format.
	shader_container_format = memnew(RenderingShaderContainerFormatWebGPU);

	return OK;
}

void RenderingDeviceDriverWebGPU::_check_capabilities() {
	capabilities.device_family = DEVICE_UNKNOWN; // TODO: Consider adding DEVICE_WEBGPU to enum.
	capabilities.version_major = 1;
	capabilities.version_minor = 0;

	// Multiview not supported in WebGPU.
	multiview_capabilities.is_supported = false;

	// Fragment shading rate not supported.
	fsr_capabilities = {};

	// Fragment density map not supported.
	fdm_capabilities = {};
}

// =============================================================================
// BUFFERS
// =============================================================================

RDD::BufferID RenderingDeviceDriverWebGPU::buffer_create(uint64_t p_size, BitField<BufferUsageBits> p_usage, MemoryAllocationType p_allocation_type, uint64_t p_frames_drawn) {
	WGBuffer *buf = new WGBuffer();

	// WebGPU buffer sizes must be a multiple of 4.
	uint64_t aligned_size = (p_size + 3) & ~3ULL;

	buf->usage = _buffer_usage_to_wgpu(p_usage);
	buf->usage |= WGPUBufferUsage_CopyDst; // Always allow writes.
	buf->size = aligned_size;

	WGPUBufferDescriptor desc = {};
	desc.size = aligned_size;
	desc.usage = buf->usage;
	desc.mappedAtCreation = false;

	buf->handle = wgpuDeviceCreateBuffer(device, &desc);
	ERR_FAIL_COND_V(buf->handle == nullptr, BufferID());

	return BufferID(buf);
}

bool RenderingDeviceDriverWebGPU::buffer_set_texel_format(BufferID p_buffer, DataFormat p_format) {
	// WebGPU has no texel buffer views. Stub: store format, emulate later if needed.
	return true;
}

void RenderingDeviceDriverWebGPU::buffer_free(BufferID p_buffer) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL(buf);
	if (buf->handle) {
		wgpuBufferRelease(buf->handle);
	}
	if (buf->shadow_map) {
		memfree(buf->shadow_map);
	}
	delete buf;
}

uint64_t RenderingDeviceDriverWebGPU::buffer_get_allocation_size(BufferID p_buffer) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL_V(buf, 0);
	return buf->size;
}

uint8_t *RenderingDeviceDriverWebGPU::buffer_map(BufferID p_buffer) {
	// WebGPU mapping is async and cannot be done synchronously in the browser.
	// Use a shadow CPU buffer for mapped data.
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL_V(buf, nullptr);
	if (!buf->shadow_map) {
		buf->shadow_map = (uint8_t *)memalloc(buf->size);
		memset(buf->shadow_map, 0, buf->size);
	}
	return buf->shadow_map;
}

void RenderingDeviceDriverWebGPU::buffer_unmap(BufferID p_buffer) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL(buf);
	if (buf->shadow_map && buf->map_dirty) {
		wgpuQueueWriteBuffer(queue, buf->handle, 0, buf->shadow_map, buf->size);
		buf->map_dirty = false;
	}
}

uint8_t *RenderingDeviceDriverWebGPU::buffer_persistent_map_advance(BufferID p_buffer, uint64_t p_frames_drawn) {
	// No persistent mapping in WebGPU.
	return nullptr;
}

uint64_t RenderingDeviceDriverWebGPU::buffer_get_dynamic_offsets(Span<BufferID> p_buffers) {
	return 0;
}

void RenderingDeviceDriverWebGPU::buffer_flush(BufferID p_buffer) {
	// If using shadow buffer, flush it.
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	if (buf && buf->shadow_map && buf->map_dirty) {
		wgpuQueueWriteBuffer(queue, buf->handle, 0, buf->shadow_map, buf->size);
		buf->map_dirty = false;
	}
}

uint64_t RenderingDeviceDriverWebGPU::buffer_get_device_address(BufferID p_buffer) {
	return 0; // No device addresses in WebGPU.
}

WGPUBufferUsageFlags RenderingDeviceDriverWebGPU::_buffer_usage_to_wgpu(BitField<BufferUsageBits> p_usage) const {
	WGPUBufferUsageFlags flags = 0;
	if (p_usage.has_flag(BUFFER_USAGE_TRANSFER_FROM_BIT)) {
		flags |= WGPUBufferUsage_CopySrc;
	}
	if (p_usage.has_flag(BUFFER_USAGE_TRANSFER_TO_BIT)) {
		flags |= WGPUBufferUsage_CopyDst;
	}
	if (p_usage.has_flag(BUFFER_USAGE_UNIFORM_BIT)) {
		flags |= WGPUBufferUsage_Uniform;
	}
	if (p_usage.has_flag(BUFFER_USAGE_STORAGE_BIT) || p_usage.has_flag(BUFFER_USAGE_TEXEL_BIT)) {
		flags |= WGPUBufferUsage_Storage;
	}
	if (p_usage.has_flag(BUFFER_USAGE_INDEX_BIT)) {
		flags |= WGPUBufferUsage_Index;
	}
	if (p_usage.has_flag(BUFFER_USAGE_VERTEX_BIT)) {
		flags |= WGPUBufferUsage_Vertex;
	}
	if (p_usage.has_flag(BUFFER_USAGE_INDIRECT_BIT)) {
		flags |= WGPUBufferUsage_Indirect;
	}
	return flags;
}

// =============================================================================
// TEXTURES
// =============================================================================

RDD::TextureID RenderingDeviceDriverWebGPU::texture_create(const TextureFormat &p_format, const TextureView &p_view) {
	WGTexture *tex = new WGTexture();

	tex->format = _data_format_to_wgpu(p_format.format);
	tex->dimension = _texture_type_to_dimension(p_format.texture_type);
	tex->view_dimension = _texture_type_to_view_dimension(p_format.texture_type);
	tex->width = p_format.width;
	tex->height = p_format.height;
	tex->depth = p_format.depth;
	tex->mipmaps = p_format.mipmaps;
	tex->layers = p_format.array_layers;
	tex->sample_count = 1 << p_format.samples;
	tex->usage = _texture_usage_to_wgpu(p_format.usage_bits);

	WGPUTextureDescriptor desc = {};
	desc.dimension = tex->dimension;
	desc.format = tex->format;
	desc.size.width = tex->width;
	desc.size.height = tex->height;
	desc.size.depthOrArrayLayers = (tex->dimension == WGPUTextureDimension_3D) ? tex->depth : tex->layers;
	desc.mipLevelCount = tex->mipmaps;
	desc.sampleCount = tex->sample_count;
	desc.usage = tex->usage;

	tex->handle = wgpuDeviceCreateTexture(device, &desc);
	ERR_FAIL_COND_V(tex->handle == nullptr, TextureID());

	// Create default view.
	WGPUTextureViewDescriptor view_desc = {};
	view_desc.format = tex->format;
	view_desc.dimension = tex->view_dimension;
	view_desc.baseMipLevel = 0;
	view_desc.mipLevelCount = tex->mipmaps;
	view_desc.baseArrayLayer = 0;
	view_desc.arrayLayerCount = tex->layers;
	view_desc.aspect = WGPUTextureAspect_All;

	tex->default_view = wgpuTextureCreateView(tex->handle, &view_desc);

	return TextureID(tex);
}

RDD::TextureID RenderingDeviceDriverWebGPU::texture_create_from_extension(uint64_t p_native_texture, TextureType p_type, DataFormat p_format, uint32_t p_array_layers, bool p_depth_stencil, uint32_t p_mipmaps) {
	// Not supported on web platform.
	ERR_FAIL_V_MSG(TextureID(), "WebGPU: texture_create_from_extension not supported.");
}

RDD::TextureID RenderingDeviceDriverWebGPU::texture_create_shared(TextureID p_original_texture, const TextureView &p_view) {
	WGTexture *orig = (WGTexture *)(p_original_texture.id);
	ERR_FAIL_NULL_V(orig, TextureID());

	WGTexture *tex = new WGTexture();
	*tex = *orig; // Copy base properties.

	// Create a new view with potentially different format.
	WGPUTextureViewDescriptor view_desc = {};
	if (p_view.format != DATA_FORMAT_MAX) {
		view_desc.format = _data_format_to_wgpu(p_view.format);
		tex->format = view_desc.format;
	} else {
		view_desc.format = orig->format;
	}
	view_desc.dimension = tex->view_dimension;
	view_desc.baseMipLevel = 0;
	view_desc.mipLevelCount = tex->mipmaps;
	view_desc.baseArrayLayer = 0;
	view_desc.arrayLayerCount = tex->layers;
	view_desc.aspect = WGPUTextureAspect_All;

	tex->default_view = wgpuTextureCreateView(orig->handle, &view_desc);
	tex->handle = nullptr; // Shared texture does not own the WGPUTexture.

	return TextureID(tex);
}

RDD::TextureID RenderingDeviceDriverWebGPU::texture_create_shared_from_slice(TextureID p_original_texture, const TextureView &p_view, TextureSliceType p_slice_type, uint32_t p_layer, uint32_t p_layers, uint32_t p_mipmap, uint32_t p_mipmaps) {
	WGTexture *orig = (WGTexture *)(p_original_texture.id);
	ERR_FAIL_NULL_V(orig, TextureID());

	WGTexture *tex = new WGTexture();
	*tex = *orig;

	WGPUTextureViewDescriptor view_desc = {};
	view_desc.format = (p_view.format != DATA_FORMAT_MAX) ? _data_format_to_wgpu(p_view.format) : orig->format;
	view_desc.baseMipLevel = p_mipmap;
	view_desc.mipLevelCount = p_mipmaps;
	view_desc.baseArrayLayer = p_layer;
	view_desc.arrayLayerCount = p_layers;
	view_desc.aspect = WGPUTextureAspect_All;

	switch (p_slice_type) {
		case TEXTURE_SLICE_2D:
			view_desc.dimension = WGPUTextureViewDimension_2D;
			break;
		case TEXTURE_SLICE_CUBEMAP:
			view_desc.dimension = WGPUTextureViewDimension_Cube;
			break;
		case TEXTURE_SLICE_3D:
			view_desc.dimension = WGPUTextureViewDimension_3D;
			break;
		case TEXTURE_SLICE_2D_ARRAY:
			view_desc.dimension = WGPUTextureViewDimension_2DArray;
			break;
		default:
			view_desc.dimension = orig->view_dimension;
			break;
	}

	tex->default_view = wgpuTextureCreateView(orig->handle, &view_desc);
	tex->handle = nullptr;
	tex->layers = p_layers;
	tex->mipmaps = p_mipmaps;

	return TextureID(tex);
}

void RenderingDeviceDriverWebGPU::texture_free(TextureID p_texture) {
	WGTexture *tex = (WGTexture *)(p_texture.id);
	ERR_FAIL_NULL(tex);
	if (tex->default_view) {
		wgpuTextureViewRelease(tex->default_view);
	}
	if (tex->handle && !tex->is_from_swap_chain) {
		wgpuTextureRelease(tex->handle);
	}
	delete tex;
}

uint64_t RenderingDeviceDriverWebGPU::texture_get_allocation_size(TextureID p_texture) {
	WGTexture *tex = (WGTexture *)(p_texture.id);
	ERR_FAIL_NULL_V(tex, 0);
	// Approximate: width * height * depth * layers * bpp * mipmaps.
	// TODO: Use proper bytes_per_pixel for the format.
	uint64_t bpp = 4; // Assume 4 bytes per pixel for now.
	return tex->width * tex->height * tex->depth * tex->layers * bpp;
}

void RenderingDeviceDriverWebGPU::texture_get_copyable_layout(TextureID p_texture, const TextureSubresource &p_subresource, TextureCopyableLayout *r_layout) {
	WGTexture *tex = (WGTexture *)(p_texture.id);
	ERR_FAIL_NULL(tex);
	ERR_FAIL_NULL(r_layout);

	uint32_t bpp = 4; // TODO: Get from format.
	uint32_t mip_width = MAX(1u, tex->width >> p_subresource.mipmap);
	uint32_t mip_height = MAX(1u, tex->height >> p_subresource.mipmap);

	// WebGPU requires 256-byte row alignment for buffer <-> texture copies.
	r_layout->row_pitch = ((mip_width * bpp + 255) / 256) * 256;
	r_layout->size = r_layout->row_pitch * mip_height;
}

Vector<uint8_t> RenderingDeviceDriverWebGPU::texture_get_data(TextureID p_texture, uint32_t p_layer) {
	// TODO: Implement async texture readback (copy texture → staging buffer → map → read).
	WARN_PRINT_ONCE("WebGPU: texture_get_data not yet implemented.");
	return Vector<uint8_t>();
}

BitField<RDD::TextureUsageBits> RenderingDeviceDriverWebGPU::texture_get_usages_supported_by_format(DataFormat p_format, bool p_cpu_readable) {
	// TODO: Check per-format capabilities. For now, return common usages.
	BitField<TextureUsageBits> flags = TEXTURE_USAGE_SAMPLING_BIT | TEXTURE_USAGE_CAN_UPDATE_BIT | TEXTURE_USAGE_CAN_COPY_FROM_BIT | TEXTURE_USAGE_CAN_COPY_TO_BIT;
	// Most color formats support render attachment.
	// TODO: Check if format is depth/stencil and add appropriate flags.
	flags.set_flag(TEXTURE_USAGE_COLOR_ATTACHMENT_BIT);
	return flags;
}

bool RenderingDeviceDriverWebGPU::texture_can_make_shared_with_format(TextureID p_texture, DataFormat p_format, bool &r_raw_reinterpretation) {
	r_raw_reinterpretation = false;
	// TODO: Check WebGPU view format compatibility rules.
	return true;
}

WGPUTextureUsageFlags RenderingDeviceDriverWebGPU::_texture_usage_to_wgpu(BitField<TextureUsageBits> p_usage) const {
	WGPUTextureUsageFlags flags = 0;
	if (p_usage.has_flag(TEXTURE_USAGE_SAMPLING_BIT)) {
		flags |= WGPUTextureUsage_TextureBinding;
	}
	if (p_usage.has_flag(TEXTURE_USAGE_COLOR_ATTACHMENT_BIT) || p_usage.has_flag(TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
		flags |= WGPUTextureUsage_RenderAttachment;
	}
	if (p_usage.has_flag(TEXTURE_USAGE_STORAGE_BIT) || p_usage.has_flag(TEXTURE_USAGE_STORAGE_ATOMIC_BIT)) {
		flags |= WGPUTextureUsage_StorageBinding;
	}
	if (p_usage.has_flag(TEXTURE_USAGE_CPU_READ_BIT) || p_usage.has_flag(TEXTURE_USAGE_CAN_COPY_FROM_BIT)) {
		flags |= WGPUTextureUsage_CopySrc;
	}
	if (p_usage.has_flag(TEXTURE_USAGE_CAN_UPDATE_BIT) || p_usage.has_flag(TEXTURE_USAGE_CAN_COPY_TO_BIT)) {
		flags |= WGPUTextureUsage_CopyDst;
	}
	return flags;
}

WGPUTextureDimension RenderingDeviceDriverWebGPU::_texture_type_to_dimension(TextureType p_type) const {
	switch (p_type) {
		case TEXTURE_TYPE_1D:
			return WGPUTextureDimension_1D;
		case TEXTURE_TYPE_3D:
			return WGPUTextureDimension_3D;
		default:
			return WGPUTextureDimension_2D;
	}
}

WGPUTextureViewDimension RenderingDeviceDriverWebGPU::_texture_type_to_view_dimension(TextureType p_type) const {
	switch (p_type) {
		case TEXTURE_TYPE_1D:
			return WGPUTextureViewDimension_1D;
		case TEXTURE_TYPE_2D:
			return WGPUTextureViewDimension_2D;
		case TEXTURE_TYPE_3D:
			return WGPUTextureViewDimension_3D;
		case TEXTURE_TYPE_CUBE:
			return WGPUTextureViewDimension_Cube;
		case TEXTURE_TYPE_2D_ARRAY:
			return WGPUTextureViewDimension_2DArray;
		case TEXTURE_TYPE_CUBE_ARRAY:
			return WGPUTextureViewDimension_CubeArray;
		default:
			return WGPUTextureViewDimension_2D;
	}
}

WGPUTextureFormat RenderingDeviceDriverWebGPU::_data_format_to_wgpu(DataFormat p_format) const {
	// TODO: Move to pixel_formats_webgpu.cpp. See DESIGN.md Appendix B for full table.
	switch (p_format) {
		case DATA_FORMAT_R8_UNORM: return WGPUTextureFormat_R8Unorm;
		case DATA_FORMAT_R8_SNORM: return WGPUTextureFormat_R8Snorm;
		case DATA_FORMAT_R8_UINT: return WGPUTextureFormat_R8Uint;
		case DATA_FORMAT_R8_SINT: return WGPUTextureFormat_R8Sint;
		case DATA_FORMAT_R8G8_UNORM: return WGPUTextureFormat_RG8Unorm;
		case DATA_FORMAT_R8G8_SNORM: return WGPUTextureFormat_RG8Snorm;
		case DATA_FORMAT_R8G8_UINT: return WGPUTextureFormat_RG8Uint;
		case DATA_FORMAT_R8G8_SINT: return WGPUTextureFormat_RG8Sint;
		case DATA_FORMAT_R8G8B8A8_UNORM: return WGPUTextureFormat_RGBA8Unorm;
		case DATA_FORMAT_R8G8B8A8_SNORM: return WGPUTextureFormat_RGBA8Snorm;
		case DATA_FORMAT_R8G8B8A8_UINT: return WGPUTextureFormat_RGBA8Uint;
		case DATA_FORMAT_R8G8B8A8_SINT: return WGPUTextureFormat_RGBA8Sint;
		case DATA_FORMAT_R8G8B8A8_SRGB: return WGPUTextureFormat_RGBA8UnormSrgb;
		case DATA_FORMAT_B8G8R8A8_UNORM: return WGPUTextureFormat_BGRA8Unorm;
		case DATA_FORMAT_B8G8R8A8_SRGB: return WGPUTextureFormat_BGRA8UnormSrgb;
		case DATA_FORMAT_R16_UINT: return WGPUTextureFormat_R16Uint;
		case DATA_FORMAT_R16_SINT: return WGPUTextureFormat_R16Sint;
		case DATA_FORMAT_R16_SFLOAT: return WGPUTextureFormat_R16Float;
		case DATA_FORMAT_R16G16_UINT: return WGPUTextureFormat_RG16Uint;
		case DATA_FORMAT_R16G16_SINT: return WGPUTextureFormat_RG16Sint;
		case DATA_FORMAT_R16G16_SFLOAT: return WGPUTextureFormat_RG16Float;
		case DATA_FORMAT_R16G16B16A16_UINT: return WGPUTextureFormat_RGBA16Uint;
		case DATA_FORMAT_R16G16B16A16_SINT: return WGPUTextureFormat_RGBA16Sint;
		case DATA_FORMAT_R16G16B16A16_SFLOAT: return WGPUTextureFormat_RGBA16Float;
		case DATA_FORMAT_R32_UINT: return WGPUTextureFormat_R32Uint;
		case DATA_FORMAT_R32_SINT: return WGPUTextureFormat_R32Sint;
		case DATA_FORMAT_R32_SFLOAT: return WGPUTextureFormat_R32Float;
		case DATA_FORMAT_R32G32_UINT: return WGPUTextureFormat_RG32Uint;
		case DATA_FORMAT_R32G32_SINT: return WGPUTextureFormat_RG32Sint;
		case DATA_FORMAT_R32G32_SFLOAT: return WGPUTextureFormat_RG32Float;
		case DATA_FORMAT_R32G32B32A32_UINT: return WGPUTextureFormat_RGBA32Uint;
		case DATA_FORMAT_R32G32B32A32_SINT: return WGPUTextureFormat_RGBA32Sint;
		case DATA_FORMAT_R32G32B32A32_SFLOAT: return WGPUTextureFormat_RGBA32Float;
		case DATA_FORMAT_A2B10G10R10_UNORM_PACK32: return WGPUTextureFormat_RGB10A2Unorm;
		case DATA_FORMAT_B10G11R11_UFLOAT_PACK32: return WGPUTextureFormat_RG11B10Ufloat;
		case DATA_FORMAT_E5B9G9R9_UFLOAT_PACK32: return WGPUTextureFormat_RGB9E5Ufloat;
		case DATA_FORMAT_D16_UNORM: return WGPUTextureFormat_Depth16Unorm;
		case DATA_FORMAT_D32_SFLOAT: return WGPUTextureFormat_Depth32Float;
		case DATA_FORMAT_X8_D24_UNORM_PACK32: return WGPUTextureFormat_Depth24Plus;
		case DATA_FORMAT_D24_UNORM_S8_UINT: return WGPUTextureFormat_Depth24PlusStencil8;
		case DATA_FORMAT_D32_SFLOAT_S8_UINT: return WGPUTextureFormat_Depth32FloatStencil8;
		case DATA_FORMAT_S8_UINT: return WGPUTextureFormat_Stencil8;
		default:
			WARN_PRINT(vformat("WebGPU: Unsupported DataFormat %d", (int)p_format));
			return WGPUTextureFormat_Undefined;
	}
}

DataFormat RenderingDeviceDriverWebGPU::_wgpu_to_data_format(WGPUTextureFormat p_format) const {
	// TODO: Full reverse mapping. For now, handle common cases.
	switch (p_format) {
		case WGPUTextureFormat_BGRA8Unorm: return DATA_FORMAT_B8G8R8A8_UNORM;
		case WGPUTextureFormat_RGBA8Unorm: return DATA_FORMAT_R8G8B8A8_UNORM;
		default: return DATA_FORMAT_MAX;
	}
}

// =============================================================================
// SAMPLERS
// =============================================================================

RDD::SamplerID RenderingDeviceDriverWebGPU::sampler_create(const SamplerState &p_state) {
	WGPUSamplerDescriptor desc = {};

	auto map_filter = [](SamplerFilter f) -> WGPUFilterMode {
		return (f == SAMPLER_FILTER_LINEAR) ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
	};
	auto map_address = [](SamplerRepeatMode m) -> WGPUAddressMode {
		switch (m) {
			case SAMPLER_REPEAT_MODE_REPEAT: return WGPUAddressMode_Repeat;
			case SAMPLER_REPEAT_MODE_MIRRORED_REPEAT: return WGPUAddressMode_MirrorRepeat;
			default: return WGPUAddressMode_ClampToEdge;
		}
	};
	auto map_compare = [](CompareOperator op) -> WGPUCompareFunction {
		switch (op) {
			case COMPARE_OP_NEVER: return WGPUCompareFunction_Never;
			case COMPARE_OP_LESS: return WGPUCompareFunction_Less;
			case COMPARE_OP_EQUAL: return WGPUCompareFunction_Equal;
			case COMPARE_OP_LESS_OR_EQUAL: return WGPUCompareFunction_LessEqual;
			case COMPARE_OP_GREATER: return WGPUCompareFunction_Greater;
			case COMPARE_OP_NOT_EQUAL: return WGPUCompareFunction_NotEqual;
			case COMPARE_OP_GREATER_OR_EQUAL: return WGPUCompareFunction_GreaterEqual;
			case COMPARE_OP_ALWAYS: return WGPUCompareFunction_Always;
			default: return WGPUCompareFunction_Undefined;
		}
	};

	desc.magFilter = map_filter(p_state.mag_filter);
	desc.minFilter = map_filter(p_state.min_filter);
	desc.mipmapFilter = (p_state.mip_filter == SAMPLER_FILTER_LINEAR) ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
	desc.addressModeU = map_address(p_state.repeat_u);
	desc.addressModeV = map_address(p_state.repeat_v);
	desc.addressModeW = map_address(p_state.repeat_w);
	desc.lodMinClamp = p_state.min_lod;
	desc.lodMaxClamp = p_state.max_lod;

	if (p_state.enable_compare) {
		desc.compare = map_compare(p_state.compare_op);
	}

	if (p_state.use_anisotropy) {
		desc.maxAnisotropy = (uint16_t)p_state.anisotropy_max;
	} else {
		desc.maxAnisotropy = 1;
	}

	WGPUSampler sampler = wgpuDeviceCreateSampler(device, &desc);
	ERR_FAIL_COND_V(sampler == nullptr, SamplerID());

	// Store the WGPUSampler handle directly as the ID (no wrapper struct needed).
	return SamplerID((uint64_t)sampler);
}

void RenderingDeviceDriverWebGPU::sampler_free(SamplerID p_sampler) {
	WGPUSampler sampler = (WGPUSampler)(p_sampler.id);
	if (sampler) {
		wgpuSamplerRelease(sampler);
	}
}

bool RenderingDeviceDriverWebGPU::sampler_is_format_supported_for_filter(DataFormat p_format, SamplerFilter p_filter) {
	if (p_filter == SAMPLER_FILTER_NEAREST) {
		return true; // All formats support nearest filtering.
	}
	// TODO: Check if float32-filterable feature is available for R32Float, etc.
	// Integer formats don't support linear filtering.
	// Depth formats may or may not depending on implementation.
	return true;
}

// =============================================================================
// VERTEX FORMAT
// =============================================================================

RDD::VertexFormatID RenderingDeviceDriverWebGPU::vertex_format_create(Span<VertexAttribute> p_vertex_attribs, const VertexAttributeBindingsMap &p_vertex_bindings) {
	WGVertexFormat *vf = new WGVertexFormat();
	// TODO: Map vertex attributes to WGPUVertexBufferLayout + WGPUVertexAttribute.
	// Store for use during pipeline creation.
	return VertexFormatID(vf);
}

void RenderingDeviceDriverWebGPU::vertex_format_free(VertexFormatID p_vertex_format) {
	WGVertexFormat *vf = (WGVertexFormat *)(p_vertex_format.id);
	delete vf;
}

// =============================================================================
// BARRIERS (ALL NO-OPS)
// =============================================================================

void RenderingDeviceDriverWebGPU::command_pipeline_barrier(
		CommandBufferID p_cmd_buffer,
		BitField<PipelineStageBits> p_src_stages,
		BitField<PipelineStageBits> p_dst_stages,
		VectorView<MemoryAccessBarrier> p_memory_barriers,
		VectorView<BufferBarrier> p_buffer_barriers,
		VectorView<TextureBarrier> p_texture_barriers) {
	// No-op: WebGPU handles synchronization automatically.
}

// =============================================================================
// FENCES
// =============================================================================

RDD::FenceID RenderingDeviceDriverWebGPU::fence_create() {
	WGFence *fence = new WGFence();
	return FenceID(fence);
}

Error RenderingDeviceDriverWebGPU::fence_wait(FenceID p_fence) {
	WGFence *fence = (WGFence *)(p_fence.id);
	ERR_FAIL_NULL_V(fence, ERR_INVALID_PARAMETER);
	// TODO: Poll with wgpuDeviceTick() or use callback-based completion.
	// For now, assume completion is immediate (single-threaded browser context).
	fence->signaled = true;
	return OK;
}

void RenderingDeviceDriverWebGPU::fence_free(FenceID p_fence) {
	WGFence *fence = (WGFence *)(p_fence.id);
	delete fence;
}

// =============================================================================
// SEMAPHORES (NO-OPS - single queue)
// =============================================================================

RDD::SemaphoreID RenderingDeviceDriverWebGPU::semaphore_create() {
	WGSemaphore *sem = new WGSemaphore();
	return SemaphoreID(sem);
}

void RenderingDeviceDriverWebGPU::semaphore_free(SemaphoreID p_semaphore) {
	WGSemaphore *sem = (WGSemaphore *)(p_semaphore.id);
	delete sem;
}

// =============================================================================
// COMMAND BUFFERS
// =============================================================================

RDD::CommandQueueFamilyID RenderingDeviceDriverWebGPU::command_queue_family_get(BitField<CommandQueueFamilyBits> p_cmd_queue_family_bits, RenderingContextDriver::SurfaceID p_surface) {
	return CommandQueueFamilyID(1); // Single family that supports everything.
}

RDD::CommandQueueID RenderingDeviceDriverWebGPU::command_queue_create(CommandQueueFamilyID p_cmd_queue_family, bool p_identify_as_main_queue) {
	WGCommandQueue *cq = new WGCommandQueue();
	cq->queue = queue; // Share the single device queue.
	return CommandQueueID(cq);
}

Error RenderingDeviceDriverWebGPU::command_queue_execute_and_present(CommandQueueID p_cmd_queue, VectorView<SemaphoreID> p_wait_semaphores, VectorView<CommandBufferID> p_cmd_buffers, VectorView<SemaphoreID> p_cmd_semaphores, FenceID p_cmd_fence, VectorView<SwapChainID> p_swap_chains) {
	// Submit all command buffers.
	LocalVector<WGPUCommandBuffer> wgpu_cmd_buffers;
	for (uint32_t i = 0; i < p_cmd_buffers.size(); i++) {
		WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffers[i].id);
		if (cmd && cmd->finished_buffer) {
			wgpu_cmd_buffers.push_back(cmd->finished_buffer);
		}
	}

	if (wgpu_cmd_buffers.size() > 0) {
		wgpuQueueSubmit(queue, wgpu_cmd_buffers.size(), wgpu_cmd_buffers.ptr());
	}

	// Signal fence if provided.
	if (p_cmd_fence) {
		WGFence *fence = (WGFence *)(p_cmd_fence.id);
		if (fence) {
			// TODO: Use wgpuQueueOnSubmittedWorkDone() callback to set fence->signaled = true.
			fence->signaled = true;
		}
	}

	// Present swap chains.
	for (uint32_t i = 0; i < p_swap_chains.size(); i++) {
		WGSwapChain *sc = (WGSwapChain *)(p_swap_chains[i].id);
		if (sc && sc->surface) {
#ifndef __EMSCRIPTEN__
			wgpuSurfacePresent(sc->surface);
#endif
			// In Emscripten, presentation happens automatically via requestAnimationFrame.
		}
	}

	// Clear finished command buffers (they are consumed by submit).
	for (uint32_t i = 0; i < p_cmd_buffers.size(); i++) {
		WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffers[i].id);
		if (cmd) {
			cmd->finished_buffer = nullptr;
		}
	}

	return OK;
}

void RenderingDeviceDriverWebGPU::command_queue_free(CommandQueueID p_cmd_queue) {
	WGCommandQueue *cq = (WGCommandQueue *)(p_cmd_queue.id);
	delete cq;
}

RDD::CommandPoolID RenderingDeviceDriverWebGPU::command_pool_create(CommandQueueFamilyID p_cmd_queue_family, CommandBufferType p_cmd_buffer_type) {
	WGCommandPool *pool = new WGCommandPool();
	pool->buffer_type = p_cmd_buffer_type;
	return CommandPoolID(pool);
}

bool RenderingDeviceDriverWebGPU::command_pool_reset(CommandPoolID p_cmd_pool) {
	// Nothing to reset — each command buffer creates its own encoder.
	return true;
}

void RenderingDeviceDriverWebGPU::command_pool_free(CommandPoolID p_cmd_pool) {
	WGCommandPool *pool = (WGCommandPool *)(p_cmd_pool.id);
	delete pool;
}

RDD::CommandBufferID RenderingDeviceDriverWebGPU::command_buffer_create(CommandPoolID p_cmd_pool) {
	WGCommandBuffer *cmd = new WGCommandBuffer();
	return CommandBufferID(cmd);
}

bool RenderingDeviceDriverWebGPU::command_buffer_begin(CommandBufferID p_cmd_buffer) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL_V(cmd, false);

	WGPUCommandEncoderDescriptor desc = {};
	cmd->encoder = wgpuDeviceCreateCommandEncoder(device, &desc);
	ERR_FAIL_COND_V(cmd->encoder == nullptr, false);

	cmd->active_encoder = WGCommandBuffer::NONE;
	cmd->push_constants_dirty = false;
	cmd->push_constant_data_len = 0;

	return true;
}

bool RenderingDeviceDriverWebGPU::command_buffer_begin_secondary(CommandBufferID p_cmd_buffer, RenderPassID p_render_pass, uint32_t p_subpass, FramebufferID p_framebuffer) {
	// WebGPU has no secondary command buffers. Treat as primary.
	return command_buffer_begin(p_cmd_buffer);
}

void RenderingDeviceDriverWebGPU::command_buffer_end(CommandBufferID p_cmd_buffer) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	// End any active render/compute pass.
	cmd->end_active_encoder();

	// Finish the command encoder.
	if (cmd->encoder) {
		cmd->finished_buffer = wgpuCommandEncoderFinish(cmd->encoder, nullptr);
		wgpuCommandEncoderRelease(cmd->encoder);
		cmd->encoder = nullptr;
	}
}

void RenderingDeviceDriverWebGPU::command_buffer_execute_secondary(CommandBufferID p_cmd_buffer, VectorView<CommandBufferID> p_secondary_cmd_buffers) {
	// No-op: WebGPU has no secondary command buffers.
}

// =============================================================================
// SWAP CHAIN
// =============================================================================

RDD::SwapChainID RenderingDeviceDriverWebGPU::swap_chain_create(RenderingContextDriver::SurfaceID p_surface) {
	WGSwapChain *sc = new WGSwapChain();
	// TODO: Retrieve WGPUSurface from context driver's surface map.
	sc->format = WGPUTextureFormat_BGRA8Unorm;
	return SwapChainID(sc);
}

Error RenderingDeviceDriverWebGPU::swap_chain_resize(CommandQueueID p_cmd_queue, SwapChainID p_swap_chain, uint32_t p_desired_framebuffer_count) {
	WGSwapChain *sc = (WGSwapChain *)(p_swap_chain.id);
	ERR_FAIL_NULL_V(sc, ERR_INVALID_PARAMETER);

	// TODO: Get width/height from context driver surface.
	// TODO: Call wgpuSurfaceConfigure() with device, format, usage, alphaMode, width, height.
	// TODO: Create render pass for swap chain if not already created.

	sc->configured = true;
	return OK;
}

RDD::FramebufferID RenderingDeviceDriverWebGPU::swap_chain_acquire_framebuffer(CommandQueueID p_cmd_queue, SwapChainID p_swap_chain, bool &r_resize_required) {
	WGSwapChain *sc = (WGSwapChain *)(p_swap_chain.id);
	ERR_FAIL_NULL_V(sc, FramebufferID());

	// TODO: Call wgpuSurfaceGetCurrentTexture().
	// TODO: Check status, set r_resize_required if needed.
	// TODO: Create texture view and framebuffer.

	r_resize_required = false;
	return FramebufferID(); // TODO: Return actual framebuffer.
}

RDD::RenderPassID RenderingDeviceDriverWebGPU::swap_chain_get_render_pass(SwapChainID p_swap_chain) {
	WGSwapChain *sc = (WGSwapChain *)(p_swap_chain.id);
	ERR_FAIL_NULL_V(sc, RenderPassID());
	return RenderPassID(sc->render_pass);
}

RDD::DataFormat RenderingDeviceDriverWebGPU::swap_chain_get_format(SwapChainID p_swap_chain) {
	WGSwapChain *sc = (WGSwapChain *)(p_swap_chain.id);
	ERR_FAIL_NULL_V(sc, DATA_FORMAT_MAX);
	return _wgpu_to_data_format(sc->format);
}

void RenderingDeviceDriverWebGPU::swap_chain_free(SwapChainID p_swap_chain) {
	WGSwapChain *sc = (WGSwapChain *)(p_swap_chain.id);
	ERR_FAIL_NULL(sc);
	if (sc->surface && sc->configured) {
		wgpuSurfaceUnconfigure(sc->surface);
	}
	delete sc;
}

// =============================================================================
// FRAMEBUFFER
// =============================================================================

RDD::FramebufferID RenderingDeviceDriverWebGPU::framebuffer_create(RenderPassID p_render_pass, VectorView<TextureID> p_attachments, uint32_t p_width, uint32_t p_height) {
	WGFramebuffer *fb = new WGFramebuffer();
	fb->render_pass = (WGRenderPass *)(p_render_pass.id);
	fb->width = p_width;
	fb->height = p_height;

	for (uint32_t i = 0; i < p_attachments.size(); i++) {
		WGTexture *tex = (WGTexture *)(p_attachments[i].id);
		fb->attachments.push_back(tex);
		fb->attachment_views.push_back(tex ? tex->default_view : nullptr);
	}

	return FramebufferID(fb);
}

void RenderingDeviceDriverWebGPU::framebuffer_free(FramebufferID p_framebuffer) {
	WGFramebuffer *fb = (WGFramebuffer *)(p_framebuffer.id);
	delete fb;
}

// =============================================================================
// SHADER
// =============================================================================

RDD::ShaderID RenderingDeviceDriverWebGPU::shader_create_from_container(const Ref<RenderingShaderContainer> &p_shader_container, const Vector<ImmutableSampler> &p_immutable_samplers) {
	// TODO: Extract WGSL from container.
	// TODO: Create WGPUShaderModule via wgpuDeviceCreateShaderModule().
	// TODO: Build WGPUBindGroupLayout array from reflection data.
	// TODO: Build WGPUPipelineLayout.
	WARN_PRINT_ONCE("WebGPU: shader_create_from_container not yet implemented.");
	return ShaderID();
}

uint32_t RenderingDeviceDriverWebGPU::shader_get_layout_hash(ShaderID p_shader) {
	WGShader *shader = (WGShader *)(p_shader.id);
	if (!shader) return 0;
	// TODO: Compute hash from bind group layouts.
	return 0;
}

void RenderingDeviceDriverWebGPU::shader_free(ShaderID p_shader) {
	WGShader *shader = (WGShader *)(p_shader.id);
	ERR_FAIL_NULL(shader);
	if (shader->module) {
		wgpuShaderModuleRelease(shader->module);
	}
	if (shader->pipeline_layout) {
		wgpuPipelineLayoutRelease(shader->pipeline_layout);
	}
	for (WGPUBindGroupLayout &layout : shader->bind_group_layouts) {
		if (layout) {
			wgpuBindGroupLayoutRelease(layout);
		}
	}
	delete shader;
}

void RenderingDeviceDriverWebGPU::shader_destroy_modules(ShaderID p_shader) {
	WGShader *shader = (WGShader *)(p_shader.id);
	ERR_FAIL_NULL(shader);
	if (shader->module) {
		wgpuShaderModuleRelease(shader->module);
		shader->module = nullptr;
	}
}

// =============================================================================
// UNIFORM SET
// =============================================================================

RDD::UniformSetID RenderingDeviceDriverWebGPU::uniform_set_create(VectorView<BoundUniform> p_uniforms, ShaderID p_shader, uint32_t p_set_index, int p_linear_pool_index) {
	// TODO: For each BoundUniform, create WGPUBindGroupEntry.
	// TODO: Get bind group layout from shader at p_set_index.
	// TODO: Call wgpuDeviceCreateBindGroup().
	WARN_PRINT_ONCE("WebGPU: uniform_set_create not yet implemented.");
	return UniformSetID();
}

void RenderingDeviceDriverWebGPU::uniform_set_free(UniformSetID p_uniform_set) {
	WGUniformSet *us = (WGUniformSet *)(p_uniform_set.id);
	ERR_FAIL_NULL(us);
	if (us->handle) {
		wgpuBindGroupRelease(us->handle);
	}
	delete us;
}

uint32_t RenderingDeviceDriverWebGPU::uniform_sets_get_dynamic_offsets(VectorView<UniformSetID> p_uniform_sets, ShaderID p_shader, uint32_t p_first_set_index, uint32_t p_set_count) const {
	// TODO: Calculate and return dynamic offsets for dynamic uniform/storage buffers.
	return 0;
}

void RenderingDeviceDriverWebGPU::command_uniform_set_prepare_for_use(CommandBufferID p_cmd_buffer, UniformSetID p_uniform_set, ShaderID p_shader, uint32_t p_set_index) {
	// No-op: WebGPU doesn't need explicit preparation.
}

// =============================================================================
// TRANSFER
// =============================================================================

void RenderingDeviceDriverWebGPU::command_clear_buffer(CommandBufferID p_cmd_buffer, BufferID p_buffer, uint64_t p_offset, uint64_t p_size) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(buf);

	cmd->end_active_encoder();

	uint64_t size = (p_size == BUFFER_WHOLE_SIZE) ? (buf->size - p_offset) : p_size;
	size = (size + 3) & ~3ULL; // Must be multiple of 4.
	wgpuCommandEncoderClearBuffer(cmd->encoder, buf->handle, p_offset, size);
}

void RenderingDeviceDriverWebGPU::command_copy_buffer(CommandBufferID p_cmd_buffer, BufferID p_src_buffer, BufferID p_dst_buffer, VectorView<BufferCopyRegion> p_regions) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *src = (WGBuffer *)(p_src_buffer.id);
	WGBuffer *dst = (WGBuffer *)(p_dst_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(src);
	ERR_FAIL_NULL(dst);

	cmd->end_active_encoder();

	for (uint32_t i = 0; i < p_regions.size(); i++) {
		const BufferCopyRegion &region = p_regions[i];
		uint64_t size = (region.size + 3) & ~3ULL;
		wgpuCommandEncoderCopyBufferToBuffer(cmd->encoder, src->handle, region.src_offset, dst->handle, region.dst_offset, size);
	}
}

void RenderingDeviceDriverWebGPU::command_copy_texture(CommandBufferID p_cmd_buffer, TextureID p_src_texture, TextureLayout p_src_texture_layout, TextureID p_dst_texture, TextureLayout p_dst_texture_layout, VectorView<TextureCopyRegion> p_regions) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGTexture *src = (WGTexture *)(p_src_texture.id);
	WGTexture *dst = (WGTexture *)(p_dst_texture.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(src);
	ERR_FAIL_NULL(dst);

	cmd->end_active_encoder();

	for (uint32_t i = 0; i < p_regions.size(); i++) {
		const TextureCopyRegion &region = p_regions[i];

		WGPUTexelCopyTextureInfo src_copy = {};
		src_copy.texture = src->handle;
		src_copy.mipLevel = region.src_subresources.mipmap;
		src_copy.origin = { (uint32_t)region.src_offset.x, (uint32_t)region.src_offset.y, region.src_subresources.base_layer };
		src_copy.aspect = WGPUTextureAspect_All;

		WGPUTexelCopyTextureInfo dst_copy = {};
		dst_copy.texture = dst->handle;
		dst_copy.mipLevel = region.dst_subresources.mipmap;
		dst_copy.origin = { (uint32_t)region.dst_offset.x, (uint32_t)region.dst_offset.y, region.dst_subresources.base_layer };
		dst_copy.aspect = WGPUTextureAspect_All;

		WGPUExtent3D extent = { (uint32_t)region.size.x, (uint32_t)region.size.y, (uint32_t)region.size.z };

		wgpuCommandEncoderCopyTextureToTexture(cmd->encoder, &src_copy, &dst_copy, &extent);
	}
}

void RenderingDeviceDriverWebGPU::command_resolve_texture(CommandBufferID p_cmd_buffer, TextureID p_src_texture, TextureLayout p_src_texture_layout, uint32_t p_src_layer, uint32_t p_src_mipmap, TextureID p_dst_texture, TextureLayout p_dst_texture_layout, uint32_t p_dst_layer, uint32_t p_dst_mipmap) {
	// TODO: Create a minimal render pass with MSAA texture as color attachment
	// and the resolve target as resolveTarget. Begin and immediately end the pass.
	WARN_PRINT_ONCE("WebGPU: command_resolve_texture not yet implemented.");
}

void RenderingDeviceDriverWebGPU::command_clear_color_texture(CommandBufferID p_cmd_buffer, TextureID p_texture, TextureLayout p_texture_layout, const Color &p_color, const TextureSubresourceRange &p_subresources) {
	// TODO: Create a render pass with loadOp=Clear and the clear color.
	// Begin and immediately end it. Iterate over layers/mips in the subresource range.
	WARN_PRINT_ONCE("WebGPU: command_clear_color_texture not yet implemented.");
}

void RenderingDeviceDriverWebGPU::command_clear_depth_stencil_texture(CommandBufferID p_cmd_buffer, TextureID p_texture, TextureLayout p_texture_layout, float p_depth, uint8_t p_stencil, const TextureSubresourceRange &p_subresources) {
	// TODO: Same as above but with depth/stencil attachment.
	WARN_PRINT_ONCE("WebGPU: command_clear_depth_stencil_texture not yet implemented.");
}

void RenderingDeviceDriverWebGPU::command_copy_buffer_to_texture(CommandBufferID p_cmd_buffer, BufferID p_src_buffer, TextureID p_dst_texture, TextureLayout p_dst_texture_layout, VectorView<BufferTextureCopyRegion> p_regions) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *src = (WGBuffer *)(p_src_buffer.id);
	WGTexture *dst = (WGTexture *)(p_dst_texture.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(src);
	ERR_FAIL_NULL(dst);

	cmd->end_active_encoder();

	for (uint32_t i = 0; i < p_regions.size(); i++) {
		const BufferTextureCopyRegion &region = p_regions[i];

		WGPUTexelCopyBufferLayout src_layout = {};
		src_layout.offset = region.buffer_offset;
		src_layout.bytesPerRow = ((region.row_pitch + 255) / 256) * 256; // 256-byte aligned.
		src_layout.rowsPerImage = region.texture_region_size.y;

		WGPUTexelCopyTextureInfo dst_copy = {};
		dst_copy.texture = dst->handle;
		dst_copy.mipLevel = region.texture_subresource.mipmap;
		dst_copy.origin = { (uint32_t)region.texture_offset.x, (uint32_t)region.texture_offset.y, region.texture_subresource.layer };
		dst_copy.aspect = WGPUTextureAspect_All;

		WGPUExtent3D extent = { (uint32_t)region.texture_region_size.x, (uint32_t)region.texture_region_size.y, (uint32_t)region.texture_region_size.z };

		wgpuCommandEncoderCopyBufferToTexture(cmd->encoder, &src_layout, &dst_copy, &extent);
	}
}

void RenderingDeviceDriverWebGPU::command_copy_texture_to_buffer(CommandBufferID p_cmd_buffer, TextureID p_src_texture, TextureLayout p_src_texture_layout, BufferID p_dst_buffer, VectorView<BufferTextureCopyRegion> p_regions) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGTexture *src = (WGTexture *)(p_src_texture.id);
	WGBuffer *dst = (WGBuffer *)(p_dst_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(src);
	ERR_FAIL_NULL(dst);

	cmd->end_active_encoder();

	for (uint32_t i = 0; i < p_regions.size(); i++) {
		const BufferTextureCopyRegion &region = p_regions[i];

		WGPUTexelCopyTextureInfo src_copy = {};
		src_copy.texture = src->handle;
		src_copy.mipLevel = region.texture_subresource.mipmap;
		src_copy.origin = { (uint32_t)region.texture_offset.x, (uint32_t)region.texture_offset.y, region.texture_subresource.layer };
		src_copy.aspect = WGPUTextureAspect_All;

		WGPUTexelCopyBufferLayout dst_layout = {};
		dst_layout.offset = region.buffer_offset;
		dst_layout.bytesPerRow = ((region.row_pitch + 255) / 256) * 256;
		dst_layout.rowsPerImage = region.texture_region_size.y;

		WGPUExtent3D extent = { (uint32_t)region.texture_region_size.x, (uint32_t)region.texture_region_size.y, (uint32_t)region.texture_region_size.z };

		wgpuCommandEncoderCopyTextureToBuffer(cmd->encoder, &src_copy, &dst_layout, &extent);
	}
}

// =============================================================================
// PIPELINE
// =============================================================================

void RenderingDeviceDriverWebGPU::pipeline_free(PipelineID p_pipeline) {
	WGPipelineWrapper *pw = (WGPipelineWrapper *)(p_pipeline.id);
	ERR_FAIL_NULL(pw);
	if (pw->type == WGPipelineWrapper::RENDER && pw->render_handle) {
		wgpuRenderPipelineRelease(pw->render_handle);
	} else if (pw->type == WGPipelineWrapper::COMPUTE && pw->compute_handle) {
		wgpuComputePipelineRelease(pw->compute_handle);
	}
	delete pw;
}

void RenderingDeviceDriverWebGPU::command_bind_push_constants(CommandBufferID p_cmd_buffer, ShaderID p_shader, uint32_t p_first_index, VectorView<uint32_t> p_data) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	uint32_t offset = p_first_index * sizeof(uint32_t);
	uint32_t size = p_data.size() * sizeof(uint32_t);
	ERR_FAIL_COND(offset + size > WGCommandBuffer::MAX_PUSH_CONSTANT_SIZE);

	memcpy(cmd->push_constant_data + offset, &p_data[0], size);
	cmd->push_constant_data_len = MAX(cmd->push_constant_data_len, offset + size);
	cmd->push_constants_dirty = true;
}

void RenderingDeviceDriverWebGPU::_flush_push_constants(WGCommandBuffer *p_cmd_buf, WGShader *p_shader) {
	if (!p_cmd_buf->push_constants_dirty || p_cmd_buf->push_constant_data_len == 0 || !p_shader) {
		return;
	}

	// Write push constant data to ring buffer.
	uint32_t aligned_size = (p_cmd_buf->push_constant_data_len + PUSH_CONSTANT_SLOT_ALIGNMENT - 1) & ~(PUSH_CONSTANT_SLOT_ALIGNMENT - 1);

	if (push_constant_ring_offset + aligned_size > PUSH_CONSTANT_RING_SIZE) {
		push_constant_ring_offset = 0; // Wrap around.
	}

	wgpuQueueWriteBuffer(queue, push_constant_ring_buffer, push_constant_ring_offset, p_cmd_buf->push_constant_data, p_cmd_buf->push_constant_data_len);

	uint32_t dynamic_offset = push_constant_ring_offset;

	// Set the push constant bind group with dynamic offset.
	if (p_cmd_buf->render_encoder) {
		wgpuRenderPassEncoderSetBindGroup(p_cmd_buf->render_encoder, p_shader->push_constant_bind_group, nullptr /* TODO: PC bind group */, 1, &dynamic_offset);
	} else if (p_cmd_buf->compute_encoder) {
		wgpuComputePassEncoderSetBindGroup(p_cmd_buf->compute_encoder, p_shader->push_constant_bind_group, nullptr /* TODO: PC bind group */, 1, &dynamic_offset);
	}

	push_constant_ring_offset += aligned_size;
	p_cmd_buf->push_constants_dirty = false;
}

bool RenderingDeviceDriverWebGPU::pipeline_cache_create(const Vector<uint8_t> &p_data) {
	return true; // No-op.
}

void RenderingDeviceDriverWebGPU::pipeline_cache_free() {
	// No-op.
}

size_t RenderingDeviceDriverWebGPU::pipeline_cache_query_size() {
	return 0;
}

Vector<uint8_t> RenderingDeviceDriverWebGPU::pipeline_cache_serialize() {
	return Vector<uint8_t>();
}

// =============================================================================
// RENDERING
// =============================================================================

RDD::RenderPassID RenderingDeviceDriverWebGPU::render_pass_create(VectorView<Attachment> p_attachments, VectorView<Subpass> p_subpasses, VectorView<SubpassDependency> p_subpass_dependencies, uint32_t p_view_count, AttachmentReference p_fragment_density_map_attachment) {
	WGRenderPass *rp = new WGRenderPass();
	rp->view_count = p_view_count;

	for (uint32_t i = 0; i < p_attachments.size(); i++) {
		rp->attachments.push_back(p_attachments[i]);
	}

	for (uint32_t i = 0; i < p_subpasses.size(); i++) {
		WGRenderPass::SubpassInfo sp;
		sp.color_references = p_subpasses[i].color_references;
		sp.input_references = p_subpasses[i].input_references;
		sp.depth_stencil_reference = p_subpasses[i].depth_stencil_reference;
		sp.resolve_references = p_subpasses[i].resolve_references;
		rp->subpasses.push_back(sp);
	}

	return RenderPassID(rp);
}

void RenderingDeviceDriverWebGPU::render_pass_free(RenderPassID p_render_pass) {
	WGRenderPass *rp = (WGRenderPass *)(p_render_pass.id);
	delete rp;
}

void RenderingDeviceDriverWebGPU::command_begin_render_pass(CommandBufferID p_cmd_buffer, RenderPassID p_render_pass, FramebufferID p_framebuffer, CommandBufferType p_cmd_buffer_type, const Rect2i &p_rect, VectorView<RenderPassClearValue> p_clear_values) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGRenderPass *rp = (WGRenderPass *)(p_render_pass.id);
	WGFramebuffer *fb = (WGFramebuffer *)(p_framebuffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(rp);
	ERR_FAIL_NULL(fb);

	// End any active encoder.
	cmd->end_active_encoder();

	// Store render state.
	cmd->render_state.render_pass = rp;
	cmd->render_state.framebuffer = fb;
	cmd->render_state.current_subpass = 0;

	// Build render pass descriptor from first subpass.
	ERR_FAIL_COND(rp->subpasses.size() == 0);
	const WGRenderPass::SubpassInfo &subpass = rp->subpasses[0];

	// TODO: Build WGPURenderPassDescriptor from subpass color/depth references + framebuffer texture views.
	// TODO: Set load/store ops from attachment descriptions.
	// TODO: Set clear values.
	// TODO: Call wgpuCommandEncoderBeginRenderPass().
	// TODO: Store the WGPURenderPassEncoder.
	// For now, create a minimal render pass descriptor.

	WGPURenderPassDescriptor pass_desc = {};
	// TODO: Fill in color attachments and depth attachment from references.

	cmd->render_encoder = wgpuCommandEncoderBeginRenderPass(cmd->encoder, &pass_desc);
	cmd->active_encoder = WGCommandBuffer::RENDER;
}

void RenderingDeviceDriverWebGPU::command_end_render_pass(CommandBufferID p_cmd_buffer) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	if (cmd->render_encoder) {
		wgpuRenderPassEncoderEnd(cmd->render_encoder);
		wgpuRenderPassEncoderRelease(cmd->render_encoder);
		cmd->render_encoder = nullptr;
	}
	cmd->active_encoder = WGCommandBuffer::NONE;
	cmd->render_state = {};
}

void RenderingDeviceDriverWebGPU::command_next_render_subpass(CommandBufferID p_cmd_buffer, CommandBufferType p_cmd_buffer_type) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	// End current render pass.
	if (cmd->render_encoder) {
		wgpuRenderPassEncoderEnd(cmd->render_encoder);
		wgpuRenderPassEncoderRelease(cmd->render_encoder);
		cmd->render_encoder = nullptr;
	}

	// Advance to next subpass.
	cmd->render_state.current_subpass++;

	// TODO: Build new WGPURenderPassDescriptor for the next subpass.
	// TODO: Handle input attachments (previous subpass outputs become texture inputs).
	// TODO: Begin new render pass encoder.

	WARN_PRINT_ONCE("WebGPU: command_next_render_subpass not yet fully implemented.");
}

void RenderingDeviceDriverWebGPU::command_render_set_viewport(CommandBufferID p_cmd_buffer, VectorView<Rect2i> p_viewports) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	if (p_viewports.size() > 0) {
		const Rect2i &vp = p_viewports[0]; // WebGPU supports only one viewport.
		wgpuRenderPassEncoderSetViewport(cmd->render_encoder, (float)vp.position.x, (float)vp.position.y, (float)vp.size.x, (float)vp.size.y, 0.0f, 1.0f);
	}
}

void RenderingDeviceDriverWebGPU::command_render_set_scissor(CommandBufferID p_cmd_buffer, VectorView<Rect2i> p_scissors) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	if (p_scissors.size() > 0) {
		const Rect2i &sr = p_scissors[0];
		wgpuRenderPassEncoderSetScissorRect(cmd->render_encoder, sr.position.x, sr.position.y, sr.size.x, sr.size.y);
	}
}

void RenderingDeviceDriverWebGPU::command_render_clear_attachments(CommandBufferID p_cmd_buffer, VectorView<AttachmentClear> p_attachment_clears, VectorView<Rect2i> p_rects) {
	// WebGPU has no mid-pass clear. Options:
	// (a) End pass, do a clear pass, restart — simplest.
	// (b) Draw full-screen quad with clear color.
	// TODO: Implement option (a).
	WARN_PRINT_ONCE("WebGPU: command_render_clear_attachments not yet implemented.");
}

void RenderingDeviceDriverWebGPU::command_bind_render_pipeline(CommandBufferID p_cmd_buffer, PipelineID p_pipeline) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGPipelineWrapper *pw = (WGPipelineWrapper *)(p_pipeline.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(pw);
	ERR_FAIL_COND(!cmd->render_encoder);

	wgpuRenderPassEncoderSetPipeline(cmd->render_encoder, pw->render_handle);
	cmd->render_state.current_pipeline = pw;
}

void RenderingDeviceDriverWebGPU::command_bind_render_uniform_sets(CommandBufferID p_cmd_buffer, VectorView<UniformSetID> p_uniform_sets, ShaderID p_shader, uint32_t p_first_set_index, uint32_t p_set_count, uint32_t p_dynamic_offsets) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	for (uint32_t i = 0; i < p_set_count; i++) {
		WGUniformSet *us = (WGUniformSet *)(p_uniform_sets[i].id);
		if (us && us->handle) {
			// TODO: Handle dynamic offsets properly.
			wgpuRenderPassEncoderSetBindGroup(cmd->render_encoder, p_first_set_index + i, us->handle, 0, nullptr);
		}
	}
}

void RenderingDeviceDriverWebGPU::command_render_draw(CommandBufferID p_cmd_buffer, uint32_t p_vertex_count, uint32_t p_instance_count, uint32_t p_base_vertex, uint32_t p_first_instance) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	if (cmd->render_state.current_pipeline) {
		_flush_push_constants(cmd, cmd->render_state.current_pipeline->shader);
	}

	wgpuRenderPassEncoderDraw(cmd->render_encoder, p_vertex_count, p_instance_count, p_base_vertex, p_first_instance);
}

void RenderingDeviceDriverWebGPU::command_render_draw_indexed(CommandBufferID p_cmd_buffer, uint32_t p_index_count, uint32_t p_instance_count, uint32_t p_first_index, int32_t p_vertex_offset, uint32_t p_first_instance) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	if (cmd->render_state.current_pipeline) {
		_flush_push_constants(cmd, cmd->render_state.current_pipeline->shader);
	}

	wgpuRenderPassEncoderDrawIndexed(cmd->render_encoder, p_index_count, p_instance_count, p_first_index, p_vertex_offset, p_first_instance);
}

void RenderingDeviceDriverWebGPU::command_render_draw_indexed_indirect(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, uint32_t p_draw_count, uint32_t p_stride) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *indirect = (WGBuffer *)(p_indirect_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(indirect);
	ERR_FAIL_COND(!cmd->render_encoder);

	if (cmd->render_state.current_pipeline) {
		_flush_push_constants(cmd, cmd->render_state.current_pipeline->shader);
	}

	// WebGPU has no multi-draw-indirect — must loop.
	for (uint32_t i = 0; i < p_draw_count; i++) {
		wgpuRenderPassEncoderDrawIndexedIndirect(cmd->render_encoder, indirect->handle, p_offset + i * p_stride);
	}
}

void RenderingDeviceDriverWebGPU::command_render_draw_indexed_indirect_count(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, BufferID p_count_buffer, uint64_t p_count_buffer_offset, uint32_t p_max_draw_count, uint32_t p_stride) {
	// TODO: Read count from buffer (requires async readback). For now, use max count.
	command_render_draw_indexed_indirect(p_cmd_buffer, p_indirect_buffer, p_offset, p_max_draw_count, p_stride);
}

void RenderingDeviceDriverWebGPU::command_render_draw_indirect(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, uint32_t p_draw_count, uint32_t p_stride) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *indirect = (WGBuffer *)(p_indirect_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(indirect);
	ERR_FAIL_COND(!cmd->render_encoder);

	if (cmd->render_state.current_pipeline) {
		_flush_push_constants(cmd, cmd->render_state.current_pipeline->shader);
	}

	for (uint32_t i = 0; i < p_draw_count; i++) {
		wgpuRenderPassEncoderDrawIndirect(cmd->render_encoder, indirect->handle, p_offset + i * p_stride);
	}
}

void RenderingDeviceDriverWebGPU::command_render_draw_indirect_count(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, BufferID p_count_buffer, uint64_t p_count_buffer_offset, uint32_t p_max_draw_count, uint32_t p_stride) {
	command_render_draw_indirect(p_cmd_buffer, p_indirect_buffer, p_offset, p_max_draw_count, p_stride);
}

void RenderingDeviceDriverWebGPU::command_render_bind_vertex_buffers(CommandBufferID p_cmd_buffer, uint32_t p_binding_count, const BufferID *p_buffers, const uint64_t *p_offsets, uint64_t p_dynamic_offsets) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	for (uint32_t i = 0; i < p_binding_count; i++) {
		WGBuffer *buf = (WGBuffer *)(p_buffers[i].id);
		if (buf) {
			wgpuRenderPassEncoderSetVertexBuffer(cmd->render_encoder, i, buf->handle, p_offsets[i], WGPU_WHOLE_SIZE);
		}
	}
}

void RenderingDeviceDriverWebGPU::command_render_bind_index_buffer(CommandBufferID p_cmd_buffer, BufferID p_buffer, IndexBufferFormat p_format, uint64_t p_offset) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(buf);
	ERR_FAIL_COND(!cmd->render_encoder);

	WGPUIndexFormat format = (p_format == INDEX_BUFFER_FORMAT_UINT16) ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
	wgpuRenderPassEncoderSetIndexBuffer(cmd->render_encoder, buf->handle, format, p_offset, WGPU_WHOLE_SIZE);
}

void RenderingDeviceDriverWebGPU::command_render_set_blend_constants(CommandBufferID p_cmd_buffer, const Color &p_constants) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	WGPUColor color = { p_constants.r, p_constants.g, p_constants.b, p_constants.a };
	wgpuRenderPassEncoderSetBlendConstant(cmd->render_encoder, &color);
}

void RenderingDeviceDriverWebGPU::command_render_set_line_width(CommandBufferID p_cmd_buffer, float p_width) {
	// No-op: WebGPU doesn't support line width > 1.0.
}

RDD::PipelineID RenderingDeviceDriverWebGPU::render_pipeline_create(
		ShaderID p_shader,
		VertexFormatID p_vertex_format,
		RenderPrimitive p_render_primitive,
		PipelineRasterizationState p_rasterization_state,
		PipelineMultisampleState p_multisample_state,
		PipelineDepthStencilState p_depth_stencil_state,
		PipelineColorBlendState p_blend_state,
		VectorView<int32_t> p_color_attachments,
		BitField<PipelineDynamicStateFlags> p_dynamic_state,
		RenderPassID p_render_pass,
		uint32_t p_render_subpass,
		VectorView<PipelineSpecializationConstant> p_specialization_constants) {
	// TODO: Build WGPURenderPipelineDescriptor from all parameters.
	// TODO: Map primitive topology, cull mode, front face, blend state, depth/stencil state.
	// TODO: Map vertex format to vertex buffer layouts.
	// TODO: Map specialization constants to WGPUConstantEntry array.
	// TODO: Call wgpuDeviceCreateRenderPipeline().
	WARN_PRINT_ONCE("WebGPU: render_pipeline_create not yet implemented.");
	return PipelineID();
}

// =============================================================================
// COMPUTE
// =============================================================================

void RenderingDeviceDriverWebGPU::command_bind_compute_pipeline(CommandBufferID p_cmd_buffer, PipelineID p_pipeline) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGPipelineWrapper *pw = (WGPipelineWrapper *)(p_pipeline.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(pw);

	// Ensure we're in a compute pass (end any active render pass).
	if (cmd->active_encoder == WGCommandBuffer::RENDER) {
		cmd->end_active_encoder();
	}
	if (cmd->active_encoder != WGCommandBuffer::COMPUTE) {
		WGPUComputePassDescriptor pass_desc = {};
		cmd->compute_encoder = wgpuCommandEncoderBeginComputePass(cmd->encoder, &pass_desc);
		cmd->active_encoder = WGCommandBuffer::COMPUTE;
	}

	wgpuComputePassEncoderSetPipeline(cmd->compute_encoder, pw->compute_handle);
	cmd->render_state.current_pipeline = pw;
}

void RenderingDeviceDriverWebGPU::command_bind_compute_uniform_sets(CommandBufferID p_cmd_buffer, VectorView<UniformSetID> p_uniform_sets, ShaderID p_shader, uint32_t p_first_set_index, uint32_t p_set_count, uint32_t p_dynamic_offsets) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->compute_encoder);

	for (uint32_t i = 0; i < p_set_count; i++) {
		WGUniformSet *us = (WGUniformSet *)(p_uniform_sets[i].id);
		if (us && us->handle) {
			wgpuComputePassEncoderSetBindGroup(cmd->compute_encoder, p_first_set_index + i, us->handle, 0, nullptr);
		}
	}
}

void RenderingDeviceDriverWebGPU::command_compute_dispatch(CommandBufferID p_cmd_buffer, uint32_t p_x_groups, uint32_t p_y_groups, uint32_t p_z_groups) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->compute_encoder);

	if (cmd->render_state.current_pipeline) {
		_flush_push_constants(cmd, cmd->render_state.current_pipeline->shader);
	}

	wgpuComputePassEncoderDispatchWorkgroups(cmd->compute_encoder, p_x_groups, p_y_groups, p_z_groups);
}

void RenderingDeviceDriverWebGPU::command_compute_dispatch_indirect(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *indirect = (WGBuffer *)(p_indirect_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(indirect);
	ERR_FAIL_COND(!cmd->compute_encoder);

	if (cmd->render_state.current_pipeline) {
		_flush_push_constants(cmd, cmd->render_state.current_pipeline->shader);
	}

	wgpuComputePassEncoderDispatchWorkgroupsIndirect(cmd->compute_encoder, indirect->handle, p_offset);
}

RDD::PipelineID RenderingDeviceDriverWebGPU::compute_pipeline_create(ShaderID p_shader, VectorView<PipelineSpecializationConstant> p_specialization_constants) {
	// TODO: Build WGPUComputePipelineDescriptor.
	// TODO: Map specialization constants to WGPUConstantEntry.
	// TODO: Call wgpuDeviceCreateComputePipeline().
	WARN_PRINT_ONCE("WebGPU: compute_pipeline_create not yet implemented.");
	return PipelineID();
}

// =============================================================================
// QUERIES
// =============================================================================

RDD::QueryPoolID RenderingDeviceDriverWebGPU::timestamp_query_pool_create(uint32_t p_query_count) {
	// TODO: Check if timestamp-query feature is available.
	// If available: wgpuDeviceCreateQuerySet() with type=Timestamp.
	return QueryPoolID(); // Null — not supported initially.
}

void RenderingDeviceDriverWebGPU::timestamp_query_pool_free(QueryPoolID p_pool_id) {
	WGQueryPool *pool = (WGQueryPool *)(p_pool_id.id);
	if (pool) {
		if (pool->handle) {
			wgpuQuerySetRelease(pool->handle);
		}
		if (pool->resolve_buffer) {
			wgpuBufferRelease(pool->resolve_buffer);
		}
		delete pool;
	}
}

void RenderingDeviceDriverWebGPU::timestamp_query_pool_get_results(QueryPoolID p_pool_id, uint32_t p_query_count, uint64_t *r_results) {
	// TODO: Resolve query set to buffer, map buffer, copy results.
	memset(r_results, 0, sizeof(uint64_t) * p_query_count);
}

uint64_t RenderingDeviceDriverWebGPU::timestamp_query_result_to_time(uint64_t p_result) {
	return p_result; // WebGPU timestamps are already in nanoseconds.
}

void RenderingDeviceDriverWebGPU::command_timestamp_query_pool_reset(CommandBufferID p_cmd_buffer, QueryPoolID p_pool_id, uint32_t p_query_count) {
	// No-op: WebGPU query sets don't need reset.
}

void RenderingDeviceDriverWebGPU::command_timestamp_write(CommandBufferID p_cmd_buffer, QueryPoolID p_pool_id, uint32_t p_index) {
	// TODO: wgpuCommandEncoderWriteTimestamp(encoder, querySet, index) if available.
}

// =============================================================================
// LABELS & DEBUG
// =============================================================================

void RenderingDeviceDriverWebGPU::command_begin_label(CommandBufferID p_cmd_buffer, const char *p_label_name, const Color &p_color) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	if (cmd->render_encoder) {
		wgpuRenderPassEncoderPushDebugGroup(cmd->render_encoder, p_label_name);
	} else if (cmd->compute_encoder) {
		wgpuComputePassEncoderPushDebugGroup(cmd->compute_encoder, p_label_name);
	} else if (cmd->encoder) {
		wgpuCommandEncoderPushDebugGroup(cmd->encoder, p_label_name);
	}
}

void RenderingDeviceDriverWebGPU::command_end_label(CommandBufferID p_cmd_buffer) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	if (cmd->render_encoder) {
		wgpuRenderPassEncoderPopDebugGroup(cmd->render_encoder);
	} else if (cmd->compute_encoder) {
		wgpuComputePassEncoderPopDebugGroup(cmd->compute_encoder);
	} else if (cmd->encoder) {
		wgpuCommandEncoderPopDebugGroup(cmd->encoder);
	}
}

void RenderingDeviceDriverWebGPU::command_insert_breadcrumb(CommandBufferID p_cmd_buffer, uint32_t p_data) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	char marker[32];
	snprintf(marker, sizeof(marker), "breadcrumb_%u", p_data);

	if (cmd->render_encoder) {
		wgpuRenderPassEncoderInsertDebugMarker(cmd->render_encoder, marker);
	} else if (cmd->compute_encoder) {
		wgpuComputePassEncoderInsertDebugMarker(cmd->compute_encoder, marker);
	} else if (cmd->encoder) {
		wgpuCommandEncoderInsertDebugMarker(cmd->encoder, marker);
	}
}

// =============================================================================
// SUBMISSION
// =============================================================================

void RenderingDeviceDriverWebGPU::begin_segment(uint32_t p_frame_index, uint32_t p_frames_drawn) {
	frame_index = p_frame_index;
	frames_drawn = p_frames_drawn;

	// Reset push constant ring buffer offset at the start of each frame.
	push_constant_ring_offset = 0;
}

void RenderingDeviceDriverWebGPU::end_segment() {
	// Nothing to do.
}

// =============================================================================
// MISC
// =============================================================================

void RenderingDeviceDriverWebGPU::set_object_name(ObjectType p_type, ID p_driver_id, const String &p_name) {
	// TODO: Call wgpuBufferSetLabel() / wgpuTextureSetLabel() etc. based on type.
}

uint64_t RenderingDeviceDriverWebGPU::get_resource_native_handle(DriverResource p_type, ID p_driver_id) {
	return p_driver_id.id;
}

uint64_t RenderingDeviceDriverWebGPU::get_total_memory_used() {
	return 0; // TODO: Track internally.
}

uint64_t RenderingDeviceDriverWebGPU::get_lazily_memory_used() {
	return 0;
}

uint64_t RenderingDeviceDriverWebGPU::limit_get(Limit p_limit) {
	// TODO: Query from WGPUSupportedLimits. See RESEARCH.md Appendix C for full mapping.
	switch (p_limit) {
		case LIMIT_MAX_BOUND_UNIFORM_SETS: return 4;
		case LIMIT_MAX_FRAMEBUFFER_COLOR_ATTACHMENTS: return 8;
		case LIMIT_MAX_TEXTURES_PER_UNIFORM_SET: return 16;
		case LIMIT_MAX_SAMPLERS_PER_UNIFORM_SET: return 16;
		case LIMIT_MAX_STORAGE_BUFFERS_PER_UNIFORM_SET: return 8;
		case LIMIT_MAX_STORAGE_IMAGES_PER_UNIFORM_SET: return 4;
		case LIMIT_MAX_UNIFORM_BUFFERS_PER_UNIFORM_SET: return 12;
		case LIMIT_MAX_PUSH_CONSTANT_SIZE: return 128;
		case LIMIT_MAX_UNIFORM_BUFFER_SIZE: return 65536;
		case LIMIT_MAX_VERTEX_INPUT_ATTRIBUTE_OFFSET: return 2048;
		case LIMIT_MAX_VERTEX_INPUT_ATTRIBUTES: return 16;
		case LIMIT_MAX_VERTEX_INPUT_BINDINGS: return 8;
		case LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X: return 65535;
		case LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_Y: return 65535;
		case LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_Z: return 65535;
		case LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_X: return 256;
		case LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_Y: return 256;
		case LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_Z: return 64;
		case LIMIT_SUBGROUP_SIZE: return 0; // Subgroups not guaranteed.
		case LIMIT_SUBGROUP_MIN_SIZE: return 0;
		case LIMIT_SUBGROUP_MAX_SIZE: return 0;
		default: return 0;
	}
}

uint64_t RenderingDeviceDriverWebGPU::api_trait_get(ApiTrait p_trait) {
	switch (p_trait) {
		case API_TRAIT_HONORS_PIPELINE_BARRIERS: return 0; // No barriers in WebGPU.
		case API_TRAIT_SHADER_CHANGE_INVALIDATION: return SHADER_CHANGE_INVALIDATION_ALL_BOUND_UNIFORM_SETS;
		case API_TRAIT_TEXTURE_TRANSFER_ALIGNMENT: return 256;
		case API_TRAIT_TEXTURE_DATA_ROW_PITCH_STEP: return 256;
		case API_TRAIT_SECONDARY_VIEWPORT_SCISSOR: return 0;
		case API_TRAIT_CLEARS_WITH_COPY_ENGINE: return 0;
		case API_TRAIT_USE_GENERAL_IN_COPY_QUEUES: return 0;
		case API_TRAIT_BUFFERS_REQUIRE_TRANSITIONS: return 0;
		case API_TRAIT_TEXTURE_OUTPUTS_REQUIRE_CLEARS: return 0;
		default: return RenderingDeviceDriver::api_trait_get(p_trait);
	}
}

bool RenderingDeviceDriverWebGPU::has_feature(Features p_feature) {
	// TODO: Check WebGPU device features.
	return false;
}

const RDD::MultiviewCapabilities &RenderingDeviceDriverWebGPU::get_multiview_capabilities() {
	return multiview_capabilities;
}

const RDD::FragmentShadingRateCapabilities &RenderingDeviceDriverWebGPU::get_fragment_shading_rate_capabilities() {
	return fsr_capabilities;
}

const RDD::FragmentDensityMapCapabilities &RenderingDeviceDriverWebGPU::get_fragment_density_map_capabilities() {
	return fdm_capabilities;
}

String RenderingDeviceDriverWebGPU::get_api_name() const {
	return "WebGPU";
}

String RenderingDeviceDriverWebGPU::get_api_version() const {
	return "1.0";
}

String RenderingDeviceDriverWebGPU::get_pipeline_cache_uuid() const {
	return "";
}

const RDD::Capabilities &RenderingDeviceDriverWebGPU::get_capabilities() const {
	return capabilities;
}

const RenderingShaderContainerFormat &RenderingDeviceDriverWebGPU::get_shader_container_format() const {
	return *shader_container_format;
}

#endif // WEBGPU_ENABLED
