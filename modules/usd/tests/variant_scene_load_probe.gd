extends SceneTree

var failed := false

func _fail(message: String) -> void:
	failed = true
	push_error(message)

func _require(condition: bool, message: String) -> void:
	if not condition:
		_fail(message)

func _count_generated_roots(node: Node) -> int:
	var count := 0
	for child in node.get_children():
		if child.name == "_Generated":
			count += 1
	return count

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 2, "Expected arguments: <variant-stage> <save-usdz-path>")

	var packed_scene: PackedScene = ResourceLoader.load(args[0], "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load variant USD as PackedScene.")

	var root := packed_scene.instantiate()
	_require(root is UsdStageInstance, "Variant USD did not instantiate as a UsdStageInstance root.")
	var instance := root as UsdStageInstance
	_require(instance.stage != null, "UsdStageInstance root has no stage resource.")
	_require(instance.stage.source_path.get_file() == args[0].get_file(), "UsdStageInstance stage source path did not point at the loaded USD.")
	_require(instance.get_node_or_null("_Generated") != null, "UsdStageInstance root did not generate USD children.")
	_require(_count_generated_roots(instance) == 1, "UsdStageInstance root had duplicate _Generated children after instantiation.")
	_require(instance.get_node_for_prim_path("/Model") != null, "Generated variant scene did not include the expected root prim.")

	var saved_scene := PackedScene.new()
	_require(saved_scene.pack(root) == OK, "Failed to pack instantiated variant scene.")
	_require(ResourceSaver.save(saved_scene, args[1]) == OK, "Failed to save variant scene as USDZ.")
	_require(FileAccess.file_exists(args[1]), "Saved USDZ file was not created.")

	root.free()
	quit(1 if failed else 0)
