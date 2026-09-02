# WebGPU Implementation Notes

> Final implementation summary as of March 13, 2026.
> For pre-implementation design rationale, see `DESIGN.md` and `RESEARCH.md`.

## Implementation Statistics

- **Total driver code**: ~6,800 lines across 9 files in `drivers/webgpu/`
- **Shader translation**: Tint (C++20, compiled to WASM via Emscripten)
- **Shaders compiling**: 278 unique shaders (0 Tint failures, 0 Dawn validation errors)
- **Renderer**: Mobile renderer auto-selected (WebGPU typically reports 16 sampled textures/stage; Forward+ requires ≥48)

## Architecture Decisions (Final)

### 1. Device Initialization
- JS shell calls `navigator.gpu.requestAdapter()` → `requestDevice()` before WASM loads
- Device stored in `Module["preinitializedWebGPUDevice"]`
- C++ imports via `WebGPU["importJsDevice"]()` (emdawnwebgpu port API)
- `timestamp-query` feature auto-requested if available

### 2. Shader Pipeline
```
GLSL (Godot shaders) → SPIR-V (glslang, build-time) → WGSL (Tint, runtime)
```
- 12 SPIR-V preprocessing passes before Tint: combined image-sampler splitting, push constant → uniform buffer rewrite, binding remapping, Y-flip, etc.
- WGSL source passed to `wgpuDeviceCreateShaderModule()` with `WGPUShaderSourceWGSL`
- Reflection data extracted from SPIR-V at container creation time

### 3. Push Constants → Ring Buffer
- 256 KB storage buffer, 256-byte aligned slots, dynamic offsets
- Binding 120 in group 3 (avoids collision with split sampler bindings up to ~41)
- Per-draw: `wgpuQueueWriteBuffer()` + `SetBindGroup()` with dynamic offset
- Dirty-state tracking skips unchanged data between draws
- Universal bind group shared across shaders; merged bind group when group 3 has material uniforms

### 4. Subpass Flattening
- Each Godot subpass → separate `WGPURenderPassEncoder`
- Load/store ops computed per-pass from Godot's attachment configuration
- Input attachments from previous subpasses read as regular textures

### 5. Buffer Management
- Shadow buffer pattern: CPU copy maintained for all mapped buffers
- Write: `memcpy` to shadow → `wgpuQueueWriteBuffer()` on unmap
- Read: `wgpuBufferMapAsync()` with callback → `memcpy` from mapped range

### 6. Bind Group Layout Adaptation
- Shaders may have different BGL expectations due to specialization constant variants
- Cache of "adapted" bind groups: re-created with shader's layout when mismatch detected
- Cache key: (uniform_set_id, pipeline_layout_id)

### 7. Timestamp Queries
- Optional: checks `WGPUFeatureName_TimestampQuery` at device init
- Three-buffer pipeline: QuerySet → resolve buffer (GPU) → readback buffer (MapRead) → CPU shadow
- Async readback via `wgpuBufferMapAsync()` with `WGPUCallbackMode_AllowSpontaneous`
- Graceful dummy results when feature unavailable

## Performance Characteristics

| Operation | Approach | Notes |
|-----------|----------|-------|
| Push constants | Ring buffer + dynamic offset | 1024 draws/frame at 256B each |
| Buffer upload | `wgpuQueueWriteBuffer` | Async, no CPU stall |
| Texture upload | `wgpuQueueWriteTexture` | 256-byte row alignment enforced |
| Barriers | No-op | WebGPU auto-tracks |
| Bind groups | Cached + adapted | Per-shader layout adaptation when needed |
| Shader compile | Tint WASM (~5ms/shader) | One-time cost at shader creation |

## Browser Compatibility

| Browser | Min Version | Status | Notes |
|---------|-------------|--------|-------|
| Chrome | 113 | Stable | Full WebGPU support |
| Edge | 113 | Stable | Chromium-based, same as Chrome |
| Firefox | 130+ | Rolling out | timestamp-query may be unavailable |
| Safari | 18 | Stable | macOS Sonoma / iOS 18; some feature gaps |

### Known Browser-Specific Issues
- **Safari**: May not support `timestamp-query` feature; driver falls back to dummy timestamps
- **Firefox**: Some WebGPU features are behind flags in earlier versions
- **All browsers**: 256-byte buffer→texture copy row alignment enforced by spec

## Export Workflow

1. Set project setting: `rendering/renderer/rendering_method.web` = `mobile` (or `forward_plus`)
2. Build template: `scons platform=web target=template_release webgpu=yes opengl3=no threads=no`
3. Export from Godot editor: HTML shell auto-initializes WebGPU device
4. If browser lacks WebGPU: clear error message shown before WASM loads

## Files Modified Outside `drivers/webgpu/`

| File | Change |
|------|--------|
| `platform/web/detect.py` | Added `webgpu=yes` flag, `--use-port=emdawnwebgpu`, `WEBGPU_ENABLED` define |
| `platform/web/display_server_web.cpp` | WebGPU driver registration and initialization path |
| `drivers/SCsub` | Conditional `webgpu/` subdirectory inclusion |
| `drivers/register_driver_types.cpp` | WebGPU driver registration |
| `servers/rendering/rendering_device_driver.h` | (unchanged — interface only) |
| `modules/glslang/SCsub` | Added `webgpu` to `can_build()` |
| `main/main.cpp` | Added `rendering_device/driver.web` project setting |
| `platform/web/export/export_plugin.cpp` | Emit `renderingDriver` in Engine.js config; WebGPU warning |
| `platform/web/js/engine/config.js` | Added `renderingDriver` config property |
| `platform/web/js/engine/engine.js` | Auto WebGPU device init; `Engine.requestWebGPUDevice()` |
| `misc/dist/html/full-size.html` | WebGPU detection + status messaging |
