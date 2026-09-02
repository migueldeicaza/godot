/**************************************************************************/
/*  tint_wrapper.cpp                                                      */
/**************************************************************************/
/*                       This file is part of:                            */
/*                           GODOT ENGINE                                 */
/*                      https://godotengine.org                           */
/**************************************************************************/
/* Compiled with C++20 in the Tint build environment.  Wraps Tint's       */
/* SPIR-V reader + WGSL writer behind a simple C-compatible interface     */
/* so that the main Godot driver code (C++17) never includes Tint headers.*/
/**************************************************************************/

#include "tint_wrapper.h"

#include "src/tint/api/tint.h"
#include "src/tint/lang/wgsl/writer/common/options.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

void tint_wrapper_initialize() {
	tint::Initialize();
}

char *tint_wrapper_spirv_to_wgsl(const uint32_t *p_spirv_words, size_t p_word_count, char **r_error) {
	std::vector<uint32_t> words(p_spirv_words, p_spirv_words + p_word_count);

	// Allow all WGSL extensions and language features so Tint can emit
	// constructs like readonly storage textures without validation errors.
	tint::wgsl::writer::Options wgsl_options;
	wgsl_options.allowed_features = tint::wgsl::AllowedFeatures::Everything();
	// Godot's GLSL shaders use textureSample/dpdx in non-uniform control flow
	// (valid in Vulkan, but WGSL requires uniform control flow for derivatives).
	// This inserts `diagnostic(off, derivative_uniformity)` in the output.
	wgsl_options.allow_non_uniform_derivatives = true;

	auto result = tint::SpirvToWgsl(words, wgsl_options);
	if (result != tint::Success) {
		if (r_error) {
			const std::string &reason = result.Failure().reason;
			char *err = (char *)malloc(reason.size() + 1);
			if (err) {
				memcpy(err, reason.c_str(), reason.size() + 1);
			}
			*r_error = err;
		}
		return nullptr;
	}

	const std::string &wgsl = result.Get();
	char *out = (char *)malloc(wgsl.size() + 1);
	if (!out) {
		return nullptr;
	}
	memcpy(out, wgsl.c_str(), wgsl.size() + 1);
	return out;
}
