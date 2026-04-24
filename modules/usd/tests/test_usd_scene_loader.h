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

#include "core/io/resource_loader.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"

namespace TestUsdSceneLoader {

TEST_CASE("[SceneTree][USD] Load a minimal USD scene as PackedScene") {
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
	CHECK(root->get_child_count() == 1);

	Dictionary root_metadata = root->get_meta(StringName("usd"), Dictionary());
	CHECK((bool)root_metadata.get("usd:read_only_loader", false));
	CHECK((String)root_metadata.get("usd:default_prim_path", String()) == String("/Root"));

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

	Dictionary unmapped_attributes = mesh_metadata.get("usd:unmapped_attributes", Dictionary());
	CHECK(unmapped_attributes.has("primvars:displayColor"));

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
	CHECK(root->get_child_count() == 1);

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

} // namespace TestUsdSceneLoader

#endif // MODULE_USD_ENABLED
#endif // _3D_DISABLED
