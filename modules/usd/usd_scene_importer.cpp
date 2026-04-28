/**************************************************************************/
/*  usd_scene_importer.cpp                                                */
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

#include "usd_scene_importer.h"

#ifdef TOOLS_ENABLED

#include "usd_scene_loader.h"

#include "core/io/json.h"
#include "scene/resources/packed_scene.h"

static constexpr const char *USD_IMPORT_OPTION_VARIANT_SELECTIONS = "usd/variant_selections";
static constexpr const char *USD_STAGE_INSTANCE_GENERATED_META = "usd_stage_instance_generated";

static bool _parse_import_variant_selections(const HashMap<StringName, Variant> &p_options, Dictionary *r_variant_selections, Error *r_error) {
	ERR_FAIL_NULL_V(r_variant_selections, false);
	if (r_error != nullptr) {
		*r_error = OK;
	}

	r_variant_selections->clear();
	const Variant *option_ptr = p_options.getptr(StringName(USD_IMPORT_OPTION_VARIANT_SELECTIONS));
	if (option_ptr == nullptr) {
		return true;
	}

	String serialized = *option_ptr;
	serialized = serialized.strip_edges();
	if (serialized.is_empty()) {
		return true;
	}

	const Variant parsed = JSON::parse_string(serialized);
	if (parsed.get_type() != Variant::DICTIONARY) {
		if (r_error != nullptr) {
			*r_error = ERR_PARSE_ERROR;
		}
		ERR_PRINT(vformat("UsdSceneFormatImporter expected '%s' to be a JSON object.", USD_IMPORT_OPTION_VARIANT_SELECTIONS));
		return false;
	}

	*r_variant_selections = parsed;
	return true;
}

static void _clear_generated_root_marker_recursive(Node *p_node) {
	ERR_FAIL_NULL(p_node);
	if (p_node->has_meta(USD_STAGE_INSTANCE_GENERATED_META)) {
		p_node->remove_meta(USD_STAGE_INSTANCE_GENERATED_META);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_clear_generated_root_marker_recursive(p_node->get_child(i));
	}
}

void UsdSceneFormatImporter::get_extensions(List<String> *r_extensions) const {
	r_extensions->push_back("usd");
	r_extensions->push_back("usda");
	r_extensions->push_back("usdc");
	r_extensions->push_back("usdz");
}

Node *UsdSceneFormatImporter::import_scene(const String &p_path, uint32_t p_flags, const HashMap<StringName, Variant> &p_options, List<String> *r_missing_deps, Error *r_err) {
	(void)p_flags;
	(void)r_missing_deps;

	if (r_err != nullptr) {
		*r_err = ERR_CANT_OPEN;
	}

	Dictionary variant_selections;
	if (!_parse_import_variant_selections(p_options, &variant_selections, r_err)) {
		return nullptr;
	}

	Error load_error = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(p_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &load_error);
	if (packed_scene.is_null() || load_error != OK) {
		if (r_err != nullptr) {
			*r_err = load_error;
		}
		return nullptr;
	}

	Node *root = packed_scene->instantiate();
	ERR_FAIL_NULL_V(root, nullptr);

	UsdStageInstance *stage_instance = Object::cast_to<UsdStageInstance>(root);
	if (stage_instance == nullptr) {
		if (r_err != nullptr) {
			*r_err = OK;
		}
		return root;
	}

	if (!variant_selections.is_empty()) {
		stage_instance->set_variant_selections(variant_selections);
	}
	const Error rebuild_error = stage_instance->rebuild();
	if (rebuild_error != OK) {
		if (r_err != nullptr) {
			*r_err = rebuild_error;
		}
		memdelete(stage_instance);
		return nullptr;
	}

	Node *generated_root = stage_instance->get_node_or_null(NodePath("_Generated"));
	if (generated_root == nullptr) {
		if (r_err != nullptr) {
			*r_err = ERR_BUG;
		}
		ERR_PRINT(vformat("UsdSceneFormatImporter could not find generated USD content for import: %s", p_path));
		memdelete(stage_instance);
		return nullptr;
	}

	stage_instance->remove_child(generated_root);
	_clear_generated_root_marker_recursive(generated_root);
	generated_root->set_name(p_path.get_file().get_basename());
	memdelete(stage_instance);

	if (r_err != nullptr) {
		*r_err = OK;
	}
	return generated_root;
}

void UsdSceneFormatImporter::get_import_options(const String &p_path, List<ResourceImporter::ImportOption> *r_options) {
	(void)p_path;
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::STRING, USD_IMPORT_OPTION_VARIANT_SELECTIONS), String()));
}

#endif // TOOLS_ENABLED
