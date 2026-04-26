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

	quit(1 if failed else 0)
