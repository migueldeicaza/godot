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

func _load_fixture(path: String) -> Node:
	var packed_scene: PackedScene = ResourceLoader.load(path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load BasisCurves fixture: %s" % path)
	if packed_scene == null:
		return null
	var loaded_root: Node = packed_scene.instantiate()
	_require(loaded_root != null, "Failed to instantiate BasisCurves fixture: %s" % path)
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

func _path_child(parent: Node3D, index: int) -> Path3D:
	_require(parent.get_child_count() > index, "Expected Path3D child %d under %s." % [index, parent.name])
	if parent.get_child_count() <= index:
		return null
	var path := parent.get_child(index) as Path3D
	_require(path != null, "Expected child %d under %s to be a Path3D." % [index, parent.name])
	return path

func _test_linear(base_dir: String) -> void:
	var fixture := _load_fixture(base_dir.path_join("usd_curve_linear_all.usda"))
	if fixture == null:
		return

	var periodic := _find_prim_node(fixture, "/root/linear_periodic/single/linear_periodic_single_varying") as Node3D
	_require(periodic != null, "Linear periodic BasisCurves prim was not imported.")
	if periodic != null:
		var periodic_metadata := _metadata(periodic)
		_require(periodic_metadata.get("usd:curve_mapping", "") == "path3d_children", "Linear periodic BasisCurves should use Path3D children mapping.")
		_require(periodic_metadata.get("usd:curve_type", "") == "linear", "Linear periodic BasisCurves lost type metadata.")
		_require(periodic_metadata.get("usd:curve_wrap", "") == "periodic", "Linear periodic BasisCurves lost wrap metadata.")
		_require(periodic_metadata.get("usd:curve_widths_interpolation", "") == "varying", "Linear periodic BasisCurves lost widths interpolation metadata.")
		var path := _path_child(periodic, 0)
		if path != null:
			_require(path.curve != null, "Linear periodic Path3D was missing its Curve3D.")
			_require(path.curve.get_point_count() == 5, "Linear periodic Path3D did not preserve the authored point count.")
			_require(path.curve.is_closed(), "Linear periodic Path3D should be closed.")

	var multiple := _find_prim_node(fixture, "/root/linear_nonperiodic/multiple/linear_nonperiodic_multiple_varying") as Node3D
	_require(multiple != null, "Linear multiple BasisCurves prim was not imported.")
	if multiple != null:
		var multiple_metadata := _metadata(multiple)
		_require(int(multiple_metadata.get("usd:curve_count", 0)) == 4, "Linear multiple BasisCurves lost its authored curve count.")
		_require(int(multiple_metadata.get("usd:generated_curve_children", 0)) == 4, "Linear multiple BasisCurves did not produce one Path3D per authored curve.")
		var third_path := _path_child(multiple, 2)
		if third_path != null:
			_require(third_path.curve.get_point_count() == 4, "Linear multiple BasisCurves third curve did not preserve its authored vertex count.")
			_require(not third_path.curve.is_closed(), "Linear nonperiodic Path3D should not be closed.")

	_release_fixture(fixture)

func _test_bezier(base_dir: String) -> void:
	var fixture := _load_fixture(base_dir.path_join("usd_curve_bezier_all.usda"))
	if fixture == null:
		return

	var single := _find_prim_node(fixture, "/root/bezier_nonperiodic/single/bezier_nonperiodic_single_vertex") as Node3D
	_require(single != null, "Bezier single BasisCurves prim was not imported.")
	if single != null:
		var single_metadata := _metadata(single)
		_require(single_metadata.get("usd:curve_basis", "") == "bezier", "Bezier BasisCurves lost basis metadata.")
		_require(single_metadata.get("usd:curve_type", "") == "cubic", "Bezier BasisCurves lost type metadata.")
		var path := _path_child(single, 0)
		if path != null:
			_require(path.curve.get_point_count() == 2, "Bezier single Path3D should have 2 anchor points.")
			_require(path.curve.get_point_out(0).length() > 0.0, "Bezier single Path3D lost its outgoing handle.")
			_require(path.curve.get_point_in(1).length() > 0.0, "Bezier single Path3D lost its incoming handle.")

	var periodic_multiple := _find_prim_node(fixture, "/root/bezier_periodic/multiple/bezier_periodic_multiple_vertex") as Node3D
	_require(periodic_multiple != null, "Bezier periodic multiple BasisCurves prim was not imported.")
	if periodic_multiple != null:
		var multiple_metadata := _metadata(periodic_multiple)
		_require(int(multiple_metadata.get("usd:curve_count", 0)) == 4, "Bezier periodic multiple BasisCurves lost its authored curve count.")
		_require(int(multiple_metadata.get("usd:generated_curve_children", 0)) == 4, "Bezier periodic multiple BasisCurves did not produce one Path3D per authored curve.")
		var first_path := _path_child(periodic_multiple, 0)
		var last_path := _path_child(periodic_multiple, 3)
		if first_path != null:
			_require(first_path.curve.is_closed(), "Bezier periodic Path3D should be closed.")
			_require(first_path.curve.get_point_count() == 2, "Bezier periodic first Path3D should have 2 anchor points.")
		if last_path != null:
			_require(last_path.curve.get_point_count() == 5, "Bezier periodic last Path3D should have 5 anchor points.")

	_release_fixture(fixture)

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 1, "Expected one argument: <blender-test-data-usd directory>")
	if failed:
		quit(1)
		return

	var fixture_dir: String = args[0]
	_test_linear(fixture_dir)
	_test_bezier(fixture_dir)

	quit(1 if failed else 0)
