extends Node3D

var frame_times: Array[float] = []
var warmup_frames := 60
var measure_frames := 300
var total_frames := 0
var meshes: Array[MeshInstance3D] = []
var fps_label: Label

const MESH_COUNT := 196

func _ready() -> void:
	# Live FPS overlay
	var canvas := CanvasLayer.new()
	add_child(canvas)
	fps_label = Label.new()
	fps_label.position = Vector2(10, 10)
	fps_label.add_theme_font_size_override("font_size", 28)
	fps_label.add_theme_color_override("font_color", Color.YELLOW)
	canvas.add_child(fps_label)

	# Camera
	var cam := Camera3D.new()
	cam.transform.origin = Vector3(0, 15, 25)
	cam.transform = cam.transform.looking_at(Vector3.ZERO, Vector3.UP)
	add_child(cam)

	# Single directional light with shadow
	var dir_light := DirectionalLight3D.new()
	dir_light.transform.origin = Vector3(0, 10, 0)
	dir_light.transform = dir_light.transform.looking_at(Vector3.ZERO, Vector3.FORWARD)
	dir_light.shadow_enabled = true
	dir_light.light_energy = 1.0
	add_child(dir_light)

	# 3000 mesh instances in a grid, each with unique material
	var box := BoxMesh.new()
	box.size = Vector3(1, 1, 1)
	var cols := 14  # 14x14 = 196 (square grid)
	var rows := MESH_COUNT / cols
	for i in MESH_COUNT:
		var m := MeshInstance3D.new()
		m.mesh = box
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color(randf(), randf(), randf())
		mat.metallic = randf_range(0.0, 0.8)
		mat.roughness = randf_range(0.1, 0.9)
		m.set_surface_override_material(0, mat)
		var row := i / cols
		var col := i % cols
		m.transform.origin = Vector3((col - cols / 2.0) * 2.0, 0.5, (row - rows / 2.0) * 2.0)
		add_child(m)
		meshes.append(m)

	print("[SceneC] Spawned %d rotating mesh instances" % MESH_COUNT)

func _process(delta: float) -> void:
	total_frames += 1

	# Rotate all meshes every frame (keeps transforms dirty)
	for m in meshes:
		m.rotate_y(delta * 0.5)

	# Live FPS display
	var live_fps := 1.0 / delta if delta > 0 else 0.0
	if fps_label:
		fps_label.text = "FPS: %.0f  [%d instances]  t=%.1fs" % [live_fps, MESH_COUNT, total_frames * delta]

	if total_frames <= warmup_frames:
		return

	if frame_times.size() >= measure_frames:
		_report_results()
		set_process(false)
		return

	frame_times.append(delta * 1000.0)

func _report_results() -> void:
	frame_times.sort()
	var sum := 0.0
	for t in frame_times:
		sum += t
	var mean := sum / frame_times.size()
	var median := frame_times[frame_times.size() / 2]
	var min_t := frame_times[0]
	var max_t := frame_times[frame_times.size() - 1]
	var p5 := frame_times[int(frame_times.size() * 0.05)]
	var p95 := frame_times[int(frame_times.size() * 0.95)]
	var p99 := frame_times[int(frame_times.size() * 0.99)]

	print("")
	print("=== SCENE C RESULTS (Instance Draw Calls) ===")
	print("Mesh instances: %d (rotating each frame)" % MESH_COUNT)
	print("Frames measured: %d (after %d warmup)" % [frame_times.size(), warmup_frames])
	print("Mean frame time: %.2f ms (%.1f fps)" % [mean, 1000.0 / mean])
	print("Median frame time: %.2f ms (%.1f fps)" % [median, 1000.0 / median])
	print("Min: %.2f ms | Max: %.2f ms" % [min_t, max_t])
	print("P5: %.2f ms | P95: %.2f ms | P99: %.2f ms" % [p5, p95, p99])
	print("=============================================")
