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
#include "scene/3d/world_environment.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/packed_scene.h"

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
