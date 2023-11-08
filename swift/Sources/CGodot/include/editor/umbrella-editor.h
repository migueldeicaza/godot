#define TOOLS_ENABLED 1
#define DEBUG_ENABLED 1

#include "editor/editor_run.h"
#include "editor/editor_atlas_packer.h"
#include "editor/editor_translation_parser.h"
#include "editor/doc_data_class_path.gen.h"
#include "editor/multi_node_edit.h"
#include "editor/animation_track_editor_plugins.h"
#include "editor/project_manager.h"
#include "editor/editor_scale.h"
#include "editor/dependency_editor.h"
#include "editor/editor_about.h"
#include "editor/fbx_importer_manager.h"
#include "editor/shader_globals_editor.h"
#include "editor/editor_folding.h"
#include "editor/editor_layouts_dialog.h"
#include "editor/editor_fonts.h"
#include "editor/find_in_files.h"
#include "editor/editor_command_palette.h"
#include "editor/input_event_configuration_dialog.h"
#include "editor/import_dock.h"
#include "editor/editor_settings.h"
#include "editor/editor_vcs_interface.h"
#include "editor/code_editor.h"
#include "editor/surface_upgrade_tool.h"
#include "editor/editor_file_system.h"
#include "editor/create_dialog.h"
#include "editor/plugins/gpu_particles_collision_sdf_editor_plugin.h"
#include "editor/plugins/texture_layered_editor_plugin.h"
#include "editor/plugins/script_editor_plugin.h"
#include "editor/plugins/asset_library_editor_plugin.h"
#include "editor/plugins/node_3d_editor_plugin.h"
#include "editor/plugins/occluder_instance_3d_editor_plugin.h"
#include "editor/plugins/polygon_3d_editor_plugin.h"
#include "editor/plugins/navigation_obstacle_2d_editor_plugin.h"
#include "editor/plugins/packed_scene_translation_parser_plugin.h"
#include "editor/plugins/theme_editor_plugin.h"

#include "editor/plugins/text_shader_editor.h"
#include "editor/plugins/animation_tree_editor_plugin.h"
#include "editor/plugins/visual_shader_editor_plugin.h"
#include "editor/plugins/sub_viewport_preview_editor_plugin.h"
#include "editor/plugins/texture_3d_editor_plugin.h"
#include "editor/plugins/gpu_particles_2d_editor_plugin.h"
#include "editor/plugins/mesh_editor_plugin.h"
#include "editor/plugins/collision_polygon_2d_editor_plugin.h"
#include "editor/plugins/lightmap_gi_editor_plugin.h"
#include "editor/plugins/collision_shape_2d_editor_plugin.h"
#include "editor/plugins/shader_editor_plugin.h"
#include "editor/plugins/resource_preloader_editor_plugin.h"
#include "editor/plugins/mesh_library_editor_plugin.h"
#include "editor/plugins/gizmos/gizmo_3d_helper.h"
#include "editor/plugins/gizmos/audio_stream_player_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/physics_bone_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/reflection_probe_gizmo_plugin.h"
#include "editor/plugins/gizmos/lightmap_gi_gizmo_plugin.h"
#include "editor/plugins/gizmos/gpu_particles_collision_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/occluder_instance_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/spring_arm_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/collision_polygon_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/decal_gizmo_plugin.h"
#include "editor/plugins/gizmos/light_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/sprite_base_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/collision_shape_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/marker_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/voxel_gi_gizmo_plugin.h"
#include "editor/plugins/gizmos/collision_object_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/navigation_link_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/visible_on_screen_notifier_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/shape_cast_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/gpu_particles_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/mesh_instance_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/soft_body_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/navigation_region_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/ray_cast_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/camera_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/vehicle_body_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/cpu_particles_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/joint_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/audio_listener_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/label_3d_gizmo_plugin.h"
#include "editor/plugins/gizmos/fog_volume_gizmo_plugin.h"
#include "editor/plugins/gizmos/lightmap_probe_gizmo_plugin.h"
#include "editor/plugins/animation_blend_space_2d_editor.h"
#include "editor/plugins/navigation_obstacle_3d_editor_plugin.h"
#include "editor/plugins/polygon_2d_editor_plugin.h"
#include "editor/plugins/debugger_editor_plugin.h"
#include "editor/plugins/editor_resource_tooltip_plugins.h"
#include "editor/plugins/curve_editor_plugin.h"
#include "editor/plugins/animation_player_editor_plugin.h"
#include "editor/plugins/texture_editor_plugin.h"
#include "editor/plugins/camera_3d_editor_plugin.h"
#include "editor/plugins/version_control_editor_plugin.h"
#include "editor/plugins/physical_bone_3d_editor_plugin.h"
#include "editor/plugins/bit_map_editor_plugin.h"
#include "editor/plugins/skeleton_ik_3d_editor_plugin.h"
#include "editor/plugins/gpu_particles_3d_editor_plugin.h"
#include "editor/plugins/material_editor_plugin.h"
#include "editor/plugins/dedicated_server_export_plugin.h"
#include "editor/plugins/sprite_2d_editor_plugin.h"
#include "editor/plugins/shader_file_editor_plugin.h"
#include "editor/plugins/animation_blend_space_1d_editor.h"
#include "editor/plugins/skeleton_3d_editor_plugin.h"
#include "editor/plugins/multimesh_editor_plugin.h"
#include "editor/plugins/sprite_frames_editor_plugin.h"
#include "editor/plugins/cast_2d_editor_plugin.h"
#include "editor/plugins/tiles/tile_proxies_manager_dialog.h"
#include "editor/plugins/tiles/tile_map_editor.h"
#include "editor/plugins/tiles/tile_set_scenes_collection_source_editor.h"
#include "editor/plugins/tiles/tiles_editor_plugin.h"
#include "editor/plugins/tiles/tile_set_editor.h"
#include "editor/plugins/tiles/tile_atlas_view.h"
#include "editor/plugins/tiles/tile_data_editors.h"
#include "editor/plugins/tiles/atlas_merging_dialog.h"
#include "editor/plugins/tiles/tile_set_atlas_source_editor.h"
#include "editor/plugins/navigation_polygon_editor_plugin.h"
#include "editor/plugins/animation_library_editor.h"
#include "editor/plugins/gradient_texture_2d_editor_plugin.h"
#include "editor/plugins/abstract_polygon_2d_editor.h"
#include "editor/plugins/canvas_item_editor_plugin.h"
#include "editor/plugins/input_event_editor_plugin.h"
#include "editor/plugins/audio_stream_editor_plugin.h"
#include "editor/plugins/editor_debugger_plugin.h"
#include "editor/plugins/navigation_link_2d_editor_plugin.h"
#include "editor/plugins/animation_blend_tree_editor_plugin.h"
#include "editor/plugins/style_box_editor_plugin.h"
#include "editor/plugins/control_editor_plugin.h"
#include "editor/plugins/path_3d_editor_plugin.h"
#include "editor/plugins/cpu_particles_2d_editor_plugin.h"
#include "editor/plugins/root_motion_editor_plugin.h"
#include "editor/plugins/text_editor.h"
#include "editor/plugins/animation_state_machine_editor.h"
#include "editor/plugins/script_text_editor.h"
#include "editor/plugins/light_occluder_2d_editor_plugin.h"
#include "editor/plugins/gdextension_export_plugin.h"
#include "editor/plugins/audio_stream_randomizer_editor_plugin.h"
#include "editor/plugins/packed_scene_editor_plugin.h"
#include "editor/plugins/texture_region_editor_plugin.h"
#include "editor/plugins/skeleton_2d_editor_plugin.h"
#include "editor/plugins/line_2d_editor_plugin.h"
#include "editor/plugins/bone_map_editor_plugin.h"
#include "editor/plugins/gradient_editor_plugin.h"
#include "editor/plugins/voxel_gi_editor_plugin.h"
#include "editor/plugins/node_3d_editor_gizmos.h"
#include "editor/plugins/font_config_plugin.h"
#include "editor/plugins/cpu_particles_3d_editor_plugin.h"
#include "editor/plugins/theme_editor_preview.h"
#include "editor/plugins/path_2d_editor_plugin.h"
#include "editor/plugins/mesh_instance_3d_editor_plugin.h"
#include "editor/plugins/editor_resource_conversion_plugin.h"
#include "editor/plugins/editor_preview_plugins.h"
#include "editor/editor_translations.gen.h"
#include "editor/editor_feature_profile.h"
// This conflicts with one from CGodot: scene/resources/default_theme/default_font.gen.h
//#include "editor/builtin_fonts.gen.h"
#include "editor/history_dock.h"
#include "editor/rename_dialog.h"
#include "editor/editor_log.h"
#include "editor/editor_build_profile.h"
#include "editor/progress_dialog.h"
#include "editor/groups_editor.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/editor_help_search.h"
#include "editor/editor_sectioned_inspector.h"
#include "editor/scene_create_dialog.h"
#include "editor/doc_translations.gen.h"
#include "editor/scene_tree_dock.h"
#include "editor/editor_string_names.h"
#include "editor/editor_native_shader_source_visualizer.h"
#include "editor/editor_property_name_processor.h"
#include "editor/window_wrapper.h"
#include "editor/property_translations.gen.h"
#include "editor/animation_track_editor.h"
#include "editor/editor_properties.h"
#include "editor/event_listener_line_edit.h"
#include "editor/renames_map_3_to_4.h"
#include "editor/register_exporters.h"
#include "editor/editor_locale_dialog.h"
#include "editor/umbrella-editor.h"
#include "editor/editor_help.h"
#include "editor/editor_script.h"
#include "editor/doc_tools.h"
#include "editor/editor_plugin.h"
#include "editor/node_dock.h"
#include "editor/directory_create_dialog.h"
#include "editor/audio_stream_preview.h"
#include "editor/register_editor_types.h"
#include "editor/project_settings_editor.h"
#include "editor/editor_run_native.h"
#include "editor/editor_asset_installer.h"
#include "editor/gui/editor_zoom_widget.h"
#include "editor/gui/editor_validation_panel.h"
#include "editor/gui/editor_scene_tabs.h"
#include "editor/gui/editor_toaster.h"
#include "editor/gui/editor_object_selector.h"
#include "editor/gui/editor_spin_slider.h"
#include "editor/gui/editor_dir_dialog.h"
#include "editor/gui/editor_title_bar.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/scene_tree_editor.h"
#include "editor/gui/editor_run_bar.h"
#include "editor/action_map_editor.h"
#include "editor/editor_properties_vector.h"
#include "editor/editor_paths.h"
#include "editor/editor_settings_dialog.h"
#include "editor/editor_translation.h"
#include "editor/editor_audio_buses.h"
#include "editor/editor_properties_array_dict.h"
#include "editor/debugger/editor_performance_profiler.h"
#include "editor/debugger/editor_file_server.h"
#include "editor/debugger/editor_debugger_server.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/debugger/editor_visual_profiler.h"
#include "editor/debugger/editor_debugger_inspector.h"
#include "editor/debugger/debug_adapter/debug_adapter_server.h"
#include "editor/debugger/debug_adapter/debug_adapter_protocol.h"
#include "editor/debugger/debug_adapter/debug_adapter_parser.h"
#include "editor/debugger/debug_adapter/debug_adapter_types.h"
#include "editor/debugger/editor_debugger_tree.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/editor_profiler.h"
#include "editor/editor_themes.h"
#include "editor/editor_resource_preview.h"
#include "editor/plugin_config_dialog.h"
#include "editor/editor_quick_open.h"
#include "editor/script_create_dialog.h"
#include "editor/export/editor_export_shared_object.h"
#include "editor/export/editor_export_plugin.h"
#include "editor/export/export_template_manager.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform_pc.h"
#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_preset.h"
#include "editor/export/project_export.h"
#include "editor/editor_autoload_settings.h"
#include "editor/pot_generator.h"
#include "editor/editor_data.h"
#include "editor/import_defaults_editor.h"
#include "editor/project_converter_3_to_4.h"
#include "editor/editor_inspector.h"
#include "editor/doc_data_compressed.gen.h"
#include "editor/import/post_import_plugin_skeleton_renamer.h"
#include "editor/import/resource_importer_obj.h"
#include "editor/import/resource_importer_dynamic_font.h"
#include "editor/import/resource_importer_csv_translation.h"
#include "editor/import/post_import_plugin_skeleton_track_organizer.h"
#include "editor/import/resource_importer_wav.h"
#include "editor/import/post_import_plugin_skeleton_rest_fixer.h"
#include "editor/import/collada.h"
#include "editor/import/resource_importer_scene.h"
#include "editor/import/resource_importer_image.h"
#include "editor/import/resource_importer_texture.h"
#include "editor/import/editor_import_collada.h"
#include "editor/import/resource_importer_texture_atlas.h"
#include "editor/import/editor_import_plugin.h"
#include "editor/import/resource_importer_bmfont.h"
#include "editor/import/scene_import_settings.h"
#include "editor/import/dynamic_font_import_settings.h"
#include "editor/import/resource_importer_shader_file.h"
#include "editor/import/resource_importer_texture_settings.h"
#include "editor/import/resource_importer_layered_texture.h"
#include "editor/import/resource_importer_bitmask.h"
#include "editor/import/resource_importer_imagefont.h"
#include "editor/import/audio_stream_import_settings.h"
#include "editor/animation_bezier_editor.h"
#include "editor/inspector_dock.h"
#include "editor/editor_interface.h"
#include "editor/shader_create_dialog.h"
#include "editor/connections_dialog.h"
#include "editor/editor_resource_picker.h"
#include "editor/localization_editor.h"
#include "editor/editor_node.h"
#include "editor/editor_icons.gen.h"
#include "editor/property_selector.h"
#include "editor/filesystem_dock.h"
#include "editor/editor_plugin_settings.h"
#include "editor/reparent_dialog.h"
