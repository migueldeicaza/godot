/**************************************************************************/
/*  libgodot_ios.mm                                                       */
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

#include "core/extension/libgodot.h"

#include "core/extension/godot_instance.h"
#include "core/object/class_db.h"
#include "drivers/apple_embedded/display_server_apple_embedded.h"
#include "main/main.h"

#import "display_layer_ios.h"
#import <UIKit/UIKit.h>
#include "os_ios.h"

static OS_IOS *os = nullptr;
static GodotInstance *instance = nullptr;
static bool apple_embedded_class_registered = false;

// SDL's iOS platform checks are referenced by the static library build.
extern "C" bool SDL_IsIPad(void) {
	return UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad;
}

extern "C" bool SDL_IsAppleTV(void) {
	return UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomTV;
}

static CALayer<GDTDisplayLayer> *_get_rendering_layer(void *p_rendering_layer) {
	ERR_FAIL_NULL_V(p_rendering_layer, nullptr);
	id object = (__bridge id)p_rendering_layer;
	ERR_FAIL_COND_V_MSG(![object conformsToProtocol:@protocol(GDTDisplayLayer)], nullptr, "The rendering layer does not implement GDTDisplayLayer.");
	return (CALayer<GDTDisplayLayer> *)object;
}

void *libgodot_ios_create_rendering_layer(const char *p_rendering_driver) {
	ERR_FAIL_NULL_V(p_rendering_driver, nullptr);
	ERR_FAIL_COND_V_MSG(![NSThread isMainThread], nullptr, "Rendering layers must be created on the main thread.");

	NSString *driver = [NSString stringWithUTF8String:p_rendering_driver];
	CALayer<GDTDisplayLayer> *layer = nullptr;
	if ([driver isEqualToString:@"metal"] || [driver isEqualToString:@"vulkan"]) {
		layer = [GDTMetalLayer layer];
	} else if ([driver isEqualToString:@"opengl3"]) {
		GODOT_CLANG_WARNING_PUSH_AND_IGNORE("-Wdeprecated-declarations")
		layer = [GDTOpenGLLayer layer];
		GODOT_CLANG_WARNING_POP
	}

	return (__bridge_retained void *)layer;
}

void libgodot_ios_initialize_rendering_layer(void *p_rendering_layer) {
	ERR_FAIL_COND_MSG(![NSThread isMainThread], "Rendering layers must be initialized on the main thread.");
	CALayer<GDTDisplayLayer> *layer = _get_rendering_layer(p_rendering_layer);
	ERR_FAIL_NULL(layer);
	[layer initializeDisplayLayer];
}

void libgodot_ios_layout_rendering_layer(void *p_rendering_layer) {
	ERR_FAIL_COND_MSG(![NSThread isMainThread], "Rendering layers must be laid out on the main thread.");
	CALayer<GDTDisplayLayer> *layer = _get_rendering_layer(p_rendering_layer);
	ERR_FAIL_NULL(layer);
	[layer layoutDisplayLayer];
}

void libgodot_ios_start_rendering_layer(void *p_rendering_layer) {
	CALayer<GDTDisplayLayer> *layer = _get_rendering_layer(p_rendering_layer);
	ERR_FAIL_NULL(layer);
	[layer startRenderDisplayLayer];
}

void libgodot_ios_stop_rendering_layer(void *p_rendering_layer) {
	CALayer<GDTDisplayLayer> *layer = _get_rendering_layer(p_rendering_layer);
	ERR_FAIL_NULL(layer);
	[layer stopRenderDisplayLayer];
}

GDExtensionObjectPtr libgodot_create_godot_instance(int p_argc, char *p_argv[], GDExtensionInitializationFunction p_init_func) {
	ERR_FAIL_COND_V_MSG(instance != nullptr, nullptr, "Only one Godot Instance may be created.");

	os = new OS_IOS();

	Error err = Main::setup(p_argv[0], p_argc - 1, &p_argv[1], false);
	if (err != OK) {
		memdelete(os);
		os = nullptr;
		return nullptr;
	}

	if (!apple_embedded_class_registered) {
		ClassDB::register_abstract_class<DisplayServerAppleEmbedded>();
		apple_embedded_class_registered = true;
	}

	instance = memnew(GodotInstance);
	if (!instance->initialize(p_init_func)) {
		memdelete(instance);
		instance = nullptr;
		Main::cleanup();
		memdelete(os);
		os = nullptr;
		return nullptr;
	}

	return (GDExtensionObjectPtr)instance;
}

void libgodot_destroy_godot_instance(GDExtensionObjectPtr p_godot_instance) {
	GodotInstance *godot_instance = (GodotInstance *)p_godot_instance;
	if (instance == godot_instance) {
		godot_instance->stop();
		memdelete(godot_instance);
		instance = nullptr;
		Main::cleanup();
		if (os != nullptr) {
			memdelete(os);
			os = nullptr;
		}
	}
}
