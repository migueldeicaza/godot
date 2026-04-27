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
	_require(args.size() == 2, "Expected arguments: <variant-stage-usd-layer> <flattened-save-usdz-path>")

	DirAccess.remove_absolute(args[1])

	var packed_scene: PackedScene = ResourceLoader.load(args[0], "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load variant USD layer as PackedScene.")

	var root := packed_scene.instantiate()
	_require(root is UsdStageInstance, "Variant USD layer did not instantiate as a UsdStageInstance root.")
	var stage_instance := root as UsdStageInstance
	stage_instance.set("variants/Model/modelingVariant", "blue")

	_require(_save_instantiated_scene(root, args[1]) == OK, "Flattened USDZ composed export should save successfully.")
	root.free()

	var flattened_scene: PackedScene = ResourceLoader.load(args[1], "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(flattened_scene != null, "Failed to reload flattened USDZ export.")
	root = flattened_scene.instantiate()
	_require(not root is UsdStageInstance, "Flattened USDZ export should reload as a composed static scene, not a live UsdStageInstance.")
	_require(root.get_node_or_null("Model/BlueSphere") != null, "Flattened USDZ export should contain the selected composed branch.")
	_require(root.get_node_or_null("Model/RedCube") == null, "Flattened USDZ export should not contain inactive variant branches.")
	root.free()

	quit(1 if failed else 0)
