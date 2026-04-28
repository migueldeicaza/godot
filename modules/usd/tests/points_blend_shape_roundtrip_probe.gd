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

func _validate_points_blend_shape_import(root_node: Node) -> void:
	var points_instance := _find_prim_node(root_node, "/root/Cloud/Cloud") as MeshInstance3D
	_require(points_instance != null, "Point blend shape fixture lost the points prim.")
	if points_instance == null or points_instance.mesh == null:
		return

	_require(points_instance.mesh.surface_get_primitive_type(0) == Mesh.PRIMITIVE_POINTS, "Point blend shape fixture should stay on point primitives.")
	_require(points_instance.get_blend_shape_count() == 2, "Point blend shape fixture should expose primary and inbetween channels.")
	_require(points_instance.find_blend_shape_by_name(&"Puff") == 0, "Point blend shape fixture lost the primary blend shape name.")
	_require(points_instance.find_blend_shape_by_name(&"Puff__inbetween__HalfPuff") == 1, "Point blend shape fixture lost the generated inbetween channel.")

	var metadata := _metadata(points_instance)
	_require(metadata.get("usd:points_mapping", "") == "mesh_points", "Point blend shape fixture should preserve its points mapping metadata.")
	_require(metadata.get("usd:blend_shape_mapping", "") == "array_points_relative_piecewise", "Point blend shape fixture should record point-based blend shape mapping.")

	var blend_shape_arrays: Array = points_instance.mesh.surface_get_blend_shape_arrays(0)
	_require(blend_shape_arrays.size() == 2, "Point blend shape fixture should keep one primary and one inbetween surface.")
	if blend_shape_arrays.size() == 2:
		var primary_surface: Array = blend_shape_arrays[0]
		var inbetween_surface: Array = blend_shape_arrays[1]
		var primary_vertices: PackedVector3Array = primary_surface[Mesh.ARRAY_VERTEX]
		var inbetween_vertices: PackedVector3Array = inbetween_surface[Mesh.ARRAY_VERTEX]
		_require(primary_vertices.size() == 4, "Point blend shape fixture primary deltas lost point count.")
		_require(inbetween_vertices.size() == 4, "Point blend shape fixture inbetween deltas lost point count.")
		_require(is_equal_approx(primary_vertices[0].z, 0.25), "Point blend shape fixture lost the primary point delta.")
		_require(is_equal_approx(inbetween_vertices[3].z, 0.15), "Point blend shape fixture lost the inbetween point delta.")

	var animation_player := _find_animation_player(root_node)
	_require(animation_player != null, "Point blend shape fixture lost its animation player.")
	if animation_player == null or not animation_player.has_animation(&"Anim"):
		return

	var animation: Animation = animation_player.get_animation(&"Anim")
	_require(animation != null, "Point blend shape fixture returned a null animation.")
	if animation == null:
		return

	var found_primary := false
	var found_inbetween := false
	for track_index in range(animation.get_track_count()):
		var track_path := str(animation.track_get_path(track_index))
		if animation.track_get_type(track_index) == Animation.TYPE_BLEND_SHAPE and track_path == "root/Cloud/Cloud:Puff":
			found_primary = true
		elif animation.track_get_type(track_index) == Animation.TYPE_BLEND_SHAPE and track_path == "root/Cloud/Cloud:Puff__inbetween__HalfPuff":
			found_inbetween = true
	_require(found_primary, "Point blend shape fixture lost the baked primary blend shape track.")
	_require(found_inbetween, "Point blend shape fixture lost the baked inbetween blend shape track.")

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 1, "Expected one argument: <save-path>")
	if failed:
		quit(1)
		return

	var save_path: String = args[0]
	var source_path := "res://tests/data/usd/points_blend_shape_basic.usda"
	var packed_scene: PackedScene = ResourceLoader.load(source_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load point blend shape fixture.")
	if packed_scene == null:
		quit(1)
		return

	var imported_root: Node = packed_scene.instantiate()
	_require(imported_root != null, "Failed to instantiate point blend shape fixture.")
	if imported_root != null:
		_validate_points_blend_shape_import(imported_root)
		imported_root.free()

	DirAccess.remove_absolute(save_path)
	_require(ResourceSaver.save(packed_scene, save_path) == OK, "Failed to save point blend shape round-trip fixture.")
	var saved_text := FileAccess.get_file_as_string(save_path)
	_require_contains(saved_text, "def Points \"Cloud\"", "Point blend shape round-trip should save the prim back as UsdGeomPoints.")
	_require_contains(saved_text, "primvars:displayColor", "Point blend shape round-trip should preserve point colors.")
	_require_contains(saved_text, "float[] widths = [0.25]", "Point blend shape round-trip should preserve point widths.")
	_require_contains(saved_text, "def BlendShape \"Puff\"", "Point blend shape round-trip save lost the point blend shape prim.")
	_require_contains(saved_text, "skel:blendShapeTargets", "Point blend shape round-trip save lost the point blend-shape relationship.")
	_require_contains(saved_text, "blendShapeWeights.timeSamples", "Point blend shape round-trip save lost blend-shape animation samples.")

	var reloaded_scene: PackedScene = ResourceLoader.load(save_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(reloaded_scene != null, "Failed to reload point blend shape round-trip save.")
	if reloaded_scene != null:
		var root_node: Node = reloaded_scene.instantiate()
		_require(root_node != null, "Failed to instantiate point blend shape round-trip save.")
		if root_node != null:
			_validate_points_blend_shape_import(root_node)
			root_node.free()

	quit(1 if failed else 0)
