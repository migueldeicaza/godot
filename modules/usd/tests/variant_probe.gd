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
	_require(args.size() == 1, "Expected one argument: <variant_stage.usda>")

	var stage := UsdStageResource.new()
	var instance := UsdStageInstance.new()
	root.add_child(instance)
	instance.stage = stage

	var source_path: String = args[0]
	stage.source_path = source_path
	_require(stage.get_stage_metadata().get("usd:default_prim_path", "") == "/Model", "Stage metadata did not record the default prim.")

	var source_uid_id := ResourceUID.create_id_for_path(source_path)
	ResourceUID.add_id(source_uid_id, source_path)
	var source_uid := ResourceUID.id_to_text(source_uid_id)
	var uid_stage := UsdStageResource.new()
	uid_stage.source_path = source_uid
	_require(uid_stage.get_stage_metadata().get("usd:default_prim_path", "") == "/Model", "Stage metadata did not resolve uid:// source paths.")

	var variant_sets := stage.get_variant_sets()
	_require(variant_sets.has("/Model"), "Variant catalog did not include /Model.")
	var model_sets: Dictionary = variant_sets["/Model"]
	_require(model_sets.has("modelingVariant"), "Variant catalog did not include modelingVariant.")
	var modeling_variant: Dictionary = model_sets["modelingVariant"]
	_require(modeling_variant.get("selection", "") == "red", "Unexpected default variant selection.")
	_require(modeling_variant.get("variants", []).has("blue"), "Variant catalog did not include blue.")

	_require(instance.get_node_for_prim_path("/Model/RedCube") != null, "Stage resource change did not rebuild the instance.")
	_require(instance.get_node_for_prim_path("/Model/RedCube") != null, "Initial rebuild did not include RedCube.")
	_require(instance.get_node_for_prim_path("/Model/BlueSphere") == null, "Initial rebuild unexpectedly included BlueSphere.")

	var attachment := Node3D.new()
	attachment.name = "Attachment"
	instance.add_child(attachment)

	var has_variant_group := false
	var has_variant_property := false
	for property in instance.get_property_list():
		if property.get("name", "") == "USD Variants":
			has_variant_group = true
			_require((int(property.get("usage", 0)) & PROPERTY_USAGE_GROUP) != 0, "Variant inspector group did not use group property usage.")
		if property.get("name", "") == "variant_selections":
			_require((int(property.get("usage", 0)) & PROPERTY_USAGE_EDITOR) == 0, "Raw variant_selections dictionary should not be the primary inspector editing surface.")
		if property.get("name", "") == "variants/Model/modelingVariant":
			has_variant_property = true
			_require(String(property.get("hint_string", "")).contains("red"), "Variant property hint did not include red.")
			_require(String(property.get("hint_string", "")).contains("blue"), "Variant property hint did not include blue.")
	_require(has_variant_group, "Instance did not expose a USD Variants inspector group.")
	_require(has_variant_property, "Instance did not expose modelingVariant as an inspector property.")
	_require(instance.get("variants/Model/modelingVariant") == "red", "Variant property did not read the default selection.")

	instance.set("variants/Model/modelingVariant", "blue")
	_require(instance.get("variants/Model/modelingVariant") == "blue", "Variant property did not update to blue.")
	_require(instance.variant_selections.get("/Model", {}).get("modelingVariant", "") == "blue", "Variant property did not update variant_selections.")
	_require(instance.get_node_or_null("Attachment") == attachment, "Rebuild removed a user-owned child.")
	_require(instance.get_node_for_prim_path("/Model/BlueSphere") != null, "Variant rebuild did not include BlueSphere.")
	_require(instance.get_node_for_prim_path("/Model/RedCube") == null, "Variant rebuild still included RedCube.")

	var has_nested_variant_property := false
	for property in instance.get_property_list():
		if property.get("name", "") == "variants/Model/Nested/detail":
			has_nested_variant_property = true
			_require(String(property.get("hint_string", "")).contains("cube"), "Nested variant property hint did not include cube.")
			_require(String(property.get("hint_string", "")).contains("sphere"), "Nested variant property hint did not include sphere.")
	_require(has_nested_variant_property, "Instance did not refresh the variant catalog from the selected branch.")
	_require(instance.get("variants/Model/Nested/detail") == "cube", "Nested variant property did not read the composed default selection.")

	instance.set("variants/Model/Nested/detail", "sphere")
	_require(instance.get("variants/Model/Nested/detail") == "sphere", "Nested variant property did not update to sphere.")
	_require(instance.get_node_for_prim_path("/Model/Nested/NestedSphere") != null, "Nested variant rebuild did not include NestedSphere.")
	_require(instance.get_node_for_prim_path("/Model/Nested/NestedCube") == null, "Nested variant rebuild still included NestedCube.")

	var ordered_instance := UsdStageInstance.new()
	root.add_child(ordered_instance)
	ordered_instance.stage = stage
	var ordered_selections := {}
	ordered_selections["/Model/Nested"] = { "detail": "sphere" }
	ordered_selections["/Model"] = { "modelingVariant": "blue" }
	ordered_instance.variant_selections = ordered_selections
	_require(ordered_instance.get_node_for_prim_path("/Model/Nested/NestedSphere") != null, "Depth-sorted variant selections did not apply a nested selection after its parent.")

	quit(1 if failed else 0)
