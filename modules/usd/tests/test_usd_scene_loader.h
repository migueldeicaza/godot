/**************************************************************************/
/*  test_usd_scene_loader.h                                               */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_usd_scene_loader)

#ifndef _3D_DISABLED

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_USD_ENABLED

#include "tests/test_utils.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/path_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/animation/animation_player.h"
#include "scene/resources/curve.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/packed_scene.h"
#include "modules/usd/usd_scene_loader.h"
#ifdef TOOLS_ENABLED
#include "modules/usd/usd_scene_importer.h"
#endif

namespace TestUsdSceneLoader {

static constexpr const char *USD_PREVIEW_LIGHTING_MODE_SETTING = "filesystem/import/usd/preview_lighting_mode";

class PreviewLightingModeScope {
	Variant original_mode;

public:
	PreviewLightingModeScope() {
		original_mode = ProjectSettings::get_singleton()->get_setting(USD_PREVIEW_LIGHTING_MODE_SETTING, 1);
	}

	~PreviewLightingModeScope() {
		ProjectSettings::get_singleton()->set_setting(USD_PREVIEW_LIGHTING_MODE_SETTING, original_mode);
	}

	void set(int p_mode) {
		ProjectSettings::get_singleton()->set_setting(USD_PREVIEW_LIGHTING_MODE_SETTING, p_mode);
	}
};

static Ref<ArrayMesh> _make_test_triangle_mesh() {
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);

	PackedVector3Array vertices;
	vertices.push_back(Vector3(0.0f, 0.0f, 0.0f));
	vertices.push_back(Vector3(1.0f, 0.0f, 0.0f));
	vertices.push_back(Vector3(0.0f, 1.0f, 0.0f));
	arrays[Mesh::ARRAY_VERTEX] = vertices;

	PackedVector3Array normals;
	normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
	normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
	normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
	arrays[Mesh::ARRAY_NORMAL] = normals;

	PackedVector2Array uvs;
	uvs.push_back(Vector2(0.0f, 0.0f));
	uvs.push_back(Vector2(1.0f, 0.0f));
	uvs.push_back(Vector2(0.0f, 1.0f));
	arrays[Mesh::ARRAY_TEX_UV] = uvs;

	PackedInt32Array indices;
	indices.push_back(0);
	indices.push_back(1);
	indices.push_back(2);
	arrays[Mesh::ARRAY_INDEX] = indices;

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

static Node *_find_prim_node(Node *p_root, const String &p_prim_path) {
	ERR_FAIL_NULL_V(p_root, nullptr);

	Dictionary metadata = p_root->get_meta(StringName("usd"), Dictionary());
	if ((String)metadata.get("usd:prim_path", String()) == p_prim_path) {
		return p_root;
	}

	for (int i = 0; i < p_root->get_child_count(); i++) {
		if (Node *match = _find_prim_node(p_root->get_child(i), p_prim_path)) {
			return match;
		}
	}

	return nullptr;
}

#ifdef TOOLS_ENABLED
static const ResourceImporter::ImportOption *_find_import_option(const List<ResourceImporter::ImportOption> &p_options, const String &p_name) {
	for (const List<ResourceImporter::ImportOption>::Element *E = p_options.front(); E != nullptr; E = E->next()) {
		if (E->get().option.name == p_name) {
			return &E->get();
		}
	}
	return nullptr;
}
#endif

TEST_CASE("[SceneTree][USD] Load a minimal USD scene as PackedScene") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(1);

	const String usd_path = TestUtils::get_data_path("usd/basic.usda");

	CHECK(ResourceLoader::get_resource_type(usd_path) == "PackedScene");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->is_class("Node3D"));
	CHECK(root->get_name() == "basic");
	CHECK(root->get_child_count() >= 1);

	Dictionary root_metadata = root->get_meta(StringName("usd"), Dictionary());
	CHECK((bool)root_metadata.get("usd:read_only_loader", false));
	CHECK((String)root_metadata.get("usd:default_prim_path", String()) == String("/Root"));
	CHECK((bool)root_metadata.get("usd:has_authored_lights", true) == false);
	CHECK((bool)root_metadata.get("usd:has_preview_lighting", false));
	CHECK((String)root_metadata.get("usd:preview_lighting_mode", String()) == String("when_missing"));

	Node *usd_root = root->get_child(0);
	REQUIRE(usd_root != nullptr);
	REQUIRE(usd_root->is_class("Node3D"));
	CHECK(usd_root->get_name() == "Root");
	CHECK(usd_root->get_child_count() == 1);

	Dictionary prim_metadata = usd_root->get_meta(StringName("usd"), Dictionary());
	CHECK((String)prim_metadata.get("usd:prim_path", String()) == String("/Root"));
	CHECK((String)prim_metadata.get("usd:type_name", String()) == String("Xform"));

	Node *mesh_node = usd_root->get_child(0);
	REQUIRE(mesh_node != nullptr);
	REQUIRE(mesh_node->is_class("MeshInstance3D"));
	CHECK(mesh_node->get_name() == "Triangle");

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(mesh_node);
	REQUIRE(mesh_instance != nullptr);
	CHECK(mesh_instance->get_mesh().is_valid());

	Dictionary mesh_metadata = mesh_node->get_meta(StringName("usd"), Dictionary());
	CHECK((String)mesh_metadata.get("usd:prim_path", String()) == String("/Root/Triangle"));
	CHECK((String)mesh_metadata.get("usd:type_name", String()) == String("Mesh"));

		memdelete(root);
	}

TEST_CASE("[SceneTree][USD] Load a textured UsdPreviewSurface material") {
	const String usd_path = TestUtils::get_data_path("usd/textured_preview.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD textured material load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->is_class("Node3D"));
	CHECK(root->get_child_count() >= 1);

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(root->get_child(0));
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());
	CHECK(mesh_instance->get_mesh()->get_surface_count() == 1);

	Ref<Material> surface_material = mesh_instance->get_mesh()->surface_get_material(0);
	REQUIRE(surface_material.is_valid());

	BaseMaterial3D *base_material = Object::cast_to<BaseMaterial3D>(surface_material.ptr());
	REQUIRE(base_material != nullptr);
	CHECK(base_material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO).is_valid());

	Dictionary mesh_metadata = mesh_instance->get_meta(StringName("usd"), Dictionary());
	Array material_bindings = mesh_metadata.get("usd:material_bindings", Array());
	CHECK(material_bindings.size() == 1);
	CHECK((String)material_bindings[0] == String("/TexturedQuad/Materials/TestMaterial"));

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Load a packaged USDZ texture asset") {
	const String usd_path = TestUtils::get_data_path("usd/packaged_preview.usdz");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USDZ packaged material load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->is_class("Node3D"));
	CHECK(root->get_child_count() >= 1);

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(root->get_child(0));
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());

	Ref<Material> surface_material = mesh_instance->get_mesh()->surface_get_material(0);
	REQUIRE(surface_material.is_valid());

	BaseMaterial3D *base_material = Object::cast_to<BaseMaterial3D>(surface_material.ptr());
	REQUIRE(base_material != nullptr);
	CHECK(base_material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO).is_valid());

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Load extended UsdPreviewSurface inputs") {
	const String usd_path = TestUtils::get_data_path("usd/preview_surface_extended.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD extended PreviewSurface load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() >= 1);

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(root->get_child(0)->get_child(0));
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());

	Ref<Material> surface_material = mesh_instance->get_mesh()->surface_get_material(0);
	REQUIRE(surface_material.is_valid());

	BaseMaterial3D *base_material = Object::cast_to<BaseMaterial3D>(surface_material.ptr());
	REQUIRE(base_material != nullptr);
	CHECK(base_material->get_specular() == doctest::Approx(0.5f).epsilon(0.01f));
	CHECK(base_material->get_roughness() == doctest::Approx(0.45f));
	CHECK(base_material->get_transparency() == BaseMaterial3D::TRANSPARENCY_ALPHA);
	CHECK(base_material->get_feature(BaseMaterial3D::FEATURE_CLEARCOAT));
	CHECK(base_material->get_clearcoat() == doctest::Approx(0.6f));
	CHECK(base_material->get_clearcoat_roughness() == doctest::Approx(0.2f));
	CHECK(base_material->get_texture(BaseMaterial3D::TEXTURE_CLEARCOAT).is_valid());
	CHECK(base_material->get_feature(BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION));
	CHECK(base_material->get_texture(BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION).is_valid());
	CHECK(base_material->get_ao_texture_channel() == BaseMaterial3D::TEXTURE_CHANNEL_BLUE);

	Dictionary material_metadata = base_material->get_meta(StringName("usd"), Dictionary());
	CHECK((bool)material_metadata.get("usd:preview_surface_use_specular_workflow", false));
	CHECK((double)material_metadata.get("usd:preview_surface_ior", 0.0) == doctest::Approx(1.5));
	CHECK(material_metadata.has("usd:preview_surface_specular_color"));
	Dictionary texture_sources = material_metadata.get("usd:preview_surface_texture_sources", Dictionary());
	CHECK(texture_sources.has("opacity"));
	CHECK(texture_sources.has("clearcoat"));
	CHECK(texture_sources.has("clearcoatRoughness"));
	CHECK(texture_sources.has("occlusion"));

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Preserve clockwise winding for right-handed USD meshes") {
	const String usd_path = TestUtils::get_data_path("usd/winding_right_handed.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD winding test load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() >= 1);

	Node *scene_root = root->get_child(0);
	REQUIRE(scene_root != nullptr);
	REQUIRE(scene_root->get_child_count() == 1);

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(scene_root->get_child(0));
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());

	Array arrays = mesh_instance->get_mesh()->surface_get_arrays(0);
	REQUIRE(arrays.size() == Mesh::ARRAY_MAX);

	PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
	REQUIRE(vertices.size() >= 3);

	const Vector3 face_normal = (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]).normalized();
	CHECK(face_normal.z < 0.0f);

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Import face-varying primvars:normals") {
	const String usd_path = TestUtils::get_data_path("usd/primvar_normals.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD primvar normals load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() >= 1);

	Node *scene_root = root->get_child(0);
	REQUIRE(scene_root != nullptr);
	REQUIRE(scene_root->get_child_count() == 1);

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(scene_root->get_child(0));
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());

	Array arrays = mesh_instance->get_mesh()->surface_get_arrays(0);
	REQUIRE(arrays.size() == Mesh::ARRAY_MAX);

	PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
	PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
	REQUIRE(vertices.size() == 6);
	REQUIRE(normals.size() == 6);
	CHECK(normals[0].z == doctest::Approx(1.0f));
	CHECK(normals[5].z == doctest::Approx(1.0f));

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Import emissive preview materials and additional light schemas") {
	const String usd_path = TestUtils::get_data_path("usd/emissive_and_lights.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD emissive/light load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->is_class("Node3D"));
	CHECK(root->get_child_count() >= 1);

	Node *scene_root = root->get_child(0);
	REQUIRE(scene_root != nullptr);
	CHECK(scene_root->get_child_count() == 3);

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(scene_root->get_child(0));
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());

	Ref<Material> surface_material = mesh_instance->get_mesh()->surface_get_material(0);
	REQUIRE(surface_material.is_valid());

	BaseMaterial3D *base_material = Object::cast_to<BaseMaterial3D>(surface_material.ptr());
	REQUIRE(base_material != nullptr);
	CHECK(base_material->get_feature(BaseMaterial3D::FEATURE_EMISSION));
	CHECK(base_material->get_texture(BaseMaterial3D::TEXTURE_EMISSION).is_valid());
	CHECK(base_material->get_transparency() == BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
	CHECK(base_material->get_alpha_scissor_threshold() == doctest::Approx(0.3f));

	AreaLight3D *area_light = Object::cast_to<AreaLight3D>(scene_root->get_child(1));
	REQUIRE(area_light != nullptr);
	CHECK(area_light->get_area_size().x == doctest::Approx(0.5f));
	CHECK(area_light->get_area_size().y == doctest::Approx(0.25f));
	CHECK(area_light->get_area_texture().is_valid());

	SpotLight3D *spot_light = Object::cast_to<SpotLight3D>(scene_root->get_child(2));
	REQUIRE(spot_light != nullptr);
	CHECK(spot_light->get_param(Light3D::PARAM_SPOT_ANGLE) == doctest::Approx(25.0f));

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Import linear BasisCurves as Path3D children") {
	const String usd_path = TestUtils::get_data_path("usd/basis_curves_linear.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD linear BasisCurves load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() >= 1);

	Node *scene_root = root->get_child(0);
	REQUIRE(scene_root != nullptr);
	REQUIRE(scene_root->get_child_count() == 1);

	Node3D *curves_root = Object::cast_to<Node3D>(scene_root->get_child(0));
	REQUIRE(curves_root != nullptr);
	Dictionary curves_metadata = curves_root->get_meta(StringName("usd"), Dictionary());
	CHECK((String)curves_metadata.get("usd:type_name", String()) == String("BasisCurves"));
	CHECK((String)curves_metadata.get("usd:curve_type", String()) == String("linear"));
	CHECK((String)curves_metadata.get("usd:curve_wrap", String()) == String("periodic"));
	CHECK((String)curves_metadata.get("usd:curve_mapping", String()) == String("path3d_children"));
	CHECK((int)curves_metadata.get("usd:curve_count", 0) == 1);

	Array vertex_counts = curves_metadata.get("usd:curve_vertex_counts", Array());
	REQUIRE(vertex_counts.size() == 1);
	CHECK((int)vertex_counts[0] == 5);

	Path3D *path = Object::cast_to<Path3D>(curves_root->get_child(0));
	REQUIRE(path != nullptr);
	REQUIRE(path->get_curve().is_valid());
	CHECK(path->get_curve()->get_point_count() == 5);
	CHECK(path->get_curve()->is_closed());

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Import cubic bezier BasisCurves as multiple Path3D children") {
	const String usd_path = TestUtils::get_data_path("usd/basis_curves_bezier.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD bezier BasisCurves load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() == 1);

	Node *scene_root = root->get_child(0);
	REQUIRE(scene_root != nullptr);
	REQUIRE(scene_root->get_child_count() == 1);

	Node3D *curves_root = Object::cast_to<Node3D>(scene_root->get_child(0));
	REQUIRE(curves_root != nullptr);
	Dictionary curves_metadata = curves_root->get_meta(StringName("usd"), Dictionary());
	CHECK((String)curves_metadata.get("usd:type_name", String()) == String("BasisCurves"));
	CHECK((String)curves_metadata.get("usd:curve_type", String()) == String("cubic"));
	CHECK((String)curves_metadata.get("usd:curve_basis", String()) == String("bezier"));
	CHECK((int)curves_metadata.get("usd:generated_curve_children", 0) == 2);

	Array vertex_counts = curves_metadata.get("usd:curve_vertex_counts", Array());
	REQUIRE(vertex_counts.size() == 2);
	CHECK((int)vertex_counts[0] == 4);
	CHECK((int)vertex_counts[1] == 7);

	Path3D *first_path = Object::cast_to<Path3D>(curves_root->get_child(0));
	Path3D *second_path = Object::cast_to<Path3D>(curves_root->get_child(1));
	REQUIRE(first_path != nullptr);
	REQUIRE(second_path != nullptr);
	REQUIRE(first_path->get_curve().is_valid());
	REQUIRE(second_path->get_curve().is_valid());
	CHECK(first_path->get_curve()->get_point_count() == 2);
	CHECK(second_path->get_curve()->get_point_count() == 3);
	CHECK(first_path->get_curve()->get_point_out(0).length() > 0.0f);
	CHECK(first_path->get_curve()->get_point_in(1).length() > 0.0f);
	CHECK(second_path->get_curve()->get_point_out(1).length() > 0.0f);
	CHECK(second_path->get_curve()->get_point_in(2).length() > 0.0f);

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Import a structural UsdSkelSkeleton as Skeleton3D") {
	const String usd_path = TestUtils::get_data_path("usd/skeleton_basic.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD skeleton load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() == 1);

	Node *scene_root = root->get_child(0);
	REQUIRE(scene_root != nullptr);
	REQUIRE(scene_root->get_child_count() == 1);

	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(scene_root->get_child(0));
	REQUIRE(skeleton != nullptr);
	CHECK(skeleton->get_bone_count() == 3);
	CHECK(skeleton->get_bone_name(0) == String("Shoulder"));
	CHECK(skeleton->get_bone_name(1) == String("Elbow"));
	CHECK(skeleton->get_bone_name(2) == String("Hand"));
	CHECK(skeleton->get_bone_parent(0) == -1);
	CHECK(skeleton->get_bone_parent(1) == 0);
	CHECK(skeleton->get_bone_parent(2) == 1);

	const Transform3D shoulder_rest = skeleton->get_bone_rest(0);
	const Transform3D elbow_rest = skeleton->get_bone_rest(1);
	const Transform3D hand_rest = skeleton->get_bone_rest(2);
	CHECK(shoulder_rest.origin.is_equal_approx(Vector3(0.0f, 0.0f, 0.0f)));
	CHECK(elbow_rest.origin.is_equal_approx(Vector3(0.0f, 0.0f, 2.0f)));
	CHECK(hand_rest.origin.is_equal_approx(Vector3(0.0f, 0.0f, 2.0f)));

	CHECK((String)skeleton->get_bone_meta(1, StringName("usd_joint_path")) == String("Shoulder/Elbow"));
	CHECK((String)skeleton->get_bone_meta(2, StringName("usd_joint_parent_path")) == String("Shoulder/Elbow"));

	Dictionary skeleton_metadata = skeleton->get_meta(StringName("usd"), Dictionary());
	CHECK((String)skeleton_metadata.get("usd:type_name", String()) == String("Skeleton"));
	CHECK((String)skeleton_metadata.get("usd:skeleton_mapping", String()) == String("skeleton3d_bones"));
	CHECK((int)skeleton_metadata.get("usd:skeleton_joint_count", 0) == 3);
	CHECK((bool)skeleton_metadata.get("usd:skeleton_has_rest_transforms", false));
	CHECK((bool)skeleton_metadata.get("usd:skeleton_has_bind_transforms", true));

	Array animation_sources = skeleton_metadata.get("usd:animation_sources", Array());
	REQUIRE(animation_sources.size() == 1);
	CHECK((String)animation_sources[0] == String("/Model/Skel/Anim1"));

	AnimationPlayer *player = nullptr;
	for (int i = 0; i < root->get_child_count(); i++) {
		player = Object::cast_to<AnimationPlayer>(root->get_child(i));
		if (player != nullptr) {
			break;
		}
	}
	REQUIRE(player != nullptr);
	REQUIRE(player->has_animation("Anim1"));

	Ref<Animation> animation = player->get_animation("Anim1");
	REQUIRE(animation.is_valid());
	CHECK(animation->get_length() == doctest::Approx(9.0f / 24.0f));
	CHECK(animation->get_track_count() == 1);
	CHECK(String(animation->track_get_path(0)) == String("Model/Skel:Elbow"));
	CHECK(animation->track_get_type(0) == Animation::TYPE_ROTATION_3D);
	CHECK(animation->track_get_key_count(0) == 2);

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Round-trip a baked UsdSkelAnimation joint track through USDA save") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/skeleton_basic.usda");
	const String save_path = TestUtils::get_temp_path("usd_skeleton_roundtrip_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD skeleton source load failed.");
	REQUIRE(loaded_scene.is_valid());
	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);

	Ref<PackedScene> reloaded_scene = ResourceLoader::load(save_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD skeleton round-trip reload failed.");
	REQUIRE(reloaded_scene.is_valid());

	Node *root = reloaded_scene->instantiate();
	REQUIRE(root != nullptr);

	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(_find_prim_node(root, "/Model/Skel"));
	REQUIRE(skeleton != nullptr);
	CHECK(skeleton->get_bone_count() == 3);
	CHECK(skeleton->get_bone_name(1) == String("Elbow"));

	AnimationPlayer *player = nullptr;
	for (int i = 0; i < root->get_child_count(); i++) {
		player = Object::cast_to<AnimationPlayer>(root->get_child(i));
		if (player != nullptr) {
			break;
		}
	}
	REQUIRE(player != nullptr);
	REQUIRE(player->has_animation("Anim1"));

	Ref<Animation> animation = player->get_animation("Anim1");
	REQUIRE(animation.is_valid());
	CHECK(animation->get_track_count() == 1);
	CHECK(animation->track_get_type(0) == Animation::TYPE_ROTATION_3D);
	CHECK(String(animation->track_get_path(0)) == String("Model/Skel:Elbow"));
	CHECK(animation->track_get_key_count(0) == 2);

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Bind a skinned USD mesh to the imported Skeleton3D") {
	const String usd_path = TestUtils::get_data_path("usd/skeleton_skin_basic.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD skinned mesh load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() == 1);

	Node3D *scene_root = Object::cast_to<Node3D>(root->get_child(0));
	REQUIRE(scene_root != nullptr);
	REQUIRE(scene_root->get_child_count() == 2);

	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(scene_root->get_child(0));
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(scene_root->get_child(1));
	REQUIRE(skeleton != nullptr);
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());

	Dictionary mesh_metadata = mesh_instance->get_meta(StringName("usd"), Dictionary());
	CHECK((String)mesh_metadata.get("usd:skel_skeleton_path", String()) == String("/Model/Skel"));

	Ref<Skin> skin = mesh_instance->get_skin();
	REQUIRE(skin.is_valid());
	CHECK(mesh_instance->get_node_or_null(mesh_instance->get_skeleton_path()) == skeleton);
	CHECK(skin->get_bind_count() == skeleton->get_bone_count());

	Array arrays = mesh_instance->get_mesh()->surface_get_arrays(0);
	REQUIRE(arrays.size() == Mesh::ARRAY_MAX);

	PackedInt32Array bones = arrays[Mesh::ARRAY_BONES];
	PackedFloat32Array weights = arrays[Mesh::ARRAY_WEIGHTS];
	CHECK(bones.size() == 12);
	CHECK(weights.size() == 12);
	CHECK(bones[0] == 0);
	CHECK(weights[0] == doctest::Approx(1.0f));
	CHECK(bones[4] == 1);
	CHECK(weights[4] == doctest::Approx(1.0f));

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Bind a skinned USD points prim to the imported Skeleton3D") {
	const String usd_path = TestUtils::get_data_path("usd/skeleton_skin_basic.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD skinned points load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);

	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(_find_prim_node(root, "/Model/Skel"));
	MeshInstance3D *points_instance = Object::cast_to<MeshInstance3D>(_find_prim_node(root, "/Model/Tips"));
	REQUIRE(skeleton != nullptr);
	REQUIRE(points_instance != nullptr);
	REQUIRE(points_instance->get_mesh().is_valid());
	CHECK(points_instance->get_mesh()->surface_get_primitive_type(0) == Mesh::PRIMITIVE_POINTS);

	Dictionary points_metadata = points_instance->get_meta(StringName("usd"), Dictionary());
	CHECK((String)points_metadata.get("usd:points_mapping", String()) == String("mesh_points"));
	CHECK((int)points_metadata.get("usd:point_count", 0) == 3);
	CHECK((String)points_metadata.get("usd:skel_skeleton_path", String()) == String("/Model/Skel"));

	Ref<Skin> skin = points_instance->get_skin();
	REQUIRE(skin.is_valid());
	CHECK(points_instance->get_node_or_null(points_instance->get_skeleton_path()) == skeleton);

	Array arrays = points_instance->get_mesh()->surface_get_arrays(0);
	REQUIRE(arrays.size() == Mesh::ARRAY_MAX);
	PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
	PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];
	PackedInt32Array bones = arrays[Mesh::ARRAY_BONES];
	PackedFloat32Array weights = arrays[Mesh::ARRAY_WEIGHTS];
	CHECK(vertices.size() == 3);
	CHECK(colors.size() == 3);
	CHECK(bones.size() == 12);
	CHECK(weights.size() == 12);
	CHECK(bones[0] == 0);
	CHECK(weights[0] == doctest::Approx(1.0f));
	CHECK(bones[4] == 1);
	CHECK(weights[4] == doctest::Approx(1.0f));

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Preserve 8-weight USD skinning on imported meshes") {
	const String usd_path = TestUtils::get_data_path("usd/skeleton_eight_weights.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD eight-weight skinning load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(_find_prim_node(root, "/Model/Ribbon"));
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(_find_prim_node(root, "/Model/Skel"));
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(skeleton != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());
	CHECK((mesh_instance->get_mesh()->surface_get_format(0) & Mesh::ARRAY_FLAG_USE_8_BONE_WEIGHTS) != 0);

	Array arrays = mesh_instance->get_mesh()->surface_get_arrays(0);
	REQUIRE(arrays.size() == Mesh::ARRAY_MAX);
	PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
	PackedInt32Array bones = arrays[Mesh::ARRAY_BONES];
	PackedFloat32Array weights = arrays[Mesh::ARRAY_WEIGHTS];
	CHECK(vertices.size() == 3);
	CHECK(bones.size() == 24);
	CHECK(weights.size() == 24);

	float total_weight = 0.0f;
	for (int i = 0; i < 8; i++) {
		total_weight += weights[i];
	}
	CHECK(total_weight == doctest::Approx(1.0f));
	CHECK(bones[0] == 0);
	CHECK(bones[1] == 1);
	CHECK(bones[2] == 2);
	CHECK(bones[3] == 3);

	Ref<Skin> skin = mesh_instance->get_skin();
	REQUIRE(skin.is_valid());
	CHECK(mesh_instance->get_node_or_null(mesh_instance->get_skeleton_path()) == skeleton);

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Import multiple SkelAnimation clips including a mixed joint-and-blendshape clip") {
	const String usd_path = TestUtils::get_data_path("usd/skeleton_multi_anim_blendshape.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD multi-clip rigging load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);

	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(_find_prim_node(root, "/root/Actor/Skel"));
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(_find_prim_node(root, "/root/Actor/Body"));
	REQUIRE(skeleton != nullptr);
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());

	AnimationPlayer *player = nullptr;
	for (int i = 0; i < root->get_child_count(); i++) {
		player = Object::cast_to<AnimationPlayer>(root->get_child(i));
		if (player != nullptr) {
			break;
		}
	}
	REQUIRE(player != nullptr);
	REQUIRE(player->has_animation("Rotate"));
	REQUIRE(player->has_animation("Combo"));

	Ref<Animation> rotate = player->get_animation("Rotate");
	Ref<Animation> combo = player->get_animation("Combo");
	REQUIRE(rotate.is_valid());
	REQUIRE(combo.is_valid());

	bool found_combo_rotation_track = false;
	bool found_combo_blend_shape_track = false;
	for (int track_index = 0; track_index < combo->get_track_count(); track_index++) {
		if (combo->track_get_type(track_index) == Animation::TYPE_ROTATION_3D &&
				String(combo->track_get_path(track_index)) == String("root/Actor/Skel:joint1")) {
			found_combo_rotation_track = true;
		}
		if (combo->track_get_type(track_index) == Animation::TYPE_BLEND_SHAPE &&
				String(combo->track_get_path(track_index)) == String("root/Actor/Body:Smile")) {
			found_combo_blend_shape_track = true;
		}
	}
	CHECK(found_combo_rotation_track);
	CHECK(found_combo_blend_shape_track);

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Import USD blend shapes and bake blendShapeWeights animation tracks") {
	const String usd_path = TestUtils::get_data_path("usd/blend_shape_basic.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD blend shape load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(_find_prim_node(root, "/root/Plane/Plane"));
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(_find_prim_node(root, "/root/Plane/Skel"));
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(skeleton != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());
	CHECK(mesh_instance->get_blend_shape_count() == 2);
	CHECK(mesh_instance->find_blend_shape_by_name(StringName("Key_1")) == 0);
	CHECK(mesh_instance->find_blend_shape_by_name(StringName("Key_1__inbetween__HalfKey")) == 1);

	Dictionary mesh_metadata = mesh_instance->get_meta(StringName("usd"), Dictionary());
	CHECK((String)mesh_metadata.get("usd:blend_shape_mapping", String()) == String("array_mesh_relative_piecewise"));
	Dictionary blend_shape_has_normal_offsets = mesh_metadata.get("usd:blend_shape_has_normal_offsets", Dictionary());
	CHECK((bool)blend_shape_has_normal_offsets.get("Key_1", false));
	Dictionary blend_shape_channels = mesh_metadata.get("usd:blend_shape_channels", Dictionary());
	REQUIRE(blend_shape_channels.has("Key_1"));
	Array key_channels = blend_shape_channels["Key_1"];
	REQUIRE(key_channels.size() == 2);
	Dictionary blend_shape_inbetweens = mesh_metadata.get("usd:blend_shape_inbetweens", Dictionary());
	REQUIRE(blend_shape_inbetweens.has("Key_1"));
	Array key_inbetweens = blend_shape_inbetweens["Key_1"];
	REQUIRE(key_inbetweens.size() == 1);
	Dictionary half_key = key_inbetweens[0];
	CHECK((String)half_key.get("name", String()) == String("HalfKey"));
	CHECK((double)half_key.get("weight", 0.0) == doctest::Approx(0.5));
	CHECK((int)half_key.get("offset_count", 0) == 4);
	CHECK((int)half_key.get("normal_offset_count", 0) == 4);

	TypedArray<Array> blend_shape_arrays = mesh_instance->get_mesh()->surface_get_blend_shape_arrays(0);
	REQUIRE(blend_shape_arrays.size() == 2);
	Array blend_shape_surface = blend_shape_arrays[0];
	REQUIRE(blend_shape_surface.size() == Mesh::ARRAY_MAX);
	PackedVector3Array blend_shape_vertices = blend_shape_surface[Mesh::ARRAY_VERTEX];
	PackedVector3Array blend_shape_normals = blend_shape_surface[Mesh::ARRAY_NORMAL];
	CHECK(blend_shape_vertices.size() == 6);
	CHECK(blend_shape_vertices[0].z == doctest::Approx(0.5f));
	CHECK(blend_shape_normals.size() == 6);
	CHECK(blend_shape_normals[0].z == doctest::Approx(0.25f));
	Array inbetween_blend_shape_surface = blend_shape_arrays[1];
	REQUIRE(inbetween_blend_shape_surface.size() == Mesh::ARRAY_MAX);
	PackedVector3Array inbetween_blend_shape_vertices = inbetween_blend_shape_surface[Mesh::ARRAY_VERTEX];
	CHECK(inbetween_blend_shape_vertices.size() == 6);
	CHECK(inbetween_blend_shape_vertices[0].z == doctest::Approx(0.2f));

	AnimationPlayer *player = nullptr;
	for (int i = 0; i < root->get_child_count(); i++) {
		player = Object::cast_to<AnimationPlayer>(root->get_child(i));
		if (player != nullptr) {
			break;
		}
	}
	REQUIRE(player != nullptr);
	REQUIRE(player->has_animation("Anim"));
	Ref<Animation> animation = player->get_animation("Anim");
	REQUIRE(animation.is_valid());

	bool found_primary_blend_shape_track = false;
	bool found_inbetween_blend_shape_track = false;
	for (int track_index = 0; track_index < animation->get_track_count(); track_index++) {
		if (animation->track_get_type(track_index) == Animation::TYPE_BLEND_SHAPE &&
				String(animation->track_get_path(track_index)) == String("root/Plane/Plane:Key_1")) {
			found_primary_blend_shape_track = true;
			CHECK(animation->track_get_key_count(track_index) == 5);
			CHECK((double)animation->track_get_key_value(track_index, 2) == doctest::Approx(0.5));
		}
		if (animation->track_get_type(track_index) == Animation::TYPE_BLEND_SHAPE &&
				String(animation->track_get_path(track_index)) == String("root/Plane/Plane:Key_1__inbetween__HalfKey")) {
			found_inbetween_blend_shape_track = true;
			CHECK(animation->track_get_key_count(track_index) == 5);
			CHECK((double)animation->track_get_key_value(track_index, 1) == doctest::Approx(1.0));
			CHECK((double)animation->track_get_key_value(track_index, 2) == doctest::Approx(0.5));
		}
	}
	CHECK(found_primary_blend_shape_track);
	CHECK(found_inbetween_blend_shape_track);

	memdelete(root);
}

#ifdef TOOLS_ENABLED
TEST_CASE("[SceneTree][USD] UsdSceneFormatImporter exposes structured variant import options") {
	const String usd_path = TestUtils::get_data_path("usd/variant_stage.usda");

	Ref<UsdSceneFormatImporter> importer;
	importer.instantiate();
	REQUIRE(importer.is_valid());

	List<ResourceImporter::ImportOption> options;
	importer->get_import_options(usd_path, &options);

	const ResourceImporter::ImportOption *legacy_json = _find_import_option(options, "usd/variant_selections");
	REQUIRE(legacy_json != nullptr);
	CHECK((legacy_json->option.usage & PROPERTY_USAGE_NO_EDITOR) != 0);
	CHECK(bool(importer->get_option_visibility(usd_path, "PackedScene", "usd/variant_selections", HashMap<StringName, Variant>())) == false);

	const ResourceImporter::ImportOption *modeling_variant = _find_import_option(options, "usd/variants/Model/modelingVariant");
	REQUIRE(modeling_variant != nullptr);
	CHECK(modeling_variant->option.type == Variant::STRING);
	CHECK(modeling_variant->option.hint == PROPERTY_HINT_ENUM);
	CHECK(String(modeling_variant->option.hint_string).contains("red"));
	CHECK(String(modeling_variant->option.hint_string).contains("blue"));
	CHECK(String(modeling_variant->default_value) == "red");

	const ResourceImporter::ImportOption *nested_detail = _find_import_option(options, "usd/variants/Model/Nested/detail");
	REQUIRE(nested_detail != nullptr);
	CHECK(nested_detail->option.type == Variant::STRING);
	CHECK(nested_detail->option.hint == PROPERTY_HINT_ENUM);
	CHECK(String(nested_detail->option.hint_string).contains("cube"));
	CHECK(String(nested_detail->option.hint_string).contains("sphere"));
}

TEST_CASE("[SceneTree][USD] UsdSceneFormatImporter bakes a variant USD file into a static scene root") {
	const String usd_path = TestUtils::get_data_path("usd/variant_stage.usda");

	Ref<UsdSceneFormatImporter> importer;
	importer.instantiate();
	REQUIRE(importer.is_valid());

	HashMap<StringName, Variant> options;
	Error err = OK;
	Node *root = importer->import_scene(usd_path, EditorSceneFormatImporter::IMPORT_SCENE, options, nullptr, &err);
	REQUIRE_MESSAGE(err == OK, "UsdSceneFormatImporter failed to import the default variant stage.");
	REQUIRE(root != nullptr);
	CHECK(Object::cast_to<UsdStageInstance>(root) == nullptr);
	CHECK(root->get_node_or_null(NodePath("_Generated")) == nullptr);
	CHECK(_find_prim_node(root, "/Model/RedCube") != nullptr);
	CHECK(_find_prim_node(root, "/Model/BlueSphere") == nullptr);
	CHECK(root->has_meta(StringName("usd:importer_warnings")) == false);

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] UsdSceneFormatImporter applies structured variant overrides and prefers them over legacy JSON") {
	const String usd_path = TestUtils::get_data_path("usd/variant_stage.usda");

	Ref<UsdSceneFormatImporter> importer;
	importer.instantiate();
	REQUIRE(importer.is_valid());

	HashMap<StringName, Variant> options;
	options.insert(StringName("usd/variant_selections"), String("{\"/Model\":{\"modelingVariant\":\"red\"},\"/Model/Nested\":{\"detail\":\"cube\"}}"));
	options.insert(StringName("usd/variants/Model/modelingVariant"), String("blue"));
	options.insert(StringName("usd/variants/Model/Nested/detail"), String("sphere"));

	Error err = OK;
	Node *root = importer->import_scene(usd_path, EditorSceneFormatImporter::IMPORT_SCENE, options, nullptr, &err);
	REQUIRE_MESSAGE(err == OK, "UsdSceneFormatImporter failed to import the overridden variant stage.");
	REQUIRE(root != nullptr);
	CHECK(Object::cast_to<UsdStageInstance>(root) == nullptr);
	CHECK(_find_prim_node(root, "/Model/RedCube") == nullptr);
	CHECK(_find_prim_node(root, "/Model/BlueSphere") != nullptr);
	CHECK(_find_prim_node(root, "/Model/Nested/NestedSphere") != nullptr);
	CHECK(_find_prim_node(root, "/Model/Nested/NestedCube") == nullptr);
	CHECK(root->has_meta(StringName("usd:importer_variant_selections")) == false);

	memdelete(root);
}
#endif

TEST_CASE("[SceneTree][USD] Import point-based USD blend shapes and bake blendShapeWeights animation tracks") {
	const String usd_path = TestUtils::get_data_path("usd/points_blend_shape_basic.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD point blend shape load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);

	MeshInstance3D *points_instance = Object::cast_to<MeshInstance3D>(_find_prim_node(root, "/root/Cloud/Cloud"));
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(_find_prim_node(root, "/root/Cloud/Skel"));
	REQUIRE(points_instance != nullptr);
	REQUIRE(skeleton != nullptr);
	REQUIRE(points_instance->get_mesh().is_valid());
	CHECK(points_instance->get_mesh()->surface_get_primitive_type(0) == Mesh::PRIMITIVE_POINTS);
	CHECK(points_instance->get_blend_shape_count() == 2);
	CHECK(points_instance->find_blend_shape_by_name(StringName("Puff")) == 0);
	CHECK(points_instance->find_blend_shape_by_name(StringName("Puff__inbetween__HalfPuff")) == 1);

	Dictionary points_metadata = points_instance->get_meta(StringName("usd"), Dictionary());
	CHECK((String)points_metadata.get("usd:points_mapping", String()) == String("mesh_points"));
	CHECK((String)points_metadata.get("usd:blend_shape_mapping", String()) == String("array_points_relative_piecewise"));

	TypedArray<Array> blend_shape_arrays = points_instance->get_mesh()->surface_get_blend_shape_arrays(0);
	REQUIRE(blend_shape_arrays.size() == 2);
	Array primary_surface = blend_shape_arrays[0];
	REQUIRE(primary_surface.size() == Mesh::ARRAY_MAX);
	PackedVector3Array primary_vertices = primary_surface[Mesh::ARRAY_VERTEX];
	CHECK(primary_vertices.size() == 4);
	CHECK(primary_vertices[0].z == doctest::Approx(0.25f));

	Array inbetween_surface = blend_shape_arrays[1];
	REQUIRE(inbetween_surface.size() == Mesh::ARRAY_MAX);
	PackedVector3Array inbetween_vertices = inbetween_surface[Mesh::ARRAY_VERTEX];
	CHECK(inbetween_vertices.size() == 4);
	CHECK(inbetween_vertices[3].z == doctest::Approx(0.15f));

	AnimationPlayer *player = nullptr;
	for (int i = 0; i < root->get_child_count(); i++) {
		player = Object::cast_to<AnimationPlayer>(root->get_child(i));
		if (player != nullptr) {
			break;
		}
	}
	REQUIRE(player != nullptr);
	REQUIRE(player->has_animation("Anim"));
	Ref<Animation> animation = player->get_animation("Anim");
	REQUIRE(animation.is_valid());

	bool found_primary_track = false;
	bool found_inbetween_track = false;
	for (int track_index = 0; track_index < animation->get_track_count(); track_index++) {
		if (animation->track_get_type(track_index) == Animation::TYPE_BLEND_SHAPE &&
				String(animation->track_get_path(track_index)) == String("root/Cloud/Cloud:Puff")) {
			found_primary_track = true;
		}
		if (animation->track_get_type(track_index) == Animation::TYPE_BLEND_SHAPE &&
				String(animation->track_get_path(track_index)) == String("root/Cloud/Cloud:Puff__inbetween__HalfPuff")) {
			found_inbetween_track = true;
		}
	}
	CHECK(found_primary_track);
	CHECK(found_inbetween_track);

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Preserve authored SkelAnimation sample domains even when some channels bake to no visible tracks") {
	const String usd_path = TestUtils::get_data_path("usd/skel_animation_sparsity.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD sparse SkelAnimation load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);

	AnimationPlayer *player = nullptr;
	for (int i = 0; i < root->get_child_count(); i++) {
		player = Object::cast_to<AnimationPlayer>(root->get_child(i));
		if (player != nullptr) {
			break;
		}
	}
	REQUIRE(player != nullptr);
	REQUIRE(player->has_animation("Anim"));

	Ref<Animation> animation = player->get_animation("Anim");
	REQUIRE(animation.is_valid());
	CHECK(animation->get_track_count() == 1);
	if (animation->get_track_count() == 1) {
		CHECK(animation->track_get_type(0) == Animation::TYPE_ROTATION_3D);
		CHECK(String(animation->track_get_path(0)) == String("root/Model/Skel:joint1"));
		CHECK(animation->track_get_key_count(0) >= 2);
	}

	Dictionary animation_metadata = animation->get_meta(StringName("usd"), Dictionary());
	CHECK((bool)animation_metadata.get("usd:has_authored_translations", false));
	CHECK((bool)animation_metadata.get("usd:has_authored_rotations", false));
	CHECK((bool)animation_metadata.get("usd:has_authored_scales", false));
	CHECK((bool)animation_metadata.get("usd:has_authored_blend_shape_weights", false));
	CHECK((bool)animation_metadata.get("usd:translations_constant", true) == false);
	CHECK((bool)animation_metadata.get("usd:scales_constant", true) == false);
	CHECK((bool)animation_metadata.get("usd:blend_shape_weights_constant", true) == false);

	Array translation_time_codes = animation_metadata.get("usd:translation_time_codes", Array());
	Array rotation_time_codes = animation_metadata.get("usd:rotation_time_codes", Array());
	Array scale_time_codes = animation_metadata.get("usd:scale_time_codes", Array());
	Array blend_shape_weight_time_codes = animation_metadata.get("usd:blend_shape_weight_time_codes", Array());
	REQUIRE(translation_time_codes.size() == 2);
	REQUIRE(rotation_time_codes.size() == 2);
	REQUIRE(scale_time_codes.size() == 2);
	REQUIRE(blend_shape_weight_time_codes.size() == 2);
	CHECK((double)translation_time_codes[0] == doctest::Approx(1.0));
	CHECK((double)translation_time_codes[1] == doctest::Approx(9.0));
	CHECK((double)rotation_time_codes[0] == doctest::Approx(5.0));
	CHECK((double)rotation_time_codes[1] == doctest::Approx(10.0));
	CHECK((double)scale_time_codes[0] == doctest::Approx(3.0));
	CHECK((double)scale_time_codes[1] == doctest::Approx(7.0));
	CHECK((double)blend_shape_weight_time_codes[0] == doctest::Approx(2.0));
	CHECK((double)blend_shape_weight_time_codes[1] == doctest::Approx(8.0));

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Add preview sun and environment when a stage has no authored lights") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(1);

	const String usd_path = TestUtils::get_data_path("usd/basic.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD preview lighting load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->is_class("Node3D"));
	REQUIRE(root->get_child_count() == 3);

	Node *authored_root = root->get_child(0);
	REQUIRE(authored_root != nullptr);
	CHECK(authored_root->get_name() == "Root");

	WorldEnvironment *world_environment = Object::cast_to<WorldEnvironment>(root->get_child(1));
	REQUIRE(world_environment != nullptr);
	CHECK(world_environment->get_name() == "USDPreviewEnvironment");
	REQUIRE(world_environment->get_environment().is_valid());
	CHECK(world_environment->get_environment()->get_background() == Environment::BG_COLOR);

	DirectionalLight3D *preview_sun = Object::cast_to<DirectionalLight3D>(root->get_child(2));
	REQUIRE(preview_sun != nullptr);
	CHECK(preview_sun->get_name() == "USDPreviewSun");
	CHECK(preview_sun->has_shadow());

	Dictionary root_metadata = root->get_meta(StringName("usd"), Dictionary());
	CHECK((bool)root_metadata.get("usd:has_authored_lights", true) == false);
	CHECK((bool)root_metadata.get("usd:has_preview_lighting", false));
	CHECK((String)root_metadata.get("usd:preview_lighting_mode", String()) == String("when_missing"));

	Dictionary environment_metadata = world_environment->get_meta(StringName("usd"), Dictionary());
	CHECK((bool)environment_metadata.get("usd:generated_preview", false));
	CHECK((String)environment_metadata.get("usd:generated_preview_kind", String()) == String("world_environment"));

	Dictionary light_metadata = preview_sun->get_meta(StringName("usd"), Dictionary());
	CHECK((bool)light_metadata.get("usd:generated_preview", false));
	CHECK((String)light_metadata.get("usd:generated_preview_kind", String()) == String("directional_light"));

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Respect preview lighting project setting overrides") {
	const String no_light_stage = TestUtils::get_data_path("usd/basic.usda");
	const String authored_light_stage = TestUtils::get_data_path("usd/emissive_and_lights.usda");

	{
		PreviewLightingModeScope preview_lighting_mode_scope;
		preview_lighting_mode_scope.set(0);

		Error err = OK;
		Ref<PackedScene> packed_scene = ResourceLoader::load(no_light_stage, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
		REQUIRE_MESSAGE(err == OK, "USD preview override load failed for never mode.");
		REQUIRE(packed_scene.is_valid());

		Node *root = packed_scene->instantiate();
		REQUIRE(root != nullptr);
		REQUIRE(root->get_child_count() == 1);

		Dictionary root_metadata = root->get_meta(StringName("usd"), Dictionary());
		CHECK((bool)root_metadata.get("usd:has_authored_lights", true) == false);
		CHECK((bool)root_metadata.get("usd:has_preview_lighting", true) == false);
		CHECK((String)root_metadata.get("usd:preview_lighting_mode", String()) == String("never"));

		memdelete(root);
	}

	{
		PreviewLightingModeScope preview_lighting_mode_scope;
		preview_lighting_mode_scope.set(2);

		Error err = OK;
		Ref<PackedScene> packed_scene = ResourceLoader::load(authored_light_stage, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
		REQUIRE_MESSAGE(err == OK, "USD preview override load failed for always mode.");
		REQUIRE(packed_scene.is_valid());

		Node *root = packed_scene->instantiate();
		REQUIRE(root != nullptr);
		REQUIRE(root->get_child_count() == 3);
		REQUIRE(Object::cast_to<WorldEnvironment>(root->get_child(1)) != nullptr);
		REQUIRE(Object::cast_to<DirectionalLight3D>(root->get_child(2)) != nullptr);

		Dictionary root_metadata = root->get_meta(StringName("usd"), Dictionary());
		CHECK((bool)root_metadata.get("usd:has_authored_lights", false));
		CHECK((bool)root_metadata.get("usd:has_preview_lighting", false));
		CHECK((String)root_metadata.get("usd:preview_lighting_mode", String()) == String("always"));

		memdelete(root);
	}
}

TEST_CASE("[SceneTree][USD] Preserve stage correction on resetXformStack nodes") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String usd_path = TestUtils::get_data_path("usd/reset_stage_transform.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD reset-stage transform load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() == 1);

	Node3D *scene_root = Object::cast_to<Node3D>(root);
	REQUIRE(scene_root != nullptr);
	CHECK(scene_root->get_transform().basis.get_scale().x == doctest::Approx(0.01f));
	CHECK(scene_root->get_transform().basis.get_scale().y == doctest::Approx(0.01f));
	CHECK(scene_root->get_transform().basis.get_scale().z == doctest::Approx(0.01f));

	Node3D *usd_root = Object::cast_to<Node3D>(root->get_child(0));
	REQUIRE(usd_root != nullptr);
	REQUIRE(usd_root->get_child_count() == 1);

	Node3D *parent = Object::cast_to<Node3D>(usd_root->get_child(0));
	REQUIRE(parent != nullptr);
	REQUIRE(parent->get_child_count() == 1);

	Node3D *reset_branch = Object::cast_to<Node3D>(parent->get_child(0));
	REQUIRE(reset_branch != nullptr);
	CHECK(reset_branch->is_set_as_top_level());

	Dictionary reset_metadata = reset_branch->get_meta(StringName("usd"), Dictionary());
	CHECK((bool)reset_metadata.get("usd:resets_xform_stack", false));

	const Transform3D corrected_transform = reset_branch->get_transform();
	CHECK(corrected_transform.origin.x == doctest::Approx(0.0f));
	CHECK(corrected_transform.origin.y == doctest::Approx(2.0f));
	CHECK(corrected_transform.origin.z == doctest::Approx(0.0f).epsilon(0.0001f));
	CHECK(corrected_transform.basis.determinant() < 0.0f);

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Save a simple Godot PackedScene to USDA and load it back") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	Node3D *scene_root = memnew(Node3D);
	scene_root->set_name("Root");

	MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name("Triangle");
	mesh_instance->set_mesh(_make_test_triangle_mesh());
	scene_root->add_child(mesh_instance);
	mesh_instance->set_owner(scene_root);

	Camera3D *camera = memnew(Camera3D);
	camera->set_name("Camera");
	camera->set_perspective(60.0f, 0.1f, 100.0f);
	scene_root->add_child(camera);
	camera->set_owner(scene_root);

	DirectionalLight3D *sun = memnew(DirectionalLight3D);
	sun->set_name("Sun");
	sun->set_color(Color(1.0f, 0.95f, 0.9f));
	sun->set_param(Light3D::PARAM_INTENSITY, 1234.0f);
	scene_root->add_child(sun);
	sun->set_owner(scene_root);

	Ref<PackedScene> source_scene;
	source_scene.instantiate();
	REQUIRE(source_scene->pack(scene_root) == OK);

	const String save_path = TestUtils::get_temp_path("usd_save_roundtrip.usda");
	REQUIRE(ResourceSaver::save(source_scene, save_path) == OK);
	REQUIRE(FileAccess::exists(save_path));

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(save_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD saver round-trip load failed.");
	REQUIRE(loaded_scene.is_valid());

	Node *loaded_root = loaded_scene->instantiate();
	REQUIRE(loaded_root != nullptr);
	REQUIRE(loaded_root->get_child_count() == 1);

	Dictionary root_metadata = loaded_root->get_meta(StringName("usd"), Dictionary());
	CHECK((bool)root_metadata.get("usd:has_authored_lights", false));
	CHECK((bool)root_metadata.get("usd:has_preview_lighting", true) == false);

	Node *usd_root = loaded_root->get_child(0);
	REQUIRE(usd_root != nullptr);
	CHECK(usd_root->get_name() == "Root");
	REQUIRE(usd_root->get_child_count() == 3);
	REQUIRE(Object::cast_to<MeshInstance3D>(usd_root->get_child(0)) != nullptr);
	REQUIRE(Object::cast_to<Camera3D>(usd_root->get_child(1)) != nullptr);
	REQUIRE(Object::cast_to<DirectionalLight3D>(usd_root->get_child(2)) != nullptr);

	memdelete(loaded_root);
	memdelete(scene_root);
}

TEST_CASE("[SceneTree][USD] Save StandardMaterial3D preview properties to USDA and load them back") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	Node3D *scene_root = memnew(Node3D);
	scene_root->set_name("Root");

	Ref<ArrayMesh> mesh = _make_test_triangle_mesh();
	Ref<StandardMaterial3D> source_material;
	source_material.instantiate();
	source_material->set_name("BodyMaterial");
	source_material->set_albedo(Color(0.8f, 0.2f, 0.1f, 0.5f));
	source_material->set_metallic(0.35f);
	source_material->set_roughness(0.65f);
	source_material->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
	source_material->set_emission(Color(0.15f, 0.05f, 0.02f));
	source_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
	source_material->set_alpha_scissor_threshold(0.4f);
	mesh->surface_set_material(0, source_material);

	MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name("Triangle");
	mesh_instance->set_mesh(mesh);
	scene_root->add_child(mesh_instance);
	mesh_instance->set_owner(scene_root);

	Ref<PackedScene> source_scene;
	source_scene.instantiate();
	REQUIRE(source_scene->pack(scene_root) == OK);

	const String save_path = TestUtils::get_temp_path("usd_material_roundtrip.usda");
	REQUIRE(ResourceSaver::save(source_scene, save_path) == OK);
	REQUIRE(FileAccess::exists(save_path));

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(save_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD material saver round-trip load failed.");
	REQUIRE(loaded_scene.is_valid());

	Node *loaded_root = loaded_scene->instantiate();
	REQUIRE(loaded_root != nullptr);
	REQUIRE(loaded_root->get_child_count() == 1);

	MeshInstance3D *loaded_mesh = Object::cast_to<MeshInstance3D>(loaded_root->get_child(0)->get_child(0));
	REQUIRE(loaded_mesh != nullptr);
	REQUIRE(loaded_mesh->get_mesh().is_valid());

	Ref<Material> loaded_material_ref = loaded_mesh->get_mesh()->surface_get_material(0);
	REQUIRE(loaded_material_ref.is_valid());

	BaseMaterial3D *loaded_material = Object::cast_to<BaseMaterial3D>(loaded_material_ref.ptr());
	REQUIRE(loaded_material != nullptr);
	CHECK(loaded_material->get_albedo().r == doctest::Approx(0.8f));
	CHECK(loaded_material->get_albedo().g == doctest::Approx(0.2f));
	CHECK(loaded_material->get_albedo().b == doctest::Approx(0.1f));
	CHECK(loaded_material->get_albedo().a == doctest::Approx(0.5f));
	CHECK(loaded_material->get_metallic() == doctest::Approx(0.35f));
	CHECK(loaded_material->get_roughness() == doctest::Approx(0.65f));
	CHECK(loaded_material->get_feature(BaseMaterial3D::FEATURE_EMISSION));
	CHECK(loaded_material->get_emission().r == doctest::Approx(0.15f));
	CHECK(loaded_material->get_emission().g == doctest::Approx(0.05f));
	CHECK(loaded_material->get_emission().b == doctest::Approx(0.02f));
	CHECK(loaded_material->get_transparency() == BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
	CHECK(loaded_material->get_alpha_scissor_threshold() == doctest::Approx(0.4f));

	memdelete(loaded_root);
	memdelete(scene_root);
}

TEST_CASE("[SceneTree][USD] Save extended UsdPreviewSurface properties to USDA") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	Node3D *scene_root = memnew(Node3D);
	scene_root->set_name("Root");

	Ref<ArrayMesh> mesh = _make_test_triangle_mesh();
	Ref<StandardMaterial3D> source_material;
	source_material.instantiate();
	source_material->set_name("BodyMaterial");
	source_material->set_albedo(Color(0.8f, 0.8f, 0.8f, 0.5f));
	source_material->set_roughness(0.45f);
	source_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	source_material->set_feature(BaseMaterial3D::FEATURE_CLEARCOAT, true);
	source_material->set_clearcoat(0.6f);
	source_material->set_clearcoat_roughness(0.2f);
	source_material->set_feature(BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION, true);
	source_material->set_ao_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_BLUE);

	Error texture_load_error = OK;
	Ref<Texture2D> icon_texture = ResourceLoader::load(TestUtils::get_data_path("images/icon.png"), "Texture2D", ResourceFormatLoader::CACHE_MODE_IGNORE, &texture_load_error);
	REQUIRE_MESSAGE(texture_load_error == OK, "Failed to load icon texture for PreviewSurface save test.");
	REQUIRE(icon_texture.is_valid());
	source_material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, icon_texture);
	source_material->set_texture(BaseMaterial3D::TEXTURE_CLEARCOAT, icon_texture);
	source_material->set_texture(BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, icon_texture);

	Dictionary preview_metadata;
	preview_metadata["usd:preview_surface_use_specular_workflow"] = true;
	preview_metadata["usd:preview_surface_ior"] = 1.5;
	preview_metadata["usd:preview_surface_specular_color"] = Color(0.04f, 0.04f, 0.04f, 1.0f);
	source_material->set_meta(StringName("usd"), preview_metadata);

	mesh->surface_set_material(0, source_material);

	MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name("Triangle");
	mesh_instance->set_mesh(mesh);
	scene_root->add_child(mesh_instance);
	mesh_instance->set_owner(scene_root);

	Ref<PackedScene> source_scene;
	source_scene.instantiate();
	REQUIRE(source_scene->pack(scene_root) == OK);

	const String save_path = TestUtils::get_temp_path("usd_preview_surface_extended_save.usda");
	REQUIRE(ResourceSaver::save(source_scene, save_path) == OK);
	REQUIRE(FileAccess::exists(save_path));

	const String saved_text = FileAccess::get_file_as_string(save_path);
	CHECK(saved_text.contains("inputs:useSpecularWorkflow"));
	CHECK(saved_text.contains("inputs:specularColor"));
	CHECK(saved_text.contains("inputs:ior = 1.5"));
	CHECK(saved_text.contains("inputs:clearcoat = 0.6"));
	CHECK(saved_text.contains("inputs:clearcoatRoughness = 0.2"));
	CHECK(saved_text.contains("inputs:occlusion.connect"));
	CHECK(saved_text.contains("inputs:opacity.connect"));
	CHECK(saved_text.contains("ClearcoatTexture.outputs:r"));
	CHECK(saved_text.contains("ClearcoatTexture.outputs:g"));
	CHECK(saved_text.contains("AlbedoTexture.outputs:a"));

	memdelete(scene_root);
}

TEST_CASE("[SceneTree][USD] Round-trip typed unmapped USD attributes and relationships") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/unmapped_roundtrip.usda");
	const String save_path = TestUtils::get_temp_path("usd_unmapped_roundtrip_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD unmapped-property source load failed.");
	REQUIRE(loaded_scene.is_valid());
	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);

	Ref<PackedScene> reloaded_scene = ResourceLoader::load(save_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD unmapped-property reload failed.");
	REQUIRE(reloaded_scene.is_valid());

	Node *root = reloaded_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() == 1);

	Node *usd_root = root->get_child(0);
	REQUIRE(usd_root != nullptr);

	Dictionary metadata = usd_root->get_meta(StringName("usd"), Dictionary());
	Dictionary unmapped_attributes = metadata.get("usd:unmapped_attributes", Dictionary());
	REQUIRE(unmapped_attributes.has("debugLabel"));
	REQUIRE(unmapped_attributes.has("debugDirection"));
	REQUIRE(unmapped_attributes.has("debugIds"));

	const Dictionary debug_label = unmapped_attributes["debugLabel"];
	CHECK((String)debug_label.get("typed_value_kind", String()) == String("string"));
	CHECK((String)debug_label.get("typed_value", String()) == String("car"));

	const Dictionary debug_direction = unmapped_attributes["debugDirection"];
	CHECK((String)debug_direction.get("typed_value_kind", String()) == String("vector3"));
	CHECK(((Vector3)debug_direction.get("typed_value", Vector3())).is_equal_approx(Vector3(1.0f, 2.0f, 3.0f)));

	const Dictionary debug_ids = unmapped_attributes["debugIds"];
	CHECK((String)debug_ids.get("typed_value_kind", String()) == String("int_array"));
	const Array ids = debug_ids.get("typed_value", Array());
	REQUIRE(ids.size() == 3);
	CHECK((int)ids[0] == 1);
	CHECK((int)ids[1] == 3);
	CHECK((int)ids[2] == 5);

	Dictionary unmapped_relationships = metadata.get("usd:unmapped_relationships", Dictionary());
	REQUIRE(unmapped_relationships.has("debugTarget"));
	const Dictionary debug_target = unmapped_relationships["debugTarget"];
	CHECK((bool)debug_target.get("is_custom", false));
	const Array targets = debug_target.get("targets", Array());
	REQUIRE(targets.size() == 1);
	CHECK((String)targets[0] == String("/Root/Target"));

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Save per-surface materials as USD material subsets and load them back") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	Array first_surface;
	first_surface.resize(Mesh::ARRAY_MAX);
	{
		PackedVector3Array vertices;
		vertices.push_back(Vector3(0.0f, 0.0f, 0.0f));
		vertices.push_back(Vector3(1.0f, 0.0f, 0.0f));
		vertices.push_back(Vector3(0.0f, 1.0f, 0.0f));
		first_surface[Mesh::ARRAY_VERTEX] = vertices;

		PackedVector3Array normals;
		normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
		normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
		normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
		first_surface[Mesh::ARRAY_NORMAL] = normals;

		PackedInt32Array indices;
		indices.push_back(0);
		indices.push_back(1);
		indices.push_back(2);
		first_surface[Mesh::ARRAY_INDEX] = indices;
	}

	Array second_surface;
	second_surface.resize(Mesh::ARRAY_MAX);
	{
		PackedVector3Array vertices;
		vertices.push_back(Vector3(1.0f, 0.0f, 0.0f));
		vertices.push_back(Vector3(1.0f, 1.0f, 0.0f));
		vertices.push_back(Vector3(0.0f, 1.0f, 0.0f));
		second_surface[Mesh::ARRAY_VERTEX] = vertices;

		PackedVector3Array normals;
		normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
		normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
		normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
		second_surface[Mesh::ARRAY_NORMAL] = normals;

		PackedInt32Array indices;
		indices.push_back(0);
		indices.push_back(1);
		indices.push_back(2);
		second_surface[Mesh::ARRAY_INDEX] = indices;
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, first_surface);
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, second_surface);

	Ref<StandardMaterial3D> red_material;
	red_material.instantiate();
	red_material->set_name("Red");
	red_material->set_albedo(Color(0.8f, 0.1f, 0.1f));
	mesh->surface_set_material(0, red_material);

	Ref<StandardMaterial3D> blue_material;
	blue_material.instantiate();
	blue_material->set_name("Blue");
	blue_material->set_albedo(Color(0.1f, 0.1f, 0.8f));
	mesh->surface_set_material(1, blue_material);

	Node3D *scene_root = memnew(Node3D);
	scene_root->set_name("Root");

	MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name("Quad");
	mesh_instance->set_mesh(mesh);
	scene_root->add_child(mesh_instance);
	mesh_instance->set_owner(scene_root);

	Ref<PackedScene> source_scene;
	source_scene.instantiate();
	REQUIRE(source_scene->pack(scene_root) == OK);

	const String save_path = TestUtils::get_temp_path("usd_subset_material_roundtrip.usda");
	REQUIRE(ResourceSaver::save(source_scene, save_path) == OK);

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(save_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD subset material round-trip load failed.");
	REQUIRE(loaded_scene.is_valid());

	Node *loaded_root = loaded_scene->instantiate();
	REQUIRE(loaded_root != nullptr);
	REQUIRE(loaded_root->get_child_count() == 1);

	MeshInstance3D *loaded_mesh = Object::cast_to<MeshInstance3D>(loaded_root->get_child(0)->get_child(0));
	REQUIRE(loaded_mesh != nullptr);
	REQUIRE(loaded_mesh->get_mesh().is_valid());
	CHECK(loaded_mesh->get_mesh()->get_surface_count() == 2);

	BaseMaterial3D *loaded_first_material = Object::cast_to<BaseMaterial3D>(loaded_mesh->get_mesh()->surface_get_material(0).ptr());
	BaseMaterial3D *loaded_second_material = Object::cast_to<BaseMaterial3D>(loaded_mesh->get_mesh()->surface_get_material(1).ptr());
	REQUIRE(loaded_first_material != nullptr);
	REQUIRE(loaded_second_material != nullptr);
	CHECK(loaded_first_material->get_albedo().r == doctest::Approx(0.8f));
	CHECK(loaded_first_material->get_albedo().b == doctest::Approx(0.1f));
	CHECK(loaded_second_material->get_albedo().r == doctest::Approx(0.1f));
	CHECK(loaded_second_material->get_albedo().b == doctest::Approx(0.8f));

	Dictionary mesh_metadata = loaded_mesh->get_meta(StringName("usd"), Dictionary());
	const Array material_bindings = mesh_metadata.get("usd:material_bindings", Array());
	CHECK(material_bindings.size() == 2);

	memdelete(loaded_root);
	memdelete(scene_root);
}

TEST_CASE("[SceneTree][USD] Reuse authored USD materials when multiple surfaces share one Godot material") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	for (int surface_index = 0; surface_index < 3; surface_index++) {
		Array surface;
		surface.resize(Mesh::ARRAY_MAX);

		PackedVector3Array vertices;
		vertices.push_back(Vector3((real_t)surface_index, 0.0f, 0.0f));
		vertices.push_back(Vector3((real_t)surface_index + 0.5f, 0.0f, 0.0f));
		vertices.push_back(Vector3((real_t)surface_index, 0.5f, 0.0f));
		surface[Mesh::ARRAY_VERTEX] = vertices;

		PackedVector3Array normals;
		normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
		normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
		normals.push_back(Vector3(0.0f, 0.0f, 1.0f));
		surface[Mesh::ARRAY_NORMAL] = normals;

		PackedInt32Array indices;
		indices.push_back(0);
		indices.push_back(1);
		indices.push_back(2);
		surface[Mesh::ARRAY_INDEX] = indices;

		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, surface);
	}

	Ref<StandardMaterial3D> shared_material;
	shared_material.instantiate();
	shared_material->set_name("SharedRed");
	shared_material->set_albedo(Color(0.8f, 0.1f, 0.1f));

	Ref<StandardMaterial3D> unique_material;
	unique_material.instantiate();
	unique_material->set_name("UniqueBlue");
	unique_material->set_albedo(Color(0.1f, 0.1f, 0.8f));

	mesh->surface_set_material(0, shared_material);
	mesh->surface_set_material(1, unique_material);
	mesh->surface_set_material(2, shared_material);

	Node3D *scene_root = memnew(Node3D);
	scene_root->set_name("Root");

	MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name("TriStrip");
	mesh_instance->set_mesh(mesh);
	scene_root->add_child(mesh_instance);
	mesh_instance->set_owner(scene_root);

	Ref<PackedScene> source_scene;
	source_scene.instantiate();
	REQUIRE(source_scene->pack(scene_root) == OK);

	const String save_path = TestUtils::get_temp_path("usd_subset_material_reuse.usda");
	REQUIRE(ResourceSaver::save(source_scene, save_path) == OK);

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(save_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD shared subset material round-trip load failed.");
	REQUIRE(loaded_scene.is_valid());

	Node *loaded_root = loaded_scene->instantiate();
	REQUIRE(loaded_root != nullptr);
	REQUIRE(loaded_root->get_child_count() == 1);

	MeshInstance3D *loaded_mesh = Object::cast_to<MeshInstance3D>(loaded_root->get_child(0)->get_child(0));
	REQUIRE(loaded_mesh != nullptr);
	REQUIRE(loaded_mesh->get_mesh().is_valid());
	CHECK(loaded_mesh->get_mesh()->get_surface_count() == 3);

	Dictionary mesh_metadata = loaded_mesh->get_meta(StringName("usd"), Dictionary());
	const Array material_bindings = mesh_metadata.get("usd:material_bindings", Array());
	REQUIRE(material_bindings.size() == 3);
	CHECK((String)material_bindings[0] == (String)material_bindings[2]);
	CHECK((String)material_bindings[0] != (String)material_bindings[1]);

	memdelete(loaded_root);
	memdelete(scene_root);
}

TEST_CASE("[SceneTree][USD] Preserve authored material subset names across USD round-trip") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/named_material_subsets.usda");
	const String save_path = TestUtils::get_temp_path("usd_named_material_subsets_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD named subset source load failed.");
	REQUIRE(loaded_scene.is_valid());
	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);

	Ref<PackedScene> reloaded_scene = ResourceLoader::load(save_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD named subset reload failed.");
	REQUIRE(reloaded_scene.is_valid());

	Node *root = reloaded_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() == 1);

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(root->get_child(0)->get_child(0));
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());
	CHECK(mesh_instance->get_mesh()->get_surface_count() == 2);

	Dictionary mesh_metadata = mesh_instance->get_meta(StringName("usd"), Dictionary());
	const Array subset_descriptions = mesh_metadata.get("usd:material_subsets", Array());
	REQUIRE(subset_descriptions.size() == 2);

	const Dictionary first_description = subset_descriptions[0];
	const Dictionary second_description = subset_descriptions[1];
	CHECK((String)first_description.get("binding_kind", String()) == String("mesh"));
	CHECK((String)second_description.get("binding_kind", String()) == String("subset"));
	CHECK((String)second_description.get("subset_path", String()) == String("/Root/Panel/Trim"));
	CHECK((String)second_description.get("subset_name", String()) == String("Trim"));
	CHECK((String)second_description.get("family_name", String()) == String("materialBind"));
	const Array material_bindings = mesh_metadata.get("usd:material_bindings", Array());
	REQUIRE(material_bindings.size() == 2);
	CHECK((String)material_bindings[0] == String("/Root/Looks/Base"));
	CHECK((String)material_bindings[1] == String("/Root/Looks/Accent"));

	memdelete(root);
}

TEST_CASE("[SceneTree][USD] Preserve subsets that inherit the mesh material binding across USD round-trip") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/inherited_material_subset.usda");
	const String save_path = TestUtils::get_temp_path("usd_inherited_material_subset_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD inherited subset source load failed.");
	REQUIRE(loaded_scene.is_valid());

	Node *loaded_root = loaded_scene->instantiate();
	REQUIRE(loaded_root != nullptr);
	MeshInstance3D *loaded_mesh = Object::cast_to<MeshInstance3D>(loaded_root->get_child(0)->get_child(0));
	REQUIRE(loaded_mesh != nullptr);
	REQUIRE(loaded_mesh->get_mesh().is_valid());
	CHECK(loaded_mesh->get_mesh()->get_surface_count() == 2);

	BaseMaterial3D *first_surface_material = Object::cast_to<BaseMaterial3D>(loaded_mesh->get_mesh()->surface_get_material(0).ptr());
	BaseMaterial3D *second_surface_material = Object::cast_to<BaseMaterial3D>(loaded_mesh->get_mesh()->surface_get_material(1).ptr());
	REQUIRE(first_surface_material != nullptr);
	REQUIRE(second_surface_material != nullptr);
	CHECK(second_surface_material->get_albedo().r == doctest::Approx(first_surface_material->get_albedo().r));
	CHECK(second_surface_material->get_albedo().g == doctest::Approx(first_surface_material->get_albedo().g));
	CHECK(second_surface_material->get_albedo().b == doctest::Approx(first_surface_material->get_albedo().b));

	Dictionary mesh_metadata = loaded_mesh->get_meta(StringName("usd"), Dictionary());
	Array subset_descriptions = mesh_metadata.get("usd:material_subsets", Array());
	REQUIRE(subset_descriptions.size() == 2);
	Dictionary inherited_subset_description = subset_descriptions[1];
	CHECK((String)inherited_subset_description.get("binding_kind", String()) == String("subset"));
	CHECK((bool)inherited_subset_description.get("has_material_binding", true) == false);
	CHECK((String)inherited_subset_description.get("subset_name", String()) == String("Trim"));
	memdelete(loaded_root);

	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);
	Ref<PackedScene> reloaded_scene = ResourceLoader::load(save_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD inherited subset reload failed.");
	REQUIRE(reloaded_scene.is_valid());

	Node *reloaded_root = reloaded_scene->instantiate();
	REQUIRE(reloaded_root != nullptr);
	MeshInstance3D *reloaded_mesh = Object::cast_to<MeshInstance3D>(reloaded_root->get_child(0)->get_child(0));
	REQUIRE(reloaded_mesh != nullptr);

	mesh_metadata = reloaded_mesh->get_meta(StringName("usd"), Dictionary());
	subset_descriptions = mesh_metadata.get("usd:material_subsets", Array());
	REQUIRE(subset_descriptions.size() == 2);
	inherited_subset_description = subset_descriptions[1];
	CHECK((String)inherited_subset_description.get("binding_kind", String()) == String("subset"));
	CHECK((bool)inherited_subset_description.get("has_material_binding", true) == false);
	CHECK((String)inherited_subset_description.get("subset_name", String()) == String("Trim"));

	const Array material_bindings = mesh_metadata.get("usd:material_bindings", Array());
	REQUIRE(material_bindings.size() == 2);
	CHECK((String)material_bindings[0] == String("/Root/Looks/Base"));
	CHECK((String)material_bindings[1] == String("/Root/Looks/Base"));

	memdelete(reloaded_root);
}

TEST_CASE("[SceneTree][USD] Preserve sparse authored subset face indices across USD round-trip") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/sparse_material_subset.usda");
	const String save_path = TestUtils::get_temp_path("usd_sparse_material_subset_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD sparse subset source load failed.");
	REQUIRE(loaded_scene.is_valid());
	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);

	Ref<FileAccess> saved_file = FileAccess::open(save_path, FileAccess::READ);
	REQUIRE(saved_file.is_valid());
	const String saved_text = saved_file->get_as_text();
	CHECK(saved_text.contains("def GeomSubset \"OddFaces\""));
	CHECK(saved_text.contains("int[] indices = [1, 3]"));
	CHECK(saved_text.contains("rel material:binding = </Root/Looks/Accent>"));
}

TEST_CASE("[SceneTree][USD] Preserve generic face geom subsets across USD round-trip") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/generic_face_subset.usda");
	const String save_path = TestUtils::get_temp_path("usd_generic_face_subset_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD generic geom subset source load failed.");
	REQUIRE(loaded_scene.is_valid());
	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);

	Ref<FileAccess> saved_file = FileAccess::open(save_path, FileAccess::READ);
	REQUIRE(saved_file.is_valid());
	const String saved_text = saved_file->get_as_text();
	CHECK(saved_text.contains("def GeomSubset \"Corners\""));
	CHECK(saved_text.contains("uniform token familyName = \"selection\""));
	CHECK(saved_text.contains("int[] indices = [0, 3]"));
}

TEST_CASE("[SceneTree][USD] Preserve generic point geom subsets across USD round-trip") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/point_subset.usda");
	const String save_path = TestUtils::get_temp_path("usd_point_subset_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD point subset source load failed.");
	REQUIRE(loaded_scene.is_valid());
	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);

	Ref<FileAccess> saved_file = FileAccess::open(save_path, FileAccess::READ);
	REQUIRE(saved_file.is_valid());
	const String saved_text = saved_file->get_as_text();
	CHECK(saved_text.contains("def GeomSubset \"PinnedPoints\""));
	CHECK(saved_text.contains("uniform token elementType = \"point\""));
	CHECK(saved_text.contains("uniform token familyName = \"selection\""));
	CHECK(saved_text.contains("int[] indices = [2, 3]"));
}

TEST_CASE("[SceneTree][USD] Preserve generic edge geom subsets across USD round-trip") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/edge_subset.usda");
	const String save_path = TestUtils::get_temp_path("usd_edge_subset_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD edge subset source load failed.");
	REQUIRE(loaded_scene.is_valid());
	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);

	Ref<FileAccess> saved_file = FileAccess::open(save_path, FileAccess::READ);
	REQUIRE(saved_file.is_valid());
	const String saved_text = saved_file->get_as_text();
	CHECK(saved_text.contains("def GeomSubset \"SharedEdge\""));
	CHECK(saved_text.contains("uniform token elementType = \"edge\""));
	CHECK(saved_text.contains("uniform token familyName = \"selection\""));
	CHECK(saved_text.contains("int[] indices = [1, 2, 3, 4]"));
}

TEST_CASE("[SceneTree][USD] Preserve authored references in read-only composition mode") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/composition_reference_source.usda");
	const String save_path = TestUtils::get_temp_path("usd_composition_reference_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD composition reference source load failed.");
	REQUIRE(loaded_scene.is_valid());

	Node *root = loaded_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() == 1);

	Node *usd_root = root->get_child(0);
	REQUIRE(usd_root != nullptr);
	Node *car_node = usd_root->get_child(0);
	REQUIRE(car_node != nullptr);
	Dictionary car_metadata = car_node->get_meta(StringName("usd"), Dictionary());
	CHECK((String)car_metadata.get("usd:composition_preservation_mode", String()) == String("read_only"));
	const Array references = car_metadata.get("usd:references", Array());
	REQUIRE(references.size() == 1);
	const Dictionary reference = references[0];
	CHECK((String)reference.get("asset_path", String()) == String("./composition_ref_target.usda"));
	CHECK((String)reference.get("prim_path", String()) == String("/Asset"));
	memdelete(root);

	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);

	Ref<FileAccess> saved_file = FileAccess::open(save_path, FileAccess::READ);
	REQUIRE(saved_file.is_valid());
	const String saved_text = saved_file->get_as_text();
	CHECK(saved_text.contains("over \"Car\""));
	CHECK(saved_text.contains("references = @./composition_ref_target.usda@</Asset>"));
	CHECK(saved_text.contains("xformOp:transform"));
	CHECK(saved_text.contains("def Mesh \"Geom\"") == false);
}

TEST_CASE("[SceneTree][USD] Preserve authored payloads in read-only composition mode") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/composition_payload_source.usda");
	const String save_path = TestUtils::get_temp_path("usd_composition_payload_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD composition payload source load failed.");
	REQUIRE(loaded_scene.is_valid());

	Node *root = loaded_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() == 1);

	Node *usd_root = root->get_child(0);
	REQUIRE(usd_root != nullptr);
	Node *payload_node = usd_root->get_child(0);
	REQUIRE(payload_node != nullptr);
	Dictionary payload_metadata = payload_node->get_meta(StringName("usd"), Dictionary());
	CHECK((String)payload_metadata.get("usd:composition_preservation_mode", String()) == String("read_only"));
	const Array payloads = payload_metadata.get("usd:payloads", Array());
	REQUIRE(payloads.size() == 1);
	const Dictionary payload = payloads[0];
	CHECK((String)payload.get("asset_path", String()) == String("./composition_payload_target.usda"));
	CHECK((String)payload.get("prim_path", String()) == String("/PayloadAsset"));
	memdelete(root);

	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);

	Ref<FileAccess> saved_file = FileAccess::open(save_path, FileAccess::READ);
	REQUIRE(saved_file.is_valid());
	const String saved_text = saved_file->get_as_text();
	CHECK(saved_text.contains("over \"PayloadCar\""));
	CHECK(saved_text.contains("payload = @./composition_payload_target.usda@</PayloadAsset>"));
	CHECK(saved_text.contains("xformOp:transform"));
	CHECK(saved_text.contains("def Mesh \"PayloadGeom\"") == false);
}

TEST_CASE("[SceneTree][USD] Preserve authored inherits in read-only composition mode") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/composition_inherits_source.usda");
	const String save_path = TestUtils::get_temp_path("usd_composition_inherits_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD composition inherits source load failed.");
	REQUIRE(loaded_scene.is_valid());

	Node *root = loaded_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() == 1);

	Node *usd_root = root->get_child(0);
	REQUIRE(usd_root != nullptr);
	Node *inherited_node = usd_root->get_child(0);
	REQUIRE(inherited_node != nullptr);
	Dictionary inherited_metadata = inherited_node->get_meta(StringName("usd"), Dictionary());
	CHECK((String)inherited_metadata.get("usd:composition_preservation_mode", String()) == String("read_only"));
	const Array inherits = inherited_metadata.get("usd:inherits", Array());
	REQUIRE(inherits.size() == 1);
	CHECK((String)inherits[0] == String("/BaseAsset"));
	memdelete(root);

	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);

	Ref<FileAccess> saved_file = FileAccess::open(save_path, FileAccess::READ);
	REQUIRE(saved_file.is_valid());
	const String saved_text = saved_file->get_as_text();
	CHECK(saved_text.contains("def Xform \"InheritedAsset\""));
	CHECK(saved_text.contains("inherits = </BaseAsset>"));
	CHECK(saved_text.contains("xformOp:transform"));
}

TEST_CASE("[SceneTree][USD] Preserve authored specializes in read-only composition mode") {
	PreviewLightingModeScope preview_lighting_mode_scope;
	preview_lighting_mode_scope.set(0);

	const String source_path = TestUtils::get_data_path("usd/composition_specializes_source.usda");
	const String save_path = TestUtils::get_temp_path("usd_composition_specializes_saved.usda");

	Error err = OK;
	Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD composition specializes source load failed.");
	REQUIRE(loaded_scene.is_valid());

	Node *root = loaded_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->get_child_count() == 1);

	Node *usd_root = root->get_child(0);
	REQUIRE(usd_root != nullptr);
	Node *specialized_node = usd_root->get_child(0);
	REQUIRE(specialized_node != nullptr);
	Dictionary specialized_metadata = specialized_node->get_meta(StringName("usd"), Dictionary());
	CHECK((String)specialized_metadata.get("usd:composition_preservation_mode", String()) == String("read_only"));
	const Array specializes = specialized_metadata.get("usd:specializes", Array());
	REQUIRE(specializes.size() == 1);
	CHECK((String)specializes[0] == String("/BaseAsset"));
	memdelete(root);

	REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);

	Ref<FileAccess> saved_file = FileAccess::open(save_path, FileAccess::READ);
	REQUIRE(saved_file.is_valid());
	const String saved_text = saved_file->get_as_text();
	CHECK(saved_text.contains("def Xform \"SpecializedAsset\""));
	CHECK(saved_text.contains("specializes = </BaseAsset>"));
	CHECK(saved_text.contains("xformOp:transform"));
}

TEST_CASE("[SceneTree][USD] Skip synthetic preview lighting when saving USDA") {
	const String source_path = TestUtils::get_data_path("usd/basic.usda");
	const String save_path = TestUtils::get_temp_path("usd_skip_preview_nodes.usda");

	{
		PreviewLightingModeScope preview_lighting_mode_scope;
		preview_lighting_mode_scope.set(1);

		Error err = OK;
		Ref<PackedScene> loaded_scene = ResourceLoader::load(source_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
		REQUIRE_MESSAGE(err == OK, "USD source load failed before save.");
		REQUIRE(loaded_scene.is_valid());
		REQUIRE(ResourceSaver::save(loaded_scene, save_path) == OK);
	}

	{
		PreviewLightingModeScope preview_lighting_mode_scope;
		preview_lighting_mode_scope.set(0);

		Error err = OK;
		Ref<PackedScene> reloaded_scene = ResourceLoader::load(save_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
		REQUIRE_MESSAGE(err == OK, "USD preview-skip reload failed.");
		REQUIRE(reloaded_scene.is_valid());

		Node *root = reloaded_scene->instantiate();
		REQUIRE(root != nullptr);
		REQUIRE(root->get_child_count() == 1);

		Dictionary root_metadata = root->get_meta(StringName("usd"), Dictionary());
		CHECK((bool)root_metadata.get("usd:has_authored_lights", true) == false);
		CHECK((bool)root_metadata.get("usd:has_preview_lighting", true) == false);

		memdelete(root);
	}
}

TEST_CASE("[SceneTree][USD] Import preview texture channels and UV transforms") {
	const String usd_path = TestUtils::get_data_path("usd/preview_texture_channels.usda");

	Error err = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(usd_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
	REQUIRE_MESSAGE(err == OK, "USD preview texture channel load failed.");
	REQUIRE(packed_scene.is_valid());

	Node *root = packed_scene->instantiate();
	REQUIRE(root != nullptr);
	REQUIRE(root->is_class("Node3D"));
	CHECK(root->get_child_count() >= 1);

	Node *scene_root = root->get_child(0);
	REQUIRE(scene_root != nullptr);
	CHECK(scene_root->get_child_count() == 1);

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(scene_root->get_child(0));
	REQUIRE(mesh_instance != nullptr);
	REQUIRE(mesh_instance->get_mesh().is_valid());

	Ref<Material> surface_material = mesh_instance->get_mesh()->surface_get_material(0);
	REQUIRE(surface_material.is_valid());

	BaseMaterial3D *base_material = Object::cast_to<BaseMaterial3D>(surface_material.ptr());
	REQUIRE(base_material != nullptr);
	CHECK(base_material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO).is_valid());
	CHECK(base_material->get_texture(BaseMaterial3D::TEXTURE_METALLIC).is_valid());
	CHECK(base_material->get_texture(BaseMaterial3D::TEXTURE_ROUGHNESS).is_valid());
	CHECK(base_material->get_texture(BaseMaterial3D::TEXTURE_NORMAL).is_valid());
	CHECK(base_material->get_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING));
	CHECK(base_material->get_normal_scale() == doctest::Approx(1.0f));
	CHECK(base_material->get_metallic_texture_channel() == BaseMaterial3D::TEXTURE_CHANNEL_GREEN);
	CHECK(base_material->get_roughness_texture_channel() == BaseMaterial3D::TEXTURE_CHANNEL_BLUE);
	CHECK(base_material->get_transparency() == BaseMaterial3D::TRANSPARENCY_ALPHA);
	CHECK(base_material->get_uv1_scale().x == doctest::Approx(2.0f));
	CHECK(base_material->get_uv1_scale().y == doctest::Approx(3.0f));
	CHECK(base_material->get_uv1_offset().x == doctest::Approx(0.25f));
	CHECK(base_material->get_uv1_offset().y == doctest::Approx(-2.1f));

	Ref<Image> albedo_image = base_material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO)->get_image();
	REQUIRE(albedo_image.is_valid());
	CHECK(albedo_image->detect_alpha() != Image::ALPHA_NONE);

	memdelete(root);
}

} // namespace TestUsdSceneLoader

#endif // MODULE_USD_ENABLED
#endif // _3D_DISABLED
