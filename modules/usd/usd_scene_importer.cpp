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
static constexpr const char *USD_IMPORT_OPTION_VARIANT_PREFIX = "usd/variants/";
static constexpr const char *USD_STAGE_INSTANCE_GENERATED_META = "usd_stage_instance_generated";

static String _get_variant_option_name(const String &p_prim_path, const String &p_variant_set_name) {
	String property_prim_path = p_prim_path.trim_prefix("/");
	if (property_prim_path.is_empty()) {
		property_prim_path = "_root";
	}
	return String(USD_IMPORT_OPTION_VARIANT_PREFIX) + property_prim_path + "/" + p_variant_set_name;
}

static bool _parse_variant_option_name(const String &p_option, String *r_prim_path, String *r_variant_set_name) {
	ERR_FAIL_NULL_V(r_prim_path, false);
	ERR_FAIL_NULL_V(r_variant_set_name, false);
	if (!p_option.begins_with(USD_IMPORT_OPTION_VARIANT_PREFIX)) {
		return false;
	}

	String suffix = p_option.trim_prefix(USD_IMPORT_OPTION_VARIANT_PREFIX);
	int separator = suffix.rfind("/");
	if (separator <= 0 || separator == suffix.length() - 1) {
		return false;
	}

	String property_prim_path = suffix.substr(0, separator);
	if (property_prim_path == "_root") {
		*r_prim_path = "/";
	} else {
		*r_prim_path = "/" + property_prim_path;
	}
	*r_variant_set_name = suffix.substr(separator + 1);
	return !r_prim_path->is_empty() && !r_variant_set_name->is_empty();
}

static Dictionary _merge_variant_selection_dicts(const Dictionary &p_base, const Dictionary &p_override) {
	Dictionary merged = p_base.duplicate(true);
	for (const KeyValue<Variant, Variant> &prim_entry : p_override) {
		if (prim_entry.value.get_type() != Variant::DICTIONARY) {
			continue;
		}

		Dictionary merged_prim;
		Variant merged_prim_variant = merged.get(prim_entry.key, Variant());
		if (merged_prim_variant.get_type() == Variant::DICTIONARY) {
			merged_prim = merged_prim_variant;
		}

		const Dictionary override_prim = prim_entry.value;
		for (const KeyValue<Variant, Variant> &set_entry : override_prim) {
			if (set_entry.value.get_type() != Variant::STRING && set_entry.value.get_type() != Variant::STRING_NAME) {
				continue;
			}
			merged_prim[set_entry.key] = set_entry.value;
		}

		if (!merged_prim.is_empty()) {
			merged[prim_entry.key] = merged_prim;
		}
	}
	return merged;
}

static Dictionary _collect_variant_selections_from_structured_options(const HashMap<StringName, Variant> &p_options) {
	Dictionary variant_selections;
	for (const KeyValue<StringName, Variant> &entry : p_options) {
		const String option_name = entry.key;
		String prim_path;
		String variant_set_name;
		if (!_parse_variant_option_name(option_name, &prim_path, &variant_set_name)) {
			continue;
		}
		if (entry.value.get_type() != Variant::STRING && entry.value.get_type() != Variant::STRING_NAME) {
			continue;
		}

		const String selection = String(entry.value).strip_edges();
		if (selection.is_empty()) {
			continue;
		}

		Dictionary prim_selections;
		Variant prim_selection_variant = variant_selections.get(prim_path, Variant());
		if (prim_selection_variant.get_type() == Variant::DICTIONARY) {
			prim_selections = prim_selection_variant;
		}
		prim_selections[variant_set_name] = selection;
		variant_selections[prim_path] = prim_selections;
	}
	return variant_selections;
}

static Dictionary _get_stage_variant_catalog(const String &p_path) {
	Ref<UsdStageResource> stage;
	stage.instantiate();
	stage->set_source_path(p_path);
	return stage->get_variant_sets();
}

static Array _build_import_warnings(const Dictionary &p_variant_catalog, const Dictionary &p_variant_selections) {
	Array warnings;
	if (!p_variant_catalog.is_empty()) {
		String message = "UsdSceneFormatImporter baked a composed USD scene. Inactive variant branches and live variant switching are not preserved in the imported result.";
		if (!p_variant_selections.is_empty()) {
			message += " Import-time variant selections are recorded in node metadata.";
		}
		warnings.push_back(message);
	}
	return warnings;
}

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

static void _append_variant_import_options(const Dictionary &p_variant_catalog, List<ResourceImporter::ImportOption> *r_options) {
	ERR_FAIL_NULL(r_options);
	if (p_variant_catalog.is_empty()) {
		return;
	}

	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::NIL, "USD Variants", PROPERTY_HINT_NONE, "usd/variants/", PROPERTY_USAGE_GROUP), Variant()));
	for (const KeyValue<Variant, Variant> &prim_entry : p_variant_catalog) {
		if (prim_entry.value.get_type() != Variant::DICTIONARY) {
			continue;
		}

		const String prim_path = prim_entry.key;
		const Dictionary prim_sets = prim_entry.value;
		for (const KeyValue<Variant, Variant> &set_entry : prim_sets) {
			if (set_entry.value.get_type() != Variant::DICTIONARY) {
				continue;
			}

			const String variant_set_name = set_entry.key;
			const Dictionary set_description = set_entry.value;
			const Array variants = set_description.get("variants", Array());
			if (variants.is_empty()) {
				continue;
			}

			String hint_string;
			for (int i = 0; i < variants.size(); i++) {
				if (variants[i].get_type() != Variant::STRING && variants[i].get_type() != Variant::STRING_NAME) {
					continue;
				}
				if (!hint_string.is_empty()) {
					hint_string += ",";
				}
				hint_string += String(variants[i]);
			}
			if (hint_string.is_empty()) {
				continue;
			}

			const String option_name = _get_variant_option_name(prim_path, variant_set_name);
			const String selection = String(set_description.get("selection", String()));
			r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::STRING, option_name, PROPERTY_HINT_ENUM, hint_string, PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), selection));
		}
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
	variant_selections = _merge_variant_selection_dicts(variant_selections, _collect_variant_selections_from_structured_options(p_options));

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

	const Dictionary source_variant_catalog = stage_instance->get_stage().is_valid() ? stage_instance->get_stage()->get_variant_sets() : Dictionary();
	const Array import_warnings = _build_import_warnings(source_variant_catalog, variant_selections);
	if (!import_warnings.is_empty()) {
		for (int i = 0; i < import_warnings.size(); i++) {
			if (import_warnings[i].get_type() == Variant::STRING || import_warnings[i].get_type() == Variant::STRING_NAME) {
				WARN_PRINT(String(import_warnings[i]));
			}
		}
	}
	memdelete(stage_instance);

	if (r_err != nullptr) {
		*r_err = OK;
	}
	return generated_root;
}

void UsdSceneFormatImporter::get_import_options(const String &p_path, List<ResourceImporter::ImportOption> *r_options) {
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::STRING, USD_IMPORT_OPTION_VARIANT_SELECTIONS, PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), String()));
	if (p_path.is_empty()) {
		return;
	}
	_append_variant_import_options(_get_stage_variant_catalog(p_path), r_options);
}

Variant UsdSceneFormatImporter::get_option_visibility(const String &p_path, const String &p_scene_import_type, const String &p_option, const HashMap<StringName, Variant> &p_options) {
	(void)p_path;
	(void)p_scene_import_type;
	(void)p_options;
	if (p_option == USD_IMPORT_OPTION_VARIANT_SELECTIONS) {
		return false;
	}
	return true;
}

#endif // TOOLS_ENABLED
