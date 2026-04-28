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
	_require(_find_prim_node(arm_root, "/Model/ArmPoints") != null, "Armature fixture lost the authored points prim.")
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

	var blend_shape_node := _find_prim_node(blend_shape_root, "/root/Plane/Plane/Key_1")
	_require(blend_shape_node != null, "Blend shape fixture lost the authored BlendShape prim.")
	if blend_shape_node != null:
		var metadata := _metadata(blend_shape_node)
		_require(metadata.get("usd:type_name", "") == "BlendShape", "Blend shape fixture lost BlendShape type metadata.")
		_require(metadata.get("usd:mapping_status", "") != "", "Blend shape fixture should still document that BlendShape is on the fallback path.")

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
