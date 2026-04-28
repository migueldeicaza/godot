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

func _validate_loaded_roundtrip(root: Node) -> void:
	var skeleton := _find_prim_node(root, "/Model/Skel") as Skeleton3D
	_require(skeleton != null, "Round-tripped skeleton scene lost the skeleton node.")
	if skeleton != null:
		_require(skeleton.get_bone_count() == 3, "Round-tripped skeleton scene lost authored bone count.")
		_require(skeleton.get_bone_name(1) == "Elbow", "Round-tripped skeleton scene lost the Elbow bone.")

	var animation_player: AnimationPlayer = null
	for child in root.get_children():
		if child is AnimationPlayer:
			animation_player = child
			break
	_require(animation_player != null, "Round-tripped skeleton scene lost the baked animation player.")
	if animation_player != null:
		_require(animation_player.has_animation(&"Anim1"), "Round-tripped skeleton scene lost the joint animation.")
		var animation: Animation = animation_player.get_animation(&"Anim1")
		_require(animation != null, "Round-tripped skeleton scene returned a null joint animation.")
		if animation != null:
			_require(animation.get_track_count() == 1, "Round-tripped skeleton scene should still have one baked joint track.")
			_require(animation.track_get_type(0) == Animation.TYPE_ROTATION_3D, "Round-tripped skeleton scene lost the rotation track type.")
			_require(str(animation.track_get_path(0)) == "Model/Skel:Elbow", "Round-tripped skeleton scene rotation track should still target the Elbow bone.")
			_require(animation.track_get_key_count(0) == 2, "Round-tripped skeleton scene should preserve the two authored rotation keys.")

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 1, "Expected one argument: <save-path>")
	if failed:
		quit(1)
		return

	var save_path: String = args[0]
	DirAccess.remove_absolute(save_path)

	var packed_scene: PackedScene = ResourceLoader.load("res://tests/data/usd/skeleton_basic.usda", "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load checked-in skeleton fixture.")
	if packed_scene == null:
		quit(1)
		return

	var save_error := ResourceSaver.save(packed_scene, save_path)
	_require(save_error == OK, "Failed to save skeleton round-trip USDA: %s" % save_path)

	var saved_text := FileAccess.get_file_as_string(save_path)
	_require(saved_text != "", "Saved skeleton round-trip USDA was empty.")
	_require_contains(saved_text, "startTimeCode = 1", "Skeleton round-trip save lost the stage startTimeCode.")
	_require_contains(saved_text, "endTimeCode = 10", "Skeleton round-trip save lost the stage endTimeCode.")
	_require_contains(saved_text, "timeCodesPerSecond = 24", "Skeleton round-trip save lost the stage timeCodesPerSecond.")
	_require_contains(saved_text, "def SkelAnimation \"Anim1\"", "Skeleton round-trip save lost the SkelAnimation prim.")
	_require_contains(saved_text, "uniform token[] joints = [\"Shoulder/Elbow\"]", "Skeleton round-trip save lost the animated joint list.")
	_require_contains(saved_text, "float3[] translations = [(0, 0, 2)]", "Skeleton round-trip save lost the authored constant translations channel.")
	_require_contains(saved_text, "quatf[] rotations.timeSamples", "Skeleton round-trip save lost the joint rotation time samples.")
	_require_contains(saved_text, "half3[] scales = [(1, 1, 1)]", "Skeleton round-trip save lost the authored constant scales channel.")

	var reloaded_scene: PackedScene = ResourceLoader.load(save_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(reloaded_scene != null, "Failed to reload the saved skeleton round-trip USDA.")
	if reloaded_scene != null:
		var root: Node = reloaded_scene.instantiate()
		_require(root != null, "Failed to instantiate the saved skeleton round-trip USDA.")
		if root != null:
			_validate_loaded_roundtrip(root)
			root.free()

	quit(1 if failed else 0)
