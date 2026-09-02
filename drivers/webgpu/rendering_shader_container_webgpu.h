/**************************************************************************/
/*  rendering_shader_container_webgpu.h                                   */
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

#include "servers/rendering/rendering_shader_container.h"

class RenderingShaderContainerWebGPU : public RenderingShaderContainer {
	GDSOFTCLASS(RenderingShaderContainerWebGPU, RenderingShaderContainer);

public:
	// Format identifier for WebGPU shader containers.
	static constexpr uint32_t FORMAT_WEBGPU = 0x57475055; // "WGPU"
	static constexpr uint32_t FORMAT_VERSION = 1;
	static constexpr uint32_t NO_PUSH_CONSTANTS = UINT32_MAX;

	struct HeaderData {
		uint32_t push_constant_bind_group = NO_PUSH_CONSTANTS; // UINT32_MAX = no push constants.
		uint32_t push_constant_binding = 0;
		uint32_t flags = 0;
	};

protected:
	HeaderData header_data;

	// --- RenderingShaderContainer overrides ---

	virtual uint32_t _format() const override { return FORMAT_WEBGPU; }
	virtual uint32_t _format_version() const override { return FORMAT_VERSION; }

	/// Called by set_code_from_spirv() — stores raw SPIR-V bytes per stage.
	/// Dawn's WebGPU implementation supports WGPUShaderSourceSPIRV natively;
	/// no WGSL/Tint translation step is needed.
	virtual bool _set_code_from_spirv(const ReflectShader &p_shader) override;

	// Serialization overrides for extra header data.
	virtual uint32_t _from_bytes_header_extra_data(const uint8_t *p_bytes) override;
	virtual uint32_t _to_bytes_header_extra_data(uint8_t *p_bytes) const override;

public:
	uint32_t get_push_constant_bind_group() const { return header_data.push_constant_bind_group; }
	uint32_t get_push_constant_binding() const { return header_data.push_constant_binding; }
	bool has_push_constants() const { return header_data.push_constant_bind_group != NO_PUSH_CONSTANTS; }

	RenderingShaderContainerWebGPU();
	virtual ~RenderingShaderContainerWebGPU();
};

// =============================================================================
// Format Factory
// =============================================================================

class RenderingShaderContainerFormatWebGPU : public RenderingShaderContainerFormat {
	GDSOFTCLASS(RenderingShaderContainerFormatWebGPU, RenderingShaderContainerFormat);

public:
	virtual Ref<RenderingShaderContainer> create_container() const override {
		return Ref<RenderingShaderContainerWebGPU>(memnew(RenderingShaderContainerWebGPU));
	}

	virtual ShaderLanguageVersion get_shader_language_version() const override {
		// Vulkan-flavour GLSL 1.1 — same as the Vulkan driver.
		return SHADER_LANGUAGE_VULKAN_VERSION_1_1;
	}

	virtual ShaderSpirvVersion get_shader_spirv_version() const override {
		// SPIR-V 1.3 — required so glslang emits SSBOs as StorageClass::StorageBuffer
		// (not the old-style StorageClass::Uniform + BufferBlock used in SPIR-V 1.0).
		// Tint correctly converts StorageClass::StorageBuffer → var<storage, read/read_write>.
		// SPIR-V 1.3 requires Vulkan 1.1 client, which matches our language version.
		return SHADER_SPIRV_VERSION_1_3;
	}
};

#endif // WEBGPU_ENABLED
