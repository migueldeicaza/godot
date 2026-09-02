# Godot WebGPU Backend — Architecture & Design

## Executive Summary

The Godot WebGPU backend is a complete implementation of Godot's `RenderingDeviceDriver` interface targeting the browser's WebGPU API. It enables Godot 4.6 games to run in the browser with the Forward Mobile renderer at performance parity with native builds, despite WebGPU's fundamentally different execution model.

The implementation spans ~11,000 lines of driver-specific code across 8 files under `drivers/webgpu/`, plus a ~1,670-line Rust/WASM shader converter, ~1,200 lines of rendering server modifications, and ~400 lines of platform/web integration. It achieves functional parity with the Mobile renderer path while working within WebGPU's constraints: no push constants, no combined image-samplers, async-only buffer mapping, no explicit barriers, and limited format support.

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
|                                  | naga-converter (Rust/WASM)   |    |
|   +---------------------------+  | - SPIR-V binary transforms   |    |
|   | webgpu_objects.h          |  | - Push constant rewrite      |    |
|   | - WGBuffer, WGTexture     |  | - Combined sampler split     |    |
|   | - WGShader, WGPipeline    |  | - WGSL generation via Naga   |    |
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
| Naga Converter | `naga-converter/src/lib.rs` | ~1,670 | SPIR-V preprocessing + WGSL output |
| Naga (patched) | `naga-converter/naga-patched/` | (vendored) | Patched Naga v28 for Godot compat |
| Renderer Integration | `servers/rendering/` (42 files) | ~1,224 | API traits, format promotion, batching |
| Platform/Web | `platform/web/` (7 files) | ~388 | JS engine glue, build config |
| **Total new code** | | **~12,600** | |

---

## 2. Key Design Decisions

### 2.1 JS-First Device Initialization

**Decision**: The WebGPU device is created by JavaScript *before* WASM loads, then imported into C++ via `Module["preinitializedWebGPUDevice"]`.

**Why**: WebGPU device creation is async (Promises). Emscripten's main-thread C++ cannot yield to the event loop without Asyncify (abandoned due to binary size doubling and runtime instability). The device must be ready before WASM begins execution.

**How it works**:
1. `engine.js` calls `navigator.gpu.requestAdapter()` + `requestDevice()` in parallel with loading the naga WASM converter
2. Both complete before `Godot(moduleConfig)` is called
3. C++ imports the device via `WebGPU.importJsDevice(Module.preinitializedWebGPUDevice)`
4. From that point, all C++ code has synchronous access to the GPU device

### 2.2 Runtime SPIR-V to WGSL Conversion

**Decision**: Store raw SPIR-V in shader containers. At runtime, convert to WGSL using a patched Naga compiled to WASM. Cache results keyed by 64-bit hash.

**Why**:
- emdawnwebgpu only accepts WGSL, not SPIR-V
- Build-time conversion would lose runtime specialization constant support
- Naga is mature for SPIR-V→WGSL and compiles to WASM cleanly
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

**Decision**: At the SPIR-V binary level (before Naga parsing), split combined image-sampler variables into separate image and sampler variables with doubled binding indices.

**Why**: WebGPU/WGSL does not support combined image-samplers. Godot's GLSL shaders use `sampler2D` (combined). Naga's SPIR-V frontend requires separate OpLoad of image and sampler followed by OpSampledImage at use sites.

**Binding convention**: Original binding N → sampler at N*2, image at N*2+1. All other bindings are also doubled to avoid collision.

**Why SPIR-V-level**: Operating at the binary level (rather than post-Naga text patching) is robust and handles edge cases — function parameters, multi-level call chains, access chain expressions.

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
| Shader format | SPIR-V native | SPIR-V → MSL (SPIRV-Cross) | SPIR-V → WGSL (Naga) |
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
    ↓ glslang (at Godot compile time)
SPIR-V 1.3 bytecode (stored in shader container)
    ↓ C++ driver: specialization constant patching (if needed)
    ↓ 64-bit hash cache lookup (hit → return cached WGSL)
    ↓ JS bridge: MAIN_THREAD_EM_ASM → window.nagaSpirvToWgsl()
    ↓ WASM naga-converter:
        1. freeze_spec_constant_ops (evaluate OpSpecConstantOp)
        2. infer_readonly_storage (add NonWritable decorations)
        3. convert_push_constants_to_uniforms (PC → storage buffer)
        4. split_combined_samplers (combined → image + sampler)
        5. fix_depth2_images (depth=2 → depth=1)
        → naga SPIR-V parser (with Y-flip for Vulkan→WebGPU NDC)
        → module fixups: writeonly→readwrite, Inf/NaN→MAX, strip point_size
        → flatten binding arrays
        → naga validation
        → naga WGSL writer
        → WGSL post-processing: hex float fix, strip f16, add diagnostics
    ↓ Return WGSL string
    ↓ C++ additional WGSL transforms:
        - Format remapping (r8/rg8/r16/rg16 → 32-bit equivalents)
        - read_write storage demote for vertex/fragment stages
        - binding_array flattening at text level
    ↓ wgpuDeviceCreateShaderModule
```

### Naga Patches (6 modifications to vendored naga v28)

1. **IO-Shareable relaxation**: Allow booleans in `@location` bindings (Godot passes bools between stages)
2. **Image class mismatch tolerance**: Allow Depth↔Float type mismatches in function arguments
3. **TEXTURE barrier flag**: New barrier type + `textureBarrier()` WGSL emission for compute shaders
4. **Inconsistent comparison sampling split**: Textures used for both comparison and non-comparison are cloned
5. **Function parameter depth promotion**: Comparison-only parameters promoted to Depth type
6. **Sampling flags through access chains**: Propagate sampling flags through array index expressions

---

## 5. The Performance Architecture

### The Core Problem

WebGPU on the web runs inside a browser sandbox. Every GPU API call crosses an IPC boundary: WASM → JS (V8 context switch) → GPU process (Mojo IPC). A draw call costs ~0.2-0.5 microseconds vs ~5ns on native. This 40-250x overhead makes Godot's renderer (designed around "commands are free") IPC-bound, not GPU-bound.

### The Strategy

**Reduce the number of IPC messages** rather than optimizing GPU utilization:

```
Layer 0: Staging fixes + Direct Write     → eliminate loading/per-frame overhead
Layer 1: Shadow Pass Merging + DP mode    → reduce render pass count (196 → 4)
Layer 2: Instance Batching (shadow+color) → merge N draws into 1
Layer 3: firstInstance + PC dedup         → eliminate per-draw SetBindGroup
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
    |→                         |                              |
    |                          | Promise.all([                |
    |  requestWebGPUDevice()   |   device + features,         |
    |  loadNagaSpirvToWgsl()   |   naga WASM module          |
    |←                         | ])                           |
    |                          |                              |
    |                          | Godot(moduleConfig)          |
    |                          |→                             |
    |                          |   DisplayServerWeb           |
    |                          |   RenderingContextWebGPU     |
    |                          |   importJsDevice()           |
    |                          |   RenderingDevice::initialize|
    |                          |   First frame                |
```

### Build System

- SCons with `webgpu=yes` flag, `--use-port=emdawnwebgpu` for Emscripten
- Naga-converter is a prebuilt WASM sidecar (~1MB), built separately via `wasm-pack`
- Bundled into export template zip alongside game WASM
- No Rust toolchain required for engine contributors

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
2. **WGSL string manipulation**: Should move to the Rust naga-converter (AST transforms vs fragile text patching)
4. **Vendored Naga**: Upstream would want patches submitted to naga project or built as external dep
5. **Test infrastructure**: Headless Chrome/Firefox CI exists (`webgpu_tests/`, `.github/workflows/webgpu_tests.yml`) but would need adaptation to Godot's upstream CI runners
6. **Shared constants**: Magic numbers (binding 120, ring size 256KB) should be in one shared header

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
- **WGSL string patching**: Fragile dependency on Naga's exact output format
- **Naga patch maintenance**: 6 patches to vendored naga v28 require ongoing maintenance with upstream
- **Single developer knowledge**: No co-authors visible in commit history (bus factor risk)

### Overall Grade: B+

The architecture is sound, pragmatic, and production-ready for its target use case. It correctly identifies and addresses the 9 major gaps between WebGPU and Vulkan with well-designed emulation. The main risks are maintainability and upstream acceptability — both addressable with incremental refactoring that doesn't change the fundamental design.
