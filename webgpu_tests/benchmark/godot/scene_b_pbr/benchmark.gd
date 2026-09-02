extends Node3D

const GRID_ROWS := 5
const GRID_COLS := 5

var frame_count := 0
var elapsed := 0.0
var fps_samples: Array[float] = []
var benchmark_duration := 10.0
var mesh_node: MeshInstance3D
var fps_label: Label

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
	cam.transform = Transform3D.IDENTITY.looking_at(Vector3.ZERO, Vector3.UP)
	var extent := float(max(GRID_ROWS, GRID_COLS)) * 1.5
	cam.transform.origin = Vector3(0, extent * 0.5, extent * 0.8)
	cam.transform = cam.transform.looking_at(Vector3.ZERO, Vector3.UP)
	cam.far = extent * 5.0
	add_child(cam)

	# Directional light with shadows
	var light := DirectionalLight3D.new()
	light.transform.origin = Vector3(5, 8, 5)
	light.transform = light.transform.looking_at(Vector3.ZERO, Vector3.UP)
	light.shadow_enabled = true
	add_child(light)

	# PBR spheres in a grid to stress-test draw calls + shading
	var sphere_mesh := SphereMesh.new()
	sphere_mesh.radius = 0.5
	sphere_mesh.height = 1.0
	sphere_mesh.radial_segments = 48
	sphere_mesh.rings = 24
	for row in GRID_ROWS:
		for col in GRID_COLS:
			var s := MeshInstance3D.new()
			s.mesh = sphere_mesh
			var mat := StandardMaterial3D.new()
			mat.albedo_color = Color(randf_range(0.3, 1.0), randf_range(0.1, 0.6), randf_range(0.1, 0.5))
			mat.metallic = float(col) / float(GRID_COLS - 1)
			mat.roughness = float(row) / float(GRID_ROWS - 1)
			s.set_surface_override_material(0, mat)
			s.transform.origin = Vector3((col - GRID_COLS / 2) * 1.5, 0.5, (row - GRID_ROWS / 2) * 1.5)
			add_child(s)
			if mesh_node == null:
				mesh_node = s

	# Ground plane
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(40, 40)
	ground.mesh = plane
	var ground_mat := StandardMaterial3D.new()
	ground_mat.albedo_color = Color(0.6, 0.6, 0.55)
	ground.set_surface_override_material(0, ground_mat)
	add_child(ground)

	# Environment
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.3, 0.4, 0.6)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.4, 0.4, 0.5)
	var world_env := WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)

func _process(delta: float) -> void:
	elapsed += delta
	frame_count += 1

	var live_fps := 1.0 / delta if delta > 0 else 0.0
	if fps_label:
		fps_label.text = "FPS: %.0f  [%d PBR spheres]  t=%.1fs" % [live_fps, GRID_ROWS * GRID_COLS, elapsed]

	if elapsed > 1.0:
		fps_samples.append(live_fps)

	if elapsed >= benchmark_duration + 1.0:
		_report_results()
		set_process(false)

func _report_results() -> void:
	fps_samples.sort()
	var avg := 0.0
	for f in fps_samples:
		avg += f
	avg /= fps_samples.size()
	var p1 := fps_samples[int(fps_samples.size() * 0.01)]
	var p99 := fps_samples[int(fps_samples.size() * 0.99)]
	var result := "BENCHMARK_RESULT|scene_b_pbr|avg=%.1f|p1=%.1f|p99=%.1f|frames=%d|duration=%.1f" % [avg, p1, p99, frame_count, elapsed]
	print(result)
	if fps_label:
		fps_label.text = "DONE  avg=%.1f  p1=%.1f  p99=%.1f  [%d PBR spheres]" % [avg, p1, p99, GRID_ROWS * GRID_COLS]
