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
	_require(args.size() == 3, "Expected arguments: <variant-stage-usdc> <unchanged-save-usdc-path> <edited-save-usdc-path>")

	DirAccess.remove_absolute(args[1])
	DirAccess.remove_absolute(args[2])

	var packed_scene: PackedScene = ResourceLoader.load(args[0], "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load variant USDC as PackedScene.")

	var root := packed_scene.instantiate()
	_require(root is UsdStageInstance, "Variant USDC did not instantiate as a UsdStageInstance root.")
	_require(_save_instantiated_scene(root, args[1]) == OK, "Saving an unchanged source USDC scene should preserve the layer.")
	root.free()

	var source_size := FileAccess.get_file_as_bytes(args[0]).size()
	var saved_size := FileAccess.get_file_as_bytes(args[1]).size()
	_require(source_size == saved_size, "Unchanged USDC save did not preserve the layer byte size.")

	root = packed_scene.instantiate()
	_require(root is UsdStageInstance, "Variant USDC did not instantiate as a UsdStageInstance root for edited save.")
	root.set("variants/Model/modelingVariant", "blue")
	root.set("variants/Model/Nested/detail", "sphere")
	_require(_save_instantiated_scene(root, args[2]) == OK, "Edited source USDC variant save should author the default selections.")
	root.free()

	var edited_packed_scene: PackedScene = ResourceLoader.load(args[2], "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(edited_packed_scene != null, "Failed to reload edited USDC as PackedScene.")
	root = edited_packed_scene.instantiate()
	_require(root is UsdStageInstance, "Edited USDC did not instantiate as a UsdStageInstance root.")
	_require(root.get("variants/Model/modelingVariant") == "blue", "Edited USDC did not load with blue as the default model variant.")
	_require(root.get("variants/Model/Nested/detail") == "sphere", "Edited USDC did not load with sphere as the nested default variant.")
	_require(root.get_node_for_prim_path("/Model/BlueSphere") != null, "Edited USDC did not load the blue branch.")
	_require(root.get_node_for_prim_path("/Model/RedCube") == null, "Edited USDC still loaded the red branch.")
	_require(root.get_node_for_prim_path("/Model/Nested/NestedSphere") != null, "Edited USDC did not load the nested sphere branch.")
	_require(root.get_node_for_prim_path("/Model/Nested/NestedCube") == null, "Edited USDC still loaded the nested cube branch.")
	root.free()

	quit(1 if failed else 0)
