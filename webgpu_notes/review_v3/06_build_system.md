# Build System & Configuration Review

## Overview

The WebGPU backend integrates into Godot's build system through three layers:
1. **SCons/SCsub** — compiles the C++ driver code into the Godot binary
2. **Emscripten/emdawnwebgpu** — provides WebGPU C headers and JS glue for the web platform
3. **Rust/wasm-pack (naga-converter)** — a separate WASM module for SPIR-V to WGSL translation at runtime

The architecture is notable: unlike Vulkan/Metal/D3D12 which compile shaders at build time,
WebGPU converts SPIR-V to WGSL at runtime in the browser via a sidecar WASM module.

---

## 1. SCons Integration

### 1.1 Top-Level Build Option

**File:** `SConstruct` (line 201)
```python
opts.Add(BoolVariable("webgpu", "Enable the WebGPU rendering driver on supported platforms", False))
```

The option defaults to `False`. Users must explicitly pass `webgpu=yes` to enable it.
This follows the same pattern as `d3d12` and `metal` (opt-in), unlike `vulkan` and `opengl3` (opt-out).

### 1.2 Platform Support Gate

**File:** `platform/web/detect.py` (line 96)
```python
def get_flags():
    return {
        ...
        "supported": ["webgpu"],
    }
```

Only the web platform declares `"webgpu"` in its `supported` list. This means attempting
`scons platform=linuxbsd webgpu=yes` will fail with a clear error message (enforced in `drivers/SCsub`).

### 1.3 Driver Compilation

**File:** `drivers/SCsub` (lines 62-66)
```python
if env["webgpu"]:
    if "webgpu" not in supported:
        print_error("Target platform '{}' does not support the WebGPU rendering driver".format(env["platform"]))
        Exit(255)
    SConscript("webgpu/SCsub")
```

**File:** `drivers/webgpu/SCsub`
```python
Import("env")
env_webgpu = env.Clone()
env_webgpu.add_source_files(env.drivers_sources, "*.cpp")
```

The WebGPU SCsub is minimal — it compiles all `.cpp` files in `drivers/webgpu/` into the
shared `drivers` library. There are exactly 3 source files:
- `rendering_context_driver_webgpu.cpp`
- `rendering_device_driver_webgpu.cpp`
- `rendering_shader_container_webgpu.cpp`

### 1.4 Preprocessor Defines

**File:** `platform/web/detect.py` (lines 270-275)
```python
if env["webgpu"]:
    env.AppendUnique(CPPDEFINES=["WEBGPU_ENABLED", "RD_ENABLED"])
    env.Append(CCFLAGS=["--use-port=emdawnwebgpu"])
    env.Append(LINKFLAGS=["--use-port=emdawnwebgpu"])
```

Two critical defines:
- `WEBGPU_ENABLED` — gates all WebGPU-specific code via `#ifdef`
- `RD_ENABLED` — enables the RenderingDevice abstraction layer (shared with Vulkan/Metal/D3D12)

### 1.5 Emscripten Port: emdawnwebgpu

The `--use-port=emdawnwebgpu` flag (both compile and link time) provides:
- The `<webgpu/webgpu.h>` C header (Dawn's WebGPU API)
- JS glue code that maps C function calls to browser WebGPU API calls
- `WebGPU.importJsDevice()` for wrapping a JS GPUDevice as a C WGPUDevice handle

Comment in detect.py notes: "Emscripten 4.0.10+ uses the emdawnwebgpu port; -sUSE_WEBGPU=1 was removed in 5.0."
This is forward-compatible with Emscripten 5.x.

### 1.6 Glslang Module Dependency

**File:** `modules/glslang/config.py`
```python
def can_build(env, platform):
    return env["vulkan"] or env["d3d12"] or env["metal"] or env["webgpu"]
```

WebGPU requires glslang because Godot's shader pipeline is:
GLSL -> SPIR-V (glslang, at Godot compile time) -> WGSL (naga, at browser runtime)

### 1.7 Minimum Emscripten Version

**File:** `platform/web/detect.py` (lines 120-121)
```python
if cc_semver < (4, 0, 0):
    print_error("The minimum Emscripten version to build Godot is 4.0.0, ...")
```

Emscripten 4.0.0 is the minimum for Godot 4.6 overall. The emdawnwebgpu port requires 4.0.10+.
There is no explicit version check for emdawnwebgpu availability — if the port is missing,
the Emscripten compiler will fail with an error about the unknown port.

---

## 2. Rust/Cargo Integration (naga-converter)

### 2.1 Architecture

The naga-converter is a **standalone Rust crate** that compiles to a WebAssembly module
(`naga_wasm_bg.wasm`, ~1 MB). It is NOT linked into the Godot binary at compile time.
Instead, it is:
1. Pre-built using `wasm-pack`
2. Committed as a binary at `drivers/webgpu/naga-converter/prebuilt/naga_wasm_bg.wasm`
3. Included in the web export template zip at build time
4. Loaded and instantiated by JavaScript at game startup
5. Called from C++ via `MAIN_THREAD_EM_ASM_PTR` to convert SPIR-V -> WGSL on demand

### 2.2 Cargo Configuration

**File:** `drivers/webgpu/naga-converter/Cargo.toml`
```toml
[package]
name = "naga-converter"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["cdylib"]

[dependencies]
wasm-bindgen = "0.2"
naga = { path = "naga-patched", features = ["spv-in", "wgsl-out", "glsl-out"] }

[profile.release]
opt-level = "s"
lto = true
```

Key design choices:
- `crate-type = ["cdylib"]` — produces a dynamic library (WASM module via wasm-pack)
- `opt-level = "s"` — optimize for size (appropriate for a download-sensitive web module)
- `lto = true` — link-time optimization further reduces binary size
- `wasm-bindgen = "0.2"` — standard WASM-JS interop layer
- Features: `spv-in` (read SPIR-V), `wgsl-out` (write WGSL), `glsl-out` (unused but present)

### 2.3 Vendored/Patched Naga

The crate uses a **locally vendored and patched** naga at `naga-patched/` (naga v28.0.0).
This is referenced via a path dependency rather than crates.io. The patched naga includes:
- Relaxed validation for boolean I/O types (NotIOShareableType)
- SPIR-V BuiltIn mappings for Godot's custom builtins
- WGSL output tuned for Dawn/Chrome WebGPU compliance

### 2.4 Cargo.lock

The lockfile pins 33 crate dependencies. Notable versions:
- `naga` 28.0.0 (local patched)
- `wasm-bindgen` 0.2.114
- `spirv` 0.3.0+sdk-1.3.268.0
- `thiserror` 2.0.18
- `petgraph` 0.8.3

All dependencies are from crates.io (with checksums) except the local naga path dep.

### 2.5 Build Process

The naga-converter is built **out-of-band** from the SCons build. The process is:

```bash
cd drivers/webgpu/naga-converter
wasm-pack build --target web --release
cp pkg/naga_converter_bg.wasm prebuilt/naga_wasm_bg.wasm
```

The prebuilt binary is then committed to the repo. SCons does NOT invoke cargo/wasm-pack.
This is a deliberate design choice — it avoids requiring Rust toolchain for Godot contributors
who just want to build the web template.

### 2.6 FFI Boundary

The WASM module exports a single function via `#[wasm_bindgen]`:

```rust
#[wasm_bindgen]
pub fn spirv_to_wgsl(spirv_bytes: &[u8]) -> Result<String, JsError>
```

Additional wasm-bindgen runtime exports (memory management):
- `__wbindgen_malloc` — allocate memory in WASM heap
- `__wbindgen_free` — free allocated memory
- `__wbindgen_start` — initialization entry point
- `__wbindgen_externrefs` — externref table for error passing

The function takes SPIR-V bytes, performs multiple transformation passes, and returns WGSL text.

### 2.7 Prebuilt Binary Sizes

- `prebuilt/naga_wasm_bg.wasm`: 1,053,644 bytes (~1.0 MB)
- `out/naga_converter_bg.wasm`: 1,070,564 bytes (~1.02 MB)
- `pkg/naga_converter_bg.wasm`: 1,053,644 bytes (matches prebuilt)

The ~17 KB difference between `out/` and `prebuilt/` suggests the prebuilt is from a
later build (possibly with updated optimizations).

---

## 3. Template Packaging

### 3.1 WASM Inclusion in Export Template

**File:** `platform/web/emscripten_helpers.py` (lines 53-57)
```python
if env.get("webgpu", False):
    in_files.append("#drivers/webgpu/naga-converter/prebuilt/naga_wasm_bg.wasm")
    out_files.append(zip_dir.File("naga_wasm_bg.wasm"))
```

When building with `webgpu=yes`, the prebuilt naga WASM is included in the export template zip.
During game export, it is extracted alongside the game HTML/JS/WASM files.

### 3.2 Runtime Loading in engine.js

**File:** `platform/web/js/engine/engine.js` (lines 297-393)

The JavaScript engine loader:
1. Detects if `renderingDriver === 'webgpu'`
2. In parallel: requests a WebGPU device AND loads the naga WASM
3. `Engine.loadNagaSpirvToWgsl(exe)` fetches `naga_wasm_bg.wasm` from the same directory
4. Instantiates it with a minimal wasm-bindgen runtime (inlined, not ES-module)
5. Exposes `window.nagaSpirvToWgsl(Uint8Array) -> string` globally

### 3.3 C++ to JavaScript Bridge

**File:** `drivers/webgpu/rendering_device_driver_webgpu.cpp` (lines 117-130)

The C++ driver calls naga via Emscripten's `MAIN_THREAD_EM_ASM_PTR`:
```cpp
char *wgsl_str = (char *)(uintptr_t)MAIN_THREAD_EM_ASM_PTR({
    if (typeof window.nagaSpirvToWgsl !== 'function') {
        console.error('naga SPIR-V->WGSL converter not loaded!');
        return 0;
    }
    var spirvBytes = new Uint8Array(HEAPU8.buffer, $0, $1);
    var wgsl = window.nagaSpirvToWgsl(spirvBytes);
    ...
});
```

This is synchronous and runs on the main thread. A WGSL cache (keyed by SPIR-V hash)
avoids redundant conversions for duplicate shader stages.

---

## 4. Complete Build Command

```bash
# Full WebGPU-only web template (release):
scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no

# Debug build with both renderers:
scons platform=web target=template_debug dlink_enabled=yes webgpu=yes opengl3=yes
```

The build process:
1. SCons reads `platform/web/detect.py` -> sets `WEBGPU_ENABLED`, `RD_ENABLED`, links emdawnwebgpu
2. SCons reads `modules/glslang/config.py` -> enables glslang for SPIR-V compilation
3. SCons reads `drivers/SCsub` -> includes `drivers/webgpu/SCsub` -> compiles 3 .cpp files
4. Emscripten (emcc) compiles all C++ with `--use-port=emdawnwebgpu` for WebGPU headers
5. Emscripten links with emdawnwebgpu port JS glue
6. `emscripten_helpers.py` packages: .js + .wasm + naga_wasm_bg.wasm into template zip

---

## 5. Cross-Platform Considerations

### 5.1 Web-Only Support

WebGPU is currently only supported on the web platform (`platform=web`). The `supported`
list in `detect.py` and the gate in `drivers/SCsub` enforce this. Desktop WebGPU (via wgpu-native
or Dawn native) is not supported.

### 5.2 Emscripten Version Sensitivity

The build requires Emscripten 4.0.10+ for the emdawnwebgpu port. The code comments note
that `-sUSE_WEBGPU=1` was the older mechanism (removed in Emscripten 5.0). The current
approach (`--use-port=emdawnwebgpu`) is forward-compatible.

### 5.3 Threads Configuration

The web platform supports both threaded (`threads=yes`, using SharedArrayBuffer/pthreads)
and non-threaded builds. WebGPU works with either configuration. The naga WASM module
always runs on the main thread regardless of threading mode.

### 5.4 LTO Compatibility

When `lto=thin` or `lto=full` is enabled, WebGPU code participates in LTO normally.
No special handling is needed since WebGPU code is standard C++ compiled by emcc.

---

## 6. Issues and Concerns

### 6.1 Prebuilt Binary in Git (Medium Concern)

The ~1 MB WASM binary (`prebuilt/naga_wasm_bg.wasm`) is committed to git. This is
pragmatic (avoids requiring Rust toolchain) but:
- Increases repo clone size
- Risk of binary getting stale vs. source code
- No automated CI to verify prebuilt matches source

**Recommendation:** Add a CI job that rebuilds naga-converter and verifies the output
matches the committed prebuilt (or at least that it compiles successfully).

### 6.2 Missing Explicit Emscripten Version Check for emdawnwebgpu (Low Concern)

The general Emscripten minimum is 4.0.0, but emdawnwebgpu needs 4.0.10. If someone
uses exactly Emscripten 4.0.0-4.0.9, they'll get an unhelpful port-not-found error.

**Recommendation:** Add a version check in `detect.py` when `webgpu=yes`:
```python
if cc_semver < (4, 0, 10):
    print_error("WebGPU requires Emscripten 4.0.10+ (detected: %s.%s.%s)" % cc_semver)
```

### 6.3 glsl-out Feature in Cargo.toml (Trivial)

The naga-converter enables `glsl-out` feature but never uses GLSL output. This adds
dead code to the WASM binary.

**Recommendation:** Remove `"glsl-out"` from features to reduce WASM size by ~50-100KB.

### 6.4 Synchronous Main-Thread Conversion (Known Limitation)

The SPIR-V -> WGSL conversion via `MAIN_THREAD_EM_ASM_PTR` is synchronous and blocks
the main thread. With ~383 shader stages and ~40ms each, this causes a ~15s startup stall.
The WGSL cache mitigates repeated conversions but not first-time loading.

This is a known architectural limitation documented in the codebase. Moving naga to a
Web Worker would require significant async plumbing through Godot's shader loading path.

### 6.5 `out/` vs `prebuilt/` vs `pkg/` Directory Redundancy (Low Concern)

Three directories contain copies of the compiled WASM:
- `out/` — older wasm-pack output (1,070,564 bytes)
- `pkg/` — current wasm-pack output (gitignored but present locally)
- `prebuilt/` — the canonical copy used by the build (1,053,644 bytes)

**Recommendation:** Remove `out/` from the repo; it's superseded by `prebuilt/`.

### 6.6 Stale Comment Reference (Trivial)

In `rendering_shader_container_webgpu.cpp` line 69 and `rendering_device_driver_webgpu.h`
line 109, the comments reference `tmp/naga-converter/src/lib.rs` but the actual path is
`drivers/webgpu/naga-converter/src/lib.rs`.

---

## 7. Summary of Findings

| Aspect | Status | Notes |
|--------|--------|-------|
| SCons option registration | Good | Standard pattern, defaults to off |
| Platform support gating | Good | Clean error if unsupported platform |
| Preprocessor defines | Good | WEBGPU_ENABLED + RD_ENABLED |
| Emscripten port usage | Good | Modern emdawnwebgpu, forward-compatible |
| Driver compilation | Good | Simple SCsub, 3 source files |
| Glslang dependency | Good | Correctly enabled for WebGPU |
| Naga WASM build | Good | Clean separation, size-optimized |
| Cargo dependency pinning | Good | Lockfile present, all deps pinned |
| Template packaging | Good | Automatic inclusion in zip |
| Runtime loading | Good | Parallel with device init |
| FFI bridge | Good | Clean wasm-bindgen interface |
| Documentation | Good | README with rebuild instructions |

### Recommendations (Priority Order)

1. Add Emscripten 4.0.10 minimum version check when `webgpu=yes`
2. Remove unused `glsl-out` feature from Cargo.toml to shrink WASM
3. Fix stale path references in comments (`tmp/` -> `drivers/webgpu/`)
4. Remove `out/` directory (superseded by `prebuilt/`)
5. Consider CI verification that prebuilt matches source

The build system integration is well-designed and follows Godot's established patterns
for rendering backends. The separation of the naga-converter as a prebuilt sidecar
is pragmatic and avoids coupling the main build to Rust tooling.
