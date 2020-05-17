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
#include "utils/string_utils.h"
#include "editor/swift_bindings_generator.h"

SwiftLanguage *SwiftLanguage::singleton;

void SwiftLanguage::set_language_index(int p_idx) 
{
	ERR_FAIL_COND(lang_idx != -1);
	lang_idx = p_idx;
}

SwiftLanguage::SwiftLanguage() {

	ERR_FAIL_COND_MSG(singleton, "Swift singleton already exist.");
	singleton = this;

	finalizing = false;

	gdswift = NULL;

#ifdef NO_THREADS
	script_instances_mutex = NULL;
	script_gchandle_release_mutex = NULL;
	language_bind_mutex = NULL;
#else
	script_instances_mutex = Mutex::create();
	script_gchandle_release_mutex = Mutex::create();
	language_bind_mutex = Mutex::create();
#endif

	lang_idx = -1;
#ifdef TOOLS_ENABLED
	godotswift_editor = NULL;
#endif
}

SwiftLanguage::~SwiftLanguage() {
    printf ("SwiftLanguage::~SwiftLanguage() called\n");
	singleton = NULL;
}
String SwiftLanguage::get_name() const {

	return "Swift";
}

String SwiftLanguage::get_type() const {

	return "SwiftScript";
}

String SwiftLanguage::get_extension() const {

	return "swift";
}

Error SwiftLanguage::execute_file(const String &p_path) {
    printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	// ??
	return OK;
}


#ifdef TOOLS_ENABLED
#endif

void SwiftLanguage::init() {
#ifdef TOOLS_ENABLED
	//EditorNode::add_init_callback(&_editor_init_callback);
#endif
    gdswift = memnew(GDSwift);
    gdswift->initialize();
#if defined(TOOLS_ENABLED) && defined(DEBUG_METHODS_ENABLED)
	// Generate bindings here, before loading assemblies. 'initialize_load_assemblies' aborts
	// the applications if the api assemblies or the main tools assembly is missing, but this
	// is not a problem for BindingsGenerator as it only needs the tools project editor assembly.
	List<String> cmdline_args = OS::get_singleton()->get_cmdline_args();
	SwiftBindingsGenerator::handle_cmdline_args(cmdline_args);
#endif

#ifndef MONO_GLUE_ENABLED
	print_line("Run this binary with '--generate-swift-glue path/to/modules/mono/glue'");
#endif
}

void SwiftLanguage::finish() {
}

// This returns words that should be highlighted, so we include both
// reserved keywords and positional keywords
void SwiftLanguage::get_reserved_words(List<String> *p_words) const {

	static const char *_reserved_words[] = {
        "#available",
        "#colorLiteral",
        "#column",
        "#else",
        "#elseif",
        "#endif",
        "#error",
        "#file",
        "#fileLiteral",
        "#function",
        "#if",
        "#imageLiteral",
        "#line",
        "#selector",
        "#sourceLocation",
        "#warning",
        "Any",
        "Protocol",
        "Self",
        "Type",
        "_",
        "as",
        "associatedtype",
        "associativity",
        "break",
        "case",
        "catch",
        "class",
        "continue",
        "convenience",
        "default",
        "defer",
        "deinit",
        "didSet",
        "do",
        "dynamic",
        "else",
        "enum",
        "extension",
        "fallthrough",
        "false",
        "fileprivate",
        "final",
        "for",
        "func",
        "get",
        "guard",
        "if",
        "import",
        "in",
        "indirect",
        "infix",
        "init",
        "inout",
        "internal",
        "is",
        "lazy",
        "left",
        "let",
        "mutating",
        "nil",
        "none",
        "nonmutating",
        "open",
        "operator",
        "optional",
        "override",
        "postfix",
        "precedence",
        "prefix",
        "private",
        "protocol",
        "public",
        "repeat",
        "required",
        "rethrows",
        "return",
        "right",
        "self",
        "set",
        "static",
        "struct",
        "subscript",
        "super",
        "switch",
        "throw",
        "throws",
        "true",
        "try",
        "typealias",
        "unowned",
        "var",
        "weak",
        "where",
        "while",

		0
	};

	const char **w = _reserved_words;

	while (*w) {
		p_words->push_back(*w);
		w++;
	}
}

void SwiftLanguage::get_comment_delimiters(List<String> *p_delimiters) const {

	p_delimiters->push_back("//"); // single-line comment
	p_delimiters->push_back("/* */"); // delimited comment
}

void SwiftLanguage::get_string_delimiters(List<String> *p_delimiters) const {

	p_delimiters->push_back("\" \""); // regular string literal
}

static String get_base_class_name(const String &p_base_class_name, const String p_class_name) {

	String base_class = p_base_class_name;
	return base_class;
}

Ref<Script> SwiftLanguage::get_template(const String &p_class_name, const String &p_base_class_name) const {

	String script_template = "import " BINDINGS_NAMESPACE "\n"
							 "import Foundation\n"
							 "\n"
							 "public class %CLASS%: %BASE%\n {"
							 "    // Declare member variables here. Examples:\n"
							 "    // var a = 2\n"
							 "    // var b: String = \"text\"\n"
							 "\n"
							 "    // Called when the node enters the scene tree for the first time.\n"
							 "    public override func ready ()\n"
							 "    {\n"
							 "        \n"
							 "    }\n"
							 "\n"
							 "//  // Called every frame. 'delta' is the elapsed time since the previous frame.\n"
							 "//  public override func process (delta: Float)\n"
							 "//  {\n"
							 "//      \n"
							 "//  }\n"
							 "}\n";

	String base_class_name = get_base_class_name(p_base_class_name, p_class_name);
	script_template = script_template.replace("%BASE%", base_class_name)
							  .replace("%CLASS%", p_class_name);

	Ref<SwiftScript> script;
	script.instance();
	script->set_source_code(script_template);
	script->set_name(p_class_name);

	return script;
}

bool SwiftLanguage::is_using_templates() {

	return true;
}

void SwiftLanguage::make_template(const String &p_class_name, const String &p_base_class_name, Ref<Script> &p_script) {

	String src = p_script->get_source_code();
	String base_class_name = get_base_class_name(p_base_class_name, p_class_name);
	src = src.replace("%BASE%", base_class_name)
				  .replace("%CLASS%", p_class_name)
				  .replace("%TS%", _get_indentation());
	p_script->set_source_code(src);
}

String SwiftLanguage::validate_path(const String &p_path) const {

	String class_name = p_path.get_file().get_basename();
	List<String> keywords;
	get_reserved_words(&keywords);
	if (keywords.find(class_name)) {
		return TTR("Class name can't be a reserved keyword");
	}
	return "";
}

Script *SwiftLanguage::create_script() const {

	return memnew(SwiftScript);
}

bool SwiftLanguage::has_named_classes() const {

	return false;
}

bool SwiftLanguage::supports_builtin_mode() const {

	return false;
}

void SwiftLanguage::thread_enter() {
    //
}

void SwiftLanguage::thread_exit() {
    //
}

bool SwiftLanguage::debug_break_parse(const String &p_file, int p_line, const String &p_error) {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return false;
}

bool SwiftLanguage::debug_break(const String &p_error, bool p_allow_continue) {
	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
    return false;
}

String SwiftLanguage::debug_get_error() const {
    abort ();
	return String();
}

int SwiftLanguage::debug_get_stack_level_count() const {
    printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return 1;
}

int SwiftLanguage::debug_get_stack_level_line(int p_level) const {
    printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return 1;
}

String SwiftLanguage::debug_get_stack_level_function(int p_level) const {
    printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return String();
}

String SwiftLanguage::debug_get_stack_level_source(int p_level) const {

	printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return String();
}

Vector<ScriptLanguage::StackInfo> SwiftLanguage::debug_get_current_stack_info() {
    printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	Vector<StackInfo> si;
	return si;
}

void SwiftLanguage::reload_all_scripts() {
	printf ("SwiftLanguage::reload_all_scripts()\n");
}

void SwiftLanguage::reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) {
	printf ("SwiftLanguage::reload_tool_script(...)\n");
}


#ifdef TOOLS_ENABLED
Error SwiftLanguage::open_in_external_editor(const Ref<Script> &p_script, int p_line, int p_col) {

	return (Error)(int)get_godotswift_editor()->call("OpenInExternalEditor", p_script, p_line, p_col);
}

bool SwiftLanguage::overrides_external_editor() {
    return false;
	return get_godotswift_editor()->call("OverridesExternalEditor");
}
#endif

void *SwiftLanguage::alloc_instance_binding_data(Object *p_object) {
    printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
    return NULL;
}

#ifdef TOOLS_ENABLED
static String variant_type_to_managed_name(const String &p_var_type_name) {

	if (p_var_type_name.empty())
		return "object";

	if (!ClassDB::class_exists(p_var_type_name)) {
		return p_var_type_name;
	}

	if (p_var_type_name == Variant::get_type_name(Variant::OBJECT))
		return "Godot.Object";

	if (p_var_type_name == Variant::get_type_name(Variant::REAL)) {
#ifdef REAL_T_IS_DOUBLE
		return "Double";
#else
		return "Float";
#endif
	}

	if (p_var_type_name == Variant::get_type_name(Variant::STRING))
		return "String"; // I prefer this one >:[

	if (p_var_type_name == Variant::get_type_name(Variant::DICTIONARY))
		return "Dictionary";

	if (p_var_type_name == Variant::get_type_name(Variant::ARRAY))
		return "Array";

	if (p_var_type_name == Variant::get_type_name(Variant::POOL_BYTE_ARRAY))
		return "[UInt8";
	if (p_var_type_name == Variant::get_type_name(Variant::POOL_INT_ARRAY))
		return "[Int]";
	if (p_var_type_name == Variant::get_type_name(Variant::POOL_REAL_ARRAY)) {
#ifdef REAL_T_IS_DOUBLE
		return "[Double";
#else
		return "[Float]";
#endif
	}
	if (p_var_type_name == Variant::get_type_name(Variant::POOL_STRING_ARRAY))
		return "[String]";
	if (p_var_type_name == Variant::get_type_name(Variant::POOL_VECTOR2_ARRAY))
		return "[Vector2";
	if (p_var_type_name == Variant::get_type_name(Variant::POOL_VECTOR3_ARRAY))
		return "[Vector3]";
	if (p_var_type_name == Variant::get_type_name(Variant::POOL_COLOR_ARRAY))
		return "[Color]";

	Variant::Type var_types[] = {
		Variant::BOOL,
		Variant::INT,
		Variant::VECTOR2,
		Variant::RECT2,
		Variant::VECTOR3,
		Variant::TRANSFORM2D,
		Variant::PLANE,
		Variant::QUAT,
		Variant::AABB,
		Variant::BASIS,
		Variant::TRANSFORM,
		Variant::COLOR,
		Variant::NODE_PATH,
		Variant::_RID
	};

	for (unsigned int i = 0; i < sizeof(var_types) / sizeof(Variant::Type); i++) {
		if (p_var_type_name == Variant::get_type_name(var_types[i]))
			return p_var_type_name;
	}

	return "object";
}

String SwiftLanguage::make_function(const String &, const String &p_name, const PoolStringArray &p_args) const {
	// FIXME
	// - Due to Godot's API limitation this just appends the function to the end of the file
	// - Use fully qualified name if there is ambiguity
	String s = "func " + p_name + "(";
	for (int i = 0; i < p_args.size(); i++) {
		const String &arg = p_args[i];

		if (i > 0)
			s += ", ";

		s += variant_type_to_managed_name(arg.get_slice(":", 1)) + " " + escape_swift_keyword(arg.get_slice(":", 0));
	}
	s += ")\n{\n    // Replace with function body.\n}\n";

	return s;
}
#else
String SwiftLanguage::make_function(const String &, const String &, const PoolStringArray &) const {
	return String();
}
#endif

String SwiftLanguage::_get_indentation() const {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		bool use_space_indentation = EDITOR_DEF("text_editor/indent/type", 0);

		if (use_space_indentation) {
			int indent_size = EDITOR_DEF("text_editor/indent/size", 4);

			String space_indent = "";
			for (int i = 0; i < indent_size; i++) {
				space_indent += " ";
			}
			return space_indent;
		}
	}
#endif
	return "\t";
}
void SwiftLanguage::get_recognized_extensions(List<String> *p_extensions) const {

	p_extensions->push_back("swift");
}

void SwiftLanguage::frame() {
    // nothing for now
}

void SwiftLanguage::free_instance_binding_data(void *p_data) {

    printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
}

void SwiftLanguage::refcount_incremented_instance_binding(Object *p_object) {
    printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
}

bool SwiftLanguage::refcount_decremented_instance_binding(Object *p_object) {
printf ("%s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	return true;
}

