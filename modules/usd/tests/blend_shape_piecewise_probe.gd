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

func _init() -> void:
	var packed_scene: PackedScene = ResourceLoader.load("res://tests/data/usd/blend_shape_basic.usda", "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load checked-in blend shape fixture.")
	if packed_scene == null:
		quit(1)
		return

	var loaded_root: Node = packed_scene.instantiate()
	_require(loaded_root != null, "Failed to instantiate checked-in blend shape fixture.")
	if loaded_root == null:
		quit(1)
		return
	root.add_child(loaded_root)

	var mesh := _find_prim_node(loaded_root, "/root/Plane/Plane") as MeshInstance3D
	_require(mesh != null, "Checked-in blend shape fixture lost the mesh node.")
	if mesh != null:
		_require(mesh.get_blend_shape_count() == 2, "Piecewise blend shape fixture should expose primary and inbetween channels.")
		_require(mesh.find_blend_shape_by_name(&"Key_1") == 0, "Piecewise blend shape fixture lost the primary blend shape channel.")
		_require(mesh.find_blend_shape_by_name(&"Key_1__inbetween__HalfKey") == 1, "Piecewise blend shape fixture lost the generated inbetween channel.")
		var metadata: Dictionary = _metadata(mesh)
		_require(metadata.get("usd:blend_shape_mapping", "") == "array_mesh_relative_piecewise", "Piecewise blend shape fixture should record the evaluator mapping.")
		var channels: Dictionary = metadata.get("usd:blend_shape_channels", {})
		_require(channels.has("Key_1"), "Piecewise blend shape fixture lost its channel mapping metadata.")
		var key_channels: Array = channels.get("Key_1", [])
		_require(key_channels.size() == 2, "Piecewise blend shape fixture should record one primary and one inbetween channel.")
		if mesh.mesh != null:
			var blend_shape_arrays: Array = mesh.mesh.surface_get_blend_shape_arrays(0)
			_require(blend_shape_arrays.size() == 2, "Piecewise blend shape fixture should build two surface blend shape arrays.")

	var animation_player: AnimationPlayer = null
	for child in loaded_root.get_children():
		if child is AnimationPlayer:
			animation_player = child
			break
	_require(animation_player != null, "Piecewise blend shape fixture did not produce an AnimationPlayer.")
	if animation_player != null:
		_require(animation_player.has_animation(&"Anim"), "Piecewise blend shape fixture lost the baked animation.")
		var animation: Animation = animation_player.get_animation(&"Anim")
		_require(animation != null, "Piecewise blend shape fixture returned a null animation.")
		if animation != null:
			var primary_track := -1
			var inbetween_track := -1
			for track_index in range(animation.get_track_count()):
				if animation.track_get_type(track_index) != Animation.TYPE_BLEND_SHAPE:
					continue
				var track_path := str(animation.track_get_path(track_index))
				if track_path == "root/Plane/Plane:Key_1":
					primary_track = track_index
				elif track_path == "root/Plane/Plane:Key_1__inbetween__HalfKey":
					inbetween_track = track_index

			_require(primary_track >= 0, "Piecewise blend shape fixture did not bake the primary track.")
			_require(inbetween_track >= 0, "Piecewise blend shape fixture did not bake the inbetween evaluator track.")
			if primary_track >= 0:
				_require(animation.track_get_key_count(primary_track) == 5, "Primary piecewise track should insert crossing keys.")
				_require(is_equal_approx(float(animation.track_get_key_value(primary_track, 2)), 0.5), "Primary piecewise track should hand over half weight at the middle sample.")
			if inbetween_track >= 0:
				_require(animation.track_get_key_count(inbetween_track) == 5, "Inbetween piecewise track should insert crossing keys.")
				_require(is_equal_approx(float(animation.track_get_key_value(inbetween_track, 1)), 1.0), "Inbetween piecewise track should peak at the threshold crossing.")
				_require(is_equal_approx(float(animation.track_get_key_value(inbetween_track, 2)), 0.5), "Inbetween piecewise track should share weight at the 0.75 sample.")

	loaded_root.queue_free()
	quit(1 if failed else 0)
