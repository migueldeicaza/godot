# WebGPU for Godot 4.6 — Comprehensive Research Document

> **Purpose**: This document compiles all architectural research, API analysis, and prior art needed by AI agents implementing WebGPU support in Godot 4.6. It is a living reference—update as new findings emerge.
>
> **Last Updated**: March 10, 2026

---

## Table of Contents

1. [Godot Rendering Architecture](#1-godot-rendering-architecture)
2. [RenderingDeviceDriver Interface (The Implementation Target)](#2-renderingdevicedriver-interface)
3. [Existing Driver Implementations (Reference Patterns)](#3-existing-driver-implementations)
4. [Driver Registration & Selection Flow](#4-driver-registration--selection-flow)
5. [Web Platform Current State](#5-web-platform-current-state)
6. [WebGPU API vs Vulkan — Critical Differences](#6-webgpu-api-vs-vulkan--critical-differences)
7. [SPIR-V to WGSL Shader Translation](#7-spir-v-to-wgsl-shader-translation)
8. [Emscripten WebGPU Integration](#8-emscripten-webgpu-integration)
9. [davnotdev/godot Fork Analysis](#9-davnotdevgodot-fork-analysis)
10. [Three.js WebGPU Renderer](#10-threejs-webgpu-renderer)
11. [Bevy Engine & wgpu](#11-bevy-engine--wgpu)
12. [Browser WebGPU Maturity (March 2026)](#12-browser-webgpu-maturity-march-2026)
13. [Key Challenges & Mitigations](#13-key-challenges--mitigations)
14. [File Inventory — What to Create, What to Modify](#14-file-inventory)

---

## 1. Godot Rendering Architecture

### Class Hierarchy

Godot 4.6 has a three-layer RenderingDevice abstraction:

```
RenderingDeviceCommons (Object)
  ├── RenderingDeviceDriver       ← Pure virtual driver interface (WHAT BACKENDS IMPLEMENT)
  ├── RenderingDevice             ← High-level API, resource management, render graph
  └── RenderingContextDriver      ← Per-platform context (instance, surfaces, device enumeration)
```

- **`RenderingDeviceCommons`** (`servers/rendering/rendering_device_commons.h`): Shared enums and structs — ~230 `DataFormat` values, `TextureType`, `TextureSamples`, `TextureUsageBits`, `SamplerFilter`, `ShaderStage`, `UniformType`, `RenderPrimitive`, `CompareOperator`, pipeline state structs, `ShaderReflection`, `Limit` enum, `Features` enum, etc.
- **`RenderingDeviceDriver`** (`servers/rendering/rendering_device_driver.h`): ~80+ pure virtual methods that each backend must implement. This is the primary implementation target.
- **`RenderingDevice`** (`servers/rendering/rendering_device.h`): The singleton high-level API that renderer_rd components call. It owns a `RenderingContextDriver*` and a `RenderingDeviceDriver*`.
- **`RenderingContextDriver`** (`servers/rendering/rendering_context_driver.h`): Factory pattern — enumerates GPUs, creates surfaces, and creates the `RenderingDeviceDriver` via `driver_create()`.
- **`RenderingDeviceGraph`** (`servers/rendering/rendering_device_graph.h`): Render graph that records commands, manages resource tracking, and emits driver calls. Operates between `RenderingDevice` and `RenderingDeviceDriver`. **Does not need modification for a new backend.**

### Renderer Components (renderer_rd/)

All of these call through the `RenderingDeviceDriver` abstraction and **do not need modification**:

| Path | Purpose |
|------|---------|
| `renderer_compositor_rd.{cpp,h}` | Top-level compositor |
| `renderer_canvas_render_rd.{cpp,h}` | 2D canvas rendering |
| `renderer_scene_render_rd.{cpp,h}` | 3D scene rendering base |
| `shader_rd.{cpp,h}` | Shader compilation/management |
| `cluster_builder_rd.{cpp,h}` | Light clustering |
| `forward_clustered/` | Forward+ renderer |
| `forward_mobile/` | Mobile renderer |
| `effects/` | Post-processing effects (~35 files) |
| `storage_rd/` | GPU resource storage (~20 files) |
| `shaders/` | ~100 GLSL shader source files |

### Shader Pipeline

1. **Build time**: `glsl_builders.py` reads `.glsl` files with `#[vertex]`, `#[fragment]`, `#[compute]` sections → generates `.glsl.gen.h` headers with GLSL source as C strings
2. **Runtime**: `ShaderRD` builds variant GLSL source strings → `RenderingDevice::shader_compile_spirv_from_source()` compiles GLSL → SPIR-V using **glslang** (thirdparty) → `shader_compile_binary_from_spirv()` creates a `RenderingShaderContainer` with reflection data (via SPIRV-Reflect)
3. **Driver-specific**: Each driver's `RenderingShaderContainer` subclass processes the SPIR-V into its native format (Vulkan stores SPIR-V, Metal cross-compiles to MSL via SPIRV-Cross, D3D12 converts via Mesa/NIR or dxc)

### Key Type Definitions

ID types (defined via `DEFINE_ID` macro in `rendering_device_driver.h`):
`BufferID`, `TextureID`, `SamplerID`, `VertexFormatID`, `CommandQueueID`, `CommandQueueFamilyID`, `CommandPoolID`, `CommandBufferID`, `SwapChainID`, `FramebufferID`, `ShaderID`, `UniformSetID`, `PipelineID`, `RenderPassID`, `QueryPoolID`, `FenceID`, `SemaphoreID`

---

## 2. RenderingDeviceDriver Interface

**File**: `servers/rendering/rendering_device_driver.h`

A new backend must implement **~80+ pure virtual methods**. Organized by category:

### Initialization
- `initialize(uint32_t p_device_index, uint32_t p_frame_count)`

### Buffers
- `buffer_create`, `buffer_set_texel_format`, `buffer_free`, `buffer_get_allocation_size`
- `buffer_map`, `buffer_unmap`, `buffer_persistent_map_advance`, `buffer_get_dynamic_offsets`
- `buffer_get_device_address`

### Textures
- `texture_create`, `texture_create_from_extension`, `texture_create_shared`, `texture_create_shared_from_slice`
- `texture_free`, `texture_get_allocation_size`, `texture_get_copyable_layout`
- `texture_get_data`, `texture_get_usages_supported_by_format`, `texture_can_make_shared_with_format`

### Samplers
- `sampler_create`, `sampler_free`, `sampler_is_format_supported_for_filter`

### Vertex Formats
- `vertex_format_create`, `vertex_format_free`

### Barriers / Synchronization
- `command_pipeline_barrier` → **No-op for WebGPU** (automatic tracking)

### Fences & Semaphores
- `fence_create`, `fence_wait`, `fence_free`
- `semaphore_create`, `semaphore_free`

### Command Buffers
- `command_queue_family_get`, `command_queue_create`, `command_queue_execute_and_present`, `command_queue_free`
- `command_pool_create`, `command_pool_reset`, `command_pool_free`
- `command_buffer_create`, `command_buffer_begin`, `command_buffer_begin_secondary`, `command_buffer_end`, `command_buffer_execute_secondary`

### Swap Chain
- `swap_chain_create`, `swap_chain_resize`, `swap_chain_acquire_framebuffer`, `swap_chain_get_render_pass`, `swap_chain_get_format`, `swap_chain_free`

### Framebuffers
- `framebuffer_create`, `framebuffer_free`

### Shaders
- `shader_create_from_container(const Ref<RenderingShaderContainer>&, const Vector<ImmutableSampler>&)`
- `shader_free`, `shader_destroy_modules`

### Uniform Sets
- `uniform_set_create`, `uniform_set_free`, `uniform_sets_get_dynamic_offsets`
- `command_uniform_set_prepare_for_use`

### Transfer Commands
- `command_clear_buffer`, `command_copy_buffer`, `command_copy_texture`, `command_resolve_texture`
- `command_clear_color_texture`, `command_clear_depth_stencil_texture`
- `command_copy_buffer_to_texture`, `command_copy_texture_to_buffer`

### Pipelines
- `render_pipeline_create`, `compute_pipeline_create`, `pipeline_free`
- `command_bind_push_constants`
- `pipeline_cache_create/free/query_size/serialize`

### Render Passes
- `render_pass_create`, `render_pass_free`

### Render Commands
- `command_begin_render_pass`, `command_end_render_pass`, `command_next_render_subpass`
- `command_render_set_viewport`, `command_render_set_scissor`, `command_render_clear_attachments`
- `command_bind_render_pipeline`, `command_bind_render_uniform_sets`
- `command_render_draw`, `command_render_draw_indexed`, `command_render_draw_indexed_indirect[_count]`, `command_render_draw_indirect[_count]`
- `command_render_bind_vertex_buffers`, `command_render_bind_index_buffer`
- `command_render_set_blend_constants`, `command_render_set_line_width`

### Compute Commands
- `command_bind_compute_pipeline`, `command_bind_compute_uniform_sets`
- `command_compute_dispatch`, `command_compute_dispatch_indirect`

### Queries / Timestamps
- `timestamp_query_pool_create/free/get_results`, `timestamp_query_result_to_time`
- `command_timestamp_query_pool_reset`, `command_timestamp_write`

### Debug Labels
- `command_begin_label`, `command_end_label`, `command_insert_breadcrumb`

### Capabilities / Limits
- `limit_get`, `has_feature`, `get_multiview_capabilities`, `get_capabilities`
- `get_api_name`, `get_api_version`, `get_pipeline_cache_uuid`
- `get_shader_container_format`

### Submission
- `begin_segment`, `end_segment`

---

## 3. Existing Driver Implementations

### Pattern: Each driver provides 3 classes

| Class | File Pattern | Purpose |
|-------|-------------|---------|
| `RenderingContextDriverXxx` | `rendering_context_driver_xxx.{h,cpp/mm}` | Enumerate devices, create surfaces, factory for driver |
| `RenderingDeviceDriverXxx` | `rendering_device_driver_xxx.{h,cpp/mm}` | Implement all ~80 pure virtual methods |
| `RenderingShaderContainerXxx` | `rendering_shader_container_xxx.{h,cpp/mm}` | Handle shader binary storage + cross-compilation |

### Vulkan Driver (`drivers/vulkan/`)
- `RenderingContextDriverVulkan` (208-line header)
- `RenderingDeviceDriverVulkan` (745-line header)
- `RenderingShaderContainerVulkan` — stores SPIR-V directly (+ optional SMOL-V compression)
- Closest API model to WebGPU conceptually, but with all the explicit Vulkan complexity

### Metal Driver (`drivers/metal/`) — **BEST REFERENCE FOR WEBGPU**
- `RenderingContextDriverMetal` (134-line header)
- `RenderingDeviceDriverMetal` (526-line header)
- `RenderingShaderContainerMetal` — cross-compiles SPIR-V → MSL via SPIRV-Cross
- Additional files:
  - `metal_objects.{h,mm}` — Internal objects (command buffers, shaders, render passes, pipelines)
  - `pixel_formats.{h,mm}` — Format mapping tables
  - `metal_device_properties.{h,mm}` — Device feature queries
  - `metal_device_profile.{cpp,h}` — Capability profiles

**Why Metal is the best reference**: Metal shares almost all of WebGPU's limitations:
- **No push constants** → emulated via buffer bindings (same needed for WebGPU)
- **No subpasses** → flattened into separate render passes (same for WebGPU)
- **Automatic synchronization** → barriers handled internally (same for WebGPU)
- **Similar shader model** → SPIR-V cross-compiled to target language (MSL vs WGSL)

### D3D12 Driver (`drivers/d3d12/`)
- `RenderingContextDriverD3D12`
- `RenderingDeviceDriverD3D12`
- `RenderingShaderContainerD3D12`
- Push constants emulated via root constants (`PUSH_CONSTANT_SIZE = 128`)
- Root signature: push constants always at root parameter index 0

### Metal Push Constant Emulation (Detailed — Use as WebGPU Template)

Metal stores push constant state in `MDCommandBuffer`:
```cpp
static constexpr uint32_t MAX_PUSH_CONSTANT_SIZE = 128;
uint8_t push_constant_data[MAX_PUSH_CONSTANT_SIZE];
uint32_t push_constant_data_len = 0;
uint32_t push_constant_binding = UINT32_MAX;  // Metal buffer binding slot
```

Flow:
1. `command_bind_push_constants` → copies data into `push_constant_data[]`, marks dirty
2. At draw time, `_render_set_dirty_state()` → uploads via Metal API: `[encoder setVertexBytes:... atIndex:push_constant_binding]` and `[encoder setFragmentBytes:... atIndex:push_constant_binding]`
3. For compute: `[compute.encoder setBytes:... atIndex:push_constant_binding]`
4. The binding slot is determined during SPIRV-Cross compilation and stored in shader metadata

### Metal Subpass Flattening (Detailed — Use as WebGPU Template)

`MDSubpass` struct holds per-subpass attachment references. On `command_next_render_subpass`:
1. End current render encoder
2. Increment `current_subpass`
3. Build new render pass descriptor from next subpass's attachments
4. Create new render command encoder
5. Mark all state dirty

For WebGPU: identical approach — each Vulkan subpass becomes a separate `GPURenderPassEncoder`.

---

## 4. Driver Registration & Selection Flow

### Build System Defines

Each platform's `detect.py` sets C++ defines that control which drivers are compiled:

| Platform | Defines |
|----------|---------|
| Linux/Windows | `VULKAN_ENABLED` + `RD_ENABLED` |
| macOS | `METAL_ENABLED` + `RD_ENABLED` (or `VULKAN_ENABLED` + `RD_ENABLED`) |
| Windows (D3D12) | `D3D12_ENABLED` + `RD_ENABLED` |
| **Web (current)** | **`GLES3_ENABLED` only — NO `RD_ENABLED`** |
| **Web (target)** | **`WEBGPU_ENABLED` + `RD_ENABLED` + `GLES3_ENABLED`** |

### Instantiation Flow

1. **Display server creation** (`servers/display/display_server.cpp` ~line 2009):
   ```cpp
   #if defined(VULKAN_ENABLED)
       rcd = memnew(RenderingContextDriverVulkan);
   #endif
   #ifdef D3D12_ENABLED
       if (rcd == nullptr) rcd = memnew(RenderingContextDriverD3D12);
   #endif
   #ifdef METAL_ENABLED
       if (rcd == nullptr) rcd = memnew(RenderingContextDriverMetal);
   #endif
   ```

2. **RenderingDevice initialization** (`servers/rendering/rendering_device.cpp` ~line 6940):
   ```cpp
   Error RenderingDevice::initialize(RenderingContextDriver *p_context, DisplayServer::WindowID p_main_window) {
       // Get surface from display server window
       // driver = context->driver_create()
       // Select best GPU device
       // driver->initialize(device_index, frame_count)
       // Create command queues, pools, fences, semaphores, staging buffers
   }
   ```

### Web Rendering Method Restriction

`main/main.cpp` line 2571:
```cpp
GLOBAL_DEF_RST_BASIC(PropertyInfo(Variant::STRING,
    "rendering/renderer/rendering_method.web", PROPERTY_HINT_ENUM,
    "gl_compatibility"), "gl_compatibility");
// Comment: "This is a bit of a hack until we have WebGPU support."
```

This `.web` suffix override forces web exports to `gl_compatibility` only. To enable WebGPU: change hint to `"forward_plus,mobile,gl_compatibility"` and add `"webgpu"` to valid drivers for `forward_plus`/`mobile`.

### Rendering Method → Driver Mapping

```
forward_plus  → requires RD_ENABLED → uses RendererCompositorRD
mobile        → requires RD_ENABLED → uses RendererCompositorRD
gl_compatibility → uses RasterizerGLES3 (separate path, no RD)
```

---

## 5. Web Platform Current State

### File Inventory (`platform/web/`)

| File | Purpose |
|------|---------|
| `detect.py` (348 lines) | Build configuration — compiler flags, defines |
| `SCsub` (138 lines) | Build script |
| `display_server_web.{h,cpp}` | Display server — WebGL context, input, window management |
| `os_web.{h,cpp}` | OS abstraction |
| `web_main.cpp` (179 lines) | Entry point |
| `platform_gl.h` | GLES3 defines, includes `godot_webgl2.h` |
| `godot_webgl2.h` | WebGL2 extension declarations |
| `godot_js.h` | JS interop API |
| `js/` | JavaScript libraries for browser integration |

### Current WebGL Initialization

In `DisplayServerWeb` constructor (`display_server_web.cpp` line 1130):
```cpp
#ifdef GLES3_ENABLED
    EmscriptenWebGLContextAttributes attributes;
    attributes.majorVersion = 2;
    webgl_ctx = emscripten_webgl_create_context(canvas_id, &attributes);
    RasterizerGLES3::make_current(false);
#else
    RasterizerDummy::make_current();
#endif
```

The display server reports only `"opengl3"` as available:
```cpp
Vector<String> DisplayServerWeb::get_rendering_drivers_func() {
    Vector<String> drivers;
#ifdef GLES3_ENABLED
    drivers.push_back("opengl3");
#endif
    return drivers;
}
```

### Build Configuration (`detect.py`)

- Compiler: `emcc` / `em++` (Emscripten, minimum 4.0.0)
- Architecture: `wasm32`
- GLES3 enabled: `env.AppendUnique(CPPDEFINES=["GLES3_ENABLED"])` + `-sMAX_WEBGL_VERSION=2` + `-sOFFSCREEN_FRAMEBUFFER=1`
- Key defines: `WEB_ENABLED`, `UNIX_ENABLED`
- `"vulkan": False` hardcoded in `get_flags()`

### Web Entry Point (`web_main.cpp`)

1. `godot_web_main(argc, argv)` — entry point (EMSCRIPTEN_KEEPALIVE)
2. Creates `OS_Web` instance
3. Calls `Main::setup(...)` → creates display server → creates rendering device
4. Calls `Main::start()` → `os->get_main_loop()->initialize()`
5. `emscripten_set_main_loop(main_loop_callback, -1, false)` — browser animation frame loop
6. `main_loop_callback()` calls `os->main_loop_iterate()` each frame
7. On exit: cancels main loop, async cleanup

---

## 6. WebGPU API vs Vulkan — Critical Differences

### Push Constants (BIGGEST CHALLENGE)

| | Vulkan | WebGPU |
|---|--------|--------|
| Support | Native, ~128–256 bytes | **None** |
| Overhead | Nearly zero | N/A |

**Godot impact**:
- **67 out of 100** shader files use `layout(push_constant, std430)`
- **136 C++ call sites** use `draw_list_set_push_constant` or `compute_list_set_push_constant`
- **~1,216 total references** to push constants in `renderer_rd/`
- Push constants change **per draw call** in many cases (e.g., Forward+ scene draw loop)

**Emulation strategy** (follow Metal driver pattern):
1. Shader compiler rewrites `push_constant` block → uniform buffer at dedicated binding
2. Driver stores push constant data in a CPU-side buffer (128 bytes max)
3. Before each draw/dispatch, upload data via `wgpuQueueWriteBuffer()` to a small ring buffer
4. Bind the buffer at the assigned binding slot
5. Use dynamic offsets where possible for efficiency

### Descriptor Sets vs Bind Groups

| | Vulkan | WebGPU |
|---|--------|--------|
| Max sets/groups | `MAX_UNIFORM_SETS = 16` (Godot) | **4 bind groups** (0–3) |
| Rebinding granularity | Per set | Per group |

**Godot impact analysis**:

Shader set index usage in `servers/rendering/renderer_rd/shaders/`:
| Set Index | Occurrences |
|-----------|------------|
| `set = 0` | 218 |
| `set = 1` | 120 |
| `set = 2` | 25 |
| `set = 3` | 10 |

**Maximum set index used in any shader = 3.** No shader uses set 4+.

This means WebGPU's 4 bind group limit is sufficient with **direct 1:1 mapping** (Vulkan set N → WebGPU bind group N). No remapping or packing needed.

### Subpasses

| | Vulkan | WebGPU |
|---|--------|--------|
| Support | Native subpasses within render pass | **None** |
| Input attachments | Supported | Must use texture reads |

**Godot impact**: 48 subpass references in `renderer_rd/` C++ code. `render_pass_create()` takes vectors of `Subpass` and `SubpassDependency` structs.

**Solution**: Follow Metal driver's flattening approach — each Vulkan subpass becomes a separate `GPURenderPassEncoder`. Input attachments become texture reads between render passes. This is proven to work in Godot's Metal driver.

### Synchronization

| | Vulkan | WebGPU |
|---|--------|--------|
| Barriers | Explicit (22 pipeline stage bits, 18 access bits) | **Automatic** |
| Layout transitions | Explicit | **Automatic** |
| Fences | VkFence | `onSubmittedWorkDone()` callback |
| Semaphores | VkSemaphore | **Not needed** (single queue) |

**Impact**: This is a **simplification**. `command_pipeline_barrier()` → no-op. Texture layout tracking → unnecessary. Fences → `onSubmittedWorkDone()` or polling. Semaphores → trivial (WebGPU is single-queue in browsers).

### Command Buffer Model

| | Vulkan | WebGPU |
|---|--------|--------|
| Secondary command buffers | Supported | **Not supported** |
| Multi-threaded recording | Native | **Not supported** (single-threaded encoding) |
| Submit | `vkQueueSubmit` with semaphore sync | `queue.submit([commandBuffer])` |

**Impact**: `command_buffer_begin_secondary` → no-op or sequential encoder. Multi-threaded recording benefits are lost on WebGPU, but this is acceptable for web targets.

### Texture Formats

| | Vulkan | WebGPU |
|---|--------|--------|
| Format count | Hundreds | ~40 |
| 3-component formats | Supported | **Not supported** (use RGBA) |
| Compressed BC | Supported | Optional feature |
| Compressed ETC2/ASTC | Supported | Optional features |

The WebGPU driver must map Godot's `DataFormat` enum to `WGPUTextureFormat`. 3-component formats (R8G8B8, etc.) must be mapped to 4-component equivalents.

### Buffer Mapping

| | Vulkan | WebGPU |
|---|--------|--------|
| Map | Synchronous (`vkMapMemory`) | **Asynchronous** (`buffer.mapAsync()`) |
| Persistent mapping | Supported | **Not supported** |
| Immediate upload | Via mapped memory | `queue.writeBuffer()` |

**Impact**: Use `wgpuQueueWriteBuffer()` for most uploads (staging). Async mapping needed only for GPU readback (occlusion queries, compute results).

### WebGPU Limits Summary

| Limit | WebGPU Default | Typical Vulkan | Impact |
|-------|---------------|----------------|--------|
| Max bind groups | **4** | 4–32 | OK (Godot uses 0–3) |
| Max uniform buffers / stage | **12** | 12–72 | May be tight |
| Max storage buffers / stage | **8** | 16+ | May need adjustment |
| Max sampled textures / stage | **16** | 16+ | Tight for Forward+ |
| Max samplers / stage | **16** | 16+ | Tight for Forward+ |
| Max texture size 2D | **8192** (common) | 16384+ | Acceptable |
| Max uniform buffer size | **64 KB** | 64 KB+ | OK |
| Max push constant size | **0** | 128–256 | Must emulate |

### Features NOT in WebGPU

- Geometry shaders → Godot doesn't use them ✓
- Tessellation shaders → Godot doesn't use them ✓
- Bindless textures → Godot doesn't require them ✓
- Multi-draw indirect → Not in base spec (can work around)
- Float16 shader types → Optional feature
- Timestamp queries → Optional feature

---

## 7. SPIR-V to WGSL Shader Translation

### Option 1: Tint (Dawn's Shader Compiler) — RECOMMENDED

- **What**: Google's shader compiler from the Dawn project. SPIR-V ↔ WGSL translation.
- **License**: BSD 3-Clause (compatible with Godot's MIT)
- **Quality**: Highest — it IS Chrome's implementation
- **Standalone use**: Yes, builds as `libtint` with C API
- **WASM compilation**: Can be compiled to WASM with Emscripten for runtime translation (~2-5MB binary size impact)
- **Recommended usage**: Offline/export-time translation (avoid WASM binary bloat)
- **Approach**: At export time, run all SPIR-V blobs through Tint CLI → produce WGSL strings → ship with web export

### Option 2: ~~Naga~~ (Rust, wgpu's shader compiler) — NOT USED

- **What**: Mozilla/wgpu's shader compiler. SPIR-V → WGSL, GLSL → WGSL, etc.
- **Status**: Was initially used but replaced by Tint (Option 1) for better correctness and no Rust dependency
- **Cons**: Rust dependency, fewer SPIR-V features than Tint, no built-in coordinate space adjustment

### Option 3: SPIRV-Cross — NOT VIABLE

- **Status**: Already in Godot at `thirdparty/spirv-cross/`
- **WGSL support**: **NONE.** Only supports GLSL, HLSL, MSL output. Khronos has stated WGSL is not planned.
- **Verdict**: Cannot be used for WGSL generation

### Recommended Approach

**Two-stage pipeline**:

1. **Export time (offline)**: Use Tint CLI to convert all SPIR-V shader blobs → WGSL strings. Store WGSL alongside or instead of SPIR-V in the export.
2. **Runtime (browser)**: `RenderingShaderContainerWebGPU` loads pre-compiled WGSL and passes it directly to `wgpuDeviceCreateShaderModule()` with `WGPUShaderModuleWGSLDescriptor`.

**For user custom shaders**: Either embed a minimal Tint build in the WASM (accepting ~2-5MB size increase) OR require users to provide WGSL for custom shaders on web.

### Push Constant Rewriting in Shaders

The shader translation must handle `layout(push_constant, std430) uniform PushConstants { ... }` blocks:

1. Rewrite to a regular uniform buffer: `@group(3) @binding(0) var<uniform> push_constants: PushConstants;`
2. Reserve bind group 3 binding 0 (or similar) for this purpose
3. The driver creates a small uniform buffer per pipeline for push constant data
4. On `command_bind_push_constants`, write data to this buffer

This is analogous to what SPIRV-Cross does for Metal — it has explicit options for remapping push constants to buffer bindings.

---

## 8. Emscripten WebGPU Integration

### Headers and APIs

- **`<emscripten/html5_webgpu.h>`**: Provides `emscripten_webgpu_get_device()` — retrieves `WGPUDevice` from browser
- **`<webgpu/webgpu.h>`**: Standard C header (Dawn-compatible). Defines `WGPUDevice`, `WGPUQueue`, `WGPURenderPipeline`, etc.
- **`<webgpu/webgpu_cpp.h>`**: Optional C++ wrappers

### Linker Flags

```
-sUSE_WEBGPU=1          # Critical — links browser WebGPU bindings
-sASYNCIFY              # May be needed for async init (adds ~10-20% binary size)
-sALLOW_MEMORY_GROWTH=1 # Already used by Godot web builds
```

### How Emscripten WebGPU Works

1. Emscripten implements `webgpu.h` C API via JavaScript bindings generated at link time
2. C++ calls like `wgpuDeviceCreateRenderPipeline(...)` → JS glue → `GPUDevice.createRenderPipeline(...)`
3. GPU objects live in JavaScript; C++ holds integer handles via a handle table
4. **No native Dawn or wgpu-native compiled into WASM** — the browser provides the GPU implementation
5. This is identical to how Emscripten handles WebGL (via `-sUSE_WEBGL2=1`)

### Emscripten Versions

- **3.1.50+** (late 2023): Reasonably stable WebGPU bindings
- **4.0+** (2025): Mature, spec-aligned. **Godot 4.6 already requires Emscripten 4.0.0 minimum** (see `detect.py` line 97)

### Async Initialization — CRITICAL CHALLENGE

WebGPU device creation is **asynchronous** (Promises in JS):
1. `navigator.gpu.requestAdapter()` → Promise
2. `adapter.requestDevice()` → Promise

In Emscripten C code:
```c
wgpuInstanceRequestAdapter(instance, &options, callback, userdata);  // callback-based
wgpuAdapterRequestDevice(adapter, &descriptor, callback, userdata);  // callback-based
```

**Problem**: Godot's `Main::setup()` is synchronous — display server creation → rendering device creation → all before main loop starts.

**Solutions** (in order of preference):

1. **JS pre-init** (SIMPLEST): Have the HTML shell/JS glue request the WebGPU device BEFORE loading the WASM module. Pass the device handle to C++ via `emscripten_webgpu_get_device()`. This function returns the device synchronously IF the JS side has already acquired it.

2. **Multi-stage init**: Split initialization so adapter/device request happens in a pre-init callback, then `Main::setup()` runs after device is ready.

3. **ASYNCIFY**: Compile with `-sASYNCIFY` to allow C code to "pause" during async JS calls. This adds binary size and complexity but requires the least restructuring of Godot's init code.

### JS Shell Modifications

The HTML shell (`misc/dist/html/` or export templates) would need:
```javascript
// Before WASM init
const adapter = await navigator.gpu.requestAdapter();
const device = await adapter.requestDevice({
    requiredFeatures: ['texture-compression-bc'], // as needed
    requiredLimits: { maxStorageBuffersPerShaderStage: 10 }, // as needed
});
// Pass to Emscripten module
Module.preinitializedWebGPUDevice = device;
```

---

## 9. davnotdev/godot Fork Analysis

### Approach

- Used **wgpu-native** (C API wrapper around wgpu-rs) with Dawn as alternative backend
- Implemented `RenderingDeviceDriverWebGPU` mirroring the Vulkan driver pattern
- Targeted **native desktop first** (not browser/WASM), using wgpu-native as standalone library
- Browser/WASM support marked "heavy WIP" and not functional

### What Worked

- **Native 2D rendering**: 24/26 official 2D samples working on desktop (via wgpu-native)
- Basic resource creation (buffers, textures, pipelines)
- Basic command buffer recording and submission
- Some shader translation (via wgpu's built-in shader compiler)

### What Didn't Work

- **Browser/WASM builds**: Not functional — the core blocker
- 3D rendering: Forward+ not ported
- 2/26 2D samples failing (likely push-constant or subpass related)
- Compute shaders: unclear status

### Lessons Learned

1. **Desktop-first was suboptimal for a web target**: wgpu-native gives native validation but doesn't solve Emscripten integration
2. **Push constant emulation is mandatory and non-trivial**: Can't skip this — 67% of shaders use them
3. **The RenderingDeviceDriver abstraction is the correct integration point**: Their driver subclass approach is right
4. **Shader translation must be solved early**: It's foundational
5. **The Metal driver is a better reference than Vulkan**: Metal shares WebGPU's limitations, Vulkan doesn't

---

## 10. Three.js WebGPU Renderer

### Architecture

- Separate `WebGPURenderer` class alongside `WebGLRenderer`
- Backend abstraction: `WebGLBackend` / `WebGPUBackend` — similar to Godot's `RenderingDeviceDriver`
- Pipeline caching and bind group management per-backend

### Shader Approach

Three.js does **NOT** translate GLSL to WGSL. Instead:
- **TSL (Three Shading Language)**: JS-based node graph shader system
- TSL compiles to GLSL (for WebGL) or WGSL (for WebGPU) at runtime
- All WGSL generation happens in JavaScript
- This avoids GLSL→WGSL translation entirely

**Lesson for Godot**: Godot can't easily adopt this approach (GLSL pipeline is deeply ingrained). SPIR-V → WGSL translation is the practical path.

### Performance Characteristics

- **Draw call overhead**: 2–5× lower than WebGL for draw-call-heavy scenes
- **Fill-rate-bound**: 1.5–2× improvement
- **Compute shaders**: Available (GPU particles, post-processing, GPGPU)
- **Startup**: Slightly slower (async pipeline compilation), but pipelines are cached
- **Key win**: Command buffer model amortizes validation costs

### Key Design Decisions

1. **Async pipeline creation with fallback materials**: Show fallback while compiling — avoids frame hitches
2. **Aggressive bind group caching**: Reusing bind groups across frames is critical
3. **Graceful WebGL fallback**: Auto-detection + fallback when WebGPU unavailable
4. **WebGPU-first for new features**: Compute-based features as WebGPU-only incentivize adoption

---

## 11. Bevy Engine & wgpu

### Architecture

- Uses **wgpu** (Rust crate) as sole rendering backend for ALL platforms
- One unified API — wgpu selects Vulkan/D3D12/Metal (native) or WebGPU/WebGL2 (web) automatically
- No separate per-platform drivers — wgpu handles everything

### Web Compilation

- Bevy + wgpu compile to WASM via `wasm32-unknown-unknown` target (Rust native, no Emscripten)
- wgpu uses `web-sys` (Rust WebAPI bindings) to call `navigator.gpu` directly
- Shader compiler compiled into WASM binary (~5-10MB compressed total)
- Async init via Rust `async/await` + `wasm-bindgen-futures`

### Performance

- Native: ~5-10% overhead vs raw Vulkan
- Web: 60-80% of native performance for GPU-bound workloads
- Lower CPU overhead than WebGL-based approaches

### Key Lesson for Godot

Bevy's approach works because they committed to wgpu from day one. Godot has existing Vulkan/Metal/D3D12 drivers, so the practical path is **adding a WebGPU driver alongside them**, not replacing the stack. This is also the approach Godot core team expects (proposal #6646).

---

## 12. Browser WebGPU Maturity (March 2026)

### Browser Support

| Browser | Status | Backend | Since |
|---------|--------|---------|-------|
| Chrome / Edge | Full support, production-ready | Dawn | Chrome 113 (April 2023) |
| Firefox | Full support, enabled by default | wgpu | Firefox 141+ (2024) |
| Safari | Supported, enabled by default | Metal-based | Safari 18 (Sept 2024) |

All major browsers have had WebGPU for **1-3 years** by March 2026. The API is a W3C Recommendation.

### Known Feature Variations

- **Timestamp queries**: Optional, not universal
- **Float32-filterable textures**: Optional
- **Shader f16**: Optional, not widely available
- **Texture compression (BC/ETC2/ASTC)**: Optional features, need to be requested
- **Multi-draw indirect**: Not in base spec

### Typical Device Limits

| Limit | Chrome Desktop | Safari macOS | Firefox |
|-------|---------------|-------------|---------|
| Max texture size 2D | 8192–16384 | 8192–16384 | 8192–16384 |
| Max bind groups | 4 | 4 | 4 |
| Max samplers / stage | 16 | 16 | 16 |
| Max sampled textures / stage | 16 | 16 | 16 |
| Max storage buffers / stage | 8–10 | 8 | 8–10 |
| Max uniform buffer size | 64KB+ | 64KB | 64KB |
| Max color attachments | 8 | 8 | 8 |

---

## 13. Key Challenges & Mitigations

### Challenge 1: Push Constant Emulation (CRITICAL)

**Scale**: 67/100 shaders, 136 C++ call sites, per-draw-call frequency
**Mitigation**: Follow Metal driver pattern. Shader compiler rewrites push_constant → uniform buffer at reserved binding. Driver maintains small ring buffer. Upload via `wgpuQueueWriteBuffer()` before each draw/dispatch.
**Risk**: Performance overhead from frequent small buffer writes. Mitigate with dynamic offsets and ring buffer allocation.

### Challenge 2: Async WebGPU Initialization

**Problem**: `Main::setup()` is synchronous; WebGPU device creation is async
**Mitigation**: JS pre-init — HTML shell requests device before WASM loads, then `emscripten_webgpu_get_device()` retrieves it synchronously
**Risk**: Low — this is a well-established pattern for Emscripten WebGPU apps

### Challenge 3: SPIR-V → WGSL Translation

**Problem**: All shaders are GLSL → SPIR-V. WebGPU needs WGSL.
**Mitigation**: Use Tint (Dawn's compiler) as an offline export-time tool. Ship WGSL with web exports.
**Risk**: Translation edge cases, especially around push constant rewriting, descriptor set mapping, and GLSL extensions. Test early and thoroughly.

### Challenge 4: Subpass Flattening

**Problem**: Vulkan subpasses don't exist in WebGPU
**Mitigation**: Follow Metal driver's approach — proven in Godot codebase already
**Risk**: Low — Metal driver demonstrates this works correctly

### Challenge 5: Sampler/Texture Limits

**Problem**: WebGPU limits (16 samplers/textures per stage) may be tight for Forward+
**Mitigation**: Audit Forward+ shader resource usage. May need to combine samplers or reduce max shadow cascades on web.
**Risk**: Medium — may require renderer_rd adjustments for web

### Challenge 6: Binary Size

**Problem**: Adding RD_ENABLED to web builds increases WASM binary size significantly (the entire renderer_rd pipeline is compiled in)
**Mitigation**: Keep GLES3 fallback. Use compression (Brotli/gzip). WebGPU path can be an opt-in export option.
**Risk**: Medium — acceptable tradeoff for massive performance improvement

---

## 14. File Inventory

### Files to CREATE

| File | Purpose | Reference |
|------|---------|-----------|
| `drivers/webgpu/rendering_context_driver_webgpu.h` | Context/device enumeration | `drivers/metal/rendering_context_driver_metal.h` |
| `drivers/webgpu/rendering_context_driver_webgpu.cpp` | Context implementation | `drivers/metal/rendering_context_driver_metal.mm` |
| `drivers/webgpu/rendering_device_driver_webgpu.h` | Main RDD header (~500+ lines) | `drivers/metal/rendering_device_driver_metal.h` |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | Main RDD implementation (~5000+ lines) | `drivers/metal/rendering_device_driver_metal.mm` |
| `drivers/webgpu/rendering_shader_container_webgpu.h` | Shader container (SPIR-V → WGSL) | `drivers/metal/rendering_shader_container_metal.h` |
| `drivers/webgpu/rendering_shader_container_webgpu.cpp` | Shader container implementation | `drivers/metal/rendering_shader_container_metal.mm` |
| `drivers/webgpu/webgpu_objects.h` | Internal objects (cmd buffers, shaders, render passes) | `drivers/metal/metal_objects.h` |
| `drivers/webgpu/webgpu_objects.cpp` | Internal objects implementation | `drivers/metal/metal_objects.mm` |
| `drivers/webgpu/pixel_formats_webgpu.h` | DataFormat → WGPUTextureFormat mapping | `drivers/metal/pixel_formats.h` |
| `drivers/webgpu/pixel_formats_webgpu.cpp` | Format mapping implementation | `drivers/metal/pixel_formats.mm` |
| `drivers/webgpu/SCsub` | Build script | `drivers/metal/SCsub` |

### Files to MODIFY

| File | Change | Why |
|------|--------|-----|
| `platform/web/detect.py` | Add `WEBGPU_ENABLED` + `RD_ENABLED` defines, `-sUSE_WEBGPU=1` flag | Enable WebGPU compilation |
| `platform/web/display_server_web.h` | Add WebGPU device/surface members | WebGPU context management |
| `platform/web/display_server_web.cpp` | Add WebGPU init path, report `"webgpu"` driver | Initialize WebGPU alongside/instead of WebGL |
| `main/main.cpp` | Update web rendering method hint, add WebGPU driver | Unlock Forward+/Mobile for web |
| `servers/display/display_server.cpp` | Add `#ifdef WEBGPU_ENABLED` context driver creation | Wire up WebGPU context driver |
| `SConstruct` | Add `webgpu` build option | Top-level build configuration |
| `drivers/SCsub` | Include `webgpu/` subdirectory when enabled | Build system |
| `platform/web/js/` | Modify JS glue for WebGPU device pre-init | Async device creation |
| `misc/dist/html/` | Update HTML shell template | WebGPU canvas setup |

### Files that do NOT need modification

- Everything in `servers/rendering/renderer_rd/` — works through RDD abstraction
- Everything in `drivers/gles3/` — separate renderer path, untouched
- Everything in `core/` — no rendering-specific changes
- Everything in `scene/` — uses renderer APIs unchanged
- Everything in `editor/` — uses renderer APIs unchanged

---

## Appendix A: WebGPU C API Quick Reference

Key types and functions the driver will use extensively:

```c
// Device & Queue
WGPUDevice device;
WGPUQueue queue = wgpuDeviceGetQueue(device);

// Buffers
WGPUBuffer buf = wgpuDeviceCreateBuffer(device, &descriptor);
wgpuQueueWriteBuffer(queue, buf, offset, data, size);

// Textures
WGPUTexture tex = wgpuDeviceCreateTexture(device, &descriptor);
WGPUTextureView view = wgpuTextureCreateView(tex, &viewDescriptor);
wgpuQueueWriteTexture(queue, &destination, data, size, &dataLayout, &writeSize);

// Samplers
WGPUSampler sampler = wgpuDeviceCreateSampler(device, &descriptor);

// Shaders
WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &descriptor);
// descriptor.nextInChain = (WGPUChainedStruct*)&wgslDescriptor;

// Bind Groups (= Uniform Sets)
WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device, &descriptor);
WGPUBindGroup group = wgpuDeviceCreateBindGroup(device, &descriptor);

// Pipeline Layout
WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &descriptor);

// Render Pipeline
WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);

// Command Encoding
WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &descriptor);
WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
wgpuRenderPassEncoderSetPipeline(pass, pipeline);
wgpuRenderPassEncoderSetBindGroup(pass, groupIndex, group, dynamicOffsetCount, dynamicOffsets);
wgpuRenderPassEncoderSetVertexBuffer(pass, slot, buffer, offset, size);
wgpuRenderPassEncoderSetIndexBuffer(pass, buffer, format, offset, size);
wgpuRenderPassEncoderDrawIndexed(pass, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
wgpuRenderPassEncoderEnd(pass);

// Submit
WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &descriptor);
wgpuQueueSubmit(queue, 1, &cmdBuf);

// Surface / Swap Chain
WGPUSurface surface = /* from canvas */;
wgpuSurfaceConfigure(surface, &config);
WGPUSurfaceTexture surfaceTexture;
wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
wgpuSurfacePresent(surface);
```

## Appendix B: DataFormat → WGPUTextureFormat Mapping (Partial)

| Godot DataFormat | WGPUTextureFormat |
|-----------------|-------------------|
| `DATA_FORMAT_R8_UNORM` | `WGPUTextureFormat_R8Unorm` |
| `DATA_FORMAT_R8G8_UNORM` | `WGPUTextureFormat_RG8Unorm` |
| `DATA_FORMAT_R8G8B8A8_UNORM` | `WGPUTextureFormat_RGBA8Unorm` |
| `DATA_FORMAT_R8G8B8A8_SRGB` | `WGPUTextureFormat_RGBA8UnormSrgb` |
| `DATA_FORMAT_B8G8R8A8_UNORM` | `WGPUTextureFormat_BGRA8Unorm` |
| `DATA_FORMAT_B8G8R8A8_SRGB` | `WGPUTextureFormat_BGRA8UnormSrgb` |
| `DATA_FORMAT_R16_SFLOAT` | `WGPUTextureFormat_R16Float` |
| `DATA_FORMAT_R16G16_SFLOAT` | `WGPUTextureFormat_RG16Float` |
| `DATA_FORMAT_R16G16B16A16_SFLOAT` | `WGPUTextureFormat_RGBA16Float` |
| `DATA_FORMAT_R32_SFLOAT` | `WGPUTextureFormat_R32Float` |
| `DATA_FORMAT_R32G32_SFLOAT` | `WGPUTextureFormat_RG32Float` |
| `DATA_FORMAT_R32G32B32A32_SFLOAT` | `WGPUTextureFormat_RGBA32Float` |
| `DATA_FORMAT_D16_UNORM` | `WGPUTextureFormat_Depth16Unorm` |
| `DATA_FORMAT_D24_UNORM_S8_UINT` | `WGPUTextureFormat_Depth24PlusStencil8` |
| `DATA_FORMAT_D32_SFLOAT` | `WGPUTextureFormat_Depth32Float` |
| `DATA_FORMAT_D32_SFLOAT_S8_UINT` | `WGPUTextureFormat_Depth32FloatStencil8` |
| `DATA_FORMAT_R8G8B8_UNORM` | **UNSUPPORTED** (must convert to RGBA) |
| `DATA_FORMAT_BC1_RGB_UNORM_BLOCK` | `WGPUTextureFormat_BC1RGBAUnorm` |
| `DATA_FORMAT_BC3_UNORM_BLOCK` | `WGPUTextureFormat_BC3RGBAUnorm` |
| `DATA_FORMAT_BC7_UNORM_BLOCK` | `WGPUTextureFormat_BC7RGBAUnorm` |
| `DATA_FORMAT_ETC2_R8G8B8_UNORM_BLOCK` | `WGPUTextureFormat_ETC2RGB8Unorm` |
| `DATA_FORMAT_ASTC_4x4_UNORM_BLOCK` | `WGPUTextureFormat_ASTC4x4Unorm` |

## Appendix C: Godot Limit Constants → WebGPU Values

| Godot Limit | Constant Name | WebGPU Value to Report |
|------------|---------------|----------------------|
| Max bound uniform sets | `LIMIT_MAX_BOUND_UNIFORM_SETS` | 4 |
| Max textures per uniform set | `LIMIT_MAX_TEXTURES_PER_UNIFORM_SET` | 16 |
| Max samplers per uniform set | `LIMIT_MAX_SAMPLERS_PER_UNIFORM_SET` | 16 |
| Max uniform buffer size | `LIMIT_MAX_UNIFORM_BUFFER_SIZE` | 65536 (64KB) |
| Max push constant size | `LIMIT_MAX_PUSH_CONSTANT_SIZE` | 128 (emulated) |
| Max texture size 2D | `LIMIT_MAX_TEXTURE_SIZE_2D` | 8192 |
| Max texture size 3D | `LIMIT_MAX_TEXTURE_SIZE_3D` | 2048 |
| Max compute workgroup count X | `LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X` | 65535 |
| Max compute workgroup size X | `LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_X` | 256 |
