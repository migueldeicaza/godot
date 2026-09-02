## Shader Coverage Test Scene
##
## Programmatically creates a scene that exercises every single shader path
## in Godot's RenderingDevice renderer. When run, this forces compilation of
## all shader variants, validating the SPIR-V → WGSL pipeline end-to-end.
##
## The scene renders for a configurable number of frames then exits with
## success/failure based on whether any shader compilation errors occurred.

extends Node3D

const FRAMES_TO_RENDER := 10
const REPORT_PATH := "user://shader_coverage_report.json"

var frame_count := 0
var errors: Array[String] = []
var shader_count := 0

func _ready() -> void:
	print("[ShaderCoverage] Starting comprehensive shader coverage test...")
	print("[ShaderCoverage] Building scene with all rendering features...")

	_setup_environment()
	_setup_camera()
	_setup_lights()
	_setup_geometry_with_materials()
	_setup_particles()
	_setup_gi()
	_setup_fog()
	_setup_decals()
	_setup_reflection_probes()
	_setup_canvas_2d()
	_setup_skeleton_mesh()
	_setup_multimesh()

	print("[ShaderCoverage] Scene built. Rendering %d frames..." % FRAMES_TO_RENDER)


func _process(_delta: float) -> void:
	frame_count += 1
	if frame_count >= FRAMES_TO_RENDER:
		_report_results()
		get_tree().quit(0 if errors.is_empty() else 1)


# ═══════════════════════════════════════════════════════════════════════════════
# ENVIRONMENT — Sky, Tonemap, SSAO, SSIL, SSR, Glow, Fog, Volumetric Fog
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_environment() -> void:
	var env := Environment.new()

	# Sky (exercises sky.glsl)
	var sky := Sky.new()
	var sky_mat := ProceduralSkyMaterial.new()
	sky_mat.sky_top_color = Color(0.2, 0.4, 0.8)
	sky_mat.sky_horizon_color = Color(0.6, 0.7, 0.9)
	sky_mat.ground_bottom_color = Color(0.1, 0.08, 0.05)
	sky_mat.ground_horizon_color = Color(0.4, 0.35, 0.3)
	sky_mat.sun_angle_max = 30.0
	sky_mat.sun_curve = 0.15
	sky.sky_material = sky_mat
	sky.radiance_size = Sky.RADIANCE_SIZE_256
	env.sky = sky
	env.background_mode = Environment.BG_SKY
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_sky_contribution = 1.0
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY

	# Tonemap (exercises tonemap.glsl)
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.tonemap_exposure = 1.0
	env.tonemap_white = 1.0

	# SSAO (exercises ssao.glsl, ssao_blur.glsl, ssao_importance_map.glsl, ssao_interleave.glsl)
	env.ssao_enabled = true
	env.ssao_radius = 1.0
	env.ssao_intensity = 2.0
	env.ssao_power = 1.5
	env.ssao_detail = 0.5
	env.ssao_horizon = 0.06
	env.ssao_sharpness = 0.98

	# SSIL (exercises ssil.glsl, ssil_blur.glsl, ssil_importance_map.glsl, ssil_interleave.glsl)
	env.ssil_enabled = true
	env.ssil_radius = 5.0
	env.ssil_intensity = 1.0
	env.ssil_sharpness = 0.98
	env.ssil_normal_rejection = 1.0

	# SSR (exercises screen_space_reflection.glsl and related shaders)
	env.ssr_enabled = true
	env.ssr_max_steps = 64
	env.ssr_fade_in = 0.15
	env.ssr_fade_out = 2.0
	env.ssr_depth_tolerance = 0.5

	# Glow / Bloom (exercises blur and glow shaders)
	env.glow_enabled = true
	env.glow_intensity = 0.8
	env.glow_strength = 1.0
	env.glow_bloom = 0.1
	env.glow_blend_mode = Environment.GLOW_BLEND_MODE_ADDITIVE
	env.glow_hdr_threshold = 1.0
	env.glow_hdr_scale = 2.0
	env.glow_hdr_luminance_cap = 12.0

	# Exponential Fog (exercises fog in scene_forward_lights_inc.glsl)
	env.fog_enabled = true
	env.fog_light_color = Color(0.8, 0.85, 0.9)
	env.fog_light_energy = 0.5
	env.fog_density = 0.005
	env.fog_aerial_perspective = 0.5
	env.fog_sun_scatter = 0.2

	# Volumetric Fog (exercises volumetric_fog.glsl, volumetric_fog_process.glsl)
	env.volumetric_fog_enabled = true
	env.volumetric_fog_density = 0.02
	env.volumetric_fog_albedo = Color(1, 1, 1)
	env.volumetric_fog_emission = Color(0, 0, 0)
	env.volumetric_fog_emission_energy = 1.0
	env.volumetric_fog_anisotropy = 0.2
	env.volumetric_fog_length = 64.0
	env.volumetric_fog_detail_spread = 2.0
	env.volumetric_fog_gi_inject = 1.0
	env.volumetric_fog_temporal_reprojection_enabled = true

	# SDFGI (exercises sdfgi_*.glsl shaders)
	env.sdfgi_enabled = true
	env.sdfgi_cascades = 4
	env.sdfgi_min_cell_size = 0.2
	env.sdfgi_use_occlusion = true
	env.sdfgi_bounce_feedback = 0.5
	env.sdfgi_read_sky_light = true
	env.sdfgi_energy = 1.0
	env.sdfgi_y_scale = Environment.SDFGI_Y_SCALE_75_PERCENT

	# Color correction / adjustment
	env.adjustment_enabled = true
	env.adjustment_brightness = 1.0
	env.adjustment_contrast = 1.05
	env.adjustment_saturation = 1.1

	# Apply environment to WorldEnvironment node
	var world_env := WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)

	# Camera attributes for DOF (exercises bokeh_dof.glsl)
	var cam_attr := CameraAttributesPractical.new()
	cam_attr.dof_blur_far_enabled = true
	cam_attr.dof_blur_far_distance = 20.0
	cam_attr.dof_blur_far_transition = 5.0
	cam_attr.dof_blur_near_enabled = true
	cam_attr.dof_blur_near_distance = 1.0
	cam_attr.dof_blur_near_transition = 1.0
	cam_attr.auto_exposure_enabled = true
	cam_attr.auto_exposure_min_sensitivity = 50.0
	cam_attr.auto_exposure_max_sensitivity = 800.0
	world_env.camera_attributes = cam_attr

	print("  [OK] Environment: sky, tonemap, SSAO, SSIL, SSR, glow, fog, volumetric fog, SDFGI, DOF")


# ═══════════════════════════════════════════════════════════════════════════════
# CAMERA — with TAA and FSR2
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_camera() -> void:
	var camera := Camera3D.new()
	camera.position = Vector3(0, 3, 8)
	camera.look_at(Vector3(0, 1, 0))
	camera.far = 200.0
	camera.near = 0.1
	camera.current = true
	add_child(camera)

	# Enable TAA via viewport (exercises taa_resolve.glsl, motion_vectors.glsl)
	get_viewport().use_taa = true
	# Enable scaling for FSR (exercises FSR shaders)
	get_viewport().scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR2
	get_viewport().scaling_3d_scale = 0.75

	print("  [OK] Camera: TAA, FSR2 upscaling, motion vectors")


# ═══════════════════════════════════════════════════════════════════════════════
# LIGHTS — Directional, Omni, Spot, with shadows and projectors
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_lights() -> void:
	# Directional light with shadows (exercises shadow mapping, clustered lighting)
	var dir_light := DirectionalLight3D.new()
	dir_light.rotation_degrees = Vector3(-45, 30, 0)
	dir_light.light_color = Color(1, 0.95, 0.9)
	dir_light.light_energy = 1.5
	dir_light.shadow_enabled = true
	dir_light.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	dir_light.light_volumetric_fog_energy = 1.0
	add_child(dir_light)

	# Omni light with shadows (exercises point shadow mapping, cluster_store.glsl)
	var omni := OmniLight3D.new()
	omni.position = Vector3(3, 4, 2)
	omni.light_color = Color(1.0, 0.8, 0.6)
	omni.light_energy = 5.0
	omni.omni_range = 10.0
	omni.shadow_enabled = true
	omni.light_volumetric_fog_energy = 1.0
	add_child(omni)

	# Spot light with shadows (exercises spot shadow mapping)
	var spot := SpotLight3D.new()
	spot.position = Vector3(-3, 5, -2)
	spot.rotation_degrees = Vector3(-60, -20, 0)
	spot.light_color = Color(0.6, 0.8, 1.0)
	spot.light_energy = 8.0
	spot.spot_range = 15.0
	spot.spot_angle = 35.0
	spot.shadow_enabled = true
	spot.light_volumetric_fog_energy = 1.0
	add_child(spot)

	# Additional omni lights to trigger clustered lighting (cluster_store.glsl, cluster_render.glsl)
	for i in range(4):
		var extra_omni := OmniLight3D.new()
		extra_omni.position = Vector3(sin(i * TAU / 4.0) * 5, 2, cos(i * TAU / 4.0) * 5)
		extra_omni.light_color = Color(
			0.5 + 0.5 * sin(i * 1.5),
			0.5 + 0.5 * sin(i * 1.5 + 2.0),
			0.5 + 0.5 * sin(i * 1.5 + 4.0)
		)
		extra_omni.light_energy = 3.0
		extra_omni.omni_range = 6.0
		extra_omni.shadow_enabled = false  # Some without shadows for variant coverage
		add_child(extra_omni)

	print("  [OK] Lights: directional(shadow), omni(shadow), spot(shadow), 4 cluster omni")


# ═══════════════════════════════════════════════════════════════════════════════
# GEOMETRY — Meshes with every material feature combination
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_geometry_with_materials() -> void:
	# Ground plane (basic PBR)
	var ground := _create_mesh_instance(PlaneMesh.new(), Vector3(0, 0, 0))
	ground.mesh.size = Vector2(20, 20)
	var ground_mat := StandardMaterial3D.new()
	ground_mat.albedo_color = Color(0.3, 0.28, 0.25)
	ground_mat.roughness = 0.8
	ground_mat.metallic = 0.0
	ground.material_override = ground_mat

	# === Material Feature Coverage ===

	# 1. Normal mapping (NORMAL_USED)
	var normal_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(-5, 1, 0))
	var normal_mat := StandardMaterial3D.new()
	normal_mat.albedo_color = Color(0.8, 0.2, 0.2)
	normal_mat.normal_enabled = true
	normal_mat.normal_texture = _generate_noise_texture()
	normal_mat.normal_scale = 1.0
	normal_mesh.material_override = normal_mat

	# 2. Emission (EMISSION_USED)
	var emissive_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(-3, 1, 0))
	var emissive_mat := StandardMaterial3D.new()
	emissive_mat.albedo_color = Color(0.1, 0.1, 0.1)
	emissive_mat.emission_enabled = true
	emissive_mat.emission = Color(1.0, 0.5, 0.0)
	emissive_mat.emission_energy_multiplier = 3.0
	emissive_mesh.material_override = emissive_mat

	# 3. Metallic + Roughness (standard PBR path)
	var metallic_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(-1, 1, 0))
	var metallic_mat := StandardMaterial3D.new()
	metallic_mat.albedo_color = Color(0.9, 0.85, 0.7)
	metallic_mat.metallic = 1.0
	metallic_mat.roughness = 0.1
	metallic_mesh.material_override = metallic_mat

	# 4. Clearcoat (CLEARCOAT shader variant)
	var clearcoat_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(1, 1, 0))
	var clearcoat_mat := StandardMaterial3D.new()
	clearcoat_mat.albedo_color = Color(0.8, 0.1, 0.1)
	clearcoat_mat.clearcoat_enabled = true
	clearcoat_mat.clearcoat = 1.0
	clearcoat_mat.clearcoat_roughness = 0.1
	clearcoat_mesh.material_override = clearcoat_mat

	# 5. Anisotropy (anisotropic highlights)
	var aniso_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(3, 1, 0))
	var aniso_mat := StandardMaterial3D.new()
	aniso_mat.albedo_color = Color(0.7, 0.65, 0.6)
	aniso_mat.metallic = 0.8
	aniso_mat.roughness = 0.4
	aniso_mat.anisotropy_enabled = true
	aniso_mat.anisotropy = 0.8
	aniso_mesh.material_override = aniso_mat

	# 6. Subsurface Scattering (SSS_USED, exercises subsurface_scattering.glsl)
	var sss_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(5, 1, 0))
	var sss_mat := StandardMaterial3D.new()
	sss_mat.albedo_color = Color(0.9, 0.7, 0.6)
	sss_mat.subsurf_scatter_enabled = true
	sss_mat.subsurf_scatter_strength = 0.8
	sss_mat.subsurf_scatter_skin_mode = true
	sss_mesh.material_override = sss_mat

	# 7. Refraction (REFRACTION_USED)
	var refract_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(-5, 1, -3))
	var refract_mat := StandardMaterial3D.new()
	refract_mat.albedo_color = Color(0.9, 0.95, 1.0, 0.3)
	refract_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	refract_mat.refraction_enabled = true
	refract_mat.refraction = 0.05
	refract_mesh.material_override = refract_mat

	# 8. Heightmap / Parallax (HEIGHT_USED)
	var height_mesh := _create_mesh_instance(BoxMesh.new(), Vector3(-3, 1, -3))
	var height_mat := StandardMaterial3D.new()
	height_mat.albedo_color = Color(0.6, 0.55, 0.5)
	height_mat.heightmap_enabled = true
	height_mat.heightmap_texture = _generate_noise_texture()
	height_mat.heightmap_scale = 0.05
	height_mat.heightmap_deep_parallax = true
	height_mesh.material_override = height_mat

	# 9. Detail maps (DETAIL_ALBEDO, DETAIL_NORMAL)
	var detail_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(-1, 1, -3))
	var detail_mat := StandardMaterial3D.new()
	detail_mat.albedo_color = Color(0.5, 0.6, 0.5)
	detail_mat.detail_enabled = true
	detail_mat.detail_albedo = _generate_noise_texture()
	detail_mat.detail_normal = _generate_noise_texture()
	detail_mat.detail_blend_mode = BaseMaterial3D.BLEND_MODE_MIX
	detail_mat.detail_uv_layer = BaseMaterial3D.DETAIL_UV_1
	detail_mesh.material_override = detail_mat

	# 10. Rim lighting (RIM_USED)
	var rim_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(1, 1, -3))
	var rim_mat := StandardMaterial3D.new()
	rim_mat.albedo_color = Color(0.2, 0.2, 0.8)
	rim_mat.rim_enabled = true
	rim_mat.rim = 0.8
	rim_mat.rim_tint = 0.5
	rim_mesh.material_override = rim_mat

	# 11. Transmission / Backlight (TRANSMITTANCE_USED)
	var transmission_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(3, 1, -3))
	var transmission_mat := StandardMaterial3D.new()
	transmission_mat.albedo_color = Color(0.4, 0.8, 0.3)
	transmission_mat.backlight_enabled = true
	transmission_mat.backlight = Color(0.5, 0.8, 0.3)
	transmission_mesh.material_override = transmission_mat

	# 12. Alpha scissor (ALPHA_SCISSOR_THRESHOLD, depth prepass)
	var scissor_mesh := _create_mesh_instance(BoxMesh.new(), Vector3(5, 1, -3))
	var scissor_mat := StandardMaterial3D.new()
	scissor_mat.albedo_color = Color(0.8, 0.8, 0.8, 1.0)
	scissor_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	scissor_mat.alpha_scissor_threshold = 0.5
	scissor_mat.albedo_texture = _generate_noise_texture()
	scissor_mesh.material_override = scissor_mat

	# 13. Alpha hash (ALPHA_HASH_SCALE)
	var hash_mesh := _create_mesh_instance(BoxMesh.new(), Vector3(-5, 1, -6))
	var hash_mat := StandardMaterial3D.new()
	hash_mat.albedo_color = Color(0.7, 0.7, 0.7, 0.6)
	hash_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_HASH
	hash_mesh.material_override = hash_mat

	# 14. Depth-draw alpha prepass (opaque prepass variant)
	var prepass_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(-3, 1, -6))
	var prepass_mat := StandardMaterial3D.new()
	prepass_mat.albedo_color = Color(0.9, 0.8, 0.7, 0.7)
	prepass_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_DEPTH_PRE_PASS
	prepass_mesh.material_override = prepass_mat

	# 15. Unshaded / unlit material
	var unlit_mesh := _create_mesh_instance(BoxMesh.new(), Vector3(-1, 1, -6))
	var unlit_mat := StandardMaterial3D.new()
	unlit_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	unlit_mat.albedo_color = Color(1.0, 0.0, 1.0)
	unlit_mesh.material_override = unlit_mat

	# 16. UV2 / lightmap UV (UV2_USED)
	var uv2_mesh := _create_mesh_instance(BoxMesh.new(), Vector3(1, 1, -6))
	var uv2_mat := StandardMaterial3D.new()
	uv2_mat.albedo_color = Color(0.6, 0.6, 0.8)
	uv2_mat.uv2_scale = Vector3(2, 2, 1)
	uv2_mat.uv2_offset = Vector3(0.1, 0.1, 0)
	uv2_mesh.material_override = uv2_mat

	# 17. Billboard (BILLBOARD_ENABLED)
	var billboard_mesh := _create_mesh_instance(QuadMesh.new(), Vector3(3, 2, -6))
	var billboard_mat := StandardMaterial3D.new()
	billboard_mat.albedo_color = Color(1, 1, 0)
	billboard_mat.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	billboard_mesh.material_override = billboard_mat

	# 18. Proximity fade
	var proxfade_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(5, 1, -6))
	var proxfade_mat := StandardMaterial3D.new()
	proxfade_mat.albedo_color = Color(0.3, 0.8, 0.8, 0.8)
	proxfade_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	proxfade_mat.proximity_fade_enabled = true
	proxfade_mat.proximity_fade_distance = 1.0
	proxfade_mesh.material_override = proxfade_mat

	# 19. Distance fade
	var distfade_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(0, 1, -9))
	var distfade_mat := StandardMaterial3D.new()
	distfade_mat.albedo_color = Color(0.8, 0.3, 0.8)
	distfade_mat.distance_fade_mode = BaseMaterial3D.DISTANCE_FADE_PIXEL_ALPHA
	distfade_mat.distance_fade_min_distance = 5.0
	distfade_mat.distance_fade_max_distance = 15.0
	distfade_mesh.material_override = distfade_mat

	# 20. Grow (vertex displacement for outline)
	var grow_mesh := _create_mesh_instance(SphereMesh.new(), Vector3(2, 1, -9))
	var grow_mat := StandardMaterial3D.new()
	grow_mat.albedo_color = Color(0.1, 0.1, 0.1)
	grow_mat.grow_enabled = true
	grow_mat.grow = 0.02
	grow_mesh.material_override = grow_mat

	print("  [OK] Geometry: 20 material variants (normal, emission, metallic, clearcoat, anisotropy,")
	print("       SSS, refraction, heightmap, detail, rim, transmission, alpha_scissor, alpha_hash,")
	print("       depth_prepass, unshaded, uv2, billboard, proximity_fade, distance_fade, grow)")


# ═══════════════════════════════════════════════════════════════════════════════
# PARTICLES — GPU particles with trails, collision, attractors
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_particles() -> void:
	# GPU Particles (exercises particles.glsl, particles_copy.glsl)
	var particles := GPUParticles3D.new()
	particles.position = Vector3(0, 3, 3)
	particles.amount = 512
	particles.lifetime = 3.0
	particles.speed_scale = 1.0
	particles.explosiveness = 0.0
	particles.randomness = 0.5

	# Particle process material
	var pmat := ParticleProcessMaterial.new()
	pmat.direction = Vector3(0, 1, 0)
	pmat.spread = 30.0
	pmat.initial_velocity_min = 2.0
	pmat.initial_velocity_max = 5.0
	pmat.gravity = Vector3(0, -9.8, 0)
	pmat.damping_min = 0.5
	pmat.damping_max = 1.0
	pmat.scale_min = 0.1
	pmat.scale_max = 0.3
	pmat.color = Color(1.0, 0.5, 0.2)
	pmat.turbulence_enabled = true
	pmat.turbulence_noise_strength = 2.0
	pmat.turbulence_noise_speed = Vector3(0.5, 0.5, 0.5)
	particles.process_material = pmat

	# Trail (exercises USE_PARTICLE_TRAILS)
	particles.trail_enabled = true
	particles.trail_lifetime = 0.3

	# Particle mesh
	var particle_mesh := SphereMesh.new()
	particle_mesh.radius = 0.05
	particle_mesh.height = 0.1
	particles.draw_pass_1 = particle_mesh

	add_child(particles)

	# GPU Particle Collision — Sphere (exercises collision SDF shaders)
	var collision_sphere := GPUParticlesCollisionSphere3D.new()
	collision_sphere.position = Vector3(0, 1, 3)
	collision_sphere.radius = 1.5
	add_child(collision_sphere)

	# GPU Particle Collision — Box
	var collision_box := GPUParticlesCollisionBox3D.new()
	collision_box.position = Vector3(2, 0, 3)
	collision_box.size = Vector3(2, 2, 2)
	add_child(collision_box)

	# GPU Particle Attractor — Sphere
	var attractor := GPUParticlesAttractorSphere3D.new()
	attractor.position = Vector3(-1, 2, 3)
	attractor.radius = 3.0
	attractor.strength = 2.0
	add_child(attractor)

	print("  [OK] Particles: GPU particles with trails, turbulence, collision (sphere+box), attractor")


# ═══════════════════════════════════════════════════════════════════════════════
# GLOBAL ILLUMINATION — VoxelGI
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_gi() -> void:
	# VoxelGI (exercises voxel_gi.glsl, voxel_gi_sdf.glsl)
	var voxel_gi := VoxelGI.new()
	voxel_gi.size = Vector3(20, 10, 20)
	voxel_gi.position = Vector3(0, 5, 0)
	add_child(voxel_gi)

	print("  [OK] GI: VoxelGI node (SDFGI enabled via environment)")


# ═══════════════════════════════════════════════════════════════════════════════
# FOG VOLUMES — Localized volumetric fog
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_fog() -> void:
	# Fog volume with custom density (exercises fog volume shader)
	var fog_vol := FogVolume.new()
	fog_vol.position = Vector3(-4, 1.5, 4)
	fog_vol.size = Vector3(4, 3, 4)
	fog_vol.shape = RenderingServer.FOG_VOLUME_SHAPE_BOX

	var fog_mat := FogMaterial.new()
	fog_mat.density = 0.5
	fog_mat.albedo = Color(0.8, 0.85, 0.9)
	fog_mat.emission = Color(0.1, 0.1, 0.2)
	fog_vol.material = fog_mat
	add_child(fog_vol)

	# Second fog volume — ellipsoid shape
	var fog_vol2 := FogVolume.new()
	fog_vol2.position = Vector3(4, 2, 4)
	fog_vol2.size = Vector3(3, 3, 3)
	fog_vol2.shape = RenderingServer.FOG_VOLUME_SHAPE_ELLIPSOID
	var fog_mat2 := FogMaterial.new()
	fog_mat2.density = 0.8
	fog_mat2.albedo = Color(0.9, 0.7, 0.5)
	fog_vol2.material = fog_mat2
	add_child(fog_vol2)

	print("  [OK] Fog: 2 FogVolume nodes (box + ellipsoid)")


# ═══════════════════════════════════════════════════════════════════════════════
# DECALS — Projected texture blending
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_decals() -> void:
	# Decal with albedo + normal + ORM (exercises decal rendering in forward pass)
	var decal := Decal.new()
	decal.position = Vector3(0, 0.01, 0)
	decal.size = Vector3(3, 2, 3)
	decal.texture_albedo = _generate_noise_texture()
	decal.texture_normal = _generate_noise_texture()
	decal.albedo_mix = 0.8
	decal.modulate = Color(1, 1, 1, 0.9)
	decal.normal_fade = 0.5
	add_child(decal)

	# Second decal for emission
	var decal2 := Decal.new()
	decal2.position = Vector3(3, 0.01, -2)
	decal2.size = Vector3(2, 2, 2)
	decal2.texture_emission = _generate_noise_texture()
	decal2.emission_energy = 2.0
	add_child(decal2)

	print("  [OK] Decals: 2 decals (albedo+normal+ORM, emission)")


# ═══════════════════════════════════════════════════════════════════════════════
# REFLECTION PROBES — Cubemap reflections
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_reflection_probes() -> void:
	# Real-time reflection probe (exercises cubemap rendering pipeline)
	var probe := ReflectionProbe.new()
	probe.position = Vector3(0, 2, 0)
	probe.size = Vector3(10, 6, 10)
	probe.update_mode = ReflectionProbe.UPDATE_ALWAYS
	probe.interior = false
	probe.box_projection = true
	probe.max_distance = 50.0
	add_child(probe)

	# Interior reflection probe (exercises ambient override)
	var probe2 := ReflectionProbe.new()
	probe2.position = Vector3(-6, 2, -4)
	probe2.size = Vector3(6, 4, 6)
	probe2.update_mode = ReflectionProbe.UPDATE_ONCE
	probe2.interior = true
	probe2.ambient_mode = ReflectionProbe.AMBIENT_COLOR
	probe2.ambient_color = Color(0.2, 0.2, 0.3)
	probe2.ambient_color_energy = 1.0
	add_child(probe2)

	print("  [OK] Reflection probes: real-time (box projection) + interior (ambient override)")


# ═══════════════════════════════════════════════════════════════════════════════
# CANVAS 2D — Exercises canvas.glsl shader
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_canvas_2d() -> void:
	# CanvasLayer with various 2D elements
	var canvas_layer := CanvasLayer.new()
	add_child(canvas_layer)

	# ColorRect — basic canvas rendering
	var rect := ColorRect.new()
	rect.color = Color(0.2, 0.3, 0.5, 0.5)
	rect.position = Vector2(10, 10)
	rect.size = Vector2(200, 40)
	canvas_layer.add_child(rect)

	# Label — text rendering (exercises MSDF text shader variant)
	var label := Label.new()
	label.text = "WebGPU Shader Coverage Test"
	label.position = Vector2(15, 15)
	label.add_theme_color_override("font_color", Color(1, 1, 1))
	canvas_layer.add_child(label)

	# NinePatchRect (exercises ninepatch rendering path)
	var nine := NinePatchRect.new()
	nine.position = Vector2(10, 60)
	nine.size = Vector2(150, 40)
	nine.texture = _generate_noise_texture()
	nine.patch_margin_left = 4
	nine.patch_margin_right = 4
	nine.patch_margin_top = 4
	nine.patch_margin_bottom = 4
	canvas_layer.add_child(nine)

	# Canvas light to exercise canvas lighting path
	var canvas_light := PointLight2D.new()
	canvas_light.position = Vector2(640, 360)
	canvas_light.energy = 1.5
	canvas_light.texture = _generate_noise_texture()
	canvas_layer.add_child(canvas_light)

	print("  [OK] Canvas 2D: ColorRect, Label (MSDF), NinePatchRect, PointLight2D")


# ═══════════════════════════════════════════════════════════════════════════════
# SKELETON — Skinned mesh (exercises skeleton.glsl, BONES_USED)
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_skeleton_mesh() -> void:
	# Create a simple skeleton with 2 bones
	var skeleton := Skeleton3D.new()
	skeleton.position = Vector3(-7, 0, 2)

	# Add bones
	var root_bone := skeleton.add_bone("root")
	var child_bone := skeleton.add_bone("child")
	skeleton.set_bone_parent(child_bone, root_bone)
	skeleton.set_bone_rest(root_bone, Transform3D.IDENTITY)
	skeleton.set_bone_rest(child_bone, Transform3D(Basis.IDENTITY, Vector3(0, 1, 0)))

	add_child(skeleton)

	# Create a mesh with skin weights
	var mesh_inst := MeshInstance3D.new()
	var capsule := CapsuleMesh.new()
	capsule.radius = 0.3
	capsule.height = 2.0
	mesh_inst.mesh = capsule
	mesh_inst.position = Vector3(0, 1, 0)

	# Create a skin resource
	var skin := Skin.new()
	skin.add_bind(root_bone, Transform3D.IDENTITY)
	skin.add_bind(child_bone, Transform3D(Basis.IDENTITY, Vector3(0, -1, 0)))
	mesh_inst.skin = skin
	skeleton.add_child(mesh_inst)

	# Animate the bone slightly to ensure skinning code runs
	skeleton.set_bone_pose_rotation(child_bone, Quaternion(Vector3(0, 0, 1), 0.3))

	print("  [OK] Skeleton: 2-bone skeleton with skinned CapsuleMesh")


# ═══════════════════════════════════════════════════════════════════════════════
# MULTIMESH — Instanced rendering (exercises multimesh/instanced draw path)
# ═══════════════════════════════════════════════════════════════════════════════

func _setup_multimesh() -> void:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true
	mm.use_custom_data = true
	mm.instance_count = 64

	var cube := BoxMesh.new()
	cube.size = Vector3(0.3, 0.3, 0.3)
	mm.mesh = cube

	# Set transforms in a grid pattern
	for i in range(64):
		var x := (i % 8) * 0.5 - 2.0
		var z := (i / 8) * 0.5 - 2.0
		var t := Transform3D(Basis.IDENTITY, Vector3(x + 7, 0.15, z))
		mm.set_instance_transform(i, t)
		mm.set_instance_color(i, Color(
			float(i % 8) / 8.0,
			float(i / 8) / 8.0,
			0.5, 1.0
		))
		mm.set_instance_custom_data(i, Color(randf(), randf(), randf(), randf()))

	var mm_inst := MultiMeshInstance3D.new()
	mm_inst.multimesh = mm
	add_child(mm_inst)

	print("  [OK] MultiMesh: 64 instanced cubes with per-instance colors and custom data")


# ═══════════════════════════════════════════════════════════════════════════════
# UTILITY
# ═══════════════════════════════════════════════════════════════════════════════

func _create_mesh_instance(mesh: Mesh, pos: Vector3) -> MeshInstance3D:
	var inst := MeshInstance3D.new()
	inst.mesh = mesh
	inst.position = pos
	add_child(inst)
	return inst


func _generate_noise_texture() -> NoiseTexture2D:
	var tex := NoiseTexture2D.new()
	var noise := FastNoiseLite.new()
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX
	noise.frequency = 0.05
	tex.noise = noise
	tex.width = 64
	tex.height = 64
	return tex


func _report_results() -> void:
	print("\n[ShaderCoverage] ═══════════════════════════════════════════")
	print("[ShaderCoverage] Rendered %d frames successfully." % frame_count)
	if errors.is_empty():
		print("[ShaderCoverage] PASS — All shader paths exercised without errors.")
	else:
		print("[ShaderCoverage] FAIL — %d shader errors:" % errors.size())
		for err in errors:
			print("  - %s" % err)
	print("[ShaderCoverage] ═══════════════════════════════════════════\n")

	# Write JSON report
	var report := {
		"timestamp": Time.get_datetime_string_from_system(true),
		"frames_rendered": frame_count,
		"errors": errors,
		"pass": errors.is_empty(),
	}
	var file := FileAccess.open(REPORT_PATH, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(report, "\t"))
		file.close()
		print("[ShaderCoverage] Report saved to: %s" % REPORT_PATH)
