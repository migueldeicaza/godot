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

func _read_zip_root(path: String) -> Dictionary:
	var zip := ZIPReader.new()
	_require(zip.open(path) == OK, "Failed to open saved USDZ.")
	var files := zip.get_files()
	var root_text := ""
	if files.has("vehicleVariants.usda"):
		root_text = zip.read_file("vehicleVariants.usda", true).get_string_from_utf8()
	zip.close()
	return {
		"files": files,
		"root_text": root_text,
	}

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 3, "Expected arguments: <vehicle-usdz> <unchanged-save-usdz-path> <edited-save-usdz-path>")

	DirAccess.remove_absolute(args[1])
	DirAccess.remove_absolute(args[2])

	var packed_scene: PackedScene = ResourceLoader.load(args[0], "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load vehicle USDZ as PackedScene.")

	var root := packed_scene.instantiate()
	_require(root is UsdStageInstance, "Vehicle USDZ did not instantiate as a UsdStageInstance root.")
	_require(_save_instantiated_scene(root, args[1]) == OK, "Saving an unchanged source USDZ scene should preserve the package.")
	root.free()

	var source_size := FileAccess.get_file_as_bytes(args[0]).size()
	var saved_size := FileAccess.get_file_as_bytes(args[1]).size()
	_require(source_size == saved_size, "Unchanged USDZ save did not preserve the package byte size.")

	var zip_data := _read_zip_root(args[1])
	var files: PackedStringArray = zip_data["files"]
	var root_text: String = zip_data["root_text"]
	_require(files.size() > 10, "Preserved USDZ should keep the source package assets.")
	_require(files.has("vehicleVariants.usda"), "Preserved USDZ should keep the source root layer name.")
	_require(root_text.contains("variantSet \"wheels\""), "Preserved USDZ root layer lost its wheel variant set.")
	_require(root_text.contains("tractorFullAsset") and root_text.contains("ambulanceFullAsset"), "Preserved USDZ root layer lost inactive variant assets.")

	root = packed_scene.instantiate()
	_require(root is UsdStageInstance, "Vehicle USDZ did not instantiate as a UsdStageInstance root for edited save.")
	root.set("variants/vehicleVariant/wheels", "ambulance")
	_require(_save_instantiated_scene(root, args[2]) == OK, "Edited source USDZ variant save should preserve the package and author the default selection.")
	root.free()

	zip_data = _read_zip_root(args[2])
	files = zip_data["files"]
	root_text = zip_data["root_text"]
	_require(files.size() > 10, "Edited USDZ should keep the source package assets.")
	_require(files.has("vehicleVariants.usda"), "Edited USDZ should keep the source root layer name.")
	_require(root_text.contains("string wheels = \"ambulance\""), "Edited USDZ root layer did not author the selected wheel variant.")
	_require(root_text.contains("tractorFullAsset") and root_text.contains("ambulanceFullAsset"), "Edited USDZ root layer lost inactive variant assets.")

	var edited_packed_scene: PackedScene = ResourceLoader.load(args[2], "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(edited_packed_scene != null, "Failed to reload edited USDZ as PackedScene.")
	root = edited_packed_scene.instantiate()
	_require(root is UsdStageInstance, "Edited USDZ did not instantiate as a UsdStageInstance root.")
	_require(root.get_node_for_prim_path("/vehicleVariant/ambulanceFullAsset") != null, "Edited USDZ did not load with ambulance as the default vehicle.")
	_require(root.get_node_for_prim_path("/vehicleVariant/tractorFullAsset") == null, "Edited USDZ still loaded tractor as the default vehicle.")
	root.free()

	quit(1 if failed else 0)
