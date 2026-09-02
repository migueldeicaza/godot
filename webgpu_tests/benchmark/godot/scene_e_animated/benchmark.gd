extends Node3D

# Scene E: Skeletal Animation (GPU Skinning) — STRESS VERSION
# Tests: Skeleton3D + skinned MeshInstance3D via GPU bone transforms.
# 20 instances, 16 bones each, animating every frame via set_bone_pose_rotation.
# Purpose: isolate bone buffer upload overhead (WASM→JS bridge crossings per skeleton).

var elapsed := 0.0
var frame_count := 0
var fps_label: Label
var skeletons: Array[Skeleton3D] = []

const ROWS := 5
const COLS := 5
const NUM_BONES := 4   # bones per skeleton (chain)
const SIDES := 8        # vertices per ring
const RADIUS := 0.12    # cylinder radius
const SEG_LEN := 0.25   # length per bone segment

# Frame time measurement
var _frame_times: PackedFloat64Array
var _frame_idx: int = 0
var _warmup_frames: int = 60
var _measure_frames: int = 300
var _total_frames: int = 0
var _reported: bool = false


func _create_skinned_mesh() -> ArrayMesh:
	# Build a multi-segment cylinder: (NUM_BONES + 1) rings of SIDES vertices.
	# Each ring blends between adjacent bones.
	var ring_count := NUM_BONES + 1
	var ring_defs: Array[Dictionary] = []

	for r in ring_count:
		var y := float(r) * SEG_LEN
		var bone_below := mini(r, NUM_BONES - 1)
		var bone_above := mini(r, NUM_BONES - 1)
		# Ring at joint between bone (r-1) and bone r
		if r == 0:
			ring_defs.append({"y": y, "bone0": 0, "bone1": 0, "w0": 1.0, "w1": 0.0})
		elif r == ring_count - 1:
			ring_defs.append({"y": y, "bone0": NUM_BONES - 1, "bone1": NUM_BONES - 1, "w0": 1.0, "w1": 0.0})
		else:
			ring_defs.append({"y": y, "bone0": r - 1, "bone1": r, "w0": 0.5, "w1": 0.5})

	# Pre-build vertex data
	var verts: Array = []
	for ring in ring_defs:
		for i in SIDES:
			var a := TAU * i / SIDES
			verts.append({
				"pos": Vector3(cos(a) * RADIUS, ring.y, sin(a) * RADIUS),
				"bone0": ring.bone0,
				"bone1": ring.bone1,
				"w0": ring.w0,
				"w1": ring.w1,
			})

	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)

	# Quads between adjacent rings
	for ring_i in (ring_count - 1):
		for s in SIDES:
			var s1 := (s + 1) % SIDES
			var i0 := ring_i * SIDES + s
			var i1 := ring_i * SIDES + s1
			var i2 := (ring_i + 1) * SIDES + s
			var i3 := (ring_i + 1) * SIDES + s1
			for vi in [i0, i2, i1, i1, i2, i3]:
				var v: Dictionary = verts[vi]
				st.set_bones(PackedInt32Array([v.bone0, v.bone1, 0, 0]))
				st.set_weights(PackedFloat32Array([v.w0, v.w1, 0.0, 0.0]))
				st.add_vertex(v.pos)

	st.generate_normals()
	return st.commit()


func _ready() -> void:
	_frame_times.resize(_measure_frames)
	_frame_times.fill(0.0)

	# FPS overlay
	var canvas := CanvasLayer.new()
	add_child(canvas)
	fps_label = Label.new()
	fps_label.position = Vector2(10, 10)
	fps_label.add_theme_font_size_override("font_size", 24)
	fps_label.add_theme_color_override("font_color", Color.YELLOW)
	canvas.add_child(fps_label)

	# Camera
	var cam := Camera3D.new()
	cam.transform.origin = Vector3(0, 1, 5)
	cam.transform = cam.transform.looking_at(Vector3(0, 0.5, 0), Vector3.UP)
	add_child(cam)

	# Directional light with shadows
	var light := DirectionalLight3D.new()
	light.transform.origin = Vector3(5, 8, 5)
	light.transform = light.transform.looking_at(Vector3.ZERO, Vector3.FORWARD)
	light.shadow_enabled = true
	add_child(light)

	# Environment
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.15, 0.2, 0.35)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.25, 0.25, 0.35)
	var world_env := WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)

	# Build shared mesh once
	var mesh := _create_skinned_mesh()

	# Spawn ROWS*COLS skeleton instances
	var colors := [
		Color(0.9, 0.3, 0.3), Color(0.3, 0.8, 0.3), Color(0.3, 0.5, 0.9),
		Color(0.9, 0.7, 0.2), Color(0.7, 0.3, 0.9),
	]
	for row in ROWS:
		for col in COLS:
			var skel := Skeleton3D.new()
			skel.transform.origin = Vector3(
				(col - (COLS - 1) * 0.5) * 1.0,
				0.0,
				(row - (ROWS - 1) * 0.5) * 1.5
			)

			# Chain of NUM_BONES bones
			for b in NUM_BONES:
				var bone_name := "bone_%d" % b
				skel.add_bone(bone_name)
				if b > 0:
					skel.set_bone_parent(b, b - 1)
				skel.set_bone_rest(b, Transform3D(Basis.IDENTITY, Vector3(0, SEG_LEN if b > 0 else 0, 0)))

			add_child(skel)
			skeletons.append(skel)

			# Skinned mesh instance
			var mi := MeshInstance3D.new()
			mi.mesh = mesh
			mi.skeleton = NodePath("..")
			var mat := StandardMaterial3D.new()
			mat.albedo_color = colors[(row * COLS + col) % colors.size()]
			mat.roughness = 0.4
			mat.metallic = 0.3
			mi.set_surface_override_material(0, mat)
			skel.add_child(mi)

	print("[SceneE] Spawned %d skeletons × %d bones = %d total bones" % [
		skeletons.size(), NUM_BONES, skeletons.size() * NUM_BONES
	])


func _process(delta: float) -> void:
	elapsed += delta
	_total_frames += 1

	var live_fps := 1.0 / delta if delta > 0 else 0.0
	if fps_label:
		fps_label.text = "FPS: %.0f  [%d skeletons × %d bones]  t=%.1fs" % [
			live_fps, skeletons.size(), NUM_BONES, elapsed
		]

	# Animate all skeletons — each bone in the chain swings with offset phase
	for skel in skeletons:
		for b in range(1, NUM_BONES):
			var phase_offset := float(b) * 0.4
			var angle := sin(elapsed * 2.0 + phase_offset) * 0.3
			var q := Quaternion(Vector3.RIGHT, angle)
			skel.set_bone_pose_rotation(b, q)

	# Collect frame times after warmup
	if _total_frames > _warmup_frames and _frame_idx < _measure_frames:
		_frame_times[_frame_idx] = delta * 1000.0
		_frame_idx += 1
		if _frame_idx >= _measure_frames and not _reported:
			_reported = true
			_report_results()


func _report_results() -> void:
	var sorted := _frame_times.duplicate()
	sorted.sort()
	var sum := 0.0
	for t in sorted:
		sum += t
	var mean := sum / sorted.size()
	var median := sorted[sorted.size() / 2]
	var p5 := sorted[int(sorted.size() * 0.05)]
	var p95 := sorted[int(sorted.size() * 0.95)]
	var p99 := sorted[int(sorted.size() * 0.99)]
	var min_t := sorted[0]
	var max_t := sorted[sorted.size() - 1]

	print("""
=== SCENE E RESULTS (Skeletal Animation) ===
Skeletons: %d × %d bones = %d total bones
Frames measured: %d (after %d warmup)
Mean frame time: %.2f ms (%.1f fps)
Median frame time: %.2f ms (%.1f fps)
Min: %.2f ms | Max: %.2f ms
P5: %.2f ms | P95: %.2f ms | P99: %.2f ms
=============================================""" % [
		skeletons.size(), NUM_BONES, skeletons.size() * NUM_BONES,
		_measure_frames, _warmup_frames,
		mean, 1000.0 / mean,
		median, 1000.0 / median,
		min_t, max_t,
		p5, p95, p99
	])
