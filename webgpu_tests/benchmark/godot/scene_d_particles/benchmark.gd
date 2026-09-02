extends Node3D

var frame_count := 0
var elapsed := 0.0
var fps_samples: Array[float] = []
var benchmark_duration := 10.0
var fps_label: Label

const PARTICLE_COUNT := 1000

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
	cam.transform.origin = Vector3(0, 5, 15)
	cam.transform = cam.transform.looking_at(Vector3(0, 3, 0), Vector3.UP)
	add_child(cam)

	# Light
	var light := DirectionalLight3D.new()
	light.transform.origin = Vector3(5, 10, 5)
	light.transform = light.transform.looking_at(Vector3.ZERO, Vector3.UP)
	add_child(light)

	# GPU Particles — stress test
	var particles := GPUParticles3D.new()
	particles.amount = PARTICLE_COUNT
	particles.lifetime = 3.0
	particles.emitting = true

	# Process material
	var pmat := ParticleProcessMaterial.new()
	pmat.direction = Vector3(0, 1, 0)
	pmat.spread = 45.0
	pmat.initial_velocity_min = 2.0
	pmat.initial_velocity_max = 6.0
	pmat.gravity = Vector3(0, -4, 0)
	pmat.color = Color(1, 0.6, 0.2)
	var color_ramp := GradientTexture1D.new()
	var gradient := Gradient.new()
	gradient.set_color(0, Color(1, 0.8, 0.2, 1))
	gradient.add_point(0.5, Color(1, 0.3, 0.1, 0.8))
	gradient.set_color(1, Color(0.5, 0.1, 0.1, 0))
	color_ramp.gradient = gradient
	pmat.color_ramp = color_ramp
	particles.process_material = pmat

	# Draw pass — small sphere per particle
	var sphere := SphereMesh.new()
	sphere.radius = 0.05
	sphere.height = 0.1
	sphere.radial_segments = 8
	sphere.rings = 4
	particles.draw_pass_1 = sphere

	add_child(particles)

	# Environment
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.05, 0.05, 0.1)
	var world_env := WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)

func _process(delta: float) -> void:
	elapsed += delta
	frame_count += 1

	var live_fps := 1.0 / delta if delta > 0 else 0.0
	if fps_label:
		fps_label.text = "FPS: %.0f  [%d GPU particles]  t=%.1fs" % [live_fps, PARTICLE_COUNT, elapsed]

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
	var result := "BENCHMARK_RESULT|scene_d_particles|avg=%.1f|p1=%.1f|p99=%.1f|frames=%d|duration=%.1f" % [avg, p1, p99, frame_count, elapsed]
	print(result)
	if fps_label:
		fps_label.text = "DONE  avg=%.1f  p1=%.1f  p99=%.1f  [%d particles]" % [avg, p1, p99, PARTICLE_COUNT]
