extends SceneTree

const USD_PREVIEW_LIGHTING_MODE_SETTING := "filesystem/import/usd/preview_lighting_mode"

var failed := false

func _fail(message: String) -> void:
	failed = true
	push_error(message)

func _require(condition: bool, message: String) -> void:
	if not condition:
		_fail(message)

func _metadata(node: Node) -> Dictionary:
	var value: Variant = node.get_meta(&"usd", {})
	return value if value is Dictionary else {}

func _fixture_path(base_dir: String, relative_path: String) -> String:
	return base_dir.path_join(relative_path)

func _gather_usd_files(base_dir: String) -> PackedStringArray:
	var files := PackedStringArray()
	var entries := DirAccess.get_files_at(base_dir)
	entries.sort()
	for entry in entries:
		var extension: String = entry.get_extension().to_lower()
		if extension == "usd" or extension == "usda" or extension == "usdc" or extension == "usdz":
			files.append(base_dir.path_join(entry))
	var directories := DirAccess.get_directories_at(base_dir)
	directories.sort()
	for directory in directories:
		if directory.begins_with("."):
			continue
		files.append_array(_gather_usd_files(base_dir.path_join(directory)))
	return files

func _load_fixture(path: String) -> Node:
	var packed_scene: PackedScene = ResourceLoader.load(path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load Blender USD fixture as PackedScene: %s" % path)
	if packed_scene == null:
		return null
	var loaded_root: Node = packed_scene.instantiate()
	_require(loaded_root != null, "Failed to instantiate Blender USD fixture: %s" % path)
	if loaded_root != null:
		root.add_child(loaded_root)
	return loaded_root

func _release_fixture(node: Node) -> void:
	if node == null:
		return
	node.queue_free()

func _find_prim_node(node: Node, prim_path: String) -> Node:
	var metadata: Dictionary = _metadata(node)
	if metadata.get("usd:prim_path", "") == prim_path:
		return node
	for child in node.get_children():
		var result := _find_prim_node(child, prim_path)
		if result != null:
			return result
	return null

func _collect_nodes(node: Node, predicate: Callable, out: Array) -> void:
	if predicate.call(node):
		out.append(node)
	for child in node.get_children():
		_collect_nodes(child, predicate, out)

func _nodes_with_type_name(node: Node, type_name: String) -> Array:
	var nodes: Array = []
	_collect_nodes(node, func(candidate: Node) -> bool:
		return _metadata(candidate).get("usd:type_name", "") == type_name
	, nodes)
	return nodes

func _placeholder_nodes_with_type_name(node: Node, type_name: String) -> Array:
	var nodes: Array = []
	_collect_nodes(node, func(candidate: Node) -> bool:
		var metadata: Dictionary = _metadata(candidate)
		return metadata.get("usd:type_name", "") == type_name and metadata.get("usd:mapping_status", "") != ""
	, nodes)
	return nodes

func _mesh_surface_arrays(mesh_node: MeshInstance3D, surface := 0) -> Array:
	_require(mesh_node.mesh != null, "Mesh node %s had no mesh resource." % mesh_node.name)
	if mesh_node.mesh == null:
		return []
	_require(mesh_node.mesh.get_surface_count() > surface, "Mesh node %s did not have surface %d." % [mesh_node.name, surface])
	if mesh_node.mesh.get_surface_count() <= surface:
		return []
	return mesh_node.mesh.surface_get_arrays(surface)

func _smoke_load_all_fixtures(base_dir: String) -> void:
	var usd_files: PackedStringArray = _gather_usd_files(base_dir)
	_require(not usd_files.is_empty(), "No USD fixtures were found under %s" % base_dir)
	for path in usd_files:
		var loaded_root: Node = _load_fixture(path)
		if loaded_root == null:
			continue
		_require(loaded_root is Node, "Fixture did not instantiate to a Node: %s" % path)
		print("blender_coverage smoke:", path)
		_release_fixture(loaded_root)

func _test_camera_units(base_dir: String) -> void:
	var first := _load_fixture(_fixture_path(base_dir, "usd_camera_test_1.usda"))
	var second := _load_fixture(_fixture_path(base_dir, "usd_camera_test_2.usda"))
	if first == null or second == null:
		return

	var first_root := first as Node3D
	var second_root := second as Node3D
	_require(first_root != null and second_root != null, "Blender camera fixtures should instantiate as Node3D roots.")
	if first_root == null or second_root == null:
		return

	var first_metadata: Dictionary = _metadata(first_root)
	var second_metadata: Dictionary = _metadata(second_root)
	_require(is_equal_approx(float(first_metadata.get("usd:meters_per_unit", -1.0)), 1.0), "Camera fixture 1 lost metersPerUnit metadata.")
	_require(is_equal_approx(float(second_metadata.get("usd:meters_per_unit", -1.0)), 0.1), "Camera fixture 2 lost metersPerUnit metadata.")
	_require(is_equal_approx(first_root.transform.basis.get_scale().x, 1.0), "Camera fixture 1 root scale did not match metersPerUnit.")
	_require(is_equal_approx(second_root.transform.basis.get_scale().x, 0.1), "Camera fixture 2 root scale did not match metersPerUnit.")

	var first_camera := _find_prim_node(first_root, "/TestGroup/Test_Camera") as Camera3D
	var second_camera := _find_prim_node(second_root, "/TestGroup/Test_Camera") as Camera3D
	_require(first_camera != null and second_camera != null, "Blender camera fixtures did not produce Camera3D nodes.")
	if first_camera != null and second_camera != null:
		_require(is_equal_approx(first_camera.fov, second_camera.fov), "Camera focal/aperture mapping should be independent of stage metersPerUnit.")
		_require(first_camera.projection == Camera3D.PROJECTION_PERSPECTIVE, "Camera fixture 1 should map to a perspective camera.")
		_require(second_camera.projection == Camera3D.PROJECTION_PERSPECTIVE, "Camera fixture 2 should map to a perspective camera.")

	_release_fixture(first)
	_release_fixture(second)

func _test_hierarchy_and_mesh_topology(base_dir: String) -> void:
	var hierarchy_root := _load_fixture(_fixture_path(base_dir, "prim-hierarchy.usda"))
	if hierarchy_root == null:
		return
	var hierarchy_node := hierarchy_root as Node3D
	_require(hierarchy_node != null, "prim-hierarchy.usda should instantiate as a Node3D root.")
	if hierarchy_node != null:
		var stage_metadata: Dictionary = _metadata(hierarchy_node)
		_require(stage_metadata.get("usd:up_axis", "") == "Z", "prim-hierarchy.usda lost stage upAxis metadata.")

		var world := _find_prim_node(hierarchy_node, "/World") as Node3D
		var plane_group := _find_prim_node(hierarchy_node, "/World/Plane") as Node3D
		var empty_group := _find_prim_node(hierarchy_node, "/World/Empty") as Node3D
		var nested_plane_group := _find_prim_node(hierarchy_node, "/World/Empty/Plane_002") as Node3D
		_require(world != null and plane_group != null and empty_group != null and nested_plane_group != null, "Hierarchy fixture did not preserve the authored Xform structure.")
		if world != null:
			_require(world.transform.origin.is_equal_approx(Vector3(0, 0, 2)), "World Xform translation was not preserved.")
		if plane_group != null:
			_require(plane_group.transform.origin.is_equal_approx(Vector3(0, -2, -2)), "Plane Xform translation was not preserved.")
		if empty_group != null:
			_require(empty_group.transform.origin.is_equal_approx(Vector3(3, 0, -2)), "Empty Xform translation was not preserved.")
		if nested_plane_group != null:
			_require(nested_plane_group.transform.origin.is_equal_approx(Vector3(0, 0, -2)), "Nested Plane_002 Xform translation was not preserved.")

			var plane_mesh := _find_prim_node(hierarchy_node, "/World/Plane/Plane") as MeshInstance3D
			_require(plane_mesh != null, "Hierarchy fixture did not produce the Plane mesh node.")
			if plane_mesh != null:
				var arrays: Array = _mesh_surface_arrays(plane_mesh)
				if arrays.size() == Mesh.ARRAY_MAX:
					var uvs_value: Variant = arrays[Mesh.ARRAY_TEX_UV]
					_require(uvs_value is PackedVector2Array, "Hierarchy fixture did not produce a typed UV array.")
					if uvs_value is PackedVector2Array:
						var uvs: PackedVector2Array = uvs_value
						_require(uvs.size() > 0, "Hierarchy fixture did not keep Blender UVs on the plane mesh.")

	_release_fixture(hierarchy_root)

	var polygon_root := _load_fixture(_fixture_path(base_dir, "usd_mesh_polygon_types.usda"))
	if polygon_root == null:
		return
	for prim_path in [
		"/ngon_concave/m_ngon_concave",
		"/ngon_convex/m_ngon_convex",
		"/degenerate/m_degenerate",
		"/quad/m_quad",
		"/triangles/m_triangles",
	]:
		var mesh_node := _find_prim_node(polygon_root, prim_path) as MeshInstance3D
		_require(mesh_node != null, "Polygon topology fixture did not produce mesh %s." % prim_path)
		if mesh_node == null:
			continue
		var arrays: Array = _mesh_surface_arrays(mesh_node)
		if arrays.size() == Mesh.ARRAY_MAX:
			var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
			_require(vertices.size() > 0, "Polygon topology fixture produced an empty vertex array for %s." % prim_path)
			_require(indices.size() > 0, "Polygon topology fixture produced an empty index array for %s." % prim_path)

	_release_fixture(polygon_root)

func _test_shapes_and_materials(base_dir: String) -> void:
	var shapes_root := _load_fixture(_fixture_path(base_dir, "usd_shapes_test.usda"))
	if shapes_root != null:
		var primitive_expectations: Dictionary = {
			"/root/Pill/capsule": "CapsuleMesh",
			"/root/Hat/cone": "CylinderMesh",
			"/root/Box/cube": "BoxMesh",
			"/root/Tube/cylinder": "CylinderMesh",
			"/root/Ball/sphere": "SphereMesh",
			"/root/Ground/plane": "PlaneMesh",
		}
		for prim_path in primitive_expectations.keys():
			var mesh_node := _find_prim_node(shapes_root, prim_path) as MeshInstance3D
			_require(mesh_node != null, "Primitive shape fixture did not create %s." % prim_path)
			if mesh_node == null or mesh_node.mesh == null:
				continue
			_require(mesh_node.mesh.get_class() == primitive_expectations[prim_path], "Primitive %s mapped to %s instead of %s." % [prim_path, mesh_node.mesh.get_class(), primitive_expectations[prim_path]])

		for unsupported_prim in ["/root/Pill_1/capsule_1", "/root/Tube_1/cylinder_1"]:
			var unsupported_node := _find_prim_node(shapes_root, unsupported_prim)
			_require(unsupported_node != null, "Custom Blender primitive %s disappeared entirely." % unsupported_prim)
			if unsupported_node != null:
				var unsupported_metadata: Dictionary = _metadata(unsupported_node)
				_require(unsupported_metadata.get("usd:mapping_status", "") != "", "Custom Blender primitive %s should degrade to a documented placeholder until it is implemented." % unsupported_prim)

		_release_fixture(shapes_root)

	var mtlx_root := _load_fixture(_fixture_path(base_dir, "usd_simple_mtlx.usda"))
	if mtlx_root != null:
		_require(_find_prim_node(mtlx_root, "/root/Camera/Camera") is Camera3D, "MaterialX fixture lost the Blender camera.")
		_require(_find_prim_node(mtlx_root, "/root/Light/Light") is Light3D, "MaterialX fixture lost the Blender light.")
		var cube := _find_prim_node(mtlx_root, "/root/Cube/Cube") as MeshInstance3D
		_require(cube != null, "MaterialX fixture lost the Blender cube mesh.")
		if cube != null and cube.mesh != null and cube.mesh.get_surface_count() > 0:
			var material: Material = cube.mesh.surface_get_material(0)
			_require(material is BaseMaterial3D, "MaterialX fixture should still map the preview surface to a StandardMaterial3D.")
			if material is BaseMaterial3D:
				var base_material: BaseMaterial3D = material as BaseMaterial3D
				_require(base_material.albedo_color.is_equal_approx(Color(0.8, 0.8, 0.8, 1.0)), "MaterialX fixture should currently follow the Blender-authored UsdPreviewSurface fallback.")
		_release_fixture(mtlx_root)

	var udim_root := _load_fixture(_fixture_path(base_dir, "usd_mesh_udim.usda"))
	if udim_root != null:
		var mesh_node := _find_prim_node(udim_root, "/root/uvmap_plane/uvmap_plane") as MeshInstance3D
		_require(mesh_node != null, "UDIM fixture did not create the authored mesh.")
		if mesh_node != null:
			var metadata: Dictionary = _metadata(mesh_node)
			var bindings: Array = metadata.get("usd:material_bindings", [])
			_require(bindings.size() == 1, "UDIM fixture lost its material binding metadata.")
			if mesh_node.mesh != null and mesh_node.mesh.get_surface_count() > 0:
				var material: Material = mesh_node.mesh.surface_get_material(0)
				_require(material is BaseMaterial3D, "UDIM fixture should still create a preview material wrapper.")
				if material is BaseMaterial3D:
					var albedo_texture: Texture2D = (material as BaseMaterial3D).get_texture(BaseMaterial3D.TEXTURE_ALBEDO)
					if albedo_texture == null:
						_require(metadata.get("usd:texture_status", "") != "" or metadata.get("usd:material_status", "") != "", "UDIM fixture should document why its texture could not be mapped.")
		_release_fixture(udim_root)

func _test_placeholder_fallbacks(base_dir: String) -> void:
	var point_instancer_root := _load_fixture(_fixture_path(base_dir, "usd_nested_point_instancer.usda"))
	if point_instancer_root != null:
		_require(_placeholder_nodes_with_type_name(point_instancer_root, "PointInstancer").size() > 0, "Nested point instancer fixture should currently fall back to documented placeholder nodes.")
		_release_fixture(point_instancer_root)

	var point_instancer_anim_root := _load_fixture(_fixture_path(base_dir, "usd_point_instancer_anim.usda"))
	if point_instancer_anim_root != null:
		_require(_placeholder_nodes_with_type_name(point_instancer_anim_root, "PointInstancer").size() > 0, "Animated point instancer fixture should currently fall back to documented placeholder nodes.")
		_release_fixture(point_instancer_anim_root)

	var blend_shape_root := _load_fixture(_fixture_path(base_dir, "usd_blend_shape_test.usda"))
	if blend_shape_root != null:
		_require(_find_prim_node(blend_shape_root, "/root/Plane/Plane") is MeshInstance3D, "Blend shape fixture lost the base mesh.")
		_require(_find_prim_node(blend_shape_root, "/root/Plane/Skel") is Skeleton3D, "Blend shape fixture should now expose Skeleton as a Skeleton3D node.")
		_require(_placeholder_nodes_with_type_name(blend_shape_root, "BlendShape").size() > 0, "Blend shape fixture should currently expose BlendShape as a documented placeholder.")
		_release_fixture(blend_shape_root)

	var arm_root := _load_fixture(_fixture_path(base_dir, "arm.usda"))
	if arm_root != null:
		_require(_find_prim_node(arm_root, "/Model/Arm") is MeshInstance3D, "Armature fixture lost the skinned mesh.")
		_require(_find_prim_node(arm_root, "/Model/Skel") is Skeleton3D, "Armature fixture should now expose Skeleton as a Skeleton3D node.")
		_require(_placeholder_nodes_with_type_name(arm_root, "Points").size() > 0, "Armature fixture should currently expose Points as a documented placeholder.")
		_release_fixture(arm_root)

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 1, "Expected one argument: <blender-test-data-usd directory>")
	if failed:
		quit(1)
		return

	var fixture_dir: String = args[0]
	ProjectSettings.set_setting(USD_PREVIEW_LIGHTING_MODE_SETTING, 0)

	_smoke_load_all_fixtures(fixture_dir)
	_test_camera_units(fixture_dir)
	_test_hierarchy_and_mesh_topology(fixture_dir)
	_test_shapes_and_materials(fixture_dir)
	_test_placeholder_fallbacks(fixture_dir)

	quit(1 if failed else 0)
