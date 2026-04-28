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
	var mesh := _find_prim_node(root, "/root/Plane/Plane") as MeshInstance3D
	_require(mesh != null, "Round-tripped blend shape scene lost the mesh node.")
	if mesh != null:
		_require(mesh.get_blend_shape_count() == 2, "Round-tripped blend shape scene should preserve the primary and inbetween channels.")
		_require(mesh.find_blend_shape_by_name(&"Key_1") == 0, "Round-tripped blend shape scene lost the primary channel.")
		_require(mesh.find_blend_shape_by_name(&"Key_1__inbetween__HalfKey") == 1, "Round-tripped blend shape scene lost the generated inbetween channel.")
		var mesh_metadata: Dictionary = _metadata(mesh)
		_require(mesh_metadata.get("usd:blend_shape_mapping", "") == "array_mesh_relative_piecewise", "Round-tripped blend shape scene lost its piecewise mapping metadata.")

	var skeleton := _find_prim_node(root, "/root/Plane/Skel") as Skeleton3D
	_require(skeleton != null, "Round-tripped blend shape scene lost the skeleton node.")

	var animation_player: AnimationPlayer = null
	for child in root.get_children():
		if child is AnimationPlayer:
			animation_player = child
			break
	_require(animation_player != null, "Round-tripped blend shape scene lost the baked animation player.")
	if animation_player != null:
		_require(animation_player.has_animation(&"Anim"), "Round-tripped blend shape scene lost the blend-shape animation.")
		var animation: Animation = animation_player.get_animation(&"Anim")
		_require(animation != null, "Round-tripped blend shape scene returned a null blend-shape animation.")
		if animation != null:
			var found_primary := false
			var found_inbetween := false
			for track_index in range(animation.get_track_count()):
				if animation.track_get_type(track_index) != Animation.TYPE_BLEND_SHAPE:
					continue
				var track_path := str(animation.track_get_path(track_index))
				if track_path == "root/Plane/Plane:Key_1":
					found_primary = true
				elif track_path == "root/Plane/Plane:Key_1__inbetween__HalfKey":
					found_inbetween = true
			_require(found_primary, "Round-tripped blend shape scene lost the primary blend-shape track.")
			_require(found_inbetween, "Round-tripped blend shape scene lost the inbetween evaluator track.")

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 1, "Expected one argument: <save-path>")
	if failed:
		quit(1)
		return

	var save_path: String = args[0]
	DirAccess.remove_absolute(save_path)

	var packed_scene: PackedScene = ResourceLoader.load("res://tests/data/usd/blend_shape_basic.usda", "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load checked-in blend shape fixture.")
	if packed_scene == null:
		quit(1)
		return

	var save_error := ResourceSaver.save(packed_scene, save_path)
	_require(save_error == OK, "Failed to save blend shape round-trip USDA: %s" % save_path)

	var saved_text := FileAccess.get_file_as_string(save_path)
	_require(saved_text != "", "Saved blend shape round-trip USDA was empty.")
	_require_contains(saved_text, "def Skeleton \"Skel\"", "Blend shape round-trip save lost the skeleton prim.")
	_require_contains(saved_text, "def BlendShape \"Key_1\"", "Blend shape round-trip save lost the primary blend shape prim.")
	_require_contains(saved_text, "inbetweens:HalfKey", "Blend shape round-trip save lost the authored inbetween.")
	_require_contains(saved_text, "skel:blendShapeTargets", "Blend shape round-trip save lost the mesh blend-shape target relationship.")
	_require_contains(saved_text, "blendShapeWeights.timeSamples", "Blend shape round-trip save lost the blend-shape animation samples.")

	var reloaded_scene: PackedScene = ResourceLoader.load(save_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(reloaded_scene != null, "Failed to reload the saved blend shape round-trip USDA.")
	if reloaded_scene != null:
		var root: Node = reloaded_scene.instantiate()
		_require(root != null, "Failed to instantiate the saved blend shape round-trip USDA.")
		if root != null:
			_validate_loaded_roundtrip(root)
			root.free()

	quit(1 if failed else 0)
