/**************************************************************************/
/*  naga_bridge.h                                                         */
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

#ifndef NAGA_BRIDGE_H
#define NAGA_BRIDGE_H

#include "servers/rendering/rendering_device_commons.h"

class NagaShaderModule {
	void *module = nullptr;

public:
	struct Binding {
		uint32_t group = 0;
		uint32_t binding = 0;
		int32_t buffer = -1;
		int32_t texture = -1;
		int32_t sampler = -1;
		bool writable = false;
	};

	NagaShaderModule() = default;
	~NagaShaderModule();

	NagaShaderModule(const NagaShaderModule &) = delete;
	NagaShaderModule &operator=(const NagaShaderModule &) = delete;

	static String preprocess_for_glslang(const String &p_source, String &r_error);
	bool parse(RenderingDeviceCommons::ShaderStage p_stage, const String &p_source, String &r_error);
	bool parse_spirv(RenderingDeviceCommons::ShaderStage p_stage, const Vector<uint8_t> &p_spirv, String &r_error);
	Vector<uint8_t> write_spirv(String &r_error) const;
	String write_msl(uint32_t p_msl_major, uint32_t p_msl_minor, const Vector<Binding> &p_bindings, int32_t p_push_constant_buffer, String &r_entry_point, String &r_error) const;
};

#endif // NAGA_BRIDGE_H
