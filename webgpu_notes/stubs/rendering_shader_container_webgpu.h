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

	struct HeaderData {
		uint32_t push_constant_bind_group = 3;  // Which bind group index holds push constants.
		uint32_t push_constant_binding = 0;     // Which binding within that group.
		uint32_t flags = 0;
	};

	struct StageData {
		uint32_t wgsl_source_size = 0; // Size of WGSL source code.
	};

protected:
	HeaderData header_data;
	LocalVector<StageData> stage_data;
	LocalVector<String> wgsl_sources; // WGSL source per stage.

	// --- RenderingShaderContainer overrides ---

	virtual uint32_t _format() const override { return FORMAT_WEBGPU; }
	virtual uint32_t _format_version() const override { return FORMAT_VERSION; }

	/// Called by set_code_from_spirv() — this is where SPIR-V → WGSL translation happens.
	virtual bool _set_code_from_spirv(const ReflectShader &p_shader) override;

	// Serialization overrides for extra data.
	virtual uint32_t _from_bytes_header_extra_data(const uint8_t *p_bytes) override;
	virtual uint32_t _to_bytes_header_extra_data(uint8_t *p_bytes) const override;
	virtual uint32_t _from_bytes_shader_extra_data_start(const uint8_t *p_bytes) override;
	virtual uint32_t _from_bytes_shader_extra_data(const uint8_t *p_bytes, uint32_t p_index) override;
	virtual uint32_t _to_bytes_shader_extra_data(uint8_t *p_bytes, uint32_t p_index) const override;

public:
	const String &get_wgsl_source(uint32_t p_stage_index) const;
	uint32_t get_push_constant_bind_group() const { return header_data.push_constant_bind_group; }
	uint32_t get_push_constant_binding() const { return header_data.push_constant_binding; }

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
		// SPIR-V 1.0 — maximum compatibility with Tint.
		return SHADER_SPIRV_VERSION_1_0;
	}
};

#endif // WEBGPU_ENABLED
