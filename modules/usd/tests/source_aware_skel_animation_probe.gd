extends SceneTree

var failed := false

func _fail(message: String) -> void:
	failed = true
	push_error(message)

func _require(condition: bool, message: String) -> void:
	if not condition:
		_fail(message)

func _require_contains(text: String, needle: String, message: String) -> void:
	_require(text.contains(needle), "%s Missing: %s" % [message, needle])

func _count_occurrences(text: String, needle: String) -> int:
	var count := 0
	var start := 0
	while true:
		var index := text.find(needle, start)
		if index < 0:
			return count
		count += 1
		start = index + needle.length()
	return count

func _find_animation_player(root_node: Node) -> AnimationPlayer:
	for child in root_node.get_children():
		if child is AnimationPlayer:
			return child
	return null

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_require(args.size() == 1, "Expected one argument: <save-path>")
	if failed:
		quit(1)
		return

	var save_path: String = args[0]
	DirAccess.remove_absolute(save_path)

	var packed_scene: PackedScene = ResourceLoader.load("res://tests/data/usd/skeleton_external_animation.usda", "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_require(packed_scene != null, "Failed to load external-animation skeleton fixture.")
	if packed_scene == null:
		quit(1)
		return

	var root := packed_scene.instantiate()
	_require(root != null, "Failed to instantiate external-animation skeleton fixture.")
	if root == null:
		quit(1)
		return

	var animation_player := _find_animation_player(root)
	_require(animation_player != null, "External-animation fixture lost its baked AnimationPlayer.")
	if animation_player == null or not animation_player.has_animation(&"ElbowAnim"):
		if root != null:
			root.free()
		quit(1)
		return

	var animation: Animation = animation_player.get_animation(&"ElbowAnim")
	_require(animation != null, "External-animation fixture returned a null animation resource.")
	if animation != null:
		_require(animation.get_track_count() == 1, "External-animation fixture should expose one baked joint track.")
		animation.track_set_key_value(0, 1, Quaternion(1, 0, 0, 0))

	var modified_scene := PackedScene.new()
	var pack_error := modified_scene.pack(root)
	_require(pack_error == OK, "Failed to repack modified external-animation fixture.")
	root.free()
	if failed:
		quit(1)
		return

	var save_error := ResourceSaver.save(modified_scene, save_path)
	_require(save_error == OK, "Failed to save source-aware external-animation USDA: %s" % save_path)

	var saved_text := FileAccess.get_file_as_string(save_path)
	_require(saved_text != "", "Saved source-aware external-animation USDA was empty.")
	_require_contains(saved_text, "def Xform \"Animations\"", "Source-aware save lost the authored external animation container.")
	_require_contains(saved_text, "def SkelAnimation \"ElbowAnim\"", "Source-aware save lost the external SkelAnimation prim.")
	_require_contains(saved_text, "rel skel:animationSource = </Animations/ElbowAnim>", "Source-aware save did not preserve the authored external animation relationship target.")
	_require_contains(saved_text, "10: [(0, 1, 0, 0)]", "Source-aware save did not author the modified rotation sample onto the preserved animation prim.")
	_require(_count_occurrences(saved_text, "def SkelAnimation \"ElbowAnim\"") == 1, "Source-aware save should preserve one external ElbowAnim prim, not duplicate it.")

	quit(1 if failed else 0)
