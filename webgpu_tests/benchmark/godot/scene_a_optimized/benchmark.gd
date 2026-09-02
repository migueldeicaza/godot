extends Node2D

var frame_count := 0
var elapsed := 0.0
var fps_samples: Array[float] = []
var benchmark_duration := 10.0
var viewport_size := Vector2(1280, 720)
var fps_label: Label
var multimesh: MultiMesh

const SPRITE_COUNT := 2000

var velocities: PackedVector2Array
var positions: PackedVector2Array
var colors: PackedColorArray

func _ready() -> void:
	# Live FPS overlay
	fps_label = Label.new()
	fps_label.position = Vector2(10, 10)
	fps_label.add_theme_font_size_override("font_size", 28)
	fps_label.add_theme_color_override("font_color", Color.YELLOW)
	fps_label.z_index = 100
	add_child(fps_label)

	# Create circle texture
	var tex := _create_circle_texture()

	# Create a QuadMesh for the MultiMesh (2D equivalent of a sprite quad)
	var quad := QuadMesh.new()
	quad.size = Vector2(16, 16)  # 32 * 0.5 scale like original

	# MultiMesh setup
	multimesh = MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_2D
	multimesh.use_colors = true
	multimesh.instance_count = SPRITE_COUNT
	multimesh.mesh = quad

	# Initialize arrays
	velocities = PackedVector2Array()
	velocities.resize(SPRITE_COUNT)
	positions = PackedVector2Array()
	positions.resize(SPRITE_COUNT)

	for i in SPRITE_COUNT:
		var pos := Vector2(randf() * viewport_size.x, randf() * viewport_size.y)
		positions[i] = pos
		velocities[i] = Vector2(randf_range(-200, 200), randf_range(-200, 200))
		multimesh.set_instance_transform_2d(i, Transform2D(0.0, pos))
		multimesh.set_instance_color(i, Color(randf(), randf(), randf(), 1.0))

	# MultiMeshInstance2D with texture
	var mmi := MultiMeshInstance2D.new()
	mmi.multimesh = multimesh
	mmi.texture = tex
	add_child(mmi)

	print("[SceneA_Optimized] MultiMesh2D: %d instances" % SPRITE_COUNT)

func _create_circle_texture() -> ImageTexture:
	var img := Image.create(32, 32, false, Image.FORMAT_RGBA8)
	var center := Vector2(16, 16)
	for y in 32:
		for x in 32:
			var d := Vector2(x, y).distance_to(center)
			if d < 14.0:
				img.set_pixel(x, y, Color.WHITE)
			else:
				img.set_pixel(x, y, Color(1, 1, 1, 0))
	return ImageTexture.create_from_image(img)

func _process(delta: float) -> void:
	elapsed += delta
	frame_count += 1

	# Move sprites (bounce off edges)
	for i in SPRITE_COUNT:
		var pos := positions[i]
		var vel := velocities[i]
		pos += vel * delta
		if pos.x < 0 or pos.x > viewport_size.x:
			vel.x *= -1
			velocities[i] = vel
		if pos.y < 0 or pos.y > viewport_size.y:
			vel.y *= -1
			velocities[i] = vel
		positions[i] = pos
		multimesh.set_instance_transform_2d(i, Transform2D(0.0, pos))

	# Live FPS display
	var live_fps := 1.0 / delta if delta > 0 else 0.0
	if fps_label:
		fps_label.text = "FPS: %.0f  [%d MultiMesh2D]  t=%.1fs" % [live_fps, SPRITE_COUNT, elapsed]

	if frame_count % 30 == 0:
		print("FPS: %.0f  [%d MultiMesh2D]  t=%.1fs" % [live_fps, SPRITE_COUNT, elapsed])

	# Sample FPS after warm-up
	if elapsed > 1.0:
		fps_samples.append(live_fps)

	# Report after benchmark duration
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
	var result := "BENCHMARK_RESULT|scene_a_optimized|avg=%.1f|p1=%.1f|p99=%.1f|frames=%d|duration=%.1f" % [avg, p1, p99, frame_count, elapsed]
	print(result)
	if fps_label:
		fps_label.text = "DONE  avg=%.1f  p1=%.1f  p99=%.1f  [%d MultiMesh2D]" % [avg, p1, p99, SPRITE_COUNT]
