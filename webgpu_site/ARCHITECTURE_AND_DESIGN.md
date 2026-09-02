# Godot WebGPU Backend — Architecture & Design

## Executive Summary

The Godot WebGPU backend is a complete implementation of Godot's `RenderingDeviceDriver` interface targeting the browser's WebGPU API. It enables Godot 4.6 games to run in the browser with the Forward Mobile renderer at performance parity with native builds, despite WebGPU's fundamentally different execution model.

The implementation spans ~11,000 lines of driver-specific code across 8 files under `drivers/webgpu/`, plus ~3,000 lines of C++ SPIR-V preprocessing and Tint (Google's Dawn/Chrome shader compiler) linked directly into the engine WASM, ~1,200 lines of rendering server modifications, and ~400 lines of platform/web integration. It achieves functional parity with the Mobile renderer path while working within WebGPU's constraints: no push constants, no combined image-samplers, async-only buffer mapping, no explicit barriers, and limited format support.

---

## 1. High-Level Architecture

### System Diagram

```
+---------------------------------------------------------------------+
|                     Godot Engine 4.6.2                               |
|                                                                       |
|   +---------------------------------------------------------------+  |
|   |          RenderingDevice (Abstract Interface)                  |  |
|   |   servers/rendering/rendering_device_driver.h                  |  |
|   +---------------------------------------------------------------+  |
|          |                    |                    |                   |
|   +------+------+    +-------+------+    +--------+-------+          |
|   | VulkanDriver|    | MetalDriver  |    | WebGPU Driver  |          |
|   | (desktop)   |    | (Apple)      |    | (THIS IMPL)    |          |
|   +-------------+    +--------------+    +--------+-------+          |
|                                                   |                   |
+---------------------------------------------------------------------+
                                                    |
+---------------------------------------------------------------------+
|                    WebGPU Driver Layer                                |
|                                                                       |
|   +---------------------------+  +------------------------------+    |
|   | RenderingContextDriver    |  | RenderingDeviceDriver        |    |
|   | WebGPU (context/device)   |  | WebGPU (7733 LOC impl)       |    |
|   |                           |  |                              |    |
|   | - WGPUInstance             |  | - Buffer management          |    |
|   | - WGPUAdapter             |  | - Texture management         |    |
|   | - WGPUDevice (from JS)    |  | - Shader creation (SPIR-V)   |    |
|   | - WGPUQueue               |  | - Pipeline creation          |    |
|   | - Surface management      |  | - Command recording          |    |
|   +---------------------------+  | - Push constant emulation    |    |
|                                  | - Bind group management      |    |
|   +---------------------------+  | - Swap chain management      |    |
|   | RenderingShaderContainer  |  +------------------------------+    |
|   | WebGPU (SPIR-V storage)   |                                      |
|   +---------------------------+  +------------------------------+    |
|                                  | spirv_preprocess.cpp (C++)   |    |
|   +---------------------------+  | - 12 SPIR-V binary passes    |    |
|   | webgpu_objects.h          |  | - Push constant rewrite      |    |
|   | - WGBuffer, WGTexture     |  | - Combined sampler split     |    |
|   | - WGShader, WGPipeline    |  | + Tint (SPIR-V → WGSL)      |    |
|   | - WGCommandBuffer         |  +------------------------------+    |
|   +---------------------------+                                      |
|                                  +------------------------------+    |
|                                  | Platform Integration         |    |
|                                  | - engine.js (device init)    |    |
|                                  | - detect.py (build config)   |    |
|                                  | - display_server_web.cpp     |    |
|                                  +------------------------------+    |
+---------------------------------------------------------------------+
                          |
+---------------------------------------------------------------------+
|              Browser WebGPU API (via emdawnwebgpu)                    |
|   - navigator.gpu.requestAdapter() / requestDevice()                 |
|   - wgpuDeviceCreateBuffer, wgpuDeviceCreateTexture, etc.            |
|   - Single queue, implicit synchronization, no explicit barriers     |
+---------------------------------------------------------------------+
```

### Component Inventory

| Component | Files | LOC | Role |
|-----------|-------|-----|------|
| Device Driver | `rendering_device_driver_webgpu.h/cpp` | ~8,370 | Core: implements all RDD virtual methods |
| Context Driver | `rendering_context_driver_webgpu.h/cpp` | ~300 | Device/surface lifecycle from JS shell |
| Object Wrappers | `webgpu_objects.h` | 430 | GPU object wrapper structs |
| Shader Container | `rendering_shader_container_webgpu.h/cpp` | ~210 | SPIR-V storage + push constant metadata |
| SPIR-V Preprocessing | `spirv_preprocess.cpp/h` | ~3,000 | 12 binary-level SPIR-V rewriting passes |
| Tint Wrapper | `tint_wrapper.cpp/h` | ~80 | C-compatible isolation layer for Tint (C++20) |
| Tint (vendored) | `thirdparty/tint/` | (vendored, 811 files) | Google's Dawn/Chrome SPIR-V → WGSL compiler |
| Renderer Integration | `servers/rendering/` (42 files) | ~1,224 | API traits, format promotion, batching |
| Platform/Web | `platform/web/` (7 files) | ~388 | JS engine glue, build config |
| **Total new code** | | **~12,600** | |

---

## 2. Key Design Decisions

### 2.1 JS-First Device Initialization

**Decision**: The WebGPU device is created by JavaScript *before* WASM loads, then imported into C++ via `Module["preinitializedWebGPUDevice"]`.

**Why**: WebGPU device creation is async (Promises). Emscripten's main-thread C++ cannot yield to the event loop without Asyncify (abandoned due to binary size doubling and runtime instability). The device must be ready before WASM begins execution.

**How it works**:
1. `engine.js` calls `navigator.gpu.requestAdapter()` + `requestDevice()`
2. Device is ready before `Godot(moduleConfig)` is called
3. C++ imports the device via `WebGPU.importJsDevice(Module.preinitializedWebGPUDevice)`
4. From that point, all C++ code has synchronous access to the GPU device

### 2.2 Runtime SPIR-V to WGSL Conversion

**Decision**: Store raw SPIR-V in shader containers. At runtime, preprocess with 12 C++ binary-level passes and convert to WGSL using Tint (compiled directly into the engine WASM). Cache results keyed by 64-bit hash.

**Why**:
- emdawnwebgpu only accepts WGSL, not SPIR-V
- Build-time conversion would lose runtime specialization constant support
- Tint is Google's own SPIR-V→WGSL compiler (same as Chrome uses), C++, BSD-licensed
- Caching prevents redundant conversions (~40ms/shader)

**Trade-off**: ~15s startup cost for first-time conversion of ~383 unique shader stages. Acceptable for initial release; pre-compilation at export time is a future optimization.

### 2.3 Push Constant Emulation via Ring Buffer

**Decision**: Emulate Vulkan push constants using a 256KB storage buffer ring with 256-byte aligned slots and dynamic offsets.

**Architecture**:
```
Ring Buffer (256KB, storage buffer on GPU):
[slot0:256B][slot1:256B][slot2:256B]...[slot1023:256B]
     ^--- push_constant_ring_offset advances per draw

CPU Shadow Buffer (256KB):
- Accumulates push constant data during command recording
- Single wgpuQueueWriteBuffer flush at submit time
- Dirty range tracking minimizes transfer size
```

**Why this design**:
- WebGPU has no push constants; emulation is required
- Storage buffer (not uniform) because push constant structs use std430 layout with arrays
- Dynamic offset allows reusing one bind group across all draws
- 256-byte alignment matches WebGPU's `minStorageBufferOffsetAlignment`
- Batched flush = one IPC crossing per frame for all push constant data
- Ring wrapping handled gracefully with pre-wrap flush

**Placement**: Group 3, binding 120 (high enough to never collide with combined-sampler split bindings which max at ~41).

### 2.4 Combined Image-Sampler Splitting

**Decision**: At the SPIR-V binary level (before Tint parsing), split combined image-sampler variables into separate image and sampler variables with doubled binding indices.

**Why**: WebGPU/WGSL does not support combined image-samplers. Godot's GLSL shaders use `sampler2D` (combined). The SPIR-V must present separate image and sampler variables for Tint to generate valid WGSL.

**Binding convention**: Original binding N → sampler at N*2, image at N*2+1. All other bindings are also doubled to avoid collision.

**Why SPIR-V-level**: Operating at the binary level (rather than post-conversion text patching) is robust and handles edge cases — function parameters, multi-level call chains, access chain expressions.

### 2.5 Subpass Flattening

**Decision**: Each Godot render subpass maps to a separate `WGPURenderPassEncoder`.

**Why**: WebGPU has no subpass concept. Input attachments from previous subpasses are read as regular textures (same underlying WGPUTexture, bound as TextureBinding instead of RenderAttachment).

**Impact**: The Metal backend uses the same pattern. Performance impact is minimal — Godot's Mobile renderer typically uses 1-2 subpasses per render pass. Subpass-based post-processing (tone mapping) is disabled on WebGPU.

### 2.6 No-Op Barriers

**Decision**: All `command_pipeline_barrier` calls are no-ops.

**Why**: The WebGPU spec guarantees sequential execution of passes within a command encoder with implicit barriers at usage scope boundaries. There is no need for (and no API for) explicit barriers.

**Communication**: `API_TRAIT_HONORS_PIPELINE_BARRIERS = 0` tells higher-level Godot code not to waste effort inserting barriers.

### 2.7 Shadow Buffer Pattern for Async Mapping

**Decision**: All buffer reads use a CPU shadow buffer. Readbacks use a persistent staging buffer with async map callbacks and 1-frame latency.

**Why**: WebGPU buffer mapping is async-only. Godot's synchronous `buffer_map()`/`buffer_unmap()` API cannot block for a Promise. The shadow buffer allows immediate CPU access while GPU transfers happen asynchronously.

**Pattern**:
- Write path: CPU writes to shadow buffer → single `wgpuQueueWriteBuffer` flush at submit
- Read path: GPU copies to staging buffer → async `mapAsync` → data available next frame
- The 1-frame readback latency is acceptable for typical use cases (profiling, compute results, viewport capture)

### 2.8 API Traits: Capability-Driven Extension

**Decision**: Introduce `ApiTrait` enum values to the base driver interface that allow the WebGPU driver to advertise capabilities without modifying renderer logic.

| Trait | Purpose |
|-------|---------|
| `TEXTURE_GET_DATA_VIA_DRIVER` | Routes readback through async driver path |
| `TEXTURE_INITIALIZE_DIRECT_WRITE` | Skips staging for texture uploads |
| `BUFFER_CREATE_MAPPED_AT_CREATION` | Uses `mappedAtCreation` optimization |
| `STAGING_BUFFER_MAX_SIZE_MB` | Caps staging pool at 16MB |
| `SKELETON_BUFFER_DIRECT_WRITE` | Direct queue write for bone data |
| `FORCE_OMNI_DUAL_PARABOLOID` | Reduces shadow pass count |
| `BATCH_INSTANCE_DRAWS` | Instance batching for shadows + color |
| `FIRST_INSTANCE_INDEX` | Eliminates per-draw push constant overhead |

All traits default to 0 in the base class — non-WebGPU backends are unaffected.

---

## 3. How It Compares to Vulkan/Metal Backends

| Aspect | Vulkan | Metal | WebGPU |
|--------|--------|-------|--------|
| Object model | Heap alloc, pointer-as-ID | Same | Same |
| Push constants | Native | Emulated (ring buffer) | Emulated (ring buffer) |
| Subpasses | Native | Flattened | Flattened |
| Barriers | Explicit, complex | Tracked implicitly | No-op |
| Shader format | SPIR-V native | SPIR-V → MSL (SPIRV-Cross) | SPIR-V → WGSL (Tint) |
| Combined samplers | Native | Split at shader level | Split at SPIR-V level |
| Multi-draw indirect | Native | Emulated (loop) | Emulated (loop) |
| Buffer mapping | Synchronous | Synchronous | Shadow buffer (async) |
| Pipeline cache | Explicit API | Implicit | No API (browser handles) |
| Queue count | Multiple | Multiple | Single |
| Secondary cmd buffers | Native | Native | Not available |

### Key Divergences

1. **BGL Adaptation**: WebGPU requires bind group layouts to match exactly. The driver maintains a rebind cache that re-creates bind groups when shader BGL expectations differ from the original.

2. **Format Promotion**: WebGPU has limited storage texture format support. R8/RG8/R16/RG16 are promoted to 32-bit equivalents at both the texture and WGSL level.

3. **Strip Topology Dual Pipelines**: WebGPU bakes `stripIndexFormat` into pipeline state. The driver creates both Uint16 and Uint32 variants, selecting at draw time.

4. **Encoder Splitting**: When a texture has both TextureBinding and RenderAttachment usage, the driver proactively splits the command encoder to prevent sync scope violations.

---

## 4. The Shader Pipeline

### Complete Flow

```
GLSL (Godot shaders, authored or built-in)
    | glslang (at Godot compile time)
SPIR-V 1.3 bytecode (stored in shader container)
    | C++ driver: specialization constant patching (if needed)
    | 64-bit hash cache lookup (hit -> return cached WGSL)
    | C++ spirv_preprocess.cpp (12 binary-level passes):
        1. freeze_spec_constant_ops (evaluate OpSpecConstantOp)
        2. rewrite_copy_logical (OpCopyLogical -> OpCopyObject)
        3. rewrite_terminate_invocation (OpTerminateInvocation -> OpKill)
        4. convert_push_constants_to_uniforms (PC -> storage buffer)
        5. split_combined_samplers (combined -> image + sampler)
        6. fix_depth2_images (depth=2 -> depth=1)
        7. negate_position_y (Vulkan -> WebGPU NDC Y-flip)
        8. strip_restrict_decoration (remove Restrict, no WGSL equivalent)
        9. strip_memory_barrier (replace OpMemoryBarrier with OpNop)
        10. fix_nonfinite_literals (Inf/NaN -> FLT_MAX/MIN)
        11. flatten_binding_arrays (unwrap handle arrays)
        12. infer_readonly_storage (add NonWritable to read-only SSBOs)
    | Tint (SPIR-V -> WGSL, compiled into engine WASM)
    | Return WGSL string
    | C++ WGSL post-processing:
        - Format remapping (r8/rg8/r16/rg16 -> 32-bit equivalents)
        - read_write storage demote for vertex/fragment stages
        - hex float fix, strip f16, add diagnostics
    | wgpuDeviceCreateShaderModule
```

### Tint Patches (6 patches covering 8 files, 3 logical groups)

1. **UBO layout relaxation** (1 file): `SetSkipBlockLayout(true)` — Godot's UBO layout uses C++ struct packing, not std140/std430
2. **Spec constant size mismatches** (5 files): Godot's specialization constants change effective struct/array sizes at runtime. Patches add a `kAllowStructMemberSizeMismatch` capability to the IR validator, gate decompose_strided_array padding, and suppress invalid `@size` attributes
3. **Point size tolerance** (1 file): Accept non-constant `point_size` stores — Godot passes through `gl_PointSize` from input to output, Tint strips it anyway
4. **Vendoring** (1 file): Replace `absl::from_chars` with `std::from_chars` — Godot doesn't ship Abseil

Compared to the previous naga approach (42 patched Rust files across many subsystems), Tint requires only 8 files in 3 logical groups, making upgrades straightforward.

---

## 5. The Performance Architecture

### The Core Problem

WebGPU on the web runs inside a browser sandbox. Every GPU API call crosses an IPC boundary: WASM → JS (V8 context switch) → GPU process (Mojo IPC). A draw call costs ~0.2-0.5 microseconds vs ~5ns on native. This 40-250x overhead makes Godot's renderer (designed around "commands are free") IPC-bound, not GPU-bound.

### The Strategy

**Reduce the number of IPC messages** rather than optimizing GPU utilization:

```
Layer 0: Staging fixes + Direct Write     -> eliminate loading/per-frame overhead
Layer 1: Shadow Pass Merging + DP mode    -> reduce render pass count (196 -> 4)
Layer 2: Instance Batching (shadow+color) -> merge N draws into 1
Layer 3: firstInstance + PC dedup         -> eliminate per-draw SetBindGroup
```

### The Journey to Parity

| Phase | Frame Time | vs Native |
|-------|-----------|-----------|
| Baseline (no optimization) | 133 ms | 3.25x slower |
| +Staging fixes | ~120 ms | ~2.9x |
| +Dual-paraboloid + pass merging | 76 ms | ~1.86x |
| +Shadow instance batching | 57 ms | ~1.39x |
| +Skeleton atlas | ~48 ms | ~1.17x |
| +firstInstance dedup | ~46 ms | ~1.12x |
| +Color pass batching | ~36 ms | **Parity** |

---

## 6. Platform Integration

### Initialization Sequence

```
[Browser]                  [Engine JS]                    [WASM/C++]
    |                          |                              |
    | Engine.startGame()       |                              |
    |->                        |                              |
    |                          |                              |
    |  requestWebGPUDevice()   |   device + features          |
    |<-                        |                              |
    |                          |                              |
    |                          | Godot(moduleConfig)          |
    |                          |->                            |
    |                          |   DisplayServerWeb           |
    |                          |   RenderingContextWebGPU     |
    |                          |   importJsDevice()           |
    |                          |   RenderingDevice::initialize|
    |                          |   First frame                |
```

### Build System

- SCons with `webgpu=yes` flag, `--use-port=emdawnwebgpu` for Emscripten
- Tint and SPIRV-Tools compiled directly into the engine WASM (no separate sidecar binary)
- No Rust toolchain needed — all C++ dependencies built with SCons
- Standard Godot build dependencies only (SCons, Python, C++ compiler, Emscripten)

---

## 7. Limitations vs Vulkan Backend

### Not Supported in WebGPU Spec
- Multiview (no VR stereo rendering)
- Variable Rate Shading
- Tessellation / Geometry Shaders
- Subgroups / Wave operations
- Buffer Device Address / Bindless
- Hardware point/line size control

### Implemented with Trade-offs
- Omni shadows: forced dual-paraboloid (quality trade-off for 43% performance gain)
- Texture readback: async with 1-frame delay
- Storage textures: limited formats requiring promotion
- Multi-draw indirect: loop emulation
- Indirect draw count: always uses max count
- binding_array: flattened to single element (no multi-lightmap on web)

### Stubs / TODOs
- `command_resolve_texture`: unimplemented (MSAA resolve via render pass only)
- `command_render_clear_attachments`: mid-pass clears silently ignored
- `get_total_memory_used`: returns 0
- No pipeline caching API (browser handles internally)

---

## 8. Upstream Acceptance Considerations

### What Would Need to Change for Mainline Godot

1. **Base driver interface**: 10 new ApiTrait enums and ~9 new virtual methods affect all backends (have safe no-op defaults, but need review)
2. **Vendored Tint + SPIRV-Tools**: ~16 MB of vendored source. Upstream would want to evaluate vendoring strategy (extraction script provided)
3. **Tint patches**: 6 patches covering 8 files — upstream may want these submitted to Dawn/Tint project
4. **Test infrastructure**: Need headless Chrome/Firefox WebGPU for CI
5. **Shared constants**: Magic numbers (binding 120, ring size 256KB) should be in one shared header

### What Aligns Well with Godot Architecture
- Follows RenderingDevice abstraction correctly
- Uses established heap-alloc + pointer-as-ID pattern
- Properly isolated behind `WEBGPU_ENABLED` define
- Respects export template model
- Mobile renderer auto-selection based on device limits is correct

---

## 9. Architecture Assessment

### Strengths
- **Correct abstractions**: Successfully bridges Godot's Vulkan-centric interface to WebGPU's different model
- **Performance-aware design**: IPC reduction is baked into the architecture, not bolted on
- **Comprehensive documentation**: Extensive inline comments explain "why" not just "what"
- **Graceful degradation**: Missing features detected at init and handled without crashes
- **Consistent patterns**: Object lifecycle, error handling, and async safety are uniform throughout

### Concerns
- **Tint patch maintenance**: 6 patches covering 8 files (3 logical groups). Low complexity, but must be maintained across Tint upgrades
- **Vendored dependency size**: Tint (~9.1 MB) + SPIRV-Tools (~6.8 MB) = ~16 MB of vendored source
- **Single developer knowledge**: No co-authors visible in commit history (bus factor risk)

### Overall Grade: B+

The architecture is sound, pragmatic, and production-ready for its target use case. It correctly identifies and addresses the 9 major gaps between WebGPU and Vulkan with well-designed emulation. The main risks are maintainability and upstream acceptability — both addressable with incremental refactoring that doesn't change the fundamental design.
