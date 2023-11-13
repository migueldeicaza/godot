#include "scene/scene_string_names.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/voxel_gi.h"
#include "scene/3d/collision_object_3d.h"
#include "scene/3d/marker_3d.h"
#include "scene/3d/reflection_probe.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/remote_transform_3d.h"
#include "scene/3d/visible_on_screen_notifier_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/3d/path_3d.h"
#include "scene/3d/ray_cast_3d.h"
#include "scene/3d/physics_body_3d.h"
#include "scene/3d/spring_arm_3d.h"
#include "scene/3d/area_3d.h"
#include "scene/3d/voxelizer.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/decal.h"
#include "scene/3d/navigation_obstacle_3d.h"
#include "scene/3d/vehicle_body_3d.h"
#include "scene/3d/joint_3d.h"
#include "scene/3d/navigation_region_3d.h"
#include "scene/3d/navigation_agent_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/3d/lightmap_probe.h"
#include "scene/3d/fog_volume.h"
#include "scene/3d/velocity_tracker_3d.h"
#include "scene/3d/shape_cast_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/collision_polygon_3d.h"
#include "scene/3d/soft_body_3d.h"
#include "scene/3d/audio_listener_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/navigation_link_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/skeleton_ik_3d.h"
#include "scene/3d/occluder_instance_3d.h"
#include "scene/3d/collision_shape_3d.h"
#include "scene/3d/bone_attachment_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/3d/gpu_particles_collision_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/label_3d.h"
#include "scene/3d/lightmap_gi.h"
#include "scene/3d/lightmapper.h"
#include "scene/3d/importer_mesh_instance_3d.h"
#include "scene/3d/sprite_3d.h"
#include "scene/3d/xr_nodes.h"
#include "scene/resources/portable_compressed_texture.h"
#include "scene/resources/style_box_texture.h"
#include "scene/resources/capsule_shape_2d.h"
#include "scene/resources/shape_2d.h"
#include "scene/resources/syntax_highlighter.h"
#include "scene/resources/concave_polygon_shape_3d.h"
#include "scene/resources/theme.h"
#include "scene/resources/animation.h"
#include "scene/resources/bit_map.h"
#include "scene/resources/skeleton_modification_2d.h"
#include "scene/resources/animated_texture.h"
#include "scene/resources/sprite_frames.h"
#include "scene/resources/style_box_line.h"
#include "scene/resources/circle_shape_2d.h"
#include "scene/resources/shader.h"
#include "scene/resources/tile_set.h"
#include "scene/resources/polygon_path_finder.h"
#include "scene/resources/capsule_shape_3d.h"
#include "scene/resources/cylinder_shape_3d.h"
#include "scene/resources/surface_tool.h"
#include "scene/resources/shape_3d.h"
#include "scene/resources/skeleton_modification_2d_ccdik.h"
#include "scene/resources/mesh_library.h"
#include "scene/resources/concave_polygon_shape_2d.h"
#include "scene/resources/text_paragraph.h"
#include "scene/resources/compressed_texture.h"
#include "scene/resources/gradient.h"
#include "scene/resources/navigation_polygon.h"
#include "scene/resources/sky_material.h"
#include "scene/resources/visual_shader.h"
#include "scene/resources/rectangle_shape_2d.h"
#include "scene/resources/height_map_shape_3d.h"
#include "scene/resources/navigation_mesh_source_geometry_data_2d.h"
#include "scene/resources/material.h"
#include "scene/resources/animation_library.h"
#include "scene/resources/multimesh.h"
#include "scene/resources/text_file.h"
#include "scene/resources/audio_stream_wav.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/navigation_mesh_source_geometry_data_3d.h"
#include "scene/resources/mesh_data_tool.h"
#include "scene/resources/skeleton_modification_2d_stackholder.h"
#include "scene/resources/sphere_shape_3d.h"
#include "scene/resources/physics_material.h"
#include "scene/resources/particle_process_material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/skeleton_modification_2d_lookat.h"
#include "scene/resources/environment.h"
#include "scene/resources/curve.h"
#include "scene/resources/texture_rd.h"
#include "scene/resources/segment_shape_2d.h"
#include "scene/resources/visual_shader_particle_nodes.h"
#include "scene/resources/visual_shader_nodes.h"
#include "scene/resources/shader_include.h"
#include "scene/resources/skin.h"
#include "scene/resources/fog_material.h"
#include "scene/resources/skeleton_modification_2d_jiggle.h"
#include "scene/resources/camera_texture.h"
#include "scene/resources/skeleton_modification_2d_twoboneik.h"
#include "scene/resources/box_shape_3d.h"
#include "scene/resources/skeleton_modification_2d_physicalbones.h"
#include "scene/resources/placeholder_textures.h"
#include "scene/resources/navigation_mesh.h"
#include "scene/resources/resource_format_text.h"
#include "scene/resources/importer_mesh.h"
#include "scene/resources/skeleton_modification_stack_2d.h"
#include "scene/resources/style_box.h"
#include "scene/resources/mesh_texture.h"
#include "scene/resources/font.h"
#include "scene/resources/texture.h"
#include "scene/resources/convex_polygon_shape_3d.h"
#include "scene/resources/world_boundary_shape_3d.h"
#include "scene/resources/audio_stream_polyphonic.h"
#include "scene/resources/skeleton_profile.h"
#include "scene/resources/separation_ray_shape_2d.h"
#include "scene/resources/label_settings.h"
#include "scene/resources/world_2d.h"
#include "scene/resources/visual_shader_sdf_nodes.h"
#include "scene/resources/video_stream.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/bone_map.h"
#include "scene/resources/immediate_mesh.h"
#include "scene/resources/skeleton_modification_2d_fabrik.h"
#include "scene/resources/primitive_meshes.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/curve_texture.h"
#include "scene/resources/canvas_item_material.h"
#include "scene/resources/convex_polygon_shape_2d.h"
#include "scene/resources/text_line.h"
#include "scene/resources/world_boundary_shape_2d.h"
#include "scene/resources/separation_ray_shape_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/world_3d.h"
#include "scene/resources/sky.h"
#include "scene/animation/tween.h"
#include "scene/animation/animation_node_state_machine.h"
#include "scene/animation/animation_blend_space_2d.h"
#include "scene/animation/animation_mixer.h"
#include "scene/animation/animation_blend_space_1d.h"
#include "scene/animation/animation_tree.h"
#include "scene/animation/animation_blend_tree.h"
#include "scene/animation/easing_equations.h"
#include "scene/animation/animation_player.h"
#include "scene/animation/root_motion_view.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/theme/default_theme.h"
#include "scene/theme/theme_owner.h"
#include "scene/theme/theme_db.h"
#include "scene/register_scene_types.h"
#include "scene/gui/video_stream_player.h"
#include "scene/gui/file_dialog.h"
#include "scene/gui/item_list.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/control.h"
#include "scene/gui/range.h"
#include "scene/gui/label.h"
#include "scene/gui/graph_edit.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/popup.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/container.h"
#include "scene/gui/rich_text_effect.h"
#include "scene/gui/graph_element.h"
#include "scene/gui/code_edit.h"
#include "scene/gui/base_button.h"
#include "scene/gui/graph_node.h"
#include "scene/gui/texture_progress_bar.h"
#include "scene/gui/tree.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/nine_patch_rect.h"
#include "scene/gui/texture_button.h"
#include "scene/gui/box_container.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/aspect_ratio_container.h"
#include "scene/gui/menu_bar.h"
#include "scene/gui/center_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/option_button.h"
#include "scene/gui/slider.h"
#include "scene/gui/check_button.h"
#include "scene/gui/panel.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/gui/view_panner.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/separator.h"
#include "scene/gui/button.h"
#include "scene/gui/link_button.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/color_mode.h"
#include "scene/gui/reference_rect.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/graph_edit_arranger.h"
#include "scene/debugger/scene_debugger.h"
#include "scene/main/instance_placeholder.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/multiplayer_peer.h"
#include "scene/main/viewport.h"
#include "scene/main/missing_node.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/window.h"
#include "scene/main/canvas_item.h"
#include "scene/main/node.h"
#include "scene/main/timer.h"
#include "scene/main/resource_preloader.h"
#include "scene/main/scene_tree.h"
#include "scene/main/http_request.h"
#include "scene/main/shader_globals_override.h"
#include "scene/2d/visible_on_screen_notifier_2d.h"
#include "scene/2d/path_2d.h"
#include "scene/2d/line_2d.h"
#include "scene/2d/remote_transform_2d.h"
#include "scene/2d/parallax_background.h"
#include "scene/2d/touch_screen_button.h"
#include "scene/2d/marker_2d.h"
#include "scene/2d/camera_2d.h"
#include "scene/2d/audio_stream_player_2d.h"
#include "scene/2d/collision_object_2d.h"
#include "scene/2d/navigation_agent_2d.h"
#include "scene/2d/line_builder.h"
#include "scene/2d/back_buffer_copy.h"
#include "scene/2d/navigation_obstacle_2d.h"
#include "scene/2d/joint_2d.h"
#include "scene/2d/physical_bone_2d.h"
#include "scene/2d/navigation_region_2d.h"
#include "scene/2d/light_occluder_2d.h"
#include "scene/2d/area_2d.h"
#include "scene/2d/mesh_instance_2d.h"
#include "scene/2d/ray_cast_2d.h"
#include "scene/2d/physics_body_2d.h"
#include "scene/2d/animated_sprite_2d.h"
#include "scene/2d/navigation_link_2d.h"
#include "scene/2d/light_2d.h"
#include "scene/2d/canvas_group.h"
#include "scene/2d/audio_listener_2d.h"
#include "scene/2d/gpu_particles_2d.h"
#include "scene/2d/cpu_particles_2d.h"
#include "scene/2d/collision_polygon_2d.h"
#include "scene/2d/shape_cast_2d.h"
#include "scene/2d/skeleton_2d.h"
#include "scene/2d/sprite_2d.h"
#include "scene/2d/parallax_layer.h"
#include "scene/2d/canvas_modulate.h"
#include "scene/2d/tile_map.h"
#include "scene/2d/node_2d.h"
#include "scene/2d/polygon_2d.h"
#include "scene/2d/collision_shape_2d.h"
#include "scene/2d/multimesh_instance_2d.h"
#include "scene/property_utils.h"
