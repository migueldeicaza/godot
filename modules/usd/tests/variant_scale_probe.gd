extends SceneTree

var failed := false

func _fail(message: String) -> void:
	failed = true
	push_error(message)

func _require(condition: bool, message: String) -> void:
	if not condition:
		_fail(message)

func _max_global_aabb_axis(node: Node, parent_transform := Transform3D.IDENTITY) -> float:
	var max_axis := 0.0
	var node_transform := parent_transform
	if node is Node3D:
		node_transform = parent_transform * (node as Node3D).transform
	if node is MeshInstance3D:
		var mesh_instance := node as MeshInstance3D
		var local_aabb := mesh_instance.get_aabb()
		var global_aabb := node_transform * local_aabb
		max_axis = max(max_axis, global_aabb.size.x, global_aabb.size.y, global_aabb.size.z)
	for child in node.get_children():
		max_axis = max(max_axis, _max_global_aabb_axis(child, node_transform))
	return max_axis

func _generated_scale(instance: UsdStageInstance) -> Vector3:
	var generated := instance.get_node_or_null("_Generated")
	_require(generated is Node3D, "UsdStageInstance did not have a Node3D _Generated root.")
	return (generated as Node3D).transform.basis.get_scale()

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 1, "Expected one argument: <vehicleVariants.selfcontained.usdz>")

	var packed_scene: PackedScene = ResourceLoader.load(args[0], "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load variant USD as PackedScene.")
	var loaded_root := packed_scene.instantiate()
	_require(loaded_root is UsdStageInstance, "Variant USD did not instantiate as UsdStageInstance.")
	var instance := loaded_root as UsdStageInstance
	root.add_child(instance)

	var initial_scale := _generated_scale(instance)
	var initial_axis := _max_global_aabb_axis(instance)
	instance.set("variants/vehicleVariant/wheels", "sedan")
	var sedan_scale := _generated_scale(instance)
	instance.set("variants/vehicleVariant/wheels", "tractor")
	var tractor_scale := _generated_scale(instance)
	var tractor_axis := _max_global_aabb_axis(instance)

	print("scale_probe initial_scale=", initial_scale, " initial_axis=", initial_axis)
	print("scale_probe tractor_scale=", tractor_scale, " tractor_axis=", tractor_axis)
	_require(initial_scale.is_equal_approx(sedan_scale), "Initial generated root scale did not match rebuilt generated root scale.")
	_require(sedan_scale.is_equal_approx(tractor_scale), "Generated root scale changed between rebuilds.")
	_require(is_equal_approx(initial_axis, tractor_axis), "Initial tractor bounds did not match rebuilt tractor bounds.")

	loaded_root.free()
	quit(1 if failed else 0)
