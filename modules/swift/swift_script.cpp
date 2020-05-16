/*************************************************************************/
/*  Swift_script.cpp                                                    */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2020 Miguel de Icaza                                    */
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
#include <stdlib.h>
#include "swift_script.h"

#include "core/io/json.h"
#include "core/os/file_access.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/project_settings.h"

#ifdef TOOLS_ENABLED
//#include "editor/bindings_generator.h"
//#include "editor/Swift_project.h"
//#include "editor/editor_node.h"
//#include "editor/node_dock.h"
#endif

#ifdef DEBUG_METHODS_ENABLED
//#include "class_db_api_json.h"
#endif

//#include "editor/editor_internal_calls.h"
//#include "godotsharp_dirs.h"
//#include "signal_awaiter_utils.h"
// #include "utils/macros.h"
// #include "utils/mutex_utils.h"
#include "utils/string_utils.h"
//#include "utils/thread_local.h"

#define CACHED_STRING_NAME(m_var) (SwiftLanguage::get_singleton()->get_string_names().m_var)

#ifdef TOOLS_ENABLED
static bool _create_project_if_needed() {

	// String sln_path = GodoSharpDirs::get_project_sln_path();
	// String csproj_path = GodotSharpDirs::get_project_csproj_path();

	// if (!FileAccess::exists(sln_path) || !FileAccess::exists(csproj_path)) {
	// 	// A solution does not yet exist, create a new one

	// 	CRASH_COND(SwiftLanguage::get_singleton()->get_godotsharp_editor() == NULL);
	// 	return SwiftLanguage::get_singleton()->get_godotsharp_editor()->call("CreateProjectSolution");
	// }

	return true;
}
#endif

SwiftScript::SwiftScript() : script_list(this) {

}

bool SwiftScript::can_instance() const {
	return true;
}

Error SwiftScript::load_source_code(const String &p_path) {
	Error ferr = sread_all_file_utf8(p_path, source);

	ERR_FAIL_COND_V_MSG(ferr != OK, ferr,
			ferr == ERR_INVALID_DATA ?
					"Script '" + p_path + "' contains invalid unicode (UTF-8), so it was not loaded."
										  " Please ensure that scripts are saved in valid UTF-8 unicode." :
					"Failed to read file: '" + p_path + "'.");

#ifdef TOOLS_ENABLED
	source_changed_cache = true;
#endif

	return OK;
}

StringName SwiftScript::get_instance_base_type() const {
	return StringName();
}

ScriptInstance *SwiftScript::instance_create(Object *p_this) {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return NULL;
}

void SwiftScript::update_exports() {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
}

bool SwiftScript::has_source_code() const {
	return !source.empty();
}

String SwiftScript::get_source_code() const {
	return source;
}

void SwiftScript::set_source_code(const String &p_code) {
	if (source == p_code)
		return;
	source = p_code;
#ifdef TOOLS_ENABLED
	source_changed_cache = true;
#endif
}

bool SwiftScript::instance_has(const Object *p_this) const {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return false;
}

void SwiftScript::get_script_method_list(List<MethodInfo> *p_list) const {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
}

PlaceHolderScriptInstance *SwiftScript::placeholder_instance_create(Object *p_this) {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return NULL;
}

Error SwiftScript::reload(bool p_keep_state) {
	printf ("SwiftScript::reload\n");
	return OK;
}

SwiftScript::~SwiftScript() {
}

ScriptLanguage *SwiftScript::get_language() const {

	return SwiftLanguage::get_singleton();
}

bool SwiftScript::has_method(const StringName &p_method) const {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); 
	return true;
}

bool SwiftScript::get_property_default_value(const StringName &p_property, Variant &r_value) const {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return false;
}

MethodInfo SwiftScript::get_method_info(const StringName &p_method) const {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return get_method_info ("Helo");
}

bool SwiftScript::has_script_signal(const StringName &p_signal) const {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return false;
}

void SwiftScript::get_script_signal_list(List<MethodInfo> *r_signals) const {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
}

Ref<Script> SwiftScript::get_base_script() const {

	// TODO search in metadata file once we have it, not important any way?
	return Ref<Script>();
}

void SwiftScript::get_script_property_list(List<PropertyInfo> *p_list) const {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
}

int SwiftScript::get_member_line(const StringName &p_member) const {

	// TODO omnisharp
	return -1;
}

StringName SwiftScript::get_script_name() const {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return StringName("SwiftClass");
}



RES ResourceFormatLoaderSwiftScript::load(const String &p_path, const String &p_original_path, Error *r_error) {

	if (r_error)
		*r_error = ERR_FILE_CANT_OPEN;

	// TODO ignore anything inside bin/ and obj/ in tools builds?

	SwiftScript *script = memnew(SwiftScript);

	Ref<SwiftScript> scriptres(script);

#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
	Error err = script->load_source_code(p_path);
	ERR_FAIL_COND_V_MSG(err != OK, RES(), "Cannot load Swift script file '" + p_path + "'.");
#endif

	script->set_path(p_original_path);

	script->reload();

	if (r_error)
		*r_error = OK;

	return scriptres;
}

void ResourceFormatLoaderSwiftScript::get_recognized_extensions(List<String> *p_extensions) const {

	p_extensions->push_back("swift");
}

bool ResourceFormatLoaderSwiftScript::handles_type(const String &p_type) const {

	return p_type == "Script" || p_type == SwiftLanguage::get_singleton()->get_type();
}

String ResourceFormatLoaderSwiftScript::get_resource_type(const String &p_path) const {

	return p_path.get_extension().to_lower() == "swift" ? SwiftLanguage::get_singleton()->get_type() : "";
}

Error ResourceFormatSaverSwiftScript::save(const String &p_path, const RES &p_resource, uint32_t p_flags) {

	Ref<SwiftScript> sqscr = p_resource;
	ERR_FAIL_COND_V(sqscr.is_null(), ERR_INVALID_PARAMETER);

	String source = sqscr->get_source_code();

#ifdef TOOLS_ENABLED
	if (!FileAccess::exists(p_path)) {
		// The file does not yet exists, let's assume the user just created this script

		// if (_create_project_if_needed()) {
		// 	SwiftProject::add_item(ProjectSettings::get_singleton()->globalize_path("res://"),
		// 			"Compile",
		// 			ProjectSettings::get_singleton()->globalize_path(p_path));
		// } else {
		// 	ERR_PRINTS("Swift project could not be created; cannot add file: '" + p_path + "'.");
		// }
	}
#endif

	Error err;
	FileAccess *file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot save Swift script file '" + p_path + "'.");

	file->store_string(source);

	if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF) {
		memdelete(file);
		return ERR_CANT_CREATE;
	}

	file->close();
	memdelete(file);

#ifdef TOOLS_ENABLED
	if (ScriptServer::is_reload_scripts_on_save_enabled()) {
		SwiftLanguage::get_singleton()->reload_tool_script(p_resource, false);
	}
#endif

	return OK;
}

void ResourceFormatSaverSwiftScript::get_recognized_extensions(const RES &p_resource, List<String> *p_extensions) const {

	if (Object::cast_to<SwiftScript>(p_resource.ptr())) {
		p_extensions->push_back("swift");
	}
}

bool ResourceFormatSaverSwiftScript::recognize(const RES &p_resource) const {

	return Object::cast_to<SwiftScript>(p_resource.ptr()) != NULL;
}

