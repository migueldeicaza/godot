extends Node3D

var frame_times: Array[float] = []
var warmup_frames := 180
var measure_frames := 300
var total_frames := 0
var fps_label: Label

# Shadow stress — many lights with moderate geometry
const OMNI_LIGHT_COUNT := 6
const MESH_COUNT := 200
const ARENA_SIZE := 60.0

func _ready() -> void:
	# Live FPS overlay
	var canvas := CanvasLayer.new()
	add_child(canvas)
	fps_label = Label.new()
	fps_label.position = Vector2(10, 10)
	fps_label.add_theme_font_size_override("font_size", 28)
	fps_label.add_theme_color_override("font_color", Color.YELLOW)
	canvas.add_child(fps_label)

	# Camera looking at the scene
	var cam := Camera3D.new()
	cam.transform.origin = Vector3(0, 30, 50)
	cam.transform = cam.transform.looking_at(Vector3.ZERO, Vector3.UP)
	cam.far = 200.0
	add_child(cam)

	# Environment with ambient light so we can see something
	var env := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.1, 0.1, 0.15)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.05, 0.05, 0.08)
	env.environment = environment
	add_child(env)

	# Floor plane (shadow receiver)
	var floor_mesh := PlaneMesh.new()
	floor_mesh.size = Vector2(ARENA_SIZE * 2.5, ARENA_SIZE * 2.5)
	var floor_inst := MeshInstance3D.new()
	floor_inst.mesh = floor_mesh
	var floor_mat := StandardMaterial3D.new()
	floor_mat.albedo_color = Color(0.4, 0.4, 0.4)
	floor_inst.set_surface_override_material(0, floor_mat)
	add_child(floor_inst)

	# Scattered mesh instances (shadow casters)
	var box := BoxMesh.new()
	box.size = Vector3(1.5, 3.0, 1.5)
	for i in MESH_COUNT:
		var m := MeshInstance3D.new()
		m.mesh = box
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color(randf_range(0.3, 1.0), randf_range(0.3, 1.0), randf_range(0.3, 1.0))
		m.set_surface_override_material(0, mat)
		m.transform.origin = Vector3(
			randf_range(-ARENA_SIZE, ARENA_SIZE),
			1.5,
			randf_range(-ARENA_SIZE, ARENA_SIZE)
		)
		m.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
		add_child(m)

	# OmniLight3D with shadows - distributed around the scene
	for i in OMNI_LIGHT_COUNT:
		var light := OmniLight3D.new()
		var angle := (float(i) / OMNI_LIGHT_COUNT) * TAU
		var radius := ARENA_SIZE * 0.6
		light.transform.origin = Vector3(
			cos(angle) * radius,
			6.0 + sin(angle * 2.0) * 3.0,
			sin(angle) * radius
		)
		light.omni_range = 25.0
		light.light_energy = 2.0
		light.shadow_enabled = true
		light.omni_shadow_mode = OmniLight3D.SHADOW_CUBE
		light.light_color = Color(
			randf_range(0.7, 1.0),
			randf_range(0.7, 1.0),
			randf_range(0.7, 1.0)
		)
		add_child(light)

	# One directional light with shadow for good measure
	var dir_light := DirectionalLight3D.new()
	dir_light.transform.origin = Vector3(0, 20, 0)
	dir_light.transform = dir_light.transform.looking_at(Vector3(5, 0, 5), Vector3.UP)
	dir_light.shadow_enabled = true
	dir_light.light_energy = 0.5
	dir_light.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	add_child(dir_light)

	print("[SceneG] Spawned %d meshes + %d omni shadow lights + 1 directional" % [MESH_COUNT, OMNI_LIGHT_COUNT])

func _process(delta: float) -> void:
	total_frames += 1

	var live_fps := 1.0 / delta if delta > 0 else 0.0
	if fps_label:
		fps_label.text = "FPS: %.0f  [%d meshes, %d shadow lights]  t=%.1fs" % [live_fps, MESH_COUNT, OMNI_LIGHT_COUNT, total_frames * delta]

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
	print("=== SCENE G RESULTS (Shadow Stress) ===")
	print("Meshes: %d | Omni lights (shadow): %d | Directional (4-split): 1" % [MESH_COUNT, OMNI_LIGHT_COUNT])
	print("Frames measured: %d (after %d warmup)" % [frame_times.size(), warmup_frames])
	print("Mean frame time: %.2f ms (%.1f fps)" % [mean, 1000.0 / mean])
	print("Median frame time: %.2f ms (%.1f fps)" % [median, 1000.0 / median])
	print("Min: %.2f ms | Max: %.2f ms" % [min_t, max_t])
	print("P5: %.2f ms | P95: %.2f ms | P99: %.2f ms" % [p5, p95, p99])
	print("========================================")
