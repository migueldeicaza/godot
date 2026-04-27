extends SceneTree

var failed := false

func _fail(message: String) -> void:
	failed = true
	push_error(message)

func _require(condition: bool, message: String) -> void:
	if not condition:
		_fail(message)

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 1, "Expected one argument: <vehicleVariants.selfcontained.usdz>")

	var stage := UsdStageResource.new()
	stage.source_path = args[0]

	var instance := UsdStageInstance.new()
	root.add_child(instance)
	instance.stage = stage
	instance.rebuild()

	_require(instance.get_node_for_prim_path("/vehicleVariant/tractorFullAsset") != null, "Default vehicle branch did not include tractorFullAsset.")
	_require(instance.get_node_for_prim_path("/vehicleVariant/sedanFullAsset") == null, "Default vehicle branch unexpectedly included sedanFullAsset.")
	_require(instance.get_node_for_prim_path("/vehicleVariant/tractorFullAsset/wheel1/wheelWideAsset") != null, "Default tractor branch did not include wheel1 wheelWideAsset.")

	var generated_root := instance.get_node_or_null("_Generated")
	var tractor_node := instance.get_node_for_prim_path("/vehicleVariant/tractorFullAsset")
	instance.set("variants/vehicleVariant/wheels", "sedan")
	_require(instance.get_node_or_null("_Generated") == generated_root, "Vehicle rebuild replaced the generated root instead of refreshing its children.")
	_require(not is_instance_valid(tractor_node), "Vehicle rebuild did not free the previous tractor node.")
	_require(instance.get("variants/vehicleVariant/wheels") == "sedan", "Vehicle variant property did not update to sedan.")
	_require(instance.get_node_for_prim_path("/vehicleVariant/sedanFullAsset") != null, "Vehicle rebuild did not include sedanFullAsset.")
	_require(instance.get_node_for_prim_path("/vehicleVariant/tractorFullAsset") == null, "Vehicle rebuild still included tractorFullAsset.")

	instance.set("variants/vehicleVariant/wheels", "tractor")
	_require(instance.get_node_for_prim_path("/vehicleVariant/tractorFullAsset") != null, "Vehicle rebuild did not restore tractorFullAsset.")
	var wide_wheel_node := instance.get_node_for_prim_path("/vehicleVariant/tractorFullAsset/wheel1/wheelWideAsset")
	instance.set("variants/vehicleVariant/tractorFullAsset/wheel1/wheels", "wheelRed")
	_require(not is_instance_valid(wide_wheel_node), "Wheel rebuild did not free the previous wheelWide node.")
	_require(instance.get("variants/vehicleVariant/tractorFullAsset/wheel1/wheels") == "wheelRed", "Wheel variant property did not update to wheelRed.")
	_require(instance.get_node_for_prim_path("/vehicleVariant/tractorFullAsset/wheel1/wheelRedAsset") != null, "Wheel rebuild did not include wheelRedAsset.")
	_require(instance.get_node_for_prim_path("/vehicleVariant/tractorFullAsset/wheel1/wheelWideAsset") == null, "Wheel rebuild still included wheelWideAsset.")

	var adopted_instance := UsdStageInstance.new()
	root.add_child(adopted_instance)
	var stale_generated := Node3D.new()
	stale_generated.name = "_Generated"
	adopted_instance.add_child(stale_generated)
	var stale_child := Node3D.new()
	stale_child.name = "StaleTractor"
	stale_generated.add_child(stale_child)
	adopted_instance.stage = stage
	adopted_instance.rebuild()
	_require(adopted_instance.get_node_or_null("_Generated") == stale_generated, "Instance did not adopt an existing generated root.")
	_require(not is_instance_valid(stale_child), "Adopted generated root did not free stale children.")
	adopted_instance.set("variants/vehicleVariant/wheels", "formula")
	_require(adopted_instance.get_node_or_null("_Generated") == stale_generated, "Adopted generated root was replaced during rebuild.")
	_require(adopted_instance.get_node_for_prim_path("/vehicleVariant/formulaFullAsset") != null, "Adopted generated root did not receive rebuilt formula branch.")

	var owned_scene := Node3D.new()
	root.add_child(owned_scene)
	var owned_instance := UsdStageInstance.new()
	owned_scene.add_child(owned_instance)
	owned_instance.owner = owned_scene
	owned_instance.stage = stage
	owned_instance.rebuild()
	var owned_generated := owned_instance.get_node_or_null("_Generated")
	_require(owned_generated != null, "Owned instance did not create generated root.")
	_require(owned_generated.owner == owned_scene, "Generated root did not inherit the instance owner.")
	owned_instance.set("variants/vehicleVariant/wheels", "ambulance")
	var ambulance_node := owned_instance.get_node_for_prim_path("/vehicleVariant/ambulanceFullAsset")
	_require(ambulance_node != null, "Owned instance did not rebuild to ambulanceFullAsset.")
	_require(ambulance_node.owner == owned_scene, "Generated rebuild child did not inherit the instance owner.")

	quit(1 if failed else 0)
