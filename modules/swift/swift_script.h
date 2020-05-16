/*************************************************************************/
/*  swift_script.h                                                       */
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

#ifndef SWIFT_SCRIPT_H
#define SWIFT_SCRIPT_H

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/script_language.h"
#include "core/self_list.h"
#include "swift_gd/gd_swift.h"
#ifdef TOOLS_ENABLED
#include "editor/editor_plugin.h"
#endif

class SwiftScript;
class SwiftInstance;
class SwiftLanguage;

#ifdef NO_SAFE_CAST
template <typename TScriptInstance, typename TScriptLanguage>
TScriptInstance *cast_script_instance(ScriptInstance *p_inst) {
	if (!p_inst)
		return NULL;
	return p_inst->get_language() == TScriptLanguage::get_singleton() ? static_cast<TScriptInstance *>(p_inst) : NULL;
}
#else
template <typename TScriptInstance, typename TScriptLanguage>
TScriptInstance *cast_script_instance(ScriptInstance *p_inst) {
	return dynamic_cast<TScriptInstance *>(p_inst);
}
#endif

#define CAST_SWIFT_INSTANCE(m_inst) (cast_script_instance<SwiftInstance, SwiftLanguage>(m_inst))

class SwiftScript : public Script {
  	GDCLASS(SwiftScript, Script);
	friend class SwiftInstance;
	friend class SwiftLanguage;

  	bool tool;
  	bool valid;
  	bool placeholder_fallback_enabled;
	SelfList<SwiftScript> script_list;
	String source;
#ifdef TOOLS_ENABLED
	bool source_changed_cache;
#endif
public:
	virtual bool can_instance() const;
	virtual StringName get_instance_base_type() const;
	virtual ScriptInstance *instance_create(Object *p_this);
	virtual PlaceHolderScriptInstance *placeholder_instance_create(Object *p_this);
	virtual bool instance_has(const Object *p_this) const;

	virtual bool has_source_code() const;
	virtual String get_source_code() const;
	virtual void set_source_code(const String &p_code);

	virtual Error reload(bool p_keep_state = false);

	virtual bool has_script_signal(const StringName &p_signal) const;
	virtual void get_script_signal_list(List<MethodInfo> *r_signals) const;

	virtual bool get_property_default_value(const StringName &p_property, Variant &r_value) const;
	virtual void get_script_property_list(List<PropertyInfo> *p_list) const;
	virtual void update_exports();

	virtual bool is_tool() const { return tool; }
	virtual bool is_valid() const { return valid; }

	virtual Ref<Script> get_base_script() const;
	virtual ScriptLanguage *get_language() const;

	virtual void get_script_method_list(List<MethodInfo> *p_list) const;
	bool has_method(const StringName &p_method) const;
	MethodInfo get_method_info(const StringName &p_method) const;

	virtual int get_member_line(const StringName &p_member) const;

#ifdef TOOLS_ENABLED
	virtual bool is_placeholder_fallback_enabled() const { return placeholder_fallback_enabled; }
#endif

	Error load_source_code(const String &p_path);

	StringName get_script_name() const;

	SwiftScript();
	~SwiftScript();
};

class SwiftInstance : public ScriptInstance {

	friend class SwiftScript;
	friend class SwiftLanguage;

	bool destructing_script_instance;
	
public:
	void *get_swift_object() const;

	_FORCE_INLINE_ bool is_destructing_script_instance() { return destructing_script_instance; }

	virtual Object *get_owner();

	virtual bool set(const StringName &p_name, const Variant &p_value);
	virtual bool get(const StringName &p_name, Variant &r_ret) const;
	virtual void get_property_list(List<PropertyInfo> *p_properties) const;
	virtual Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid) const;

	/* TODO */ virtual void get_method_list(List<MethodInfo> *p_list) const {}
	virtual bool has_method(const StringName &p_method) const;
	virtual Variant call(const StringName &p_method, const Variant **p_args, int p_argcount, Variant::CallError &r_error);
	virtual void call_multilevel(const StringName &p_method, const Variant **p_args, int p_argcount);
	virtual void call_multilevel_reversed(const StringName &p_method, const Variant **p_args, int p_argcount);

	void swift_object_disposed(void *p_obj);

	virtual void refcount_incremented();
	virtual bool refcount_decremented();

	virtual MultiplayerAPI::RPCMode get_rpc_mode(const StringName &p_method) const;
	virtual MultiplayerAPI::RPCMode get_rset_mode(const StringName &p_variable) const;

	virtual void notification(int p_notification);
	void _call_notification(int p_notification);

	virtual String to_string(bool *r_valid);

	virtual Ref<Script> get_script() const;

	virtual ScriptLanguage *get_language();

	SwiftInstance();
	~SwiftInstance();
};

struct SwiftScriptBinding {
	bool inited;
	StringName type_name;
	Object *owner;
};

class SwiftLanguage : public ScriptLanguage {
	static SwiftLanguage *singleton;

	friend class SwiftScript;
	friend class SwiftInstance;

	int lang_idx;
	Dictionary scripts_metadata;
	bool scripts_metadata_invalidated;
	void _load_scripts_metadata();
	GDSwift *gdswift;
	bool finalizing;

	Mutex *script_instances_mutex;
	Mutex *script_gchandle_release_mutex;
	Mutex *language_bind_mutex;

	// For debug_break and debug_break_parse
	int _debug_parse_err_line;
	String _debug_parse_err_file;
	String _debug_error;

#ifdef TOOLS_ENABLED
	EditorPlugin *godotswift_editor;

	static void _editor_init_callback();
#endif

public:

	Mutex *get_language_bind_mutex() { return language_bind_mutex; }

	_FORCE_INLINE_ int get_language_index() { return lang_idx; }
	void set_language_index(int p_idx);

	//_FORCE_INLINE_ const StringNameCache &get_string_names() { return string_names; }

	_FORCE_INLINE_ static SwiftLanguage *get_singleton() { return singleton; }

#ifdef TOOLS_ENABLED
	_FORCE_INLINE_ EditorPlugin *get_godotswift_editor() const { return godotswift_editor; }
#endif

  //static void release_script_gchandle(Ref<MonoGCHandle> &p_gchandle);
  //static void release_script_gchandle(MonoObject *p_expected_obj, Ref<MonoGCHandle> &p_gchandle);

	bool debug_break(const String &p_error, bool p_allow_continue = true);
	bool debug_break_parse(const String &p_file, int p_line, const String &p_error);

#ifdef GD_MONO_HOT_RELOAD
	bool is_assembly_reloading_needed();
	void reload_assemblies(bool p_soft_reload);
#endif

	_FORCE_INLINE_ Dictionary get_scripts_metadata_or_nothing() {
		return scripts_metadata_invalidated ? Dictionary() : scripts_metadata;
	}

	_FORCE_INLINE_ const Dictionary &get_scripts_metadata() {
		if (scripts_metadata_invalidated)
			_load_scripts_metadata();
		return scripts_metadata;
	}

	virtual String get_name() const;

	/* LANGUAGE FUNCTIONS */
	virtual String get_type() const;
	virtual String get_extension() const;
	virtual Error execute_file(const String &p_path);
	virtual void init();
	virtual void finish();

	/* EDITOR FUNCTIONS */
	virtual void get_reserved_words(List<String> *p_words) const;
	virtual void get_comment_delimiters(List<String> *p_delimiters) const;
	virtual void get_string_delimiters(List<String> *p_delimiters) const;
	virtual Ref<Script> get_template(const String &p_class_name, const String &p_base_class_name) const;
	virtual bool is_using_templates();
	virtual void make_template(const String &p_class_name, const String &p_base_class_name, Ref<Script> &p_script);
	/* TODO */ virtual bool validate(const String &p_script, int &r_line_error, int &r_col_error, String &r_test_error, const String &p_path, List<String> *r_functions, List<ScriptLanguage::Warning> *r_warnings = NULL, Set<int> *r_safe_lines = NULL) const { return true; }
	virtual String validate_path(const String &p_path) const;
	virtual Script *create_script() const;
	virtual bool has_named_classes() const;
	virtual bool supports_builtin_mode() const;
	/* TODO? */ virtual int find_function(const String &p_function, const String &p_code) const { return -1; }
	virtual String make_function(const String &p_class, const String &p_name, const PoolStringArray &p_args) const;
	virtual String _get_indentation() const;
	/* TODO? */ virtual void auto_indent_code(String &p_code, int p_from_line, int p_to_line) const {}
	/* TODO */ virtual void add_global_constant(const StringName &p_variable, const Variant &p_value) {}

	/* DEBUGGER FUNCTIONS */
	virtual String debug_get_error() const;
	virtual int debug_get_stack_level_count() const;
	virtual int debug_get_stack_level_line(int p_level) const;
	virtual String debug_get_stack_level_function(int p_level) const;
	virtual String debug_get_stack_level_source(int p_level) const;
	/* TODO */ virtual void debug_get_stack_level_locals(int p_level, List<String> *p_locals, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {}
	/* TODO */ virtual void debug_get_stack_level_members(int p_level, List<String> *p_members, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {}
	/* TODO */ virtual void debug_get_globals(List<String> *p_locals, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {}
	/* TODO */ virtual String debug_parse_stack_level_expression(int p_level, const String &p_expression, int p_max_subitems, int p_max_depth) { return ""; }
	virtual Vector<StackInfo> debug_get_current_stack_info();

	/* PROFILING FUNCTIONS */
	/* TODO */ virtual void profiling_start() {}
	/* TODO */ virtual void profiling_stop() {}
	/* TODO */ virtual int profiling_get_accumulated_data(ProfilingInfo *p_info_arr, int p_info_max) { return 0; }
	/* TODO */ virtual int profiling_get_frame_data(ProfilingInfo *p_info_arr, int p_info_max) { return 0; }

	virtual void frame();

	/* TODO? */ virtual void get_public_functions(List<MethodInfo> *p_functions) const {}
	/* TODO? */ virtual void get_public_constants(List<Pair<String, Variant> > *p_constants) const {}

	virtual void reload_all_scripts();
	virtual void reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload);

	/* LOADER FUNCTIONS */
	virtual void get_recognized_extensions(List<String> *p_extensions) const;

#ifdef TOOLS_ENABLED
	virtual Error open_in_external_editor(const Ref<Script> &p_script, int p_line, int p_col);
	virtual bool overrides_external_editor();
#endif

	/* THREAD ATTACHING */
	virtual void thread_enter();
	virtual void thread_exit();

	// Don't use these. I'm watching you
	virtual void *alloc_instance_binding_data(Object *p_object);
	virtual void free_instance_binding_data(void *p_data);
	virtual void refcount_incremented_instance_binding(Object *p_object);
	virtual bool refcount_decremented_instance_binding(Object *p_object);

	Map<Object *, SwiftScriptBinding>::Element *insert_script_binding(Object *p_object, const SwiftScriptBinding &p_script_binding);
	bool setup_swift_script_binding(SwiftScriptBinding &r_script_binding, Object *p_object);

	void post_unsafe_reference(Object *p_obj);
	void pre_unsafe_unreference(Object *p_obj);

	SwiftLanguage();
	~SwiftLanguage();
};

class ResourceFormatLoaderSwiftScript : public ResourceFormatLoader {
public:
	virtual RES load(const String &p_path, const String &p_original_path = "", Error *r_error = NULL);
	virtual void get_recognized_extensions(List<String> *p_extensions) const;
	virtual bool handles_type(const String &p_type) const;
	virtual String get_resource_type(const String &p_path) const;
};

class ResourceFormatSaverSwiftScript : public ResourceFormatSaver {
public:
	virtual Error save(const String &p_path, const RES &p_resource, uint32_t p_flags = 0);
	virtual void get_recognized_extensions(const RES &p_resource, List<String> *p_extensions) const;
	virtual bool recognize(const RES &p_resource) const;
};

#endif // SWIFT_SCRIPT_H
