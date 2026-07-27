/**************************************************************************/
/*  naga_bridge.cpp                                                       */
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

#include "naga_bridge.h"

#include "core/os/os.h"
#include "core/string/ustring.h"

static thread_local NagaShaderModule::Timing naga_timing;

extern "C" {
struct GodotNagaBinding {
	uint32_t group;
	uint32_t binding;
	int32_t buffer;
	int32_t texture;
	int32_t sampler;
	uint8_t writable;
};

struct GodotNagaBytes {
	uint8_t *data;
	size_t length;
};

struct GodotNagaUniformReflection {
	uint32_t group;
	uint32_t binding;
	uint32_t kind;
	uint32_t length;
	uint32_t writable;
	uint32_t active;
	uint32_t image_dimension;
	uint32_t image_format;
	uint32_t image_arrayed;
	uint32_t image_multisampled;
};
}

static RenderingDeviceCommons::DataFormat naga_image_format_to_data_format(uint32_t p_format) {
	using Naga = NagaShaderModule;
	using RDC = RenderingDeviceCommons;
	switch (p_format) {
		case Naga::REFLECTION_IMAGE_FORMAT_R8_UNORM:
			return RDC::DATA_FORMAT_R8_UNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_R8_SNORM:
			return RDC::DATA_FORMAT_R8_SNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_R8_UINT:
			return RDC::DATA_FORMAT_R8_UINT;
		case Naga::REFLECTION_IMAGE_FORMAT_R8_SINT:
			return RDC::DATA_FORMAT_R8_SINT;
		case Naga::REFLECTION_IMAGE_FORMAT_R16_UINT:
			return RDC::DATA_FORMAT_R16_UINT;
		case Naga::REFLECTION_IMAGE_FORMAT_R16_SINT:
			return RDC::DATA_FORMAT_R16_SINT;
		case Naga::REFLECTION_IMAGE_FORMAT_R16_FLOAT:
			return RDC::DATA_FORMAT_R16_SFLOAT;
		case Naga::REFLECTION_IMAGE_FORMAT_RG8_UNORM:
			return RDC::DATA_FORMAT_R8G8_UNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_RG8_SNORM:
			return RDC::DATA_FORMAT_R8G8_SNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_RG8_UINT:
			return RDC::DATA_FORMAT_R8G8_UINT;
		case Naga::REFLECTION_IMAGE_FORMAT_RG8_SINT:
			return RDC::DATA_FORMAT_R8G8_SINT;
		case Naga::REFLECTION_IMAGE_FORMAT_R32_UINT:
			return RDC::DATA_FORMAT_R32_UINT;
		case Naga::REFLECTION_IMAGE_FORMAT_R32_SINT:
			return RDC::DATA_FORMAT_R32_SINT;
		case Naga::REFLECTION_IMAGE_FORMAT_R32_FLOAT:
			return RDC::DATA_FORMAT_R32_SFLOAT;
		case Naga::REFLECTION_IMAGE_FORMAT_RG16_UINT:
			return RDC::DATA_FORMAT_R16G16_UINT;
		case Naga::REFLECTION_IMAGE_FORMAT_RG16_SINT:
			return RDC::DATA_FORMAT_R16G16_SINT;
		case Naga::REFLECTION_IMAGE_FORMAT_RG16_FLOAT:
			return RDC::DATA_FORMAT_R16G16_SFLOAT;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA8_UNORM:
			return RDC::DATA_FORMAT_R8G8B8A8_UNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA8_SNORM:
			return RDC::DATA_FORMAT_R8G8B8A8_SNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA8_UINT:
			return RDC::DATA_FORMAT_R8G8B8A8_UINT;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA8_SINT:
			return RDC::DATA_FORMAT_R8G8B8A8_SINT;
		case Naga::REFLECTION_IMAGE_FORMAT_BGRA8_UNORM:
			return RDC::DATA_FORMAT_B8G8R8A8_UNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_RGB10A2_UINT:
			return RDC::DATA_FORMAT_A2B10G10R10_UINT_PACK32;
		case Naga::REFLECTION_IMAGE_FORMAT_RGB10A2_UNORM:
			return RDC::DATA_FORMAT_A2B10G10R10_UNORM_PACK32;
		case Naga::REFLECTION_IMAGE_FORMAT_RG11B10_UFLOAT:
			return RDC::DATA_FORMAT_B10G11R11_UFLOAT_PACK32;
		case Naga::REFLECTION_IMAGE_FORMAT_R64_UINT:
			return RDC::DATA_FORMAT_R64_UINT;
		case Naga::REFLECTION_IMAGE_FORMAT_RG32_UINT:
			return RDC::DATA_FORMAT_R32G32_UINT;
		case Naga::REFLECTION_IMAGE_FORMAT_RG32_SINT:
			return RDC::DATA_FORMAT_R32G32_SINT;
		case Naga::REFLECTION_IMAGE_FORMAT_RG32_FLOAT:
			return RDC::DATA_FORMAT_R32G32_SFLOAT;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA16_UINT:
			return RDC::DATA_FORMAT_R16G16B16A16_UINT;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA16_SINT:
			return RDC::DATA_FORMAT_R16G16B16A16_SINT;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA16_FLOAT:
			return RDC::DATA_FORMAT_R16G16B16A16_SFLOAT;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA32_UINT:
			return RDC::DATA_FORMAT_R32G32B32A32_UINT;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA32_SINT:
			return RDC::DATA_FORMAT_R32G32B32A32_SINT;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA32_FLOAT:
			return RDC::DATA_FORMAT_R32G32B32A32_SFLOAT;
		case Naga::REFLECTION_IMAGE_FORMAT_R16_UNORM:
			return RDC::DATA_FORMAT_R16_UNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_R16_SNORM:
			return RDC::DATA_FORMAT_R16_SNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_RG16_UNORM:
			return RDC::DATA_FORMAT_R16G16_UNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_RG16_SNORM:
			return RDC::DATA_FORMAT_R16G16_SNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA16_UNORM:
			return RDC::DATA_FORMAT_R16G16B16A16_UNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_RGBA16_SNORM:
			return RDC::DATA_FORMAT_R16G16B16A16_SNORM;
		case Naga::REFLECTION_IMAGE_FORMAT_NONE:
			return RDC::DATA_FORMAT_MAX;
	}
	return RDC::DATA_FORMAT_MAX;
}

extern "C" {
struct GodotNagaSpecializationReflection {
	uint32_t kind;
	uint32_t constant_id;
	uint32_t default_value;
};

struct GodotNagaReflection {
	uint32_t stage;
	uint64_t vertex_input_mask;
	uint32_t fragment_output_mask;
	uint32_t push_constant_size;
	uint32_t has_multiview;
	uint32_t compute_local_size[3];
	GodotNagaUniformReflection *uniforms;
	size_t uniform_count;
	GodotNagaSpecializationReflection *specialization_constants;
	size_t specialization_constant_count;
};

void *godot_naga_parse(uint32_t p_stage, const char *p_source, char **r_error);
char *godot_naga_preprocess_for_glslang(const char *p_source, char **r_error);
void *godot_naga_parse_spirv(uint32_t p_stage, const uint8_t *p_spirv, size_t p_length, char **r_error);
uint8_t godot_naga_reflect(const void *p_module, GodotNagaReflection *r_reflection, char **r_error);
void godot_naga_reflection_free(GodotNagaReflection *p_reflection);
GodotNagaBytes godot_naga_write_spirv(const void *p_module, char **r_error);
char *godot_naga_write_msl(const void *p_module, uint8_t p_msl_major, uint8_t p_msl_minor, const GodotNagaBinding *p_bindings, size_t p_binding_count, int32_t p_push_constant_buffer, char **r_entry_point, char **r_error);
void godot_naga_module_free(void *p_module);
void godot_naga_bytes_free(GodotNagaBytes p_bytes);
void godot_naga_string_free(char *p_string);
}

static String take_naga_string(char *p_string) {
	if (p_string == nullptr) {
		return String();
	}
	String result = String::utf8(p_string);
	godot_naga_string_free(p_string);
	return result;
}

NagaShaderModule::~NagaShaderModule() {
	if (module != nullptr) {
		godot_naga_module_free(module);
	}
}

String NagaShaderModule::preprocess_for_glslang(const String &p_source, String &r_error) {
	const uint64_t start = OS::get_singleton()->get_ticks_usec();
	CharString source = p_source.utf8();
	char *error = nullptr;
	char *preprocessed = godot_naga_preprocess_for_glslang(source.get_data(), &error);
	r_error = take_naga_string(error);
	String result = take_naga_string(preprocessed);
	naga_timing.fallback_usec += OS::get_singleton()->get_ticks_usec() - start;
	return result;
}

bool NagaShaderModule::parse(RenderingDeviceCommons::ShaderStage p_stage, const String &p_source, String &r_error) {
	ERR_FAIL_COND_V(module != nullptr, false);
	const uint64_t start = OS::get_singleton()->get_ticks_usec();
	CharString source = p_source.utf8();
	char *error = nullptr;
	module = godot_naga_parse(uint32_t(p_stage), source.get_data(), &error);
	r_error = take_naga_string(error);
	naga_timing.parse_usec += OS::get_singleton()->get_ticks_usec() - start;
	naga_timing.parse_count++;
	return module != nullptr;
}

bool NagaShaderModule::parse_spirv(RenderingDeviceCommons::ShaderStage p_stage, const Vector<uint8_t> &p_spirv, String &r_error) {
	ERR_FAIL_COND_V(module != nullptr, false);
	const uint64_t start = OS::get_singleton()->get_ticks_usec();
	char *error = nullptr;
	module = godot_naga_parse_spirv(uint32_t(p_stage), p_spirv.ptr(), p_spirv.size(), &error);
	r_error = take_naga_string(error);
	naga_timing.fallback_usec += OS::get_singleton()->get_ticks_usec() - start;
	naga_timing.fallback_count++;
	return module != nullptr;
}

bool NagaShaderModule::reflect(Reflection &r_reflection, String &r_error) const {
	ERR_FAIL_NULL_V(module, false);
	const uint64_t start = OS::get_singleton()->get_ticks_usec();
	GodotNagaReflection reflection = {};
	char *error = nullptr;
	bool success = godot_naga_reflect(module, &reflection, &error);
	r_error = take_naga_string(error);
	if (!success) {
		godot_naga_reflection_free(&reflection);
		naga_timing.reflect_usec += OS::get_singleton()->get_ticks_usec() - start;
		naga_timing.reflect_count++;
		return false;
	}

	r_reflection.stage = RenderingDeviceCommons::ShaderStage(reflection.stage);
	r_reflection.vertex_input_mask = reflection.vertex_input_mask;
	r_reflection.fragment_output_mask = reflection.fragment_output_mask;
	r_reflection.push_constant_size = reflection.push_constant_size;
	r_reflection.has_multiview = reflection.has_multiview;
	for (uint32_t i = 0; i < 3; i++) {
		r_reflection.compute_local_size[i] = reflection.compute_local_size[i];
	}
	r_reflection.uniforms.resize(reflection.uniform_count);
	for (uint32_t i = 0; i < reflection.uniform_count; i++) {
		const GodotNagaUniformReflection &source = reflection.uniforms[i];
		ReflectionUniform &target = r_reflection.uniforms.write[i];
		target.group = source.group;
		target.binding = source.binding;
		target.kind = ReflectionUniformKind(source.kind);
		target.length = source.length;
		target.writable = source.writable;
		target.active = source.active;
		target.image_dimension = ReflectionImageDimension(source.image_dimension);
		target.image_format = naga_image_format_to_data_format(source.image_format);
		target.image_arrayed = source.image_arrayed;
		target.image_multisampled = source.image_multisampled;
	}
	r_reflection.specialization_constants.resize(reflection.specialization_constant_count);
	for (uint32_t i = 0; i < reflection.specialization_constant_count; i++) {
		const GodotNagaSpecializationReflection &source = reflection.specialization_constants[i];
		ReflectionSpecialization &target = r_reflection.specialization_constants.write[i];
		target.kind = ReflectionSpecializationKind(source.kind);
		target.constant_id = source.constant_id;
		target.default_value = source.default_value;
	}
	godot_naga_reflection_free(&reflection);
	naga_timing.reflect_usec += OS::get_singleton()->get_ticks_usec() - start;
	naga_timing.reflect_count++;
	return true;
}

Vector<uint8_t> NagaShaderModule::write_spirv(String &r_error) const {
	ERR_FAIL_NULL_V(module, Vector<uint8_t>());
	const uint64_t start = OS::get_singleton()->get_ticks_usec();
	char *error = nullptr;
	GodotNagaBytes bytes = godot_naga_write_spirv(module, &error);
	r_error = take_naga_string(error);
	Vector<uint8_t> result;
	if (bytes.data != nullptr && bytes.length > 0) {
		result.resize(bytes.length);
		memcpy(result.ptrw(), bytes.data, bytes.length);
	}
	godot_naga_bytes_free(bytes);
	naga_timing.fallback_usec += OS::get_singleton()->get_ticks_usec() - start;
	naga_timing.fallback_count++;
	return result;
}

String NagaShaderModule::write_msl(uint32_t p_msl_major, uint32_t p_msl_minor, const Vector<Binding> &p_bindings, int32_t p_push_constant_buffer, String &r_entry_point, String &r_error) const {
	ERR_FAIL_NULL_V(module, String());
	const uint64_t start = OS::get_singleton()->get_ticks_usec();
	Vector<GodotNagaBinding> bindings;
	bindings.resize(p_bindings.size());
	for (uint32_t i = 0; i < p_bindings.size(); i++) {
		const Binding &source = p_bindings[i];
		GodotNagaBinding &target = bindings.write[i];
		target.group = source.group;
		target.binding = source.binding;
		target.buffer = source.buffer;
		target.texture = source.texture;
		target.sampler = source.sampler;
		target.writable = source.writable;
	}

	char *entry_point = nullptr;
	char *error = nullptr;
	char *msl = godot_naga_write_msl(module, p_msl_major, p_msl_minor, bindings.ptr(), bindings.size(), p_push_constant_buffer, &entry_point, &error);
	r_entry_point = take_naga_string(entry_point);
	r_error = take_naga_string(error);
	String result = take_naga_string(msl);
	naga_timing.write_msl_usec += OS::get_singleton()->get_ticks_usec() - start;
	naga_timing.write_msl_count++;
	return result;
}

NagaShaderModule::Timing NagaShaderModule::get_timing() {
	return naga_timing;
}

void NagaShaderModule::reset_timing() {
	naga_timing = Timing();
}
