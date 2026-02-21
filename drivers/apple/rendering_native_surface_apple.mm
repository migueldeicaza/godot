/**************************************************************************/
/*  rendering_native_surface_apple.mm                                     */
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

#include "rendering_native_surface_apple.h"

void RenderingNativeSurfaceApple::_bind_methods() {
	ClassDB::bind_static_method("RenderingNativeSurfaceApple", D_METHOD("create", "layer"), &RenderingNativeSurfaceApple::create_api);
	ClassDB::bind_method(D_METHOD("get_layer"), &RenderingNativeSurfaceApple::get_layer);
}

Ref<RenderingNativeSurfaceApple> RenderingNativeSurfaceApple::create_api(uint64_t p_layer) {
	Ref<RenderingNativeSurfaceApple> result = memnew(RenderingNativeSurfaceApple);
	result->set_layer(p_layer);
	return result;
}

RenderingNativeSurfaceApple::RenderingNativeSurfaceApple() {
}

RenderingNativeSurfaceApple::~RenderingNativeSurfaceApple() {
}
