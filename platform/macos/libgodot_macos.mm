/**************************************************************************/
/*  libgodot_macos.mm                                                     */
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

#include "core/object/class_db.h"
#include "core/extension/godot_instance.h"
#include "main/main.h"

#include "display_server_embedded.h"
#include "os_macos.h"

static OS_MacOS *os = nullptr;

static GodotInstance *instance = nullptr;
static bool embedded_driver_registered = false;
static bool embedded_class_registered = false;

static bool _wants_embedded_driver(int p_argc, char *p_argv[]) {
	for (int i = 1; i < p_argc; i++) {
		if (strcmp("--embedded", p_argv[i]) == 0) {
			return true;
		}
		if (i < p_argc - 1 && strcmp("--display-driver", p_argv[i]) == 0 && strcmp("embedded", p_argv[i + 1]) == 0) {
			return true;
		}
	}
	return false;
}

GDExtensionObjectPtr libgodot_create_godot_instance(int p_argc, char *p_argv[], GDExtensionInitializationFunction p_init_func) {
	ERR_FAIL_COND_V_MSG(instance != nullptr, nullptr, "Only one Godot Instance may be created.");

	if (!embedded_driver_registered && _wants_embedded_driver(p_argc, p_argv)) {
		DisplayServerEmbedded::register_embedded_driver();
		embedded_driver_registered = true;
	}

	uint32_t remaining_args = p_argc - 1;
	os = new OS_MacOS_NSApp(p_argv[0], remaining_args, remaining_args > 0 ? &p_argv[1] : nullptr);

	@autoreleasepool {
		Error err = Main::setup(p_argv[0], remaining_args, remaining_args > 0 ? &p_argv[1] : nullptr, false);
		if (err != OK) {
			return nullptr;
		}

		if (!embedded_class_registered) {
			ClassDB::register_abstract_class<DisplayServerEmbedded>();
			embedded_class_registered = true;
		}

		instance = memnew(GodotInstance);
		if (!instance->initialize(p_init_func)) {
			memdelete(instance);
			instance = nullptr;
			return nullptr;
		}

		return (GDExtensionObjectPtr)instance;
	}
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
