/**************************************************************************/
/*  rendering_device_driver_webgpu.h                                      */
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

#pragma once

#ifdef WEBGPU_ENABLED

#include "webgpu_objects.h"

#include "servers/rendering/rendering_device_driver.h"

#include <webgpu/webgpu.h>

class RenderingContextDriverWebGPU;
class RenderingShaderContainerFormatWebGPU;

class RenderingDeviceDriverWebGPU : public RenderingDeviceDriver {
	RenderingContextDriverWebGPU *context_driver = nullptr;
	WGPUDevice device = nullptr;
	WGPUQueue queue = nullptr;

	uint32_t frame_count = 1;
	uint32_t frame_index = 0;
	uint32_t frames_drawn = 0;

	Capabilities capabilities;
	MultiviewCapabilities multiview_capabilities;
	FragmentShadingRateCapabilities fsr_capabilities;
	FragmentDensityMapCapabilities fdm_capabilities;

	RenderingShaderContainerFormatWebGPU *shader_container_format = nullptr;

	// --- Push Constant Emulation ---
	// Ring buffer for push constant data. Each slot is 256-byte aligned.
	WGPUBuffer push_constant_ring_buffer = nullptr;
	static constexpr uint32_t PUSH_CONSTANT_RING_SIZE = 256 * 1024; // 256KB
	static constexpr uint32_t PUSH_CONSTANT_SLOT_ALIGNMENT = 256;
	uint32_t push_constant_ring_offset = 0;

	// --- Pixel Format Mapping ---
	// TODO: Move to dedicated pixel_formats_webgpu.h/cpp when ready.

	// --- Internal Helpers ---
	void _check_capabilities();
	WGPUTextureFormat _data_format_to_wgpu(DataFormat p_format) const;
	DataFormat _wgpu_to_data_format(WGPUTextureFormat p_format) const;
	WGPUBufferUsageFlags _buffer_usage_to_wgpu(BitField<BufferUsageBits> p_usage) const;
	WGPUTextureUsageFlags _texture_usage_to_wgpu(BitField<TextureUsageBits> p_usage) const;
	WGPUTextureDimension _texture_type_to_dimension(TextureType p_type) const;
	WGPUTextureViewDimension _texture_type_to_view_dimension(TextureType p_type) const;

	void _flush_push_constants(WGCommandBuffer *p_cmd_buf, WGShader *p_shader);

public:
	RenderingDeviceDriverWebGPU(RenderingContextDriverWebGPU *p_context_driver);
	virtual ~RenderingDeviceDriverWebGPU() override;

	// -----------------------------------------------------------------------
	// GENERIC
	// -----------------------------------------------------------------------

	virtual Error initialize(uint32_t p_device_index, uint32_t p_frame_count) override final;

	// -----------------------------------------------------------------------
	// BUFFERS
	// -----------------------------------------------------------------------

	virtual BufferID buffer_create(uint64_t p_size, BitField<BufferUsageBits> p_usage, MemoryAllocationType p_allocation_type, uint64_t p_frames_drawn) override final;
	virtual bool buffer_set_texel_format(BufferID p_buffer, DataFormat p_format) override final;
	virtual void buffer_free(BufferID p_buffer) override final;
	virtual uint64_t buffer_get_allocation_size(BufferID p_buffer) override final;
	virtual uint8_t *buffer_map(BufferID p_buffer) override final;
	virtual void buffer_unmap(BufferID p_buffer) override final;
	virtual uint8_t *buffer_persistent_map_advance(BufferID p_buffer, uint64_t p_frames_drawn) override final;
	virtual uint64_t buffer_get_dynamic_offsets(Span<BufferID> p_buffers) override final;
	virtual void buffer_flush(BufferID p_buffer) override final;
	virtual uint64_t buffer_get_device_address(BufferID p_buffer) override final;

	// -----------------------------------------------------------------------
	// TEXTURES
	// -----------------------------------------------------------------------

	virtual TextureID texture_create(const TextureFormat &p_format, const TextureView &p_view) override final;
	virtual TextureID texture_create_from_extension(uint64_t p_native_texture, TextureType p_type, DataFormat p_format, uint32_t p_array_layers, bool p_depth_stencil, uint32_t p_mipmaps) override final;
	virtual TextureID texture_create_shared(TextureID p_original_texture, const TextureView &p_view) override final;
	virtual TextureID texture_create_shared_from_slice(TextureID p_original_texture, const TextureView &p_view, TextureSliceType p_slice_type, uint32_t p_layer, uint32_t p_layers, uint32_t p_mipmap, uint32_t p_mipmaps) override final;
	virtual void texture_free(TextureID p_texture) override final;
	virtual uint64_t texture_get_allocation_size(TextureID p_texture) override final;
	virtual void texture_get_copyable_layout(TextureID p_texture, const TextureSubresource &p_subresource, TextureCopyableLayout *r_layout) override final;
	virtual Vector<uint8_t> texture_get_data(TextureID p_texture, uint32_t p_layer) override final;
	virtual BitField<TextureUsageBits> texture_get_usages_supported_by_format(DataFormat p_format, bool p_cpu_readable) override final;
	virtual bool texture_can_make_shared_with_format(TextureID p_texture, DataFormat p_format, bool &r_raw_reinterpretation) override final;

	// -----------------------------------------------------------------------
	// SAMPLERS
	// -----------------------------------------------------------------------

	virtual SamplerID sampler_create(const SamplerState &p_state) override final;
	virtual void sampler_free(SamplerID p_sampler) override final;
	virtual bool sampler_is_format_supported_for_filter(DataFormat p_format, SamplerFilter p_filter) override final;

	// -----------------------------------------------------------------------
	// VERTEX FORMAT
	// -----------------------------------------------------------------------

	virtual VertexFormatID vertex_format_create(Span<VertexAttribute> p_vertex_attribs, const VertexAttributeBindingsMap &p_vertex_bindings) override final;
	virtual void vertex_format_free(VertexFormatID p_vertex_format) override final;

	// -----------------------------------------------------------------------
	// BARRIERS
	// -----------------------------------------------------------------------

	virtual void command_pipeline_barrier(
			CommandBufferID p_cmd_buffer,
			BitField<PipelineStageBits> p_src_stages,
			BitField<PipelineStageBits> p_dst_stages,
			VectorView<MemoryAccessBarrier> p_memory_barriers,
			VectorView<BufferBarrier> p_buffer_barriers,
			VectorView<TextureBarrier> p_texture_barriers) override final;

	// -----------------------------------------------------------------------
	// FENCES
	// -----------------------------------------------------------------------

	virtual FenceID fence_create() override final;
	virtual Error fence_wait(FenceID p_fence) override final;
	virtual void fence_free(FenceID p_fence) override final;

	// -----------------------------------------------------------------------
	// SEMAPHORES
	// -----------------------------------------------------------------------

	virtual SemaphoreID semaphore_create() override final;
	virtual void semaphore_free(SemaphoreID p_semaphore) override final;

	// -----------------------------------------------------------------------
	// COMMAND BUFFERS
	// -----------------------------------------------------------------------

	virtual CommandQueueFamilyID command_queue_family_get(BitField<CommandQueueFamilyBits> p_cmd_queue_family_bits, RenderingContextDriver::SurfaceID p_surface = 0) override final;
	virtual CommandQueueID command_queue_create(CommandQueueFamilyID p_cmd_queue_family, bool p_identify_as_main_queue = false) override final;
	virtual Error command_queue_execute_and_present(CommandQueueID p_cmd_queue, VectorView<SemaphoreID> p_wait_semaphores, VectorView<CommandBufferID> p_cmd_buffers, VectorView<SemaphoreID> p_cmd_semaphores, FenceID p_cmd_fence, VectorView<SwapChainID> p_swap_chains) override final;
	virtual void command_queue_free(CommandQueueID p_cmd_queue) override final;

	virtual CommandPoolID command_pool_create(CommandQueueFamilyID p_cmd_queue_family, CommandBufferType p_cmd_buffer_type) override final;
	virtual bool command_pool_reset(CommandPoolID p_cmd_pool) override final;
	virtual void command_pool_free(CommandPoolID p_cmd_pool) override final;

	virtual CommandBufferID command_buffer_create(CommandPoolID p_cmd_pool) override final;
	virtual bool command_buffer_begin(CommandBufferID p_cmd_buffer) override final;
	virtual bool command_buffer_begin_secondary(CommandBufferID p_cmd_buffer, RenderPassID p_render_pass, uint32_t p_subpass, FramebufferID p_framebuffer) override final;
	virtual void command_buffer_end(CommandBufferID p_cmd_buffer) override final;
	virtual void command_buffer_execute_secondary(CommandBufferID p_cmd_buffer, VectorView<CommandBufferID> p_secondary_cmd_buffers) override final;

	// -----------------------------------------------------------------------
	// SWAP CHAIN
	// -----------------------------------------------------------------------

	virtual SwapChainID swap_chain_create(RenderingContextDriver::SurfaceID p_surface) override final;
	virtual Error swap_chain_resize(CommandQueueID p_cmd_queue, SwapChainID p_swap_chain, uint32_t p_desired_framebuffer_count) override final;
	virtual FramebufferID swap_chain_acquire_framebuffer(CommandQueueID p_cmd_queue, SwapChainID p_swap_chain, bool &r_resize_required) override final;
	virtual RenderPassID swap_chain_get_render_pass(SwapChainID p_swap_chain) override final;
	virtual DataFormat swap_chain_get_format(SwapChainID p_swap_chain) override final;
	virtual void swap_chain_free(SwapChainID p_swap_chain) override final;

	// -----------------------------------------------------------------------
	// FRAMEBUFFER
	// -----------------------------------------------------------------------

	virtual FramebufferID framebuffer_create(RenderPassID p_render_pass, VectorView<TextureID> p_attachments, uint32_t p_width, uint32_t p_height) override final;
	virtual void framebuffer_free(FramebufferID p_framebuffer) override final;

	// -----------------------------------------------------------------------
	// SHADER
	// -----------------------------------------------------------------------

	virtual ShaderID shader_create_from_container(const Ref<RenderingShaderContainer> &p_shader_container, const Vector<ImmutableSampler> &p_immutable_samplers) override final;
	virtual uint32_t shader_get_layout_hash(ShaderID p_shader) override final;
	virtual void shader_free(ShaderID p_shader) override final;
	virtual void shader_destroy_modules(ShaderID p_shader) override final;

	// -----------------------------------------------------------------------
	// UNIFORM SET
	// -----------------------------------------------------------------------

	virtual UniformSetID uniform_set_create(VectorView<BoundUniform> p_uniforms, ShaderID p_shader, uint32_t p_set_index, int p_linear_pool_index) override final;
	virtual void uniform_set_free(UniformSetID p_uniform_set) override final;
	virtual uint32_t uniform_sets_get_dynamic_offsets(VectorView<UniformSetID> p_uniform_sets, ShaderID p_shader, uint32_t p_first_set_index, uint32_t p_set_count) const override final;
	virtual void command_uniform_set_prepare_for_use(CommandBufferID p_cmd_buffer, UniformSetID p_uniform_set, ShaderID p_shader, uint32_t p_set_index) override final;

	// -----------------------------------------------------------------------
	// TRANSFER
	// -----------------------------------------------------------------------

	virtual void command_clear_buffer(CommandBufferID p_cmd_buffer, BufferID p_buffer, uint64_t p_offset, uint64_t p_size) override final;
	virtual void command_copy_buffer(CommandBufferID p_cmd_buffer, BufferID p_src_buffer, BufferID p_dst_buffer, VectorView<BufferCopyRegion> p_regions) override final;
	virtual void command_copy_texture(CommandBufferID p_cmd_buffer, TextureID p_src_texture, TextureLayout p_src_texture_layout, TextureID p_dst_texture, TextureLayout p_dst_texture_layout, VectorView<TextureCopyRegion> p_regions) override final;
	virtual void command_resolve_texture(CommandBufferID p_cmd_buffer, TextureID p_src_texture, TextureLayout p_src_texture_layout, uint32_t p_src_layer, uint32_t p_src_mipmap, TextureID p_dst_texture, TextureLayout p_dst_texture_layout, uint32_t p_dst_layer, uint32_t p_dst_mipmap) override final;
	virtual void command_clear_color_texture(CommandBufferID p_cmd_buffer, TextureID p_texture, TextureLayout p_texture_layout, const Color &p_color, const TextureSubresourceRange &p_subresources) override final;
	virtual void command_clear_depth_stencil_texture(CommandBufferID p_cmd_buffer, TextureID p_texture, TextureLayout p_texture_layout, float p_depth, uint8_t p_stencil, const TextureSubresourceRange &p_subresources) override final;
	virtual void command_copy_buffer_to_texture(CommandBufferID p_cmd_buffer, BufferID p_src_buffer, TextureID p_dst_texture, TextureLayout p_dst_texture_layout, VectorView<BufferTextureCopyRegion> p_regions) override final;
	virtual void command_copy_texture_to_buffer(CommandBufferID p_cmd_buffer, TextureID p_src_texture, TextureLayout p_src_texture_layout, BufferID p_dst_buffer, VectorView<BufferTextureCopyRegion> p_regions) override final;

	// -----------------------------------------------------------------------
	// PIPELINE
	// -----------------------------------------------------------------------

	virtual void pipeline_free(PipelineID p_pipeline) override final;
	virtual void command_bind_push_constants(CommandBufferID p_cmd_buffer, ShaderID p_shader, uint32_t p_first_index, VectorView<uint32_t> p_data) override final;
	virtual bool pipeline_cache_create(const Vector<uint8_t> &p_data) override final;
	virtual void pipeline_cache_free() override final;
	virtual size_t pipeline_cache_query_size() override final;
	virtual Vector<uint8_t> pipeline_cache_serialize() override final;

	// -----------------------------------------------------------------------
	// RENDERING
	// -----------------------------------------------------------------------

	virtual RenderPassID render_pass_create(VectorView<Attachment> p_attachments, VectorView<Subpass> p_subpasses, VectorView<SubpassDependency> p_subpass_dependencies, uint32_t p_view_count, AttachmentReference p_fragment_density_map_attachment) override final;
	virtual void render_pass_free(RenderPassID p_render_pass) override final;

	virtual void command_begin_render_pass(CommandBufferID p_cmd_buffer, RenderPassID p_render_pass, FramebufferID p_framebuffer, CommandBufferType p_cmd_buffer_type, const Rect2i &p_rect, VectorView<RenderPassClearValue> p_clear_values) override final;
	virtual void command_end_render_pass(CommandBufferID p_cmd_buffer) override final;
	virtual void command_next_render_subpass(CommandBufferID p_cmd_buffer, CommandBufferType p_cmd_buffer_type) override final;
	virtual void command_render_set_viewport(CommandBufferID p_cmd_buffer, VectorView<Rect2i> p_viewports) override final;
	virtual void command_render_set_scissor(CommandBufferID p_cmd_buffer, VectorView<Rect2i> p_scissors) override final;
	virtual void command_render_clear_attachments(CommandBufferID p_cmd_buffer, VectorView<AttachmentClear> p_attachment_clears, VectorView<Rect2i> p_rects) override final;

	virtual void command_bind_render_pipeline(CommandBufferID p_cmd_buffer, PipelineID p_pipeline) override final;
	virtual void command_bind_render_uniform_sets(CommandBufferID p_cmd_buffer, VectorView<UniformSetID> p_uniform_sets, ShaderID p_shader, uint32_t p_first_set_index, uint32_t p_set_count, uint32_t p_dynamic_offsets) override final;

	virtual void command_render_draw(CommandBufferID p_cmd_buffer, uint32_t p_vertex_count, uint32_t p_instance_count, uint32_t p_base_vertex, uint32_t p_first_instance) override final;
	virtual void command_render_draw_indexed(CommandBufferID p_cmd_buffer, uint32_t p_index_count, uint32_t p_instance_count, uint32_t p_first_index, int32_t p_vertex_offset, uint32_t p_first_instance) override final;
	virtual void command_render_draw_indexed_indirect(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, uint32_t p_draw_count, uint32_t p_stride) override final;
	virtual void command_render_draw_indexed_indirect_count(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, BufferID p_count_buffer, uint64_t p_count_buffer_offset, uint32_t p_max_draw_count, uint32_t p_stride) override final;
	virtual void command_render_draw_indirect(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, uint32_t p_draw_count, uint32_t p_stride) override final;
	virtual void command_render_draw_indirect_count(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, BufferID p_count_buffer, uint64_t p_count_buffer_offset, uint32_t p_max_draw_count, uint32_t p_stride) override final;

	virtual void command_render_bind_vertex_buffers(CommandBufferID p_cmd_buffer, uint32_t p_binding_count, const BufferID *p_buffers, const uint64_t *p_offsets, uint64_t p_dynamic_offsets) override final;
	virtual void command_render_bind_index_buffer(CommandBufferID p_cmd_buffer, BufferID p_buffer, IndexBufferFormat p_format, uint64_t p_offset) override final;

	virtual void command_render_set_blend_constants(CommandBufferID p_cmd_buffer, const Color &p_constants) override final;
	virtual void command_render_set_line_width(CommandBufferID p_cmd_buffer, float p_width) override final;

	virtual PipelineID render_pipeline_create(
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
			VectorView<PipelineSpecializationConstant> p_specialization_constants) override final;

	// -----------------------------------------------------------------------
	// COMPUTE
	// -----------------------------------------------------------------------

	virtual void command_bind_compute_pipeline(CommandBufferID p_cmd_buffer, PipelineID p_pipeline) override final;
	virtual void command_bind_compute_uniform_sets(CommandBufferID p_cmd_buffer, VectorView<UniformSetID> p_uniform_sets, ShaderID p_shader, uint32_t p_first_set_index, uint32_t p_set_count, uint32_t p_dynamic_offsets) override final;
	virtual void command_compute_dispatch(CommandBufferID p_cmd_buffer, uint32_t p_x_groups, uint32_t p_y_groups, uint32_t p_z_groups) override final;
	virtual void command_compute_dispatch_indirect(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset) override final;
	virtual PipelineID compute_pipeline_create(ShaderID p_shader, VectorView<PipelineSpecializationConstant> p_specialization_constants) override final;

	// -----------------------------------------------------------------------
	// QUERIES
	// -----------------------------------------------------------------------

	virtual QueryPoolID timestamp_query_pool_create(uint32_t p_query_count) override final;
	virtual void timestamp_query_pool_free(QueryPoolID p_pool_id) override final;
	virtual void timestamp_query_pool_get_results(QueryPoolID p_pool_id, uint32_t p_query_count, uint64_t *r_results) override final;
	virtual uint64_t timestamp_query_result_to_time(uint64_t p_result) override final;
	virtual void command_timestamp_query_pool_reset(CommandBufferID p_cmd_buffer, QueryPoolID p_pool_id, uint32_t p_query_count) override final;
	virtual void command_timestamp_write(CommandBufferID p_cmd_buffer, QueryPoolID p_pool_id, uint32_t p_index) override final;

	// -----------------------------------------------------------------------
	// LABELS & DEBUG
	// -----------------------------------------------------------------------

	virtual void command_begin_label(CommandBufferID p_cmd_buffer, const char *p_label_name, const Color &p_color) override final;
	virtual void command_end_label(CommandBufferID p_cmd_buffer) override final;
	virtual void command_insert_breadcrumb(CommandBufferID p_cmd_buffer, uint32_t p_data) override final;

	// -----------------------------------------------------------------------
	// SUBMISSION
	// -----------------------------------------------------------------------

	virtual void begin_segment(uint32_t p_frame_index, uint32_t p_frames_drawn) override final;
	virtual void end_segment() override final;

	// -----------------------------------------------------------------------
	// MISC
	// -----------------------------------------------------------------------

	virtual void set_object_name(ObjectType p_type, ID p_driver_id, const String &p_name) override final;
	virtual uint64_t get_resource_native_handle(DriverResource p_type, ID p_driver_id) override final;
	virtual uint64_t get_total_memory_used() override final;
	virtual uint64_t get_lazily_memory_used() override final;
	virtual uint64_t limit_get(Limit p_limit) override final;
	virtual uint64_t api_trait_get(ApiTrait p_trait) override final;
	virtual bool has_feature(Features p_feature) override final;
	virtual const MultiviewCapabilities &get_multiview_capabilities() override final;
	virtual const FragmentShadingRateCapabilities &get_fragment_shading_rate_capabilities() override final;
	virtual const FragmentDensityMapCapabilities &get_fragment_density_map_capabilities() override final;
	virtual String get_api_name() const override final;
	virtual String get_api_version() const override final;
	virtual String get_pipeline_cache_uuid() const override final;
	virtual const Capabilities &get_capabilities() const override final;
	virtual const RenderingShaderContainerFormat &get_shader_container_format() const override final;
};

#endif // WEBGPU_ENABLED
