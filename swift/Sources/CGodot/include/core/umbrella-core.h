#include "core/crypto/crypto.h"
#include "core/crypto/hashing_context.h"
#include "core/crypto/aes_context.h"
#include "core/crypto/crypto_core.h"
#include "core/core_globals.h"
#include "core/version.h"
#include "core/extension/extension_api_dump.h"
#include "core/extension/gdextension_interface.h"
#include "core/extension/gdextension_interface_dump.gen.h"
#include "core/extension/gdextension_compat_hashes.h"
#include "core/extension/gdextension_manager.h"
#include "core/extension/gdextension.h"
#include "core/typedefs.h"
#include "core/config/project_settings.h"
#include "core/config/engine.h"
#include "core/doc_data.h"
#include "core/input/shortcut.h"
#include "core/input/input_event.h"
#include "core/input/input.h"
#include "core/input/input_map.h"
#include "core/input/default_controller_mappings.h"
#include "core/input/input_enums.h"
#include "core/authors.gen.h"
#include "core/io/http_client_tcp.h"
#include "core/io/file_access_compressed.h"
#include "core/io/http_client.h"
#include "core/io/stream_peer.h"
#include "core/io/image_loader.h"
#include "core/io/file_access_pack.h"
#include "core/io/tcp_server.h"
#include "core/io/resource_importer.h"
#include "core/io/stream_peer_tls.h"
#include "core/io/net_socket.h"
#include "core/io/config_file.h"
#include "core/io/resource_loader.h"
#include "core/io/dir_access.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/xml_parser.h"
#include "core/io/packet_peer_dtls.h"
#include "core/io/file_access_zip.h"
#include "core/io/file_access_memory.h"
#include "core/io/compression.h"
#include "core/io/resource_saver.h"
#include "core/io/certs_compressed.gen.h"
#include "core/io/dtls_server.h"
#include "core/io/file_access.h"
#include "core/io/remote_filesystem_client.h"
#include "core/io/resource_uid.h"
#include "core/io/pck_packer.h"
#include "core/io/packet_peer.h"
#include "core/io/json.h"
#include "core/io/stream_peer_gzip.h"
#include "core/io/packet_peer_udp.h"
#include "core/io/translation_loader_po.h"
#include "core/io/resource_format_binary.h"
#include "core/io/resource.h"
#include "core/io/ip_address.h"
#include "core/io/zip_io.h"
#include "core/io/logger.h"
#include "core/io/ip.h"
#include "core/io/marshalls.h"
#include "core/io/udp_server.h"
#include "core/io/packed_data_container.h"
#include "core/io/image.h"
#include "core/io/missing_resource.h"
#include "core/io/file_access_encrypted.h"
#include "core/version_generated.gen.h"
#include "core/license.gen.h"
#include "core/math/geometry_2d.h"
#include "core/math/a_star_grid_2d.h"
#include "core/math/basis.h"
#include "core/math/math_funcs.h"
#include "core/math/math_defs.h"
#include "core/math/bvh_tree.h"
#include "core/math/projection.h"
#include "core/math/face3.h"
#include "core/math/geometry_3d.h"
#include "core/math/vector2.h"
#include "core/math/static_raycaster.h"
#include "core/math/random_number_generator.h"
#include "core/math/quaternion.h"
#include "core/math/color.h"
#include "core/math/plane.h"
#include "core/math/rect2.h"
#include "core/math/vector4i.h"
#include "core/math/math_fieldwise.h"
#include "core/math/vector3i.h"
#include "core/math/bvh.h"
#include "core/math/expression.h"
#include "core/math/rect2i.h"
#include "core/math/bvh_abb.h"
#include "core/math/audio_frame.h"
#include "core/math/vector4.h"
#include "core/math/vector2i.h"
#include "core/math/convex_hull.h"
#include "core/math/quick_hull.h"
#include "core/math/random_pcg.h"
#include "core/math/triangulate.h"
#include "core/math/delaunay_2d.h"
#include "core/math/disjoint_set.h"
#include "core/math/a_star.h"
#include "core/math/dynamic_bvh.h"
#include "core/math/transform_2d.h"
#include "core/math/transform_3d.h"
#include "core/math/triangle_mesh.h"
#include "core/math/aabb.h"
#include "core/math/vector3.h"
#include "core/math/delaunay_3d.h"
#include "core/disabled_classes.gen.h"
#include "core/object/method_bind.h"
#include "core/object/object_id.h"
#include "core/object/callable_method_pointer.h"
#include "core/object/script_language_extension.h"
#include "core/object/class_db.h"
#include "core/object/script_instance.h"
#include "core/object/script_language.h"
#include "core/object/object.h"
#include "core/object/message_queue.h"
#include "core/object/undo_redo.h"
#include "core/object/worker_thread_pool.h"
#include "core/object/ref_counted.h"
#include "core/core_string_names.h"
#include "core/debugger/engine_debugger.h"
#include "core/debugger/engine_profiler.h"
#include "core/debugger/script_debugger.h"
#include "core/debugger/remote_debugger_peer.h"
#include "core/debugger/debugger_marshalls.h"
#include "core/debugger/local_debugger.h"
#include "core/debugger/remote_debugger.h"
#include "core/variant/binder_common.h"
#include "core/variant/variant_op.h"
#include "core/variant/variant_setget.h"
#include "core/variant/variant_destruct.h"
#include "core/variant/variant_parser.h"
#include "core/variant/variant_internal.h"
#include "core/variant/callable.h"
#include "core/variant/variant_construct.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/method_ptrcall.h"
#include "core/variant/dictionary.h"
#include "core/variant/array.h"
#include "core/variant/callable_bind.h"
#include "core/variant/type_info.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant_utility.h"
#include "core/variant/native_ptr.h"
#include "core/variant/variant.h"
#include "core/templates/vmap.h"
#include "core/templates/simple_type.h"
#include "core/templates/paged_array.h"
#include "core/templates/hash_map.h"
#include "core/templates/cowdata.h"
#include "core/templates/safe_refcount.h"
#include "core/templates/pass_func.h"
#include "core/templates/hashfuncs.h"
#include "core/templates/ring_buffer.h"
#include "core/templates/rb_set.h"
#include "core/templates/search_array.h"
#include "core/templates/bin_sorted_array.h"
#include "core/templates/list.h"
#include "core/templates/safe_list.h"
#include "core/templates/lru.h"
#include "core/templates/sort_array.h"
#include "core/templates/rb_map.h"
#include "core/templates/paged_allocator.h"
#include "core/templates/self_list.h"
#include "core/templates/pair.h"
#include "core/templates/pooled_list.h"
#include "core/templates/local_vector.h"
#include "core/templates/command_queue_mt.h"
#include "core/templates/vset.h"
#include "core/templates/rid.h"
#include "core/templates/vector.h"
#include "core/templates/hash_set.h"
#include "core/templates/rid_owner.h"
#include "core/templates/oa_hash_map.h"
#include "core/os/time.h"
#include "core/os/semaphore.h"
#include "core/os/condition_variable.h"
#include "core/os/time_enums.h"
#include "core/os/midi_driver.h"
#include "core/os/rw_lock.h"
#include "core/os/os.h"
#include "core/os/thread_safe.h"
#include "core/os/thread.h"
#include "core/os/keyboard.h"
#include "core/os/memory.h"
#include "core/os/spin_lock.h"
#include "core/os/mutex.h"
#include "core/os/pool_allocator.h"
#include "core/os/main_loop.h"
#include "core/core_constants.h"
#include "core/core_bind.h"
#include "core/string/ucaps.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "core/string/string_builder.h"
#include "core/string/char_utils.h"
#include "core/string/node_path.h"
#include "core/string/translation.h"
#include "core/string/string_buffer.h"
#include "core/string/string_name.h"
#include "core/string/translation_po.h"
#include "core/string/optimized_translation.h"
#include "core/string/locales.h"
#include "core/error/error_macros.h"
#include "core/error/error_list.h"
#include "core/donors.gen.h"
#include "core/register_core_types.h"
