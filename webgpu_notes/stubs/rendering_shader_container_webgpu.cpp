/**************************************************************************/
/*  rendering_shader_container_webgpu.cpp                                 */
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

#include "rendering_shader_container_webgpu.h"

// =========================================================================
// SPIR-V → WGSL Translation
// =========================================================================
//
// This is the core translation pipeline. Options:
//   (A) Google Tint (from Dawn) — recommended, best WGSL output.
//   (B) Naga (Rust, from wgpu) — would require FFI.
//   (C) SPIRV-Cross — does NOT have a WGSL backend, cannot use.
//
// Integration approach:
//   1. At BUILD/EXPORT time: Cross-compile SPIR-V → WGSL using Tint.
//   2. Store WGSL source in the shader container (alongside reflection data).
//   3. At RUNTIME (browser): Load pre-compiled WGSL, no translation needed.
//
// Push constant handling during translation:
//   - Detect push_constant block in SPIR-V.
//   - Rewrite to uniform buffer at @group(N) @binding(M).
//   - Store N and M in header_data for runtime use.
//
// Specialization constant handling:
//   - SPIR-V specialization constants → WGSL pipeline-overridable constants.
//   - layout(constant_id = N) → @id(N) override constName: Type = default;
//
// subpassInput handling:
//   - SPIR-V subpassInput → WGSL texture_2d (sample from previous pass output).
//   - subpassLoad() → textureSample() or textureLoad().
//

bool RenderingShaderContainerWebGPU::_set_code_from_spirv(const ReflectShader &p_shader) {
	// This method is called during shader compilation (build/export time).
	// It receives fully reflected SPIR-V data and must produce WGSL.

	wgsl_sources.clear();
	stage_data.clear();

	for (uint32_t i = 0; i < p_shader.shader_stages.size(); i++) {
		const ReflectShaderStage &stage = p_shader.shader_stages[i];
		Span<uint32_t> spirv = stage.spirv();

		// TODO: Use Tint to translate SPIR-V → WGSL.
		//
		// Tint integration sketch:
		//   #include "src/tint/api/tint.h"
		//
		//   tint::spirv::reader::Options reader_options;
		//   reader_options.allowed_features = tint::wgsl::AllowedFeatures::Everything();
		//
		//   tint::Program program = tint::spirv::reader::Read(spirv.ptr(), spirv.size(), reader_options);
		//   if (!program.IsValid()) {
		//       ERR_FAIL_V_MSG(false, vformat("Tint SPIR-V read error: %s", program.Diagnostics().Str().c_str()));
		//   }
		//
		//   // Apply transforms:
		//   // 1. Push constant → uniform buffer rewrite.
		//   // 2. subpassInput → texture rewrite.
		//   // 3. Rename entry points if needed.
		//
		//   tint::wgsl::writer::Options writer_options;
		//   auto result = tint::wgsl::writer::Generate(program, writer_options);
		//   if (result != tint::Success) {
		//       ERR_FAIL_V_MSG(false, "Tint WGSL generation failed.");
		//   }
		//
		//   String wgsl = String::utf8(result->wgsl.c_str());

		// PLACEHOLDER: Store empty WGSL until Tint is integrated.
		String wgsl = "// TODO: SPIR-V → WGSL translation not yet implemented.\n";
		wgsl += vformat("// Stage: %d, SPIR-V size: %d bytes\n", (int)stage.shader_stage, spirv.size() * 4);

		wgsl_sources.push_back(wgsl);

		StageData sd;
		sd.wgsl_source_size = wgsl.utf8().length();
		stage_data.push_back(sd);
	}

	// Record push constant location.
	if (p_shader.push_constant_size > 0) {
		header_data.push_constant_bind_group = 3; // Convention: push constants go in group 3.
		header_data.push_constant_binding = 0;
	}

	// Store shader code in the container format.
	shaders.resize(wgsl_sources.size());
	for (uint32_t i = 0; i < wgsl_sources.size(); i++) {
		CharString utf8 = wgsl_sources[i].utf8();
		shaders.write[i].shader_stage = p_shader.shader_stages[i].shader_stage;
		shaders.write[i].code_decompressed_size = utf8.length();
		shaders.write[i].code_compression_flags = 0;
		shaders.write[i].code_compressed_bytes.resize(utf8.length());
		memcpy(shaders.write[i].code_compressed_bytes.ptrw(), utf8.get_data(), utf8.length());
	}

	return true;
}

// =========================================================================
// Serialization
// =========================================================================

uint32_t RenderingShaderContainerWebGPU::_from_bytes_header_extra_data(const uint8_t *p_bytes) {
	if (p_bytes) {
		memcpy(&header_data, p_bytes, sizeof(HeaderData));
	}
	return sizeof(HeaderData);
}

uint32_t RenderingShaderContainerWebGPU::_to_bytes_header_extra_data(uint8_t *p_bytes) const {
	if (p_bytes) {
		memcpy(p_bytes, &header_data, sizeof(HeaderData));
	}
	return sizeof(HeaderData);
}

uint32_t RenderingShaderContainerWebGPU::_from_bytes_shader_extra_data_start(const uint8_t *p_bytes) {
	stage_data.resize(reflection_data.stage_count);
	wgsl_sources.resize(reflection_data.stage_count);
	return 0;
}

uint32_t RenderingShaderContainerWebGPU::_from_bytes_shader_extra_data(const uint8_t *p_bytes, uint32_t p_index) {
	if (p_bytes) {
		memcpy(&stage_data[p_index], p_bytes, sizeof(StageData));
	}
	return sizeof(StageData);
}

uint32_t RenderingShaderContainerWebGPU::_to_bytes_shader_extra_data(uint8_t *p_bytes, uint32_t p_index) const {
	if (p_bytes) {
		memcpy(p_bytes, &stage_data[p_index], sizeof(StageData));
	}
	return sizeof(StageData);
}

// =========================================================================
// Public API
// =========================================================================

const String &RenderingShaderContainerWebGPU::get_wgsl_source(uint32_t p_stage_index) const {
	ERR_FAIL_INDEX_V(p_stage_index, wgsl_sources.size(), wgsl_sources[0]);
	return wgsl_sources[p_stage_index];
}

RenderingShaderContainerWebGPU::RenderingShaderContainerWebGPU() {
}

RenderingShaderContainerWebGPU::~RenderingShaderContainerWebGPU() {
}

#endif // WEBGPU_ENABLED
