extends Node3D

var frame_times: Array[float] = []
var warmup_frames := 60
var measure_frames := 300
var total_frames := 0
var fps_label: Label
var multimesh: MultiMesh
var elapsed := 0.0

const MESH_COUNT := 60000
const MATERIAL_COUNT := 10
const COLS := 100

var rotations: PackedFloat32Array

func _ready() -> void:
	rotations = PackedFloat32Array()
	rotations.resize(MESH_COUNT)
	rotations.fill(0.0)

	# FPS overlay — same pattern as scene_c (which works)
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

	# Light
	var dir_light := DirectionalLight3D.new()
	dir_light.transform.origin = Vector3(0, 10, 0)
	dir_light.transform = dir_light.transform.looking_at(Vector3.ZERO, Vector3.FORWARD)
	dir_light.shadow_enabled = true
	dir_light.light_energy = 1.0
	add_child(dir_light)

	# Mesh
	var box := BoxMesh.new()
	box.size = Vector3(1, 1, 1)

	# Material
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.metallic = 0.3
	mat.roughness = 0.5

	# MultiMesh
	multimesh = MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.instance_count = MESH_COUNT
	multimesh.mesh = box

	var rows := MESH_COUNT / COLS
	for i in MESH_COUNT:
		var row := i / COLS
		var col := i % COLS
		var pos := Vector3((col - COLS / 2.0) * 2.0, 0.5, (row - rows / 2.0) * 2.0)
		multimesh.set_instance_transform(i, Transform3D(Basis.IDENTITY, pos))
		var hue := float(i % MATERIAL_COUNT) / float(MATERIAL_COUNT)
		multimesh.set_instance_color(i, Color.from_hsv(hue, 0.7, 0.9))

	var mmi := MultiMeshInstance3D.new()
	mmi.multimesh = multimesh
	mmi.material_override = mat
	mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	add_child(mmi)

	print("[SceneH_Batched] MultiMesh: %d instances, %d colors" % [MESH_COUNT, MATERIAL_COUNT])

func _process(delta: float) -> void:
	total_frames += 1
	elapsed += delta

	var rows := MESH_COUNT / COLS
	for i in MESH_COUNT:
		rotations[i] += delta * 0.5
		var row := i / COLS
		var col := i % COLS
		var pos := Vector3((col - COLS / 2.0) * 2.0, 0.5, (row - rows / 2.0) * 2.0)
		var basis := Basis(Vector3.UP, rotations[i])
		multimesh.set_instance_transform(i, Transform3D(basis, pos))

	var live_fps := 1.0 / delta if delta > 0 else 0.0
	if fps_label:
		fps_label.text = "FPS: %.0f  [%d MultiMesh, %d colors]  t=%.1fs" % [live_fps, MESH_COUNT, MATERIAL_COUNT, elapsed]
	# Console fallback — label has rendering bug with MultiMesh on WebGPU
	if total_frames % 30 == 0:
		print("FPS: %.0f  [%d MultiMesh]  t=%.1fs" % [live_fps, MESH_COUNT, elapsed])

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
	print("=== SCENE H RESULTS (MultiMesh Batched) ===")
	print("Instances: %d (%d colors, MultiMesh)" % [MESH_COUNT, MATERIAL_COUNT])
	print("Frames measured: %d (after %d warmup)" % [frame_times.size(), warmup_frames])
	print("Mean frame time: %.2f ms (%.1f fps)" % [mean, 1000.0 / mean])
	print("Median frame time: %.2f ms (%.1f fps)" % [median, 1000.0 / median])
	print("Min: %.2f ms | Max: %.2f ms" % [min_t, max_t])
	print("P5: %.2f ms | P95: %.2f ms | P99: %.2f ms" % [p5, p95, p99])
	print("=============================================")
