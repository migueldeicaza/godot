extends Node3D


func _ready() -> void:
	var capture_path := OS.get_environment("GODOT_NAGA_RENDER_CAPTURE")
	if capture_path.is_empty():
		return

	# Let render resources and shadows settle before taking the deterministic sample.
	for frame in 8:
		await RenderingServer.frame_post_draw
		await get_tree().process_frame

	var image := get_viewport().get_texture().get_image()
	image.convert(Image.FORMAT_RGBA8)
	var png_error := image.save_png(capture_path + ".png")
	if png_error != OK:
		push_error("Could not save Naga render capture PNG: %s" % error_string(png_error))
		get_tree().quit(1)
		return

	var raw_file := FileAccess.open(capture_path + ".rgba8", FileAccess.WRITE)
	if raw_file == null:
		push_error("Could not open Naga raw render capture: %s" % FileAccess.get_open_error())
		get_tree().quit(1)
		return
	raw_file.store_buffer(image.get_data())

	var metadata_file := FileAccess.open(capture_path + ".size", FileAccess.WRITE)
	if metadata_file == null:
		push_error("Could not open Naga render capture metadata: %s" % FileAccess.get_open_error())
		get_tree().quit(1)
		return
	metadata_file.store_line("%d %d" % [image.get_width(), image.get_height()])
	print("Naga render capture: path=%s width=%d height=%d" % [capture_path, image.get_width(), image.get_height()])
	get_tree().quit()
