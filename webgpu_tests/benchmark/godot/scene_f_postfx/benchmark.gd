extends Node3D

# Scene F: SubViewport + Post-Processing (SSAO + Bloom + Procedural Sky)
# Tests:
#   - SubViewport (off-screen render-to-texture, independent render world)
#   - ViewportTexture bound as albedo on a mesh in the main scene
#   - SSAO (screen-space ambient occlusion — depth texture sampling)
#   - Bloom/glow (multi-pass compute downsample+upsample)
#   - Procedural sky (new sky shader permutation)

var elapsed := 0.0
var frame_count := 0
var fps_label: Label
var spinning_cubes: Array[MeshInstance3D] = []
var torus_node: MeshInstance3D

const CUBE_COUNT := 5
const NUM_SUBVIEWPORTS := 6
const SUBVIEWPORT_SIZE := 512

func _ready() -> void:
	# FPS overlay
	var canvas := CanvasLayer.new()
	add_child(canvas)
	fps_label = Label.new()
	fps_label.position = Vector2(10, 10)
	fps_label.add_theme_font_size_override("font_size", 28)
	fps_label.add_theme_color_override("font_color", Color.YELLOW)
	canvas.add_child(fps_label)

	# ---- MAIN SCENE CAMERA ----
	var cam := Camera3D.new()
	cam.transform.origin = Vector3(0, 3, 9)
	cam.transform = cam.transform.looking_at(Vector3(0, 0.5, 0), Vector3.UP)
	add_child(cam)

	# ---- MAIN SCENE ENVIRONMENT (SSAO + Glow + Procedural Sky) ----
	var sky_mat := ProceduralSkyMaterial.new()
	sky_mat.sky_top_color = Color(0.2, 0.4, 0.8)
	sky_mat.sky_horizon_color = Color(0.6, 0.7, 0.9)
	sky_mat.ground_bottom_color = Color(0.3, 0.25, 0.2)
	sky_mat.ground_horizon_color = Color(0.5, 0.5, 0.45)
	sky_mat.sun_angle_max = 30.0

	var sky := Sky.new()
	sky.sky_material = sky_mat

	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = 0.4
	# SSAO
	env.ssao_enabled = true
	env.ssao_radius = 1.2
	env.ssao_intensity = 2.5
	# Glow / bloom
	env.glow_enabled = true
	env.glow_intensity = 0.9
	env.glow_bloom = 0.15
	env.glow_blend_mode = Environment.GLOW_BLEND_MODE_SOFTLIGHT

	var world_env := WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)

	# ---- MAIN SCENE LIGHTS ----
	var dir_light := DirectionalLight3D.new()
	dir_light.transform.origin = Vector3(5, 10, 5)
	dir_light.transform = dir_light.transform.looking_at(Vector3.ZERO, Vector3.FORWARD)
	dir_light.shadow_enabled = true
	dir_light.light_energy = 1.2
	add_child(dir_light)

	# ---- MAIN SCENE CONTENT: spinning PBR cubes + ground ----
	var cube_mesh := BoxMesh.new()
	cube_mesh.size = Vector3(0.7, 0.7, 0.7)
	for i in CUBE_COUNT:
		var mi := MeshInstance3D.new()
		mi.mesh = cube_mesh
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color.from_hsv(float(i) / CUBE_COUNT, 0.75, 0.9)
		mat.roughness = 0.25
		mat.metallic = 0.6
		mi.set_surface_override_material(0, mat)
		mi.transform.origin = Vector3(cos(TAU * i / CUBE_COUNT) * 2.8, 0.35, sin(TAU * i / CUBE_COUNT) * 2.8)
		add_child(mi)
		spinning_cubes.append(mi)

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(14, 14)
	ground.mesh = plane
	var gmat := StandardMaterial3D.new()
	gmat.albedo_color = Color(0.55, 0.52, 0.48)
	gmat.roughness = 0.85
	ground.set_surface_override_material(0, gmat)
	ground.transform.origin = Vector3(0, 0, 0)
	add_child(ground)

	# ---- SUBVIEWPORTS (multiple large ones to stress post-processing) ----
	var torus_mesh := TorusMesh.new()
	torus_mesh.inner_radius = 0.35
	torus_mesh.outer_radius = 0.75

	for vi in NUM_SUBVIEWPORTS:
		var viewport := SubViewport.new()
		viewport.size = Vector2i(SUBVIEWPORT_SIZE, SUBVIEWPORT_SIZE)
		viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		add_child(viewport)

		var vp_cam := Camera3D.new()
		vp_cam.transform.origin = Vector3(0, 0, 3.5)
		viewport.add_child(vp_cam)

		var vp_env := Environment.new()
		vp_env.background_mode = Environment.BG_COLOR
		vp_env.background_color = Color(0.05, 0.02, 0.15)
		vp_env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
		vp_env.ambient_light_color = Color(0.2, 0.1, 0.35)
		vp_env.ssao_enabled = true
		vp_env.glow_enabled = true
		var vp_world_env := WorldEnvironment.new()
		vp_world_env.environment = vp_env
		viewport.add_child(vp_world_env)

		var vp_light := OmniLight3D.new()
		vp_light.transform.origin = Vector3(2, 2, 2)
		vp_light.light_color = Color(0.6, 0.4, 1.0)
		vp_light.light_energy = 3.0
		viewport.add_child(vp_light)

		var torus := MeshInstance3D.new()
		torus.mesh = torus_mesh
		var torus_mat := StandardMaterial3D.new()
		torus_mat.albedo_color = Color.from_hsv(float(vi) / NUM_SUBVIEWPORTS, 0.75, 0.9)
		torus_mat.roughness = 0.15
		torus_mat.metallic = 0.85
		torus.set_surface_override_material(0, torus_mat)
		viewport.add_child(torus)
		if vi == 0:
			torus_node = torus

		# Monitor quad showing this viewport
		var monitor_mesh := QuadMesh.new()
		monitor_mesh.size = Vector2(1.0, 1.0)
		var monitor := MeshInstance3D.new()
		monitor.mesh = monitor_mesh
		var monitor_mat := StandardMaterial3D.new()
		monitor_mat.albedo_texture = viewport.get_texture()
		monitor_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		monitor.set_surface_override_material(0, monitor_mat)
		var angle := TAU * vi / NUM_SUBVIEWPORTS
		monitor.transform.origin = Vector3(cos(angle) * 4.0, 1.1, sin(angle) * 4.0)
		monitor.transform = monitor.transform.looking_at(Vector3(0, 1.1, 0), Vector3.UP)
		add_child(monitor)

func _process(delta: float) -> void:
	elapsed += delta
	frame_count += 1

	var live_fps := 1.0 / delta if delta > 0 else 0.0
	if fps_label:
		fps_label.text = "FPS: %.0f  [SubViewport + SSAO + Bloom + ProceduralSky]  t=%.1fs" % [
			live_fps, elapsed
		]

	# Spin main-scene cubes
	for i in spinning_cubes.size():
		spinning_cubes[i].rotation.y = elapsed * (0.4 + i * 0.12)

	# Spin torus in SubViewport
	if torus_node:
		torus_node.rotation.y = elapsed * 1.1
		torus_node.rotation.x = elapsed * 0.4
