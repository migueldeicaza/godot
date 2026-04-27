extends SceneTree

const USD_PREVIEW_LIGHTING_MODE_SETTING := "filesystem/import/usd/preview_lighting_mode"

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

func _require(condition: bool, message: String) -> void:
	if not condition:
		_fail(message)

func _is_generated_preview(node: Node) -> bool:
	return bool(node.get_meta(&"usd", {}).get("usd:generated_preview", false))

func _first_non_preview_child(node: Node) -> Node:
	for child in node.get_children():
		if child is Node and not _is_generated_preview(child):
			return child
	return null

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 4, "Expected 4 arguments: <source> <save> <metadata_key> <path>")
	ProjectSettings.set_setting(USD_PREVIEW_LIGHTING_MODE_SETTING, 0)

	var source_path: String = args[0]
	var save_path: String = args[1]
	var metadata_key: String = args[2]
	var expected_path: String = args[3]

	var packed_scene: PackedScene = load(source_path)
	_require(packed_scene != null, "Failed to load source scene: %s" % source_path)

	var root := packed_scene.instantiate()
	_require(root != null, "Failed to instantiate source scene.")

	var usd_root := _first_non_preview_child(root)
	_require(usd_root != null, "Expected one authored USD root child.")
	var composition_node := _first_non_preview_child(usd_root)
	_require(composition_node != null, "Expected one authored composition node under USD root.")
	var metadata: Dictionary = composition_node.get_meta(&"usd", {})
	_require(metadata.get("usd:composition_preservation_mode", "") == "read_only", "Composition node did not preserve read-only mode.")
	var entries: Array = metadata.get(metadata_key, [])
	_require(entries.size() == 1, "Expected one preserved composition path in metadata.")
	_require(entries[0] == expected_path, "Unexpected preserved composition path.")
	root.free()

	var save_error := ResourceSaver.save(packed_scene, save_path)
	_require(save_error == OK, "Failed to save USDA: %s" % save_path)

	var saved_text := FileAccess.get_file_as_string(save_path)
	_require(saved_text != "", "Saved USDA text was empty.")
	_require(saved_text.contains(expected_path), "Saved USDA did not preserve the composition path.")
	if metadata_key == "usd:inherits":
		_require(saved_text.contains("inherits ="), "Saved USDA did not author an inherits arc.")
	else:
		_require(saved_text.contains("specializes ="), "Saved USDA did not author a specializes arc.")

	quit(0)
