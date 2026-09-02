extends Node2D

var frame_count := 0
var elapsed := 0.0
var fps_samples: Array[float] = []
var benchmark_duration := 10.0
var sprites: Array[Sprite2D] = []
var velocities: Array[Vector2] = []
var viewport_size := Vector2(1280, 720)
var fps_label: Label

const SPRITE_COUNT := 1000

func _ready() -> void:
	# Live FPS overlay (updates every frame)
	fps_label = Label.new()
	fps_label.position = Vector2(10, 10)
	fps_label.add_theme_font_size_override("font_size", 28)
	fps_label.add_theme_color_override("font_color", Color.YELLOW)
	fps_label.z_index = 100  # Ensure it's on top of sprites
	add_child(fps_label)

	# Create sprites with random positions and velocities.
	var tex := _create_circle_texture()
	for i in SPRITE_COUNT:
		var s := Sprite2D.new()
		s.texture = tex
		s.position = Vector2(randf() * viewport_size.x, randf() * viewport_size.y)
		s.modulate = Color(randf(), randf(), randf(), 1.0)
		s.scale = Vector2(0.5, 0.5)
		add_child(s)
		sprites.append(s)
		velocities.append(Vector2(randf_range(-200, 200), randf_range(-200, 200)))

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

	# Move sprites (bounce off edges).
	for i in sprites.size():
		sprites[i].position += velocities[i] * delta
		if sprites[i].position.x < 0 or sprites[i].position.x > viewport_size.x:
			velocities[i].x *= -1
		if sprites[i].position.y < 0 or sprites[i].position.y > viewport_size.y:
			velocities[i].y *= -1

	# Live FPS display.
	var live_fps := 1.0 / delta if delta > 0 else 0.0
	if fps_label:
		fps_label.text = "FPS: %.0f  [%d sprites]  t=%.1fs" % [live_fps, SPRITE_COUNT, elapsed]

	# Sample FPS every frame after warm-up.
	if elapsed > 1.0:
		fps_samples.append(live_fps)

	# After benchmark duration, print results.
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
	var result := "BENCHMARK_RESULT|scene_a_sprites|avg=%.1f|p1=%.1f|p99=%.1f|frames=%d|duration=%.1f" % [avg, p1, p99, frame_count, elapsed]
	print(result)
	if fps_label:
		fps_label.text = "DONE  avg=%.1f  p1=%.1f  p99=%.1f  [%d sprites]" % [avg, p1, p99, SPRITE_COUNT]
