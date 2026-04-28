extends SceneTree

var failed := false

func _fail(message: String) -> void:
	failed = true
	push_error(message)

func _require(condition: bool, message: String) -> void:
	if not condition:
		_fail(message)

func _metadata(object: Object) -> Dictionary:
	var value: Variant = object.get_meta(&"usd", {})
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

func _find_animation_player(root_node: Node) -> AnimationPlayer:
	for child in root_node.get_children():
		if child is AnimationPlayer:
			return child
	return null

func _require_time_codes(actual: Array, expected: Array, label: String) -> void:
	_require(actual.size() == expected.size(), "%s time-code count mismatch. expected=%s actual=%s" % [label, expected, actual])
	if actual.size() != expected.size():
		return
	for i in range(expected.size()):
		_require(is_equal_approx(float(actual[i]), float(expected[i])), "%s time code mismatch at %d. expected=%s actual=%s" % [label, i, expected[i], actual[i]])

func _validate_loaded_fixture(root_node: Node) -> void:
	var animation_player := _find_animation_player(root_node)
	_require(animation_player != null, "Sparse animation fixture lost its AnimationPlayer.")
	if animation_player == null or not animation_player.has_animation(&"Anim"):
		return

	var animation: Animation = animation_player.get_animation(&"Anim")
	_require(animation != null, "Sparse animation fixture returned a null animation.")
	if animation == null:
		return

	_require(animation.get_track_count() == 1, "Sparse animation fixture should only bake the non-rest rotation track.")
	if animation.get_track_count() == 1:
		_require(animation.track_get_type(0) == Animation.TYPE_ROTATION_3D, "Sparse animation fixture should only expose a rotation track.")
		_require(str(animation.track_get_path(0)) == "root/Model/Skel:joint1", "Sparse animation fixture rotation track should target joint1.")
		_require(animation.track_get_key_count(0) >= 2, "Sparse animation fixture should retain a usable baked rotation track.")

	var metadata := _metadata(animation)
	_require(bool(metadata.get("usd:has_authored_translations", false)), "Sparse animation fixture lost authored translations metadata.")
	_require(bool(metadata.get("usd:has_authored_rotations", false)), "Sparse animation fixture lost authored rotations metadata.")
	_require(bool(metadata.get("usd:has_authored_scales", false)), "Sparse animation fixture lost authored scales metadata.")
	_require(bool(metadata.get("usd:has_authored_blend_shape_weights", false)), "Sparse animation fixture lost authored blend-shape metadata.")
	_require(not bool(metadata.get("usd:translations_constant", true)), "Sparse animation fixture translations should stay sampled, not constant.")
	_require(not bool(metadata.get("usd:scales_constant", true)), "Sparse animation fixture scales should stay sampled, not constant.")
	_require(not bool(metadata.get("usd:blend_shape_weights_constant", true)), "Sparse animation fixture blend-shape weights should stay sampled, not constant.")
	_require_time_codes(metadata.get("usd:translation_time_codes", []), [1.0, 9.0], "translations")
	_require_time_codes(metadata.get("usd:rotation_time_codes", []), [5.0, 10.0], "rotations")
	_require_time_codes(metadata.get("usd:scale_time_codes", []), [3.0, 7.0], "scales")
	_require_time_codes(metadata.get("usd:blend_shape_weight_time_codes", []), [2.0, 8.0], "blend-shape weights")

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 1, "Expected one argument: <save-path>")
	if failed:
		quit(1)
		return

	var save_path: String = args[0]
	var source_path := "res://tests/data/usd/skel_animation_sparsity.usda"
	var packed_scene: PackedScene = ResourceLoader.load(source_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load sparse animation fixture.")
	if packed_scene == null:
		quit(1)
		return

	var imported_root: Node = packed_scene.instantiate()
	_require(imported_root != null, "Failed to instantiate sparse animation fixture.")
	if imported_root != null:
		_validate_loaded_fixture(imported_root)
		imported_root.free()

	DirAccess.remove_absolute(save_path)
	_require(ResourceSaver.save(packed_scene, save_path) == OK, "Failed to save sparse animation round-trip fixture.")

	var reloaded_scene: PackedScene = ResourceLoader.load(save_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(reloaded_scene != null, "Failed to reload sparse animation round-trip save.")
	if reloaded_scene != null:
		var root_node: Node = reloaded_scene.instantiate()
		_require(root_node != null, "Failed to instantiate sparse animation round-trip save.")
		if root_node != null:
			_validate_loaded_fixture(root_node)
			root_node.free()

	quit(1 if failed else 0)
