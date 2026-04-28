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

func _find_prim_node(node: Node, prim_path: String) -> Node:
	var metadata: Dictionary = _metadata(node)
	if metadata.get("usd:prim_path", "") == prim_path:
		return node
	for child in node.get_children():
		var result := _find_prim_node(child, prim_path)
		if result != null:
			return result
	return null

func _require_contains(text: String, needle: String, message: String) -> void:
	_require(text.contains(needle), "%s Missing: %s" % [message, needle])

func _find_animation_player(root_node: Node) -> AnimationPlayer:
	for child in root_node.get_children():
		if child is AnimationPlayer:
			return child
	return null

func _validate_eight_weight_import(root_node: Node) -> void:
	var mesh := _find_prim_node(root_node, "/Model/Ribbon") as MeshInstance3D
	_require(mesh != null, "Eight-weight fixture lost the skinned mesh.")
	if mesh == null or mesh.mesh == null:
		return
	_require(mesh.mesh.surface_get_format(0) & Mesh.ARRAY_FLAG_USE_8_BONE_WEIGHTS, "Eight-weight fixture should set the 8-bone-weight mesh flag.")
	var arrays: Array = mesh.mesh.surface_get_arrays(0)
	_require(arrays.size() == Mesh.ARRAY_MAX, "Eight-weight fixture returned incomplete surface arrays.")
	if arrays.size() == Mesh.ARRAY_MAX:
		var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var bones: PackedInt32Array = arrays[Mesh.ARRAY_BONES]
		var weights: PackedFloat32Array = arrays[Mesh.ARRAY_WEIGHTS]
		_require(bones.size() == vertices.size() * 8, "Eight-weight fixture should preserve 8 joints per vertex.")
		_require(weights.size() == vertices.size() * 8, "Eight-weight fixture should preserve 8 weights per vertex.")
		var total := 0.0
		for i in range(8):
			total += weights[i]
		_require(abs(total - 1.0) < 0.01, "Eight-weight fixture should keep the first vertex weights normalized.")

func _validate_multiclip_combo_import(root_node: Node) -> void:
	var skeleton := _find_prim_node(root_node, "/root/Actor/Skel") as Skeleton3D
	_require(skeleton != null, "Multi-clip combo fixture lost the skeleton.")
	var animation_player := _find_animation_player(root_node)
	_require(animation_player != null, "Multi-clip combo fixture lost the animation player.")
	if animation_player == null:
		return
	_require(animation_player.has_animation(&"Rotate"), "Multi-clip combo fixture lost the Rotate animation.")
	_require(animation_player.has_animation(&"Combo"), "Multi-clip combo fixture lost the Combo animation.")
	if not animation_player.has_animation(&"Combo"):
		return
	var combo: Animation = animation_player.get_animation(&"Combo")
	_require(combo != null, "Multi-clip combo fixture returned a null Combo animation.")
	if combo == null:
		return
	var found_rotation := false
	var found_blend_shape := false
	for track_index in range(combo.get_track_count()):
		var track_path := str(combo.track_get_path(track_index))
		if combo.track_get_type(track_index) == Animation.TYPE_ROTATION_3D and track_path == "root/Actor/Skel:joint1":
			found_rotation = true
		elif combo.track_get_type(track_index) == Animation.TYPE_BLEND_SHAPE and track_path == "root/Actor/Body:Smile":
			found_blend_shape = true
	_require(found_rotation, "Multi-clip combo fixture did not bake the joint rotation track onto Combo.")
	_require(found_blend_shape, "Multi-clip combo fixture did not bake the blend shape track onto Combo.")

func _roundtrip_eight_weight_fixture(temp_dir: String) -> void:
	var source_path := "res://tests/data/usd/skeleton_eight_weights.usda"
	var packed_scene: PackedScene = ResourceLoader.load(source_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load eight-weight fixture.")
	if packed_scene == null:
		return

	var imported_root: Node = packed_scene.instantiate()
	_require(imported_root != null, "Failed to instantiate eight-weight fixture.")
	if imported_root != null:
		_validate_eight_weight_import(imported_root)
		imported_root.free()

	var save_path := temp_dir.path_join("skeleton_eight_weights_roundtrip.usda")
	DirAccess.remove_absolute(save_path)
	_require(ResourceSaver.save(packed_scene, save_path) == OK, "Failed to save eight-weight fixture round-trip.")
	var saved_text := FileAccess.get_file_as_string(save_path)
	_require_contains(saved_text, "elementSize = 8", "Eight-weight fixture round-trip save lost the 8-weight element size.")

	var reloaded_scene: PackedScene = ResourceLoader.load(save_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(reloaded_scene != null, "Failed to reload eight-weight round-trip save.")
	if reloaded_scene != null:
		var root_node: Node = reloaded_scene.instantiate()
		_require(root_node != null, "Failed to instantiate eight-weight round-trip save.")
		if root_node != null:
			_validate_eight_weight_import(root_node)
			root_node.free()

func _roundtrip_multiclip_combo_fixture(temp_dir: String) -> void:
	var source_path := "res://tests/data/usd/skeleton_multi_anim_blendshape.usda"
	var packed_scene: PackedScene = ResourceLoader.load(source_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load multi-clip combo fixture.")
	if packed_scene == null:
		return

	var imported_root: Node = packed_scene.instantiate()
	_require(imported_root != null, "Failed to instantiate multi-clip combo fixture.")
	if imported_root != null:
		_validate_multiclip_combo_import(imported_root)
		imported_root.free()

	var save_path := temp_dir.path_join("skeleton_multi_anim_blendshape_roundtrip.usda")
	DirAccess.remove_absolute(save_path)
	_require(ResourceSaver.save(packed_scene, save_path) == OK, "Failed to save multi-clip combo fixture round-trip.")
	var saved_text := FileAccess.get_file_as_string(save_path)
	_require_contains(saved_text, "def SkelAnimation \"Rotate\"", "Multi-clip combo round-trip save lost the Rotate clip.")
	_require_contains(saved_text, "def SkelAnimation \"Combo\"", "Multi-clip combo round-trip save lost the Combo clip.")
	_require_contains(saved_text, "quatf[] rotations.timeSamples", "Multi-clip combo round-trip save lost joint rotation samples.")
	_require_contains(saved_text, "blendShapeWeights.timeSamples", "Multi-clip combo round-trip save lost blend shape weight samples.")

	var reloaded_scene: PackedScene = ResourceLoader.load(save_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(reloaded_scene != null, "Failed to reload multi-clip combo round-trip save.")
	if reloaded_scene != null:
		var root_node: Node = reloaded_scene.instantiate()
		_require(root_node != null, "Failed to instantiate multi-clip combo round-trip save.")
		if root_node != null:
			_validate_multiclip_combo_import(root_node)
			root_node.free()

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 1, "Expected one argument: <temp-dir>")
	if failed:
		quit(1)
		return

	var temp_dir: String = args[0]
	_roundtrip_eight_weight_fixture(temp_dir)
	_roundtrip_multiclip_combo_fixture(temp_dir)

	quit(1 if failed else 0)
