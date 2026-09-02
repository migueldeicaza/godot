# WebGPU Build System Integration — Exact Changes Required

> **Purpose:** Every file, line number, and code change needed to make `scons platform=web webgpu=yes` compile a WebGPU-enabled Godot web export template. This is Phase 0, Day 1 work.
>
> **Context:** Godot 4.6 has no `webgpu` build option. The web platform currently only supports `opengl3` (GLES3/WebGL2). The RenderingDevice abstraction exists but is only wired up for Vulkan, D3D12, and Metal. We need to add WebGPU as a fourth RD backend and enable it for the web platform.

---

## Change 1: Add `webgpu` build option to SConstruct

**File:** `SConstruct`  
**Lines:** 197–201  
**Why:** No `webgpu` SCons option exists. Every other graphics backend (vulkan, opengl3, d3d12, metal) has one.

**Current code:**
```python
opts.Add(BoolVariable("vulkan", "Enable the Vulkan rendering driver", True))
opts.Add(BoolVariable("opengl3", "Enable the OpenGL/GLES3 rendering driver", True))
opts.Add(BoolVariable("d3d12", "Enable the Direct3D 12 rendering driver on supported platforms", False))
opts.Add(BoolVariable("metal", "Enable the Metal rendering driver on supported platforms (Apple arm64 only)", False))
opts.Add(BoolVariable("use_volk", "Use the volk library to load the Vulkan loader dynamically", True))
```

**After:**
```python
opts.Add(BoolVariable("vulkan", "Enable the Vulkan rendering driver", True))
opts.Add(BoolVariable("opengl3", "Enable the OpenGL/GLES3 rendering driver", True))
opts.Add(BoolVariable("d3d12", "Enable the Direct3D 12 rendering driver on supported platforms", False))
opts.Add(BoolVariable("metal", "Enable the Metal rendering driver on supported platforms (Apple arm64 only)", False))
opts.Add(BoolVariable("webgpu", "Enable the WebGPU rendering driver on supported platforms", False))
opts.Add(BoolVariable("use_volk", "Use the volk library to load the Vulkan loader dynamically", True))
```

---

## Change 2: Register WebGPU as supported in `platform/web/detect.py`

**File:** `platform/web/detect.py`  
**Section A — `get_flags()` (lines 79–95):**

**Current:**
```python
def get_flags():
    return {
        "arch": "wasm32",
        "target": "template_debug",
        "builtin_pcre2_with_jit": False,
        "vulkan": False,
        "module_raycast_enabled": False,
        "optimize": "size",
    }
```

**After:**
```python
def get_flags():
    return {
        "arch": "wasm32",
        "target": "template_debug",
        "builtin_pcre2_with_jit": False,
        "vulkan": False,
        "module_raycast_enabled": False,
        "optimize": "size",
        "supported": ["webgpu"],
    }
```

**Section B — `configure()` (lines ~250–260):**

**Current:**
```python
    if env["opengl3"]:
        env.AppendUnique(CPPDEFINES=["GLES3_ENABLED"])
        env.Append(LINKFLAGS=["-sMAX_WEBGL_VERSION=2"])
        env.Append(LINKFLAGS=["-sOFFSCREEN_FRAMEBUFFER=1"])
        env.Append(LINKFLAGS=["-sGL_ENABLE_GET_PROC_ADDRESS=0"])
```

**Add after that block:**
```python
    if env["webgpu"]:
        env.AppendUnique(CPPDEFINES=["WEBGPU_ENABLED", "RD_ENABLED"])
        env.Append(LINKFLAGS=["-sUSE_WEBGPU=1"])
```

> **Note:** `-sUSE_WEBGPU=1` is the Emscripten flag that links the WebGPU bindings (`<webgpu/webgpu.h>`) and enables `emscripten_webgpu_get_device()`.

---

## Change 3: Add WebGPU driver to `drivers/SCsub`

**File:** `drivers/SCsub`  
**Lines:** 49–62 (after the `metal` block at line ~62)

**Current (metal block at end):**
```python
if env["metal"]:
    if "metal" not in supported:
        print_error("Target platform '{}' does not support the Metal rendering driver".format(env["platform"]))
        Exit(255)
    SConscript("metal/SCsub")
```

**Add after:**
```python
if env["webgpu"]:
    if "webgpu" not in supported:
        print_error("Target platform '{}' does not support the WebGPU rendering driver".format(env["platform"]))
        Exit(255)
    SConscript("webgpu/SCsub")
```

---

## Change 4: Create `drivers/webgpu/` directory with SCsub and sources

**New directory:** `drivers/webgpu/`  
**Files to create:** Copy from `webgpu_notes/stubs/` into `drivers/webgpu/`:
- `SCsub`
- `rendering_context_driver_webgpu.h`
- `rendering_context_driver_webgpu.cpp`
- `rendering_device_driver_webgpu.h`
- `rendering_device_driver_webgpu.cpp`
- `webgpu_objects.h`
- `rendering_shader_container_webgpu.h`
- `rendering_shader_container_webgpu.cpp`

The `SCsub` compiles all `.cpp` files:
```python
#!/usr/bin/env python
Import("env")
env.add_source_files(env.drivers_sources, "*.cpp")
```

---

## Change 5: Wire WebGPU context driver into `servers/display/display_server.cpp`

**File:** `servers/display/display_server.cpp`

**Section A — Includes (lines 38–47):**

**Current:**
```cpp
#if defined(VULKAN_ENABLED)
#include "drivers/vulkan/rendering_context_driver_vulkan.h"
#undef CursorShape
#endif
#if defined(D3D12_ENABLED)
#include "drivers/d3d12/rendering_context_driver_d3d12.h"
#endif
#if defined(METAL_ENABLED)
#include "drivers/metal/rendering_context_driver_metal.h"
#endif
```

**Add after:**
```cpp
#if defined(WEBGPU_ENABLED)
#include "drivers/webgpu/rendering_context_driver_webgpu.h"
#endif
```

**Section B — `is_rendering_device_supported()` (lines 2006–2023):**

After the `METAL_ENABLED` block, add:
```cpp
#ifdef WEBGPU_ENABLED
	if (rcd == nullptr) {
		rcd = memnew(RenderingContextDriverWebGPU);
	}
#endif
```

**Section C — `can_create_rendering_device()` (lines 2088–2105):**

Same pattern — add `#ifdef WEBGPU_ENABLED` block after Metal.

---

## Change 6: Register WebGPU in `main/main.cpp`

**File:** `main/main.cpp`

**Section A — RD driver definition for web (lines 2308–2316):**

**Current (no web entry):**
```cpp
GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.windows", PROPERTY_HINT_ENUM, "vulkan,d3d12"), "vulkan");
GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.linuxbsd", PROPERTY_HINT_ENUM, "vulkan"), "vulkan");
GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.android", PROPERTY_HINT_ENUM, "vulkan"), "vulkan");
GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.ios", PROPERTY_HINT_ENUM, "metal,vulkan"), "metal");
GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.visionos", PROPERTY_HINT_ENUM, "metal"), "metal");
GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.macos", PROPERTY_HINT_ENUM, "metal,vulkan"), "metal");
```

**Add after the macos line:**
```cpp
GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.web", PROPERTY_HINT_ENUM, "webgpu"), "webgpu");
```

**Section B — Available drivers list (lines 2521–2531):**

**Current:**
```cpp
if (rendering_method == "forward_plus" || rendering_method == "mobile") {
#ifdef VULKAN_ENABLED
    available_drivers.push_back("vulkan");
#endif
#ifdef D3D12_ENABLED
    available_drivers.push_back("d3d12");
#endif
#ifdef METAL_ENABLED
    available_drivers.push_back("metal");
#endif
}
```

**Add before the closing `}`:**
```cpp
#ifdef WEBGPU_ENABLED
    available_drivers.push_back("webgpu");
#endif
```

**Section C — Web rendering method (line 2570):**

**Current:**
```cpp
GLOBAL_DEF_RST_BASIC(PropertyInfo(Variant::STRING, "rendering/renderer/rendering_method.web", PROPERTY_HINT_ENUM, "gl_compatibility"), "gl_compatibility");
```

**After:**
```cpp
GLOBAL_DEF_RST_BASIC(PropertyInfo(Variant::STRING, "rendering/renderer/rendering_method.web", PROPERTY_HINT_ENUM, "forward_plus,mobile,gl_compatibility"), "gl_compatibility");
```

> **Note:** Default stays `gl_compatibility` for backward compat. Users opt into `forward_plus` or `mobile` which will use the WebGPU backend.

---

## Change 7: Enable glslang module for WebGPU builds

**File:** `modules/glslang/config.py`  
**Lines:** 1–4

**Current:**
```python
def can_build(env, platform):
    # glslang is only needed when Vulkan, Direct3D 12 or Metal-based renderers are available,
    # as OpenGL doesn't use glslang.
    return env["vulkan"] or env["d3d12"] or env["metal"]
```

**After:**
```python
def can_build(env, platform):
    # glslang is needed for any RenderingDevice backend (Vulkan, D3D12, Metal, WebGPU).
    # OpenGL/GLES3 doesn't use glslang.
    return env["vulkan"] or env["d3d12"] or env["metal"] or env["webgpu"]
```

> **Why:** WebGPU uses GLSL → SPIR-V (via glslang) → WGSL (via Tint). The glslang module provides the GLSL → SPIR-V stage.

---

## Change 8: Register rendering drivers in web display server

**File:** `platform/web/display_server_web.cpp`

**Section A — `get_rendering_drivers_func()` (lines 997–1003):**

**Current:**
```cpp
Vector<String> DisplayServerWeb::get_rendering_drivers_func() {
    Vector<String> drivers;
#ifdef GLES3_ENABLED
    drivers.push_back("opengl3");
#endif
    return drivers;
}
```

**After:**
```cpp
Vector<String> DisplayServerWeb::get_rendering_drivers_func() {
    Vector<String> drivers;
#ifdef WEBGPU_ENABLED
    drivers.push_back("webgpu");
#endif
#ifdef GLES3_ENABLED
    drivers.push_back("opengl3");
#endif
    return drivers;
}
```

> **Note:** WebGPU listed first so it's the preferred driver when both are available.

**Section B — Constructor (lines 1127–1155):**

Before the `#ifdef GLES3_ENABLED` block, add:
```cpp
#ifdef WEBGPU_ENABLED
    if (p_rendering_driver == "webgpu") {
        // WebGPU device is pre-initialized by the JS shell.
        // RenderingDevice is created in main/main.cpp using the RenderingContextDriverWebGPU.
        // No display-server-level init needed here — the RD path handles everything.
    }
#endif
```

> **Note:** Unlike OpenGL where the display server creates the WebGL context, the WebGPU path uses the RenderingDevice abstraction. The `RenderingContextDriverWebGPU` handles device acquisition. The display server just needs to not fall through to the dummy rasterizer.

**File:** `platform/web/display_server_web.h`

Add include (near line 57):
```cpp
#ifdef WEBGPU_ENABLED
// No additional display-server-level members needed — WebGPU uses the RD path.
#endif
```

---

## Change 9: Web export configuration (lower priority)

**File:** `platform/web/export/export_plugin.cpp`

**`get_preset_features()` (lines 345–353):**

Consider adding WebGPU-related features:
```cpp
#ifdef WEBGPU_ENABLED
    // If using forward_plus or mobile rendering method, the template needs WebGPU support.
    r_features->push_back("webgpu");
#endif
```

**`get_export_options()` (lines 368–395):**

No immediate changes needed — the rendering method is a project setting, not an export option. However, for Phase 4, consider adding a `variant/rendering_method` option if both GL and WebGPU templates are offered.

---

## Change 10: Create WebGPU-aware HTML shell (Phase 4)

**File:** `misc/dist/html/full-size.html` (or new `misc/dist/html/webgpu-full-size.html`)

The default HTML shell initializes WebGL. For WebGPU:
- Add `<canvas id="canvas">` (same as current)
- Add JS to pre-initialize WebGPU device before loading WASM:
```javascript
async function initWebGPU() {
    if (!navigator.gpu) {
        throw new Error("WebGPU not supported in this browser");
    }
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) {
        throw new Error("Failed to get GPU adapter");
    }
    const device = await adapter.requestDevice({
        requiredFeatures: ["depth-clip-control", "texture-compression-bc"],
        requiredLimits: {
            maxStorageBufferBindingSize: 1073741824,
            maxBufferSize: 1073741824,
        },
    });
    // Store globally for Emscripten's emscripten_webgpu_get_device()
    Module["preinitializedWebGPUDevice"] = device;
}
```
- Call `initWebGPU()` before `Module.instantiateWasm` or the engine loader

> **Note:** This is Phase 4 work. For early development, a minimal test HTML file suffices.

---

## Execution Order

These changes should be applied in this order for a clean incremental build:

| Step | Change | Files | Can Compile After? |
|------|--------|-------|-------------------|
| 1 | Add `webgpu` SCons option | `SConstruct` | Yes (option exists but unused) |
| 2 | Platform detect | `platform/web/detect.py` | Yes (defines set but no code uses them) |
| 3 | Enable glslang | `modules/glslang/config.py` | Yes |
| 4 | Create driver directory | `drivers/webgpu/*` | No (won't link yet — stubs not complete) |
| 5 | Wire into drivers/SCsub | `drivers/SCsub` | No (includes missing symbols) |
| 6 | Wire into display_server.cpp | `servers/display/display_server.cpp` | No |
| 7 | Wire into main.cpp | `main/main.cpp` | No |
| 8 | Wire into web display server | `platform/web/display_server_web.*` | Yes (with stub implementations) |
| 9 | Export plugin | `platform/web/export/export_plugin.cpp` | Yes |
| 10 | HTML shell | `misc/dist/html/` | N/A (runtime) |

**First compilable milestone:** Steps 1–8 done, with stub files that have `WARN_PRINT("not implemented")` returns. The binary will link and run (showing nothing on screen) but the build pipeline is proven.

---

## Quick Validation Command

```bash
# After all changes, test that the build system accepts the option:
scons platform=web target=template_debug webgpu=yes opengl3=no -j$(nproc) 2>&1 | head -50

# Expected: compilation starts, finds drivers/webgpu/*.cpp, defines WEBGPU_ENABLED and RD_ENABLED
```

---

## Files Modified (Summary)

| File | Type | Lines Changed |
|------|------|--------------|
| `SConstruct` | Modified | +1 line |
| `platform/web/detect.py` | Modified | +4 lines |
| `drivers/SCsub` | Modified | +5 lines |
| `drivers/webgpu/*` (8 files) | **New** | ~1800 lines |
| `servers/display/display_server.cpp` | Modified | +10 lines |
| `main/main.cpp` | Modified | +6 lines |
| `modules/glslang/config.py` | Modified | +1 line (edit) |
| `platform/web/display_server_web.cpp` | Modified | +8 lines |
| `platform/web/display_server_web.h` | Modified | +2 lines |
| `platform/web/export/export_plugin.cpp` | Modified | +3 lines |
| `misc/dist/html/` | New/Modified | ~30 lines |
