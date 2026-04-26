/**************************************************************************/
/*  usd_scene_loader.h                                                    */
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

#pragma once

#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "scene/3d/node_3d.h"

class UsdStageResource : public Resource {
	GDCLASS(UsdStageResource, Resource);
	OBJ_SAVE_TYPE(UsdStageResource);

	String source_path;
	Dictionary stage_metadata;
	Dictionary variant_sets;

protected:
	static void _bind_methods();

public:
	void set_source_path(const String &p_source_path);
	String get_source_path() const;

	Dictionary get_stage_metadata() const;
	Dictionary get_variant_sets() const;
	Error refresh_metadata();
};

class UsdStageInstance : public Node3D {
	GDCLASS(UsdStageInstance, Node3D);

	Ref<UsdStageResource> stage;
	Dictionary variant_selections;
	Dictionary composed_variant_sets;
	Node *generated_root = nullptr;

	void _clear_node_children(Node *p_node);
	void _clear_generated_children();
	Node *_find_node_for_prim_path(Node *p_node, const String &p_prim_path) const;
	bool _parse_variant_property(const String &p_property, String *r_prim_path, String *r_variant_set) const;
	String _get_variant_selection(const String &p_prim_path, const String &p_variant_set) const;
	void _set_variant_selection_property(const String &p_prim_path, const String &p_variant_set, const String &p_selection);
	void _stage_changed();

protected:
	static void _bind_methods();
	void _notification(int p_what);
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

public:
	void set_stage(const Ref<UsdStageResource> &p_stage);
	Ref<UsdStageResource> get_stage() const;

	void set_variant_selections(const Dictionary &p_variant_selections);
	Dictionary get_variant_selections() const;

	Error rebuild();
	Node *get_node_for_prim_path(const String &p_prim_path) const;
};

class UsdSceneFormatLoader : public ResourceFormatLoader {
	GDSOFTCLASS(UsdSceneFormatLoader, ResourceFormatLoader);

public:
	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;
};

class UsdSceneFormatSaver : public ResourceFormatSaver {
	GDSOFTCLASS(UsdSceneFormatSaver, ResourceFormatSaver);

public:
	virtual Error save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags = 0) override;
	virtual bool recognize(const Ref<Resource> &p_resource) const override;
	virtual void get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const override;
	virtual bool recognize_path(const Ref<Resource> &p_resource, const String &p_path) const override;
};
