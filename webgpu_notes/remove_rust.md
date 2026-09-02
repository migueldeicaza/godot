# Removing the Rust Dependency from godot-webgpu

**Date**: 2026-05-11
**Status**: Discussion / exploration
**Motivation**: Upstream Godot is a C++ project. For a viable upstream PR, the WebGPU backend should not introduce a Rust build dependency. Godot's pattern for thirdparty code is to vendor C/C++ source and build it with SCons — there are no prebuilt WASM binaries anywhere in the Godot repo.

---

## Current Architecture

Godot's shader pipeline is GLSL → SPIR-V (via glslang) → WGSL (via naga). WebGPU only accepts WGSL, so something must translate SPIR-V to WGSL.

The Rust component is **naga-converter** (`drivers/webgpu/naga-converter/`):
- `lib.rs` (~6,500 lines): SPIR-V preprocessing passes (push constant → UBO rewriting, combined image-sampler splitting, readonly storage inference, binding array flattening, binding index doubling)
- `naga-patched/` (42 files): Vendored naga with Godot-specific patches (OpSpecConstantOp handling, override preservation, builtin mappings)
- Prebuilt WASM: `prebuilt/naga_wasm_bg.wasm` (1.1 MB)

Translation is needed in **two contexts**:
1. **Build time**: `wgsl_precompile.py` precompiles ~400 ubershader variants into `wgsl_precompiled.gen.h` (covers ~99.8% of shader lookups at runtime)
2. **Runtime (in-browser)**: The naga WASM runs in the browser as a fallback for shaders not in the precompiled table (specialized shaders with runtime specialization constants, runtime-dependent general_defines, etc.)

**The prebuilt WASM approach won't work upstream.** Godot has no precedent for committed prebuilt binaries. All thirdparty code (spirv-cross, glslang, harfbuzz, freetype, etc.) is vendored as source and built with SCons. This means whatever translator we use must be **buildable from source as part of the normal SCons build**, for both native (build-time CLI) and Emscripten (runtime WASM) targets.

---

## Options Evaluated

### Option 1: SPIRV-Cross (C++, already in Godot)
**Verdict: NOT VIABLE**
- Already at `thirdparty/spirv-cross/`
- Does NOT support WGSL output. Khronos has stated WGSL is not planned.

### Option 2: Python
**Verdict: NOT VIABLE for runtime**
- No mature SPIR-V → WGSL library exists in Python
- Cannot run in the browser for runtime fallback

### Option 3: Keep naga (Rust)
**Verdict: PROBLEMATIC for upstream**
- Works today, but upstream Godot would need to either:
  - Accept a prebuilt WASM blob (no precedent, unlikely)
  - Require Rust + wasm-pack as a build dependency for web exports (hard sell for a C++ project)
- Modifying the translator requires Rust expertise

### Option 4: Tint (C++) — the viable path forward
**Verdict: VIABLE — the best option for upstream**

Tint is Google's shader compiler from the Dawn/Chrome project. SPIR-V → WGSL is literally what Chrome uses it for.

---

## Tint Deep Dive

### What It Is

- Google's shader compiler, part of the Dawn (WebGPU) project
- **License**: BSD 3-Clause (compatible with Godot's MIT). Source: [Dawn LICENSE](https://github.com/google/dawn/blob/main/LICENSE)
- **Language**: C++ — same as Godot
- Used by Chrome, Firefox (indirectly), and IREE (Google's ML compiler)

### SPIR-V → WGSL Feature Support (researched 2026-05-11)

| Feature | Tint Support | Notes |
|---|---|---|
| Combined image-sampler splitting | Partial | Handles `OpSampledImage` instructions (traces back to separate variables). **Rejects** `OpTypeSampledImage` variable declarations. Godot's glslang output uses the supported pattern. |
| Push constants | **NO** | `SpvStorageClassPushConstant` not recognized. Same gap as naga — our `lib.rs` already handles this at the SPIR-V binary level before the translator sees it. ([IREE #7840](https://github.com/google/iree/issues/7840)) |
| OpSpecConstantOp | **NO** | Basic spec constants → `@id(N) override` works. But `OpSpecConstantOp` (operations on spec constants) fails. Same class of problem that required naga patches. |
| Specialization constants → `override` | Yes | Correctly emits `@id(N) override varName : type = default;` |
| Binding remapping | Yes | Built-in `BindingRemapper` transform |
| Read-only storage buffer inference | Yes | Infers `read` vs `read_write` from `NonWritable` decorations. Source: [SPIR-V Reader Overview](https://dawn.googlesource.com/dawn/+/HEAD/docs/tint/spirv-reader-overview.md) |

### Speed vs Naga

A 2022 benchmark by naga's lead developer claimed naga is 13-80x faster than Tint. **This is misleading:**
- Naga ran with IR validation disabled (production naga has it on)
- It was a synthetic marketing piece by naga's creator
- Real-world measurements show **~2-3x difference** (wgpu discussion #1431)
- No updated benchmarks since 2022 — Tint has had 3+ years of optimization
- **For our use case, speed is irrelevant**: the precompiled table handles 99.8% of lookups. A few hundred runtime translations at 1-5ms each is well under a second. The real bottleneck is `createRenderPipeline()` (browser GPU shader compilation), not the SPIR-V → WGSL step.

### WASM Binary Size (MEASURED 2026-05-11)

Actual build results (Emscripten 4.0.10, -Os, LTO, SPIR-V reader + WGSL writer only):

| | Tint WASM | Naga WASM |
|---|---|---|
| Raw | **3.9 MB** | **1.1 MB** |
| Gzipped | **1.2 MB** | **372 KB** |
| Ratio | 3.3x larger raw | baseline |

**Note**: This includes SPIRV-Tools (required by Tint's SPIR-V reader for parsing/validation). The SPIRV-Tools component accounts for a significant portion of the size. However, in the context of Godot's web export (where the main godot.wasm is ~30-50 MB), an extra ~800 KB gzipped is marginal.

**Important context for upstream Godot**: If Tint is vendored and compiled as part of the main Godot WASM binary (rather than a separate sidecar WASM), the size increase would be smaller due to shared code and better dead code elimination across the whole binary.

### Build System and Vendoring

**depot_tools is NOT required.** Dawn supports `-DDAWN_FETCH_DEPENDENCIES=ON` which uses a plain Python script to clone deps.

**The goal for upstream Godot: vendor Tint source in `thirdparty/tint/` and build with SCons**, matching the pattern used for spirv-cross, glslang, etc.

Tint's dependencies and overlap with Godot:

| Dependency | Already in Godot? | Notes |
|---|---|---|
| SPIRV-Headers | Yes (`thirdparty/spirv-headers/`) | |
| SPIRV-Tools | Partially (Godot has spirv-reflect, not full SPIRV-Tools) | Would need to add |
| glslang | Yes (`thirdparty/glslang/`) | |
| Vulkan-Headers | Yes (`thirdparty/vulkan/`) | |
| **abseil-cpp** | **No** | **RESOLVED**: Only 1 file uses abseil (`parse_num.cc` → `absl::from_chars`). Replaced with `std::from_chars`. Zero abseil needed. |

**Open questions:**
- **abseil-cpp**: Can we vendor just the subset Tint needs? Or can abseil usages be replaced with Godot/STL equivalents?
- **C++20**: Tint requires C++20. Godot currently targets C++17. Need to verify if this is a hard blocker or if Godot is moving to C++20 anyway.
- **Emscripten WASM build**: Tint's CMake has Emscripten conditionals but no one has publicly built Tint-only to WASM. Needs to be proven out, and integrated into the SCons web build.

### C++ API for WASM

Tint has no C API (C++ only), but this is a non-issue:
- For native builds: Godot is C++, Tint's API works directly
- For Emscripten WASM: write a thin `extern "C"` wrapper (~20 lines of export boilerplate). Emscripten compiles C++ natively — no special API needed.

---

## What the Migration Involves

### Two independent pieces of work:

#### 1. Port `lib.rs` SPIR-V preprocessing to C++ (same work regardless of translator)

The ~6,500 lines of SPIR-V preprocessing in `lib.rs` operate directly on SPIR-V bytes — raw binary manipulation (finding opcodes, rewriting bindings, splitting samplers). This does NOT depend on naga's IR and is the same work whether we use Tint or anything else.

Options:
- **Direct C++ port**: Translate the Rust byte-manipulation code to C++ (straightforward, same approach)
- **SPIRV-Tools custom passes**: Use the SPIRV-Tools optimizer framework (C++, would need to add to `thirdparty/`) to implement the same transformations as proper SPIR-V optimization passes

The preprocessing handles:
- Push constant → UBO rewriting (Tint doesn't support push constants)
- Combined image-sampler splitting (Tint partially supports this, but our preprocessing is more robust)
- Binding index doubling (Godot-specific layout)
- Readonly storage inference (Tint handles this natively — could skip in preprocessing)
- Binding array flattening (Godot-specific)
- OpSpecConstantOp evaluation (Tint doesn't support this either)

#### 2. Integrate Tint as the SPIR-V → WGSL translator

- Vendor Tint source (+ abseil subset + SPIRV-Tools) in `thirdparty/`
- Write SCons build rules for native and Emscripten targets
- **Build time**: Tint compiles as a native CLI/library used by `wgsl_precompile.py` to generate `wgsl_precompiled.gen.h`
- **Runtime**: Tint compiles to WASM via Emscripten as part of the `scons platform=web` build, shipped alongside the Godot web export
- Wire up the same three-tier cache in `rendering_device_driver_webgpu.cpp`: in-memory cache → precompiled table → Tint WASM fallback
- May need Tint patches for OpSpecConstantOp (or handle it in the SPIR-V preprocessing step instead)

### What can be reused as-is:

- `wgsl_precompile.py` — just needs to call Tint CLI instead of naga CLI
- `wgsl_precompiled.gen.h` — same format, same lookup mechanism
- Three-tier cache in `rendering_device_driver_webgpu.cpp` — same architecture, just swap the JS bridge to call Tint WASM instead of naga WASM
- `engine.js` WASM loader — same pattern, different WASM binary
- All renderer integration code (`render_forward_mobile.cpp`, etc.) — unchanged

---

## Tint vs Naga Summary

| | Naga (current) | Tint (proposed) |
|---|---|---|
| Language | Rust | C++ |
| Upstream viability | Blocked (no prebuilt WASM precedent, Rust build dep) | Viable (vendor source, build with SCons) |
| SPIR-V preprocessing | `lib.rs` (Rust, 6,500 lines) | Port to C++ (same work) |
| SPIR-V → WGSL | Working + 42-file patch | Needs integration + likely fewer patches |
| Speed | ~2-3x faster (real-world) | Slightly slower, irrelevant for our use case |
| WASM size | 1.1 MB (372 KB gz) | 3.9 MB (1.2 MB gz) — measured |
| Browser correctness | Tested across 19 scenes × 3 browsers | IS Chrome's implementation |
| Contributor access | Needs Rust + wasm-pack to modify | Same C++ toolchain as rest of Godot |
| New dependencies | None (Rust self-contained) | abseil-cpp (subset), SPIRV-Tools |

---

## Resolved Questions

1. **abseil-cpp**: **NON-ISSUE.** Only 1 file out of 1,801 uses abseil (`parse_num.cc` → `absl::from_chars` for float parsing). Can be replaced with `std::from_chars` in ~20 lines. The core IR, SPIR-V reader, and WGSL writer have zero abseil usage.

2. **Emscripten WASM size**: **MEASURED.** 3.9 MB raw / 1.2 MB gzipped (vs naga 1.1 MB raw / 372 KB gz). Acceptable given godot.wasm is 30-50 MB. If compiled into the main binary instead of a sidecar, dead code elimination would help further.

3. **C++20**: **HARD REQUIREMENT** but manageable. Tint uses concepts (8), requires clauses (103 across 41 files), std::span (140 uses across 44 files). Godot is C++17. Solution: compile Tint sources as a separate C++20 compilation unit. The public API barely uses C++20.

4. **Source footprint**: ~356 production files for SPIR-V reader + WGSL writer + core IR + utilities.

## Remaining Open Questions

1. **OpSpecConstantOp**: Can this be handled entirely in SPIR-V preprocessing (evaluate the operations and replace with concrete values before Tint sees them)? If so, Tint needs zero internal patches.
2. **SPIRV-Tools**: Godot has `spirv-reflect` but not full SPIRV-Tools. Adding it increases thirdparty size but it's required by Tint's SPIR-V reader.
3. **C++20 in SCons**: Can SCons be configured to compile Tint sources with `-std=c++20` while keeping the rest of Godot at C++17? Likely yes, via per-target compile flags.
