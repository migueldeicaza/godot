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
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/main/node.h"
#include "scene/resources/3d/primitive_meshes.h"
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
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/property.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/plane.h>
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

String _get_absolute_path(const String &p_path) {
	if (p_path.begins_with("res://") || p_path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(p_path);
	}
	return p_path;
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

struct UsdSurfaceAccumulator {
	PackedVector3Array vertices;
	PackedInt32Array indices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array authored_face_indices;
	Ref<Material> material;
	String usd_material_path;
	bool has_material_binding = false;
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
};

struct UsdMeshSurfaceFaceRange {
	int face_start = 0;
	int face_count = 0;
	Vector<int> saved_face_indices;
};

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
		if (element_type != UsdGeomTokens->face) {
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
	mutable HashMap<String, Ref<Image>> image_cache;
	mutable HashMap<String, Ref<Texture2D>> texture_cache;
	mutable HashMap<String, Ref<Material>> material_cache;

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

	Dictionary _make_common_metadata(const UsdPrim &p_prim) const {
		Dictionary metadata;
		metadata["usd:prim_path"] = _to_godot_string(p_prim.GetPath().GetString());
		metadata["usd:type_name"] = _to_godot_string(p_prim.GetTypeName().GetString());
		metadata["usd:active"] = p_prim.IsActive();

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
		UsdGeomPrimvar uv_primvar = primvars_api.FindPrimvarWithInheritance(TfToken("st"));
		if (uv_primvar && uv_primvar.HasValue()) {
			return uv_primvar;
		}

		uv_primvar = primvars_api.FindPrimvarWithInheritance(TfToken("map1"));
		if (uv_primvar && uv_primvar.HasValue()) {
			return uv_primvar;
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

			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

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
			result.material_subsets.push_back(surface_description);
		}

		result.mesh = mesh->get_surface_count() > 0 ? mesh : Ref<ArrayMesh>();
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
			} else {
				mapping_notes["usd:mesh_status"] = "Mesh geometry was detected, but no triangulated surface could be generated.";
			}
			node = mesh_instance;
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

public:
	explicit UsdSceneBuilder(const UsdStageRefPtr &p_stage) :
			stage(p_stage),
			meters_per_unit(UsdGeomGetStageMetersPerUnit(stage)),
			up_axis(UsdGeomGetStageUpAxis(stage)) {}

	Node *build(const String &p_scene_name) const {
		Node3D *root = memnew(Node3D);
		root->set_name(p_scene_name.is_empty() ? String("USDScene") : p_scene_name);

		root->set_transform(_get_stage_correction_transform());

		Dictionary stage_metadata;
		stage_metadata["usd:source_identifier"] = _to_godot_string(stage->GetRootLayer()->GetIdentifier());
		stage_metadata["usd:up_axis"] = _to_godot_string(up_axis.GetString());
		stage_metadata["usd:meters_per_unit"] = meters_per_unit;
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

		return root;
	}
};

class UsdSceneSaver {
	struct SaveContext {
		double meters_per_unit = 1.0;
		TfToken up_axis = UsdGeomTokens->y;
		Vector<Node *> top_level_nodes;
		String default_prim_path;
	};

	static constexpr const char *GODOT_MATERIAL_SCOPE_NAME = "__GodotMaterials";

	static bool _is_generated_preview_node(const Node *p_node) {
		const Dictionary metadata = _get_usd_metadata(p_node);
		return (bool)metadata.get("usd:generated_preview", false);
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
		const Dictionary metadata = _get_usd_metadata(p_root);

		if (metadata.has("usd:meters_per_unit")) {
			context.meters_per_unit = (double)metadata["usd:meters_per_unit"];
		}
		if (metadata.has("usd:up_axis")) {
			context.up_axis = _up_axis_from_string((String)metadata["usd:up_axis"]);
		}
		if (metadata.has("usd:default_prim_path")) {
			context.default_prim_path = metadata["usd:default_prim_path"];
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

				const String subset_name = _make_valid_identifier(description.get("subset_name", vformat("Surface_%d", surface_index)));
				const String family_name_string = description.get("family_name", String("materialBind"));
				const String family_type_string = description.get("family_type", String("nonOverlapping"));
				const TfToken family_name = TfToken(family_name_string.utf8().get_data());
				const TfToken family_type = TfToken(family_type_string.utf8().get_data());

				UsdGeomSubset subset = UsdGeomSubset::CreateUniqueGeomSubset(p_usd_mesh, TfToken(subset_name.utf8().get_data()), UsdGeomTokens->face, subset_faces, family_name, family_type);
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

			const String subset_name = _make_valid_identifier(vformat("Surface_%d", surface_index));
			UsdGeomSubset subset = UsdGeomSubset::CreateUniqueGeomSubset(p_usd_mesh, TfToken(subset_name.utf8().get_data()), UsdGeomTokens->face, subset_faces, UsdShadeTokens->materialBind, UsdGeomTokens->nonOverlapping);

			const Dictionary description = get_surface_description(surface_index);
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
		for (int surface_index = 0; surface_index < mesh->get_surface_count(); surface_index++) {
			if (surface_descriptions[surface_index].get_type() != Variant::DICTIONARY) {
				continue;
			}

			const Dictionary surface_description = surface_descriptions[surface_index];
			const PackedInt32Array authored_face_indices = surface_description.get("authored_face_indices", PackedInt32Array());
			const Vector<int> &saved_face_indices = p_surface_face_ranges[surface_index].saved_face_indices;
			if (authored_face_indices.size() != saved_face_indices.size()) {
				continue;
			}

			for (int i = 0; i < authored_face_indices.size(); i++) {
				authored_to_saved_face_index.insert(authored_face_indices[i], saved_face_indices[i]);
			}
		}

		for (int subset_index = 0; subset_index < preserved_subsets.size(); subset_index++) {
			if (preserved_subsets[subset_index].get_type() != Variant::DICTIONARY) {
				continue;
			}

			const Dictionary subset_description = preserved_subsets[subset_index];
			const String element_type_string = subset_description.get("element_type", String());
			if (element_type_string != "face") {
				continue;
			}

			const PackedInt32Array authored_indices = subset_description.get("indices", PackedInt32Array());
			if (authored_indices.is_empty()) {
				continue;
			}

			VtIntArray subset_faces;
			subset_faces.reserve(authored_indices.size());
			bool complete_mapping = true;
			for (int i = 0; i < authored_indices.size(); i++) {
				if (!authored_to_saved_face_index.has(authored_indices[i])) {
					complete_mapping = false;
					break;
				}
				subset_faces.push_back(authored_to_saved_face_index[authored_indices[i]]);
			}
			if (!complete_mapping || subset_faces.empty()) {
				continue;
			}

			const String subset_name = _make_valid_identifier(subset_description.get("subset_name", vformat("GeomSubset_%d", subset_index)));
			const String family_name_string = subset_description.get("family_name", String());
			const String family_type_string = subset_description.get("family_type", String("nonOverlapping"));
			const TfToken family_name = family_name_string.is_empty() ? TfToken() : TfToken(family_name_string.utf8().get_data());
			const TfToken family_type = family_type_string.is_empty() ? UsdGeomTokens->nonOverlapping : TfToken(family_type_string.utf8().get_data());
			const TfToken element_type(element_type_string.utf8().get_data());

			UsdGeomSubset::CreateUniqueGeomSubset(p_usd_mesh, TfToken(subset_name.utf8().get_data()), element_type, subset_faces, family_name, family_type);
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
			if (has_surface_descriptions && surface_descriptions[surface_index].get_type() == Variant::DICTIONARY) {
				const Dictionary surface_description = surface_descriptions[surface_index];
				authored_face_indices = surface_description.get("authored_face_indices", PackedInt32Array());
			}
			const int32_t vertex_offset = (int32_t)points.size();
			int emitted_faces = 0;

			for (int i = 0; i < vertices.size(); i++) {
				const Vector3 vertex = vertices[i];
				points.push_back(GfVec3f(vertex.x, vertex.y, vertex.z));
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
		UsdGeomXformable xformable(p_prim);
		if (!xformable) {
			return;
		}

		Transform3D authored_transform = p_node->get_transform();
		const Dictionary metadata = _get_usd_metadata(p_node);
		const bool resets_xform_stack = (bool)metadata.get("usd:resets_xform_stack", false);
		if (resets_xform_stack) {
			authored_transform = p_stage_correction_inverse * authored_transform;
		}

		UsdGeomXformOp transform_op = xformable.MakeMatrixXform();
		transform_op.Set(_transform_to_gf_matrix(authored_transform), UsdTimeCode::Default());
		xformable.SetResetXformStack(resets_xform_stack);
	}

	static bool _serialize_node_recursive(const UsdStageRefPtr &p_stage, Node *p_node, const SdfPath &p_parent_path, const Transform3D &p_stage_correction_inverse, double p_meters_per_unit, const String &p_save_path, Vector<SdfPath> *r_top_level_paths) {
		if (_is_generated_preview_node(p_node)) {
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

		if (supports_transform) {
			if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
				_write_transform(node_3d, prim, p_stage_correction_inverse);
			}
		}

		_reapply_unmapped_properties(prim, p_node);

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
				if (!_serialize_node_recursive(p_stage, child, prim_path, p_stage_correction_inverse, p_meters_per_unit, p_save_path, nullptr)) {
					return false;
				}
			} else {
				String unique_name = vformat("%s_%d", child_base, seen_count + 1);
				const String original_name = child->get_name();
				child->set_name(unique_name);
				const bool ok = _serialize_node_recursive(p_stage, child, prim_path, p_stage_correction_inverse, p_meters_per_unit, p_save_path, nullptr);
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

		const Transform3D stage_correction_inverse = _get_stage_correction_transform(context.meters_per_unit, context.up_axis).affine_inverse();
		Vector<SdfPath> top_level_paths;
		for (int i = 0; i < context.top_level_nodes.size(); i++) {
			if (!_serialize_node_recursive(stage, context.top_level_nodes[i], SdfPath(), stage_correction_inverse, context.meters_per_unit, p_path, &top_level_paths)) {
				memdelete(root);
				return ERR_INVALID_DATA;
			}
		}

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

} // namespace

Ref<Resource> UsdSceneFormatLoader::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	(void)p_original_path;
	(void)p_use_sub_threads;

	if (r_progress) {
		*r_progress = 0.0f;
	}

	if (r_error) {
		*r_error = ERR_FILE_CANT_OPEN;
	}

	if (!FileAccess::exists(p_path)) {
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

	UsdSceneBuilder builder(stage);
	Node *scene_root = builder.build(p_path.get_file().get_basename());
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
	ERR_FAIL_COND_V_MSG(!recognize_path(p_resource, p_path), ERR_FILE_UNRECOGNIZED, "USD saver only writes .usda files in this prototype.");

	UsdSceneSaver saver;
	return saver.save(packed_scene, p_path);
}

bool UsdSceneFormatSaver::recognize(const Ref<Resource> &p_resource) const {
	return p_resource.is_valid() && p_resource->is_class("PackedScene");
}

void UsdSceneFormatSaver::get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const {
	if (recognize(p_resource)) {
		p_extensions->push_back("usda");
	}
}

bool UsdSceneFormatSaver::recognize_path(const Ref<Resource> &p_resource, const String &p_path) const {
	return recognize(p_resource) && p_path.get_extension().to_lower() == "usda";
}
