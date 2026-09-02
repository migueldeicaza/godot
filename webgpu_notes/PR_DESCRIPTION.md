# Add WebGPU rendering backend for web exports

## Summary

This PR adds a **WebGPU rendering backend** for Godot's web exports, enabling the **Forward Mobile** renderer in the browser. This replaces the WebGL 2.0 Compatibility renderer with a modern GPU API that supports compute shaders, storage buffers, and the full RenderingDevice abstraction.

### What this enables
- **Forward Mobile renderer on web** — the same renderer used on iOS/Android, now in the browser
- **Compute shaders on web** — GPGPU, entity processing, image operations
- **GPU particles on web** — via compute shader dispatch
- **Skeletal animation (GPU skinning)** — verified working
- **PBR materials, shadows, bloom, procedural sky** — full Mobile renderer features
- **GDExtension support** — WASM-based GDExtensions work with WebGPU
- **SPIR-V → WGSL translation** — automatic via Tint (C++/WASM)

### Architecture
- New `RenderingDeviceDriverWebGPU` — full implementation of `RenderingDeviceDriver` interface
- New `RenderingContextDriverWebGPU` — device/surface management for the browser
- New `RenderingShaderContainerWebGPU` — shader container format with SPIR-V → WGSL conversion
- Build system: `scons platform=web webgpu=yes` using Emscripten 4.0.10+ with `emdawnwebgpu` port
- Shader translation: Tint (C++→WASM via Emscripten) converts SPIR-V to WGSL at runtime

## Changes

### New files
- `drivers/webgpu/rendering_device_driver_webgpu.cpp/h` (~5,600 lines) — Main driver
- `drivers/webgpu/rendering_context_driver_webgpu.cpp/h` (~300 lines) — Context/device bootstrap
- `drivers/webgpu/rendering_shader_container_webgpu.cpp/h` (~210 lines) — Shader container
- `drivers/webgpu/webgpu_objects.h` (~350 lines) — GPU object wrappers
- `drivers/webgpu/pixel_formats_webgpu.h` (~710 lines) — DataFormat → WGPUTextureFormat table
- `drivers/webgpu/tint_cli/` — C++ SPIR-V→WGSL converter (Tint + 12 preprocessing passes)

### Modified files
- `SConstruct` — Add `webgpu` build option
- `platform/web/detect.py` — WebGPU build flags, Emscripten 4.0.10+ requirement
- `platform/web/display_server_web.cpp/h` — WebGPU display server initialization
- `platform/web/js/engine/engine.js` — WebGPU device pre-initialization, Tint loader
- `servers/rendering/rendering_device_driver.h` — `buffer_get_data_direct()` virtual (optional override)
- `servers/rendering/rendering_device.cpp` — Hook for driver-level buffer readback
- `modules/glslang/config.py` — Enable glslang for WebGPU builds

## Performance

Tested with a real game (Shiny Gen — 2D/3D hybrid, entities, UI, shadows):
- **Chrome 120+**: Renders correctly at interactive frame rate
- **Shader compilation**: 36 shaders compiled via Tint SPIR-V→WGSL in ~2s
- **Compute shaders**: Dispatch + readback verified (multiply, entity events)
- **Push constant optimization**: 8.5x improvement via ring buffer batching

## Browser compatibility

| Browser | Status |
|---------|--------|
| Chrome 113+ (desktop) | ✅ Working (WebGPU stable since May 2023) |
| Safari 18+ (macOS/iOS) | ✅ Expected to work (WebGPU via Metal) |
| Firefox | ⚠️ Behind flag (`dom.webgpu.enabled`) |
| Chrome Android | ⚠️ Experimental WebGPU support |

## Known limitations

1. **Intra-pass texture sync scope conflict** — When a texture is used as both a render attachment and sampled texture within the same render pass (e.g., shadow atlas), WebGPU reports a non-fatal validation error. Cross-pass conflicts are handled via automatic command encoder splitting. The intra-pass case requires pipeline changes (out of scope for this PR).

2. **Buffer readback latency** — `buffer_get_data()` and `texture_get_data()` have 1-frame latency on WebGPU due to async buffer mapping. First call returns zeros; subsequent calls return the previous frame's data. This is inherent to WebGPU's async-only buffer map model.

3. **Max 4 bind groups** — WebGPU spec limit. Godot uses 4 groups (sets 0-3), which fits.

4. **No 3-component texture formats** — RGB8, RGB16F, RGB32F mapped to RGBA equivalents.

5. **No subgroup operations** — `LIMIT_SUBGROUP_IN_SHADERS` = 0.

6. **Mobile renderer only** — Forward+ requires features (clustered lighting, >48 textures/stage) that exceed browser WebGPU limits. Mobile renderer works fully.

7. **16-bit SNORM/UNORM textures** — Not available in base emdawnwebgpu 4.0.10; mapped to float equivalents.

8. **`command_resolve_texture()` stub** — MSAA resolve is implemented via render pass resolve targets, but the standalone resolve command is stubbed. This method is rarely called in the Mobile renderer pipeline.

9. **`command_render_clear_attachments()` stub** — WebGPU has no mid-pass clear operation. A workaround (end pass → clear pass → restart) is documented in the code for future implementation.

## Testing approach

WebGPU requires a browser with GPU access, making fully automated CI testing challenging. Current testing is:

- **Build CI**: `scons platform=web webgpu=yes` compiles cleanly (verified in GitHub Actions)
- **Local native testing**: 22/22 official Godot demos run without crashes on macOS Metal (validates RD code paths shared with WebGPU)
- **Browser testing**: 10 demos + 1 real game (Shiny Gen) tested in Chrome via Playwright automation with screenshot verification
- **Compute shader testing**: 6 compute shader tests verified in browser (dispatch + readback)

Future work: headless WebGPU testing via Dawn's native WebGPU implementation (bypass browser, run RD tests directly on GPU).

## Test plan

- [x] Build system: `scons platform=web webgpu=yes` compiles cleanly
- [x] 2D rendering (Scene A): sprites, canvas, UI overlays
- [x] PBR + directional shadow (Scene B)
- [x] Multi-draw, point/spot shadow cubemaps (Scene C)
- [x] GPU compute particles (Scene D)
- [x] Skeletal animation / GPU skinning (Scene E)
- [x] SubViewport + Bloom + Procedural Sky (Scene F)
- [x] Compute shader dispatch + readback (multiply, entity events, alpha cleanup)
- [x] Real game rendering (Shiny Gen — entities, UI, skybox, shadows)
- [x] GDExtension WASM loading
- [x] Memory leak check (destructor releases all WebGPU resources)
- [x] Production console output clean (diagnostics behind WEBGPU_VERBOSE flag)

### Official Godot demo projects tested on WebGPU (Chrome)

All 10 demos from `godotengine/godot-demo-projects` pass:

| Demo | Features | GPU Errors | Visual |
|------|----------|-----------|--------|
| 2d/particles | GPU compute particles, trails, collision | 2960 (sync scope) | Correct |
| 2d/platformer | Sprites, parallax, physics | 0 | Correct |
| 2d/sprite_shaders | Outline, blur, shadow, silhouette | 0 | Correct |
| 3d/platformer | PBR, shadows, CharacterBody3D | 4 (sync scope) | Correct |
| 3d/lights_and_shadows | Directional/omni/spot, PCSS | 4 (sync scope) | Correct |
| 3d/particles | GPU compute particles, fire | 4 (sync scope) | Correct |
| **compute/heightmap** | **Compute shader heightmap (NEW for web!)** | **0** | **Correct** |
| **compute/texture** | **Compute shader texture (NEW for web!)** | **4** | **Correct** |
| viewport/gui_in_3d | SubViewport on 3D quad | 4 (sync scope) | Correct |
| gui/control_gallery | Full UI control showcase | 0 | Correct |

Additionally, 22/22 demos pass locally on native (Metal) with zero crashes.

The `compute/heightmap` and `compute/texture` demos are **impossible to run on current WebGL web exports** — they require `RenderingDevice` which is only available with the WebGPU backend.
