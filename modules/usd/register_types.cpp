/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "usd_scene_loader.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"

static Ref<UsdSceneFormatLoader> usd_scene_format_loader;
static Ref<UsdSceneFormatSaver> usd_scene_format_saver;

void initialize_usd_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GLOBAL_DEF(PropertyInfo(Variant::INT, "filesystem/import/usd/preview_lighting_mode", PROPERTY_HINT_ENUM, "Never,When Missing,Always"), 1);

	usd_scene_format_loader.instantiate();
	ResourceLoader::add_resource_format_loader(usd_scene_format_loader, true);

	usd_scene_format_saver.instantiate();
	ResourceSaver::add_resource_format_saver(usd_scene_format_saver, true);
}

void uninitialize_usd_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	if (usd_scene_format_loader.is_valid()) {
		ResourceLoader::remove_resource_format_loader(usd_scene_format_loader);
		usd_scene_format_loader.unref();
	}

	if (usd_scene_format_saver.is_valid()) {
		ResourceSaver::remove_resource_format_saver(usd_scene_format_saver);
		usd_scene_format_saver.unref();
	}
}
