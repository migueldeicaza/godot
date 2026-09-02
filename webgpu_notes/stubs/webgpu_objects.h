/**************************************************************************/
/*  webgpu_objects.h                                                      */
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

#include "servers/rendering/rendering_device_driver.h"

#include <webgpu/webgpu.h>

using RDD = RenderingDeviceDriver;

// =============================================================================
// Buffer
// =============================================================================

struct WGBuffer {
	WGPUBuffer handle = nullptr;
	uint64_t size = 0;
	WGPUBufferUsage usage = 0;
	uint8_t *shadow_map = nullptr; // For CPU-side mapping emulation.
	bool map_dirty = false;
};

// =============================================================================
// Texture
// =============================================================================

struct WGTexture {
	WGPUTexture handle = nullptr;
	WGPUTextureView default_view = nullptr;
	WGPUTextureFormat format = WGPUTextureFormat_Undefined;
	WGPUTextureDimension dimension = WGPUTextureDimension_2D;
	WGPUTextureViewDimension view_dimension = WGPUTextureViewDimension_2D;
	uint32_t width = 0, height = 0, depth = 1;
	uint32_t mipmaps = 1;
	uint32_t layers = 1;
	uint32_t sample_count = 1;
	WGPUTextureUsage usage = 0;
	bool is_from_swap_chain = false;
};

// =============================================================================
// Sampler (stored directly as WGPUSampler handle in SamplerID)
// =============================================================================

// =============================================================================
// Vertex Format
// =============================================================================

struct WGVertexFormat {
	struct Attribute {
		uint32_t location = 0;
		WGPUVertexFormat format = (WGPUVertexFormat)0;
		uint64_t offset = 0;
		uint32_t binding = 0;
	};
	struct Binding {
		uint32_t stride = 0;
		WGPUVertexStepMode step_mode = WGPUVertexStepMode_Vertex;
	};
	LocalVector<Attribute> attributes;
	LocalVector<Binding> bindings;
};

// =============================================================================
// Shader
// =============================================================================

struct WGShader {
	WGPUShaderModule module = nullptr;
	WGPUPipelineLayout pipeline_layout = nullptr;
	LocalVector<WGPUBindGroupLayout> bind_group_layouts;

	// Push constant emulation.
	uint32_t push_constant_size = 0;
	uint32_t push_constant_bind_group = 3; // Default: bind group 3.
	uint32_t push_constant_binding = 0;    // Binding 0 within that group.
	BitField<RDD::ShaderStage> push_constant_stages;

	// Reflection data for uniform set creation.
	struct BindGroupEntry {
		WGPUBindGroupLayoutEntry layout_entry = {};
		RDD::UniformType godot_type = RDD::UNIFORM_TYPE_MAX;
	};
	struct BindGroupInfo {
		LocalVector<BindGroupEntry> entries;
	};
	LocalVector<BindGroupInfo> bind_group_infos;

	String name;
};

// =============================================================================
// Render Pass (metadata — no WebGPU object)
// =============================================================================

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

// =============================================================================
// Framebuffer (metadata — no WebGPU object)
// =============================================================================

struct WGFramebuffer {
	LocalVector<WGTexture *> attachments;
	LocalVector<WGPUTextureView> attachment_views;
	uint32_t width = 0, height = 0;
	WGRenderPass *render_pass = nullptr;
};

// =============================================================================
// Pipeline
// =============================================================================

struct WGPipelineWrapper {
	enum Type { RENDER, COMPUTE };
	Type type = RENDER;
	union {
		WGPURenderPipeline render_handle;
		WGPUComputePipeline compute_handle;
	};
	WGShader *shader = nullptr;

	WGPipelineWrapper() : render_handle(nullptr) {}
};

// =============================================================================
// Uniform Set (Bind Group)
// =============================================================================

struct WGUniformSet {
	WGPUBindGroup handle = nullptr;
	uint32_t set_index = 0;
};

// =============================================================================
// Swap Chain
// =============================================================================

struct WGSwapChain {
	WGPUSurface surface = nullptr;
	WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;
	uint32_t width = 0, height = 0;
	WGRenderPass *render_pass = nullptr;
	bool configured = false;
};

// =============================================================================
// Command Queue / Pool / Buffer
// =============================================================================

struct WGCommandQueue {
	WGPUQueue queue = nullptr;
};

struct WGCommandPool {
	RDD::CommandBufferType buffer_type = RDD::COMMAND_BUFFER_TYPE_PRIMARY;
};

struct WGCommandBuffer {
	WGPUCommandEncoder encoder = nullptr;
	WGPUCommandBuffer finished_buffer = nullptr;

	// Current active encoder states.
	WGPURenderPassEncoder render_encoder = nullptr;
	WGPUComputePassEncoder compute_encoder = nullptr;

	enum ActiveEncoder { NONE, RENDER, COMPUTE };
	ActiveEncoder active_encoder = NONE;

	// Push constant emulation state.
	static constexpr uint32_t MAX_PUSH_CONSTANT_SIZE = 128;
	uint8_t push_constant_data[MAX_PUSH_CONSTANT_SIZE] = {};
	uint32_t push_constant_data_len = 0;
	bool push_constants_dirty = false;

	// Render state tracking.
	struct RenderState {
		WGRenderPass *render_pass = nullptr;
		WGFramebuffer *framebuffer = nullptr;
		uint32_t current_subpass = 0;
		WGPipelineWrapper *current_pipeline = nullptr;
	} render_state;

	// Helpers.
	void end_active_encoder() {
		if (render_encoder) {
			wgpuRenderPassEncoderEnd(render_encoder);
			wgpuRenderPassEncoderRelease(render_encoder);
			render_encoder = nullptr;
		}
		if (compute_encoder) {
			wgpuComputePassEncoderEnd(compute_encoder);
			wgpuComputePassEncoderRelease(compute_encoder);
			compute_encoder = nullptr;
		}
		active_encoder = NONE;
	}
};

// =============================================================================
// Fence
// =============================================================================

struct WGFence {
	bool signaled = false;
	uint64_t submission_id = 0;
};

// =============================================================================
// Semaphore (mostly no-op on WebGPU — single queue)
// =============================================================================

struct WGSemaphore {};

// =============================================================================
// Query Pool
// =============================================================================

struct WGQueryPool {
	WGPUQuerySet handle = nullptr;
	WGPUBuffer resolve_buffer = nullptr;
	uint32_t count = 0;
	bool available = false;
};

#endif // WEBGPU_ENABLED
