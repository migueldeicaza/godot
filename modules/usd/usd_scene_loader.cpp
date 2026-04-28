/**************************************************************************/
/*  usd_scene_loader.cpp                                                  */
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

#include "usd_scene_loader.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/resource_uid.h"
#include "core/io/zip_io.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/path_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/animation/animation_player.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"
#include "scene/resources/curve.h"
#include "scene/resources/environment.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/packed_scene.h"

#include <cstring>

#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/listOp.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdf/zipFile.h>
#include <pxr/usd/usd/property.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/inherits.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/specializes.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdUtils/usdzPackage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/plane.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdSkel/animation.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/blendShape.h>
#include <pxr/usd/usdSkel/skeleton.h>

#include <algorithm>

#if (defined(__cpp_exceptions) && __cpp_exceptions) || (defined(__EXCEPTIONS) && __EXCEPTIONS) || defined(_CPPUNWIND)
#define USD_SCENE_LOADER_HAS_EXCEPTIONS 1
#endif

using namespace pxr;

namespace {

static constexpr const char *USD_META_KEY = "usd";
static constexpr const char *USD_PREVIEW_LIGHTING_MODE_SETTING = "filesystem/import/usd/preview_lighting_mode";

enum UsdPreviewLightingMode {
	USD_PREVIEW_LIGHTING_NEVER = 0,
	USD_PREVIEW_LIGHTING_WHEN_MISSING = 1,
	USD_PREVIEW_LIGHTING_ALWAYS = 2,
};

String _to_godot_string(const std::string &p_string) {
	return String::utf8(p_string.c_str());
}

String _get_project_path(const String &p_path) {
	return ResourceUID::ensure_path(p_path);
}

String _get_absolute_path(const String &p_path) {
	const String project_path = _get_project_path(p_path);
	if (project_path.begins_with("res://") || project_path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(project_path);
	}
	return project_path;
}

static constexpr const char *USD_STAGE_INSTANCE_GENERATED_META = "usd_stage_instance_generated";

bool _is_usd_scene_extension(const String &p_extension) {
	const String extension = p_extension.to_lower();
	return extension == "usd" || extension == "usda" || extension == "usdc" || extension == "usdz";
}

bool _is_stage_instance_generated_root_node(Node *p_node) {
	if (p_node == nullptr) {
		return false;
	}

	if (p_node->has_meta(USD_STAGE_INSTANCE_GENERATED_META) && (bool)p_node->get_meta(USD_STAGE_INSTANCE_GENERATED_META)) {
		return true;
	}

	return p_node->get_name() == StringName("_Generated");
}

struct VariantSelectionRequest {
	String prim_path;
	String variant_set;
	String selection;
	int path_depth = 0;
};

int _get_prim_path_depth(const String &p_prim_path) {
	int depth = 0;
	for (int i = 0; i < p_prim_path.length(); i++) {
		if (p_prim_path[i] == '/') {
			depth++;
		}
	}
	return depth;
}

bool _is_prim_path_at_or_under(const String &p_prim_path, const String &p_ancestor_path) {
	if (p_prim_path == p_ancestor_path) {
		return true;
	}
	if (p_ancestor_path == "/") {
		return p_prim_path.begins_with("/");
	}
	return p_prim_path.begins_with(p_ancestor_path + "/");
}

Dictionary _collect_stage_metadata(const UsdStageRefPtr &p_stage) {
	Dictionary metadata;
	ERR_FAIL_COND_V(p_stage == nullptr, metadata);

	metadata["usd:source_identifier"] = _to_godot_string(p_stage->GetRootLayer()->GetIdentifier());
	metadata["usd:up_axis"] = _to_godot_string(UsdGeomGetStageUpAxis(p_stage).GetString());
	metadata["usd:meters_per_unit"] = UsdGeomGetStageMetersPerUnit(p_stage);
	if (UsdPrim default_prim = p_stage->GetDefaultPrim()) {
		metadata["usd:default_prim_path"] = _to_godot_string(default_prim.GetPath().GetString());
	}

	return metadata;
}

Dictionary _collect_variant_sets(const UsdStageRefPtr &p_stage) {
	Dictionary variant_catalog;
	ERR_FAIL_COND_V(p_stage == nullptr, variant_catalog);

	for (const UsdPrim &prim : p_stage->TraverseAll()) {
		UsdVariantSets variant_sets = prim.GetVariantSets();
		std::vector<std::string> set_names;
		variant_sets.GetNames(&set_names);
		if (set_names.empty()) {
			continue;
		}

		Dictionary prim_variant_sets;
		for (const std::string &set_name : set_names) {
			UsdVariantSet variant_set = variant_sets.GetVariantSet(set_name);
			if (!variant_set) {
				continue;
			}

			const std::vector<std::string> variant_names = variant_set.GetVariantNames();
			Array variants;
			for (const std::string &variant_name : variant_names) {
				variants.push_back(_to_godot_string(variant_name));
			}

			Dictionary set_description;
			set_description["prim_path"] = _to_godot_string(prim.GetPath().GetString());
			set_description["variant_set"] = _to_godot_string(set_name);
			set_description["variants"] = variants;
			set_description["selection"] = _to_godot_string(variant_set.GetVariantSelection());
			prim_variant_sets[_to_godot_string(set_name)] = set_description;
		}

		if (!prim_variant_sets.is_empty()) {
			variant_catalog[_to_godot_string(prim.GetPath().GetString())] = prim_variant_sets;
		}
	}

	return variant_catalog;
}

bool _set_variant_selection(const UsdStageRefPtr &p_stage, const String &p_prim_path, const String &p_variant_set_name, const String &p_selection, bool p_warn_missing = true) {
	ERR_FAIL_COND_V(p_stage == nullptr, false);
	if (p_prim_path.is_empty() || p_variant_set_name.is_empty() || p_selection.is_empty()) {
		return false;
	}

	UsdPrim prim = p_stage->GetPrimAtPath(SdfPath(p_prim_path.utf8().get_data()));
	if (!prim) {
		if (p_warn_missing) {
			WARN_PRINT(vformat("USD variant selection ignored because prim does not exist: %s", p_prim_path));
		}
		return false;
	}

	UsdVariantSet variant_set = prim.GetVariantSets().GetVariantSet(TfToken(p_variant_set_name.utf8().get_data()));
	if (!variant_set) {
		if (p_warn_missing) {
			WARN_PRINT(vformat("USD variant selection ignored because variant set does not exist: %s:%s", p_prim_path, p_variant_set_name));
		}
		return false;
	}

	if (!variant_set.SetVariantSelection(p_selection.utf8().get_data())) {
		ERR_PRINT(vformat("USD variant selection failed: %s:%s=%s", p_prim_path, p_variant_set_name, p_selection));
		return false;
	}

	return true;
}

void _add_variant_selection_request(std::vector<VariantSelectionRequest> &r_requests, const String &p_prim_path, const String &p_variant_set_name, const String &p_selection) {
	if (p_prim_path.is_empty() || p_variant_set_name.is_empty() || p_selection.is_empty()) {
		return;
	}

	VariantSelectionRequest request;
	request.prim_path = p_prim_path;
	request.variant_set = p_variant_set_name;
	request.selection = p_selection;
	request.path_depth = _get_prim_path_depth(p_prim_path);
	r_requests.push_back(request);
}

void _apply_variant_selections(const UsdStageRefPtr &p_stage, const Dictionary &p_variant_selections) {
	std::vector<VariantSelectionRequest> requests;
	for (const KeyValue<Variant, Variant> &prim_entry : p_variant_selections) {
		if (prim_entry.value.get_type() == Variant::DICTIONARY) {
			const String prim_path = prim_entry.key;
			Dictionary prim_selections = prim_entry.value;
			for (const KeyValue<Variant, Variant> &set_entry : prim_selections) {
				if (set_entry.value.get_type() != Variant::STRING && set_entry.value.get_type() != Variant::STRING_NAME) {
					continue;
				}
				_add_variant_selection_request(requests, prim_path, set_entry.key, set_entry.value);
			}
			continue;
		}

		if (prim_entry.value.get_type() != Variant::STRING && prim_entry.value.get_type() != Variant::STRING_NAME) {
			continue;
		}

		const String flat_key = prim_entry.key;
		const int separator = flat_key.rfind(":");
		if (separator <= 0) {
			continue;
		}

		const String prim_path = flat_key.substr(0, separator);
		const String variant_set_name = flat_key.substr(separator + 1);
		_add_variant_selection_request(requests, prim_path, variant_set_name, prim_entry.value);
	}

	std::sort(requests.begin(), requests.end(), [](const VariantSelectionRequest &p_left, const VariantSelectionRequest &p_right) {
		if (p_left.path_depth == p_right.path_depth) {
			return p_left.prim_path < p_right.prim_path;
		}
		return p_left.path_depth < p_right.path_depth;
	});

	for (const VariantSelectionRequest &request : requests) {
		_set_variant_selection(p_stage, request.prim_path, request.variant_set, request.selection, false);
	}
}

UsdStageRefPtr _open_stage_for_instance(const String &p_source_path, const Dictionary &p_variant_selections = Dictionary()) {
	const String absolute_path = _get_absolute_path(p_source_path);
	SdfLayerRefPtr root_layer = SdfLayer::FindOrOpen(absolute_path.utf8().get_data());
	ERR_FAIL_COND_V_MSG(!root_layer, nullptr, vformat("Failed to open USD root layer: %s", p_source_path));

	SdfLayerRefPtr session_layer = SdfLayer::CreateAnonymous("GodotUsdStageInstanceSession.usda");
	UsdStageRefPtr stage = UsdStage::Open(root_layer, session_layer, UsdStage::LoadAll);
	ERR_FAIL_COND_V_MSG(!stage, nullptr, vformat("Failed to compose USD stage: %s", p_source_path));

	if (!p_variant_selections.is_empty()) {
		stage->SetEditTarget(session_layer);
		_apply_variant_selections(stage, p_variant_selections);
		stage = UsdStage::Open(root_layer, session_layer, UsdStage::LoadAll);
		ERR_FAIL_COND_V_MSG(!stage, nullptr, vformat("Failed to recompose USD stage with variant selections: %s", p_source_path));
	}

	return stage;
}

UsdPreviewLightingMode _get_preview_lighting_mode() {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	const int configured_mode = project_settings != nullptr ? (int)project_settings->get_setting(USD_PREVIEW_LIGHTING_MODE_SETTING, USD_PREVIEW_LIGHTING_WHEN_MISSING) : USD_PREVIEW_LIGHTING_WHEN_MISSING;

	switch (configured_mode) {
		case USD_PREVIEW_LIGHTING_NEVER:
			return USD_PREVIEW_LIGHTING_NEVER;
		case USD_PREVIEW_LIGHTING_ALWAYS:
			return USD_PREVIEW_LIGHTING_ALWAYS;
		default:
			return USD_PREVIEW_LIGHTING_WHEN_MISSING;
	}
}

String _preview_lighting_mode_to_string(UsdPreviewLightingMode p_mode) {
	switch (p_mode) {
		case USD_PREVIEW_LIGHTING_NEVER:
			return "never";
		case USD_PREVIEW_LIGHTING_ALWAYS:
			return "always";
		default:
			return "when_missing";
	}
}

Dictionary _get_usd_metadata(const Object *p_object) {
	Variant metadata = p_object->get_meta(StringName(USD_META_KEY), Dictionary());
	if (metadata.get_type() == Variant::DICTIONARY) {
		return metadata;
	}
	return Dictionary();
}

void _set_usd_metadata(Object *p_object, const String &p_name, const Variant &p_value) {
	Dictionary metadata = _get_usd_metadata(p_object);
	metadata[p_name] = p_value;
	p_object->set_meta(StringName(USD_META_KEY), metadata);
}

void _set_usd_metadata_entries(Object *p_object, const Dictionary &p_entries) {
	Dictionary metadata = _get_usd_metadata(p_object);
	Array keys = p_entries.keys();
	for (int i = 0; i < keys.size(); i++) {
		metadata[keys[i]] = p_entries[keys[i]];
	}
	p_object->set_meta(StringName(USD_META_KEY), metadata);
}

Transform3D _gf_matrix_to_transform(const GfMatrix4d &p_matrix) {
	Basis basis(
			(real_t)p_matrix[0][0], (real_t)p_matrix[1][0], (real_t)p_matrix[2][0],
			(real_t)p_matrix[0][1], (real_t)p_matrix[1][1], (real_t)p_matrix[2][1],
			(real_t)p_matrix[0][2], (real_t)p_matrix[1][2], (real_t)p_matrix[2][2]);
	Vector3 origin((real_t)p_matrix[3][0], (real_t)p_matrix[3][1], (real_t)p_matrix[3][2]);
	return Transform3D(basis, origin);
}

GfMatrix4d _transform_to_gf_matrix(const Transform3D &p_transform) {
	GfMatrix4d matrix(1.0);
	const Vector3 x = p_transform.basis.get_column(0);
	const Vector3 y = p_transform.basis.get_column(1);
	const Vector3 z = p_transform.basis.get_column(2);

	matrix[0][0] = x.x;
	matrix[1][0] = x.y;
	matrix[2][0] = x.z;
	matrix[0][1] = y.x;
	matrix[1][1] = y.y;
	matrix[2][1] = y.z;
	matrix[0][2] = z.x;
	matrix[1][2] = z.y;
	matrix[2][2] = z.z;
	matrix[3][0] = p_transform.origin.x;
	matrix[3][1] = p_transform.origin.y;
	matrix[3][2] = p_transform.origin.z;

	return matrix;
}

Dictionary _serialize_attribute(const UsdAttribute &p_attribute, const UsdTimeCode &p_time) {
	Dictionary description;
	description["type_name"] = _to_godot_string(p_attribute.GetTypeName().GetAsToken().GetString());
	description["is_custom"] = p_attribute.IsCustom();

	VtValue value;
	if (p_attribute.Get(&value, p_time)) {
		description["value"] = _to_godot_string(TfStringify(value));

		const SdfValueTypeName type_name = p_attribute.GetTypeName();
		if (type_name == SdfValueTypeNames->Bool) {
			description["typed_value_kind"] = "bool";
			description["typed_value"] = value.UncheckedGet<bool>();
		} else if (type_name == SdfValueTypeNames->Int) {
			description["typed_value_kind"] = "int";
			description["typed_value"] = value.UncheckedGet<int>();
		} else if (type_name == SdfValueTypeNames->Int64) {
			description["typed_value_kind"] = "int64";
			description["typed_value"] = (int64_t)value.UncheckedGet<int64_t>();
		} else if (type_name == SdfValueTypeNames->Float) {
			description["typed_value_kind"] = "float";
			description["typed_value"] = value.UncheckedGet<float>();
		} else if (type_name == SdfValueTypeNames->Double) {
			description["typed_value_kind"] = "double";
			description["typed_value"] = value.UncheckedGet<double>();
		} else if (type_name == SdfValueTypeNames->String) {
			description["typed_value_kind"] = "string";
			description["typed_value"] = _to_godot_string(value.UncheckedGet<std::string>());
		} else if (type_name == SdfValueTypeNames->Token) {
			description["typed_value_kind"] = "token";
			description["typed_value"] = _to_godot_string(value.UncheckedGet<TfToken>().GetString());
		} else if (type_name == SdfValueTypeNames->Asset) {
			description["typed_value_kind"] = "asset";
			description["typed_value"] = _to_godot_string(value.UncheckedGet<SdfAssetPath>().GetAssetPath());
		} else if (type_name == SdfValueTypeNames->Float2 || type_name == SdfValueTypeNames->TexCoord2f) {
			const GfVec2f vec = value.UncheckedGet<GfVec2f>();
			description["typed_value_kind"] = "vector2";
			description["typed_value"] = Vector2(vec[0], vec[1]);
		} else if (type_name == SdfValueTypeNames->Float3 || type_name == SdfValueTypeNames->Color3f || type_name == SdfValueTypeNames->Normal3f || type_name == SdfValueTypeNames->Point3f || type_name == SdfValueTypeNames->Vector3f) {
			const GfVec3f vec = value.UncheckedGet<GfVec3f>();
			description["typed_value_kind"] = "vector3";
			description["typed_value"] = Vector3(vec[0], vec[1], vec[2]);
		} else if (type_name == SdfValueTypeNames->BoolArray) {
			const VtArray<bool> values = value.UncheckedGet<VtArray<bool>>();
			Array serialized_values;
			for (size_t i = 0; i < values.size(); i++) {
				serialized_values.push_back(values[i]);
			}
			description["typed_value_kind"] = "bool_array";
			description["typed_value"] = serialized_values;
		} else if (type_name == SdfValueTypeNames->IntArray) {
			const VtArray<int> values = value.UncheckedGet<VtArray<int>>();
			Array serialized_values;
			for (size_t i = 0; i < values.size(); i++) {
				serialized_values.push_back(values[i]);
			}
			description["typed_value_kind"] = "int_array";
			description["typed_value"] = serialized_values;
		} else if (type_name == SdfValueTypeNames->Int64Array) {
			const VtArray<int64_t> values = value.UncheckedGet<VtArray<int64_t>>();
			Array serialized_values;
			for (size_t i = 0; i < values.size(); i++) {
				serialized_values.push_back((int64_t)values[i]);
			}
			description["typed_value_kind"] = "int64_array";
			description["typed_value"] = serialized_values;
		} else if (type_name == SdfValueTypeNames->FloatArray) {
			const VtArray<float> values = value.UncheckedGet<VtArray<float>>();
			Array serialized_values;
			for (size_t i = 0; i < values.size(); i++) {
				serialized_values.push_back(values[i]);
			}
			description["typed_value_kind"] = "float_array";
			description["typed_value"] = serialized_values;
		} else if (type_name == SdfValueTypeNames->DoubleArray) {
			const VtArray<double> values = value.UncheckedGet<VtArray<double>>();
			Array serialized_values;
			for (size_t i = 0; i < values.size(); i++) {
				serialized_values.push_back(values[i]);
			}
			description["typed_value_kind"] = "double_array";
			description["typed_value"] = serialized_values;
		} else if (type_name == SdfValueTypeNames->StringArray) {
			const VtArray<std::string> values = value.UncheckedGet<VtArray<std::string>>();
			Array serialized_values;
			for (size_t i = 0; i < values.size(); i++) {
				serialized_values.push_back(_to_godot_string(values[i]));
			}
			description["typed_value_kind"] = "string_array";
			description["typed_value"] = serialized_values;
		} else if (type_name == SdfValueTypeNames->TokenArray) {
			const VtArray<TfToken> values = value.UncheckedGet<VtArray<TfToken>>();
			Array serialized_values;
			for (size_t i = 0; i < values.size(); i++) {
				serialized_values.push_back(_to_godot_string(values[i].GetString()));
			}
			description["typed_value_kind"] = "token_array";
			description["typed_value"] = serialized_values;
		} else if (type_name == SdfValueTypeNames->Float2Array || type_name == SdfValueTypeNames->TexCoord2fArray) {
			const VtArray<GfVec2f> values = value.UncheckedGet<VtArray<GfVec2f>>();
			Array serialized_values;
			for (size_t i = 0; i < values.size(); i++) {
				serialized_values.push_back(Vector2(values[i][0], values[i][1]));
			}
			description["typed_value_kind"] = "vector2_array";
			description["typed_value"] = serialized_values;
		} else if (type_name == SdfValueTypeNames->Float3Array || type_name == SdfValueTypeNames->Color3fArray || type_name == SdfValueTypeNames->Normal3fArray || type_name == SdfValueTypeNames->Point3fArray || type_name == SdfValueTypeNames->Vector3fArray) {
			const VtArray<GfVec3f> values = value.UncheckedGet<VtArray<GfVec3f>>();
			Array serialized_values;
			for (size_t i = 0; i < values.size(); i++) {
				serialized_values.push_back(Vector3(values[i][0], values[i][1], values[i][2]));
			}
			description["typed_value_kind"] = "vector3_array";
			description["typed_value"] = serialized_values;
		}
	}

	return description;
}

Dictionary _serialize_relationships(const UsdPrim &p_prim) {
	Dictionary relationships;
	UsdRelationshipVector authored_relationships = p_prim.GetAuthoredRelationships();
	for (const UsdRelationship &relationship : authored_relationships) {
		SdfPathVector targets;
		relationship.GetTargets(&targets);

		Array target_paths;
		for (const SdfPath &target : targets) {
			target_paths.push_back(_to_godot_string(target.GetString()));
		}

		Dictionary relationship_description;
		relationship_description["is_custom"] = relationship.IsCustom();
		relationship_description["targets"] = target_paths;
		relationships[_to_godot_string(relationship.GetName().GetString())] = relationship_description;
	}
	return relationships;
}

Dictionary _serialize_layer_offset(const SdfLayerOffset &p_layer_offset) {
	Dictionary description;
	description["offset"] = p_layer_offset.GetOffset();
	description["scale"] = p_layer_offset.GetScale();
	return description;
}

Dictionary _serialize_reference(const SdfReference &p_reference) {
	Dictionary description;
	description["asset_path"] = _to_godot_string(p_reference.GetAssetPath());
	description["prim_path"] = _to_godot_string(p_reference.GetPrimPath().GetString());
	description["layer_offset"] = _serialize_layer_offset(p_reference.GetLayerOffset());
	if (!p_reference.GetCustomData().empty()) {
		description["custom_data_status"] = "deferred";
		description["custom_data_debug"] = _to_godot_string(TfStringify(p_reference.GetCustomData()));
	}
	return description;
}

Dictionary _serialize_payload(const SdfPayload &p_payload) {
	Dictionary description;
	description["asset_path"] = _to_godot_string(p_payload.GetAssetPath());
	description["prim_path"] = _to_godot_string(p_payload.GetPrimPath().GetString());
	description["layer_offset"] = _serialize_layer_offset(p_payload.GetLayerOffset());
	return description;
}

Array _serialize_path_vector(const SdfPathVector &p_paths) {
	Array paths;
	for (const SdfPath &path : p_paths) {
		paths.push_back(_to_godot_string(path.GetString()));
	}
	return paths;
}

Array _serialize_authored_references(const UsdPrim &p_prim) {
	Array references;
	HashSet<String> seen_references;
	const SdfPrimSpecHandleVector prim_stack = p_prim.GetPrimStack();
	for (const SdfPrimSpecHandle &prim_spec : prim_stack) {
		if (!prim_spec || !prim_spec->HasReferences()) {
			continue;
		}

		const SdfReferenceVector authored_references = prim_spec->GetReferenceList().GetAddedOrExplicitItems();
		for (const SdfReference &reference : authored_references) {
			const String key = vformat("%s|%s|%s|%s",
					_to_godot_string(reference.GetAssetPath()),
					_to_godot_string(reference.GetPrimPath().GetString()),
					String::num_real(reference.GetLayerOffset().GetOffset()),
					String::num_real(reference.GetLayerOffset().GetScale()));
			if (seen_references.has(key)) {
				continue;
			}
			seen_references.insert(key);
			references.push_back(_serialize_reference(reference));
		}
	}
	return references;
}

Array _serialize_authored_payloads(const UsdPrim &p_prim) {
	Array payloads;
	HashSet<String> seen_payloads;
	const SdfPrimSpecHandleVector prim_stack = p_prim.GetPrimStack();
	for (const SdfPrimSpecHandle &prim_spec : prim_stack) {
		if (!prim_spec || !prim_spec->HasPayloads()) {
			continue;
		}

		const SdfPayloadVector authored_payloads = prim_spec->GetPayloadList().GetAddedOrExplicitItems();
		for (const SdfPayload &payload : authored_payloads) {
			const String key = vformat("%s|%s|%s|%s",
					_to_godot_string(payload.GetAssetPath()),
					_to_godot_string(payload.GetPrimPath().GetString()),
					String::num_real(payload.GetLayerOffset().GetOffset()),
					String::num_real(payload.GetLayerOffset().GetScale()));
			if (seen_payloads.has(key)) {
				continue;
			}
			seen_payloads.insert(key);
			payloads.push_back(_serialize_payload(payload));
		}
	}
	return payloads;
}

Array _serialize_authored_inherits(const UsdPrim &p_prim) {
	SdfPathVector paths;
	HashSet<String> seen_paths;
	const SdfPrimSpecHandleVector prim_stack = p_prim.GetPrimStack();
	for (const SdfPrimSpecHandle &prim_spec : prim_stack) {
		if (!prim_spec || !prim_spec->HasInheritPaths()) {
			continue;
		}

		const SdfPathVector authored_paths = prim_spec->GetInheritPathList().GetAddedOrExplicitItems();
		for (const SdfPath &path : authored_paths) {
			const String key = _to_godot_string(path.GetString());
			if (seen_paths.has(key)) {
				continue;
			}
			seen_paths.insert(key);
			paths.push_back(path);
		}
	}
	return _serialize_path_vector(paths);
}

Array _serialize_authored_specializes(const UsdPrim &p_prim) {
	SdfPathVector paths;
	HashSet<String> seen_paths;
	const SdfPrimSpecHandleVector prim_stack = p_prim.GetPrimStack();
	for (const SdfPrimSpecHandle &prim_spec : prim_stack) {
		if (!prim_spec || !prim_spec->HasSpecializes()) {
			continue;
		}

		const SdfPathVector authored_paths = prim_spec->GetSpecializesList().GetAddedOrExplicitItems();
		for (const SdfPath &path : authored_paths) {
			const String key = _to_godot_string(path.GetString());
			if (seen_paths.has(key)) {
				continue;
			}
			seen_paths.insert(key);
			paths.push_back(path);
		}
	}
	return _serialize_path_vector(paths);
}

void _store_composition_arcs(const UsdPrim &p_prim, Object *p_target) {
	const Array serialized_references = _serialize_authored_references(p_prim);
	if (!serialized_references.is_empty()) {
		_set_usd_metadata(p_target, "usd:references", serialized_references);
		_set_usd_metadata(p_target, "usd:composition_preservation_mode", "read_only");
	}

	const Array serialized_payloads = _serialize_authored_payloads(p_prim);
	if (!serialized_payloads.is_empty()) {
		_set_usd_metadata(p_target, "usd:payloads", serialized_payloads);
		_set_usd_metadata(p_target, "usd:composition_preservation_mode", "read_only");
	}

	const Array serialized_inherits = _serialize_authored_inherits(p_prim);
	if (!serialized_inherits.is_empty()) {
		_set_usd_metadata(p_target, "usd:inherits", serialized_inherits);
		_set_usd_metadata(p_target, "usd:composition_preservation_mode", "read_only");
	}

	const Array serialized_specializes = _serialize_authored_specializes(p_prim);
	if (!serialized_specializes.is_empty()) {
		_set_usd_metadata(p_target, "usd:specializes", serialized_specializes);
		_set_usd_metadata(p_target, "usd:composition_preservation_mode", "read_only");
	}
}

void _mark_owner_recursive(Node *p_node, Node *p_owner) {
	if (p_node != p_owner) {
		p_node->set_owner(p_owner);
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_mark_owner_recursive(p_node->get_child(i), p_owner);
	}
}

Transform3D _make_local_axis_correction(const TfToken &p_axis) {
	if (p_axis == UsdGeomTokens->x) {
		return Transform3D(Basis(Vector3(0, 0, 1), (real_t)-Math::PI * 0.5));
	}
	if (p_axis == UsdGeomTokens->z) {
		return Transform3D(Basis(Vector3(1, 0, 0), (real_t)Math::PI * 0.5));
	}
	return Transform3D();
}

String _normalize_usd_asset_path(const String &p_path) {
	return p_path.replace("\\", "/");
}

template <typename T>
bool _read_interpolated_value(const VtArray<T> &p_values, const TfToken &p_interpolation, int p_face_index, int p_face_vertex_index, int p_point_index, T *r_value) {
	int value_index = -1;
	if (p_interpolation == UsdGeomTokens->constant) {
		value_index = 0;
	} else if (p_interpolation == UsdGeomTokens->uniform) {
		value_index = p_face_index;
	} else if (p_interpolation == UsdGeomTokens->faceVarying) {
		value_index = p_face_vertex_index;
	} else {
		value_index = p_point_index;
	}

	if (value_index < 0 || value_index >= (int)p_values.size()) {
		return false;
	}

	*r_value = p_values[value_index];
	return true;
}

int _get_interpolated_value_index(const TfToken &p_interpolation, int p_face_index, int p_face_vertex_index, int p_point_index, int p_value_count) {
	int value_index = -1;
	if (p_interpolation == UsdGeomTokens->constant) {
		value_index = 0;
	} else if (p_interpolation == UsdGeomTokens->uniform) {
		value_index = p_face_index;
	} else if (p_interpolation == UsdGeomTokens->faceVarying) {
		value_index = p_face_vertex_index;
	} else {
		value_index = p_point_index;
	}

	if (value_index < 0 || value_index >= p_value_count) {
		return -1;
	}

	return value_index;
}

struct UsdSurfaceAccumulator {
	PackedVector3Array vertices;
	PackedInt32Array indices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array bones;
	PackedFloat32Array weights;
	PackedInt32Array authored_face_indices;
	PackedInt32Array authored_point_indices;
	Ref<Material> material;
	String usd_material_path;
	bool has_material_binding = false;
	String subset_path;
	String binding_kind;
	String subset_name;
	String family_name;
	String family_type;
};

struct UsdMeshBuildResult {
	Ref<ArrayMesh> mesh;
	Array material_paths;
	Array material_subsets;
	Array geom_subsets;
	bool has_skinning = false;
};

struct UsdSkinningData {
	VtArray<int> joint_indices_values;
	VtArray<float> joint_weights_values;
	TfToken joint_indices_interpolation = UsdGeomTokens->vertex;
	TfToken joint_weights_interpolation = UsdGeomTokens->vertex;
	int joint_indices_element_size = 0;
	int joint_weights_element_size = 0;
	bool valid = false;
	bool has_authored_joint_indices = false;
	bool has_authored_joint_weights = false;
};

struct UsdInbetweenShapeData {
	String name;
	float weight = 0.0f;
	HashMap<int, Vector3> position_offsets_by_point;
	HashMap<int, Vector3> normal_offsets_by_point;
};

struct UsdBlendShapeData {
	String name;
	String target_path;
	HashMap<int, Vector3> position_offsets_by_point;
	HashMap<int, Vector3> normal_offsets_by_point;
	Vector<UsdInbetweenShapeData> inbetweens;
	Array inbetweens_metadata;
};

struct UsdBlendShapeChannelSpec {
	String channel_name;
	float weight = 0.0f;
	bool primary = false;
};

struct UsdMeshSurfaceFaceRange {
	int face_start = 0;
	int face_count = 0;
	Vector<int> saved_face_indices;
	HashMap<int, Vector<int>> saved_point_indices_by_authored_point;
};

static uint64_t _make_sorted_pair_key(int p_a, int p_b) {
	const uint32_t a = (uint32_t)MIN(p_a, p_b);
	const uint32_t b = (uint32_t)MAX(p_a, p_b);
	return (uint64_t(a) << 32) | uint64_t(b);
}

static UsdGeomSubset _define_preserved_subset(const UsdGeomMesh &p_usd_mesh, const String &p_subset_path, const String &p_subset_name, const TfToken &p_element_type, const VtIntArray &p_indices, const TfToken &p_family_name, const TfToken &p_family_type) {
	SdfPath subset_path;
	if (!p_subset_path.is_empty()) {
		const SdfPath preferred_path(p_subset_path.utf8().get_data());
		if (preferred_path.IsAbsolutePath() && preferred_path.GetParentPath() == p_usd_mesh.GetPath()) {
			subset_path = preferred_path;
		}
	}
	if (subset_path.IsEmpty()) {
		String subset_name = _to_godot_string(TfMakeValidIdentifier((p_subset_name.is_empty() ? String("GeomSubset") : p_subset_name).strip_edges().utf8().get_data()));
		if (subset_name.is_empty()) {
			subset_name = "GeomSubset";
		}
		subset_path = p_usd_mesh.GetPath().AppendChild(TfToken(subset_name.utf8().get_data()));
	}

	UsdGeomSubset subset = UsdGeomSubset::Define(p_usd_mesh.GetPrim().GetStage(), subset_path);
	if (!subset) {
		return UsdGeomSubset();
	}

	subset.GetElementTypeAttr().Set(p_element_type);
	subset.GetIndicesAttr().Set(p_indices);
	if (!p_family_name.IsEmpty()) {
		subset.CreateFamilyNameAttr().Set(p_family_name);
		UsdGeomSubset::SetFamilyType(UsdGeomImageable(p_usd_mesh.GetPrim()), p_family_name, p_family_type.IsEmpty() ? UsdGeomTokens->nonOverlapping : p_family_type);
	}
	return subset;
}

static Array _build_preserved_geom_subsets(const UsdGeomMesh &p_mesh, const std::vector<UsdGeomSubset> &p_material_subsets, UsdTimeCode p_time) {
	Array preserved_subsets;
	HashSet<String> material_subset_paths;
	for (const UsdGeomSubset &material_subset : p_material_subsets) {
		material_subset_paths.insert(_to_godot_string(material_subset.GetPath().GetString()));
	}

	for (const UsdPrim &child : p_mesh.GetPrim().GetChildren()) {
		if (!child.IsA<UsdGeomSubset>()) {
			continue;
		}

		UsdGeomSubset subset(child);
		const String subset_path = _to_godot_string(subset.GetPath().GetString());
		if (material_subset_paths.has(subset_path)) {
			continue;
		}

		TfToken element_type;
		subset.GetElementTypeAttr().Get(&element_type, p_time);
		if (element_type != UsdGeomTokens->face && element_type != UsdGeomTokens->point && element_type != UsdGeomTokens->edge) {
			continue;
		}

		VtIntArray authored_indices;
		if (!subset.GetIndicesAttr().Get(&authored_indices, p_time) || authored_indices.empty()) {
			continue;
		}

		TfToken family_name;
		subset.GetFamilyNameAttr().Get(&family_name, p_time);
		const TfToken family_type = family_name.IsEmpty() ? TfToken() : UsdGeomSubset::GetFamilyType(UsdGeomImageable(p_mesh.GetPrim()), family_name);

		Dictionary subset_description;
		subset_description["subset_path"] = _to_godot_string(subset.GetPath().GetString());
		subset_description["subset_name"] = _to_godot_string(subset.GetPrim().GetName().GetString());
		subset_description["element_type"] = _to_godot_string(element_type.GetString());
		if (!family_name.IsEmpty()) {
			subset_description["family_name"] = _to_godot_string(family_name.GetString());
		}
		if (!family_type.IsEmpty()) {
			subset_description["family_type"] = _to_godot_string(family_type.GetString());
		}

		PackedInt32Array indices;
		for (int i = 0; i < (int)authored_indices.size(); i++) {
			indices.push_back(authored_indices[i]);
		}
		subset_description["indices"] = indices;
		preserved_subsets.push_back(subset_description);
	}

	return preserved_subsets;
}

class UsdSceneBuilder {
	const UsdStageRefPtr stage;
	const UsdTimeCode time = UsdTimeCode::Default();
	const double meters_per_unit = 1.0;
	const TfToken up_axis;
	const Dictionary variant_catalog;
	mutable HashMap<String, Ref<Image>> image_cache;
	mutable HashMap<String, Ref<Texture2D>> texture_cache;
	mutable HashMap<String, Ref<Material>> material_cache;

	struct VariantContextEntry {
		String prim_path;
		String variant_set;
		String selection;
		int path_depth = 0;
	};

	struct UsdShaderConnection {
		UsdShadeShader shader;
		TfToken output_name;

		UsdShaderConnection() = default;
		UsdShaderConnection(const UsdShadeShader &p_shader, const TfToken &p_output_name) :
				shader(p_shader),
				output_name(p_output_name) {}

		explicit operator bool() const { return shader.GetPrim().IsValid(); }
	};

	static String _node_name_for_prim(const UsdPrim &p_prim) {
		return _to_godot_string(p_prim.GetName().GetString());
	}

	static real_t _meters_scale(double p_meters_per_unit) {
		return (real_t)p_meters_per_unit;
	}

	Transform3D _get_stage_correction_transform() const {
		const real_t unit_scale = _meters_scale(meters_per_unit);
		Basis root_basis;
		if (up_axis == UsdGeomTokens->z) {
			root_basis = Basis(Vector3(1, 0, 0), (real_t)-Math::PI * 0.5);
		}
		root_basis = root_basis.scaled(Vector3(unit_scale, unit_scale, unit_scale));
		return Transform3D(root_basis, Vector3());
	}

	Dictionary _get_local_variant_sets(const String &p_prim_path) const {
		const Variant local_variant_sets = variant_catalog.get(p_prim_path, Variant());
		if (local_variant_sets.get_type() == Variant::DICTIONARY) {
			return local_variant_sets;
		}
		return Dictionary();
	}

	Array _get_variant_context(const String &p_prim_path) const {
		std::vector<VariantContextEntry> context_entries;

		for (const KeyValue<Variant, Variant> &prim_entry : variant_catalog) {
			if (prim_entry.value.get_type() != Variant::DICTIONARY) {
				continue;
			}

			const String variant_owner_path = prim_entry.key;
			if (!_is_prim_path_at_or_under(p_prim_path, variant_owner_path)) {
				continue;
			}

			const Dictionary prim_variant_sets = prim_entry.value;
			for (const KeyValue<Variant, Variant> &set_entry : prim_variant_sets) {
				if (set_entry.value.get_type() != Variant::DICTIONARY) {
					continue;
				}

				const Dictionary set_description = set_entry.value;
				const String selection = set_description.get("selection", String());
				if (selection.is_empty()) {
					continue;
				}

				VariantContextEntry entry;
				entry.prim_path = variant_owner_path;
				entry.variant_set = set_entry.key;
				entry.selection = selection;
				entry.path_depth = _get_prim_path_depth(variant_owner_path);
				context_entries.push_back(entry);
			}
		}

		std::sort(context_entries.begin(), context_entries.end(), [](const VariantContextEntry &p_left, const VariantContextEntry &p_right) {
			if (p_left.path_depth != p_right.path_depth) {
				return p_left.path_depth < p_right.path_depth;
			}
			if (p_left.prim_path != p_right.prim_path) {
				return p_left.prim_path < p_right.prim_path;
			}
			return p_left.variant_set < p_right.variant_set;
		});

		Array context;
		for (const VariantContextEntry &entry : context_entries) {
			Dictionary description;
			description["prim_path"] = entry.prim_path;
			description["variant_set"] = entry.variant_set;
			description["selection"] = entry.selection;
			context.push_back(description);
		}
		return context;
	}

	Dictionary _make_common_metadata(const UsdPrim &p_prim) const {
		Dictionary metadata;
		const String prim_path = _to_godot_string(p_prim.GetPath().GetString());
		metadata["usd:prim_path"] = prim_path;
		metadata["usd:type_name"] = _to_godot_string(p_prim.GetTypeName().GetString());
		metadata["usd:active"] = p_prim.IsActive();

		const Dictionary local_variant_sets = _get_local_variant_sets(prim_path);
		if (!local_variant_sets.is_empty()) {
			metadata["usd:variant_boundary"] = true;
			metadata["usd:variant_sets"] = local_variant_sets;
		}

		const Array variant_context = _get_variant_context(prim_path);
		if (!variant_context.is_empty()) {
			metadata["usd:variant_context"] = variant_context;
		}

		Array applied_schemas;
		for (const TfToken &schema : p_prim.GetAppliedSchemas()) {
			applied_schemas.push_back(_to_godot_string(schema.GetString()));
		}
		metadata["usd:applied_schemas"] = applied_schemas;

		return metadata;
	}

	void _apply_transform_and_visibility(const UsdPrim &p_prim, Node *p_node, HashSet<String> *r_handled_attributes) const {
		if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
			UsdGeomXformable xformable(p_prim);
			UsdGeomImageable imageable(p_prim);
			if (xformable) {
				GfMatrix4d local_matrix(1.0);
				bool resets_xform_stack = false;
				const bool has_local_transform = xformable.GetLocalTransformation(&local_matrix, &resets_xform_stack, time);
				if (resets_xform_stack) {
					Transform3D corrected_world_transform;
					bool has_corrected_world_transform = false;
					if (imageable) {
						corrected_world_transform = _get_stage_correction_transform() * _gf_matrix_to_transform(imageable.ComputeLocalToWorldTransform(time));
						has_corrected_world_transform = true;
					} else if (has_local_transform) {
						corrected_world_transform = _get_stage_correction_transform() * _gf_matrix_to_transform(local_matrix);
						has_corrected_world_transform = true;
					}

					node_3d->set_as_top_level_keep_local(true);
					if (has_corrected_world_transform) {
						node_3d->set_global_transform(corrected_world_transform);
					}
					_set_usd_metadata(node_3d, "usd:resets_xform_stack", true);
				} else if (has_local_transform) {
					node_3d->set_transform(_gf_matrix_to_transform(local_matrix));
				}

				r_handled_attributes->insert("xformOpOrder");
			}

			if (imageable) {
				const TfToken visibility = imageable.ComputeVisibility(time);
				if (visibility == UsdGeomTokens->invisible) {
					node_3d->set_visible(false);
				}

				r_handled_attributes->insert("visibility");
				r_handled_attributes->insert("purpose");
			}
		}
	}

	bool _should_skip_child_prim(const UsdPrim &p_prim) const {
		if (p_prim.IsA<UsdGeomSubset>() || p_prim.IsA<UsdShadeMaterial>() || p_prim.IsA<UsdShadeShader>()) {
			return true;
		}

		const String prim_name = _to_godot_string(p_prim.GetName().GetString()).to_lower();
		if ((prim_name == "materials" || prim_name == "shaders") && !p_prim.IsA<UsdGeomImageable>()) {
			return true;
		}

		return false;
	}

	String _resolve_asset_path(const UsdAttribute &p_attribute, const SdfAssetPath &p_asset_path) const {
		const String authored_asset_path = _normalize_usd_asset_path(_to_godot_string(p_asset_path.GetAssetPath()));
		if (authored_asset_path.is_empty()) {
			if (!p_asset_path.GetResolvedPath().empty()) {
				return _normalize_usd_asset_path(_to_godot_string(p_asset_path.GetResolvedPath()));
			}
			return String();
		}

		const ArResolver &resolver = ArGetResolver();
		if (!p_asset_path.GetResolvedPath().empty()) {
			return _normalize_usd_asset_path(_to_godot_string(p_asset_path.GetResolvedPath()));
		}

		if (authored_asset_path.is_absolute_path()) {
			return authored_asset_path;
		}

		const SdfPropertySpecHandleVector property_stack = p_attribute.GetPropertyStack(time);
		for (int i = 0; i < (int)property_stack.size(); i++) {
			if (!property_stack[i]) {
				continue;
			}
			SdfLayerHandle layer = property_stack[i]->GetLayer();
			if (!layer) {
				continue;
			}

			const ArResolvedPath &layer_resolved_path = layer->GetResolvedPath();
			if (layer_resolved_path) {
				const std::string asset_identifier = resolver.CreateIdentifier(authored_asset_path.utf8().get_data(), layer_resolved_path);
				if (!asset_identifier.empty()) {
					return _normalize_usd_asset_path(_to_godot_string(asset_identifier));
				}
			}

			return _normalize_usd_asset_path(_to_godot_string(layer->ComputeAbsolutePath(authored_asset_path.utf8().get_data())));
		}

		return authored_asset_path;
	}

	String _get_asset_extension(const String &p_asset_path) const {
		String extension_path = _normalize_usd_asset_path(p_asset_path);
		const int package_delimiter = extension_path.rfind("[");
		if (package_delimiter != -1) {
			extension_path = extension_path.substr(package_delimiter + 1, extension_path.length() - package_delimiter - 1);
		}
		if (extension_path.ends_with("]")) {
			extension_path = extension_path.left(extension_path.length() - 1);
		}
		return extension_path.get_extension().to_lower();
	}

	Ref<Image> _load_image_from_asset_bytes(const Vector<uint8_t> &p_bytes, const String &p_asset_path, Dictionary *r_mapping_notes) const {
		if (p_bytes.is_empty()) {
			(*r_mapping_notes)["usd:texture_status"] = vformat("Texture asset is empty: %s", p_asset_path);
			return Ref<Image>();
		}

		Ref<Image> image;
		image.instantiate();

		const String extension = _get_asset_extension(p_asset_path);
		Error err = ERR_FILE_UNRECOGNIZED;
		if (extension == "png") {
			err = image->load_png_from_buffer(p_bytes);
		} else if (extension == "jpg" || extension == "jpeg") {
			err = image->load_jpg_from_buffer(p_bytes);
		} else if (extension == "webp") {
			err = image->load_webp_from_buffer(p_bytes);
		} else if (extension == "tga") {
			err = image->load_tga_from_buffer(p_bytes);
		} else if (extension == "bmp") {
			err = image->load_bmp_from_buffer(p_bytes);
		} else if (extension == "dds") {
			err = image->load_dds_from_buffer(p_bytes);
		} else if (extension == "ktx") {
			err = image->load_ktx_from_buffer(p_bytes);
		} else if (extension == "exr") {
			err = image->load_exr_from_buffer(p_bytes);
		} else {
			(*r_mapping_notes)["usd:texture_status"] = vformat("Unsupported texture extension for asset: %s", p_asset_path);
			return Ref<Image>();
		}

		if (err != OK) {
			(*r_mapping_notes)["usd:texture_status"] = vformat("Failed to decode texture asset: %s", p_asset_path);
			return Ref<Image>();
		}

		return image;
	}

	Ref<Image> _load_image_from_asset_attribute(const UsdAttribute &p_asset_attribute, String *r_resolved_path, Dictionary *r_mapping_notes) const {
		if (!p_asset_attribute) {
			return Ref<Image>();
		}

		SdfAssetPath asset_path;
		if (!p_asset_attribute.Get(&asset_path, time)) {
			(*r_mapping_notes)["usd:texture_status"] = vformat("Texture asset input could not be read: %s", _to_godot_string(p_asset_attribute.GetName().GetString()));
			return Ref<Image>();
		}

		const String resolved_path = _resolve_asset_path(p_asset_attribute, asset_path);
		if (resolved_path.is_empty()) {
			(*r_mapping_notes)["usd:texture_status"] = "Texture asset path could not be resolved.";
			return Ref<Image>();
		}
		if (r_resolved_path != nullptr) {
			*r_resolved_path = resolved_path;
		}

		if (image_cache.has(resolved_path)) {
			return image_cache[resolved_path];
		}

		ArResolvedPath usd_resolved_path(resolved_path.utf8().get_data());
		if (!asset_path.GetResolvedPath().empty()) {
			usd_resolved_path = ArResolvedPath(asset_path.GetResolvedPath());
		} else {
			usd_resolved_path = ArGetResolver().Resolve(resolved_path.utf8().get_data());
		}
		if (!usd_resolved_path) {
			(*r_mapping_notes)["usd:texture_status"] = vformat("Failed to resolve texture asset: %s", resolved_path);
			return Ref<Image>();
		}

		std::shared_ptr<ArAsset> usd_asset = ArGetResolver().OpenAsset(usd_resolved_path);
		if (!usd_asset) {
			(*r_mapping_notes)["usd:texture_status"] = vformat("Failed to open texture asset: %s", _to_godot_string(usd_resolved_path.GetPathString()));
			return Ref<Image>();
		}

		Vector<uint8_t> asset_bytes;
		const size_t asset_size = usd_asset->GetSize();
		if (asset_size > 0) {
			asset_bytes.resize((int)asset_size);
			std::shared_ptr<const char> asset_buffer = usd_asset->GetBuffer();
			if (asset_buffer) {
				memcpy(asset_bytes.ptrw(), asset_buffer.get(), asset_size);
			} else {
				const size_t bytes_read = usd_asset->Read(asset_bytes.ptrw(), asset_size, 0);
				if (bytes_read != asset_size) {
					(*r_mapping_notes)["usd:texture_status"] = vformat("Failed to read texture asset bytes: %s", _to_godot_string(usd_resolved_path.GetPathString()));
					return Ref<Image>();
				}
			}
		}

		const String display_asset_path = !asset_path.GetAssetPath().empty() ? _normalize_usd_asset_path(_to_godot_string(asset_path.GetAssetPath())) : _normalize_usd_asset_path(_to_godot_string(usd_resolved_path.GetPathString()));
		Ref<Image> image = _load_image_from_asset_bytes(asset_bytes, display_asset_path, r_mapping_notes);
		if (image.is_null()) {
			if (!r_mapping_notes->has("usd:texture_status")) {
				(*r_mapping_notes)["usd:texture_status"] = vformat("Failed to load texture asset: %s", display_asset_path);
			}
			return Ref<Image>();
		}

		image_cache.insert(resolved_path, image);
		return image;
	}

	Ref<Texture2D> _texture_from_image(const Ref<Image> &p_image) const {
		if (p_image.is_null()) {
			return Ref<Texture2D>();
		}
		return ImageTexture::create_from_image(p_image);
	}

	Ref<Texture2D> _load_texture_from_asset_attribute(const UsdAttribute &p_asset_attribute, Dictionary *r_mapping_notes) const {
		String resolved_path;
		Ref<Image> image = _load_image_from_asset_attribute(p_asset_attribute, &resolved_path, r_mapping_notes);
		if (image.is_null()) {
			return Ref<Texture2D>();
		}
		if (texture_cache.has(resolved_path)) {
			return texture_cache[resolved_path];
		}

		Ref<Texture2D> texture = _texture_from_image(image);
		if (texture.is_valid()) {
			texture_cache.insert(resolved_path, texture);
		}
		return texture;
	}

	UsdShaderConnection _get_connected_shader(const UsdShadeInput &p_input) const {
		if (!p_input || !p_input.HasConnectedSource()) {
			return UsdShaderConnection();
		}

		const UsdShadeInput::SourceInfoVector sources = p_input.GetConnectedSources();
		if (sources.empty()) {
			return UsdShaderConnection();
		}

		UsdPrim source_prim = stage->GetPrimAtPath(sources[0].source.GetPath());
		if (!source_prim) {
			return UsdShaderConnection();
		}

		UsdShadeShader shader(source_prim);
		if (!shader) {
			return UsdShaderConnection();
		}

		return UsdShaderConnection(shader, sources[0].sourceName);
	}

	bool _get_shader_id(const UsdShadeShader &p_shader, TfToken *r_shader_id) const {
		if (!p_shader || r_shader_id == nullptr) {
			return false;
		}

		return p_shader.GetPrim().GetAttribute(TfToken("info:id")).Get(r_shader_id, time);
	}

	UsdShaderConnection _get_connected_texture_shader(const UsdShadeInput &p_input) const {
		const UsdShaderConnection connection = _get_connected_shader(p_input);
		if (!connection) {
			return UsdShaderConnection();
		}

		TfToken shader_id;
		if (!_get_shader_id(connection.shader, &shader_id) || shader_id != TfToken("UsdUVTexture")) {
			return UsdShaderConnection();
		}

		return connection;
	}

	Ref<Image> _load_image_from_shader(const UsdShadeShader &p_shader, String *r_resolved_path, Dictionary *r_mapping_notes) const {
		UsdShadeInput file_input = p_shader.GetInput(TfToken("file"));
		if (!file_input) {
			return Ref<Image>();
		}

		return _load_image_from_asset_attribute(file_input.GetAttr(), r_resolved_path, r_mapping_notes);
	}

	Ref<Texture2D> _load_texture_from_shader(const UsdShadeShader &p_shader, Dictionary *r_mapping_notes) const {
		UsdShadeInput file_input = p_shader.GetInput(TfToken("file"));
		if (!file_input) {
			return Ref<Texture2D>();
		}

		return _load_texture_from_asset_attribute(file_input.GetAttr(), r_mapping_notes);
	}

	BaseMaterial3D::TextureChannel _get_texture_channel_for_output(const TfToken &p_output_name) const {
		if (p_output_name == TfToken("r") || p_output_name == TfToken("red")) {
			return BaseMaterial3D::TEXTURE_CHANNEL_RED;
		}
		if (p_output_name == TfToken("g") || p_output_name == TfToken("green")) {
			return BaseMaterial3D::TEXTURE_CHANNEL_GREEN;
		}
		if (p_output_name == TfToken("b") || p_output_name == TfToken("blue")) {
			return BaseMaterial3D::TEXTURE_CHANNEL_BLUE;
		}
		if (p_output_name == TfToken("a") || p_output_name == TfToken("alpha")) {
			return BaseMaterial3D::TEXTURE_CHANNEL_ALPHA;
		}
		return BaseMaterial3D::TEXTURE_CHANNEL_GRAYSCALE;
	}

	float _get_channel_value(const Color &p_color, BaseMaterial3D::TextureChannel p_channel) const {
		switch (p_channel) {
			case BaseMaterial3D::TEXTURE_CHANNEL_RED:
				return p_color.r;
			case BaseMaterial3D::TEXTURE_CHANNEL_GREEN:
				return p_color.g;
			case BaseMaterial3D::TEXTURE_CHANNEL_BLUE:
				return p_color.b;
			case BaseMaterial3D::TEXTURE_CHANNEL_ALPHA:
				return p_color.a;
			case BaseMaterial3D::TEXTURE_CHANNEL_GRAYSCALE:
			default:
				return p_color.get_luminance();
		}
	}

	Ref<Image> _make_opacity_composited_albedo(const Ref<Texture2D> &p_albedo_texture, const Ref<Image> &p_opacity_image, BaseMaterial3D::TextureChannel p_opacity_channel) const {
		if (p_opacity_image.is_null()) {
			return Ref<Image>();
		}

		Ref<Image> opacity_image = p_opacity_image->duplicate(true);
		if (opacity_image.is_null()) {
			return Ref<Image>();
		}
		if (opacity_image->is_compressed()) {
			opacity_image->decompress();
		}
		opacity_image->convert(Image::FORMAT_RGBA8);

		Ref<Image> albedo_image;
		if (p_albedo_texture.is_valid()) {
			albedo_image = p_albedo_texture->get_image();
		}

		if (albedo_image.is_valid()) {
			albedo_image = albedo_image->duplicate(true);
			if (albedo_image->is_compressed()) {
				albedo_image->decompress();
			}
			albedo_image->convert(Image::FORMAT_RGBA8);
		} else {
			albedo_image = Image::create_empty(opacity_image->get_width(), opacity_image->get_height(), false, Image::FORMAT_RGBA8);
			albedo_image->fill(Color(1, 1, 1, 1));
		}

		if (opacity_image->get_width() != albedo_image->get_width() || opacity_image->get_height() != albedo_image->get_height()) {
			opacity_image->resize(albedo_image->get_width(), albedo_image->get_height(), Image::INTERPOLATE_BILINEAR);
		}

		for (int y = 0; y < albedo_image->get_height(); y++) {
			for (int x = 0; x < albedo_image->get_width(); x++) {
				Color albedo = albedo_image->get_pixelv(Point2i(x, y));
				const Color opacity_sample = opacity_image->get_pixelv(Point2i(x, y));
				albedo.a = CLAMP(_get_channel_value(opacity_sample, p_opacity_channel), 0.0f, 1.0f);
				albedo_image->set_pixelv(Point2i(x, y), albedo);
			}
		}

		return albedo_image;
	}

	bool _extract_texture_uv_transform(const UsdShadeShader &p_texture_shader, Vector3 *r_uv_scale, Vector3 *r_uv_offset, Dictionary *r_mapping_notes) const {
		if (!p_texture_shader || r_uv_scale == nullptr || r_uv_offset == nullptr) {
			return false;
		}

		UsdShadeInput st_input = p_texture_shader.GetInput(TfToken("st"));
		const UsdShaderConnection st_connection = _get_connected_shader(st_input);
		if (!st_connection) {
			return false;
		}

		TfToken shader_id;
		if (!_get_shader_id(st_connection.shader, &shader_id) || shader_id != TfToken("UsdTransform2d")) {
			return false;
		}

		GfVec2f scale(1.0f, 1.0f);
		if (UsdShadeInput scale_input = st_connection.shader.GetInput(TfToken("scale"))) {
			scale_input.Get(&scale, time);
		}

		GfVec2f translation(0.0f, 0.0f);
		if (UsdShadeInput translation_input = st_connection.shader.GetInput(TfToken("translation"))) {
			translation_input.Get(&translation, time);
		}

		float rotation = 0.0f;
		if (UsdShadeInput rotation_input = st_connection.shader.GetInput(TfToken("rotation"))) {
			rotation_input.Get(&rotation, time);
		}

		if (!Math::is_zero_approx(rotation)) {
			(*r_mapping_notes)["usd:material_uv_transform"] = "UsdTransform2d rotation is not supported by StandardMaterial3D; only scale and translation were applied.";
		}

		*r_uv_scale = Vector3((real_t)scale[0], (real_t)scale[1], 1.0f);
		*r_uv_offset = Vector3((real_t)translation[0], (real_t)(1.0f - scale[1] - translation[1]), 0.0f);
		return true;
	}

	void _merge_material_uv_transform(const UsdShadeShader &p_texture_shader, StandardMaterial3D *p_material, bool *r_has_uv_transform, Vector3 *r_uv_scale, Vector3 *r_uv_offset, Dictionary *r_mapping_notes) const {
		ERR_FAIL_NULL(p_material);
		ERR_FAIL_NULL(r_has_uv_transform);
		ERR_FAIL_NULL(r_uv_scale);
		ERR_FAIL_NULL(r_uv_offset);

		Vector3 uv_scale;
		Vector3 uv_offset;
		if (!_extract_texture_uv_transform(p_texture_shader, &uv_scale, &uv_offset, r_mapping_notes)) {
			return;
		}

		if (!*r_has_uv_transform) {
			*r_has_uv_transform = true;
			*r_uv_scale = uv_scale;
			*r_uv_offset = uv_offset;
			p_material->set_uv1_scale(uv_scale);
			p_material->set_uv1_offset(uv_offset);
			return;
		}

		if (!r_uv_scale->is_equal_approx(uv_scale) || !r_uv_offset->is_equal_approx(uv_offset)) {
			(*r_mapping_notes)["usd:material_uv_transform"] = "Multiple incompatible UsdTransform2d nodes were found; the first transform was kept.";
		}
	}

	Ref<Material> _build_material_from_usd_material(const UsdShadeMaterial &p_material, Dictionary *r_mapping_notes) const {
		if (!p_material) {
			return Ref<Material>();
		}

		String material_path = "<unknown>";
#ifdef USD_SCENE_LOADER_HAS_EXCEPTIONS
		try {
#endif
			material_path = _to_godot_string(p_material.GetPath().GetString());
			if (material_cache.has(material_path)) {
				return material_cache[material_path];
			}

			UsdShadeShader preview_surface = p_material.ComputeSurfaceSource();
			if (preview_surface) {
				preview_surface = UsdShadeShader(stage->GetPrimAtPath(preview_surface.GetPath()));
			}
			if (!preview_surface) {
				(*r_mapping_notes)["usd:material_status"] = vformat("Material %s has no supported surface source.", material_path);
				return Ref<Material>();
			}

			TfToken shader_id;
			preview_surface.GetPrim().GetAttribute(TfToken("info:id")).Get(&shader_id, time);
			if (shader_id != TfToken("UsdPreviewSurface")) {
				(*r_mapping_notes)["usd:material_status"] = vformat("Material %s uses unsupported shader id %s.", material_path, _to_godot_string(shader_id.GetString()));
				return Ref<Material>();
			}

			Ref<StandardMaterial3D> material;
			material.instantiate();
			bool has_uv_transform = false;
			Vector3 uv_scale;
			Vector3 uv_offset;

			UsdShadeInput diffuse_input = preview_surface.GetInput(TfToken("diffuseColor"));
			if (diffuse_input) {
				GfVec3f diffuse_color(1.0f, 1.0f, 1.0f);
				diffuse_input.Get(&diffuse_color, time);
				material->set_albedo(Color(diffuse_color[0], diffuse_color[1], diffuse_color[2], 1.0f));

				if (diffuse_input.HasConnectedSource()) {
					const UsdShaderConnection texture_connection = _get_connected_texture_shader(diffuse_input);
					if (texture_connection) {
						Ref<Texture2D> texture = _load_texture_from_shader(texture_connection.shader, r_mapping_notes);
						if (texture.is_valid()) {
							material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, texture);
							_merge_material_uv_transform(texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
						}
					} else {
						(*r_mapping_notes)["usd:material_status"] = vformat("Material %s uses an unsupported diffuseColor source shader.", material_path);
					}
				}
			}

			float metallic = 0.0f;
			UsdShadeInput metallic_input = preview_surface.GetInput(TfToken("metallic"));
			if (metallic_input && metallic_input.Get(&metallic, time)) {
				material->set_metallic(metallic);
			}
			if (metallic_input && metallic_input.HasConnectedSource()) {
				const UsdShaderConnection texture_connection = _get_connected_texture_shader(metallic_input);
				if (texture_connection) {
					Ref<Texture2D> texture = _load_texture_from_shader(texture_connection.shader, r_mapping_notes);
					if (texture.is_valid()) {
						material->set_texture(BaseMaterial3D::TEXTURE_METALLIC, texture);
						material->set_metallic_texture_channel(_get_texture_channel_for_output(texture_connection.output_name));
						_merge_material_uv_transform(texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
					}
				} else {
					(*r_mapping_notes)["usd:material_status"] = vformat("Material %s uses an unsupported metallic source shader.", material_path);
				}
			}

			float roughness = 1.0f;
			UsdShadeInput roughness_input = preview_surface.GetInput(TfToken("roughness"));
			if (roughness_input && roughness_input.Get(&roughness, time)) {
				material->set_roughness(roughness);
			}
			if (roughness_input && roughness_input.HasConnectedSource()) {
				const UsdShaderConnection texture_connection = _get_connected_texture_shader(roughness_input);
				if (texture_connection) {
					Ref<Texture2D> texture = _load_texture_from_shader(texture_connection.shader, r_mapping_notes);
					if (texture.is_valid()) {
						material->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, texture);
						material->set_roughness_texture_channel(_get_texture_channel_for_output(texture_connection.output_name));
						_merge_material_uv_transform(texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
					}
				} else {
					(*r_mapping_notes)["usd:material_status"] = vformat("Material %s uses an unsupported roughness source shader.", material_path);
				}
			}

			UsdShadeInput normal_input = preview_surface.GetInput(TfToken("normal"));
			if (normal_input && normal_input.HasConnectedSource()) {
				const UsdShaderConnection texture_connection = _get_connected_texture_shader(normal_input);
				if (texture_connection) {
					Ref<Texture2D> texture = _load_texture_from_shader(texture_connection.shader, r_mapping_notes);
					if (texture.is_valid()) {
						material->set_texture(BaseMaterial3D::TEXTURE_NORMAL, texture);
						material->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
						material->set_normal_scale(1.0f);
						_merge_material_uv_transform(texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
					}
				} else {
					(*r_mapping_notes)["usd:material_status"] = vformat("Material %s uses an unsupported normal source shader.", material_path);
				}
			}

			float opacity = 1.0f;
			UsdShadeInput opacity_input = preview_surface.GetInput(TfToken("opacity"));
			if (opacity_input && opacity_input.Get(&opacity, time) && opacity < 0.999f) {
				Color albedo = material->get_albedo();
				albedo.a = opacity;
				material->set_albedo(albedo);
				material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			}
			if (opacity_input && opacity_input.HasConnectedSource()) {
				const UsdShaderConnection texture_connection = _get_connected_texture_shader(opacity_input);
				if (texture_connection) {
					String opacity_resolved_path;
					Ref<Image> opacity_image = _load_image_from_shader(texture_connection.shader, &opacity_resolved_path, r_mapping_notes);
					if (opacity_image.is_valid()) {
						const Ref<Image> composited_albedo = _make_opacity_composited_albedo(material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO), opacity_image, _get_texture_channel_for_output(texture_connection.output_name));
						if (composited_albedo.is_valid()) {
							material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, _texture_from_image(composited_albedo));
							material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
						}
						_merge_material_uv_transform(texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
					}
				} else {
					(*r_mapping_notes)["usd:material_status"] = vformat("Material %s uses an unsupported opacity source shader.", material_path);
				}
			}

			float opacity_threshold = 0.0f;
			UsdShadeInput opacity_threshold_input = preview_surface.GetInput(TfToken("opacityThreshold"));
			if (opacity_threshold_input && opacity_threshold_input.Get(&opacity_threshold, time) && opacity_threshold > 0.0f) {
				material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
				material->set_alpha_scissor_threshold(CLAMP(opacity_threshold, 0.0f, 1.0f));
			}

			GfVec3f emission(0.0f, 0.0f, 0.0f);
			UsdShadeInput emission_input = preview_surface.GetInput(TfToken("emissiveColor"));
			bool has_emission = false;
			if (emission_input) {
				if (emission_input.Get(&emission, time)) {
					const Color emission_color(emission[0], emission[1], emission[2], 1.0f);
					if (emission_color.r > 0.0f || emission_color.g > 0.0f || emission_color.b > 0.0f) {
						material->set_emission(emission_color);
						has_emission = true;
					}
				}
				if (emission_input.HasConnectedSource()) {
					const UsdShaderConnection texture_connection = _get_connected_texture_shader(emission_input);
					if (texture_connection) {
						Ref<Texture2D> emission_texture = _load_texture_from_shader(texture_connection.shader, r_mapping_notes);
						if (emission_texture.is_valid()) {
							material->set_texture(BaseMaterial3D::TEXTURE_EMISSION, emission_texture);
							has_emission = true;
							_merge_material_uv_transform(texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
						}
					} else {
						(*r_mapping_notes)["usd:material_status"] = vformat("Material %s uses an unsupported emissiveColor source shader.", material_path);
					}
				}
			}
			if (has_emission) {
				material->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
				material->set_emission_energy_multiplier(1.0f);
			}

			material_cache.insert(material_path, material);
			return material;
#ifdef USD_SCENE_LOADER_HAS_EXCEPTIONS
		} catch (const std::exception &e) {
			const String error_text = String::utf8(e.what());
			(*r_mapping_notes)["usd:material_status"] = vformat("Material %s import failed: %s", material_path, error_text);
			ERR_PRINT(vformat("USD material import failed for %s: %s", material_path, error_text));
		} catch (...) {
			(*r_mapping_notes)["usd:material_status"] = vformat("Material %s import failed with an unknown USD exception.", material_path);
			ERR_PRINT(vformat("USD material import failed for %s with an unknown exception.", material_path));
		}
#endif
		return Ref<Material>();
	}

	Ref<Material> _make_display_color_material(const Color &p_color, bool p_use_vertex_colors) const {
		Ref<StandardMaterial3D> material;
		material.instantiate();
		material->set_albedo(p_color);
		if (p_use_vertex_colors) {
			material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
			material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
		}
		return material;
	}

	UsdGeomPrimvar _find_uv_primvar(const UsdGeomMesh &p_mesh) const {
		UsdGeomPrimvarsAPI primvars_api(p_mesh.GetPrim());
		static const TfToken uv_tokens[] = {
			TfToken("st"),
			TfToken("map1"),
			TfToken("UVMap"),
			TfToken("uvmap"),
		};

		for (const TfToken &uv_token : uv_tokens) {
			UsdGeomPrimvar uv_primvar = primvars_api.FindPrimvarWithInheritance(uv_token);
			if (uv_primvar && uv_primvar.HasValue()) {
				return uv_primvar;
			}
		}

		return UsdGeomPrimvar();
	}

	UsdGeomPrimvar _find_normals_primvar(const UsdGeomMesh &p_mesh) const {
		UsdGeomPrimvarsAPI primvars_api(p_mesh.GetPrim());
		UsdGeomPrimvar normals_primvar = primvars_api.FindPrimvarWithInheritance(TfToken("normals"));
		if (normals_primvar && normals_primvar.HasValue()) {
			return normals_primvar;
		}

		return UsdGeomPrimvar();
	}

	UsdSkinningData _read_skinning_data(const UsdPrim &p_prim, HashSet<String> *r_handled_attributes, Dictionary *r_mapping_notes) const {
		UsdSkinningData skinning_data;

		UsdGeomPrimvarsAPI primvars_api(p_prim);
		UsdGeomPrimvar joint_indices_primvar = primvars_api.FindPrimvarWithInheritance(TfToken("skel:jointIndices"));
		UsdGeomPrimvar joint_weights_primvar = primvars_api.FindPrimvarWithInheritance(TfToken("skel:jointWeights"));

		skinning_data.has_authored_joint_indices = joint_indices_primvar && joint_indices_primvar.GetAttr().HasAuthoredValueOpinion();
		skinning_data.has_authored_joint_weights = joint_weights_primvar && joint_weights_primvar.GetAttr().HasAuthoredValueOpinion();

		const bool has_joint_indices = joint_indices_primvar && joint_indices_primvar.ComputeFlattened(&skinning_data.joint_indices_values, time);
		const bool has_joint_weights = joint_weights_primvar && joint_weights_primvar.ComputeFlattened(&skinning_data.joint_weights_values, time);

		if (has_joint_indices) {
			skinning_data.joint_indices_interpolation = joint_indices_primvar.GetInterpolation();
			skinning_data.joint_indices_element_size = MAX(joint_indices_primvar.GetElementSize(), 1);
			_mark_primvar_handled(joint_indices_primvar, r_handled_attributes);
		}
		if (has_joint_weights) {
			skinning_data.joint_weights_interpolation = joint_weights_primvar.GetInterpolation();
			skinning_data.joint_weights_element_size = MAX(joint_weights_primvar.GetElementSize(), 1);
			_mark_primvar_handled(joint_weights_primvar, r_handled_attributes);
		}

		skinning_data.valid = has_joint_indices && has_joint_weights &&
				skinning_data.joint_indices_element_size == skinning_data.joint_weights_element_size &&
				skinning_data.joint_indices_element_size > 0;

		if ((skinning_data.has_authored_joint_indices || skinning_data.has_authored_joint_weights) && !skinning_data.valid) {
			(*r_mapping_notes)["usd:skinning_status"] = "Skel joint influences were authored, but jointIndices/jointWeights could not be paired for import.";
		}

		return skinning_data;
	}

	void _get_packed_skinning_influences(const UsdSkinningData &p_skinning_data, int p_face_index, int p_face_vertex_index, int p_point_index, int r_bones[4], float r_weights[4]) const {
		for (int influence_index = 0; influence_index < 4; influence_index++) {
			r_bones[influence_index] = 0;
			r_weights[influence_index] = 0.0f;
		}

		if (!p_skinning_data.valid) {
			return;
		}

		const int joint_indices_value_count = p_skinning_data.joint_indices_values.size() / p_skinning_data.joint_indices_element_size;
		const int joint_weights_value_count = p_skinning_data.joint_weights_values.size() / p_skinning_data.joint_weights_element_size;
		const int joint_indices_value_index = _get_interpolated_value_index(p_skinning_data.joint_indices_interpolation, p_face_index, p_face_vertex_index, p_point_index, joint_indices_value_count);
		const int joint_weights_value_index = _get_interpolated_value_index(p_skinning_data.joint_weights_interpolation, p_face_index, p_face_vertex_index, p_point_index, joint_weights_value_count);
		if (joint_indices_value_index < 0 || joint_weights_value_index < 0) {
			return;
		}

		struct InfluenceEntry {
			int joint = 0;
			float weight = 0.0f;
		};

		LocalVector<InfluenceEntry> influences;
		for (int influence_index = 0; influence_index < p_skinning_data.joint_indices_element_size; influence_index++) {
			const int joint_value_index = joint_indices_value_index * p_skinning_data.joint_indices_element_size + influence_index;
			const int weight_value_index = joint_weights_value_index * p_skinning_data.joint_weights_element_size + influence_index;
			if (joint_value_index >= (int)p_skinning_data.joint_indices_values.size() || weight_value_index >= (int)p_skinning_data.joint_weights_values.size()) {
				break;
			}

			const float weight = p_skinning_data.joint_weights_values[weight_value_index];
			if (weight <= 0.0f) {
				continue;
			}

			InfluenceEntry entry;
			entry.joint = p_skinning_data.joint_indices_values[joint_value_index];
			entry.weight = weight;
			influences.push_back(entry);
		}

		for (int influence_index = 0; influence_index < influences.size(); influence_index++) {
			const InfluenceEntry &entry = influences[influence_index];
			for (int slot = 0; slot < 4; slot++) {
				if (entry.weight > r_weights[slot]) {
					for (int shift = 3; shift > slot; shift--) {
						r_bones[shift] = r_bones[shift - 1];
						r_weights[shift] = r_weights[shift - 1];
					}
					r_bones[slot] = entry.joint;
					r_weights[slot] = entry.weight;
					break;
				}
			}
		}

		float total_weight = 0.0f;
		for (int influence_index = 0; influence_index < 4; influence_index++) {
			total_weight += r_weights[influence_index];
		}
		if (total_weight > 0.0f) {
			for (int influence_index = 0; influence_index < 4; influence_index++) {
				r_weights[influence_index] /= total_weight;
			}
		}
	}

	void _store_skin_binding_metadata(const UsdPrim &p_prim, HashSet<String> *r_handled_attributes, Dictionary *r_mapping_notes) const {
		UsdSkelBindingAPI skel_binding_api(p_prim);
		UsdSkelSkeleton bound_skeleton = skel_binding_api.GetInheritedSkeleton();
		if (bound_skeleton) {
			(*r_mapping_notes)["usd:skel_skeleton_path"] = _to_godot_string(bound_skeleton.GetPath().GetString());
		} else if (skel_binding_api.GetSkeletonRel()) {
			r_handled_attributes->insert("skel:skeleton");
		}

		GfMatrix4d geom_bind_matrix(1.0);
		UsdGeomPrimvar geom_bind_primvar = UsdGeomPrimvarsAPI(p_prim).FindPrimvarWithInheritance(TfToken("skel:geomBindTransform"));
		if (geom_bind_primvar && geom_bind_primvar.Get(&geom_bind_matrix, time)) {
			(*r_mapping_notes)["usd:skel_geom_bind_transform"] = _get_stage_correction_transform() * _gf_matrix_to_transform(geom_bind_matrix);
			_mark_primvar_handled(geom_bind_primvar, r_handled_attributes);
		}
	}

	Vector<UsdBlendShapeData> _read_mesh_blend_shapes(const UsdGeomMesh &p_mesh, int p_point_count, HashSet<String> *r_handled_attributes, Dictionary *r_mapping_notes) const {
		Vector<UsdBlendShapeData> blend_shapes;

		UsdSkelBindingAPI skel_binding_api(p_mesh.GetPrim());
		UsdAttribute blend_shapes_attr = skel_binding_api.GetBlendShapesAttr();
		UsdRelationship blend_shape_targets_rel = skel_binding_api.GetBlendShapeTargetsRel();
		if (!blend_shapes_attr || !blend_shape_targets_rel) {
			return blend_shapes;
		}

		VtArray<TfToken> blend_shape_names;
		SdfPathVector blend_shape_targets;
		const bool has_blend_shape_names = blend_shapes_attr.Get(&blend_shape_names, time) && !blend_shape_names.empty();
		const bool has_blend_shape_targets = blend_shape_targets_rel.GetTargets(&blend_shape_targets) && !blend_shape_targets.empty();
		if (!has_blend_shape_names || !has_blend_shape_targets) {
			return blend_shapes;
		}

		r_handled_attributes->insert("skel:blendShapes");
		r_handled_attributes->insert("skel:blendShapeTargets");

		if ((int)blend_shape_names.size() != (int)blend_shape_targets.size()) {
			(*r_mapping_notes)["usd:blend_shape_status"] = "Blend shape names and targets had mismatched counts; only paired entries were imported.";
		}

		const int blend_shape_count = MIN((int)blend_shape_names.size(), (int)blend_shape_targets.size());
		for (int blend_shape_index = 0; blend_shape_index < blend_shape_count; blend_shape_index++) {
			UsdPrim blend_shape_prim = stage->GetPrimAtPath(blend_shape_targets[blend_shape_index]);
			if (!blend_shape_prim || !blend_shape_prim.IsA<UsdSkelBlendShape>()) {
				continue;
			}

			UsdSkelBlendShape blend_shape(blend_shape_prim);
			VtArray<GfVec3f> offsets;
			if (!blend_shape.GetOffsetsAttr().Get(&offsets, time) || offsets.empty()) {
				continue;
			}

			VtArray<int> point_indices;
			const bool has_point_indices = blend_shape.GetPointIndicesAttr().Get(&point_indices, time) && !point_indices.empty();
			if (has_point_indices) {
				if ((int)point_indices.size() != (int)offsets.size()) {
					(*r_mapping_notes)["usd:blend_shape_status"] = "Blend shape pointIndices did not match offsets; that target was skipped.";
					continue;
				}
			} else if ((int)offsets.size() != p_point_count) {
				(*r_mapping_notes)["usd:blend_shape_status"] = "Blend shape offsets without pointIndices did not match the mesh point count; that target was skipped.";
				continue;
			}

			UsdBlendShapeData blend_shape_data;
			blend_shape_data.name = _to_godot_string(blend_shape_names[blend_shape_index].GetString());
			blend_shape_data.target_path = _to_godot_string(blend_shape_targets[blend_shape_index].GetString());

			for (int offset_index = 0; offset_index < (int)offsets.size(); offset_index++) {
				const int point_index = has_point_indices ? point_indices[offset_index] : offset_index;
				if (point_index < 0 || point_index >= p_point_count) {
					continue;
				}
				const GfVec3f &offset = offsets[offset_index];
				blend_shape_data.position_offsets_by_point.insert(point_index, Vector3(offset[0], offset[1], offset[2]));
			}

			VtArray<GfVec3f> normal_offsets;
			const bool has_normal_offsets = blend_shape.GetNormalOffsetsAttr().Get(&normal_offsets, time) && !normal_offsets.empty();
			if (has_normal_offsets) {
				if ((int)normal_offsets.size() != (int)offsets.size()) {
					(*r_mapping_notes)["usd:blend_shape_status"] = "Blend shape normalOffsets did not match offsets; those normal deltas were skipped.";
				} else {
					for (int offset_index = 0; offset_index < (int)normal_offsets.size(); offset_index++) {
						const int point_index = has_point_indices ? point_indices[offset_index] : offset_index;
						if (point_index < 0 || point_index >= p_point_count) {
							continue;
						}
						const GfVec3f &normal_offset = normal_offsets[offset_index];
						blend_shape_data.normal_offsets_by_point.insert(point_index, Vector3(normal_offset[0], normal_offset[1], normal_offset[2]));
					}
				}
			}

			const std::vector<UsdSkelInbetweenShape> authored_inbetweens = blend_shape.GetAuthoredInbetweens();
			for (const UsdSkelInbetweenShape &inbetween : authored_inbetweens) {
				if (!inbetween) {
					continue;
				}

				UsdInbetweenShapeData inbetween_data;
				Dictionary inbetween_metadata;
				const String attr_name = _to_godot_string(inbetween.GetAttr().GetName().GetString());
				inbetween_metadata["attr_name"] = attr_name;
				String display_name = attr_name;
				if (display_name.begins_with("inbetweens:")) {
					display_name = display_name.substr(String("inbetweens:").length());
				}
				inbetween_data.name = display_name;
				inbetween_metadata["name"] = display_name;

				float weight = 0.0f;
				if (inbetween.GetWeight(&weight)) {
					inbetween_data.weight = weight;
					inbetween_metadata["weight"] = weight;
				}

				VtArray<GfVec3f> inbetween_offsets;
				if (inbetween.GetOffsets(&inbetween_offsets)) {
					inbetween_metadata["offset_count"] = (int)inbetween_offsets.size();
					if (has_point_indices) {
						if ((int)inbetween_offsets.size() == (int)point_indices.size()) {
							for (int offset_index = 0; offset_index < (int)inbetween_offsets.size(); offset_index++) {
								const int point_index = point_indices[offset_index];
								if (point_index < 0 || point_index >= p_point_count) {
									continue;
								}
								const GfVec3f &offset = inbetween_offsets[offset_index];
								inbetween_data.position_offsets_by_point.insert(point_index, Vector3(offset[0], offset[1], offset[2]));
							}
						}
					} else if ((int)inbetween_offsets.size() == p_point_count) {
						for (int offset_index = 0; offset_index < (int)inbetween_offsets.size(); offset_index++) {
							const GfVec3f &offset = inbetween_offsets[offset_index];
							inbetween_data.position_offsets_by_point.insert(offset_index, Vector3(offset[0], offset[1], offset[2]));
						}
					}
				}

				VtArray<GfVec3f> inbetween_normal_offsets;
				if (inbetween.GetNormalOffsets(&inbetween_normal_offsets)) {
					inbetween_metadata["normal_offset_count"] = (int)inbetween_normal_offsets.size();
					if (has_point_indices) {
						if ((int)inbetween_normal_offsets.size() == (int)point_indices.size()) {
							for (int offset_index = 0; offset_index < (int)inbetween_normal_offsets.size(); offset_index++) {
								const int point_index = point_indices[offset_index];
								if (point_index < 0 || point_index >= p_point_count) {
									continue;
								}
								const GfVec3f &normal_offset = inbetween_normal_offsets[offset_index];
								inbetween_data.normal_offsets_by_point.insert(point_index, Vector3(normal_offset[0], normal_offset[1], normal_offset[2]));
							}
						}
					} else if ((int)inbetween_normal_offsets.size() == p_point_count) {
						for (int offset_index = 0; offset_index < (int)inbetween_normal_offsets.size(); offset_index++) {
							const GfVec3f &normal_offset = inbetween_normal_offsets[offset_index];
							inbetween_data.normal_offsets_by_point.insert(offset_index, Vector3(normal_offset[0], normal_offset[1], normal_offset[2]));
						}
					}
				}

				blend_shape_data.inbetweens.push_back(inbetween_data);
				blend_shape_data.inbetweens_metadata.push_back(inbetween_metadata);
			}

			if (!authored_inbetweens.empty()) {
				(*r_mapping_notes)["usd:blend_shape_status"] = "Blend shape inbetweens were preserved as metadata; only the primary target shape is imported as a live Godot blend shape.";
			}

			blend_shapes.push_back(blend_shape_data);
		}

		return blend_shapes;
	}

	bool _stage_has_authored_lights() const {
		for (const UsdPrim &prim : stage->Traverse()) {
			if (prim.HasAPI<UsdLuxLightAPI>()) {
				return true;
			}
		}
		return false;
	}

	void _append_preview_lighting(Node3D *p_root, const String &p_reason) const {
		ERR_FAIL_NULL(p_root);

		Dictionary preview_metadata;
		preview_metadata["usd:generated_preview"] = true;
		preview_metadata["usd:preview_only"] = true;
		preview_metadata["usd:generated_preview_reason"] = p_reason;

		Ref<Environment> preview_environment;
		preview_environment.instantiate();
		preview_environment->set_background(Environment::BG_COLOR);
		preview_environment->set_bg_color(Color(0.09f, 0.10f, 0.12f));
		preview_environment->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
		preview_environment->set_ambient_light_color(Color(0.42f, 0.44f, 0.48f));
		preview_environment->set_ambient_light_energy(0.7f);
		preview_environment->set_ambient_light_sky_contribution(0.0f);

		WorldEnvironment *world_environment = memnew(WorldEnvironment);
		world_environment->set_name("USDPreviewEnvironment");
		world_environment->set_environment(preview_environment);
		_set_usd_metadata_entries(world_environment, preview_metadata);
		_set_usd_metadata(world_environment, "usd:generated_preview_kind", "world_environment");
		p_root->add_child(world_environment);

		DirectionalLight3D *preview_sun = memnew(DirectionalLight3D);
		preview_sun->set_name("USDPreviewSun");
		preview_sun->set_rotation_degrees(Vector3(-50.0f, 30.0f, 0.0f));
		preview_sun->set_color(Color(1.0f, 0.97f, 0.92f));
		preview_sun->set_param(Light3D::PARAM_INTENSITY, 40000.0f);
		preview_sun->set_shadow(true);
		_set_usd_metadata_entries(preview_sun, preview_metadata);
		_set_usd_metadata(preview_sun, "usd:generated_preview_kind", "directional_light");
		p_root->add_child(preview_sun);
	}

	void _mark_primvar_handled(const UsdGeomPrimvar &p_primvar, HashSet<String> *r_handled_attributes) const {
		if (!p_primvar) {
			return;
		}

		const String attr_name = _to_godot_string(p_primvar.GetAttr().GetName().GetString());
		r_handled_attributes->insert(attr_name);
		if (p_primvar.IsIndexed()) {
			r_handled_attributes->insert(attr_name + ":indices");
		}
	}

	UsdMeshBuildResult _build_polygon_mesh(const UsdGeomMesh &p_mesh, HashSet<String> *r_handled_attributes, Dictionary *r_mapping_notes) const {
		UsdMeshBuildResult result;

		VtArray<GfVec3f> points;
		VtArray<int> face_vertex_counts;
		VtArray<int> face_vertex_indices;

		if (!p_mesh.GetPointsAttr().Get(&points, time) ||
				!p_mesh.GetFaceVertexCountsAttr().Get(&face_vertex_counts, time) ||
				!p_mesh.GetFaceVertexIndicesAttr().Get(&face_vertex_indices, time)) {
			return result;
		}

		TfToken orientation = UsdGeomTokens->rightHanded;
		UsdGeomGprim gprim(p_mesh.GetPrim());
		gprim.GetOrientationAttr().Get(&orientation, time);

		UsdGeomPrimvar normals_primvar(p_mesh.GetNormalsAttr());
		if (!normals_primvar || !normals_primvar.HasValue()) {
			normals_primvar = _find_normals_primvar(p_mesh);
		}
		VtArray<GfVec3f> normals;
		TfToken normals_interpolation = UsdGeomTokens->vertex;
		const bool has_normals = normals_primvar && normals_primvar.ComputeFlattened(&normals, time);
		if (has_normals) {
			normals_interpolation = normals_primvar.GetInterpolation();
			r_handled_attributes->insert("normals");
			_mark_primvar_handled(normals_primvar, r_handled_attributes);
		} else if (p_mesh.GetNormalsAttr().HasAuthoredValueOpinion()) {
			(*r_mapping_notes)["usd:mesh_normals"] = "Normals were authored but could not be flattened for import.";
		}

		UsdGeomPrimvar uv_primvar = _find_uv_primvar(p_mesh);
		VtArray<GfVec2f> uv_values;
		TfToken uv_interpolation = UsdGeomTokens->faceVarying;
		const bool has_uvs = uv_primvar && uv_primvar.ComputeFlattened(&uv_values, time);
		if (has_uvs) {
			uv_interpolation = uv_primvar.GetInterpolation();
			_mark_primvar_handled(uv_primvar, r_handled_attributes);
		}

		UsdGeomPrimvar display_color_primvar = gprim.GetDisplayColorPrimvar();
		VtArray<GfVec3f> display_colors;
		TfToken display_color_interpolation = UsdGeomTokens->constant;
		const bool has_display_color = display_color_primvar && display_color_primvar.ComputeFlattened(&display_colors, time);
		if (has_display_color) {
			display_color_interpolation = display_color_primvar.GetInterpolation();
			_mark_primvar_handled(display_color_primvar, r_handled_attributes);
		}

		const UsdSkinningData skinning_data = _read_skinning_data(p_mesh.GetPrim(), r_handled_attributes, r_mapping_notes);
		const Vector<UsdBlendShapeData> blend_shapes = _read_mesh_blend_shapes(p_mesh, points.size(), r_handled_attributes, r_mapping_notes);

		Vector<UsdSurfaceAccumulator> surfaces;
		surfaces.push_back(UsdSurfaceAccumulator());
		surfaces.write[0].binding_kind = "mesh";

		UsdShadeMaterialBindingAPI mesh_binding_api(p_mesh.GetPrim());
		const UsdRelationship mesh_binding_relationship = p_mesh.GetPrim().GetRelationship(TfToken("material:binding"));
		const bool mesh_has_authored_binding = mesh_binding_relationship && mesh_binding_relationship.HasAuthoredTargets();
		UsdShadeMaterial default_material = mesh_binding_api.ComputeBoundMaterial();
		if (default_material) {
			surfaces.write[0].material = _build_material_from_usd_material(default_material, r_mapping_notes);
			surfaces.write[0].usd_material_path = _to_godot_string(default_material.GetPath().GetString());
			surfaces.write[0].has_material_binding = mesh_has_authored_binding;
			r_handled_attributes->insert("material:binding");
		}

		HashMap<int, int> face_to_surface;
		const std::vector<UsdGeomSubset> material_subsets = mesh_binding_api.GetMaterialBindSubsets();
		result.geom_subsets = _build_preserved_geom_subsets(p_mesh, material_subsets, time);
		for (const UsdGeomSubset &subset : material_subsets) {
			VtIntArray subset_faces;
			if (!subset.GetIndicesAttr().Get(&subset_faces, time)) {
				continue;
			}

			UsdSurfaceAccumulator subset_surface;
			subset_surface.binding_kind = "subset";
			subset_surface.subset_path = _to_godot_string(subset.GetPath().GetString());
			subset_surface.subset_name = _to_godot_string(subset.GetPrim().GetName().GetString());
			TfToken family_name;
			subset.GetFamilyNameAttr().Get(&family_name, time);
			subset_surface.family_name = _to_godot_string(family_name.GetString());
			if (!family_name.IsEmpty()) {
				subset_surface.family_type = _to_godot_string(UsdGeomSubset::GetFamilyType(UsdGeomImageable(p_mesh.GetPrim()), family_name).GetString());
			}
			const UsdRelationship subset_binding_relationship = subset.GetPrim().GetRelationship(TfToken("material:binding"));
			const bool subset_has_authored_binding = subset_binding_relationship && subset_binding_relationship.HasAuthoredTargets();
			UsdShadeMaterial subset_material = UsdShadeMaterialBindingAPI(subset.GetPrim()).ComputeBoundMaterial();
			if (subset_material && subset_has_authored_binding) {
				subset_surface.material = _build_material_from_usd_material(subset_material, r_mapping_notes);
				subset_surface.usd_material_path = _to_godot_string(subset_material.GetPath().GetString());
				subset_surface.has_material_binding = true;
			} else if (default_material) {
				// Preserve the authored subset structure while still displaying the
				// effective mesh-bound material on the imported Godot surface.
				subset_surface.material = surfaces[0].material;
				subset_surface.usd_material_path = surfaces[0].usd_material_path;
			}

			const int surface_index = surfaces.size();
			surfaces.push_back(subset_surface);

			for (int i = 0; i < (int)subset_faces.size(); i++) {
				face_to_surface.insert(subset_faces[i], surface_index);
			}
		}

		int face_vertex_cursor = 0;
		for (int face = 0; face < (int)face_vertex_counts.size(); face++) {
			const int count = face_vertex_counts[face];
			if (count < 3 || face_vertex_cursor + count > (int)face_vertex_indices.size()) {
				face_vertex_cursor += count;
				continue;
			}

			const int surface_index = face_to_surface.has(face) ? face_to_surface[face] : 0;
			UsdSurfaceAccumulator &surface = surfaces.write[surface_index];

			auto append_corner = [&](int p_corner_offset) {
				const int point_index = face_vertex_indices[face_vertex_cursor + p_corner_offset];
				const int face_vertex_index = face_vertex_cursor + p_corner_offset;
				const int vertex_index = surface.vertices.size();
				surface.indices.push_back(vertex_index);
				surface.vertices.push_back(Vector3((real_t)points[point_index][0], (real_t)points[point_index][1], (real_t)points[point_index][2]));
				surface.authored_point_indices.push_back(point_index);

				if (has_normals) {
					GfVec3f normal_value(0.0f);
					if (_read_interpolated_value(normals, normals_interpolation, face, face_vertex_index, point_index, &normal_value)) {
						surface.normals.push_back(Vector3((real_t)normal_value[0], (real_t)normal_value[1], (real_t)normal_value[2]));
					} else {
						surface.normals.push_back(Vector3());
					}
				}

				if (has_uvs) {
					GfVec2f uv_value(0.0f);
					if (_read_interpolated_value(uv_values, uv_interpolation, face, face_vertex_index, point_index, &uv_value)) {
						surface.uvs.push_back(Vector2((real_t)uv_value[0], 1.0f - (real_t)uv_value[1]));
					} else {
						surface.uvs.push_back(Vector2());
					}
				}

				if (has_display_color) {
					GfVec3f color_value(1.0f);
					if (_read_interpolated_value(display_colors, display_color_interpolation, face, face_vertex_index, point_index, &color_value)) {
						surface.colors.push_back(Color(color_value[0], color_value[1], color_value[2], 1.0f));
					} else {
						surface.colors.push_back(Color(1, 1, 1, 1));
					}
				}

				if (skinning_data.valid) {
					int packed_bones[4];
					float packed_weights[4];
					_get_packed_skinning_influences(skinning_data, face, face_vertex_index, point_index, packed_bones, packed_weights);
					for (int influence_index = 0; influence_index < 4; influence_index++) {
						surface.bones.push_back(packed_bones[influence_index]);
						surface.weights.push_back(packed_weights[influence_index]);
					}
				}
			};

			for (int corner = 1; corner < count - 1; corner++) {
				append_corner(0);
				// Godot uses clockwise front faces. USD rightHanded meshes author
				// the opposite winding, so swap the last two corners when needed.
				if (orientation == UsdGeomTokens->rightHanded) {
					append_corner(corner + 1);
					append_corner(corner);
				} else {
					append_corner(corner);
					append_corner(corner + 1);
				}
				surface.authored_face_indices.push_back(face);
			}

			face_vertex_cursor += count;
		}

		r_handled_attributes->insert("points");
		r_handled_attributes->insert("faceVertexCounts");
		r_handled_attributes->insert("faceVertexIndices");
		r_handled_attributes->insert("orientation");
		r_handled_attributes->insert("subdivisionScheme");

		Ref<ArrayMesh> mesh;
		mesh.instantiate();
		if (!blend_shapes.is_empty()) {
			mesh->set_blend_shape_mode(Mesh::BLEND_SHAPE_MODE_RELATIVE);
			for (int blend_shape_index = 0; blend_shape_index < blend_shapes.size(); blend_shape_index++) {
				mesh->add_blend_shape(blend_shapes[blend_shape_index].name);
				for (int inbetween_index = 0; inbetween_index < blend_shapes[blend_shape_index].inbetweens.size(); inbetween_index++) {
					mesh->add_blend_shape(_make_inbetween_blend_shape_channel_name(blend_shapes[blend_shape_index].name, blend_shapes[blend_shape_index].inbetweens[inbetween_index].name));
				}
			}
		}
		for (int i = 0; i < surfaces.size(); i++) {
			const UsdSurfaceAccumulator &surface = surfaces[i];
			if (surface.vertices.is_empty() || surface.indices.is_empty()) {
				continue;
			}

			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = surface.vertices;
			arrays[Mesh::ARRAY_INDEX] = surface.indices;
			if (!surface.normals.is_empty()) {
				arrays[Mesh::ARRAY_NORMAL] = surface.normals;
			}
			if (!surface.uvs.is_empty()) {
				arrays[Mesh::ARRAY_TEX_UV] = surface.uvs;
			}
			if (!surface.colors.is_empty()) {
				arrays[Mesh::ARRAY_COLOR] = surface.colors;
			}
			if (!surface.bones.is_empty() && !surface.weights.is_empty()) {
				arrays[Mesh::ARRAY_BONES] = surface.bones;
				arrays[Mesh::ARRAY_WEIGHTS] = surface.weights;
				result.has_skinning = true;
			}

			TypedArray<Array> surface_blend_shapes;
			if (!blend_shapes.is_empty()) {
				for (int blend_shape_index = 0; blend_shape_index < blend_shapes.size(); blend_shape_index++) {
					auto append_blend_shape_surface = [&](const HashMap<int, Vector3> &p_position_offsets_by_point, const HashMap<int, Vector3> &p_normal_offsets_by_point) {
						PackedVector3Array blend_shape_vertices;
						blend_shape_vertices.resize(surface.vertices.size());
						PackedVector3Array blend_shape_normals;
						if (!surface.normals.is_empty()) {
							blend_shape_normals.resize(surface.normals.size());
						}
						for (int vertex_index = 0; vertex_index < surface.vertices.size(); vertex_index++) {
							const int point_index = surface.authored_point_indices[vertex_index];
							const Vector3 *offset_ptr = p_position_offsets_by_point.getptr(point_index);
							blend_shape_vertices.set(vertex_index, offset_ptr != nullptr ? *offset_ptr : Vector3());
							if (!surface.normals.is_empty()) {
								const Vector3 *normal_offset_ptr = p_normal_offsets_by_point.getptr(point_index);
								blend_shape_normals.set(vertex_index, normal_offset_ptr != nullptr ? *normal_offset_ptr : Vector3());
							}
						}

						Array blend_shape_arrays;
						blend_shape_arrays.resize(Mesh::ARRAY_MAX);
						blend_shape_arrays[Mesh::ARRAY_VERTEX] = blend_shape_vertices;
						if (!surface.normals.is_empty()) {
							blend_shape_arrays[Mesh::ARRAY_NORMAL] = blend_shape_normals;
						}
						surface_blend_shapes.push_back(blend_shape_arrays);
					};

					append_blend_shape_surface(blend_shapes[blend_shape_index].position_offsets_by_point, blend_shapes[blend_shape_index].normal_offsets_by_point);
					for (int inbetween_index = 0; inbetween_index < blend_shapes[blend_shape_index].inbetweens.size(); inbetween_index++) {
						const UsdInbetweenShapeData &inbetween = blend_shapes[blend_shape_index].inbetweens[inbetween_index];
						append_blend_shape_surface(inbetween.position_offsets_by_point, inbetween.normal_offsets_by_point);
					}
				}
			}

			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, surface_blend_shapes);

			Ref<Material> surface_material = surface.material;
			if (surface_material.is_null() && has_display_color && display_color_interpolation == UsdGeomTokens->constant && !display_colors.empty()) {
				const GfVec3f color_value = display_colors[0];
				surface_material = _make_display_color_material(Color(color_value[0], color_value[1], color_value[2], 1.0f), false);
			} else if (surface_material.is_null() && !surface.colors.is_empty()) {
				surface_material = _make_display_color_material(Color(1, 1, 1, 1), true);
			}

			if (surface_material.is_valid()) {
				mesh->surface_set_material(mesh->get_surface_count() - 1, surface_material);
			}

			if (!surface.usd_material_path.is_empty()) {
				result.material_paths.push_back(surface.usd_material_path);
			}

			Dictionary surface_description;
			surface_description["binding_kind"] = surface.binding_kind;
			surface_description["has_material_binding"] = surface.has_material_binding;
			if (!surface.subset_path.is_empty()) {
				surface_description["subset_path"] = surface.subset_path;
			}
			if (!surface.subset_name.is_empty()) {
				surface_description["subset_name"] = surface.subset_name;
			}
			if (!surface.family_name.is_empty()) {
				surface_description["family_name"] = surface.family_name;
			}
			if (!surface.family_type.is_empty()) {
				surface_description["family_type"] = surface.family_type;
			}
			if (!surface.usd_material_path.is_empty()) {
				surface_description["material_path"] = surface.usd_material_path;
			}
			if (!surface.authored_face_indices.is_empty()) {
				surface_description["authored_face_indices"] = surface.authored_face_indices;
			}
			if (!surface.authored_point_indices.is_empty()) {
				surface_description["authored_point_indices"] = surface.authored_point_indices;
			}
			result.material_subsets.push_back(surface_description);
		}

		if (!blend_shapes.is_empty()) {
			Array blend_shape_names;
			Array blend_shape_targets;
			Dictionary blend_shape_has_normal_offsets;
			Dictionary blend_shape_inbetweens;
			Dictionary blend_shape_channels;
			for (int blend_shape_index = 0; blend_shape_index < blend_shapes.size(); blend_shape_index++) {
				blend_shape_names.push_back(blend_shapes[blend_shape_index].name);
				blend_shape_targets.push_back(blend_shapes[blend_shape_index].target_path);
				blend_shape_has_normal_offsets[blend_shapes[blend_shape_index].name] = !blend_shapes[blend_shape_index].normal_offsets_by_point.is_empty();
				if (!blend_shapes[blend_shape_index].inbetweens_metadata.is_empty()) {
					blend_shape_inbetweens[blend_shapes[blend_shape_index].name] = blend_shapes[blend_shape_index].inbetweens_metadata;
				}

				Array channel_entries;
				Dictionary primary_channel;
				primary_channel["channel_name"] = blend_shapes[blend_shape_index].name;
				primary_channel["weight"] = 1.0;
				primary_channel["primary"] = true;
				channel_entries.push_back(primary_channel);
				for (int inbetween_index = 0; inbetween_index < blend_shapes[blend_shape_index].inbetweens.size(); inbetween_index++) {
					const UsdInbetweenShapeData &inbetween = blend_shapes[blend_shape_index].inbetweens[inbetween_index];
					Dictionary inbetween_channel;
					inbetween_channel["channel_name"] = _make_inbetween_blend_shape_channel_name(blend_shapes[blend_shape_index].name, inbetween.name);
					inbetween_channel["weight"] = inbetween.weight;
					inbetween_channel["primary"] = false;
					inbetween_channel["name"] = inbetween.name;
					channel_entries.push_back(inbetween_channel);
				}
				blend_shape_channels[blend_shapes[blend_shape_index].name] = channel_entries;
			}
			(*r_mapping_notes)["usd:blend_shape_names"] = blend_shape_names;
			(*r_mapping_notes)["usd:blend_shape_targets"] = blend_shape_targets;
			(*r_mapping_notes)["usd:blend_shape_has_normal_offsets"] = blend_shape_has_normal_offsets;
			(*r_mapping_notes)["usd:blend_shape_channels"] = blend_shape_channels;
			if (!blend_shape_inbetweens.is_empty()) {
				(*r_mapping_notes)["usd:blend_shape_inbetweens"] = blend_shape_inbetweens;
			}
			(*r_mapping_notes)["usd:blend_shape_mapping"] = "array_mesh_relative_piecewise";
		}

		result.mesh = mesh->get_surface_count() > 0 ? mesh : Ref<ArrayMesh>();
		return result;
	}

	UsdMeshBuildResult _build_points_mesh(const UsdGeomPoints &p_points, HashSet<String> *r_handled_attributes, Dictionary *r_mapping_notes) const {
		UsdMeshBuildResult result;

		VtArray<GfVec3f> points;
		if (!p_points.GetPointsAttr().Get(&points, time)) {
			return result;
		}

		UsdGeomGprim gprim(p_points.GetPrim());
		UsdGeomPrimvar display_color_primvar = gprim.GetDisplayColorPrimvar();
		VtArray<GfVec3f> display_colors;
		TfToken display_color_interpolation = UsdGeomTokens->constant;
		const bool has_display_color = display_color_primvar && display_color_primvar.ComputeFlattened(&display_colors, time);
		if (has_display_color) {
			display_color_interpolation = display_color_primvar.GetInterpolation();
			_mark_primvar_handled(display_color_primvar, r_handled_attributes);
		}

		VtArray<float> widths;
		const bool has_widths = p_points.GetWidthsAttr().Get(&widths, time) && !widths.empty();
		const TfToken widths_interpolation = p_points.GetWidthsInterpolation();
		if (has_widths) {
			r_handled_attributes->insert("widths");
			(*r_mapping_notes)["usd:point_widths"] = _to_float_array(widths);
			(*r_mapping_notes)["usd:point_widths_interpolation"] = _to_godot_string(widths_interpolation.GetString());
		}

		const UsdSkinningData skinning_data = _read_skinning_data(p_points.GetPrim(), r_handled_attributes, r_mapping_notes);

		PackedVector3Array vertices;
		PackedColorArray colors;
		PackedInt32Array bones;
		PackedFloat32Array weights_array;
		vertices.resize(points.size());
		if (has_display_color) {
			colors.resize(points.size());
		}
		if (skinning_data.valid) {
			bones.resize(points.size() * 4);
			weights_array.resize(points.size() * 4);
		}

		for (int point_index = 0; point_index < (int)points.size(); point_index++) {
			const GfVec3f &point = points[point_index];
			vertices.set(point_index, Vector3(point[0], point[1], point[2]));

			if (has_display_color) {
				GfVec3f color_value(1.0f);
				if (_read_interpolated_value(display_colors, display_color_interpolation, point_index, point_index, point_index, &color_value)) {
					colors.set(point_index, Color(color_value[0], color_value[1], color_value[2], 1.0f));
				} else {
					colors.set(point_index, Color(1, 1, 1, 1));
				}
			}

			if (skinning_data.valid) {
				int packed_bones[4];
				float packed_weights[4];
				_get_packed_skinning_influences(skinning_data, point_index, point_index, point_index, packed_bones, packed_weights);
				for (int influence_index = 0; influence_index < 4; influence_index++) {
					bones.set(point_index * 4 + influence_index, packed_bones[influence_index]);
					weights_array.set(point_index * 4 + influence_index, packed_weights[influence_index]);
				}
			}
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		if (has_display_color) {
			arrays[Mesh::ARRAY_COLOR] = colors;
		}
		if (skinning_data.valid) {
			arrays[Mesh::ARRAY_BONES] = bones;
			arrays[Mesh::ARRAY_WEIGHTS] = weights_array;
			result.has_skinning = true;
		}

		Ref<ArrayMesh> mesh;
		mesh.instantiate();
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_POINTS, arrays);

		Ref<StandardMaterial3D> material;
		material.instantiate();
		material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		material->set_flag(BaseMaterial3D::FLAG_USE_POINT_SIZE, true);
		if (has_display_color) {
			material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		}

		float point_size = 4.0f;
		if (has_widths) {
			point_size = MAX(widths[0] * 16.0f, 1.0f);
			if (widths.size() > 1 || widths_interpolation != UsdGeomTokens->constant) {
				(*r_mapping_notes)["usd:points_status"] = "UsdGeomPoints widths were authored per-point, but Godot point rendering currently approximates them with a single point size.";
			}
		}
		material->set_point_size(point_size);
		mesh->surface_set_material(0, material);

		r_handled_attributes->insert("points");
		if (has_display_color) {
			r_handled_attributes->insert("primvars:displayColor");
		}

		(*r_mapping_notes)["usd:points_mapping"] = "mesh_points";
		(*r_mapping_notes)["usd:point_count"] = (int)points.size();

		result.mesh = mesh;
		return result;
	}

	void _apply_light_attributes(const UsdLuxNonboundableLightBase &p_light, Light3D *p_target, HashSet<String> *r_handled_attributes) const {
		GfVec3f color_value(1.0f);
		float intensity = 1.0f;
		float exposure = 0.0f;

		p_light.GetColorAttr().Get(&color_value, time);
		p_light.GetIntensityAttr().Get(&intensity, time);
		p_light.GetExposureAttr().Get(&exposure, time);

		p_target->set_color(Color(color_value[0], color_value[1], color_value[2], 1.0f));
		p_target->set_param(Light3D::PARAM_INTENSITY, intensity * Math::pow(2.0f, exposure));

		r_handled_attributes->insert("inputs:color");
		r_handled_attributes->insert("inputs:intensity");
		r_handled_attributes->insert("inputs:exposure");
		r_handled_attributes->insert("inputs:normalize");
	}

	void _apply_light_attributes(const UsdLuxBoundableLightBase &p_light, Light3D *p_target, HashSet<String> *r_handled_attributes) const {
		GfVec3f color_value(1.0f);
		float intensity = 1.0f;
		float exposure = 0.0f;

		p_light.GetColorAttr().Get(&color_value, time);
		p_light.GetIntensityAttr().Get(&intensity, time);
		p_light.GetExposureAttr().Get(&exposure, time);

		p_target->set_color(Color(color_value[0], color_value[1], color_value[2], 1.0f));
		p_target->set_param(Light3D::PARAM_INTENSITY, intensity * Math::pow(2.0f, exposure));

		r_handled_attributes->insert("inputs:color");
		r_handled_attributes->insert("inputs:intensity");
		r_handled_attributes->insert("inputs:exposure");
		r_handled_attributes->insert("inputs:normalize");
	}

	bool _apply_shaping_to_spot_light(const UsdPrim &p_prim, SpotLight3D *p_target, HashSet<String> *r_handled_attributes) const {
		if (!p_prim.HasAPI<UsdLuxShapingAPI>()) {
			return false;
		}

		UsdLuxShapingAPI shaping_api(p_prim);
		if (!shaping_api) {
			return false;
		}

		float cone_angle = 90.0f;
		shaping_api.GetShapingConeAngleAttr().Get(&cone_angle, time);
		p_target->set_param(Light3D::PARAM_SPOT_ANGLE, CLAMP(cone_angle, 0.1f, 90.0f));

		float cone_softness = 0.0f;
		shaping_api.GetShapingConeSoftnessAttr().Get(&cone_softness, time);
		p_target->set_param(Light3D::PARAM_SPOT_ATTENUATION, MAX(0.01f, 1.0f - CLAMP(cone_softness, 0.0f, 1.0f)));

		r_handled_attributes->insert("inputs:shaping:cone:angle");
		r_handled_attributes->insert("inputs:shaping:cone:softness");
		return true;
	}

	void _store_unmapped_properties(const UsdPrim &p_prim, Object *p_target, const HashSet<String> &p_handled_attributes) const {
		Dictionary unmapped_attributes;
		const UsdAttributeVector authored_attributes = p_prim.GetAuthoredAttributes();
		for (const UsdAttribute &attribute : authored_attributes) {
			const String attribute_name = _to_godot_string(attribute.GetName().GetString());
			if (p_handled_attributes.has(attribute_name)) {
				continue;
			}
			unmapped_attributes[attribute_name] = _serialize_attribute(attribute, time);
		}

		if (!unmapped_attributes.is_empty()) {
			_set_usd_metadata(p_target, "usd:unmapped_attributes", unmapped_attributes);
		}

		Dictionary relationships = _serialize_relationships(p_prim);
		if (!relationships.is_empty()) {
			Dictionary filtered_relationships;
			Array relationship_keys = relationships.keys();
			for (int i = 0; i < relationship_keys.size(); i++) {
				const String relationship_name = relationship_keys[i];
				if (p_handled_attributes.has(relationship_name)) {
					continue;
				}
				filtered_relationships[relationship_name] = relationships[relationship_name];
			}

			if (!filtered_relationships.is_empty()) {
				_set_usd_metadata(p_target, "usd:unmapped_relationships", filtered_relationships);
			}
		}
	}

	MeshInstance3D *_build_primitive_mesh_instance(const UsdPrim &p_prim, HashSet<String> *r_handled_attributes) const {
		Ref<Mesh> primitive_mesh;

		if (p_prim.IsA<UsdGeomCube>()) {
			UsdGeomCube cube(p_prim);
			float size = 2.0f;
			cube.GetSizeAttr().Get(&size, time);

			Ref<BoxMesh> box_mesh;
			box_mesh.instantiate();
			box_mesh->set_size(Vector3(size, size, size));
			primitive_mesh = box_mesh;
			r_handled_attributes->insert("size");
		} else if (p_prim.IsA<UsdGeomSphere>()) {
			UsdGeomSphere sphere(p_prim);
			float radius = 1.0f;
			sphere.GetRadiusAttr().Get(&radius, time);

			Ref<SphereMesh> sphere_mesh;
			sphere_mesh.instantiate();
			sphere_mesh->set_radius(radius);
			sphere_mesh->set_height(radius * 2.0f);
			primitive_mesh = sphere_mesh;
			r_handled_attributes->insert("radius");
		} else if (p_prim.IsA<UsdGeomCapsule>()) {
			UsdGeomCapsule capsule(p_prim);
			float radius = 1.0f;
			float height = 2.0f;
			TfToken axis = UsdGeomTokens->y;
			capsule.GetRadiusAttr().Get(&radius, time);
			capsule.GetHeightAttr().Get(&height, time);
			capsule.GetAxisAttr().Get(&axis, time);

			Ref<CapsuleMesh> capsule_mesh;
			capsule_mesh.instantiate();
			capsule_mesh->set_radius(radius);
			capsule_mesh->set_height(height + radius * 2.0f);
			primitive_mesh = capsule_mesh;

			r_handled_attributes->insert("radius");
			r_handled_attributes->insert("height");
			r_handled_attributes->insert("axis");
		} else if (p_prim.IsA<UsdGeomCylinder>()) {
			UsdGeomCylinder cylinder(p_prim);
			float radius = 1.0f;
			float height = 2.0f;
			TfToken axis = UsdGeomTokens->y;
			cylinder.GetRadiusAttr().Get(&radius, time);
			cylinder.GetHeightAttr().Get(&height, time);
			cylinder.GetAxisAttr().Get(&axis, time);

			Ref<CylinderMesh> cylinder_mesh;
			cylinder_mesh.instantiate();
			cylinder_mesh->set_top_radius(radius);
			cylinder_mesh->set_bottom_radius(radius);
			cylinder_mesh->set_height(height);
			primitive_mesh = cylinder_mesh;

			r_handled_attributes->insert("radius");
			r_handled_attributes->insert("height");
			r_handled_attributes->insert("axis");
		} else if (p_prim.IsA<UsdGeomCone>()) {
			UsdGeomCone cone(p_prim);
			float radius = 1.0f;
			float height = 2.0f;
			TfToken axis = UsdGeomTokens->y;
			cone.GetRadiusAttr().Get(&radius, time);
			cone.GetHeightAttr().Get(&height, time);
			cone.GetAxisAttr().Get(&axis, time);

			Ref<CylinderMesh> cone_mesh;
			cone_mesh.instantiate();
			cone_mesh->set_top_radius(0.0f);
			cone_mesh->set_bottom_radius(radius);
			cone_mesh->set_height(height);
			primitive_mesh = cone_mesh;

			r_handled_attributes->insert("radius");
			r_handled_attributes->insert("height");
			r_handled_attributes->insert("axis");
		} else if (p_prim.IsA<UsdGeomPlane>()) {
			UsdGeomPlane plane(p_prim);
			TfToken axis = UsdGeomTokens->y;
			plane.GetAxisAttr().Get(&axis, time);

			Ref<PlaneMesh> plane_mesh;
			plane_mesh.instantiate();
			plane_mesh->set_size(Size2(2.0f, 2.0f));
			if (axis == UsdGeomTokens->x) {
				plane_mesh->set_orientation(PlaneMesh::FACE_X);
			} else if (axis == UsdGeomTokens->z) {
				plane_mesh->set_orientation(PlaneMesh::FACE_Z);
			} else {
				plane_mesh->set_orientation(PlaneMesh::FACE_Y);
			}
			primitive_mesh = plane_mesh;

			r_handled_attributes->insert("axis");
		}

		if (primitive_mesh.is_null()) {
			return nullptr;
		}

		MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
		mesh_instance->set_mesh(primitive_mesh);
		return mesh_instance;
	}

	static Array _to_float_array(const VtArray<float> &p_values) {
		Array values;
		for (size_t i = 0; i < p_values.size(); i++) {
			values.push_back((double)p_values[i]);
		}
		return values;
	}

	static Array _to_int_array(const VtArray<int> &p_values) {
		Array values;
		for (size_t i = 0; i < p_values.size(); i++) {
			values.push_back(p_values[i]);
		}
		return values;
	}

	static Array _to_token_array(const VtArray<TfToken> &p_values) {
		Array values;
		for (size_t i = 0; i < p_values.size(); i++) {
			values.push_back(_to_godot_string(p_values[i].GetString()));
		}
		return values;
	}

	static String _make_inbetween_blend_shape_channel_name(const String &p_blend_shape_name, const String &p_inbetween_name) {
		return p_blend_shape_name + "__inbetween__" + p_inbetween_name;
	}

	struct UsdBlendShapeChannelWeightComparator {
		_FORCE_INLINE_ bool operator()(const UsdBlendShapeChannelSpec &p_a, const UsdBlendShapeChannelSpec &p_b) const {
			if (Math::is_equal_approx(p_a.weight, p_b.weight)) {
				if (p_a.primary != p_b.primary) {
					return !p_a.primary && p_b.primary;
				}
				return p_a.channel_name < p_b.channel_name;
			}
			return p_a.weight < p_b.weight;
		}
	};

	static Vector<UsdBlendShapeChannelSpec> _parse_blend_shape_channel_specs(const Variant &p_channel_metadata) {
		Vector<UsdBlendShapeChannelSpec> specs;
		if (p_channel_metadata.get_type() != Variant::ARRAY) {
			return specs;
		}

		const Array channel_array = p_channel_metadata;
		for (int i = 0; i < channel_array.size(); i++) {
			if (channel_array[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary channel_dict = channel_array[i];
			const String channel_name = channel_dict.get("channel_name", String());
			if (channel_name.is_empty()) {
				continue;
			}

			UsdBlendShapeChannelSpec spec;
			spec.channel_name = channel_name;
			spec.weight = (float)(double)channel_dict.get("weight", 1.0);
			spec.primary = (bool)channel_dict.get("primary", false);
			spec.weight = MAX(spec.weight, 0.0001f);
			specs.push_back(spec);
		}

		specs.sort_custom<UsdBlendShapeChannelWeightComparator>();
		return specs;
	}

	static HashMap<String, float> _evaluate_blend_shape_channel_weights(float p_source_weight, const Vector<UsdBlendShapeChannelSpec> &p_specs) {
		HashMap<String, float> weights_by_channel;
		if (p_specs.is_empty() || p_source_weight <= 0.0f) {
			return weights_by_channel;
		}

		if (p_specs.size() == 1) {
			weights_by_channel.insert(p_specs[0].channel_name, p_source_weight / p_specs[0].weight);
			return weights_by_channel;
		}

		if (p_source_weight < p_specs[0].weight) {
			weights_by_channel.insert(p_specs[0].channel_name, p_source_weight / p_specs[0].weight);
			return weights_by_channel;
		}

		for (int spec_index = 0; spec_index < p_specs.size() - 1; spec_index++) {
			const UsdBlendShapeChannelSpec &current = p_specs[spec_index];
			const UsdBlendShapeChannelSpec &next = p_specs[spec_index + 1];
			if (p_source_weight <= next.weight) {
				const float span = MAX(next.weight - current.weight, 0.0001f);
				const float alpha = CLAMP((p_source_weight - current.weight) / span, 0.0f, 1.0f);
				weights_by_channel.insert(current.channel_name, 1.0f - alpha);
				weights_by_channel.insert(next.channel_name, alpha);
				return weights_by_channel;
			}
		}

		weights_by_channel.insert(p_specs[p_specs.size() - 1].channel_name, p_source_weight / p_specs[p_specs.size() - 1].weight);
		return weights_by_channel;
	}

	static Vector<double> _build_blend_shape_sample_times(const UsdAttribute &p_blend_shape_weights_attr, int p_animation_blend_shape_index, const Vector<UsdBlendShapeChannelSpec> &p_specs, const std::vector<double> &p_base_sample_times) {
		Vector<double> sample_times;
		for (double sample_time : p_base_sample_times) {
			sample_times.push_back(sample_time);
		}

		if (!p_blend_shape_weights_attr || p_specs.size() <= 1 || p_base_sample_times.size() <= 1) {
			return sample_times;
		}

		for (int sample_index = 0; sample_index < (int)p_base_sample_times.size() - 1; sample_index++) {
			const double start_sample_time = p_base_sample_times[sample_index];
			const double end_sample_time = p_base_sample_times[sample_index + 1];

			VtArray<float> start_weights;
			VtArray<float> end_weights;
			if (!p_blend_shape_weights_attr.Get(&start_weights, start_sample_time) || !p_blend_shape_weights_attr.Get(&end_weights, end_sample_time)) {
				continue;
			}
			if (p_animation_blend_shape_index >= (int)start_weights.size() || p_animation_blend_shape_index >= (int)end_weights.size()) {
				continue;
			}

			const float start_weight = start_weights[p_animation_blend_shape_index];
			const float end_weight = end_weights[p_animation_blend_shape_index];
			if (Math::is_equal_approx(start_weight, end_weight)) {
				continue;
			}

			const float low_weight = MIN(start_weight, end_weight);
			const float high_weight = MAX(start_weight, end_weight);
			for (int spec_index = 0; spec_index < p_specs.size() - 1; spec_index++) {
				const float threshold = p_specs[spec_index].weight;
				if (threshold <= low_weight || threshold >= high_weight) {
					continue;
				}
				const double alpha = (threshold - start_weight) / (end_weight - start_weight);
				if (alpha <= 0.0 || alpha >= 1.0) {
					continue;
				}
				sample_times.push_back(start_sample_time + (end_sample_time - start_sample_time) * alpha);
			}
		}

		sample_times.sort();
		for (int i = sample_times.size() - 1; i > 0; i--) {
			if (Math::is_equal_approx(sample_times[i], sample_times[i - 1])) {
				sample_times.remove_at(i);
			}
		}
		return sample_times;
	}

	static String _joint_parent_path(const String &p_joint_path) {
		const int slash = p_joint_path.rfind("/");
		if (slash < 0) {
			return String();
		}
		return p_joint_path.substr(0, slash);
	}

	static String _joint_leaf_name(const String &p_joint_path) {
		const int slash = p_joint_path.rfind("/");
		if (slash < 0) {
			return p_joint_path;
		}
		return p_joint_path.substr(slash + 1);
	}

	static Quaternion _gf_quat_to_godot(const GfQuatf &p_quat) {
		const GfVec3f imaginary = p_quat.GetImaginary();
		return Quaternion(imaginary[0], imaginary[1], imaginary[2], p_quat.GetReal());
	}

	static Ref<Curve3D> _build_linear_curve3d(const VtArray<GfVec3f> &p_points, int p_point_offset, int p_point_count, bool p_closed) {
		ERR_FAIL_COND_V(p_point_count < 2, Ref<Curve3D>());

		Ref<Curve3D> curve;
		curve.instantiate();
		for (int i = 0; i < p_point_count; i++) {
			const GfVec3f &point = p_points[p_point_offset + i];
			curve->add_point(Vector3(point[0], point[1], point[2]));
		}
		curve->set_closed(p_closed);
		return curve;
	}

	static Ref<Curve3D> _build_bezier_curve3d(const VtArray<GfVec3f> &p_points, int p_point_offset, int p_point_count, bool p_closed) {
		if (p_closed) {
			ERR_FAIL_COND_V((p_point_count % 3) != 0 || p_point_count < 6, Ref<Curve3D>());
		} else {
			ERR_FAIL_COND_V(((p_point_count - 4) % 3) != 0 || p_point_count < 4, Ref<Curve3D>());
		}

		const int anchor_count = p_closed ? (p_point_count / 3) : ((p_point_count + 2) / 3);
		ERR_FAIL_COND_V(anchor_count < 2, Ref<Curve3D>());

		Ref<Curve3D> curve;
		curve.instantiate();

		for (int anchor_index = 0; anchor_index < anchor_count; anchor_index++) {
			const GfVec3f &anchor = p_points[p_point_offset + anchor_index * 3];
			curve->add_point(Vector3(anchor[0], anchor[1], anchor[2]));
		}

		const int segment_count = p_closed ? anchor_count : (anchor_count - 1);
		for (int segment_index = 0; segment_index < segment_count; segment_index++) {
			const int start = p_point_offset + segment_index * 3;
			const int next_anchor_index = (segment_index + 1) % anchor_count;
			const Vector3 anchor = curve->get_point_position(segment_index);
			const Vector3 next_anchor = curve->get_point_position(next_anchor_index);
			const GfVec3f &out_handle = p_points[start + 1];
			const GfVec3f &in_handle = p_points[start + 2];
			curve->set_point_out(segment_index, Vector3(out_handle[0], out_handle[1], out_handle[2]) - anchor);
			curve->set_point_in(next_anchor_index, Vector3(in_handle[0], in_handle[1], in_handle[2]) - next_anchor);
		}

		curve->set_closed(p_closed);
		return curve;
	}

	Node *_build_basis_curves_node(const UsdPrim &p_prim, HashSet<String> *r_handled_attributes, Dictionary *r_mapping_notes) const {
		UsdGeomBasisCurves usd_curves(p_prim);
		Node3D *basis_root = memnew(Node3D);

		VtArray<GfVec3f> points;
		VtArray<int> curve_vertex_counts;
		if (!usd_curves.GetPointsAttr().Get(&points, time) || !usd_curves.GetCurveVertexCountsAttr().Get(&curve_vertex_counts, time)) {
			(*r_mapping_notes)["usd:mapping_status"] = "BasisCurves prim was detected, but its points or curveVertexCounts could not be read.";
			return basis_root;
		}

		TfToken curve_type = UsdGeomTokens->linear;
		usd_curves.GetTypeAttr().Get(&curve_type, time);

		TfToken basis = UsdGeomTokens->bezier;
		usd_curves.GetBasisAttr().Get(&basis, time);

		TfToken wrap = UsdGeomTokens->nonperiodic;
		usd_curves.GetWrapAttr().Get(&wrap, time);

		VtArray<float> widths;
		usd_curves.GetWidthsAttr().Get(&widths, time);
		const TfToken widths_interpolation = usd_curves.GetWidthsInterpolation();

		_set_usd_metadata(basis_root, "usd:curve_type", _to_godot_string(curve_type.GetString()));
		_set_usd_metadata(basis_root, "usd:curve_basis", _to_godot_string(basis.GetString()));
		_set_usd_metadata(basis_root, "usd:curve_wrap", _to_godot_string(wrap.GetString()));
		_set_usd_metadata(basis_root, "usd:curve_count", (int)curve_vertex_counts.size());
		_set_usd_metadata(basis_root, "usd:curve_vertex_counts", _to_int_array(curve_vertex_counts));
		if (!widths.empty()) {
			_set_usd_metadata(basis_root, "usd:curve_widths", _to_float_array(widths));
			_set_usd_metadata(basis_root, "usd:curve_widths_interpolation", _to_godot_string(widths_interpolation.GetString()));
		}

		r_handled_attributes->insert("points");
		r_handled_attributes->insert("curveVertexCounts");
		r_handled_attributes->insert("type");
		r_handled_attributes->insert("basis");
		r_handled_attributes->insert("wrap");
		if (!widths.empty()) {
			r_handled_attributes->insert("widths");
		}

		if (curve_type != UsdGeomTokens->linear && !(curve_type == UsdGeomTokens->cubic && basis == UsdGeomTokens->bezier)) {
			(*r_mapping_notes)["usd:mapping_status"] = vformat("BasisCurves type=%s basis=%s is not mapped yet; only linear and cubic bezier curves are imported as Path3D nodes.",
					_to_godot_string(curve_type.GetString()),
					_to_godot_string(basis.GetString()));
			return basis_root;
		}

		const bool closed = wrap == UsdGeomTokens->periodic;
		int point_offset = 0;
		for (int curve_index = 0; curve_index < (int)curve_vertex_counts.size(); curve_index++) {
			const int curve_point_count = curve_vertex_counts[curve_index];
			if (curve_point_count <= 0 || point_offset + curve_point_count > (int)points.size()) {
				(*r_mapping_notes)["usd:mapping_status"] = "BasisCurves topology was invalid; generated Path3D children were truncated.";
				break;
			}

			Ref<Curve3D> curve;
			if (curve_type == UsdGeomTokens->linear) {
				curve = _build_linear_curve3d(points, point_offset, curve_point_count, closed);
			} else {
				curve = _build_bezier_curve3d(points, point_offset, curve_point_count, closed);
			}
			if (curve.is_null()) {
				(*r_mapping_notes)["usd:mapping_status"] = "BasisCurves topology could not be converted to Curve3D; generated Path3D children were truncated.";
				break;
			}

			Path3D *path = memnew(Path3D);
			path->set_name(curve_vertex_counts.size() == 1 ? String("Path") : vformat("Path%d", curve_index));
			path->set_curve(curve);
			_set_usd_metadata(path, "usd:generated_from_basis_curves", true);
			_set_usd_metadata(path, "usd:source_prim_path", _to_godot_string(p_prim.GetPath().GetString()));
			_set_usd_metadata(path, "usd:basis_curve_index", curve_index);
			_set_usd_metadata(path, "usd:basis_curve_vertex_count", curve_point_count);
			_set_usd_metadata(path, "usd:basis_curve_closed", closed);
			basis_root->add_child(path);

			point_offset += curve_point_count;
		}

		_set_usd_metadata(basis_root, "usd:generated_curve_children", basis_root->get_child_count());
		_set_usd_metadata(basis_root, "usd:curve_mapping", "path3d_children");
		return basis_root;
	}

	Node *_build_skeleton_node(const UsdPrim &p_prim, HashSet<String> *r_handled_attributes, Dictionary *r_mapping_notes) const {
		UsdSkelSkeleton usd_skeleton(p_prim);
		Skeleton3D *skeleton = memnew(Skeleton3D);

		VtArray<TfToken> joints;
		if (!usd_skeleton.GetJointsAttr().Get(&joints, time) || joints.empty()) {
			(*r_mapping_notes)["usd:mapping_status"] = "Skeleton prim was detected, but its joints attribute could not be read.";
			return skeleton;
		}

		VtArray<GfMatrix4d> rest_transforms;
		const bool has_rest_transforms = usd_skeleton.GetRestTransformsAttr().Get(&rest_transforms, time) && rest_transforms.size() == joints.size();

		VtArray<GfMatrix4d> bind_transforms;
		const bool has_bind_transforms = usd_skeleton.GetBindTransformsAttr().Get(&bind_transforms, time) && bind_transforms.size() == joints.size();

		_set_usd_metadata(skeleton, "usd:skeleton_joint_count", (int)joints.size());
		_set_usd_metadata(skeleton, "usd:skeleton_joint_paths", _to_token_array(joints));
		_set_usd_metadata(skeleton, "usd:skeleton_has_rest_transforms", has_rest_transforms);
		_set_usd_metadata(skeleton, "usd:skeleton_has_bind_transforms", has_bind_transforms);
		_set_usd_metadata(skeleton, "usd:skeleton_mapping", "skeleton3d_bones");

		UsdRelationship animation_source_rel = p_prim.GetRelationship(TfToken("skel:animationSource"));
		if (animation_source_rel) {
			SdfPathVector targets;
			animation_source_rel.GetTargets(&targets);
			if (!targets.empty()) {
				Array animation_sources;
				for (const SdfPath &target : targets) {
					animation_sources.push_back(_to_godot_string(target.GetString()));
				}
				_set_usd_metadata(skeleton, "usd:animation_sources", animation_sources);
			}
		}

		r_handled_attributes->insert("joints");
		if (has_rest_transforms) {
			r_handled_attributes->insert("restTransforms");
		}
		if (has_bind_transforms) {
			r_handled_attributes->insert("bindTransforms");
		}

		HashMap<String, int> bone_index_by_joint_path;
		HashSet<String> used_bone_names;

		for (int joint_index = 0; joint_index < (int)joints.size(); joint_index++) {
			const String joint_path = _to_godot_string(joints[joint_index].GetString());
			String bone_name = _joint_leaf_name(joint_path);
			if (bone_name.is_empty()) {
				bone_name = vformat("Bone%d", joint_index);
			}
			String unique_bone_name = bone_name;
			for (int suffix = 1; used_bone_names.has(unique_bone_name); suffix++) {
				unique_bone_name = vformat("%s_%d", bone_name, suffix);
			}
			used_bone_names.insert(unique_bone_name);

			const int bone_index = skeleton->add_bone(unique_bone_name);
			bone_index_by_joint_path.insert(joint_path, bone_index);
			skeleton->set_bone_meta(bone_index, StringName("usd_joint_path"), joint_path);
			skeleton->set_bone_meta(bone_index, StringName("usd_joint_index"), joint_index);
		}

		for (int joint_index = 0; joint_index < (int)joints.size(); joint_index++) {
			const String joint_path = _to_godot_string(joints[joint_index].GetString());
			const String parent_joint_path = _joint_parent_path(joint_path);
			const int *bone_index_ptr = bone_index_by_joint_path.getptr(joint_path);
			ERR_CONTINUE(bone_index_ptr == nullptr);
			const int bone_index = *bone_index_ptr;

			if (!parent_joint_path.is_empty()) {
				const int *parent_bone_index_ptr = bone_index_by_joint_path.getptr(parent_joint_path);
				if (parent_bone_index_ptr != nullptr) {
					skeleton->set_bone_parent(bone_index, *parent_bone_index_ptr);
				} else {
					(*r_mapping_notes)["usd:mapping_status"] = "Skeleton joints referenced a missing parent path; unmatched joints were kept as skeleton roots.";
				}
				skeleton->set_bone_meta(bone_index, StringName("usd_joint_parent_path"), parent_joint_path);
			}
		}

		const Transform3D skeleton_world_inverse = (_get_stage_correction_transform() * _gf_matrix_to_transform(UsdGeomXformable(p_prim).ComputeLocalToWorldTransform(time))).affine_inverse();
		if (!has_rest_transforms && has_bind_transforms) {
			(*r_mapping_notes)["usd:mapping_status"] = "Skeleton restTransforms were missing; local rest pose was approximated from bindTransforms.";
		}

		for (int joint_index = 0; joint_index < (int)joints.size(); joint_index++) {
			if (has_bind_transforms) {
				const Transform3D bind_transform = skeleton_world_inverse * (_get_stage_correction_transform() * _gf_matrix_to_transform(bind_transforms[joint_index]));
				skeleton->set_bone_meta(joint_index, StringName("usd_joint_bind_transform"), bind_transform);
			}

			Transform3D local_rest;
			if (has_rest_transforms) {
				local_rest = _gf_matrix_to_transform(rest_transforms[joint_index]);
			} else if (has_bind_transforms) {
				const String joint_path = _to_godot_string(joints[joint_index].GetString());
				const String parent_joint_path = _joint_parent_path(joint_path);
				const Transform3D bind_transform = skeleton_world_inverse * (_get_stage_correction_transform() * _gf_matrix_to_transform(bind_transforms[joint_index]));
				if (parent_joint_path.is_empty()) {
					local_rest = bind_transform;
				} else {
					const int *parent_bone_index_ptr = bone_index_by_joint_path.getptr(parent_joint_path);
					if (parent_bone_index_ptr != nullptr) {
						const Transform3D parent_bind_transform = skeleton_world_inverse * (_get_stage_correction_transform() * _gf_matrix_to_transform(bind_transforms[*parent_bone_index_ptr]));
						local_rest = parent_bind_transform.affine_inverse() * bind_transform;
					} else {
						local_rest = bind_transform;
					}
				}
			}

			skeleton->set_bone_rest(joint_index, local_rest);
		}
		skeleton->reset_bone_poses();

		return skeleton;
	}

	Node *_build_node_for_prim(const UsdPrim &p_prim) const {
		HashSet<String> handled_attributes;
		Dictionary mapping_notes = _make_common_metadata(p_prim);

		Node *node = nullptr;

		if (p_prim.IsA<UsdGeomCamera>()) {
			UsdGeomCamera usd_camera(p_prim);
			Camera3D *camera = memnew(Camera3D);
			const GfCamera gf_camera = usd_camera.GetCamera(time);
			const GfRange1f clipping_range = gf_camera.GetClippingRange();
			const real_t unit_scale = _meters_scale(meters_per_unit);
			const real_t z_near = MAX((real_t)clipping_range.GetMin() * unit_scale, (real_t)0.001);
			const real_t z_far = MAX((real_t)clipping_range.GetMax() * unit_scale, z_near + (real_t)0.001);

			if (gf_camera.GetProjection() == GfCamera::Orthographic) {
				const real_t size = MAX((real_t)gf_camera.GetVerticalAperture() * (real_t)0.1 * unit_scale, (real_t)0.001);
				camera->set_orthogonal(size, z_near, z_far);
			} else {
				const double vertical_aperture = gf_camera.GetVerticalAperture();
				const double focal_length = gf_camera.GetFocalLength();
				real_t fovy = 75.0f;
				if (vertical_aperture > 0.0 && focal_length > 0.0) {
					fovy = (real_t)Math::rad_to_deg(2.0 * Math::atan((vertical_aperture * 0.5) / focal_length));
				}
				camera->set_perspective(CLAMP(fovy, (real_t)1.0, (real_t)179.0), z_near, z_far);
			}

			handled_attributes.insert("projection");
			handled_attributes.insert("horizontalAperture");
			handled_attributes.insert("verticalAperture");
			handled_attributes.insert("horizontalApertureOffset");
			handled_attributes.insert("verticalApertureOffset");
			handled_attributes.insert("focalLength");
			handled_attributes.insert("clippingRange");

			node = camera;
		} else if (p_prim.IsA<UsdLuxDistantLight>()) {
			UsdLuxDistantLight distant_light(p_prim);
			DirectionalLight3D *light = memnew(DirectionalLight3D);
			_apply_light_attributes(distant_light, light, &handled_attributes);

			float angle = 0.53f;
			distant_light.GetAngleAttr().Get(&angle, time);
			light->set_param(Light3D::PARAM_SIZE, angle);
			handled_attributes.insert("inputs:angle");

			node = light;
		} else if (p_prim.IsA<UsdLuxSphereLight>()) {
			UsdLuxSphereLight sphere_light(p_prim);
			float radius = 0.5f;
			sphere_light.GetRadiusAttr().Get(&radius, time);
			handled_attributes.insert("inputs:radius");
			handled_attributes.insert("treatAsPoint");

			SpotLight3D *spot_light = memnew(SpotLight3D);
			if (_apply_shaping_to_spot_light(p_prim, spot_light, &handled_attributes)) {
				_apply_light_attributes(sphere_light, spot_light, &handled_attributes);
				spot_light->set_param(Light3D::PARAM_SIZE, radius);
				spot_light->set_param(Light3D::PARAM_RANGE, MAX(radius * 20.0f, 1.0f));
				mapping_notes["usd:light_mapping"] = "UsdLuxSphereLight with ShapingAPI was approximated as SpotLight3D.";
				node = spot_light;
			} else {
				memdelete(spot_light);
				OmniLight3D *light = memnew(OmniLight3D);
				_apply_light_attributes(sphere_light, light, &handled_attributes);
				light->set_param(Light3D::PARAM_SIZE, radius);
				light->set_param(Light3D::PARAM_RANGE, MAX(radius * 10.0f, 1.0f));
				node = light;
			}
		} else if (p_prim.IsA<UsdLuxRectLight>()) {
			UsdLuxRectLight rect_light(p_prim);
			AreaLight3D *light = memnew(AreaLight3D);
			_apply_light_attributes(rect_light, light, &handled_attributes);

			float width = 1.0f;
			float height = 1.0f;
			rect_light.GetWidthAttr().Get(&width, time);
			rect_light.GetHeightAttr().Get(&height, time);
			light->set_area_size(Vector2(width, height));
			light->set_param(Light3D::PARAM_RANGE, MAX(MAX(width, height) * 10.0f, 1.0f));
			handled_attributes.insert("inputs:width");
			handled_attributes.insert("inputs:height");

			Ref<Texture2D> area_texture = _load_texture_from_asset_attribute(rect_light.GetTextureFileAttr(), &mapping_notes);
			if (area_texture.is_valid()) {
				light->set_area_texture(area_texture);
				handled_attributes.insert("inputs:texture:file");
			}

			node = light;
		} else if (p_prim.IsA<UsdLuxDiskLight>()) {
			UsdLuxDiskLight disk_light(p_prim);
			AreaLight3D *light = memnew(AreaLight3D);
			_apply_light_attributes(disk_light, light, &handled_attributes);

			float radius = 0.5f;
			disk_light.GetRadiusAttr().Get(&radius, time);
			light->set_area_size(Vector2(radius * 2.0f, radius * 2.0f));
			light->set_param(Light3D::PARAM_RANGE, MAX(radius * 10.0f, 1.0f));
			handled_attributes.insert("inputs:radius");
			mapping_notes["usd:light_mapping"] = "UsdLuxDiskLight was approximated as AreaLight3D.";

			node = light;
		} else if (p_prim.IsA<UsdLuxCylinderLight>()) {
			UsdLuxCylinderLight cylinder_light(p_prim);
			float radius = 0.5f;
			float length = 1.0f;
			cylinder_light.GetRadiusAttr().Get(&radius, time);
			cylinder_light.GetLengthAttr().Get(&length, time);
			handled_attributes.insert("inputs:radius");
			handled_attributes.insert("inputs:length");
			handled_attributes.insert("treatAsLine");

			SpotLight3D *spot_light = memnew(SpotLight3D);
			if (_apply_shaping_to_spot_light(p_prim, spot_light, &handled_attributes)) {
				_apply_light_attributes(cylinder_light, spot_light, &handled_attributes);
				spot_light->set_param(Light3D::PARAM_SIZE, radius);
				spot_light->set_param(Light3D::PARAM_RANGE, MAX(length * 10.0f, 1.0f));
				mapping_notes["usd:light_mapping"] = "UsdLuxCylinderLight with ShapingAPI was approximated as SpotLight3D.";
				node = spot_light;
			} else {
				memdelete(spot_light);
				OmniLight3D *light = memnew(OmniLight3D);
				_apply_light_attributes(cylinder_light, light, &handled_attributes);
				light->set_param(Light3D::PARAM_SIZE, radius);
				light->set_param(Light3D::PARAM_RANGE, MAX(MAX(length, radius) * 10.0f, 1.0f));
				mapping_notes["usd:light_mapping"] = "UsdLuxCylinderLight was approximated as OmniLight3D.";
				node = light;
			}
		} else if (p_prim.IsA<UsdGeomBasisCurves>()) {
			node = _build_basis_curves_node(p_prim, &handled_attributes, &mapping_notes);
		} else if (p_prim.IsA<UsdSkelSkeleton>()) {
			node = _build_skeleton_node(p_prim, &handled_attributes, &mapping_notes);
		} else if (p_prim.IsA<UsdGeomMesh>()) {
			UsdGeomMesh usd_mesh(p_prim);
			MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
			UsdMeshBuildResult mesh_result = _build_polygon_mesh(usd_mesh, &handled_attributes, &mapping_notes);
			if (mesh_result.mesh.is_valid()) {
				mesh_instance->set_mesh(mesh_result.mesh);
				if (!mesh_result.material_paths.is_empty()) {
					mapping_notes["usd:material_bindings"] = mesh_result.material_paths;
				}
				if (!mesh_result.material_subsets.is_empty()) {
					mapping_notes["usd:material_subsets"] = mesh_result.material_subsets;
				}
				if (!mesh_result.geom_subsets.is_empty()) {
					mapping_notes["usd:geom_subsets"] = mesh_result.geom_subsets;
				}
				if (mesh_result.has_skinning) {
					_store_skin_binding_metadata(p_prim, &handled_attributes, &mapping_notes);
				}
			} else {
				mapping_notes["usd:mesh_status"] = "Mesh geometry was detected, but no triangulated surface could be generated.";
			}
			node = mesh_instance;
		} else if (p_prim.IsA<UsdGeomPoints>()) {
			UsdGeomPoints usd_points(p_prim);
			MeshInstance3D *points_instance = memnew(MeshInstance3D);
			UsdMeshBuildResult points_result = _build_points_mesh(usd_points, &handled_attributes, &mapping_notes);
			if (points_result.mesh.is_valid()) {
				points_instance->set_mesh(points_result.mesh);
				if (points_result.has_skinning) {
					_store_skin_binding_metadata(p_prim, &handled_attributes, &mapping_notes);
				}
			} else {
				mapping_notes["usd:points_status"] = "Points geometry was detected, but no point mesh could be generated.";
			}
			node = points_instance;
		} else {
			node = _build_primitive_mesh_instance(p_prim, &handled_attributes);
		}

		if (node == nullptr) {
			if (p_prim.IsA<UsdGeomXformable>() || p_prim.IsA<UsdGeomImageable>()) {
				node = memnew(Node3D);
			} else {
				node = memnew(Node);
			}

			mapping_notes["usd:mapping_status"] = "No native Godot mapping exists yet for this prim type; a placeholder node was created.";
		}

		node->set_name(_node_name_for_prim(p_prim));
		_set_usd_metadata_entries(node, mapping_notes);
		_apply_transform_and_visibility(p_prim, node, &handled_attributes);

		if (MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(node)) {
			TfToken axis = TfToken();
			bool needs_axis_correction = false;

			if (p_prim.IsA<UsdGeomCapsule>()) {
				UsdGeomCapsule(p_prim).GetAxisAttr().Get(&axis, time);
				needs_axis_correction = true;
			} else if (p_prim.IsA<UsdGeomCylinder>()) {
				UsdGeomCylinder(p_prim).GetAxisAttr().Get(&axis, time);
				needs_axis_correction = true;
			} else if (p_prim.IsA<UsdGeomCone>()) {
				UsdGeomCone(p_prim).GetAxisAttr().Get(&axis, time);
				needs_axis_correction = true;
			}

			if (needs_axis_correction && axis != UsdGeomTokens->y) {
				mesh_instance->set_transform(mesh_instance->get_transform() * _make_local_axis_correction(axis));
			}
		}

		_store_unmapped_properties(p_prim, node, handled_attributes);
		_store_composition_arcs(p_prim, node);
		return node;
	}

	void _append_children(const UsdPrim &p_parent_prim, Node *p_parent_node) const {
		for (const UsdPrim &child_prim : p_parent_prim.GetChildren()) {
			if (_should_skip_child_prim(child_prim)) {
				continue;
			}
			Node *child_node = _build_node_for_prim(child_prim);
			p_parent_node->add_child(child_node);
			_append_children(child_prim, child_node);
		}
	}

	Node *_find_node_for_prim_path(Node *p_root, const String &p_prim_path) const {
		ERR_FAIL_NULL_V(p_root, nullptr);
		const Dictionary metadata = _get_usd_metadata(p_root);
		if ((String)metadata.get("usd:prim_path", String()) == p_prim_path) {
			return p_root;
		}

		for (int i = 0; i < p_root->get_child_count(); i++) {
			if (Node *match = _find_node_for_prim_path(p_root->get_child(i), p_prim_path)) {
				return match;
			}
		}

		return nullptr;
	}

	String _make_unique_animation_name(const Ref<AnimationLibrary> &p_library, const String &p_base_name) const {
		String animation_name = p_base_name.is_empty() ? String("Animation") : p_base_name;
		if (!p_library.is_valid()) {
			return animation_name;
		}

		if (!p_library->has_animation(animation_name)) {
			return animation_name;
		}

		for (int suffix = 1;; suffix++) {
			const String candidate = vformat("%s_%d", animation_name, suffix);
			if (!p_library->has_animation(candidate)) {
				return candidate;
			}
		}
	}

	AnimationPlayer *_get_or_create_animation_player(Node3D *p_root) const {
		ERR_FAIL_NULL_V(p_root, nullptr);

		for (int i = 0; i < p_root->get_child_count(); i++) {
			if (AnimationPlayer *existing = Object::cast_to<AnimationPlayer>(p_root->get_child(i))) {
				return existing;
			}
		}

		String player_name = "USDAnimationPlayer";
		for (int suffix = 1; p_root->get_node_or_null(NodePath(player_name)) != nullptr; suffix++) {
			player_name = vformat("USDAnimationPlayer%d", suffix);
		}

		AnimationPlayer *player = memnew(AnimationPlayer);
		player->set_name(player_name);
		player->set_root_node(NodePath(".."));
		p_root->add_child(player);

		Ref<AnimationLibrary> library;
		library.instantiate();
		player->add_animation_library("", library);
		return player;
	}

	bool _append_baked_skeleton_animation(Node3D *p_scene_root, Skeleton3D *p_skeleton, const UsdSkelAnimation &p_animation) const {
		ERR_FAIL_NULL_V(p_scene_root, false);
		ERR_FAIL_NULL_V(p_skeleton, false);
		ERR_FAIL_COND_V(!p_animation, false);

		VtArray<TfToken> animation_joints;
		p_animation.GetJointsAttr().Get(&animation_joints, time);

		HashMap<String, int> bone_index_by_joint_path;
		for (int bone_index = 0; bone_index < p_skeleton->get_bone_count(); bone_index++) {
			if (!p_skeleton->has_bone_meta(bone_index, StringName("usd_joint_path"))) {
				continue;
			}
			bone_index_by_joint_path.insert((String)p_skeleton->get_bone_meta(bone_index, StringName("usd_joint_path")), bone_index);
		}

		UsdAttribute translations_attr = p_animation.GetTranslationsAttr();
		UsdAttribute rotations_attr = p_animation.GetRotationsAttr();
		UsdAttribute scales_attr = p_animation.GetScalesAttr();
		UsdAttribute blend_shape_weights_attr = p_animation.GetBlendShapeWeightsAttr();
		VtArray<TfToken> animation_blend_shape_names;
		const bool has_animation_blend_shapes = p_animation.GetBlendShapesAttr().Get(&animation_blend_shape_names, time) && !animation_blend_shape_names.empty();

		const bool has_translations = translations_attr && translations_attr.HasAuthoredValueOpinion();
		const bool has_rotations = rotations_attr && rotations_attr.HasAuthoredValueOpinion();
		const bool has_scales = scales_attr && scales_attr.HasAuthoredValueOpinion();
		const bool has_blend_shape_weights = blend_shape_weights_attr && blend_shape_weights_attr.HasAuthoredValueOpinion() && has_animation_blend_shapes;
		if (!has_translations && !has_rotations && !has_scales && !has_blend_shape_weights) {
			return false;
		}

		std::vector<double> translation_sample_times;
		std::vector<double> rotation_sample_times;
		std::vector<double> scale_sample_times;
		std::vector<double> blend_shape_sample_times;
		std::vector<double> sample_times;
		auto append_time_samples = [&sample_times](const UsdAttribute &p_attr, std::vector<double> *r_attr_times) {
			if (!p_attr || !p_attr.HasAuthoredValueOpinion()) {
				return;
			}
			std::vector<double> attr_times;
			p_attr.GetTimeSamples(&attr_times);
			if (r_attr_times != nullptr) {
				*r_attr_times = attr_times;
			}
			sample_times.insert(sample_times.end(), attr_times.begin(), attr_times.end());
		};
		append_time_samples(translations_attr, &translation_sample_times);
		append_time_samples(rotations_attr, &rotation_sample_times);
		append_time_samples(scales_attr, &scale_sample_times);
		append_time_samples(blend_shape_weights_attr, &blend_shape_sample_times);
		if (sample_times.empty()) {
			sample_times.push_back(0.0);
		}
		std::sort(sample_times.begin(), sample_times.end());
		sample_times.erase(std::unique(sample_times.begin(), sample_times.end()), sample_times.end());

		const double time_codes_per_second = MAX(stage->GetTimeCodesPerSecond(), 1.0);
		const double start_time = sample_times.front();
		const double end_time = sample_times.back();

		AnimationPlayer *player = _get_or_create_animation_player(p_scene_root);
		ERR_FAIL_NULL_V(player, false);
		Ref<AnimationLibrary> library = player->get_animation_library("");
		if (library.is_null()) {
			library.instantiate();
			player->add_animation_library("", library);
		}

		Ref<Animation> animation;
		animation.instantiate();
		animation->set_step(1.0 / time_codes_per_second);

		const String animation_name = _make_unique_animation_name(library, _node_name_for_prim(p_animation.GetPrim()));
		animation->set_name(animation_name);
		animation->set_length(MAX((end_time - start_time) / time_codes_per_second, 0.0));

		Dictionary animation_metadata;
		animation_metadata["usd:animation_prim_path"] = _to_godot_string(p_animation.GetPrim().GetPath().GetString());
		animation_metadata["usd:time_codes_per_second"] = time_codes_per_second;
		animation_metadata["usd:start_time_code"] = start_time;
		animation_metadata["usd:end_time_code"] = end_time;
		animation_metadata["usd:has_authored_translations"] = has_translations;
		animation_metadata["usd:has_authored_rotations"] = has_rotations;
		animation_metadata["usd:has_authored_scales"] = has_scales;
		animation_metadata["usd:has_authored_blend_shape_weights"] = has_blend_shape_weights;
		animation_metadata["usd:translations_constant"] = has_translations && translation_sample_times.empty();
		animation_metadata["usd:rotations_constant"] = has_rotations && rotation_sample_times.empty();
		animation_metadata["usd:scales_constant"] = has_scales && scale_sample_times.empty();
		animation_metadata["usd:blend_shape_weights_constant"] = has_blend_shape_weights && blend_shape_sample_times.empty();
		animation_metadata["usd:joint_paths"] = _to_token_array(animation_joints);
		if (has_animation_blend_shapes) {
			animation_metadata["usd:blend_shape_names"] = _to_token_array(animation_blend_shape_names);
		}

		auto sample_times_to_array = [](const std::vector<double> &p_times) {
			Array result;
			for (double sample_time : p_times) {
				result.push_back(sample_time);
			}
			return result;
		};
		animation_metadata["usd:translation_time_codes"] = sample_times_to_array(translation_sample_times);
		animation_metadata["usd:rotation_time_codes"] = sample_times_to_array(rotation_sample_times);
		animation_metadata["usd:scale_time_codes"] = sample_times_to_array(scale_sample_times);
		animation_metadata["usd:blend_shape_weight_time_codes"] = sample_times_to_array(blend_shape_sample_times);

		VtArray<GfVec3f> default_translations;
		if (has_translations && translations_attr.Get(&default_translations, UsdTimeCode::Default()) && default_translations.size() == animation_joints.size()) {
			Array translation_defaults;
			for (int joint_index = 0; joint_index < (int)default_translations.size(); joint_index++) {
				const GfVec3f value = default_translations[joint_index];
				translation_defaults.push_back(Vector3(value[0], value[1], value[2]));
			}
			animation_metadata["usd:translation_defaults"] = translation_defaults;
		}

		VtArray<GfQuatf> default_rotations;
		if (has_rotations && rotations_attr.Get(&default_rotations, UsdTimeCode::Default()) && default_rotations.size() == animation_joints.size()) {
			Array rotation_defaults;
			for (int joint_index = 0; joint_index < (int)default_rotations.size(); joint_index++) {
				rotation_defaults.push_back(_gf_quat_to_godot(default_rotations[joint_index]));
			}
			animation_metadata["usd:rotation_defaults"] = rotation_defaults;
		}

		VtArray<GfVec3h> default_scales;
		if (has_scales && scales_attr.Get(&default_scales, UsdTimeCode::Default()) && default_scales.size() == animation_joints.size()) {
			Array scale_defaults;
			for (int joint_index = 0; joint_index < (int)default_scales.size(); joint_index++) {
				const GfVec3h value = default_scales[joint_index];
				scale_defaults.push_back(Vector3((real_t)value[0], (real_t)value[1], (real_t)value[2]));
			}
			animation_metadata["usd:scale_defaults"] = scale_defaults;
		}
		_set_usd_metadata_entries(animation.ptr(), animation_metadata);

		const String skeleton_path = String(p_scene_root->get_path_to(p_skeleton));
		bool added_any_tracks = false;

		for (int joint_index = 0; joint_index < (int)animation_joints.size(); joint_index++) {
			const String joint_path = _to_godot_string(animation_joints[joint_index].GetString());
			const int *bone_index_ptr = bone_index_by_joint_path.getptr(joint_path);
			if (bone_index_ptr == nullptr) {
				continue;
			}

			const int bone_index = *bone_index_ptr;
			const String track_path = skeleton_path + ":" + p_skeleton->get_bone_name(bone_index);
			const Transform3D rest = p_skeleton->get_bone_rest(bone_index);
			const Vector3 rest_position = rest.origin;
			const Quaternion rest_rotation = rest.basis.get_rotation_quaternion();
			const Vector3 rest_scale = rest.basis.get_scale();

			bool add_position_track = false;
			if (has_translations) {
				for (double sample_time : sample_times) {
					VtArray<GfVec3f> translations;
					if (translations_attr.Get(&translations, sample_time) && joint_index < (int)translations.size()) {
						const GfVec3f value = translations[joint_index];
						if (!Vector3(value[0], value[1], value[2]).is_equal_approx(rest_position)) {
							add_position_track = true;
							break;
						}
					}
				}
			}

			bool add_rotation_track = false;
			if (has_rotations) {
				for (double sample_time : sample_times) {
					VtArray<GfQuatf> rotations;
					if (rotations_attr.Get(&rotations, sample_time) && joint_index < (int)rotations.size()) {
						if (!_gf_quat_to_godot(rotations[joint_index]).is_equal_approx(rest_rotation)) {
							add_rotation_track = true;
							break;
						}
					}
				}
			}

			bool add_scale_track = false;
			if (has_scales) {
				for (double sample_time : sample_times) {
					VtArray<GfVec3h> scales;
					if (scales_attr.Get(&scales, sample_time) && joint_index < (int)scales.size()) {
						const GfVec3h value = scales[joint_index];
						if (!Vector3((real_t)value[0], (real_t)value[1], (real_t)value[2]).is_equal_approx(rest_scale)) {
							add_scale_track = true;
							break;
						}
					}
				}
			}

			const int position_track = add_position_track ? animation->get_track_count() : -1;
			if (add_position_track) {
				animation->add_track(Animation::TYPE_POSITION_3D);
				animation->track_set_path(position_track, NodePath(track_path));
				animation->track_set_imported(position_track, true);
			}

			const int rotation_track = add_rotation_track ? animation->get_track_count() : -1;
			if (add_rotation_track) {
				animation->add_track(Animation::TYPE_ROTATION_3D);
				animation->track_set_path(rotation_track, NodePath(track_path));
				animation->track_set_imported(rotation_track, true);
			}

			const int scale_track = add_scale_track ? animation->get_track_count() : -1;
			if (add_scale_track) {
				animation->add_track(Animation::TYPE_SCALE_3D);
				animation->track_set_path(scale_track, NodePath(track_path));
				animation->track_set_imported(scale_track, true);
			}

			for (double sample_time : sample_times) {
				const double key_time = (sample_time - start_time) / time_codes_per_second;

				if (position_track >= 0) {
					VtArray<GfVec3f> translations;
					if (translations_attr.Get(&translations, sample_time) && joint_index < (int)translations.size()) {
						const GfVec3f value = translations[joint_index];
						animation->position_track_insert_key(position_track, key_time, Vector3(value[0], value[1], value[2]));
						added_any_tracks = true;
					}
				}

				if (rotation_track >= 0) {
					VtArray<GfQuatf> rotations;
					if (rotations_attr.Get(&rotations, sample_time) && joint_index < (int)rotations.size()) {
						animation->rotation_track_insert_key(rotation_track, key_time, _gf_quat_to_godot(rotations[joint_index]));
						added_any_tracks = true;
					}
				}

				if (scale_track >= 0) {
					VtArray<GfVec3h> scales;
					if (scales_attr.Get(&scales, sample_time) && joint_index < (int)scales.size()) {
						const GfVec3h value = scales[joint_index];
						animation->scale_track_insert_key(scale_track, key_time, Vector3((real_t)value[0], (real_t)value[1], (real_t)value[2]));
						added_any_tracks = true;
					}
				}
			}
		}

		if (has_blend_shape_weights) {
			const String skeleton_prim_path = _get_usd_metadata(p_skeleton).get("usd:prim_path", String());

			List<Node *> stack;
			stack.push_back(p_scene_root);
			while (!stack.is_empty()) {
				Node *node = stack.front()->get();
				stack.pop_front();

				for (int child_index = 0; child_index < node->get_child_count(); child_index++) {
					stack.push_back(node->get_child(child_index));
				}

				MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(node);
				if (mesh_instance == nullptr || mesh_instance->get_mesh().is_null() || mesh_instance->get_blend_shape_count() == 0) {
					continue;
				}

				const Dictionary mesh_metadata = _get_usd_metadata(mesh_instance);
				if ((String)mesh_metadata.get("usd:skel_skeleton_path", String()) != skeleton_prim_path) {
					continue;
				}

				HashMap<String, bool> mesh_blend_shape_names;
				for (int blend_shape_index = 0; blend_shape_index < mesh_instance->get_blend_shape_count(); blend_shape_index++) {
					mesh_blend_shape_names.insert(String(mesh_instance->get_mesh()->get_blend_shape_name(blend_shape_index)), true);
				}

				const Dictionary mesh_blend_shape_channels = mesh_metadata.get("usd:blend_shape_channels", Dictionary());
				const String mesh_path = String(p_scene_root->get_path_to(mesh_instance));
				for (int animation_blend_shape_index = 0; animation_blend_shape_index < (int)animation_blend_shape_names.size(); animation_blend_shape_index++) {
					const String blend_shape_name = _to_godot_string(animation_blend_shape_names[animation_blend_shape_index].GetString());
					Vector<UsdBlendShapeChannelSpec> channel_specs = _parse_blend_shape_channel_specs(mesh_blend_shape_channels.get(blend_shape_name, Variant()));
					if (channel_specs.is_empty()) {
						UsdBlendShapeChannelSpec fallback_spec;
						fallback_spec.channel_name = blend_shape_name;
						fallback_spec.weight = 1.0f;
						fallback_spec.primary = true;
						channel_specs.push_back(fallback_spec);
					}

					bool has_all_channels = true;
					for (int channel_index = 0; channel_index < channel_specs.size(); channel_index++) {
						if (!mesh_blend_shape_names.has(channel_specs[channel_index].channel_name)) {
							has_all_channels = false;
							break;
						}
					}
					if (!has_all_channels) {
						continue;
					}

					const Vector<double> blend_shape_sample_times = _build_blend_shape_sample_times(blend_shape_weights_attr, animation_blend_shape_index, channel_specs, sample_times);
					HashMap<String, bool> add_blend_shape_track_by_channel;
					for (int channel_index = 0; channel_index < channel_specs.size(); channel_index++) {
						add_blend_shape_track_by_channel.insert(channel_specs[channel_index].channel_name, false);
					}
					for (int sample_index = 0; sample_index < blend_shape_sample_times.size(); sample_index++) {
						const double sample_time = blend_shape_sample_times[sample_index];
						VtArray<float> blend_shape_weights;
						if (blend_shape_weights_attr.Get(&blend_shape_weights, sample_time) && animation_blend_shape_index < (int)blend_shape_weights.size()) {
							const HashMap<String, float> evaluated_weights = _evaluate_blend_shape_channel_weights(blend_shape_weights[animation_blend_shape_index], channel_specs);
							for (int channel_index = 0; channel_index < channel_specs.size(); channel_index++) {
								const String &channel_name = channel_specs[channel_index].channel_name;
								const float *channel_weight_ptr = evaluated_weights.getptr(channel_name);
								if (channel_weight_ptr != nullptr && !Math::is_equal_approx(*channel_weight_ptr, 0.0f)) {
									add_blend_shape_track_by_channel[channel_name] = true;
								}
							}
						}
					}

					HashMap<String, int> blend_shape_tracks_by_channel;
					for (int channel_index = 0; channel_index < channel_specs.size(); channel_index++) {
						const String &channel_name = channel_specs[channel_index].channel_name;
						const bool *add_blend_shape_track_ptr = add_blend_shape_track_by_channel.getptr(channel_name);
						if (add_blend_shape_track_ptr == nullptr || !*add_blend_shape_track_ptr) {
							continue;
						}
						const int blend_shape_track = animation->get_track_count();
						animation->add_track(Animation::TYPE_BLEND_SHAPE);
						animation->track_set_path(blend_shape_track, NodePath(mesh_path + ":" + channel_name));
						animation->track_set_imported(blend_shape_track, true);
						blend_shape_tracks_by_channel.insert(channel_name, blend_shape_track);
					}

					for (int sample_index = 0; sample_index < blend_shape_sample_times.size(); sample_index++) {
						const double sample_time = blend_shape_sample_times[sample_index];
						VtArray<float> blend_shape_weights;
						if (!blend_shape_weights_attr.Get(&blend_shape_weights, sample_time) || animation_blend_shape_index >= (int)blend_shape_weights.size()) {
							continue;
						}
						const HashMap<String, float> evaluated_weights = _evaluate_blend_shape_channel_weights(blend_shape_weights[animation_blend_shape_index], channel_specs);
						for (int channel_index = 0; channel_index < channel_specs.size(); channel_index++) {
							const String &channel_name = channel_specs[channel_index].channel_name;
							const int *blend_shape_track_ptr = blend_shape_tracks_by_channel.getptr(channel_name);
							if (blend_shape_track_ptr == nullptr) {
								continue;
							}
							const float *channel_weight_ptr = evaluated_weights.getptr(channel_name);
							const float channel_weight = channel_weight_ptr != nullptr ? *channel_weight_ptr : 0.0f;
							animation->blend_shape_track_insert_key(*blend_shape_track_ptr, (sample_time - start_time) / time_codes_per_second, channel_weight);
							added_any_tracks = true;
						}
					}
				}
			}
		}

		if (!added_any_tracks) {
			return false;
		}

		library->add_animation(animation_name, animation);
		return true;
	}

	Ref<Skin> _build_mesh_skin(Skeleton3D *p_skeleton, const Transform3D &p_geom_bind_transform) const {
		ERR_FAIL_NULL_V(p_skeleton, Ref<Skin>());

		Ref<Skin> skin;
		skin.instantiate();
		skin->set_bind_count(p_skeleton->get_bone_count());

		for (int bone_index = 0; bone_index < p_skeleton->get_bone_count(); bone_index++) {
			Transform3D bind_transform = p_skeleton->get_bone_global_rest(bone_index);
			if (p_skeleton->has_bone_meta(bone_index, StringName("usd_joint_bind_transform"))) {
				bind_transform = p_skeleton->get_bone_meta(bone_index, StringName("usd_joint_bind_transform"));
			}

			skin->set_bind_bone(bone_index, bone_index);
			skin->set_bind_pose(bone_index, bind_transform.affine_inverse() * p_geom_bind_transform);
		}

		return skin;
	}

	void _append_skin_bindings(Node3D *p_root) const {
		ERR_FAIL_NULL(p_root);

		List<Node *> stack;
		stack.push_back(p_root);
		while (!stack.is_empty()) {
			Node *node = stack.front()->get();
			stack.pop_front();

			for (int i = 0; i < node->get_child_count(); i++) {
				stack.push_back(node->get_child(i));
			}

			MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(node);
			if (mesh_instance == nullptr || mesh_instance->get_mesh().is_null()) {
				continue;
			}

			const Dictionary metadata = _get_usd_metadata(mesh_instance);
			const String skeleton_prim_path = metadata.get("usd:skel_skeleton_path", String());
			if (skeleton_prim_path.is_empty()) {
				continue;
			}

			Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(_find_node_for_prim_path(p_root, skeleton_prim_path));
			if (skeleton == nullptr) {
				_set_usd_metadata(mesh_instance, "usd:skinning_status", vformat("Skinned mesh referenced missing skeleton prim: %s", skeleton_prim_path));
				continue;
			}

			Transform3D geom_bind_transform;
			if (metadata.has("usd:skel_geom_bind_transform")) {
				geom_bind_transform = metadata["usd:skel_geom_bind_transform"];
			}

			mesh_instance->set_skin(_build_mesh_skin(skeleton, geom_bind_transform));
			mesh_instance->set_skeleton_path(mesh_instance->get_path_to(skeleton));
		}
	}

	void _append_skeleton_animations(Node3D *p_root) const {
		ERR_FAIL_NULL(p_root);

		for (const UsdPrim &prim : stage->Traverse()) {
			if (!prim.IsA<UsdSkelSkeleton>()) {
				continue;
			}

			Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(_find_node_for_prim_path(p_root, _to_godot_string(prim.GetPath().GetString())));
			if (skeleton == nullptr) {
				continue;
			}

			UsdRelationship animation_source_rel = prim.GetRelationship(TfToken("skel:animationSource"));
			if (!animation_source_rel) {
				continue;
			}

			SdfPathVector targets;
			animation_source_rel.GetTargets(&targets);
			for (const SdfPath &target : targets) {
				UsdPrim animation_prim = stage->GetPrimAtPath(target);
				if (!animation_prim || !animation_prim.IsA<UsdSkelAnimation>()) {
					continue;
				}
				_append_baked_skeleton_animation(p_root, skeleton, UsdSkelAnimation(animation_prim));
			}
		}
	}

public:
	explicit UsdSceneBuilder(const UsdStageRefPtr &p_stage) :
			stage(p_stage),
			meters_per_unit(UsdGeomGetStageMetersPerUnit(stage)),
			up_axis(UsdGeomGetStageUpAxis(stage)),
			variant_catalog(_collect_variant_sets(stage)) {}

	Node *build(const String &p_scene_name) const {
		Node3D *root = memnew(Node3D);
		root->set_name(p_scene_name.is_empty() ? String("USDScene") : p_scene_name);

		root->set_transform(_get_stage_correction_transform());

	Dictionary stage_metadata;
	stage_metadata["usd:source_identifier"] = _to_godot_string(stage->GetRootLayer()->GetIdentifier());
	stage_metadata["usd:up_axis"] = _to_godot_string(up_axis.GetString());
	stage_metadata["usd:meters_per_unit"] = meters_per_unit;
	stage_metadata["usd:time_codes_per_second"] = stage->GetTimeCodesPerSecond();
	stage_metadata["usd:start_time_code"] = stage->GetStartTimeCode();
	stage_metadata["usd:end_time_code"] = stage->GetEndTimeCode();
	stage_metadata["usd:read_only_loader"] = true;
		const bool has_authored_lights = _stage_has_authored_lights();
		stage_metadata["usd:has_authored_lights"] = has_authored_lights;
		const UsdPreviewLightingMode preview_lighting_mode = _get_preview_lighting_mode();
		const bool add_preview_lighting = preview_lighting_mode == USD_PREVIEW_LIGHTING_ALWAYS || (preview_lighting_mode == USD_PREVIEW_LIGHTING_WHEN_MISSING && !has_authored_lights);
		stage_metadata["usd:preview_lighting_mode"] = _preview_lighting_mode_to_string(preview_lighting_mode);
		stage_metadata["usd:has_preview_lighting"] = add_preview_lighting;
		if (UsdPrim default_prim = stage->GetDefaultPrim()) {
			stage_metadata["usd:default_prim_path"] = _to_godot_string(default_prim.GetPath().GetString());
		}
		_set_usd_metadata_entries(root, stage_metadata);

		for (const UsdPrim &child_prim : stage->GetPseudoRoot().GetChildren()) {
			if (_should_skip_child_prim(child_prim)) {
				continue;
			}
			Node *child_node = _build_node_for_prim(child_prim);
			root->add_child(child_node);
			_append_children(child_prim, child_node);
		}

		if (add_preview_lighting) {
			const String preview_reason = preview_lighting_mode == USD_PREVIEW_LIGHTING_ALWAYS
					? String("Synthetic preview lighting was forced by project setting '") + USD_PREVIEW_LIGHTING_MODE_SETTING + "'."
					: "No authored UsdLux lights were found on the USD stage.";
			_append_preview_lighting(root, preview_reason);
		}

		_append_skin_bindings(root);
		_append_skeleton_animations(root);

		return root;
	}
};

class UsdSceneSaver {
	struct SaveContext {
		double meters_per_unit = 1.0;
		TfToken up_axis = UsdGeomTokens->y;
		double time_codes_per_second = 24.0;
		double start_time_code = 0.0;
		double end_time_code = 0.0;
		bool has_time_code_range = false;
		Vector<Node *> top_level_nodes;
		String default_prim_path;
	};

	static constexpr const char *GODOT_MATERIAL_SCOPE_NAME = "__GodotMaterials";

	static bool _is_generated_preview_node(const Node *p_node) {
		const Dictionary metadata = _get_usd_metadata(p_node);
		return (bool)metadata.get("usd:generated_preview", false);
	}

	static bool _is_skipped_support_node(const Node *p_node) {
		if (Object::cast_to<AnimationPlayer>(p_node) != nullptr) {
			return true;
		}
		const Dictionary metadata = _get_usd_metadata(p_node);
		return (String)metadata.get("usd:type_name", String()) == String("SkelAnimation");
	}

	static bool _has_preserved_composition_arcs(const Object *p_object) {
		ERR_FAIL_NULL_V(p_object, false);
		const Dictionary metadata = _get_usd_metadata(p_object);
		return !((Array)metadata.get("usd:references", Array())).is_empty() ||
				!((Array)metadata.get("usd:payloads", Array())).is_empty() ||
				!((Array)metadata.get("usd:inherits", Array())).is_empty() ||
				!((Array)metadata.get("usd:specializes", Array())).is_empty();
	}

	static real_t _meters_scale(double p_meters_per_unit) {
		return (real_t)p_meters_per_unit;
	}

	static Transform3D _get_stage_correction_transform(double p_meters_per_unit, const TfToken &p_up_axis) {
		const real_t unit_scale = _meters_scale(p_meters_per_unit);
		Basis root_basis;
		if (p_up_axis == UsdGeomTokens->z) {
			root_basis = Basis(Vector3(1, 0, 0), (real_t)-Math::PI * 0.5);
		}
		root_basis = root_basis.scaled(Vector3(unit_scale, unit_scale, unit_scale));
		return Transform3D(root_basis, Vector3());
	}

	static TfToken _up_axis_from_string(const String &p_value) {
		if (p_value.to_upper() == "Z") {
			return UsdGeomTokens->z;
		}
		return UsdGeomTokens->y;
	}

	static String _make_valid_identifier(const String &p_name) {
		String identifier = _to_godot_string(TfMakeValidIdentifier(p_name.strip_edges().utf8().get_data()));
		if (identifier.is_empty()) {
			identifier = "Node";
		}
		return identifier;
	}

	static bool _is_stage_container(const Node *p_root) {
		const Dictionary metadata = _get_usd_metadata(p_root);
		return (bool)metadata.get("usd:read_only_loader", false);
	}

	static SaveContext _make_save_context(Node *p_root) {
		SaveContext context;
		Node *metadata_source = p_root;

		if (Object::cast_to<UsdStageInstance>(p_root) || _is_stage_instance_generated_root_node(p_root)) {
			Node *generated_root = _is_stage_instance_generated_root_node(p_root) ? p_root : nullptr;
			if (generated_root == nullptr) {
				for (int i = 0; i < p_root->get_child_count(); i++) {
					Node *child = p_root->get_child(i);
					if (_is_stage_instance_generated_root_node(child)) {
						generated_root = child;
						break;
					}
				}
			}

			if (generated_root != nullptr) {
				metadata_source = generated_root;
				for (int i = 0; i < generated_root->get_child_count(); i++) {
					Node *child = generated_root->get_child(i);
					if (_is_generated_preview_node(child)) {
						continue;
					}
					context.top_level_nodes.push_back(child);
				}
			}
		}

		const Dictionary metadata = _get_usd_metadata(metadata_source);

		if (metadata.has("usd:meters_per_unit")) {
			context.meters_per_unit = (double)metadata["usd:meters_per_unit"];
		}
		if (metadata.has("usd:up_axis")) {
			context.up_axis = _up_axis_from_string((String)metadata["usd:up_axis"]);
		}
		if (metadata.has("usd:time_codes_per_second")) {
			context.time_codes_per_second = MAX((double)metadata["usd:time_codes_per_second"], 1.0);
		}
		if (metadata.has("usd:start_time_code")) {
			context.start_time_code = (double)metadata["usd:start_time_code"];
			context.has_time_code_range = true;
		}
		if (metadata.has("usd:end_time_code")) {
			context.end_time_code = (double)metadata["usd:end_time_code"];
			context.has_time_code_range = true;
		}
		if (metadata.has("usd:default_prim_path")) {
			context.default_prim_path = metadata["usd:default_prim_path"];
		}

		if (!context.top_level_nodes.is_empty()) {
			return context;
		}

		if (_is_stage_container(p_root)) {
			for (int i = 0; i < p_root->get_child_count(); i++) {
				Node *child = p_root->get_child(i);
				if (_is_generated_preview_node(child)) {
					continue;
				}
				context.top_level_nodes.push_back(child);
			}
		} else {
			context.top_level_nodes.push_back(p_root);
		}

		return context;
	}

	static void _apply_common_light_attributes(const Light3D *p_light, const UsdLuxNonboundableLightBase &p_usd_light) {
		p_usd_light.CreateColorAttr().Set(GfVec3f(p_light->get_color().r, p_light->get_color().g, p_light->get_color().b));
		p_usd_light.CreateIntensityAttr().Set((float)p_light->get_param(Light3D::PARAM_INTENSITY));
		p_usd_light.CreateExposureAttr().Set(0.0f);
	}

	static void _apply_common_light_attributes(const Light3D *p_light, const UsdLuxBoundableLightBase &p_usd_light) {
		p_usd_light.CreateColorAttr().Set(GfVec3f(p_light->get_color().r, p_light->get_color().g, p_light->get_color().b));
		p_usd_light.CreateIntensityAttr().Set((float)p_light->get_param(Light3D::PARAM_INTENSITY));
		p_usd_light.CreateExposureAttr().Set(0.0f);
	}

	static String _make_relative_asset_path(const String &p_save_path, const String &p_asset_path) {
		if (p_asset_path.is_empty()) {
			return String();
		}

		const String absolute_asset_path = _get_absolute_path(p_asset_path);
		if (!absolute_asset_path.is_absolute_path()) {
			return absolute_asset_path;
		}

		const String save_dir = _get_absolute_path(p_save_path).get_base_dir();
		if (save_dir.is_empty()) {
			return absolute_asset_path;
		}

		return save_dir.path_to_file(absolute_asset_path);
	}

	static String _get_texture_asset_path(const Ref<Texture2D> &p_texture, const String &p_save_path) {
		if (p_texture.is_null()) {
			return String();
		}

		const String texture_path = p_texture->get_path();
		if (texture_path.is_empty() || texture_path.contains("::")) {
			return String();
		}

		return _make_relative_asset_path(p_save_path, texture_path);
	}

	static TfToken _get_usd_texture_output_for_channel(BaseMaterial3D::TextureChannel p_channel) {
		switch (p_channel) {
			case BaseMaterial3D::TEXTURE_CHANNEL_RED:
				return TfToken("r");
			case BaseMaterial3D::TEXTURE_CHANNEL_GREEN:
				return TfToken("g");
			case BaseMaterial3D::TEXTURE_CHANNEL_BLUE:
				return TfToken("b");
			case BaseMaterial3D::TEXTURE_CHANNEL_ALPHA:
				return TfToken("a");
			case BaseMaterial3D::TEXTURE_CHANNEL_GRAYSCALE:
			default:
				return TfToken("rgb");
		}
	}

	static SdfValueTypeName _get_usd_output_type_for_channel(BaseMaterial3D::TextureChannel p_channel) {
		if (p_channel == BaseMaterial3D::TEXTURE_CHANNEL_GRAYSCALE) {
			return SdfValueTypeNames->Float3;
		}
		return SdfValueTypeNames->Float;
	}

	static bool _write_texture_uv_transform(const UsdStageRefPtr &p_stage, const BaseMaterial3D *p_material, UsdShadeShader p_texture_shader, const SdfPath &p_shader_path) {
		ERR_FAIL_NULL_V(p_material, false);
		if (!p_texture_shader) {
			return false;
		}

		const Vector3 uv_scale = p_material->get_uv1_scale();
		const Vector3 uv_offset = p_material->get_uv1_offset();
		if (uv_scale.is_equal_approx(Vector3(1.0f, 1.0f, 1.0f)) && uv_offset.is_equal_approx(Vector3())) {
			return false;
		}

		UsdShadeShader uv_transform = UsdShadeShader::Define(p_stage, p_shader_path.AppendChild(TfToken("UVTransform")));
		uv_transform.CreateIdAttr(VtValue(TfToken("UsdTransform2d")));
		uv_transform.CreateInput(TfToken("scale"), SdfValueTypeNames->Float2).Set(GfVec2f(uv_scale.x, uv_scale.y));
		uv_transform.CreateInput(TfToken("translation"), SdfValueTypeNames->Float2).Set(GfVec2f(uv_offset.x, 1.0f - uv_scale.y - uv_offset.y));
		uv_transform.CreateInput(TfToken("rotation"), SdfValueTypeNames->Float).Set(0.0f);

		UsdShadeOutput uv_output = uv_transform.CreateOutput(TfToken("result"), SdfValueTypeNames->Float2);
		p_texture_shader.CreateInput(TfToken("st"), SdfValueTypeNames->Float2).ConnectToSource(uv_output);
		return true;
	}

	static bool _connect_preview_texture(const UsdStageRefPtr &p_stage, const String &p_save_path, const BaseMaterial3D *p_material, const Ref<Texture2D> &p_texture, const SdfPath &p_material_path, const char *p_shader_name, const char *p_input_name, const SdfValueTypeName &p_input_type, const TfToken &p_output_name, const SdfValueTypeName &p_output_type) {
		ERR_FAIL_NULL_V(p_material, false);
		if (p_texture.is_null()) {
			return false;
		}

		const String asset_path = _get_texture_asset_path(p_texture, p_save_path);
		if (asset_path.is_empty()) {
			return false;
		}

		UsdShadeShader preview_surface = UsdShadeShader::Get(p_stage, p_material_path.AppendChild(TfToken("PreviewSurface")));
		if (!preview_surface) {
			return false;
		}

		const SdfPath texture_shader_path = p_material_path.AppendChild(TfToken(p_shader_name));
		UsdShadeShader texture_shader = UsdShadeShader::Define(p_stage, texture_shader_path);
		texture_shader.CreateIdAttr(VtValue(TfToken("UsdUVTexture")));
		texture_shader.CreateInput(TfToken("file"), SdfValueTypeNames->Asset).Set(SdfAssetPath(asset_path.utf8().get_data()));
		_write_texture_uv_transform(p_stage, p_material, texture_shader, texture_shader_path);

		UsdShadeOutput texture_output = texture_shader.CreateOutput(p_output_name, p_output_type);
		preview_surface.CreateInput(TfToken(p_input_name), p_input_type).ConnectToSource(texture_output);
		return true;
	}

	static bool _deserialize_unmapped_attribute_value(const Dictionary &p_description, SdfValueTypeName *r_type_name, VtValue *r_value) {
		ERR_FAIL_NULL_V(r_type_name, false);
		ERR_FAIL_NULL_V(r_value, false);

		const String type_name = p_description.get("type_name", String());
		const String value_kind = p_description.get("typed_value_kind", String());
		if (type_name.is_empty() || value_kind.is_empty() || !p_description.has("typed_value")) {
			return false;
		}

		const Variant typed_value = p_description["typed_value"];

		if (type_name == "bool" && value_kind == "bool") {
			*r_type_name = SdfValueTypeNames->Bool;
			*r_value = VtValue((bool)typed_value);
			return true;
		}
		if (type_name == "int" && value_kind == "int") {
			*r_type_name = SdfValueTypeNames->Int;
			*r_value = VtValue((int)typed_value);
			return true;
		}
		if (type_name == "int64" && value_kind == "int64") {
			*r_type_name = SdfValueTypeNames->Int64;
			*r_value = VtValue((int64_t)typed_value);
			return true;
		}
		if (type_name == "float" && value_kind == "float") {
			*r_type_name = SdfValueTypeNames->Float;
			*r_value = VtValue((float)(double)typed_value);
			return true;
		}
		if (type_name == "double" && value_kind == "double") {
			*r_type_name = SdfValueTypeNames->Double;
			*r_value = VtValue((double)typed_value);
			return true;
		}
		if (type_name == "string" && value_kind == "string") {
			*r_type_name = SdfValueTypeNames->String;
			*r_value = VtValue(std::string(((String)typed_value).utf8().get_data()));
			return true;
		}
		if (type_name == "token" && value_kind == "token") {
			*r_type_name = SdfValueTypeNames->Token;
			*r_value = VtValue(TfToken(((String)typed_value).utf8().get_data()));
			return true;
		}
		if (type_name == "asset" && value_kind == "asset") {
			*r_type_name = SdfValueTypeNames->Asset;
			*r_value = VtValue(SdfAssetPath(((String)typed_value).utf8().get_data()));
			return true;
		}
		if ((type_name == "float2" || type_name == "texCoord2f") && value_kind == "vector2") {
			const Vector2 vector = typed_value;
			*r_type_name = (type_name == "texCoord2f") ? SdfValueTypeNames->TexCoord2f : SdfValueTypeNames->Float2;
			*r_value = VtValue(GfVec2f(vector.x, vector.y));
			return true;
		}
		if ((type_name == "float3" || type_name == "color3f" || type_name == "normal3f" || type_name == "point3f" || type_name == "vector3f") && value_kind == "vector3") {
			const Vector3 vector = typed_value;
			if (type_name == "color3f") {
				*r_type_name = SdfValueTypeNames->Color3f;
			} else if (type_name == "normal3f") {
				*r_type_name = SdfValueTypeNames->Normal3f;
			} else if (type_name == "point3f") {
				*r_type_name = SdfValueTypeNames->Point3f;
			} else if (type_name == "vector3f") {
				*r_type_name = SdfValueTypeNames->Vector3f;
			} else {
				*r_type_name = SdfValueTypeNames->Float3;
			}
			*r_value = VtValue(GfVec3f(vector.x, vector.y, vector.z));
			return true;
		}

		if (typed_value.get_type() != Variant::ARRAY) {
			return false;
		}

		const Array values = typed_value;
		if (type_name == "bool[]" && value_kind == "bool_array") {
			VtArray<bool> vt_values;
			vt_values.resize(values.size());
			for (int i = 0; i < values.size(); i++) {
				vt_values[i] = (bool)values[i];
			}
			*r_type_name = SdfValueTypeNames->BoolArray;
			*r_value = VtValue(vt_values);
			return true;
		}
		if (type_name == "int[]" && value_kind == "int_array") {
			VtArray<int> vt_values;
			vt_values.resize(values.size());
			for (int i = 0; i < values.size(); i++) {
				vt_values[i] = (int)values[i];
			}
			*r_type_name = SdfValueTypeNames->IntArray;
			*r_value = VtValue(vt_values);
			return true;
		}
		if (type_name == "int64[]" && value_kind == "int64_array") {
			VtArray<int64_t> vt_values;
			vt_values.resize(values.size());
			for (int i = 0; i < values.size(); i++) {
				vt_values[i] = (int64_t)values[i];
			}
			*r_type_name = SdfValueTypeNames->Int64Array;
			*r_value = VtValue(vt_values);
			return true;
		}
		if (type_name == "float[]" && value_kind == "float_array") {
			VtArray<float> vt_values;
			vt_values.resize(values.size());
			for (int i = 0; i < values.size(); i++) {
				vt_values[i] = (float)(double)values[i];
			}
			*r_type_name = SdfValueTypeNames->FloatArray;
			*r_value = VtValue(vt_values);
			return true;
		}
		if (type_name == "double[]" && value_kind == "double_array") {
			VtArray<double> vt_values;
			vt_values.resize(values.size());
			for (int i = 0; i < values.size(); i++) {
				vt_values[i] = (double)values[i];
			}
			*r_type_name = SdfValueTypeNames->DoubleArray;
			*r_value = VtValue(vt_values);
			return true;
		}
		if (type_name == "string[]" && value_kind == "string_array") {
			VtArray<std::string> vt_values;
			vt_values.resize(values.size());
			for (int i = 0; i < values.size(); i++) {
				vt_values[i] = std::string(((String)values[i]).utf8().get_data());
			}
			*r_type_name = SdfValueTypeNames->StringArray;
			*r_value = VtValue(vt_values);
			return true;
		}
		if (type_name == "token[]" && value_kind == "token_array") {
			VtArray<TfToken> vt_values;
			vt_values.resize(values.size());
			for (int i = 0; i < values.size(); i++) {
				vt_values[i] = TfToken(((String)values[i]).utf8().get_data());
			}
			*r_type_name = SdfValueTypeNames->TokenArray;
			*r_value = VtValue(vt_values);
			return true;
		}
		if ((type_name == "float2[]" || type_name == "texCoord2f[]") && value_kind == "vector2_array") {
			VtArray<GfVec2f> vt_values;
			vt_values.resize(values.size());
			for (int i = 0; i < values.size(); i++) {
				const Vector2 vector = values[i];
				vt_values[i] = GfVec2f(vector.x, vector.y);
			}
			*r_type_name = (type_name == "texCoord2f[]") ? SdfValueTypeNames->TexCoord2fArray : SdfValueTypeNames->Float2Array;
			*r_value = VtValue(vt_values);
			return true;
		}
		if ((type_name == "float3[]" || type_name == "color3f[]" || type_name == "normal3f[]" || type_name == "point3f[]" || type_name == "vector3f[]") && value_kind == "vector3_array") {
			VtArray<GfVec3f> vt_values;
			vt_values.resize(values.size());
			for (int i = 0; i < values.size(); i++) {
				const Vector3 vector = values[i];
				vt_values[i] = GfVec3f(vector.x, vector.y, vector.z);
			}
			if (type_name == "color3f[]") {
				*r_type_name = SdfValueTypeNames->Color3fArray;
			} else if (type_name == "normal3f[]") {
				*r_type_name = SdfValueTypeNames->Normal3fArray;
			} else if (type_name == "point3f[]") {
				*r_type_name = SdfValueTypeNames->Point3fArray;
			} else if (type_name == "vector3f[]") {
				*r_type_name = SdfValueTypeNames->Vector3fArray;
			} else {
				*r_type_name = SdfValueTypeNames->Float3Array;
			}
			*r_value = VtValue(vt_values);
			return true;
		}

		return false;
	}

	static bool _deserialize_layer_offset(const Variant &p_value, SdfLayerOffset *r_layer_offset) {
		ERR_FAIL_NULL_V(r_layer_offset, false);
		if (p_value.get_type() != Variant::DICTIONARY) {
			return false;
		}

		const Dictionary description = p_value;
		const double offset = description.get("offset", 0.0);
		const double scale = description.get("scale", 1.0);
		*r_layer_offset = SdfLayerOffset(offset, scale);
		return true;
	}

	static bool _deserialize_reference(const Dictionary &p_description, SdfReference *r_reference) {
		ERR_FAIL_NULL_V(r_reference, false);
		const String asset_path = p_description.get("asset_path", String());
		const String prim_path = p_description.get("prim_path", String());
		SdfLayerOffset layer_offset;
		_deserialize_layer_offset(p_description.get("layer_offset", Dictionary()), &layer_offset);
		*r_reference = SdfReference(asset_path.utf8().get_data(), prim_path.is_empty() ? SdfPath() : SdfPath(prim_path.utf8().get_data()), layer_offset);
		return true;
	}

	static bool _deserialize_payload(const Dictionary &p_description, SdfPayload *r_payload) {
		ERR_FAIL_NULL_V(r_payload, false);
		const String asset_path = p_description.get("asset_path", String());
		const String prim_path = p_description.get("prim_path", String());
		SdfLayerOffset layer_offset;
		_deserialize_layer_offset(p_description.get("layer_offset", Dictionary()), &layer_offset);
		*r_payload = SdfPayload(asset_path.utf8().get_data(), prim_path.is_empty() ? SdfPath() : SdfPath(prim_path.utf8().get_data()), layer_offset);
		return true;
	}

	static SdfPathVector _deserialize_path_array(const Array &p_paths) {
		SdfPathVector paths;
		for (int i = 0; i < p_paths.size(); i++) {
			if (p_paths[i].get_type() != Variant::STRING) {
				continue;
			}
			const String path = p_paths[i];
			if (path.is_empty()) {
				continue;
			}
			const SdfPath sdf_path(path.utf8().get_data());
			if (!sdf_path.IsEmpty()) {
				paths.push_back(sdf_path);
			}
		}
		return paths;
	}

	static bool _reapply_composition_arcs(const UsdPrim &p_prim, const Object *p_source_object) {
		ERR_FAIL_NULL_V(p_source_object, false);
		if (!p_prim) {
			return false;
		}

		const Dictionary metadata = _get_usd_metadata(p_source_object);
		bool applied_any = false;

		const Array references = metadata.get("usd:references", Array());
		if (!references.is_empty()) {
			SdfReferenceVector reference_items;
			for (int i = 0; i < references.size(); i++) {
				if (references[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				SdfReference reference;
				if (_deserialize_reference(references[i], &reference)) {
					reference_items.push_back(reference);
				}
			}

			if (!reference_items.empty()) {
				UsdReferences usd_references = p_prim.GetReferences();
				if (usd_references) {
					usd_references.SetReferences(reference_items);
					applied_any = true;
				}
			}
		}

		const Array payloads = metadata.get("usd:payloads", Array());
		if (!payloads.is_empty()) {
			SdfPayloadVector payload_items;
			for (int i = 0; i < payloads.size(); i++) {
				if (payloads[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				SdfPayload payload;
				if (_deserialize_payload(payloads[i], &payload)) {
					payload_items.push_back(payload);
				}
			}

			if (!payload_items.empty()) {
				UsdPayloads usd_payloads = p_prim.GetPayloads();
				if (usd_payloads) {
					usd_payloads.SetPayloads(payload_items);
					applied_any = true;
				}
			}
		}

		const Array inherits = metadata.get("usd:inherits", Array());
		if (!inherits.is_empty()) {
			const SdfPathVector inherit_items = _deserialize_path_array(inherits);
			if (!inherit_items.empty()) {
				UsdInherits usd_inherits = p_prim.GetInherits();
				if (usd_inherits) {
					usd_inherits.SetInherits(inherit_items);
					applied_any = true;
				}
			}
		}

		const Array specializes = metadata.get("usd:specializes", Array());
		if (!specializes.is_empty()) {
			const SdfPathVector specialize_items = _deserialize_path_array(specializes);
			if (!specialize_items.empty()) {
				UsdSpecializes usd_specializes = p_prim.GetSpecializes();
				if (usd_specializes) {
					usd_specializes.SetSpecializes(specialize_items);
					applied_any = true;
				}
			}
		}

		return applied_any;
	}

	static void _reapply_unmapped_properties(const UsdPrim &p_prim, const Object *p_source_object) {
		ERR_FAIL_NULL(p_source_object);
		if (!p_prim) {
			return;
		}

		const Dictionary metadata = _get_usd_metadata(p_source_object);
		const Dictionary unmapped_attributes = metadata.get("usd:unmapped_attributes", Dictionary());
		if (!unmapped_attributes.is_empty()) {
			Array attribute_names = unmapped_attributes.keys();
			for (int i = 0; i < attribute_names.size(); i++) {
				const String attribute_name = attribute_names[i];
				const Dictionary description = unmapped_attributes[attribute_name];
				SdfValueTypeName type_name;
				VtValue value;
				if (!_deserialize_unmapped_attribute_value(description, &type_name, &value)) {
					continue;
				}

				const bool is_custom = description.get("is_custom", true);
				UsdAttribute attribute = p_prim.CreateAttribute(TfToken(attribute_name.utf8().get_data()), type_name, is_custom);
				if (attribute) {
					attribute.Set(value, UsdTimeCode::Default());
				}
			}
		}

		const Dictionary unmapped_relationships = metadata.get("usd:unmapped_relationships", Dictionary());
		if (!unmapped_relationships.is_empty()) {
			Array relationship_names = unmapped_relationships.keys();
			for (int i = 0; i < relationship_names.size(); i++) {
				const String relationship_name = relationship_names[i];
				const Variant stored_relationship = unmapped_relationships[relationship_name];

				bool is_custom = true;
				Array targets;
				if (stored_relationship.get_type() == Variant::DICTIONARY) {
					const Dictionary relationship_description = stored_relationship;
					is_custom = relationship_description.get("is_custom", true);
					targets = relationship_description.get("targets", Array());
				} else if (stored_relationship.get_type() == Variant::ARRAY) {
					targets = stored_relationship;
				} else {
					continue;
				}

				SdfPathVector target_paths;
				for (int target_index = 0; target_index < targets.size(); target_index++) {
					const String target = targets[target_index];
					if (target.is_empty()) {
						continue;
					}
					target_paths.push_back(SdfPath(target.utf8().get_data()));
				}

				UsdRelationship relationship = p_prim.CreateRelationship(TfToken(relationship_name.utf8().get_data()), is_custom);
				if (relationship && !target_paths.empty()) {
					relationship.SetTargets(target_paths);
				}
			}
		}
	}

	static bool _write_preview_material(const UsdStageRefPtr &p_stage, const Ref<Material> &p_material, const SdfPath &p_mesh_path, const String &p_save_path, const String &p_material_key, const String &p_preferred_material_path, UsdShadeMaterial *r_material) {
		ERR_FAIL_NULL_V(r_material, false);
		*r_material = UsdShadeMaterial();

		BaseMaterial3D *base_material = Object::cast_to<BaseMaterial3D>(p_material.ptr());
		if (base_material == nullptr) {
			return false;
		}

		const String material_name = _make_valid_identifier(p_material_key.is_empty() ? (p_material->get_name().is_empty() ? String("Material") : p_material->get_name()) : p_material_key);
		SdfPath material_path = p_mesh_path.AppendChild(TfToken(GODOT_MATERIAL_SCOPE_NAME)).AppendChild(TfToken(material_name.utf8().get_data()));
		if (!p_preferred_material_path.is_empty()) {
			const SdfPath preferred_material_path(p_preferred_material_path.utf8().get_data());
			if (preferred_material_path.IsAbsolutePath() && preferred_material_path.IsPrimPath()) {
				material_path = preferred_material_path;
			}
		}
		UsdShadeMaterial usd_material = UsdShadeMaterial::Define(p_stage, material_path);
		UsdShadeShader preview_surface = UsdShadeShader::Define(p_stage, material_path.AppendChild(TfToken("PreviewSurface")));
		preview_surface.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
		usd_material.CreateSurfaceOutput().ConnectToSource(preview_surface.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token));

		const Color albedo = base_material->get_albedo();
		preview_surface.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f).Set(GfVec3f(albedo.r, albedo.g, albedo.b));
		preview_surface.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float).Set(base_material->get_metallic());
		preview_surface.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(base_material->get_roughness());

		const bool has_emission = base_material->get_feature(BaseMaterial3D::FEATURE_EMISSION) || base_material->get_texture(BaseMaterial3D::TEXTURE_EMISSION).is_valid();
		if (has_emission) {
			const Color emission = base_material->get_emission();
			preview_surface.CreateInput(TfToken("emissiveColor"), SdfValueTypeNames->Color3f).Set(GfVec3f(emission.r, emission.g, emission.b));
		}

		if (base_material->get_transparency() != BaseMaterial3D::TRANSPARENCY_DISABLED) {
			preview_surface.CreateInput(TfToken("opacity"), SdfValueTypeNames->Float).Set(CLAMP(albedo.a, 0.0f, 1.0f));
		}
		if (base_material->get_transparency() == BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR) {
			preview_surface.CreateInput(TfToken("opacityThreshold"), SdfValueTypeNames->Float).Set(CLAMP(base_material->get_alpha_scissor_threshold(), 0.0f, 1.0f));
		}

		_connect_preview_texture(p_stage, p_save_path, base_material, base_material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO), material_path, "AlbedoTexture", "diffuseColor", SdfValueTypeNames->Color3f, TfToken("rgb"), SdfValueTypeNames->Float3);
		_connect_preview_texture(p_stage, p_save_path, base_material, base_material->get_texture(BaseMaterial3D::TEXTURE_EMISSION), material_path, "EmissionTexture", "emissiveColor", SdfValueTypeNames->Color3f, TfToken("rgb"), SdfValueTypeNames->Float3);
		_connect_preview_texture(p_stage, p_save_path, base_material, base_material->get_texture(BaseMaterial3D::TEXTURE_NORMAL), material_path, "NormalTexture", "normal", SdfValueTypeNames->Normal3f, TfToken("rgb"), SdfValueTypeNames->Float3);
		_connect_preview_texture(p_stage, p_save_path, base_material, base_material->get_texture(BaseMaterial3D::TEXTURE_METALLIC), material_path, "MetallicTexture", "metallic", SdfValueTypeNames->Float, _get_usd_texture_output_for_channel(base_material->get_metallic_texture_channel()), _get_usd_output_type_for_channel(base_material->get_metallic_texture_channel()));
		_connect_preview_texture(p_stage, p_save_path, base_material, base_material->get_texture(BaseMaterial3D::TEXTURE_ROUGHNESS), material_path, "RoughnessTexture", "roughness", SdfValueTypeNames->Float, _get_usd_texture_output_for_channel(base_material->get_roughness_texture_channel()), _get_usd_output_type_for_channel(base_material->get_roughness_texture_channel()));

		*r_material = usd_material;
		return true;
	}

	static void _write_mesh_material_binding(const UsdStageRefPtr &p_stage, MeshInstance3D *p_mesh_instance, const UsdGeomMesh &p_usd_mesh, const SdfPath &p_mesh_path, const String &p_save_path, const Vector<UsdMeshSurfaceFaceRange> &p_surface_face_ranges) {
		ERR_FAIL_NULL(p_mesh_instance);
		if (!p_usd_mesh) {
			return;
		}

		Ref<Mesh> mesh = p_mesh_instance->get_mesh();
		if (mesh.is_null() || mesh->get_surface_count() == 0 || p_surface_face_ranges.size() != mesh->get_surface_count()) {
			return;
		}

		Ref<Material> shared_material;
		bool can_use_shared_binding = true;
		bool have_non_empty_surface = false;
		for (int surface_index = 0; surface_index < mesh->get_surface_count(); surface_index++) {
			if (p_surface_face_ranges[surface_index].face_count <= 0) {
				continue;
			}

			have_non_empty_surface = true;
			const Ref<Material> surface_material = p_mesh_instance->get_active_material(surface_index);
			if (surface_material.is_null()) {
				can_use_shared_binding = false;
				continue;
			}
			if (shared_material.is_null()) {
				shared_material = surface_material;
			} else if (surface_material != shared_material) {
				can_use_shared_binding = false;
			}
		}

		if (!have_non_empty_surface) {
			return;
		}

		const auto make_subset_faces = [&](const UsdMeshSurfaceFaceRange &p_surface_range) {
			VtIntArray subset_faces;
			if (!p_surface_range.saved_face_indices.is_empty()) {
				subset_faces.reserve(p_surface_range.saved_face_indices.size());
				for (int i = 0; i < p_surface_range.saved_face_indices.size(); i++) {
					subset_faces.push_back(p_surface_range.saved_face_indices[i]);
				}
			} else {
				subset_faces.reserve(p_surface_range.face_count);
				for (int face_index = 0; face_index < p_surface_range.face_count; face_index++) {
					subset_faces.push_back(p_surface_range.face_start + face_index);
				}
			}
			return subset_faces;
		};

		HashMap<ObjectID, UsdShadeMaterial> material_cache;
		HashMap<String, int> material_name_counts;
		const auto resolve_usd_material = [&](const Ref<Material> &p_material, const String &p_preferred_material_path = String()) -> UsdShadeMaterial {
			if (p_material.is_null()) {
				return UsdShadeMaterial();
			}

			const ObjectID material_id = p_material->get_instance_id();
			if (material_cache.has(material_id)) {
				return material_cache[material_id];
			}

			const String base_name = _make_valid_identifier(p_material->get_name().is_empty() ? String("Material") : p_material->get_name());
			const int name_count = material_name_counts.has(base_name) ? material_name_counts[base_name] : 0;
			material_name_counts.insert(base_name, name_count + 1);
			const String material_key = name_count == 0 ? base_name : vformat("%s_%d", base_name, name_count + 1);

			UsdShadeMaterial usd_material;
			if (_write_preview_material(p_stage, p_material, p_mesh_path, p_save_path, material_key, p_preferred_material_path, &usd_material) && usd_material) {
				material_cache.insert(material_id, usd_material);
				return usd_material;
			}

			return UsdShadeMaterial();
		};

		const Dictionary usd_metadata = _get_usd_metadata(p_mesh_instance);
		const Array surface_descriptions = usd_metadata.get("usd:material_subsets", Array());
		const bool has_surface_descriptions = surface_descriptions.size() == mesh->get_surface_count();

		auto get_surface_description = [&](int p_surface_index) -> Dictionary {
			if (!has_surface_descriptions) {
				return Dictionary();
			}
			if (surface_descriptions[p_surface_index].get_type() != Variant::DICTIONARY) {
				return Dictionary();
			}
			return surface_descriptions[p_surface_index];
		};

		bool preserve_subset_structure = false;
		if (has_surface_descriptions) {
			for (int surface_index = 0; surface_index < surface_descriptions.size(); surface_index++) {
				const Dictionary description = get_surface_description(surface_index);
				if ((String)description.get("binding_kind", String()) == String("subset")) {
					preserve_subset_structure = true;
					break;
				}
			}
		}

		if (preserve_subset_structure) {
			UsdShadeMaterialBindingAPI::Apply(p_usd_mesh.GetPrim());
			for (int surface_index = 0; surface_index < mesh->get_surface_count(); surface_index++) {
				const UsdMeshSurfaceFaceRange &surface_range = p_surface_face_ranges[surface_index];
				if (surface_range.face_count <= 0) {
					continue;
				}

				const Dictionary description = get_surface_description(surface_index);
				const String binding_kind = description.get("binding_kind", String("mesh"));
				const bool has_material_binding = description.get("has_material_binding", true);
				const String preferred_material_path = description.get("material_path", String());

				if (binding_kind != "subset") {
					if (!has_material_binding) {
						continue;
					}

					const Ref<Material> surface_material = p_mesh_instance->get_active_material(surface_index);
					if (surface_material.is_null()) {
						continue;
					}

					const UsdShadeMaterial usd_material = resolve_usd_material(surface_material, preferred_material_path);
					if (!usd_material) {
						continue;
					}

					UsdShadeMaterialBindingAPI::Apply(p_usd_mesh.GetPrim()).Bind(usd_material);
					continue;
				}

				VtIntArray subset_faces = make_subset_faces(surface_range);

				const String subset_path_string = description.get("subset_path", String());
				const String subset_name = description.get("subset_name", vformat("Surface_%d", surface_index));
				const String family_name_string = description.get("family_name", String("materialBind"));
				const String family_type_string = description.get("family_type", String("nonOverlapping"));
				const TfToken family_name = TfToken(family_name_string.utf8().get_data());
				const TfToken family_type = TfToken(family_type_string.utf8().get_data());

				UsdGeomSubset subset = _define_preserved_subset(p_usd_mesh, subset_path_string, subset_name, UsdGeomTokens->face, subset_faces, family_name, family_type);
				if (!has_material_binding) {
					continue;
				}

				const Ref<Material> surface_material = p_mesh_instance->get_active_material(surface_index);
				if (surface_material.is_null()) {
					continue;
				}

				const UsdShadeMaterial usd_material = resolve_usd_material(surface_material, preferred_material_path);
				if (!usd_material) {
					continue;
				}

				UsdShadeMaterialBindingAPI::Apply(subset.GetPrim()).Bind(usd_material);
			}
			return;
		}

		if (can_use_shared_binding && shared_material.is_valid()) {
			String preferred_material_path;
			for (int surface_index = 0; surface_index < mesh->get_surface_count(); surface_index++) {
				const Dictionary description = get_surface_description(surface_index);
				preferred_material_path = description.get("material_path", String());
				if (!preferred_material_path.is_empty()) {
					break;
				}
			}
			const UsdShadeMaterial usd_material = resolve_usd_material(shared_material, preferred_material_path);
			if (usd_material) {
				UsdShadeMaterialBindingAPI::Apply(p_usd_mesh.GetPrim()).Bind(usd_material);
			}
			return;
		}

		UsdShadeMaterialBindingAPI::Apply(p_usd_mesh.GetPrim());
		for (int surface_index = 0; surface_index < mesh->get_surface_count(); surface_index++) {
			const UsdMeshSurfaceFaceRange &surface_range = p_surface_face_ranges[surface_index];
			if (surface_range.face_count <= 0) {
				continue;
			}

			const Ref<Material> surface_material = p_mesh_instance->get_active_material(surface_index);
			if (surface_material.is_null()) {
				continue;
			}

			VtIntArray subset_faces = make_subset_faces(surface_range);

			const Dictionary description = get_surface_description(surface_index);
			const String subset_path_string = description.get("subset_path", String());
			const String subset_name = description.get("subset_name", vformat("Surface_%d", surface_index));
			UsdGeomSubset subset = _define_preserved_subset(p_usd_mesh, subset_path_string, subset_name, UsdGeomTokens->face, subset_faces, UsdShadeTokens->materialBind, UsdGeomTokens->nonOverlapping);

			const String preferred_material_path = description.get("material_path", String());
			const UsdShadeMaterial usd_material = resolve_usd_material(surface_material, preferred_material_path);
			if (usd_material) {
				UsdShadeMaterialBindingAPI::Apply(subset.GetPrim()).Bind(usd_material);
			}
		}
	}

	static void _write_preserved_geom_subsets(MeshInstance3D *p_mesh_instance, const UsdGeomMesh &p_usd_mesh, const Vector<UsdMeshSurfaceFaceRange> &p_surface_face_ranges) {
		ERR_FAIL_NULL(p_mesh_instance);
		if (!p_usd_mesh) {
			return;
		}

		const Dictionary usd_metadata = _get_usd_metadata(p_mesh_instance);
		const Array preserved_subsets = usd_metadata.get("usd:geom_subsets", Array());
		if (preserved_subsets.is_empty()) {
			return;
		}

		const Ref<Mesh> mesh = p_mesh_instance->get_mesh();
		if (mesh.is_null() || mesh->get_surface_count() != p_surface_face_ranges.size()) {
			return;
		}

		const Array surface_descriptions = usd_metadata.get("usd:material_subsets", Array());
		if (surface_descriptions.size() != mesh->get_surface_count()) {
			return;
		}

		HashMap<int, int> authored_to_saved_face_index;
		HashMap<int, Vector<int>> authored_to_saved_point_indices;
		HashMap<uint64_t, Vector<Vector2i>> authored_to_saved_edges;
		for (int surface_index = 0; surface_index < mesh->get_surface_count(); surface_index++) {
			if (surface_descriptions[surface_index].get_type() != Variant::DICTIONARY) {
				continue;
			}

			const Dictionary surface_description = surface_descriptions[surface_index];
			const PackedInt32Array authored_face_indices = surface_description.get("authored_face_indices", PackedInt32Array());
			const PackedInt32Array authored_point_indices = surface_description.get("authored_point_indices", PackedInt32Array());
			const Vector<int> &saved_face_indices = p_surface_face_ranges[surface_index].saved_face_indices;
			if (authored_face_indices.size() != saved_face_indices.size()) {
				continue;
			}

			for (int i = 0; i < authored_face_indices.size(); i++) {
				authored_to_saved_face_index.insert(authored_face_indices[i], saved_face_indices[i]);
			}

			const HashMap<int, Vector<int>> &surface_point_map = p_surface_face_ranges[surface_index].saved_point_indices_by_authored_point;
			for (const KeyValue<int, Vector<int>> &entry : surface_point_map) {
				Vector<int> *saved_points = authored_to_saved_point_indices.getptr(entry.key);
				if (saved_points == nullptr) {
					authored_to_saved_point_indices.insert(entry.key, Vector<int>());
					saved_points = authored_to_saved_point_indices.getptr(entry.key);
				}
				for (int i = 0; i < entry.value.size(); i++) {
					saved_points->push_back(entry.value[i]);
				}
			}

			if (authored_point_indices.is_empty()) {
				continue;
			}

			const Array arrays = mesh->surface_get_arrays(surface_index);
			if (arrays.size() != Mesh::ARRAY_MAX) {
				continue;
			}
			const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
			if (vertices.is_empty() || authored_point_indices.size() != vertices.size()) {
				continue;
			}

			const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
			int vertex_offset = 0;
			for (int prior_surface = 0; prior_surface < surface_index; prior_surface++) {
				const Array prior_arrays = mesh->surface_get_arrays(prior_surface);
				if (prior_arrays.size() == Mesh::ARRAY_MAX) {
					vertex_offset += ((PackedVector3Array)prior_arrays[Mesh::ARRAY_VERTEX]).size();
				}
			}

			auto record_edge = [&](int p_local_a, int p_local_b) {
				const int authored_a = authored_point_indices[p_local_a];
				const int authored_b = authored_point_indices[p_local_b];
				const uint64_t authored_edge_key = _make_sorted_pair_key(authored_a, authored_b);
				Vector<Vector2i> *saved_edges = authored_to_saved_edges.getptr(authored_edge_key);
				if (saved_edges == nullptr) {
					authored_to_saved_edges.insert(authored_edge_key, Vector<Vector2i>());
					saved_edges = authored_to_saved_edges.getptr(authored_edge_key);
				}

				const int saved_a = vertex_offset + p_local_a;
				const int saved_b = vertex_offset + p_local_b;
				const Vector2i saved_edge(MIN(saved_a, saved_b), MAX(saved_a, saved_b));
				for (int i = 0; i < saved_edges->size(); i++) {
					if ((*saved_edges)[i] == saved_edge) {
						return;
					}
				}
				saved_edges->push_back(saved_edge);
			};

			if (!indices.is_empty()) {
				for (int i = 0; i + 2 < indices.size(); i += 3) {
					record_edge(indices[i], indices[i + 1]);
					record_edge(indices[i + 1], indices[i + 2]);
					record_edge(indices[i + 2], indices[i]);
				}
			} else {
				for (int i = 0; i + 2 < vertices.size(); i += 3) {
					record_edge(i, i + 1);
					record_edge(i + 1, i + 2);
					record_edge(i + 2, i);
				}
			}
		}

		for (int subset_index = 0; subset_index < preserved_subsets.size(); subset_index++) {
			if (preserved_subsets[subset_index].get_type() != Variant::DICTIONARY) {
				continue;
			}

			const Dictionary subset_description = preserved_subsets[subset_index];
			const String element_type_string = subset_description.get("element_type", String());
			if (element_type_string != "face" && element_type_string != "point" && element_type_string != "edge") {
				continue;
			}

			const PackedInt32Array authored_indices = subset_description.get("indices", PackedInt32Array());
			if (authored_indices.is_empty()) {
				continue;
			}

			const String subset_path_string = subset_description.get("subset_path", String());
			const String subset_name = subset_description.get("subset_name", vformat("GeomSubset_%d", subset_index));
			const String family_name_string = subset_description.get("family_name", String());
			const String family_type_string = subset_description.get("family_type", String("nonOverlapping"));
			const TfToken family_name = family_name_string.is_empty() ? TfToken() : TfToken(family_name_string.utf8().get_data());
			const TfToken family_type = family_type_string.is_empty() ? UsdGeomTokens->nonOverlapping : TfToken(family_type_string.utf8().get_data());
			const TfToken element_type(element_type_string.utf8().get_data());

			VtIntArray subset_indices;
			if (element_type_string == "face") {
				subset_indices.reserve(authored_indices.size());
				bool complete_mapping = true;
				for (int i = 0; i < authored_indices.size(); i++) {
					if (!authored_to_saved_face_index.has(authored_indices[i])) {
						complete_mapping = false;
						break;
					}
					subset_indices.push_back(authored_to_saved_face_index[authored_indices[i]]);
				}
				if (!complete_mapping || subset_indices.empty()) {
					continue;
				}
			} else if (element_type_string == "point") {
				HashSet<int> emitted_points;
				for (int i = 0; i < authored_indices.size(); i++) {
					const Vector<int> *saved_points = authored_to_saved_point_indices.getptr(authored_indices[i]);
					if (saved_points == nullptr) {
						continue;
					}
					for (int point_index = 0; point_index < saved_points->size(); point_index++) {
						if (emitted_points.has((*saved_points)[point_index])) {
							continue;
						}
						emitted_points.insert((*saved_points)[point_index]);
						subset_indices.push_back((*saved_points)[point_index]);
					}
				}
				if (subset_indices.empty()) {
					continue;
				}
				std::sort(subset_indices.begin(), subset_indices.end());
			} else {
				if (authored_indices.size() % 2 != 0) {
					continue;
				}

				HashSet<uint64_t> emitted_edges;
				for (int i = 0; i + 1 < authored_indices.size(); i += 2) {
					const uint64_t authored_edge_key = _make_sorted_pair_key(authored_indices[i], authored_indices[i + 1]);
					const Vector<Vector2i> *saved_edges = authored_to_saved_edges.getptr(authored_edge_key);
					if (saved_edges == nullptr) {
						continue;
					}
					for (int edge_index = 0; edge_index < saved_edges->size(); edge_index++) {
						const Vector2i &saved_edge = (*saved_edges)[edge_index];
						const uint64_t saved_edge_key = _make_sorted_pair_key(saved_edge.x, saved_edge.y);
						if (emitted_edges.has(saved_edge_key)) {
							continue;
						}
						emitted_edges.insert(saved_edge_key);
						subset_indices.push_back(saved_edge.x);
						subset_indices.push_back(saved_edge.y);
					}
				}
				if (subset_indices.empty()) {
					continue;
				}
			}

			_define_preserved_subset(p_usd_mesh, subset_path_string, subset_name, element_type, subset_indices, family_name, family_type);
		}
	}

	struct UsdPendingFace {
		int authored_face_index = -1;
		int insertion_index = -1;
		int surface_index = -1;
		int32_t i0 = 0;
		int32_t i1 = 0;
		int32_t i2 = 0;
	};

	struct UsdPendingFaceComparator {
		_FORCE_INLINE_ bool operator()(const UsdPendingFace &p_a, const UsdPendingFace &p_b) const {
			if (p_a.authored_face_index == p_b.authored_face_index) {
				return p_a.insertion_index < p_b.insertion_index;
			}
			return p_a.authored_face_index < p_b.authored_face_index;
		}
	};

	static bool _write_mesh_geometry(const Ref<Mesh> &p_mesh, UsdGeomMesh p_usd_mesh, const Dictionary *p_usd_metadata = nullptr, Vector<UsdMeshSurfaceFaceRange> *r_surface_face_ranges = nullptr) {
		ERR_FAIL_COND_V(p_mesh.is_null(), false);

		VtArray<GfVec3f> points;
		VtArray<int> face_vertex_counts;
		VtArray<int> face_vertex_indices;
		VtArray<GfVec3f> normals;
		VtArray<GfVec2f> uvs;
		Vector<UsdPendingFace> pending_faces;
		bool have_normals = true;
		bool have_uvs = true;

		if (r_surface_face_ranges != nullptr) {
			r_surface_face_ranges->resize(p_mesh->get_surface_count());
		}

		Array surface_descriptions;
		bool has_surface_descriptions = false;
		if (p_usd_metadata != nullptr) {
			surface_descriptions = p_usd_metadata->get("usd:material_subsets", Array());
			has_surface_descriptions = surface_descriptions.size() == p_mesh->get_surface_count();
		}

		for (int surface_index = 0; surface_index < p_mesh->get_surface_count(); surface_index++) {
			const Array arrays = p_mesh->surface_get_arrays(surface_index);
			if (arrays.size() != Mesh::ARRAY_MAX) {
				continue;
			}

			const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
			if (vertices.is_empty()) {
				continue;
			}

			const PackedVector3Array surface_normals = arrays[Mesh::ARRAY_NORMAL];
			const PackedVector2Array surface_uvs = arrays[Mesh::ARRAY_TEX_UV];
			const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
			PackedInt32Array authored_face_indices;
			PackedInt32Array authored_point_indices;
			if (has_surface_descriptions && surface_descriptions[surface_index].get_type() == Variant::DICTIONARY) {
				const Dictionary surface_description = surface_descriptions[surface_index];
				authored_face_indices = surface_description.get("authored_face_indices", PackedInt32Array());
				authored_point_indices = surface_description.get("authored_point_indices", PackedInt32Array());
			}
			const int32_t vertex_offset = (int32_t)points.size();
			int emitted_faces = 0;

			for (int i = 0; i < vertices.size(); i++) {
				const Vector3 vertex = vertices[i];
				points.push_back(GfVec3f(vertex.x, vertex.y, vertex.z));
				if (r_surface_face_ranges != nullptr && authored_point_indices.size() == vertices.size()) {
					UsdMeshSurfaceFaceRange &range = r_surface_face_ranges->write[surface_index];
					Vector<int> *saved_points = range.saved_point_indices_by_authored_point.getptr(authored_point_indices[i]);
					if (saved_points == nullptr) {
						range.saved_point_indices_by_authored_point.insert(authored_point_indices[i], Vector<int>());
						saved_points = range.saved_point_indices_by_authored_point.getptr(authored_point_indices[i]);
					}
					saved_points->push_back(vertex_offset + i);
				}
			}

			if (have_normals && surface_normals.size() == vertices.size()) {
				for (int i = 0; i < surface_normals.size(); i++) {
					const Vector3 normal = surface_normals[i];
					normals.push_back(GfVec3f(normal.x, normal.y, normal.z));
				}
			} else {
				have_normals = false;
			}

			if (have_uvs && surface_uvs.size() == vertices.size()) {
				for (int i = 0; i < surface_uvs.size(); i++) {
					const Vector2 uv = surface_uvs[i];
					uvs.push_back(GfVec2f(uv.x, uv.y));
				}
			} else {
				have_uvs = false;
			}

			if (!indices.is_empty()) {
				for (int i = 0; i + 2 < indices.size(); i += 3) {
					UsdPendingFace pending_face;
					pending_face.surface_index = surface_index;
					pending_face.insertion_index = pending_faces.size();
					pending_face.authored_face_index = emitted_faces < authored_face_indices.size() ? authored_face_indices[emitted_faces] : -1;
					pending_face.i0 = vertex_offset + indices[i];
					pending_face.i1 = vertex_offset + indices[i + 2];
					pending_face.i2 = vertex_offset + indices[i + 1];
					pending_faces.push_back(pending_face);
					emitted_faces++;
				}
			} else {
				for (int i = 0; i + 2 < vertices.size(); i += 3) {
					UsdPendingFace pending_face;
					pending_face.surface_index = surface_index;
					pending_face.insertion_index = pending_faces.size();
					pending_face.authored_face_index = emitted_faces < authored_face_indices.size() ? authored_face_indices[emitted_faces] : -1;
					pending_face.i0 = vertex_offset + i;
					pending_face.i1 = vertex_offset + i + 2;
					pending_face.i2 = vertex_offset + i + 1;
					pending_faces.push_back(pending_face);
					emitted_faces++;
				}
			}

			if (r_surface_face_ranges != nullptr) {
				UsdMeshSurfaceFaceRange &range = r_surface_face_ranges->write[surface_index];
				range.face_count = emitted_faces;
				range.saved_face_indices.clear();
			}
		}

		if (points.empty() || pending_faces.is_empty()) {
			return false;
		}

		bool preserve_authored_face_order = true;
		for (int i = 0; i < pending_faces.size(); i++) {
			if (pending_faces[i].authored_face_index < 0) {
				preserve_authored_face_order = false;
				break;
			}
		}
		if (preserve_authored_face_order) {
			pending_faces.sort_custom<UsdPendingFaceComparator>();
		}

		for (int face_index = 0; face_index < pending_faces.size(); face_index++) {
			const UsdPendingFace &pending_face = pending_faces[face_index];
			face_vertex_counts.push_back(3);
			face_vertex_indices.push_back(pending_face.i0);
			face_vertex_indices.push_back(pending_face.i1);
			face_vertex_indices.push_back(pending_face.i2);
			if (r_surface_face_ranges != nullptr && pending_face.surface_index >= 0 && pending_face.surface_index < r_surface_face_ranges->size()) {
				r_surface_face_ranges->write[pending_face.surface_index].saved_face_indices.push_back(face_index);
			}
		}

		if (r_surface_face_ranges != nullptr) {
			for (int surface_index = 0; surface_index < r_surface_face_ranges->size(); surface_index++) {
				UsdMeshSurfaceFaceRange &range = r_surface_face_ranges->write[surface_index];
				range.face_count = range.saved_face_indices.size();
				range.face_start = range.face_count > 0 ? range.saved_face_indices[0] : 0;
			}
		}

		p_usd_mesh.CreatePointsAttr().Set(points);
		p_usd_mesh.CreateFaceVertexCountsAttr().Set(face_vertex_counts);
		p_usd_mesh.CreateFaceVertexIndicesAttr().Set(face_vertex_indices);
		p_usd_mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
		p_usd_mesh.CreateOrientationAttr().Set(UsdGeomTokens->rightHanded);

		if (have_normals && normals.size() == points.size()) {
			p_usd_mesh.CreateNormalsAttr().Set(normals);
			p_usd_mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
		}

		if (have_uvs && uvs.size() == points.size()) {
			UsdGeomPrimvarsAPI primvars_api(p_usd_mesh);
			UsdGeomPrimvar st = primvars_api.CreatePrimvar(TfToken("st"), SdfValueTypeNames->TexCoord2fArray, UsdGeomTokens->vertex);
			st.Set(uvs);
		}

		return true;
	}

	static Node *_find_node_for_source_prim_path(Node *p_root, const String &p_prim_path) {
		ERR_FAIL_NULL_V(p_root, nullptr);
		const Dictionary metadata = _get_usd_metadata(p_root);
		if ((String)metadata.get("usd:prim_path", String()) == p_prim_path) {
			return p_root;
		}
		for (int i = 0; i < p_root->get_child_count(); i++) {
			if (Node *found = _find_node_for_source_prim_path(p_root->get_child(i), p_prim_path)) {
				return found;
			}
		}
		return nullptr;
	}

	static SdfPath _get_saved_path_for_node(const HashMap<ObjectID, SdfPath> &p_saved_paths, const Node *p_node) {
		ERR_FAIL_NULL_V(p_node, SdfPath());
		const SdfPath *saved_path = p_saved_paths.getptr(p_node->get_instance_id());
		return saved_path != nullptr ? *saved_path : SdfPath();
	}

	static String _get_skeleton_joint_path(const Skeleton3D *p_skeleton, int p_bone_index) {
		ERR_FAIL_NULL_V(p_skeleton, String());
		if (p_skeleton->has_bone_meta(p_bone_index, StringName("usd_joint_path"))) {
			return p_skeleton->get_bone_meta(p_bone_index, StringName("usd_joint_path"));
		}
		const int parent_index = p_skeleton->get_bone_parent(p_bone_index);
		if (parent_index < 0) {
			return p_skeleton->get_bone_name(p_bone_index);
		}
		return _get_skeleton_joint_path(p_skeleton, parent_index) + "/" + p_skeleton->get_bone_name(p_bone_index);
	}

	static Transform3D _compute_skeleton_world_rest(const Skeleton3D *p_skeleton, int p_bone_index) {
		ERR_FAIL_NULL_V(p_skeleton, Transform3D());
		const int parent_index = p_skeleton->get_bone_parent(p_bone_index);
		if (parent_index < 0) {
			return p_skeleton->get_bone_rest(p_bone_index);
		}
		return _compute_skeleton_world_rest(p_skeleton, parent_index) * p_skeleton->get_bone_rest(p_bone_index);
	}

	static void _write_skeleton_prim(const Skeleton3D *p_skeleton, UsdSkelSkeleton p_usd_skeleton) {
		ERR_FAIL_NULL(p_skeleton);
		if (!p_usd_skeleton) {
			return;
		}

		VtArray<TfToken> joints;
		VtArray<TfToken> joint_names;
		VtArray<GfMatrix4d> rest_transforms;
		VtArray<GfMatrix4d> bind_transforms;
		joints.resize(p_skeleton->get_bone_count());
		joint_names.resize(p_skeleton->get_bone_count());
		rest_transforms.resize(p_skeleton->get_bone_count());
		bind_transforms.resize(p_skeleton->get_bone_count());

		for (int bone_index = 0; bone_index < p_skeleton->get_bone_count(); bone_index++) {
			const String joint_path = _get_skeleton_joint_path(p_skeleton, bone_index);
			joints[bone_index] = TfToken(joint_path.utf8().get_data());
			joint_names[bone_index] = TfToken(String(p_skeleton->get_bone_name(bone_index)).utf8().get_data());
			rest_transforms[bone_index] = _transform_to_gf_matrix(p_skeleton->get_bone_rest(bone_index));

			Transform3D bind_transform = _compute_skeleton_world_rest(p_skeleton, bone_index);
			if (p_skeleton->has_bone_meta(bone_index, StringName("usd_joint_bind_transform"))) {
				const Variant bind_variant = p_skeleton->get_bone_meta(bone_index, StringName("usd_joint_bind_transform"));
				if (bind_variant.get_type() == Variant::TRANSFORM3D) {
					bind_transform = bind_variant;
				}
			}
			bind_transforms[bone_index] = _transform_to_gf_matrix(bind_transform);
		}

		p_usd_skeleton.CreateJointsAttr().Set(joints);
		p_usd_skeleton.CreateJointNamesAttr().Set(joint_names);
		p_usd_skeleton.CreateRestTransformsAttr().Set(rest_transforms);
		p_usd_skeleton.CreateBindTransformsAttr().Set(bind_transforms);
	}

	static bool _write_mesh_skinning_and_blend_shapes(const UsdStageRefPtr &p_stage, Node *p_scene_root, MeshInstance3D *p_mesh_instance, const UsdGeomMesh &p_usd_mesh, const SdfPath &p_mesh_path, const HashMap<ObjectID, SdfPath> &p_saved_paths) {
		ERR_FAIL_NULL_V(p_scene_root, false);
		ERR_FAIL_NULL_V(p_mesh_instance, false);
		if (!p_usd_mesh) {
			return false;
		}

		const Ref<Mesh> mesh = p_mesh_instance->get_mesh();
		if (mesh.is_null()) {
			return false;
		}

		const Dictionary metadata = _get_usd_metadata(p_mesh_instance);
		const String skeleton_source_prim_path = metadata.get("usd:skel_skeleton_path", String());
		UsdSkelBindingAPI binding_api = UsdSkelBindingAPI::Apply(p_usd_mesh.GetPrim());
		if (!binding_api) {
			return false;
		}

		if (!skeleton_source_prim_path.is_empty()) {
			Node *skeleton_node = _find_node_for_source_prim_path(p_scene_root, skeleton_source_prim_path);
			if (skeleton_node != nullptr) {
				const SdfPath skeleton_saved_path = _get_saved_path_for_node(p_saved_paths, skeleton_node);
				if (!skeleton_saved_path.IsEmpty()) {
					SdfPathVector skeleton_targets;
					skeleton_targets.push_back(skeleton_saved_path);
					binding_api.CreateSkeletonRel().SetTargets(skeleton_targets);
				}
			}
		}

		const Variant geom_bind_variant = metadata.get("usd:skel_geom_bind_transform", Variant());
		if (geom_bind_variant.get_type() == Variant::TRANSFORM3D) {
			binding_api.CreateGeomBindTransformAttr().Set(_transform_to_gf_matrix((Transform3D)geom_bind_variant));
		}

		VtArray<int> joint_indices_values;
		VtArray<float> joint_weights_values;
		bool have_skinning = true;
		for (int surface_index = 0; surface_index < mesh->get_surface_count(); surface_index++) {
			const Array arrays = mesh->surface_get_arrays(surface_index);
			if (arrays.size() != Mesh::ARRAY_MAX) {
				continue;
			}
			const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
			if (vertices.is_empty()) {
				continue;
			}
			const PackedInt32Array bones = arrays[Mesh::ARRAY_BONES];
			const PackedFloat32Array weights = arrays[Mesh::ARRAY_WEIGHTS];
			if (bones.size() != vertices.size() * 4 || weights.size() != vertices.size() * 4) {
				have_skinning = false;
				break;
			}
			for (int i = 0; i < bones.size(); i++) {
				joint_indices_values.push_back(bones[i]);
				joint_weights_values.push_back(weights[i]);
			}
		}
		if (have_skinning && !joint_indices_values.empty() && joint_indices_values.size() == joint_weights_values.size()) {
			UsdGeomPrimvar joint_indices = binding_api.CreateJointIndicesPrimvar(false, 4);
			UsdGeomPrimvar joint_weights = binding_api.CreateJointWeightsPrimvar(false, 4);
			joint_indices.Set(joint_indices_values);
			joint_weights.Set(joint_weights_values);
		}

		const Dictionary blend_shape_channels = metadata.get("usd:blend_shape_channels", Dictionary());
		const Array blend_shape_names = metadata.get("usd:blend_shape_names", Array());
		const Array blend_shape_targets = metadata.get("usd:blend_shape_targets", Array());
		if (blend_shape_channels.is_empty() || blend_shape_names.is_empty()) {
			return true;
		}

		const int mesh_blend_shape_count = mesh->get_blend_shape_count();
		Vector<VtArray<GfVec3f>> blend_shape_offsets;
		Vector<VtArray<GfVec3f>> blend_shape_normal_offsets;
		blend_shape_offsets.resize(mesh_blend_shape_count);
		blend_shape_normal_offsets.resize(mesh_blend_shape_count);

		for (int surface_index = 0; surface_index < mesh->get_surface_count(); surface_index++) {
			const Array arrays = mesh->surface_get_arrays(surface_index);
			if (arrays.size() != Mesh::ARRAY_MAX) {
				continue;
			}
			const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
			if (vertices.is_empty()) {
				continue;
			}

			const TypedArray<Array> surface_blend_shapes = mesh->surface_get_blend_shape_arrays(surface_index);
			if (surface_blend_shapes.size() != mesh_blend_shape_count) {
				continue;
			}

			for (int blend_shape_index = 0; blend_shape_index < mesh_blend_shape_count; blend_shape_index++) {
				const Array blend_shape_surface = surface_blend_shapes[blend_shape_index];
				if (blend_shape_surface.size() != Mesh::ARRAY_MAX) {
					continue;
				}
				const PackedVector3Array blend_shape_vertices = blend_shape_surface[Mesh::ARRAY_VERTEX];
				const Variant normals_variant = blend_shape_surface[Mesh::ARRAY_NORMAL];
				const PackedVector3Array blend_shape_normals = normals_variant.get_type() == Variant::PACKED_VECTOR3_ARRAY ? (PackedVector3Array)normals_variant : PackedVector3Array();
				for (int vertex_index = 0; vertex_index < vertices.size(); vertex_index++) {
					const Vector3 vertex_delta = vertex_index < blend_shape_vertices.size() ? blend_shape_vertices[vertex_index] : Vector3();
					blend_shape_offsets.write[blend_shape_index].push_back(GfVec3f(vertex_delta.x, vertex_delta.y, vertex_delta.z));
					const Vector3 normal_delta = vertex_index < blend_shape_normals.size() ? blend_shape_normals[vertex_index] : Vector3();
					blend_shape_normal_offsets.write[blend_shape_index].push_back(GfVec3f(normal_delta.x, normal_delta.y, normal_delta.z));
				}
			}
		}

		VtArray<TfToken> saved_blend_shape_names;
		SdfPathVector saved_blend_shape_targets;
		for (int blend_shape_name_index = 0; blend_shape_name_index < blend_shape_names.size(); blend_shape_name_index++) {
			const String primary_name = blend_shape_names[blend_shape_name_index];
			const int primary_channel_index = p_mesh_instance->find_blend_shape_by_name(StringName(primary_name));
			if (primary_channel_index < 0 || primary_channel_index >= blend_shape_offsets.size()) {
				continue;
			}

			SdfPath blend_shape_path = p_mesh_path.AppendChild(TfToken(_make_valid_identifier(primary_name).utf8().get_data()));
			if (blend_shape_name_index < blend_shape_targets.size()) {
				const String preferred_target_path = blend_shape_targets[blend_shape_name_index];
				const SdfPath preferred_path(preferred_target_path.utf8().get_data());
				if (preferred_path.IsAbsolutePath() && preferred_path.GetParentPath() == p_mesh_path) {
					blend_shape_path = preferred_path;
				}
			}

			UsdSkelBlendShape usd_blend_shape = UsdSkelBlendShape::Define(p_stage, blend_shape_path);
			usd_blend_shape.CreateOffsetsAttr().Set(blend_shape_offsets[primary_channel_index]);
			VtArray<int> point_indices;
			point_indices.resize(blend_shape_offsets[primary_channel_index].size());
			for (size_t point_index = 0; point_index < point_indices.size(); point_index++) {
				point_indices[point_index] = (int)point_index;
			}
			usd_blend_shape.CreatePointIndicesAttr().Set(point_indices);

			bool has_primary_normals = false;
			for (size_t delta_index = 0; delta_index < blend_shape_normal_offsets[primary_channel_index].size(); delta_index++) {
				if (blend_shape_normal_offsets[primary_channel_index][delta_index] != GfVec3f(0.0f)) {
					has_primary_normals = true;
					break;
				}
			}
			if (has_primary_normals) {
				usd_blend_shape.CreateNormalOffsetsAttr().Set(blend_shape_normal_offsets[primary_channel_index]);
			}

			const Array channel_entries = blend_shape_channels.get(primary_name, Array());
			for (int channel_entry_index = 0; channel_entry_index < channel_entries.size(); channel_entry_index++) {
				if (channel_entries[channel_entry_index].get_type() != Variant::DICTIONARY) {
					continue;
				}
				const Dictionary channel_entry = channel_entries[channel_entry_index];
				if ((bool)channel_entry.get("primary", false)) {
					continue;
				}

				const String channel_name = channel_entry.get("channel_name", String());
				const String inbetween_name = channel_entry.get("name", channel_name);
				const int channel_index = p_mesh_instance->find_blend_shape_by_name(StringName(channel_name));
				if (channel_index < 0 || channel_index >= blend_shape_offsets.size()) {
					continue;
				}

				UsdSkelInbetweenShape inbetween = usd_blend_shape.CreateInbetween(TfToken(_make_valid_identifier(inbetween_name).utf8().get_data()));
				if (!inbetween) {
					continue;
				}
				inbetween.SetWeight((float)(double)channel_entry.get("weight", 0.0));
				inbetween.SetOffsets(blend_shape_offsets[channel_index]);

				bool has_inbetween_normals = false;
				for (size_t delta_index = 0; delta_index < blend_shape_normal_offsets[channel_index].size(); delta_index++) {
					if (blend_shape_normal_offsets[channel_index][delta_index] != GfVec3f(0.0f)) {
						has_inbetween_normals = true;
						break;
					}
				}
				if (has_inbetween_normals) {
					inbetween.SetNormalOffsets(blend_shape_normal_offsets[channel_index]);
				}
			}

			saved_blend_shape_names.push_back(TfToken(primary_name.utf8().get_data()));
			saved_blend_shape_targets.push_back(blend_shape_path);
		}

		if (!saved_blend_shape_names.empty()) {
			binding_api.CreateBlendShapesAttr().Set(saved_blend_shape_names);
			binding_api.CreateBlendShapeTargetsRel().SetTargets(saved_blend_shape_targets);
		}

		return true;
	}

	static void _collect_animation_players(Node *p_node, Vector<AnimationPlayer *> *r_players) {
		ERR_FAIL_NULL(p_node);
		ERR_FAIL_NULL(r_players);
		if (AnimationPlayer *player = Object::cast_to<AnimationPlayer>(p_node)) {
			r_players->push_back(player);
		}
		for (int i = 0; i < p_node->get_child_count(); i++) {
			_collect_animation_players(p_node->get_child(i), r_players);
		}
	}

	static void _collect_bound_blend_shape_meshes(Node *p_node, const String &p_skeleton_source_prim_path, const HashMap<ObjectID, SdfPath> &p_saved_paths, Vector<MeshInstance3D *> *r_meshes) {
		ERR_FAIL_NULL(p_node);
		ERR_FAIL_NULL(r_meshes);
		if (MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node)) {
			const Dictionary metadata = _get_usd_metadata(mesh_instance);
			if ((String)metadata.get("usd:skel_skeleton_path", String()) == p_skeleton_source_prim_path &&
					!_get_saved_path_for_node(p_saved_paths, mesh_instance).IsEmpty() &&
					((Dictionary)metadata.get("usd:blend_shape_channels", Dictionary())).size() > 0) {
				r_meshes->push_back(mesh_instance);
			}
		}
		for (int i = 0; i < p_node->get_child_count(); i++) {
			_collect_bound_blend_shape_meshes(p_node->get_child(i), p_skeleton_source_prim_path, p_saved_paths, r_meshes);
		}
	}

	static Vector<UsdBlendShapeChannelSpec> _parse_saved_blend_shape_channel_specs(const Array &p_channel_entries) {
		Vector<UsdBlendShapeChannelSpec> specs;
		for (int i = 0; i < p_channel_entries.size(); i++) {
			if (p_channel_entries[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary entry = p_channel_entries[i];
			const String channel_name = entry.get("channel_name", String());
			if (channel_name.is_empty()) {
				continue;
			}
			UsdBlendShapeChannelSpec spec;
			spec.channel_name = channel_name;
			spec.weight = MAX((float)(double)entry.get("weight", 1.0), 0.0001f);
			spec.primary = (bool)entry.get("primary", false);
			specs.push_back(spec);
		}

		struct ChannelComparator {
			_FORCE_INLINE_ bool operator()(const UsdBlendShapeChannelSpec &p_a, const UsdBlendShapeChannelSpec &p_b) const {
				if (Math::is_equal_approx(p_a.weight, p_b.weight)) {
					if (p_a.primary != p_b.primary) {
						return !p_a.primary && p_b.primary;
					}
					return p_a.channel_name < p_b.channel_name;
				}
				return p_a.weight < p_b.weight;
			}
		};

		specs.sort_custom<ChannelComparator>();
		return specs;
	}

	struct SavedSkeletonJointAnimationTarget {
		int bone_index = -1;
		String joint_path;
		Vector3 rest_position;
		Quaternion rest_rotation;
		Vector3 rest_scale = Vector3(1.0f, 1.0f, 1.0f);
		int position_track = -1;
		int rotation_track = -1;
		int scale_track = -1;
	};

	struct SavedBlendShapeAnimationTarget {
		String primary_name;
		Vector<UsdBlendShapeChannelSpec> channel_specs;
		Vector<int> track_indices;
	};

	static void _append_unique_track_key_times(const Ref<Animation> &p_animation, int p_track_index, Vector<double> *r_sample_times) {
		ERR_FAIL_NULL(r_sample_times);
		if (p_animation.is_null() || p_track_index < 0) {
			return;
		}
		for (int key_index = 0; key_index < p_animation->track_get_key_count(p_track_index); key_index++) {
			r_sample_times->push_back(p_animation->track_get_key_time(p_track_index, key_index));
		}
	}

	static void _sort_dedupe_sample_times(Vector<double> *r_sample_times) {
		ERR_FAIL_NULL(r_sample_times);
		if (r_sample_times->is_empty()) {
			return;
		}
		r_sample_times->sort();
		for (int sample_index = r_sample_times->size() - 1; sample_index > 0; sample_index--) {
			if (Math::is_equal_approx((*r_sample_times)[sample_index], (*r_sample_times)[sample_index - 1])) {
				r_sample_times->remove_at(sample_index);
			}
		}
	}

	static void _write_saved_skeleton_animations(const UsdStageRefPtr &p_stage, Node *p_scene_root, const HashMap<ObjectID, SdfPath> &p_saved_paths) {
		ERR_FAIL_NULL(p_scene_root);
		Vector<AnimationPlayer *> animation_players;
		_collect_animation_players(p_scene_root, &animation_players);
		if (animation_players.is_empty()) {
			return;
		}

		List<Node *> stack;
		stack.push_back(p_scene_root);
		while (!stack.is_empty()) {
			Node *node = stack.front()->get();
			stack.pop_front();

			for (int child_index = 0; child_index < node->get_child_count(); child_index++) {
				stack.push_back(node->get_child(child_index));
			}

			Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(node);
			if (skeleton == nullptr) {
				continue;
			}

			const Dictionary skeleton_metadata = _get_usd_metadata(skeleton);
			const String skeleton_source_prim_path = skeleton_metadata.get("usd:prim_path", String());
			const SdfPath skeleton_saved_path = _get_saved_path_for_node(p_saved_paths, skeleton);
			if (skeleton_source_prim_path.is_empty() || skeleton_saved_path.IsEmpty()) {
				continue;
			}

			Vector<MeshInstance3D *> bound_meshes;
			_collect_bound_blend_shape_meshes(p_scene_root, skeleton_source_prim_path, p_saved_paths, &bound_meshes);

			for (int player_index = 0; player_index < animation_players.size(); player_index++) {
				AnimationPlayer *player = animation_players[player_index];
				LocalVector<StringName> animation_names;
				player->get_animation_list(&animation_names);
				for (uint32_t animation_name_index = 0; animation_name_index < animation_names.size(); animation_name_index++) {
					const StringName animation_name_sname = animation_names[animation_name_index];
					Ref<Animation> animation = player->get_animation(animation_name_sname);
					if (animation.is_null()) {
						continue;
					}
					const Dictionary animation_metadata = _get_usd_metadata(animation.ptr());

					const String skeleton_path = String(p_scene_root->get_path_to(skeleton));
					HashMap<String, int> bone_index_by_joint_path;
					for (int bone_index = 0; bone_index < skeleton->get_bone_count(); bone_index++) {
						bone_index_by_joint_path.insert(_get_skeleton_joint_path(skeleton, bone_index), bone_index);
					}

					Vector<SavedSkeletonJointAnimationTarget> joint_targets;
					const Array metadata_joint_paths = animation_metadata.get("usd:joint_paths", Array());
					if (!metadata_joint_paths.is_empty()) {
						for (int joint_index = 0; joint_index < metadata_joint_paths.size(); joint_index++) {
							const String joint_path = metadata_joint_paths[joint_index];
							const int *bone_index_ptr = bone_index_by_joint_path.getptr(joint_path);
							if (bone_index_ptr == nullptr) {
								continue;
							}

							const int bone_index = *bone_index_ptr;
							SavedSkeletonJointAnimationTarget joint_target;
							joint_target.bone_index = bone_index;
							joint_target.joint_path = joint_path;
							const Transform3D bone_rest = skeleton->get_bone_rest(bone_index);
							joint_target.rest_position = bone_rest.origin;
							joint_target.rest_rotation = bone_rest.basis.get_rotation_quaternion();
							joint_target.rest_scale = bone_rest.basis.get_scale();

							const String track_path = skeleton_path + ":" + skeleton->get_bone_name(bone_index);
							for (int track_index = 0; track_index < animation->get_track_count(); track_index++) {
								if (String(animation->track_get_path(track_index)) != track_path) {
									continue;
								}
								switch (animation->track_get_type(track_index)) {
									case Animation::TYPE_POSITION_3D:
										joint_target.position_track = track_index;
										break;
									case Animation::TYPE_ROTATION_3D:
										joint_target.rotation_track = track_index;
										break;
									case Animation::TYPE_SCALE_3D:
										joint_target.scale_track = track_index;
										break;
									default:
										break;
								}
							}
							joint_targets.push_back(joint_target);
						}
					} else {
						for (int bone_index = 0; bone_index < skeleton->get_bone_count(); bone_index++) {
						SavedSkeletonJointAnimationTarget joint_target;
						joint_target.bone_index = bone_index;
						joint_target.joint_path = _get_skeleton_joint_path(skeleton, bone_index);
						const Transform3D bone_rest = skeleton->get_bone_rest(bone_index);
						joint_target.rest_position = bone_rest.origin;
						joint_target.rest_rotation = bone_rest.basis.get_rotation_quaternion();
						joint_target.rest_scale = bone_rest.basis.get_scale();

						const String track_path = skeleton_path + ":" + skeleton->get_bone_name(bone_index);
						for (int track_index = 0; track_index < animation->get_track_count(); track_index++) {
							if (String(animation->track_get_path(track_index)) != track_path) {
								continue;
							}
							switch (animation->track_get_type(track_index)) {
								case Animation::TYPE_POSITION_3D:
									joint_target.position_track = track_index;
									break;
								case Animation::TYPE_ROTATION_3D:
									joint_target.rotation_track = track_index;
									break;
								case Animation::TYPE_SCALE_3D:
									joint_target.scale_track = track_index;
									break;
								default:
									break;
							}
						}

						if (joint_target.position_track >= 0 || joint_target.rotation_track >= 0 || joint_target.scale_track >= 0) {
							joint_targets.push_back(joint_target);
						}
					}
					}

					Vector<SavedBlendShapeAnimationTarget> blend_shape_targets;
					for (int mesh_index = 0; mesh_index < bound_meshes.size(); mesh_index++) {
						MeshInstance3D *mesh_instance = bound_meshes[mesh_index];
						const Dictionary mesh_metadata = _get_usd_metadata(mesh_instance);
						const Dictionary blend_shape_channels = mesh_metadata.get("usd:blend_shape_channels", Dictionary());
						const String mesh_path = String(p_scene_root->get_path_to(mesh_instance));
						Array primary_names = blend_shape_channels.keys();
						for (int primary_index = 0; primary_index < primary_names.size(); primary_index++) {
							const String primary_name = primary_names[primary_index];
							const Array channel_entries = blend_shape_channels.get(primary_name, Array());
							const Vector<UsdBlendShapeChannelSpec> channel_specs = _parse_saved_blend_shape_channel_specs(channel_entries);
							if (channel_specs.is_empty()) {
								continue;
							}

							SavedBlendShapeAnimationTarget blend_shape_target;
							blend_shape_target.primary_name = primary_name;
							blend_shape_target.channel_specs = channel_specs;
							blend_shape_target.track_indices.resize(channel_specs.size());
							for (int spec_index = 0; spec_index < channel_specs.size(); spec_index++) {
								blend_shape_target.track_indices.write[spec_index] = -1;
								const String expected_track_path = mesh_path + ":" + channel_specs[spec_index].channel_name;
								for (int track_index = 0; track_index < animation->get_track_count(); track_index++) {
									if (animation->track_get_type(track_index) == Animation::TYPE_BLEND_SHAPE &&
											String(animation->track_get_path(track_index)) == expected_track_path) {
										blend_shape_target.track_indices.write[spec_index] = track_index;
										break;
									}
								}
							}

							bool has_any_track = false;
							for (int spec_index = 0; spec_index < blend_shape_target.track_indices.size(); spec_index++) {
								if (blend_shape_target.track_indices[spec_index] >= 0) {
									has_any_track = true;
									break;
								}
							}
							if (has_any_track) {
								blend_shape_targets.push_back(blend_shape_target);
							}
						}
					}

					if (joint_targets.is_empty() && blend_shape_targets.is_empty()) {
						continue;
					}

					Vector<double> sample_times;
					for (int joint_index = 0; joint_index < joint_targets.size(); joint_index++) {
						_append_unique_track_key_times(animation, joint_targets[joint_index].position_track, &sample_times);
						_append_unique_track_key_times(animation, joint_targets[joint_index].rotation_track, &sample_times);
						_append_unique_track_key_times(animation, joint_targets[joint_index].scale_track, &sample_times);
					}
					for (int export_index = 0; export_index < blend_shape_targets.size(); export_index++) {
						for (int spec_index = 0; spec_index < blend_shape_targets[export_index].track_indices.size(); spec_index++) {
							_append_unique_track_key_times(animation, blend_shape_targets[export_index].track_indices[spec_index], &sample_times);
						}
					}
					if (sample_times.is_empty()) {
						continue;
					}
					_sort_dedupe_sample_times(&sample_times);

					const double time_codes_per_second = MAX((double)animation_metadata.get("usd:time_codes_per_second", animation->get_step() > 0.0 ? (1.0 / animation->get_step()) : 24.0), 1.0);
					const double start_time_code = (double)animation_metadata.get("usd:start_time_code", 0.0);
					const bool has_authored_translations = (bool)animation_metadata.get("usd:has_authored_translations", false);
					const bool has_authored_rotations = (bool)animation_metadata.get("usd:has_authored_rotations", false);
					const bool has_authored_scales = (bool)animation_metadata.get("usd:has_authored_scales", false);
					const bool translations_constant = (bool)animation_metadata.get("usd:translations_constant", false);
					const bool rotations_constant = (bool)animation_metadata.get("usd:rotations_constant", false);
					const bool scales_constant = (bool)animation_metadata.get("usd:scales_constant", false);
					const Array translation_defaults = animation_metadata.get("usd:translation_defaults", Array());
					const Array rotation_defaults = animation_metadata.get("usd:rotation_defaults", Array());
					const Array scale_defaults = animation_metadata.get("usd:scale_defaults", Array());
					UsdSkelAnimation usd_animation = UsdSkelAnimation::Define(p_stage, skeleton_saved_path.AppendChild(TfToken(_make_valid_identifier(String(animation_name_sname)).utf8().get_data())));
					if (joint_targets.size() > 0) {
						VtArray<TfToken> joints;
						joints.resize(joint_targets.size());
						for (int joint_index = 0; joint_index < joint_targets.size(); joint_index++) {
							joints[joint_index] = TfToken(joint_targets[joint_index].joint_path.utf8().get_data());
						}
						usd_animation.CreateJointsAttr().Set(joints);
					}

					if (joint_targets.size() > 0) {
						bool has_position_tracks = false;
						bool has_rotation_tracks = false;
						bool has_scale_tracks = false;
						for (int joint_index = 0; joint_index < joint_targets.size(); joint_index++) {
							has_position_tracks = has_position_tracks || joint_targets[joint_index].position_track >= 0;
							has_rotation_tracks = has_rotation_tracks || joint_targets[joint_index].rotation_track >= 0;
							has_scale_tracks = has_scale_tracks || joint_targets[joint_index].scale_track >= 0;
						}

						if (has_authored_translations && translations_constant) {
							VtArray<GfVec3f> translations;
							translations.resize(joint_targets.size());
							for (int joint_index = 0; joint_index < joint_targets.size(); joint_index++) {
								Vector3 value = joint_targets[joint_index].rest_position;
								if (joint_index < translation_defaults.size() && translation_defaults[joint_index].get_type() == Variant::VECTOR3) {
									value = translation_defaults[joint_index];
								}
								translations[joint_index] = GfVec3f(value.x, value.y, value.z);
							}
							usd_animation.CreateTranslationsAttr().Set(translations);
						} else if (has_position_tracks) {
							const UsdAttribute translations_attr = usd_animation.CreateTranslationsAttr();
							for (int sample_index = 0; sample_index < sample_times.size(); sample_index++) {
								const double sample_time_seconds = sample_times[sample_index];
								VtArray<GfVec3f> translations;
								translations.resize(joint_targets.size());
								for (int joint_index = 0; joint_index < joint_targets.size(); joint_index++) {
									Vector3 value = joint_targets[joint_index].rest_position;
									if (joint_targets[joint_index].position_track >= 0) {
										value = animation->position_track_interpolate(joint_targets[joint_index].position_track, sample_time_seconds);
									}
									translations[joint_index] = GfVec3f(value.x, value.y, value.z);
								}
								translations_attr.Set(translations, UsdTimeCode(start_time_code + sample_time_seconds * time_codes_per_second));
							}
						}

						if (has_authored_rotations && rotations_constant) {
							VtArray<GfQuatf> rotations;
							rotations.resize(joint_targets.size());
							for (int joint_index = 0; joint_index < joint_targets.size(); joint_index++) {
								Quaternion value = joint_targets[joint_index].rest_rotation;
								if (joint_index < rotation_defaults.size() && rotation_defaults[joint_index].get_type() == Variant::QUATERNION) {
									value = rotation_defaults[joint_index];
								}
								rotations[joint_index] = GfQuatf(value.w, value.x, value.y, value.z);
							}
							usd_animation.CreateRotationsAttr().Set(rotations);
						} else if (has_rotation_tracks) {
							const UsdAttribute rotations_attr = usd_animation.CreateRotationsAttr();
							for (int sample_index = 0; sample_index < sample_times.size(); sample_index++) {
								const double sample_time_seconds = sample_times[sample_index];
								VtArray<GfQuatf> rotations;
								rotations.resize(joint_targets.size());
								for (int joint_index = 0; joint_index < joint_targets.size(); joint_index++) {
									Quaternion value = joint_targets[joint_index].rest_rotation;
									if (joint_targets[joint_index].rotation_track >= 0) {
										value = animation->rotation_track_interpolate(joint_targets[joint_index].rotation_track, sample_time_seconds);
									}
									rotations[joint_index] = GfQuatf(value.w, value.x, value.y, value.z);
								}
								rotations_attr.Set(rotations, UsdTimeCode(start_time_code + sample_time_seconds * time_codes_per_second));
							}
						}

						if (has_authored_scales && scales_constant) {
							VtArray<GfVec3h> scales;
							scales.resize(joint_targets.size());
							for (int joint_index = 0; joint_index < joint_targets.size(); joint_index++) {
								Vector3 value = joint_targets[joint_index].rest_scale;
								if (joint_index < scale_defaults.size() && scale_defaults[joint_index].get_type() == Variant::VECTOR3) {
									value = scale_defaults[joint_index];
								}
								scales[joint_index] = GfVec3h((GfHalf)value.x, (GfHalf)value.y, (GfHalf)value.z);
							}
							usd_animation.CreateScalesAttr().Set(scales);
						} else if (has_scale_tracks) {
							const UsdAttribute scales_attr = usd_animation.CreateScalesAttr();
							for (int sample_index = 0; sample_index < sample_times.size(); sample_index++) {
								const double sample_time_seconds = sample_times[sample_index];
								VtArray<GfVec3h> scales;
								scales.resize(joint_targets.size());
								for (int joint_index = 0; joint_index < joint_targets.size(); joint_index++) {
									Vector3 value = joint_targets[joint_index].rest_scale;
									if (joint_targets[joint_index].scale_track >= 0) {
										value = animation->scale_track_interpolate(joint_targets[joint_index].scale_track, sample_time_seconds);
									}
									scales[joint_index] = GfVec3h((GfHalf)value.x, (GfHalf)value.y, (GfHalf)value.z);
								}
								scales_attr.Set(scales, UsdTimeCode(start_time_code + sample_time_seconds * time_codes_per_second));
							}
						}
					}

					if (!blend_shape_targets.is_empty()) {
						VtArray<TfToken> blend_shape_tokens;
						blend_shape_tokens.resize(blend_shape_targets.size());
						for (int export_index = 0; export_index < blend_shape_targets.size(); export_index++) {
							blend_shape_tokens[export_index] = TfToken(blend_shape_targets[export_index].primary_name.utf8().get_data());
						}
						usd_animation.CreateBlendShapesAttr().Set(blend_shape_tokens);

						const UsdAttribute blend_shape_weights_attr = usd_animation.CreateBlendShapeWeightsAttr();
						for (int sample_index = 0; sample_index < sample_times.size(); sample_index++) {
							const double sample_time_seconds = sample_times[sample_index];
							VtArray<float> saved_weights;
							saved_weights.resize(blend_shape_targets.size());
							for (int export_index = 0; export_index < blend_shape_targets.size(); export_index++) {
								float source_weight = 0.0f;
								for (int spec_index = 0; spec_index < blend_shape_targets[export_index].channel_specs.size(); spec_index++) {
									const int track_index = blend_shape_targets[export_index].track_indices[spec_index];
									if (track_index < 0) {
										continue;
									}
									source_weight += animation->blend_shape_track_interpolate(track_index, sample_time_seconds) * blend_shape_targets[export_index].channel_specs[spec_index].weight;
								}
								saved_weights[export_index] = source_weight;
							}
							blend_shape_weights_attr.Set(saved_weights, UsdTimeCode(start_time_code + sample_time_seconds * time_codes_per_second));
						}
					}

					SdfPathVector animation_targets;
					animation_targets.push_back(usd_animation.GetPath());
					UsdRelationship animation_source = skeleton_saved_path.IsEmpty() ? UsdRelationship() : p_stage->GetPrimAtPath(skeleton_saved_path).CreateRelationship(TfToken("skel:animationSource"), false);
					if (animation_source) {
						animation_source.SetTargets(animation_targets);
					}
				}
			}
		}
	}

	static void _write_saved_mesh_skel_data_recursive(const UsdStageRefPtr &p_stage, Node *p_scene_root, Node *p_node, const HashMap<ObjectID, SdfPath> &p_saved_paths) {
		ERR_FAIL_NULL(p_scene_root);
		ERR_FAIL_NULL(p_node);
		if (MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node)) {
			const SdfPath saved_path = _get_saved_path_for_node(p_saved_paths, mesh_instance);
			if (!saved_path.IsEmpty()) {
				_write_mesh_skinning_and_blend_shapes(p_stage, p_scene_root, mesh_instance, UsdGeomMesh(p_stage->GetPrimAtPath(saved_path)), saved_path, p_saved_paths);
			}
		}
		for (int i = 0; i < p_node->get_child_count(); i++) {
			_write_saved_mesh_skel_data_recursive(p_stage, p_scene_root, p_node->get_child(i), p_saved_paths);
		}
	}

	static void _write_camera(const Camera3D *p_camera, UsdGeomCamera p_usd_camera, double p_meters_per_unit) {
		GfCamera camera;
		camera.SetClippingRange(GfRange1f((float)p_camera->get_near() / MAX((double)p_meters_per_unit, 0.000001), (float)p_camera->get_far() / MAX((double)p_meters_per_unit, 0.000001)));
		if (p_camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL) {
			camera.SetOrthographicFromAspectRatioAndSize(1.0f, (float)p_camera->get_size() / MAX((double)p_meters_per_unit, 0.000001), GfCamera::FOVVertical);
		} else {
			camera.SetPerspectiveFromAspectRatioAndFieldOfView(1.0f, (float)p_camera->get_fov(), GfCamera::FOVVertical);
		}
		p_usd_camera.SetFromCamera(camera, UsdTimeCode::Default());
	}

	static UsdPrim _define_prim_for_node(const UsdStageRefPtr &p_stage, Node *p_node, const SdfPath &p_path, double p_meters_per_unit, const String &p_save_path, bool *r_supports_transform = nullptr) {
		if (r_supports_transform != nullptr) {
			*r_supports_transform = false;
		}

		if (_has_preserved_composition_arcs(p_node)) {
			if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
				if (Object::cast_to<MeshInstance3D>(p_node) == nullptr &&
						Object::cast_to<Camera3D>(p_node) == nullptr &&
						Object::cast_to<Light3D>(p_node) == nullptr) {
					UsdGeomXform usd_xform = UsdGeomXform::Define(p_stage, p_path);
					if (r_supports_transform != nullptr) {
						*r_supports_transform = node_3d != nullptr;
					}
					return usd_xform.GetPrim();
				}
			}
			if (r_supports_transform != nullptr) {
				*r_supports_transform = Object::cast_to<Node3D>(p_node) != nullptr;
			}
			return p_stage->OverridePrim(p_path);
		}

		if (Camera3D *camera = Object::cast_to<Camera3D>(p_node)) {
			UsdGeomCamera usd_camera = UsdGeomCamera::Define(p_stage, p_path);
			_write_camera(camera, usd_camera, p_meters_per_unit);
			if (r_supports_transform != nullptr) {
				*r_supports_transform = true;
			}
			return usd_camera.GetPrim();
		}

		if (DirectionalLight3D *directional_light = Object::cast_to<DirectionalLight3D>(p_node)) {
			UsdLuxDistantLight usd_light = UsdLuxDistantLight::Define(p_stage, p_path);
			_apply_common_light_attributes(directional_light, usd_light);
			usd_light.CreateAngleAttr().Set(0.53f);
			if (r_supports_transform != nullptr) {
				*r_supports_transform = true;
			}
			return usd_light.GetPrim();
		}

		if (SpotLight3D *spot_light = Object::cast_to<SpotLight3D>(p_node)) {
			UsdLuxSphereLight usd_light = UsdLuxSphereLight::Define(p_stage, p_path);
			_apply_common_light_attributes(spot_light, usd_light);
			usd_light.CreateRadiusAttr().Set((float)MAX((double)spot_light->get_param(Light3D::PARAM_SIZE), 0.001));
			UsdLuxShapingAPI shaping_api = UsdLuxShapingAPI::Apply(usd_light.GetPrim());
			shaping_api.CreateShapingConeAngleAttr().Set((float)spot_light->get_param(Light3D::PARAM_SPOT_ANGLE));
			shaping_api.CreateShapingConeSoftnessAttr().Set(CLAMP(1.0f - (float)spot_light->get_param(Light3D::PARAM_SPOT_ATTENUATION), 0.0f, 1.0f));
			if (r_supports_transform != nullptr) {
				*r_supports_transform = true;
			}
			return usd_light.GetPrim();
		}

		if (OmniLight3D *omni_light = Object::cast_to<OmniLight3D>(p_node)) {
			UsdLuxSphereLight usd_light = UsdLuxSphereLight::Define(p_stage, p_path);
			_apply_common_light_attributes(omni_light, usd_light);
			usd_light.CreateRadiusAttr().Set((float)MAX((double)omni_light->get_param(Light3D::PARAM_SIZE), 0.001));
			if (r_supports_transform != nullptr) {
				*r_supports_transform = true;
			}
			return usd_light.GetPrim();
		}

		if (AreaLight3D *area_light = Object::cast_to<AreaLight3D>(p_node)) {
			UsdLuxRectLight usd_light = UsdLuxRectLight::Define(p_stage, p_path);
			_apply_common_light_attributes(area_light, usd_light);
			usd_light.CreateWidthAttr().Set((float)area_light->get_area_size().x);
			usd_light.CreateHeightAttr().Set((float)area_light->get_area_size().y);
			if (r_supports_transform != nullptr) {
				*r_supports_transform = true;
			}
			return usd_light.GetPrim();
		}

		if (Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_node)) {
			UsdSkelSkeleton usd_skeleton = UsdSkelSkeleton::Define(p_stage, p_path);
			_write_skeleton_prim(skeleton, usd_skeleton);
			if (r_supports_transform != nullptr) {
				*r_supports_transform = true;
			}
			return usd_skeleton.GetPrim();
		}

		if (MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node)) {
			UsdGeomMesh usd_mesh = UsdGeomMesh::Define(p_stage, p_path);
			Vector<UsdMeshSurfaceFaceRange> surface_face_ranges;
			const Dictionary usd_metadata = _get_usd_metadata(mesh_instance);
			if (!_write_mesh_geometry(mesh_instance->get_mesh(), usd_mesh, &usd_metadata, &surface_face_ranges)) {
				UsdGeomXform usd_xform = UsdGeomXform::Define(p_stage, p_path);
				if (r_supports_transform != nullptr) {
					*r_supports_transform = true;
				}
				return usd_xform.GetPrim();
			}
			_write_mesh_material_binding(p_stage, mesh_instance, usd_mesh, p_path, p_save_path, surface_face_ranges);
			_write_preserved_geom_subsets(mesh_instance, usd_mesh, surface_face_ranges);
			if (r_supports_transform != nullptr) {
				*r_supports_transform = true;
			}
			return usd_mesh.GetPrim();
		}

		UsdGeomXform usd_xform = UsdGeomXform::Define(p_stage, p_path);
		if (r_supports_transform != nullptr) {
			*r_supports_transform = Object::cast_to<Node3D>(p_node) != nullptr;
		}
		return usd_xform.GetPrim();
	}

	static void _write_transform(Node3D *p_node, const UsdPrim &p_prim, const Transform3D &p_stage_correction_inverse) {
		Transform3D authored_transform = p_node->get_transform();
		const Dictionary metadata = _get_usd_metadata(p_node);
		const bool resets_xform_stack = (bool)metadata.get("usd:resets_xform_stack", false);
		if (resets_xform_stack) {
			authored_transform = p_stage_correction_inverse * authored_transform;
		}

		UsdGeomXformable xformable(p_prim);
		if (!xformable) {
			UsdAttribute transform_attr = p_prim.CreateAttribute(TfToken("xformOp:transform"), SdfValueTypeNames->Matrix4d, false);
			if (transform_attr) {
				transform_attr.Set(_transform_to_gf_matrix(authored_transform), UsdTimeCode::Default());
			}

			VtArray<TfToken> op_order;
			op_order.push_back(TfToken("xformOp:transform"));
			UsdAttribute order_attr = p_prim.CreateAttribute(TfToken("xformOpOrder"), SdfValueTypeNames->TokenArray, false);
			if (order_attr) {
				order_attr.Set(op_order, UsdTimeCode::Default());
			}
			return;
		}

		UsdGeomXformOp transform_op = xformable.MakeMatrixXform();
		transform_op.Set(_transform_to_gf_matrix(authored_transform), UsdTimeCode::Default());
		xformable.SetResetXformStack(resets_xform_stack);
	}

	static bool _serialize_node_recursive(const UsdStageRefPtr &p_stage, Node *p_node, const SdfPath &p_parent_path, const Transform3D &p_stage_correction_inverse, double p_meters_per_unit, const String &p_save_path, Vector<SdfPath> *r_top_level_paths, HashMap<ObjectID, SdfPath> *r_saved_paths) {
		if (_is_generated_preview_node(p_node)) {
			return true;
		}
		if (_is_skipped_support_node(p_node)) {
			return true;
		}

		const String base_name = _make_valid_identifier(p_node->get_name());
		const SdfPath prim_path = p_parent_path.IsEmpty()
				? SdfPath::AbsoluteRootPath().AppendChild(TfToken(base_name.utf8().get_data()))
				: p_parent_path.AppendChild(TfToken(base_name.utf8().get_data()));

		bool supports_transform = false;
		UsdPrim prim = _define_prim_for_node(p_stage, p_node, prim_path, p_meters_per_unit, p_save_path, &supports_transform);
		if (!prim) {
			return false;
		}

		if (p_parent_path.IsEmpty() && r_top_level_paths != nullptr) {
			r_top_level_paths->push_back(prim_path);
		}
		if (r_saved_paths != nullptr) {
			r_saved_paths->insert(p_node->get_instance_id(), prim_path);
		}

		const bool preserved_composition_arcs = _reapply_composition_arcs(prim, p_node);

		if (supports_transform) {
			if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
				_write_transform(node_3d, prim, p_stage_correction_inverse);
			}
		}

		_reapply_unmapped_properties(prim, p_node);

		if (preserved_composition_arcs) {
			return true;
		}

		HashMap<String, int> name_counts;
		for (int i = 0; i < p_node->get_child_count(); i++) {
			Node *child = p_node->get_child(i);
			if (_is_generated_preview_node(child)) {
				continue;
			}

			const String child_base = _make_valid_identifier(child->get_name());
			const int seen_count = name_counts.has(child_base) ? name_counts[child_base] : 0;
			name_counts.insert(child_base, seen_count + 1);
			if (seen_count == 0) {
				if (!_serialize_node_recursive(p_stage, child, prim_path, p_stage_correction_inverse, p_meters_per_unit, p_save_path, nullptr, r_saved_paths)) {
					return false;
				}
			} else {
				String unique_name = vformat("%s_%d", child_base, seen_count + 1);
				const String original_name = child->get_name();
				child->set_name(unique_name);
				const bool ok = _serialize_node_recursive(p_stage, child, prim_path, p_stage_correction_inverse, p_meters_per_unit, p_save_path, nullptr, r_saved_paths);
				child->set_name(original_name);
				if (!ok) {
					return false;
				}
			}
		}

		return true;
	}

public:
	Error save(const Ref<PackedScene> &p_scene, const String &p_path) const {
		ERR_FAIL_COND_V_MSG(p_scene.is_null(), ERR_INVALID_PARAMETER, "USD saver requires a valid PackedScene resource.");

		Node *root = p_scene->instantiate();
		ERR_FAIL_NULL_V_MSG(root, ERR_CANT_CREATE, "USD saver could not instantiate the PackedScene.");

		const SaveContext context = _make_save_context(root);
		if (context.top_level_nodes.is_empty()) {
			memdelete(root);
			return ERR_INVALID_DATA;
		}

		DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());

		UsdStageRefPtr stage = UsdStage::CreateNew(_get_absolute_path(p_path).utf8().get_data());
		if (!stage) {
			memdelete(root);
			return ERR_CANT_CREATE;
		}

		UsdGeomSetStageMetersPerUnit(stage, context.meters_per_unit);
		UsdGeomSetStageUpAxis(stage, context.up_axis);
		stage->SetTimeCodesPerSecond(context.time_codes_per_second);
		if (context.has_time_code_range) {
			stage->SetStartTimeCode(context.start_time_code);
			stage->SetEndTimeCode(context.end_time_code);
		}

		const Transform3D stage_correction_inverse = _get_stage_correction_transform(context.meters_per_unit, context.up_axis).affine_inverse();
		Vector<SdfPath> top_level_paths;
		HashMap<ObjectID, SdfPath> saved_paths;
		for (int i = 0; i < context.top_level_nodes.size(); i++) {
			if (!_serialize_node_recursive(stage, context.top_level_nodes[i], SdfPath(), stage_correction_inverse, context.meters_per_unit, p_path, &top_level_paths, &saved_paths)) {
				memdelete(root);
				return ERR_INVALID_DATA;
			}
		}

		_write_saved_mesh_skel_data_recursive(stage, root, root, saved_paths);
		_write_saved_skeleton_animations(stage, root, saved_paths);

		if (!top_level_paths.is_empty()) {
			const String default_prim_path = !context.default_prim_path.is_empty() ? context.default_prim_path : _to_godot_string(top_level_paths[0].GetString());
			UsdPrim default_prim = stage->GetPrimAtPath(SdfPath(default_prim_path.utf8().get_data()));
			if (!default_prim && !top_level_paths.is_empty()) {
				default_prim = stage->GetPrimAtPath(top_level_paths[0]);
			}
			if (default_prim) {
				stage->SetDefaultPrim(default_prim);
			}
		}

		const bool saved = stage->GetRootLayer()->Save();
		memdelete(root);
		return saved ? OK : ERR_CANT_CREATE;
	}
};

bool _variant_selections_match_stage_defaults(const Dictionary &p_variant_selections, const Dictionary &p_stage_variant_sets) {
	for (const KeyValue<Variant, Variant> &prim_entry : p_variant_selections) {
		if (prim_entry.value.get_type() != Variant::DICTIONARY) {
			return false;
		}

		const Variant prim_variant_sets_variant = p_stage_variant_sets.get(prim_entry.key, Variant());
		if (prim_variant_sets_variant.get_type() != Variant::DICTIONARY) {
			return false;
		}

		const Dictionary prim_selections = prim_entry.value;
		const Dictionary prim_variant_sets = prim_variant_sets_variant;
		for (const KeyValue<Variant, Variant> &set_entry : prim_selections) {
			if (set_entry.value.get_type() != Variant::STRING && set_entry.value.get_type() != Variant::STRING_NAME) {
				return false;
			}

			const Variant set_description_variant = prim_variant_sets.get(set_entry.key, Variant());
			if (set_description_variant.get_type() != Variant::DICTIONARY) {
				return false;
			}

			const Dictionary set_description = set_description_variant;
			if ((String)set_description.get("selection", String()) != (String)set_entry.value) {
				return false;
			}
		}
	}

	return true;
}

struct SourceStageInstanceSaveInfo {
	String source_path;
	String source_absolute_path;
	Dictionary variant_selections;
	Dictionary stage_variant_sets;
};

struct CompositionBoundaryNodeState {
	Node *node = nullptr;
};

bool _usd_metadata_has_preserved_composition_boundary(const Dictionary &p_metadata) {
	return (bool)p_metadata.get("usd:variant_boundary", false) ||
			!((Array)p_metadata.get("usd:variant_context", Array())).is_empty() ||
			!((Array)p_metadata.get("usd:references", Array())).is_empty() ||
			!((Array)p_metadata.get("usd:payloads", Array())).is_empty() ||
			!((Array)p_metadata.get("usd:inherits", Array())).is_empty() ||
			!((Array)p_metadata.get("usd:specializes", Array())).is_empty();
}

void _report_usd_save_mode(const String &p_message, bool p_warning = false) {
	const String report = "USD save report: " + p_message;
	if (p_warning) {
		WARN_PRINT(report);
	} else {
		print_line(report);
	}
}

bool _transforms_equal_approx(const Transform3D &p_left, const Transform3D &p_right) {
	if (!p_left.origin.is_equal_approx(p_right.origin)) {
		return false;
	}
	for (int i = 0; i < 3; i++) {
		if (!p_left.basis.get_column(i).is_equal_approx(p_right.basis.get_column(i))) {
			return false;
		}
	}
	return true;
}

bool _generated_node_state_matches(Node *p_current, Node *p_expected, String *r_reason) {
	ERR_FAIL_NULL_V(p_current, false);
	ERR_FAIL_NULL_V(p_expected, false);

	if (p_current->get_name() != p_expected->get_name()) {
		if (r_reason != nullptr) {
			*r_reason = vformat("name changed from '%s' to '%s'", p_expected->get_name(), p_current->get_name());
		}
		return false;
	}

	if (String(p_current->get_class()) != String(p_expected->get_class())) {
		if (r_reason != nullptr) {
			*r_reason = vformat("node type changed from '%s' to '%s'", p_expected->get_class(), p_current->get_class());
		}
		return false;
	}

	Node3D *current_3d = Object::cast_to<Node3D>(p_current);
	Node3D *expected_3d = Object::cast_to<Node3D>(p_expected);
	if ((current_3d == nullptr) != (expected_3d == nullptr)) {
		if (r_reason != nullptr) {
			*r_reason = "Node3D mapping changed";
		}
		return false;
	}
	if (current_3d != nullptr && expected_3d != nullptr) {
		if (!_transforms_equal_approx(current_3d->get_transform(), expected_3d->get_transform())) {
			if (r_reason != nullptr) {
				*r_reason = "transform changed";
			}
			return false;
		}
		if (current_3d->is_visible() != expected_3d->is_visible()) {
			if (r_reason != nullptr) {
				*r_reason = "visibility changed";
			}
			return false;
		}
	}

	MeshInstance3D *current_mesh = Object::cast_to<MeshInstance3D>(p_current);
	MeshInstance3D *expected_mesh = Object::cast_to<MeshInstance3D>(p_expected);
	if ((current_mesh == nullptr) != (expected_mesh == nullptr)) {
		if (r_reason != nullptr) {
			*r_reason = "mesh node mapping changed";
		}
		return false;
	}
	if (current_mesh != nullptr && expected_mesh != nullptr) {
		const Ref<Mesh> current_mesh_resource = current_mesh->get_mesh();
		const Ref<Mesh> expected_mesh_resource = expected_mesh->get_mesh();
		const int current_surface_count = current_mesh_resource.is_valid() ? current_mesh_resource->get_surface_count() : 0;
		const int expected_surface_count = expected_mesh_resource.is_valid() ? expected_mesh_resource->get_surface_count() : 0;
		if (current_surface_count != expected_surface_count) {
			if (r_reason != nullptr) {
				*r_reason = "mesh surface count changed";
			}
			return false;
		}
	}

	return true;
}

void _collect_generated_composition_boundary_nodes(Node *p_node, bool p_under_composition_boundary, HashMap<String, CompositionBoundaryNodeState> *r_nodes, Vector<String> *r_unmapped_nodes, int p_unmapped_limit = 12) {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_NULL(r_nodes);

	const Dictionary metadata = _get_usd_metadata(p_node);
	if ((bool)metadata.get("usd:generated_preview", false)) {
		return;
	}

	const bool under_composition_boundary = p_under_composition_boundary || _usd_metadata_has_preserved_composition_boundary(metadata);
	const String prim_path = metadata.get("usd:prim_path", String());

	if (under_composition_boundary) {
		if (!prim_path.is_empty()) {
			CompositionBoundaryNodeState state;
			state.node = p_node;
			r_nodes->insert(prim_path, state);
		} else if (r_unmapped_nodes != nullptr && r_unmapped_nodes->size() < p_unmapped_limit) {
			r_unmapped_nodes->push_back(String(p_node->get_path()));
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_generated_composition_boundary_nodes(p_node->get_child(i), under_composition_boundary, r_nodes, r_unmapped_nodes, p_unmapped_limit);
	}
}

void _warn_source_stage_instance_composition_boundary_edits(Node *p_stage_instance_root, const String &p_source_path, const Dictionary &p_variant_selections) {
	ERR_FAIL_NULL(p_stage_instance_root);

	Node *generated_root = nullptr;
	for (int i = 0; i < p_stage_instance_root->get_child_count(); i++) {
		Node *child = p_stage_instance_root->get_child(i);
		if (_is_stage_instance_generated_root_node(child)) {
			generated_root = child;
			break;
		}
	}
	if (generated_root == nullptr) {
		return;
	}

	UsdStageRefPtr expected_stage = _open_stage_for_instance(p_source_path, p_variant_selections);
	if (!expected_stage) {
		return;
	}

	UsdSceneBuilder builder(expected_stage);
	Node *expected_root = builder.build("_Generated");
	if (expected_root == nullptr) {
		return;
	}

	HashMap<String, CompositionBoundaryNodeState> current_nodes;
	HashMap<String, CompositionBoundaryNodeState> expected_nodes;
	Vector<String> unmapped_current_nodes;
	_collect_generated_composition_boundary_nodes(generated_root, false, &current_nodes, &unmapped_current_nodes);
	_collect_generated_composition_boundary_nodes(expected_root, false, &expected_nodes, nullptr);
	if (current_nodes.is_empty() && unmapped_current_nodes.is_empty()) {
		memdelete(expected_root);
		return;
	}

	int warning_count = 0;
	const int warning_limit = 12;
	auto warn_once = [&](const String &p_message) {
		if (warning_count >= warning_limit) {
			return;
		}
		WARN_PRINT(vformat("USD source-preserving save detected generated edits below a composition boundary in %s: %s", p_source_path, p_message));
		warning_count++;
	};

	for (const String &node_path : unmapped_current_nodes) {
		warn_once(vformat("Godot-only child '%s' is under a generated variant/reference/payload boundary and will not be represented by variant-default preservation.", node_path));
	}

	for (const KeyValue<String, CompositionBoundaryNodeState> &current_entry : current_nodes) {
		const CompositionBoundaryNodeState *expected_state = expected_nodes.getptr(current_entry.key);
		if (expected_state == nullptr || expected_state->node == nullptr) {
			warn_once(vformat("prim %s no longer exists in the freshly composed source stage.", current_entry.key));
			continue;
		}

		String mismatch_reason;
		if (!_generated_node_state_matches(current_entry.value.node, expected_state->node, &mismatch_reason)) {
			warn_once(vformat("prim %s has a generated-node edit that cannot be merged into preserved USD composition (%s).", current_entry.key, mismatch_reason));
		}
	}

	for (const KeyValue<String, CompositionBoundaryNodeState> &expected_entry : expected_nodes) {
		if (!current_nodes.has(expected_entry.key)) {
			warn_once(vformat("prim %s is missing from the generated subtree.", expected_entry.key));
		}
	}

	if (warning_count == warning_limit) {
		WARN_PRINT(vformat("USD source-preserving save detected additional generated edits below composition boundaries in %s; further warnings were suppressed.", p_source_path));
	}

	memdelete(expected_root);
}

bool _node_tree_has_usd_composition_boundaries(Node *p_node) {
	ERR_FAIL_NULL_V(p_node, false);
	if (Object::cast_to<UsdStageInstance>(p_node)) {
		return true;
	}

	const Dictionary metadata = _get_usd_metadata(p_node);
	if (_usd_metadata_has_preserved_composition_boundary(metadata)) {
		return true;
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		if (_node_tree_has_usd_composition_boundaries(p_node->get_child(i))) {
			return true;
		}
	}

	return false;
}

bool _packed_scene_has_usd_composition_boundaries(const Ref<PackedScene> &p_scene) {
	if (p_scene.is_null()) {
		return false;
	}

	Node *root = p_scene->instantiate();
	if (root == nullptr) {
		return false;
	}

	const bool has_boundaries = _node_tree_has_usd_composition_boundaries(root);
	memdelete(root);
	return has_boundaries;
}

Error _copy_file_absolute_preserving_contents(const String &p_source_absolute_path, const String &p_destination_absolute_path) {
	if (p_source_absolute_path.simplify_path() == p_destination_absolute_path.simplify_path()) {
		return OK;
	}

	const Error make_dir_error = DirAccess::make_dir_recursive_absolute(p_destination_absolute_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(make_dir_error != OK, make_dir_error, vformat("Failed to create destination directory for USD save: %s", p_destination_absolute_path.get_base_dir()));

	return DirAccess::copy_absolute(p_source_absolute_path, p_destination_absolute_path);
}

bool _is_safe_usdz_member_path(const String &p_member_path) {
	const String normalized_path = p_member_path.replace("\\", "/");
	if (normalized_path.is_empty() || normalized_path.is_absolute_path() || normalized_path.contains(":")) {
		return false;
	}

	const PackedStringArray path_parts = normalized_path.split("/", false);
	for (int i = 0; i < path_parts.size(); i++) {
		if (path_parts[i] == "." || path_parts[i] == "..") {
			return false;
		}
	}

	return true;
}

Error _extract_usdz_package(const String &p_source_absolute_path, const String &p_destination_directory, String *r_root_layer_path, Vector<String> *r_package_file_paths) {
	ERR_FAIL_NULL_V(r_root_layer_path, ERR_INVALID_PARAMETER);
	ERR_FAIL_NULL_V(r_package_file_paths, ERR_INVALID_PARAMETER);
	*r_root_layer_path = String();
	r_package_file_paths->clear();

	Ref<FileAccess> zip_file_access;
	zlib_filefunc_def io = zipio_create_io(&zip_file_access);
	unzFile zip_file = unzOpen2(p_source_absolute_path.utf8().get_data(), &io);
	ERR_FAIL_NULL_V_MSG(zip_file, ERR_CANT_OPEN, vformat("Failed to open source USDZ package: %s", p_source_absolute_path));

	int zip_error = unzGoToFirstFile(zip_file);
	if (zip_error != UNZ_OK) {
		unzClose(zip_file);
		return ERR_FILE_CORRUPT;
	}

	Vector<uint8_t> read_buffer;
	read_buffer.resize(65536);
	do {
		unz_file_info64 file_info;
		String member_path;
		zip_error = godot_unzip_get_current_file_info(zip_file, file_info, member_path);
		if (zip_error != UNZ_OK) {
			unzClose(zip_file);
			return ERR_FILE_CORRUPT;
		}

		member_path = member_path.replace("\\", "/");
		if (!_is_safe_usdz_member_path(member_path)) {
			unzClose(zip_file);
			ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, vformat("Unsafe path in USDZ package: %s", member_path));
		}

		const bool is_directory = member_path.ends_with("/");
		const String destination_path = p_destination_directory.path_join(member_path);
		if (is_directory) {
			const Error make_dir_error = DirAccess::make_dir_recursive_absolute(destination_path);
			if (make_dir_error != OK) {
				unzClose(zip_file);
				return make_dir_error;
			}
			continue;
		}

		if (r_root_layer_path->is_empty()) {
			*r_root_layer_path = member_path;
		}
		r_package_file_paths->push_back(member_path);

		const Error make_dir_error = DirAccess::make_dir_recursive_absolute(destination_path.get_base_dir());
		if (make_dir_error != OK) {
			unzClose(zip_file);
			return make_dir_error;
		}

		zip_error = unzOpenCurrentFile(zip_file);
		if (zip_error != UNZ_OK) {
			unzClose(zip_file);
			return ERR_FILE_CORRUPT;
		}

		Error open_error = OK;
		Ref<FileAccess> destination_file = FileAccess::open(destination_path, FileAccess::WRITE, &open_error);
		if (open_error != OK || destination_file.is_null()) {
			unzCloseCurrentFile(zip_file);
			unzClose(zip_file);
			return open_error != OK ? open_error : ERR_CANT_CREATE;
		}

		while (true) {
			const int bytes_read = unzReadCurrentFile(zip_file, read_buffer.ptrw(), read_buffer.size());
			if (bytes_read < 0) {
				unzCloseCurrentFile(zip_file);
				unzClose(zip_file);
				return ERR_FILE_CORRUPT;
			}
			if (bytes_read == 0) {
				break;
			}

			destination_file->store_buffer(read_buffer.ptr(), bytes_read);
			if (destination_file->get_error() != OK) {
				unzCloseCurrentFile(zip_file);
				unzClose(zip_file);
				return destination_file->get_error();
			}
		}

		zip_error = unzCloseCurrentFile(zip_file);
		if (zip_error != UNZ_OK) {
			unzClose(zip_file);
			return ERR_FILE_CORRUPT;
		}
	} while (unzGoToNextFile(zip_file) == UNZ_OK);

	unzClose(zip_file);
	ERR_FAIL_COND_V_MSG(r_root_layer_path->is_empty(), ERR_FILE_CORRUPT, vformat("USDZ package has no root layer: %s", p_source_absolute_path));
	return OK;
}

Error _author_variant_selections_in_root_layer(const String &p_root_layer_absolute_path, const Dictionary &p_variant_selections) {
	if (p_variant_selections.is_empty()) {
		return OK;
	}

	SdfLayerRefPtr root_layer = SdfLayer::FindOrOpen(p_root_layer_absolute_path.utf8().get_data());
	ERR_FAIL_COND_V_MSG(!root_layer, ERR_CANT_OPEN, vformat("Failed to open USD root layer for variant authoring: %s", p_root_layer_absolute_path));

	UsdStageRefPtr stage = UsdStage::Open(root_layer);
	ERR_FAIL_COND_V_MSG(!stage, ERR_CANT_OPEN, vformat("Failed to compose USD root layer for variant authoring: %s", p_root_layer_absolute_path));

	stage->SetEditTarget(root_layer);
	_apply_variant_selections(stage, p_variant_selections);

	const bool saved = root_layer->Save();
	return saved ? OK : ERR_CANT_CREATE;
}

Error _create_usdz_package_from_extracted_files(const String &p_extracted_directory, const Vector<String> &p_package_file_paths, const String &p_root_layer_path, const String &p_package_path) {
	ERR_FAIL_COND_V_MSG(p_package_file_paths.is_empty(), ERR_INVALID_PARAMETER, "USDZ package cannot be created without extracted package files.");

	SdfZipFileWriter package_writer = SdfZipFileWriter::CreateNew(p_package_path.utf8().get_data());
	ERR_FAIL_COND_V_MSG(!package_writer, ERR_CANT_CREATE, vformat("Failed to create USDZ package writer: %s", p_package_path));

	const String root_layer_absolute_path = p_extracted_directory.path_join(p_root_layer_path);
	if (package_writer.AddFile(root_layer_absolute_path.utf8().get_data(), p_root_layer_path.utf8().get_data()).empty()) {
		package_writer.Discard();
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, vformat("Failed to add USDZ root layer to package: %s", p_root_layer_path));
	}

	for (int i = 0; i < p_package_file_paths.size(); i++) {
		const String package_file_path = p_package_file_paths[i];
		if (package_file_path == p_root_layer_path) {
			continue;
		}

		const String extracted_file_path = p_extracted_directory.path_join(package_file_path);
		if (package_writer.AddFile(extracted_file_path.utf8().get_data(), package_file_path.utf8().get_data()).empty()) {
			package_writer.Discard();
			ERR_FAIL_V_MSG(ERR_CANT_CREATE, vformat("Failed to add USDZ package member: %s", package_file_path));
		}
	}

	const bool saved = package_writer.Save();
	return saved ? OK : ERR_CANT_CREATE;
}

Error _save_source_usdz_with_variant_defaults(const String &p_source_absolute_path, const String &p_destination_absolute_path, const String &p_destination_file_name, const Dictionary &p_variant_selections) {
	Error temp_dir_error = OK;
	Ref<DirAccess> temp_dir = DirAccess::create_temp("godot_usdz_save_", false, &temp_dir_error);
	ERR_FAIL_COND_V_MSG(temp_dir_error != OK || temp_dir.is_null(), temp_dir_error != OK ? temp_dir_error : ERR_CANT_CREATE, "Failed to create temporary directory for USDZ save.");

	const String temp_directory = temp_dir->get_current_dir();
	String root_layer_path;
	Vector<String> package_file_paths;
	Error extract_error = _extract_usdz_package(p_source_absolute_path, temp_directory, &root_layer_path, &package_file_paths);
	ERR_FAIL_COND_V_MSG(extract_error != OK, extract_error, vformat("Failed to extract source USDZ package for variant save: %s", p_source_absolute_path));

	const String root_layer_absolute_path = temp_directory.path_join(root_layer_path);
	Error author_error = _author_variant_selections_in_root_layer(root_layer_absolute_path, p_variant_selections);
	ERR_FAIL_COND_V_MSG(author_error != OK, author_error, vformat("Failed to author USDZ variant selections into root layer: %s", root_layer_path));

	const String temp_package_path = temp_directory.path_join(p_destination_file_name.is_empty() ? "stage.usdz" : p_destination_file_name);
	Error package_error = _create_usdz_package_from_extracted_files(temp_directory, package_file_paths, root_layer_path, temp_package_path);
	ERR_FAIL_COND_V_MSG(package_error != OK, package_error, vformat("Failed to create USDZ package with variant defaults: %s", p_destination_absolute_path));

	return _copy_file_absolute_preserving_contents(temp_package_path, p_destination_absolute_path);
}

Error _save_source_usd_layer_with_variant_defaults(const String &p_source_absolute_path, const String &p_destination_absolute_path, const String &p_destination_file_name, const Dictionary &p_variant_selections) {
	Error temp_dir_error = OK;
	Ref<DirAccess> temp_dir = DirAccess::create_temp("godot_usd_layer_save_", false, &temp_dir_error);
	ERR_FAIL_COND_V_MSG(temp_dir_error != OK || temp_dir.is_null(), temp_dir_error != OK ? temp_dir_error : ERR_CANT_CREATE, "Failed to create temporary directory for USD layer save.");

	const String temp_directory = temp_dir->get_current_dir();
	const String temp_layer_path = temp_directory.path_join(p_destination_file_name.is_empty() ? ("stage." + p_source_absolute_path.get_extension()) : p_destination_file_name);
	Error copy_error = _copy_file_absolute_preserving_contents(p_source_absolute_path, temp_layer_path);
	ERR_FAIL_COND_V_MSG(copy_error != OK, copy_error, vformat("Failed to copy source USD layer for variant save: %s", p_source_absolute_path));

	Error author_error = _author_variant_selections_in_root_layer(temp_layer_path, p_variant_selections);
	ERR_FAIL_COND_V_MSG(author_error != OK, author_error, vformat("Failed to author USD variant selections into layer: %s", temp_layer_path));

	return _copy_file_absolute_preserving_contents(temp_layer_path, p_destination_absolute_path);
}

bool _try_get_source_stage_instance_save_info(const Ref<PackedScene> &p_scene, const String &p_required_source_extension, SourceStageInstanceSaveInfo *r_info) {
	ERR_FAIL_NULL_V(r_info, false);
	Node *root = p_scene->instantiate();
	ERR_FAIL_NULL_V_MSG(root, false, "USD saver could not instantiate the PackedScene.");

	UsdStageInstance *stage_instance = Object::cast_to<UsdStageInstance>(root);
	if (stage_instance == nullptr) {
		memdelete(root);
		return false;
	}

	const Ref<UsdStageResource> stage = stage_instance->get_stage();
	if (stage.is_null() || stage->get_source_path().is_empty()) {
		memdelete(root);
		return false;
	}

	const String source_path = stage->get_source_path();
	const String source_absolute_path = _get_absolute_path(source_path);
	const String source_extension = source_absolute_path.get_extension().to_lower();
	if (source_extension != p_required_source_extension) {
		memdelete(root);
		return false;
	}

	r_info->source_path = source_path;
	r_info->source_absolute_path = source_absolute_path;
	r_info->variant_selections = stage_instance->get_variant_selections();
	r_info->stage_variant_sets = stage->get_variant_sets();
	_warn_source_stage_instance_composition_boundary_edits(root, r_info->source_path, r_info->variant_selections);
	memdelete(root);
	return true;
}

bool _try_save_source_stage_instance(const Ref<PackedScene> &p_scene, const String &p_path, Error *r_error) {
	const String destination_extension = p_path.get_extension().to_lower();
	if (!_is_usd_scene_extension(destination_extension)) {
		return false;
	}

	SourceStageInstanceSaveInfo source_info;
	if (!_try_get_source_stage_instance_save_info(p_scene, destination_extension, &source_info)) {
		return false;
	}

	const String destination_absolute_path = _get_absolute_path(p_path);
	if (!_variant_selections_match_stage_defaults(source_info.variant_selections, source_info.stage_variant_sets)) {
		Error save_error = ERR_UNAVAILABLE;
		if (destination_extension == "usdz") {
			save_error = _save_source_usdz_with_variant_defaults(source_info.source_absolute_path, destination_absolute_path, p_path.get_file(), source_info.variant_selections);
		} else {
			save_error = _save_source_usd_layer_with_variant_defaults(source_info.source_absolute_path, destination_absolute_path, p_path.get_file(), source_info.variant_selections);
		}
		if (r_error != nullptr) {
			*r_error = save_error;
		}
		if (save_error == OK) {
			_report_usd_save_mode(vformat("authored selected variant defaults into source %s while preserving inactive variant data: %s -> %s", destination_extension.to_upper(), source_info.source_path, p_path));
		}
		return true;
	}

	const Error copy_error = _copy_file_absolute_preserving_contents(source_info.source_absolute_path, destination_absolute_path);
	if (r_error != nullptr) {
		*r_error = copy_error;
	}
	ERR_FAIL_COND_V_MSG(copy_error != OK, true, vformat("Failed to preserve source USD file while saving: %s -> %s", source_info.source_path, p_path));
	_report_usd_save_mode(vformat("preserved source USD file unchanged: %s -> %s", source_info.source_path, p_path));
	return true;
}

} // namespace

void UsdStageResource::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_source_path", "source_path"), &UsdStageResource::set_source_path);
	ClassDB::bind_method(D_METHOD("get_source_path"), &UsdStageResource::get_source_path);
	ClassDB::bind_method(D_METHOD("get_stage_metadata"), &UsdStageResource::get_stage_metadata);
	ClassDB::bind_method(D_METHOD("get_variant_sets"), &UsdStageResource::get_variant_sets);
	ClassDB::bind_method(D_METHOD("refresh_metadata"), &UsdStageResource::refresh_metadata);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "source_path", PROPERTY_HINT_FILE, "*.usd,*.usda,*.usdc,*.usdz"), "set_source_path", "get_source_path");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "stage_metadata", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_stage_metadata");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "variant_sets", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_variant_sets");
}

void UsdStageResource::set_source_path(const String &p_source_path) {
	if (source_path == p_source_path) {
		return;
	}

	source_path = p_source_path;
	const Error error = refresh_metadata();
	if (error != OK) {
		emit_changed();
	}
}

String UsdStageResource::get_source_path() const {
	return source_path;
}

Dictionary UsdStageResource::get_stage_metadata() const {
	return stage_metadata;
}

Dictionary UsdStageResource::get_variant_sets() const {
	return variant_sets;
}

Error UsdStageResource::refresh_metadata() {
	stage_metadata.clear();
	variant_sets.clear();

	if (source_path.is_empty()) {
		notify_property_list_changed();
		return ERR_UNCONFIGURED;
	}

	if (!FileAccess::exists(_get_project_path(source_path))) {
		notify_property_list_changed();
		return ERR_FILE_NOT_FOUND;
	}

	UsdStageRefPtr stage = _open_stage_for_instance(source_path);
	if (!stage) {
		notify_property_list_changed();
		return ERR_CANT_OPEN;
	}

	stage_metadata = _collect_stage_metadata(stage);
	variant_sets = _collect_variant_sets(stage);
	notify_property_list_changed();
	emit_changed();
	return OK;
}

void UsdStageInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_stage", "stage"), &UsdStageInstance::set_stage);
	ClassDB::bind_method(D_METHOD("get_stage"), &UsdStageInstance::get_stage);
	ClassDB::bind_method(D_METHOD("set_variant_selections", "variant_selections"), &UsdStageInstance::set_variant_selections);
	ClassDB::bind_method(D_METHOD("get_variant_selections"), &UsdStageInstance::get_variant_selections);
	ClassDB::bind_method(D_METHOD("set_debug_logging", "debug_logging"), &UsdStageInstance::set_debug_logging);
	ClassDB::bind_method(D_METHOD("is_debug_logging"), &UsdStageInstance::is_debug_logging);
	ClassDB::bind_method(D_METHOD("get_debug_rebuild_count"), &UsdStageInstance::get_debug_rebuild_count);
	ClassDB::bind_method(D_METHOD("get_debug_last_selection_change"), &UsdStageInstance::get_debug_last_selection_change);
	ClassDB::bind_method(D_METHOD("get_debug_last_rebuild_status"), &UsdStageInstance::get_debug_last_rebuild_status);
	ClassDB::bind_method(D_METHOD("get_debug_last_generated_summary"), &UsdStageInstance::get_debug_last_generated_summary);
	ClassDB::bind_method(D_METHOD("rebuild"), &UsdStageInstance::rebuild);
	ClassDB::bind_method(D_METHOD("get_node_for_prim_path", "prim_path"), &UsdStageInstance::get_node_for_prim_path);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stage", PROPERTY_HINT_RESOURCE_TYPE, UsdStageResource::get_class_static()), "set_stage", "get_stage");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "variant_selections", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_variant_selections", "get_variant_selections");
	ADD_GROUP("USD Debug", "debug_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_logging"), "set_debug_logging", "is_debug_logging");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_rebuild_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_rebuild_count");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "debug_last_selection_change", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_last_selection_change");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "debug_last_rebuild_status", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_last_rebuild_status");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "debug_last_generated_summary", PROPERTY_HINT_MULTILINE_TEXT, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_last_generated_summary");
}

void UsdStageInstance::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_SCENE_INSTANTIATED: {
			_adopt_existing_generated_root();
			if (stage.is_valid() && !stage->get_source_path().is_empty()) {
				_warn_source_stage_instance_composition_boundary_edits(this, stage->get_source_path(), variant_selections);
				rebuilt_after_scene_instantiation = rebuild() == OK;
			}
		} break;
		case NOTIFICATION_READY: {
			if (!rebuilt_after_scene_instantiation && stage.is_valid() && !stage->get_source_path().is_empty()) {
				rebuild();
			}
		} break;
	}
}

void UsdStageInstance::_clear_node_children(Node *p_node) {
	ERR_FAIL_NULL(p_node);
	for (int i = p_node->get_child_count() - 1; i >= 0; i--) {
		Node *child = p_node->get_child(i);
		p_node->remove_child(child);
		if (child->is_inside_tree()) {
			child->queue_free();
		} else {
			memdelete(child);
		}
	}
}

void UsdStageInstance::_clear_generated_children() {
	for (int i = get_child_count() - 1; i >= 0; i--) {
		Node *child = get_child(i);
		if (!_is_generated_root(child)) {
			continue;
		}

		remove_child(child);
		if (child->is_inside_tree()) {
			child->queue_free();
		} else {
			memdelete(child);
		}
	}

	generated_root = nullptr;
}

bool UsdStageInstance::_is_generated_root(Node *p_node) const {
	return _is_stage_instance_generated_root_node(p_node);
}

void UsdStageInstance::_adopt_existing_generated_root() {
	if (generated_root != nullptr && generated_root->get_parent() == this && _is_generated_root(generated_root)) {
		generated_root->set_meta(USD_STAGE_INSTANCE_GENERATED_META, true);
	} else {
		generated_root = nullptr;
	}

	if (generated_root == nullptr) {
		for (int i = 0; i < get_child_count(); i++) {
			Node *child = get_child(i);
			if (!_is_generated_root(child)) {
				continue;
			}

			generated_root = child;
			generated_root->set_meta(USD_STAGE_INSTANCE_GENERATED_META, true);
			break;
		}
	}

	for (int i = get_child_count() - 1; i >= 0; i--) {
		Node *child = get_child(i);
		if (child == generated_root || !_is_generated_root(child)) {
			continue;
		}

		remove_child(child);
		if (child->is_inside_tree()) {
			child->queue_free();
		} else {
			memdelete(child);
		}
	}
}

Node *UsdStageInstance::_get_generated_owner() const {
	if (is_inside_tree()) {
		SceneTree *tree = get_tree();
		Node *edited_scene_root = tree->get_edited_scene_root();
		if (edited_scene_root != nullptr && (edited_scene_root == this || edited_scene_root->is_ancestor_of(this))) {
			return edited_scene_root;
		}
	}

	return get_owner();
}

void UsdStageInstance::_mark_generated_tree_owned() {
	if (generated_root == nullptr) {
		return;
	}

	Node *owner = _get_generated_owner();
	if (owner == nullptr) {
		return;
	}

	_mark_owner_recursive(generated_root, owner);
}

void UsdStageInstance::_append_generated_summary(Node *p_node, PackedStringArray *r_summary, int p_limit) const {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_NULL(r_summary);
	if (r_summary->size() >= p_limit) {
		return;
	}

	if (p_node->has_meta(USD_META_KEY)) {
		const Variant metadata_variant = p_node->get_meta(USD_META_KEY);
		if (metadata_variant.get_type() == Variant::DICTIONARY) {
			const Dictionary metadata = metadata_variant;
			const String prim_path = metadata.get("usd:prim_path", String());
			if (!prim_path.is_empty()) {
				r_summary->push_back(prim_path);
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count() && r_summary->size() < p_limit; i++) {
		_append_generated_summary(p_node->get_child(i), r_summary, p_limit);
	}
}

String UsdStageInstance::_get_generated_summary() const {
	if (generated_root == nullptr) {
		return "<no generated root>";
	}

	PackedStringArray summary;
	_append_generated_summary(generated_root, &summary, 12);
	if (summary.is_empty()) {
		return "<generated root has no USD prim nodes>";
	}
	return String(", ").join(summary);
}

Node *UsdStageInstance::_find_node_for_prim_path(Node *p_node, const String &p_prim_path) const {
	ERR_FAIL_NULL_V(p_node, nullptr);

	if (_get_prim_path_for_node(p_node) == p_prim_path) {
		return p_node;
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *found = _find_node_for_prim_path(p_node->get_child(i), p_prim_path);
		if (found != nullptr) {
			return found;
		}
	}

	return nullptr;
}

String UsdStageInstance::_get_prim_path_for_node(const Node *p_node) const {
	ERR_FAIL_NULL_V(p_node, String());
	if (!p_node->has_meta(USD_META_KEY)) {
		return String();
	}

	const Variant metadata_variant = p_node->get_meta(USD_META_KEY);
	if (metadata_variant.get_type() != Variant::DICTIONARY) {
		return String();
	}

	const Dictionary metadata = metadata_variant;
	if ((bool)metadata.get("usd:generated_preview", false)) {
		return String();
	}

	return metadata.get("usd:prim_path", String());
}

bool UsdStageInstance::_get_node3d_runtime_state(Node *p_node, Dictionary *r_state) const {
	ERR_FAIL_NULL_V(p_node, false);
	ERR_FAIL_NULL_V(r_state, false);

	Node3D *node_3d = Object::cast_to<Node3D>(p_node);
	if (node_3d == nullptr) {
		return false;
	}

	const String prim_path = _get_prim_path_for_node(p_node);
	if (prim_path.is_empty()) {
		return false;
	}

	Dictionary state;
	state["transform"] = node_3d->get_transform();
	state["visible"] = node_3d->is_visible();
	*r_state = state;
	return true;
}

bool UsdStageInstance::_node3d_runtime_state_matches(Node *p_node, const Dictionary &p_state) const {
	ERR_FAIL_NULL_V(p_node, false);

	Node3D *node_3d = Object::cast_to<Node3D>(p_node);
	if (node_3d == nullptr) {
		return false;
	}

	if (p_state.get("transform", Variant()).get_type() != Variant::TRANSFORM3D) {
		return false;
	}
	const Transform3D transform = p_state["transform"];
	if (!_transforms_equal_approx(node_3d->get_transform(), transform)) {
		return false;
	}

	if (p_state.get("visible", Variant()).get_type() != Variant::BOOL) {
		return false;
	}
	return node_3d->is_visible() == (bool)p_state["visible"];
}

void UsdStageInstance::_collect_runtime_node_baselines(Node *p_node, Dictionary *r_baselines) const {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_NULL(r_baselines);

	Dictionary state;
	if (_get_node3d_runtime_state(p_node, &state)) {
		const String prim_path = _get_prim_path_for_node(p_node);
		r_baselines->set(prim_path, state);
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_runtime_node_baselines(p_node->get_child(i), r_baselines);
	}
}

void UsdStageInstance::_capture_runtime_node_overrides() {
	if (generated_root == nullptr) {
		return;
	}

	Dictionary current_states;
	_collect_runtime_node_baselines(generated_root, &current_states);
	for (const KeyValue<Variant, Variant> &current_entry : current_states) {
		if (current_entry.key.get_type() != Variant::STRING || current_entry.value.get_type() != Variant::DICTIONARY) {
			continue;
		}

		const String prim_path = current_entry.key;
		const Dictionary current_state = current_entry.value;
		const Variant baseline_variant = generated_node_baselines.get(prim_path, Variant());
		if (baseline_variant.get_type() != Variant::DICTIONARY) {
			runtime_node_overrides[prim_path] = current_state;
			continue;
		}

		Node *current_node = _find_node_for_prim_path(generated_root, prim_path);
		if (current_node != nullptr && !_node3d_runtime_state_matches(current_node, baseline_variant)) {
			runtime_node_overrides[prim_path] = current_state;
		} else {
			runtime_node_overrides.erase(prim_path);
		}
	}
}

void UsdStageInstance::_refresh_runtime_node_baselines() {
	generated_node_baselines.clear();
	if (generated_root == nullptr) {
		return;
	}

	_collect_runtime_node_baselines(generated_root, &generated_node_baselines);
}

void UsdStageInstance::_apply_runtime_node_overrides(Node *p_node) {
	ERR_FAIL_NULL(p_node);

	const String prim_path = _get_prim_path_for_node(p_node);
	if (!prim_path.is_empty()) {
		const Variant override_variant = runtime_node_overrides.get(prim_path, Variant());
		if (override_variant.get_type() == Variant::DICTIONARY) {
			Node3D *node_3d = Object::cast_to<Node3D>(p_node);
			if (node_3d != nullptr) {
				const Dictionary override_state = override_variant;
				if (override_state.get("transform", Variant()).get_type() == Variant::TRANSFORM3D) {
					node_3d->set_transform(override_state["transform"]);
				}
				if (override_state.get("visible", Variant()).get_type() == Variant::BOOL) {
					node_3d->set_visible((bool)override_state["visible"]);
				}
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_apply_runtime_node_overrides(p_node->get_child(i));
	}
}

bool UsdStageInstance::_parse_variant_property(const String &p_property, String *r_prim_path, String *r_variant_set) const {
	if (!p_property.begins_with("variants/")) {
		return false;
	}

	const String variant_path = p_property.substr(9);
	const int separator = variant_path.rfind("/");
	if (separator <= 0 || separator >= variant_path.length() - 1) {
		return false;
	}

	if (r_prim_path != nullptr) {
		*r_prim_path = "/" + variant_path.substr(0, separator);
	}
	if (r_variant_set != nullptr) {
		*r_variant_set = variant_path.substr(separator + 1);
	}
	return true;
}

String UsdStageInstance::_get_variant_selection(const String &p_prim_path, const String &p_variant_set) const {
	if (variant_selections.has(p_prim_path)) {
		const Variant prim_selection_variant = variant_selections[p_prim_path];
		if (prim_selection_variant.get_type() == Variant::DICTIONARY) {
			const Dictionary prim_selections = prim_selection_variant;
			const Variant selection = prim_selections.get(p_variant_set, Variant());
			if (selection.get_type() == Variant::STRING || selection.get_type() == Variant::STRING_NAME) {
				return selection;
			}
		}
	}

	const Dictionary variant_sets = !composed_variant_sets.is_empty() ? composed_variant_sets : (stage.is_valid() ? stage->get_variant_sets() : Dictionary());
	if (!variant_sets.is_empty()) {
		const Variant prim_sets_variant = variant_sets.get(p_prim_path, Variant());
		if (prim_sets_variant.get_type() == Variant::DICTIONARY) {
			const Dictionary prim_sets = prim_sets_variant;
			const Variant set_description_variant = prim_sets.get(p_variant_set, Variant());
			if (set_description_variant.get_type() == Variant::DICTIONARY) {
				const Dictionary set_description = set_description_variant;
				return set_description.get("selection", String());
			}
		}
	}

	return String();
}

void UsdStageInstance::_set_variant_selection_property(const String &p_prim_path, const String &p_variant_set, const String &p_selection) {
	const String current_selection = _get_variant_selection(p_prim_path, p_variant_set);
	debug_last_selection_change = vformat("%s:%s %s -> %s", p_prim_path, p_variant_set, current_selection, p_selection);
	if (current_selection == p_selection) {
		debug_last_rebuild_status = "Skipped rebuild because selection was unchanged.";
		if (debug_logging) {
			print_line(vformat("UsdStageInstance: %s", debug_last_rebuild_status));
		}
		notify_property_list_changed();
		return;
	}

	Dictionary updated_selections = variant_selections.duplicate(true);
	Dictionary prim_selections;
	const Variant prim_selection_variant = updated_selections.get(p_prim_path, Variant());
	if (prim_selection_variant.get_type() == Variant::DICTIONARY) {
		prim_selections = ((Dictionary)prim_selection_variant).duplicate(true);
	}

	prim_selections[p_variant_set] = p_selection;
	updated_selections[p_prim_path] = prim_selections;
	variant_selections = updated_selections;

	if (stage.is_valid() && !stage->get_source_path().is_empty() && (is_inside_tree() || generated_root != nullptr)) {
		rebuild();
	} else {
		debug_last_rebuild_status = stage.is_valid() && !stage->get_source_path().is_empty() ? "Deferred rebuild until the instance enters the scene tree." : "Skipped rebuild because the instance has no stage source path.";
	}
	notify_property_list_changed();
}

void UsdStageInstance::_stage_changed() {
	notify_property_list_changed();
	generated_node_baselines.clear();
	runtime_node_overrides.clear();
	skip_next_runtime_override_capture = true;

	if (stage.is_null() || stage->get_source_path().is_empty()) {
		composed_variant_sets.clear();
		_clear_generated_children();
		rebuilt_after_scene_instantiation = false;
		return;
	}

	if (is_inside_tree() || generated_root != nullptr) {
		rebuild();
	} else {
		debug_last_rebuild_status = "Deferred rebuild until the instance enters the scene tree.";
	}
}

bool UsdStageInstance::_set(const StringName &p_name, const Variant &p_value) {
	String prim_path;
	String variant_set;
	if (!_parse_variant_property(p_name, &prim_path, &variant_set)) {
		return false;
	}

	if (p_value.get_type() != Variant::STRING && p_value.get_type() != Variant::STRING_NAME) {
		return false;
	}

	_set_variant_selection_property(prim_path, variant_set, p_value);
	return true;
}

bool UsdStageInstance::_get(const StringName &p_name, Variant &r_ret) const {
	String prim_path;
	String variant_set;
	if (!_parse_variant_property(p_name, &prim_path, &variant_set)) {
		return false;
	}

	r_ret = _get_variant_selection(prim_path, variant_set);
	return true;
}

void UsdStageInstance::_get_property_list(List<PropertyInfo> *p_list) const {
	if (stage.is_null()) {
		return;
	}

	const Dictionary variant_sets = !composed_variant_sets.is_empty() ? composed_variant_sets : stage->get_variant_sets();
	if (!variant_sets.is_empty()) {
		p_list->push_back(PropertyInfo(Variant::NIL, "USD Variants", PROPERTY_HINT_NONE, "variants/", PROPERTY_USAGE_GROUP));
	}

	for (const KeyValue<Variant, Variant> &prim_entry : variant_sets) {
		if (prim_entry.value.get_type() != Variant::DICTIONARY) {
			continue;
		}

		const String prim_path = prim_entry.key;
		const String property_prim_path = prim_path.trim_prefix("/");
		if (property_prim_path.is_empty()) {
			continue;
		}

		const Dictionary prim_sets = prim_entry.value;
		for (const KeyValue<Variant, Variant> &set_entry : prim_sets) {
			if (set_entry.value.get_type() != Variant::DICTIONARY) {
				continue;
			}

			const String variant_set = set_entry.key;
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

			p_list->push_back(PropertyInfo(Variant::STRING, "variants/" + property_prim_path + "/" + variant_set, PROPERTY_HINT_ENUM, hint_string, PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED));
		}
	}
}

void UsdStageInstance::set_stage(const Ref<UsdStageResource> &p_stage) {
	if (stage == p_stage) {
		return;
	}

	if (stage.is_valid()) {
		stage->disconnect_changed(callable_mp(this, &UsdStageInstance::_stage_changed));
	}

	stage = p_stage;
	rebuilt_after_scene_instantiation = false;
	generated_node_baselines.clear();
	runtime_node_overrides.clear();
	if (stage.is_valid()) {
		stage->connect_changed(callable_mp(this, &UsdStageInstance::_stage_changed));
	}

	_stage_changed();
}

Ref<UsdStageResource> UsdStageInstance::get_stage() const {
	return stage;
}

void UsdStageInstance::set_variant_selections(const Dictionary &p_variant_selections) {
	variant_selections = p_variant_selections;
	rebuilt_after_scene_instantiation = false;
	debug_last_selection_change = "variant_selections dictionary replaced";
	if (stage.is_valid() && !stage->get_source_path().is_empty() && (is_inside_tree() || generated_root != nullptr)) {
		rebuild();
	} else {
		debug_last_rebuild_status = stage.is_valid() && !stage->get_source_path().is_empty() ? "Deferred rebuild until the instance enters the scene tree." : "Skipped rebuild because the instance has no stage source path.";
	}
	notify_property_list_changed();
}

Dictionary UsdStageInstance::get_variant_selections() const {
	return variant_selections;
}

void UsdStageInstance::set_debug_logging(bool p_debug_logging) {
	debug_logging = p_debug_logging;
}

bool UsdStageInstance::is_debug_logging() const {
	return debug_logging;
}

int UsdStageInstance::get_debug_rebuild_count() const {
	return debug_rebuild_count;
}

String UsdStageInstance::get_debug_last_selection_change() const {
	return debug_last_selection_change;
}

String UsdStageInstance::get_debug_last_rebuild_status() const {
	return debug_last_rebuild_status;
}

String UsdStageInstance::get_debug_last_generated_summary() const {
	return debug_last_generated_summary;
}

Error UsdStageInstance::rebuild() {
	if (skip_next_runtime_override_capture) {
		skip_next_runtime_override_capture = false;
	} else {
		_capture_runtime_node_overrides();
	}
	composed_variant_sets.clear();
	debug_rebuild_count++;
	debug_last_rebuild_status = vformat("Rebuild #%d started.", debug_rebuild_count);
	if (debug_logging) {
		print_line(vformat("UsdStageInstance: %s selections=%s", debug_last_rebuild_status, Variant(variant_selections)));
	}

	if (stage.is_null()) {
		debug_last_rebuild_status = "Rebuild failed: instance requires a stage resource.";
		ERR_FAIL_V_MSG(ERR_UNCONFIGURED, debug_last_rebuild_status);
	}
	if (stage->get_source_path().is_empty()) {
		debug_last_rebuild_status = "Rebuild failed: stage resource has no source path.";
		ERR_FAIL_V_MSG(ERR_UNCONFIGURED, debug_last_rebuild_status);
	}

	UsdStageRefPtr composed_stage = _open_stage_for_instance(stage->get_source_path(), variant_selections);
	if (!composed_stage) {
		debug_last_rebuild_status = vformat("Rebuild failed: could not compose USD stage for %s.", stage->get_source_path());
		ERR_FAIL_V_MSG(ERR_CANT_OPEN, debug_last_rebuild_status);
	}
	composed_variant_sets = _collect_variant_sets(composed_stage);

	UsdSceneBuilder builder(composed_stage);
	Node *rebuilt_root = builder.build("_Generated");
	if (rebuilt_root == nullptr) {
		debug_last_rebuild_status = "Rebuild failed: could not build generated Godot root.";
		ERR_FAIL_V(ERR_CANT_CREATE);
	}

	_adopt_existing_generated_root();
	if (generated_root == nullptr) {
		generated_root = rebuilt_root;
		generated_root->set_meta(USD_STAGE_INSTANCE_GENERATED_META, true);
		add_child(generated_root);
	} else {
		_clear_node_children(generated_root);
		if (Node3D *generated_root_3d = Object::cast_to<Node3D>(generated_root)) {
			if (Node3D *rebuilt_root_3d = Object::cast_to<Node3D>(rebuilt_root)) {
				generated_root_3d->set_transform(rebuilt_root_3d->get_transform());
			}
		}
		while (rebuilt_root->get_child_count() > 0) {
			Node *child = rebuilt_root->get_child(0);
			rebuilt_root->remove_child(child);
			generated_root->add_child(child);
		}
		memdelete(rebuilt_root);
	}

	generated_root->set_meta("usd_stage_instance_source_path", stage->get_source_path());
	generated_root->set_meta("usd_stage_instance_variant_selections", variant_selections);
	_refresh_runtime_node_baselines();
	_apply_runtime_node_overrides(generated_root);
	_mark_generated_tree_owned();
	debug_last_generated_summary = _get_generated_summary();
	debug_last_rebuild_status = vformat("Rebuild #%d completed: %d generated root children.", debug_rebuild_count, generated_root->get_child_count());
	if (debug_logging) {
		print_line(vformat("UsdStageInstance: %s summary=%s", debug_last_rebuild_status, debug_last_generated_summary));
	}

	return OK;
}

Node *UsdStageInstance::get_node_for_prim_path(const String &p_prim_path) const {
	if (generated_root == nullptr) {
		return nullptr;
	}

	return _find_node_for_prim_path(generated_root, p_prim_path);
}

Ref<Resource> UsdSceneFormatLoader::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	(void)p_original_path;
	(void)p_use_sub_threads;

	if (r_progress) {
		*r_progress = 0.0f;
	}

	if (r_error) {
		*r_error = ERR_FILE_CANT_OPEN;
	}

	if (!FileAccess::exists(_get_project_path(p_path))) {
		if (r_error) {
			*r_error = ERR_FILE_NOT_FOUND;
		}
		return Ref<Resource>();
	}

	const String absolute_path = _get_absolute_path(p_path);
	UsdStageRefPtr stage = UsdStage::Open(absolute_path.utf8().get_data(), UsdStage::LoadAll);
	if (!stage) {
		if (r_error) {
			*r_error = ERR_PARSE_ERROR;
		}
		ERR_PRINT(vformat("Failed to open USD stage: %s", p_path));
		return Ref<Resource>();
	}

	Node *scene_root = nullptr;
	const Dictionary variant_sets = _collect_variant_sets(stage);
	if (!variant_sets.is_empty()) {
		UsdStageInstance *stage_instance = memnew(UsdStageInstance);
		stage_instance->set_name(p_path.get_file().get_basename());
		Ref<UsdStageResource> stage_resource;
		stage_resource.instantiate();
		stage_resource->set_source_path(p_path);
		stage_instance->set_stage(stage_resource);
		stage_instance->rebuild();
		scene_root = stage_instance;
	} else {
		UsdSceneBuilder builder(stage);
		scene_root = builder.build(p_path.get_file().get_basename());
	}
	ERR_FAIL_NULL_V_MSG(scene_root, Ref<Resource>(), vformat("Failed to build Godot scene from USD stage: %s", p_path));

	for (int i = 0; i < scene_root->get_child_count(); i++) {
		_mark_owner_recursive(scene_root->get_child(i), scene_root);
	}

	Ref<PackedScene> packed_scene;
	packed_scene.instantiate();
	Error pack_error = packed_scene->pack(scene_root);
	memdelete(scene_root);

	if (pack_error != OK) {
		if (r_error) {
			*r_error = pack_error;
		}
		ERR_PRINT(vformat("Failed to pack generated USD scene: %s", p_path));
		return Ref<Resource>();
	}

	if (p_cache_mode != ResourceFormatLoader::CACHE_MODE_IGNORE) {
		if (!ResourceCache::has(p_path)) {
			packed_scene->set_path(p_path);
		}
	} else {
		packed_scene->get_state()->set_path(p_path);
		packed_scene->set_path_cache(p_path);
	}

	if (r_progress) {
		*r_progress = 1.0f;
	}
	if (r_error) {
		*r_error = OK;
	}

	return packed_scene;
}

void UsdSceneFormatLoader::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("usd");
	p_extensions->push_back("usda");
	p_extensions->push_back("usdc");
	p_extensions->push_back("usdz");
}

bool UsdSceneFormatLoader::handles_type(const String &p_type) const {
	return ClassDB::is_parent_class("PackedScene", p_type);
}

String UsdSceneFormatLoader::get_resource_type(const String &p_path) const {
	const String extension = p_path.get_extension().to_lower();
	if (extension == "usd" || extension == "usda" || extension == "usdc" || extension == "usdz") {
		return "PackedScene";
	}
	return String();
}

Error UsdSceneFormatSaver::save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	(void)p_flags;

	Ref<PackedScene> packed_scene = p_resource;
	ERR_FAIL_COND_V_MSG(packed_scene.is_null(), ERR_UNAVAILABLE, "USD saver only supports PackedScene resources.");
	ERR_FAIL_COND_V_MSG(!recognize_path(p_resource, p_path), ERR_FILE_UNRECOGNIZED, "USD saver only writes .usd, .usda, .usdc, and .usdz files.");

	Error source_stage_save_error = OK;
	if (_try_save_source_stage_instance(packed_scene, p_path, &source_stage_save_error)) {
		return source_stage_save_error;
	}

	UsdSceneSaver saver;
	if (p_path.get_extension().to_lower() != "usdz") {
		const bool has_composition_boundaries = _packed_scene_has_usd_composition_boundaries(packed_scene);
		_report_usd_save_mode(vformat("exporting composed Godot scene to %s; preserved read-only composition arcs stored on nodes are reauthored, but variant sets, inactive branches, and unsupported arcs are not reconstructed by this path.", p_path), has_composition_boundaries);
		return saver.save(packed_scene, p_path);
	}

	const String package_source_path = p_path + ".tmp.usda";
	const bool has_composition_boundaries = _packed_scene_has_usd_composition_boundaries(packed_scene);
	_report_usd_save_mode(vformat("packaging composed Godot scene as USDZ at %s; preserved read-only composition arcs stored on nodes are reauthored, but original package contents, inactive variant branches, and unsupported arcs are not preserved by this path.", p_path), has_composition_boundaries);
	Error save_error = saver.save(packed_scene, package_source_path);
	if (save_error != OK) {
		return save_error;
	}

	const String absolute_package_source_path = _get_absolute_path(package_source_path);
	const String absolute_package_path = _get_absolute_path(p_path);
	const String first_layer_name = p_path.get_file().get_basename() + ".usda";
	const bool packaged = UsdUtilsCreateNewUsdzPackage(SdfAssetPath(absolute_package_source_path.utf8().get_data()), absolute_package_path.utf8().get_data(), first_layer_name.utf8().get_data());
	DirAccess::remove_absolute(package_source_path);
	return packaged ? OK : ERR_CANT_CREATE;
}

bool UsdSceneFormatSaver::recognize(const Ref<Resource> &p_resource) const {
	return p_resource.is_valid() && p_resource->is_class("PackedScene");
}

void UsdSceneFormatSaver::get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const {
	if (recognize(p_resource)) {
		p_extensions->push_back("usd");
		p_extensions->push_back("usda");
		p_extensions->push_back("usdc");
		p_extensions->push_back("usdz");
	}
}

bool UsdSceneFormatSaver::recognize_path(const Ref<Resource> &p_resource, const String &p_path) const {
	return recognize(p_resource) && _is_usd_scene_extension(p_path.get_extension());
}
