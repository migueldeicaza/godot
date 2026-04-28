extends SceneTree

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

func _load_fixture(path: String) -> Node:
	var packed_scene: PackedScene = ResourceLoader.load(path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load skeleton fixture: %s" % path)
	if packed_scene == null:
		return null
	var loaded_root: Node = packed_scene.instantiate()
	_require(loaded_root != null, "Failed to instantiate skeleton fixture: %s" % path)
	if loaded_root != null:
		root.add_child(loaded_root)
	return loaded_root

func _release_fixture(node: Node) -> void:
	if node != null:
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

func _test_armature_fixture(base_dir: String) -> void:
	var arm_root := _load_fixture(_fixture_path(base_dir, "arm.usda"))
	if arm_root == null:
		return

	var skeleton := _find_prim_node(arm_root, "/Model/Skel") as Skeleton3D
	_require(skeleton != null, "Armature fixture did not produce a Skeleton3D node for /Model/Skel.")
	if skeleton != null:
		_require(skeleton.get_bone_count() == 3, "Armature fixture lost authored joint count.")
		_require(skeleton.get_bone_name(0) == "Shoulder", "Armature fixture lost Shoulder joint name.")
		_require(skeleton.get_bone_name(1) == "Elbow", "Armature fixture lost Elbow joint name.")
		_require(skeleton.get_bone_name(2) == "Hand", "Armature fixture lost Hand joint name.")
		_require(skeleton.get_bone_parent(0) == -1, "Armature root joint should remain parentless.")
		_require(skeleton.get_bone_parent(1) == 0, "Elbow joint should stay parented to Shoulder.")
		_require(skeleton.get_bone_parent(2) == 1, "Hand joint should stay parented to Elbow.")
		_require(skeleton.get_bone_rest(1).origin.is_equal_approx(Vector3(0, 0, 2)), "Armature fixture lost Elbow local rest translation.")
		_require(skeleton.get_bone_rest(2).origin.is_equal_approx(Vector3(0, 0, 2)), "Armature fixture lost Hand local rest translation.")
		_require(str(skeleton.get_bone_meta(1, &"usd_joint_path")) == "Shoulder/Elbow", "Armature fixture lost per-bone USD joint path metadata.")

		var skeleton_metadata := _metadata(skeleton)
		_require(skeleton_metadata.get("usd:skeleton_mapping", "") == "skeleton3d_bones", "Armature fixture should record the structural skeleton mapping.")
		_require(int(skeleton_metadata.get("usd:skeleton_joint_count", 0)) == 3, "Armature fixture lost skeleton joint count metadata.")
		var animation_sources: Array = skeleton_metadata.get("usd:animation_sources", [])
		_require(animation_sources.size() == 1 and animation_sources[0] == "/Model/Skel/Anim1", "Armature fixture lost its bound animation source metadata.")

	var animation_player: AnimationPlayer = null
	for child in arm_root.get_children():
		if child is AnimationPlayer:
			animation_player = child
			break
	_require(animation_player != null, "Armature fixture did not produce an AnimationPlayer for the baked USD skeleton animation.")
	if animation_player != null:
		_require(animation_player.has_animation(&"Anim1"), "Armature fixture did not bake the bound SkelAnimation into a Godot animation.")
		var animation: Animation = animation_player.get_animation(&"Anim1")
		_require(animation != null, "Armature fixture returned a null baked animation.")
		if animation != null:
			_require(is_equal_approx(animation.length, 9.0 / 24.0), "Armature fixture baked animation length should match authored timeCodesPerSecond.")
			_require(animation.get_track_count() == 1, "Armature fixture should currently bake one rotation track for the animated Elbow joint.")
			_require(str(animation.track_get_path(0)) == "Model/Skel:Elbow", "Armature fixture baked track path should target the Elbow bone on the imported skeleton.")
			_require(animation.track_get_key_count(0) == 2, "Armature fixture baked track should preserve the two authored rotation keys.")

	var arm_mesh := _find_prim_node(arm_root, "/Model/Arm") as MeshInstance3D
	_require(arm_mesh != null, "Armature fixture lost the bound mesh.")
	if arm_mesh != null:
		var mesh_skin: Skin = arm_mesh.get_skin()
		var skeleton_path: NodePath = arm_mesh.get_skeleton_path()
		_require(mesh_skin != null, "Armature fixture mesh did not receive a Godot Skin resource.")
		_require(not skeleton_path.is_empty(), "Armature fixture mesh did not receive a skeleton path binding.")
		_require(arm_mesh.get_node_or_null(skeleton_path) == skeleton, "Armature fixture mesh skeleton path did not resolve to the imported Skeleton3D.")
		if arm_mesh.mesh != null and arm_mesh.mesh.get_surface_count() > 0:
			var arrays: Array = arm_mesh.mesh.surface_get_arrays(0)
			_require(arrays.size() == Mesh.ARRAY_MAX, "Armature fixture mesh surface arrays were incomplete.")
			if arrays.size() == Mesh.ARRAY_MAX:
				var bones: PackedInt32Array = arrays[Mesh.ARRAY_BONES]
				var weights: PackedFloat32Array = arrays[Mesh.ARRAY_WEIGHTS]
				_require(bones.size() > 0, "Armature fixture mesh did not preserve joint index data.")
				_require(weights.size() > 0, "Armature fixture mesh did not preserve joint weight data.")
	var arm_points := _find_prim_node(arm_root, "/Model/ArmPoints") as MeshInstance3D
	_require(arm_points != null, "Armature fixture should now import the authored Points prim as a MeshInstance3D.")
	if arm_points != null:
		_require(arm_points.mesh != null, "Armature fixture points import lost its mesh resource.")
		_require(arm_points.get_skin() != null, "Armature fixture points import did not receive a Godot Skin resource.")
		_require(not arm_points.get_skeleton_path().is_empty(), "Armature fixture points import did not receive a skeleton path binding.")
		_require(arm_points.get_node_or_null(arm_points.get_skeleton_path()) == skeleton, "Armature fixture points import skeleton path did not resolve to the imported Skeleton3D.")
		var points_metadata: Dictionary = _metadata(arm_points)
		_require(points_metadata.get("usd:points_mapping", "") == "mesh_points", "Armature fixture points import should record its points mapping.")
		_require(int(points_metadata.get("usd:point_count", 0)) == 12, "Armature fixture points import lost its authored point count.")
		if arm_points.mesh != null and arm_points.mesh.get_surface_count() > 0:
			_require(arm_points.mesh.surface_get_primitive_type(0) == Mesh.PRIMITIVE_POINTS, "Armature fixture points import should use point primitives.")
			var arrays: Array = arm_points.mesh.surface_get_arrays(0)
			if arrays.size() == Mesh.ARRAY_MAX:
				var bones: PackedInt32Array = arrays[Mesh.ARRAY_BONES]
				var weights: PackedFloat32Array = arrays[Mesh.ARRAY_WEIGHTS]
				_require(bones.size() > 0, "Armature fixture points import did not preserve joint index data.")
				_require(weights.size() > 0, "Armature fixture points import did not preserve joint weight data.")
	_release_fixture(arm_root)

func _test_blendshape_fixture(base_dir: String) -> void:
	var blend_shape_root := _load_fixture(_fixture_path(base_dir, "usd_blend_shape_test.usda"))
	if blend_shape_root == null:
		return

	var skeleton := _find_prim_node(blend_shape_root, "/root/Plane/Skel") as Skeleton3D
	_require(skeleton != null, "Blend shape fixture did not produce a Skeleton3D node for /root/Plane/Skel.")
	if skeleton != null:
		_require(skeleton.get_bone_count() == 1, "Blend shape fixture lost its single-joint skeleton.")
		_require(skeleton.get_bone_name(0) == "joint1", "Blend shape fixture lost the authored joint name.")
		var animation_sources: Array = _metadata(skeleton).get("usd:animation_sources", [])
		_require(animation_sources.size() == 1 and animation_sources[0] == "/root/Plane/Skel/Anim", "Blend shape fixture lost the animation source metadata.")

	var plane_mesh := _find_prim_node(blend_shape_root, "/root/Plane/Plane") as MeshInstance3D
	_require(plane_mesh != null, "Blend shape fixture lost the base mesh.")
	if plane_mesh != null and plane_mesh.mesh != null:
		_require(plane_mesh.get_blend_shape_count() == 1, "Blend shape fixture should now create one Godot blend shape.")
		_require(plane_mesh.find_blend_shape_by_name(&"Key_1") == 0, "Blend shape fixture lost the authored blend shape name.")
		var mesh_metadata: Dictionary = _metadata(plane_mesh)
		_require(mesh_metadata.get("usd:blend_shape_mapping", "") == "array_mesh_relative_piecewise", "Blend shape fixture should record the mesh blend shape mapping.")
		var blend_shape_arrays: Array = plane_mesh.mesh.surface_get_blend_shape_arrays(0)
		_require(blend_shape_arrays.size() == 1, "Blend shape fixture should have one surface blend shape array.")
		if blend_shape_arrays.size() == 1 and mesh_metadata.get("usd:blend_shape_has_normal_offsets", {}).get("Key_1", false):
			var blend_shape_surface: Array = blend_shape_arrays[0]
			var blend_shape_normals_variant: Variant = blend_shape_surface[Mesh.ARRAY_NORMAL]
			_require(blend_shape_normals_variant is PackedVector3Array, "Blend shape fixture should expose PackedVector3Array normal deltas when authored.")
			var blend_shape_normals: PackedVector3Array = blend_shape_normals_variant
			_require(blend_shape_normals.size() == 6, "Blend shape fixture should preserve triangulated normal deltas.")
			if blend_shape_normals.size() > 0:
				_require(is_equal_approx(blend_shape_normals[0].z, 0.25), "Blend shape fixture lost the authored primary normal delta.")

	var animation_player: AnimationPlayer = null
	for child in blend_shape_root.get_children():
		if child is AnimationPlayer:
			animation_player = child
			break
	_require(animation_player != null, "Blend shape fixture did not produce an AnimationPlayer.")
	if animation_player != null:
		_require(animation_player.has_animation(&"Anim"), "Blend shape fixture did not bake the blendShapeWeights animation.")
		var animation: Animation = animation_player.get_animation(&"Anim")
		_require(animation != null, "Blend shape fixture returned a null baked blend shape animation.")
		if animation != null:
			var found_blend_shape_track := false
			for track_index in range(animation.get_track_count()):
				if animation.track_get_type(track_index) == Animation.TYPE_BLEND_SHAPE and str(animation.track_get_path(track_index)) == "root/Plane/Plane:Key_1":
					found_blend_shape_track = true
					_require(animation.track_get_key_count(track_index) == 3, "Blend shape fixture should preserve the three authored weight samples.")
					break
			_require(found_blend_shape_track, "Blend shape fixture did not bake a blend shape track for Key_1.")

	_release_fixture(blend_shape_root)

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 1, "Expected one argument: <blender-test-data-usd directory>")
	if failed:
		quit(1)
		return

	var fixture_dir: String = args[0]
	_test_armature_fixture(fixture_dir)
	_test_blendshape_fixture(fixture_dir)

	quit(1 if failed else 0)
