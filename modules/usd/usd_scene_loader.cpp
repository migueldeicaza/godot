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
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/main/node.h"
#include "scene/resources/3d/primitive_meshes.h"
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
#include <pxr/usd/usd/property.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
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
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
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

String _to_godot_string(const std::string &p_string) {
	return String::utf8(p_string.c_str());
}

String _get_absolute_path(const String &p_path) {
	if (p_path.begins_with("res://") || p_path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(p_path);
	}
	return p_path;
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

Dictionary _serialize_attribute(const UsdAttribute &p_attribute, const UsdTimeCode &p_time) {
	Dictionary description;
	description["type_name"] = _to_godot_string(p_attribute.GetTypeName().GetAsToken().GetString());
	description["is_custom"] = p_attribute.IsCustom();

	VtValue value;
	if (p_attribute.Get(&value, p_time)) {
		description["value"] = _to_godot_string(TfStringify(value));
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

		relationships[_to_godot_string(relationship.GetName().GetString())] = target_paths;
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
	Ref<Material> material;
	String usd_material_path;
};

struct UsdMeshBuildResult {
	Ref<ArrayMesh> mesh;
	Array material_paths;
};

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
			if (xformable) {
				GfMatrix4d local_matrix(1.0);
				bool resets_xform_stack = false;
				if (xformable.GetLocalTransformation(&local_matrix, &resets_xform_stack, time)) {
					node_3d->set_transform(_gf_matrix_to_transform(local_matrix));
				}
				if (resets_xform_stack) {
					node_3d->set_as_top_level(true);
					_set_usd_metadata(node_3d, "usd:resets_xform_stack", true);
				}

				r_handled_attributes->insert("xformOpOrder");
			}

			UsdGeomImageable imageable(p_prim);
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

		UsdShadeMaterialBindingAPI mesh_binding_api(p_mesh.GetPrim());
		UsdShadeMaterial default_material = mesh_binding_api.ComputeBoundMaterial();
		if (default_material) {
			surfaces.write[0].material = _build_material_from_usd_material(default_material, r_mapping_notes);
			surfaces.write[0].usd_material_path = _to_godot_string(default_material.GetPath().GetString());
			r_handled_attributes->insert("material:binding");
		}

		HashMap<int, int> face_to_surface;
		const std::vector<UsdGeomSubset> material_subsets = mesh_binding_api.GetMaterialBindSubsets();
		for (const UsdGeomSubset &subset : material_subsets) {
			VtIntArray subset_faces;
			if (!subset.GetIndicesAttr().Get(&subset_faces, time)) {
				continue;
			}

			UsdSurfaceAccumulator subset_surface;
			UsdShadeMaterial subset_material = UsdShadeMaterialBindingAPI(subset.GetPrim()).ComputeBoundMaterial();
			if (subset_material) {
				subset_surface.material = _build_material_from_usd_material(subset_material, r_mapping_notes);
				subset_surface.usd_material_path = _to_godot_string(subset_material.GetPath().GetString());
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

		const real_t unit_scale = _meters_scale(meters_per_unit);
		Basis root_basis;
		if (up_axis == UsdGeomTokens->z) {
			root_basis = Basis(Vector3(1, 0, 0), (real_t)-Math::PI * 0.5);
		}
		root_basis = root_basis.scaled(Vector3(unit_scale, unit_scale, unit_scale));
		root->set_transform(Transform3D(root_basis, Vector3()));

		Dictionary stage_metadata;
		stage_metadata["usd:source_identifier"] = _to_godot_string(stage->GetRootLayer()->GetIdentifier());
		stage_metadata["usd:up_axis"] = _to_godot_string(up_axis.GetString());
		stage_metadata["usd:meters_per_unit"] = meters_per_unit;
		stage_metadata["usd:read_only_loader"] = true;
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

		return root;
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
