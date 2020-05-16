/*************************************************************************/
/*  register_types.cpp                                                   */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "register_types.h"
#include "swift_gd/gd_swift.h"
#include "core/engine.h"

#include "swift_script.h"

SwiftLanguage *script_language_swift = NULL;
Ref<ResourceFormatLoaderSwiftScript> resource_loader_swift;
Ref<ResourceFormatSaverSwiftScript> resource_saver_swift;

_GodotSwift *_godotswift = NULL;

void register_swift_types() {
	ClassDB::register_class<SwiftScript>();

	_godotswift = memnew(_GodotSwift);

	ClassDB::register_class<_GodotSwift>();
	Engine::get_singleton()->add_singleton(Engine::Singleton("GodotSwift", _GodotSwift::get_singleton()));

	script_language_swift = memnew(SwiftLanguage);
	script_language_swift->set_language_index(ScriptServer::get_language_count());
	ScriptServer::register_language(script_language_swift);

	resource_loader_swift.instance();
	ResourceLoader::add_resource_format_loader(resource_loader_swift);

	resource_saver_swift.instance();
	ResourceSaver::add_resource_format_saver(resource_saver_swift);
}

void unregister_swift_types() {
	ScriptServer::unregister_language(script_language_swift);

	if (script_language_swift)
		memdelete(script_language_swift);

	ResourceLoader::remove_resource_format_loader(resource_loader_swift);
	resource_loader_swift.unref();

	ResourceSaver::remove_resource_format_saver(resource_saver_swift);
	resource_saver_swift.unref();

	if (_godotswift)
		memdelete(_godotswift);
}
