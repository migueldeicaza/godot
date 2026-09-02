extends Node3D

var frame_times: Array[float] = []
var warmup_frames := 60
var measure_frames := 300
var total_frames := 0
var meshes: Array[MeshInstance3D] = []
var fps_label: Label

const MESH_COUNT := 60000
const MATERIAL_COUNT := 10  # Shared materials — creates large batchable groups

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
	cam.transform.origin = Vector3(0, 40, 70)
	cam.transform = cam.transform.looking_at(Vector3.ZERO, Vector3.UP)
	add_child(cam)

	# Single directional light with shadow (no omni/spot to keep specialization uniform)
	var dir_light := DirectionalLight3D.new()
	dir_light.transform.origin = Vector3(0, 10, 0)
	dir_light.transform = dir_light.transform.looking_at(Vector3.ZERO, Vector3.FORWARD)
	dir_light.shadow_enabled = true
	dir_light.light_energy = 1.0
	add_child(dir_light)

	# Create shared materials (MATERIAL_COUNT distinct materials)
	var materials: Array[StandardMaterial3D] = []
	for m_idx in MATERIAL_COUNT:
		var mat := StandardMaterial3D.new()
		# Distinct but deterministic colors per material group
		var hue := float(m_idx) / float(MATERIAL_COUNT)
		mat.albedo_color = Color.from_hsv(hue, 0.7, 0.9)
		mat.metallic = 0.3
		mat.roughness = 0.5
		materials.append(mat)

	# Single shared mesh resource (all instances share same BoxMesh RID)
	var box := BoxMesh.new()
	box.size = Vector3(1, 1, 1)

	# Spawn MESH_COUNT instances, each assigned one of the shared materials.
	# After sort: same shader -> same material -> same geometry = consecutive = batchable.
	var cols := 100
	var rows := MESH_COUNT / cols
	for i in MESH_COUNT:
		var m := MeshInstance3D.new()
		m.mesh = box
		m.set_surface_override_material(0, materials[i % MATERIAL_COUNT])
		var row := i / cols
		var col := i % cols
		m.transform.origin = Vector3((col - cols / 2.0) * 2.0, 0.5, (row - rows / 2.0) * 2.0)
		add_child(m)
		meshes.append(m)

	print("[SceneH] Spawned %d instances with %d shared materials (batch size ~%d)" % [MESH_COUNT, MATERIAL_COUNT, MESH_COUNT / MATERIAL_COUNT])

func _process(delta: float) -> void:
	total_frames += 1

	# Rotate all meshes every frame (keeps transforms dirty)
	for m in meshes:
		m.rotate_y(delta * 0.5)

	var live_fps := 1.0 / delta if delta > 0 else 0.0
	if fps_label:
		fps_label.text = "FPS: %.0f  [%d instances, %d materials]  t=%.1fs" % [live_fps, MESH_COUNT, MATERIAL_COUNT, total_frames * delta]

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
	print("=== SCENE H RESULTS (Instance Batching) ===")
	print("Mesh instances: %d (%d shared materials, batch ~%d)" % [MESH_COUNT, MATERIAL_COUNT, MESH_COUNT / MATERIAL_COUNT])
	print("Frames measured: %d (after %d warmup)" % [frame_times.size(), warmup_frames])
	print("Mean frame time: %.2f ms (%.1f fps)" % [mean, 1000.0 / mean])
	print("Median frame time: %.2f ms (%.1f fps)" % [median, 1000.0 / median])
	print("Min: %.2f ms | Max: %.2f ms" % [min_t, max_t])
	print("P5: %.2f ms | P95: %.2f ms | P99: %.2f ms" % [p5, p95, p99])
	print("=============================================")
