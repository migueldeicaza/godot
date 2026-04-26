extends SceneTree

const USD_PREVIEW_LIGHTING_MODE_SETTING := "filesystem/import/usd/preview_lighting_mode"

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

func _require(condition: bool, message: String) -> void:
	if not condition:
		_fail(message)

func _require_contains(haystack: String, needle: String, message: String) -> void:
	_require(haystack.contains(needle), "%s Missing: %s" % [message, needle])

func _require_not_contains(haystack: String, needle: String, message: String) -> void:
	_require(not haystack.contains(needle), "%s Unexpected: %s" % [message, needle])

func _is_generated_preview(node: Node) -> bool:
	return bool(node.get_meta(&"usd", {}).get("usd:generated_preview", false))

func _first_non_preview_child(node: Node) -> Node:
	for child in node.get_children():
		if child is Node and not _is_generated_preview(child):
			return child
	return null

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 5, "Expected 5 arguments: <source> <save> <metadata_key> <asset_path> <prim_path>")
	ProjectSettings.set_setting(USD_PREVIEW_LIGHTING_MODE_SETTING, 0)

	var source_path: String = args[0]
	var save_path: String = args[1]
	var metadata_key: String = args[2]
	var asset_path: String = args[3]
	var prim_path: String = args[4]

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
	_require(entries.size() == 1, "Expected one preserved composition arc in metadata.")
	var entry: Dictionary = entries[0]
	_require(entry.get("asset_path", "") == asset_path, "Unexpected preserved asset path.")
	_require(entry.get("prim_path", "") == prim_path, "Unexpected preserved prim path.")
	root.free()

	var save_error := ResourceSaver.save(packed_scene, save_path)
	_require(save_error == OK, "Failed to save USDA: %s" % save_path)

	var saved_text := FileAccess.get_file_as_string(save_path)
	_require(saved_text != "", "Saved USDA text was empty.")
	_require_contains(saved_text, asset_path, "Saved USDA did not preserve asset path.")
	_require_contains(saved_text, prim_path, "Saved USDA did not preserve prim path.")

	if metadata_key == "usd:references":
		_require_contains(saved_text, "references =", "Saved USDA did not author a reference arc.")
		_require_contains(saved_text, "\"Car\"", "Saved USDA did not preserve the reference prim name.")
		_require_contains(saved_text, "(1, 2, 3, 1)", "Saved USDA lost the reference prim local transform.")
		_require_not_contains(saved_text, "def Mesh \"Geom\"", "Saved USDA flattened referenced child mesh into local opinions.")
	else:
		_require_contains(saved_text, "payload =", "Saved USDA did not author a payload arc.")
		_require_contains(saved_text, "\"PayloadCar\"", "Saved USDA did not preserve the payload prim name.")
		_require_contains(saved_text, "(4, 5, 6, 1)", "Saved USDA lost the payload prim local transform.")
		_require_not_contains(saved_text, "def Mesh \"PayloadGeom\"", "Saved USDA flattened payload child mesh into local opinions.")

	quit(0)
