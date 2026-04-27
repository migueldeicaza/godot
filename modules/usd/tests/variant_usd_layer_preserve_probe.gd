extends SceneTree

var failed := false

func _fail(message: String) -> void:
	failed = true
	push_error(message)

func _require(condition: bool, message: String) -> void:
	if not condition:
		_fail(message)

func _save_instantiated_scene(root: Node, path: String) -> int:
	var saved_scene := PackedScene.new()
	var pack_error := saved_scene.pack(root)
	if pack_error != OK:
		return pack_error
	return ResourceSaver.save(saved_scene, path)

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 3, "Expected arguments: <variant-stage-usd-layer> <unchanged-save-path> <edited-save-path>")

	DirAccess.remove_absolute(args[1])
	DirAccess.remove_absolute(args[2])

	var packed_scene: PackedScene = ResourceLoader.load(args[0], "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load variant USD layer as PackedScene.")

	var root := packed_scene.instantiate()
	_require(root is UsdStageInstance, "Variant USD layer did not instantiate as a UsdStageInstance root.")
	_require(_save_instantiated_scene(root, args[1]) == OK, "Saving an unchanged source USD layer should preserve the layer.")
	root.free()

	var source_text := FileAccess.get_file_as_string(args[0])
	var saved_text := FileAccess.get_file_as_string(args[1])
	_require(source_text == saved_text, "Unchanged USD layer save did not preserve the layer contents.")

	root = packed_scene.instantiate()
	_require(root is UsdStageInstance, "Variant USD layer did not instantiate as a UsdStageInstance root for edited save.")
	root.set("variants/Model/modelingVariant", "blue")
	root.set("variants/Model/Nested/detail", "sphere")
	_require(_save_instantiated_scene(root, args[2]) == OK, "Edited source USD layer variant save should author the default selections.")
	root.free()

	var edited_text := FileAccess.get_file_as_string(args[2])
	_require(edited_text.begins_with("#usda"), "Edited USD layer should remain a text USDA layer.")
	_require(edited_text.contains("string modelingVariant = \"blue\""), "Edited USD layer did not author the selected model variant.")
	_require(edited_text.contains("string detail = \"sphere\""), "Edited USD layer did not author the selected nested variant.")
	_require(edited_text.contains("RedCube") and edited_text.contains("BlueSphere"), "Edited USD layer lost inactive variant content.")

	var edited_packed_scene: PackedScene = ResourceLoader.load(args[2], "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(edited_packed_scene != null, "Failed to reload edited USD layer as PackedScene.")
	root = edited_packed_scene.instantiate()
	_require(root is UsdStageInstance, "Edited USD layer did not instantiate as a UsdStageInstance root.")
	_require(root.get("variants/Model/modelingVariant") == "blue", "Edited USD layer did not load with blue as the default model variant.")
	_require(root.get("variants/Model/Nested/detail") == "sphere", "Edited USD layer did not load with sphere as the nested default variant.")
	_require(root.get_node_for_prim_path("/Model/BlueSphere") != null, "Edited USD layer did not load the blue branch.")
	_require(root.get_node_for_prim_path("/Model/RedCube") == null, "Edited USD layer still loaded the red branch.")
	_require(root.get_node_for_prim_path("/Model/Nested/NestedSphere") != null, "Edited USD layer did not load the nested sphere branch.")
	_require(root.get_node_for_prim_path("/Model/Nested/NestedCube") == null, "Edited USD layer still loaded the nested cube branch.")
	root.free()

	quit(1 if failed else 0)
