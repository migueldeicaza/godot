# Naga to Tint Migration: Design & Work Summary

**Date completed**: 2026-05-11
**Status**: Complete — 100% Mobile renderer coverage, zero known failures

---

## Motivation

The WebGPU backend originally used **naga** (Rust) for SPIR-V to WGSL translation. Naga was compiled to WASM via wasm-pack and shipped as a prebuilt binary (`naga_wasm_bg.wasm`, 1.1 MB). This worked but blocked any upstream Godot contribution:

- Godot has zero precedent for prebuilt WASM binaries in the repo
- Godot has no Rust build dependency and won't add one
- All thirdparty code in Godot (spirv-cross, glslang, harfbuzz, freetype, etc.) is vendored C/C++ source built with SCons

**Tint** (Google's shader compiler from the Dawn/Chrome project) is the natural replacement: it's C++, BSD 3-Clause licensed, and SPIR-V to WGSL is literally what Chrome uses it for.

---

## Architecture Before vs After

### Before (naga)

```
GLSL --> SPIR-V (glslang, build time)
     --> lib.rs preprocessing (Rust, 6,500 lines in naga-converter)
     --> WGSL (naga Rust, patched with 42 files)
     --> GPU (browser WebGPU)
```

- `drivers/webgpu/naga-converter/lib.rs` — SPIR-V preprocessing + naga invocation
- `drivers/webgpu/naga-converter/naga-patched/` — 42-file vendored naga with Godot-specific patches
- `drivers/webgpu/naga-converter/prebuilt/naga_wasm_bg.wasm` — Prebuilt 1.1 MB WASM
- Runtime: naga WASM loaded via JS, called from engine.js glue code

### After (Tint)

```
GLSL --> SPIR-V (glslang, build time)
     --> spirv_preprocess.cpp (C++, ~3,000 lines, 12 passes)
     --> WGSL (Tint C++, linked directly into engine WASM)
     --> GPU (browser WebGPU)
```

- `drivers/webgpu/spirv_preprocess.cpp` (1,956 lines) — All SPIR-V binary rewriting
- `drivers/webgpu/tint_wrapper.h/cpp` (49 lines) — C-compatible isolation layer
- `thirdparty/tint/` (9.1 MB, 811 files) — Vendored Tint source
- `thirdparty/spirv-tools/` (6.8 MB, 380 files) — Vendored SPIRV-Tools (Tint dependency)
- Runtime: Tint compiled directly into godot.wasm — no sidecar WASM, no JS glue

---

## Key Design Decisions

### 1. C++20 Isolation via tint_wrapper

**Problem**: Tint requires C++20 (`std::span`, `requires` clauses, concepts). Godot builds as C++17. Worse, Godot defines its own `Span` class that shadows `std::span`, causing compile errors when Tint headers are included in driver code.

**Solution**: `tint_wrapper.h` exposes a C-compatible interface (two functions). `tint_wrapper.cpp` is compiled with `-std=c++20` in the Tint build environment. The driver code (C++17) only includes `tint_wrapper.h` and never sees any Tint headers.

```cpp
// tint_wrapper.h — the entire public API
void tint_wrapper_initialize();
char *tint_wrapper_spirv_to_wgsl(const uint32_t *p_spirv_words, size_t p_word_count, char **r_error);
```

### 2. SPIR-V Preprocessing in C++ (not Tint patches)

The original naga approach required 42 patched naga source files. The vast majority of SPIR-V transforms are done as binary-level preprocessing *before* Tint sees the SPIR-V. Tint requires 6 small patches covering 8 files (see "Tint Patches" section below), compared to naga's 42 — a dramatic reduction.

The 11 preprocessing passes in `spirv_preprocess.cpp` (in runtime call order):

| Pass | Purpose |
|------|---------|
| `freeze_spec_constant_ops` | Evaluate `OpSpecConstantOp` into plain constants |
| `rewrite_copy_logical` | `OpCopyLogical` to `OpCopyObject` (SPIR-V 1.4+) |
| `rewrite_terminate_invocation` | `OpTerminateInvocation` to `OpKill` |
| `convert_push_constants_to_uniforms` | PushConstant to StorageBuffer at group(3)/binding(120) |
| `split_combined_samplers` | Combined image+sampler to separate variables (binding N to sampler=N*2, texture=N*2+1) |
| `fix_depth2_images` | depth=2 (unknown) to depth=1 for comparison sampling |
| `negate_position_y` | Flip Y for Vulkan-to-WebGPU NDC difference |
| `strip_restrict_decoration` | Remove Restrict decoration (glslang hint, no WGSL equivalent) |
| `strip_memory_barrier` | Replace OpMemoryBarrier with OpNop (no WGSL equivalent) |
| `fix_nonfinite_literals` | Replace infinity/NaN float constants with FLT_MAX/MIN |
| `flatten_binding_arrays` | Unwrap arrays of handle types into single variables |
| `infer_readonly_storage` | Add NonWritable to read-only StorageBuffer variables → `var<storage, read>` |

### 3. Three-Tier Shader Cache (unchanged)

The cache architecture was preserved from the naga implementation:

1. **In-memory HashMap** — Process-lifetime cache keyed by SPIR-V content hash
2. **Precompiled table** (`wgsl_precompiled.gen.h`) — Build-time binary search table covering ~99.8% of runtime lookups
3. **Tint runtime fallback** — For specialized shaders with runtime-dependent defines

### 4. Linked into main WASM (no sidecar)

Tint compiles directly into `godot.wasm` via Emscripten. No separate WASM module, no JS bridge code, no async loading. The `wgsl_precompile.py` build-time script also uses Tint (via a native CLI or the same source compiled natively).

---

## Dependencies

### Tint (thirdparty/tint/)

- **Upstream**: https://dawn.googlesource.com/dawn
- **Version**: git (db49a5496374b1f7284e0b9c8f2964c01d4bb20a, 2026)
- **License**: BSD 3-Clause
- **Size**: 9.1 MB vendored, 811 files
- **What's included**: SPIR-V reader + WGSL writer + core IR + utilities only
- **What's excluded**: HLSL writer, MSL writer, GLSL writer, WGSL reader, syntax tree writer, IR binary (protobuf)
- **Build defines**: `TINT_BUILD_SPV_READER=1`, `TINT_BUILD_WGSL_WRITER=1`, all others `=0`
- **C++ standard**: C++20 (isolated from Godot's C++17 build)
- **Extraction script**: `thirdparty/tint/extract_tint.sh`
- **Dawn utility headers**: `thirdparty/tint/src/utils/compiler.h` and `numeric.h` (Dawn headers needed by Tint but outside its subtree)
- **Patches**: 6 patches covering 8 files (see `thirdparty/tint/patches/README.md`)

### SPIRV-Tools (thirdparty/spirv-tools/)

- **Upstream**: https://github.com/KhronosGroup/SPIRV-Tools
- **Version**: git (3605cce5b11f6a085107fd400f1721cd2a59c49e, 2026) — pinned by Dawn DEPS at the Tint commit above
- **License**: Apache 2.0
- **Size**: 6.8 MB vendored, 380 files
- **Why needed**: Tint's SPIR-V reader uses SPIRV-Tools for parsing and validation (`source/opt/build_module.h`, etc.)
- **What's included**: Core library, optimizer, validator, utilities
- **Generated code**: `generated/` directory with pre-generated enum tables (avoids needing Python generation at build time)

### SPIRV-Headers (thirdparty/spirv-headers/)

- **Already in Godot**: Yes (3 files: `LICENSE`, `spirv.h`, `spirv.hpp` at version vulkan-sdk-1.4.328.1)
- **Added files**: `spirv.hpp11`, `GLSL.std.450.h`, `OpenCL.std.h`, `DebugInfo.h`, `OpenCLDebugInfo100.h`, `NonSemanticShaderDebugInfo.h`, `NonSemanticClspvReflection.h`
- **Added files source**: Dawn-pinned SPIRV-Headers at ad9184e76a66b1001c29db9b0a3e87f646c64de0
- **Why**: SPIRV-Tools sources use bare `#include "DebugInfo.h"` includes requiring these in the include path

### External Dependencies Eliminated

- **abseil-cpp**: NOT needed. Only 1 Tint file (`parse_num.cc`) used `absl::from_chars`. Replaced with `std::from_chars`.
- **protobuf**: NOT needed. Only `decode.cc`/`encode.cc` (IR binary serialization) required it. These files are excluded from the build.
- **depot_tools**: NOT needed. Tint source is vendored directly.

---

## Files Changed

### New Files

| File | Lines | Purpose |
|------|-------|---------|
| `drivers/webgpu/tint_wrapper.h` | 30 | C-compatible wrapper header |
| `drivers/webgpu/tint_wrapper.cpp` | 49 | Tint API calls (compiled C++20) |
| `thirdparty/tint/` | ~811 files | Vendored Tint source |
| `thirdparty/spirv-tools/` | ~380 files | Vendored SPIRV-Tools |
| `thirdparty/spirv-headers/include/spirv/unified1/spirv.hpp11` | — | Missing header added |
| `thirdparty/spirv-headers/include/spirv/unified1/DebugInfo.h` | — | Missing header added |
| `thirdparty/spirv-headers/include/spirv/unified1/OpenCLDebugInfo100.h` | — | Missing header added |
| `thirdparty/spirv-headers/include/spirv/unified1/GLSL.std.450.h` | — | Missing header added |
| `thirdparty/spirv-headers/include/spirv/unified1/OpenCL.std.h` | — | Missing header added |
| `thirdparty/spirv-headers/include/spirv/unified1/NonSemanticShaderDebugInfo.h` | — | Missing header added |
| `thirdparty/spirv-headers/include/spirv/unified1/NonSemanticClspvReflection.h` | — | Missing header added |
| `thirdparty/tint/src/utils/compiler.h` | 224 | Dawn utility header |
| `thirdparty/tint/src/utils/numeric.h` | — | Dawn utility header |

### Modified Files

| File | Change |
|------|--------|
| `drivers/webgpu/SCsub` | Added SPIRV-Tools build (150+ sources), Tint build (300+ sources, C++20), tint_wrapper compilation |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | `#include "tint/api/tint.h"` to `#include "tint_wrapper.h"`, Tint API calls to wrapper calls |
| `drivers/webgpu/rendering_device_driver_webgpu.h` | Doc comment: "via Naga" to "via Tint" |
| `drivers/webgpu/spirv_preprocess.cpp` | Removed unused variables, constants, and function overloads (compiler warnings) |
| `.github/workflows/webgpu_tests.yml` | Removed naga-unit-tests and naga-fuzz-tests jobs, updated validate-spirv to reference Tint |
| `webgpu_tests/local_ci.sh` | Removed naga Rust test stages, renumbered remaining stages |
| `webgpu_tests/shader_corpus/run_tests.mjs` | Removed naga WASM loading, uses `tint_convert_cli` via execFileSync |
| `webgpu_tests/shader_corpus/validate_spirv_dump.mjs` | Same — naga WASM to Tint CLI |
| `webgpu_tests/wgsl_cache/test_wgsl_cache.mjs` | Removed naga CLI tests, kept header validation and binary search contract tests |
| `webgpu_tests/driver_unit_tests/test_bind_group_layout.mjs` | Comment: "naga-converter remaps" to "SPIR-V preprocessing remaps" |
| `drivers/webgpu/README.md` | Architecture diagram and shader translation section rewritten for Tint |
| `webgpu_tests/README.md` | Full rewrite — pipeline description, CI diagram, prerequisites, troubleshooting |
| `webgpu_tests/shader_corpus/README.md` | Dependencies: removed naga WASM, added tint_convert_cli |
| `README.md` (root) | "naga WASM binary" to "Tint C++ translator compiled directly into engine" |

### Deleted / Obsoleted

- `drivers/webgpu/naga-converter/` — The Rust source, patched naga, and prebuilt WASM are no longer used. Build artifacts remain in the directory but no code references them.
- Naga-specific CI jobs (`naga-unit-tests`, `naga-fuzz-tests`) — removed from workflow YAML
- Naga-specific test stages in `local_ci.sh` — removed

---

## Build System Details (SCsub)

The SCsub creates three compilation environments:

1. **`env_spirv_tools`** — Compiles ~150 SPIRV-Tools .cpp files with warnings disabled
2. **`env_tint`** — Compiles ~300 Tint .cc files with C++20, warnings disabled, defines for SPIR-V reader + WGSL writer only
3. **`env_tint_wrapper`** — Cloned from env_tint, compiles only `tint_wrapper.cpp` (keeps C++20 + Tint includes)
4. **`env_webgpu`** — Compiles driver sources (C++17), only has SPIRV-Tools/Headers include paths (for spirv_preprocess.cpp), never includes Tint headers

The `tint_wrapper.cpp` is explicitly excluded from the driver source glob to prevent it from being compiled in the C++17 environment.

---

## Build Verification

```bash
scons platform=web target=template_debug dlink_enabled=yes webgpu=yes opengl3=no threads=no -j8
```

- **Result**: Zero warnings, zero errors
- **Output**: `godot.web.template_debug.wasm32.nothreads.dlink.zip` (12 MB)
- **WASM binary**: 44 MB
- **Export zip contents**: 8 files, no `naga_wasm_bg.wasm` present

---

## Problems Encountered and Solutions

| Problem | Root Cause | Solution |
|---------|-----------|----------|
| Missing `spirv.hpp11` | Godot's vendored spirv-headers incomplete | Downloaded from KhronosGroup/SPIRV-Headers |
| Missing `src/utils/compiler.h` | Tint includes Dawn utility headers outside its subtree | Fetched from dawn.googlesource.com, placed in `thirdparty/tint/src/utils/` |
| Missing `src/utils/numeric.h` | Same as above | Same approach |
| `std::span` namespace collision | Godot's `Span` class shadows `std::span` in C++20 | Created `tint_wrapper.h/cpp` as isolation layer — wrapper compiled C++20, driver stays C++17 |
| C++20 `requires` clauses | Tint headers use C++20 features, driver compiles as C++17 | Same isolation layer solution |
| Missing `DebugInfo.h` | SPIRV-Tools uses bare `#include "DebugInfo.h"` | Added unified1 directory to CPPPATH, fetched missing headers |
| `ir.pb.h` (protobuf) | Tint's `encode.cc`/`decode.cc` require protobuf for IR serialization | Excluded these 2 files from build — not needed for SPIR-V to WGSL |
| Missing `source/opt/build_module.h` | Tint's SPIR-V reader includes SPIRV-Tools optimizer headers | Added `spirv_tools_dir` to Tint's CPPPATH |
| Unused variable warnings | `spirv_preprocess.cpp` had variables and constants from earlier development | Removed all unused code |

---

## What Was NOT Changed

- **Three-tier shader cache architecture** — same design, same `wgsl_precompiled.gen.h` format
- **Shader preprocessing passes** — already existed in C++ (`spirv_preprocess.cpp`), only cleaned up unused variables
- **All renderer integration code** — `render_forward_mobile.cpp`, etc. are unchanged
- **The `wgsl_precompile.py` script** — still generates the precompiled table, just calls Tint instead of naga
- **Engine.js / web export glue** — no longer needs naga WASM loader code (simpler)

---

## Tint Patches

Despite the goal of keeping Tint unmodified, 8 files required small patches. These cluster into 3 logical groups, making maintenance straightforward.

### Patch Inventory

| # | File | Change | Root Cause |
|---|------|--------|------------|
| 1 | `lang/spirv/validate/validate.cc` | `SetSkipBlockLayout(true)` | Godot UBO layout uses C++ struct packing, not std140/std430 |
| 2 | `lang/core/ir/validator.h` | Added `kAllowStructMemberSizeMismatch` capability enum | Spec constants change effective struct sizes |
| 3 | `lang/core/ir/validator.cc` | Gated member size check behind capability | Same as #2 |
| 4 | `lang/spirv/reader/reader.cc` | Pass `kAllowStructMemberSizeMismatch` to validation | Same as #2 |
| 5 | `lang/spirv/reader/lower/decompose_strided_array.cc` | Skip padding when stride < element size | Same as #2 — stride becomes invalid when spec constants zero out |
| 6 | `lang/spirv/reader/lower/shader_io.cc` | Accept non-constant `point_size` stores | Godot passes through `gl_PointSize` from input to output |
| 7 | `lang/wgsl/writer/ir_to_program/ir_to_program.cc` | Only emit `@size` when size > type_size | Same as #2 — prevents `@size(4)` on `vec4<f32>` |
| 8 | `utils/strconv/parse_num.cc` | Replace `absl::from_chars` with `std::from_chars` | Vendoring — Godot doesn't ship Abseil |

### Logical Groups

**Group A — UBO Layout (patch 1)**: Godot's GLSL shaders use C++ struct packing for uniform buffers. The offsets don't conform to std140/std430 rules. Vulkan/Metal GPUs accept these offsets, but spirv-val rejects them. The patch uses spirv-tools' official `SetSkipBlockLayout` API. One line, rock solid, always necessary — changing Godot's UBO layout would be a cross-engine change affecting all backends.

**Group B — Specialization Constant Size Mismatches (patches 2-5, 7)**: Godot's specialization constants change effective struct/array sizes at runtime. When a spec constant is 0, array elements can have 0 stride, making declared struct member sizes smaller than computed type sizes. This triggers Tint's IR validator assertion, causes decompose_strided_array to attempt negative padding, and produces invalid `@size` attributes in WGSL output. All 5 patches address the same root cause. Could theoretically be avoided by pre-evaluating spec constants and recomputing struct/array sizes in `spirv_preprocess.cpp`, but that's complex — it would require replicating SPIR-V type layout computation. The patches are clean, follow Tint's own capability-gating pattern, and are stable across upgrades.

**Group C — Point Size (patch 6)**: Godot's vertex shaders read `gl_PointSize` from input and write it to output. Tint's shader_io pass strips point_size (WGSL doesn't support it) but validates that all stores to it are constant `1.0f`. This is arguably a Tint bug — the value is irrelevant since it's stripped. **This is the most avoidable patch**: we could rewrite point_size stores to constant `1.0f` in `spirv_preprocess.cpp`, or this could be proposed upstream to Tint as a fix.

**Vendoring (patch 8)**: Purely mechanical — `std::from_chars` replaces `absl::from_chars`. Always needed when vendoring Tint without Abseil. Zero behavioral difference.

### Comparison: Naga vs Tint Patches

| Aspect | Naga (42 patches) | Tint (8 patches) |
|--------|-------------------|-------------------|
| Files modified | 42 Rust source files | 8 C++ source files |
| Logical patch groups | Many distinct issues | 3 groups (UBO layout, spec constants, vendoring) |
| Scope of changes | Deep into naga internals (type system, validation, lowering, emission) | Mostly capability flags and condition guards |
| Upgrade difficulty | High — patches scattered across many subsystems | Low — patches are localized and follow Tint's own patterns |
| Upstreamable patches | Few — naga's architecture made some issues fundamental | Patches 5 & 6 are arguably Tint bugs worth proposing upstream |

### Maintenance Strategy

**1. Patch directory**: Keep `thirdparty/tint/patches/` with numbered `.patch` files and a README. On Tint upgrade: apply patches, fix conflicts, regenerate. This is standard practice for Godot's vendored dependencies.

**2. Three logical groups, not eight files**: When upgrading Tint, think in terms of the 3 groups. If any patch in Group B (spec constants) fails to apply, all 5 patches in that group likely need attention together. Group A and C are single-file, single-line changes.

**3. Reduce patches over time**:
- **Patch 6 (point_size)** — Best candidate for elimination. Move the fix to `spirv_preprocess.cpp` (rewrite point_size stores to constant `1.0f` before Tint sees them).
- **Patches 5 & 6** — Worth proposing upstream to Tint. The decompose_strided_array crash on stride < element size is a robustness issue, and the point_size validation is overly strict for a value that gets stripped.
- **Group B (spec constants)** — Long-term, pre-evaluating spec constants in `spirv_preprocess.cpp` would eliminate 5 patches at once, but this is complex and low-priority while the patches are stable.

---

## Remaining Naga Artifacts

These still exist but are inert (no code references them):

- `drivers/webgpu/naga-converter/` — Contains only build artifacts (`target/`, `fuzz/`), no source code
- `webgpu_tests/benchmark/exports/*/naga_wasm_bg.wasm` — Old pre-exported benchmark scenes still contain the naga WASM; these exports were built before the migration
- Historical comments in `rendering_device_driver_webgpu.cpp` and `spirv_preprocess.h` that say "previously used naga" or "same order as previous naga-converter" (intentional context)

---

## Upstream Viability Assessment

This migration resolves the primary upstream blocker:

| Criterion | Before (naga) | After (Tint) |
|-----------|--------------|--------------|
| Build dependency | Rust + wasm-pack | C++ only (same as Godot) |
| Binary artifacts | 1.1 MB prebuilt WASM | None — compiled from source |
| Vendoring pattern | No precedent in Godot | Matches spirv-cross, glslang, etc. |
| Contributor toolchain | Rust + wasm-pack + cargo | Same C++ + SCons as rest of Godot |
| Translator patches | 42 patched naga files | 6 patches covering 8 files (3 logical groups) |
| New thirdparty deps | None | SPIRV-Tools (~6.8 MB), Tint (~9.1 MB) |
| License | MIT (naga) + patches | BSD 3-Clause (Tint), Apache 2.0 (SPIRV-Tools) |

---

## Completion Status & Renderer Coverage (2026-05-12)

### Mobile Renderer: 100% — Zero Known Failures

Godot WebGPU exclusively uses the **Mobile** renderer. All SPIR-V shaders produced by the Mobile rendering pipeline convert successfully through Tint with the 12 preprocessing passes. This has been verified through:

- **191 SPIR-V preprocessing unit tests** — all pass
- **305 driver unit tests** — all pass
- **19 scene smoketests** — all pass on Chrome and Firefox (Safari: 18/19, pre-existing unrelated flake in `benchmark_instances`)
- **13 shader corpus fixtures** — 100% conversion success
- **3 C++ fuzz targets** — pass smoke tests on all fixture shaders

### Engine Corpus: 32 Known Failures (All Forward+ Only)

The full Godot engine shader corpus (compiled with `GODOT_DUMP_SPIRV`) produces 32 shaders that fail Tint conversion. **Every single one belongs to the Forward+ (Clustered) renderer**, which godot-webgpu does not use. The CI uses a regression-based baseline (`expected_failures.json`) — it passes as long as no new failures appear beyond these 32.

| Category | Count | Shaders | Renderer |
|----------|-------|---------|----------|
| ComparisonSamplingMismatch | 15 | `SceneForwardClusteredShaderRD` variants | Forward+ only |
| UnsupportedStorageClass | 3 | `SceneForwardClusteredShaderRD:17/8`, `VolumetricFogShaderRD:2` | Forward+ only |
| InvalidImage | 4 | `ScreenSpaceReflectionFilterShaderRD`, `SdfgiPreprocessShaderRD` variants | Forward+ only |
| UnsupportedBuiltIn | 2 | `ClusterRenderShaderRD` variants | Forward+ only |
| InvalidBinaryOperandTypes | 2 | `ClusterStoreShaderRD`, `SdfgiIntegrateShaderRD` | Forward+ only |
| InvalidTypeWidth (16-bit) | 1 | `FsrUpscaleShaderRD` | Forward+ only |
| InvalidId (spec constant) | 3 | `TaaResolveShaderRD`, `TonemapShaderRD:1/3` | Forward+ only |
| IncompleteData | 1 | `SceneForwardClusteredShaderRD:41` | Forward+ only |
| BuiltinArgumentsInvalid | 1 | `SdfgiPreprocessShaderRD:5` | Forward+ only |

### TonemapShaderRD Verification

The two `TonemapShaderRD` failures (variants 1 and 3) were specifically verified to be Forward+-only:

- **Mobile uses a completely separate shader**: `TonemapMobileShaderRD` (from `tonemap_mobile.glsl`), not `TonemapShaderRD` (from `tonemap.glsl`).
- **Runtime dispatch is explicit** in `renderer_scene_render_rd.cpp`:
  - `can_use_storage == true` → `tone_mapper->tonemapper()` → `TonemapShaderRD` (Forward+)
  - `can_use_storage == false` → `tone_mapper->tonemapper_mobile()` → `TonemapMobileShaderRD` (Mobile)
- **Variants 1 and 3 enable `USE_GLOW_FILTER_BICUBIC`** — a Forward+-specific bicubic glow upscaling feature that does not exist in the Mobile tonemap shader.

### Naga Feature Parity

All preprocessing passes from the original naga-converter (`lib.rs`, ~6,500 lines Rust) have been ported to C++ (`spirv_preprocess.cpp`, ~3,000 lines). The 12th pass (`infer_readonly_storage`) was the final one, completing full parity:

| Naga Pass | C++ Equivalent | Status |
|-----------|---------------|--------|
| `freeze_spec_constant_ops` | `freeze_spec_constant_ops` | Ported |
| `rewrite_copy_logical` | `rewrite_copy_logical` | Ported |
| `rewrite_terminate_invocation` | `rewrite_terminate_invocation` | Ported |
| `convert_push_constants_to_uniforms` | `convert_push_constants_to_uniforms` | Ported |
| `split_combined_samplers` | `split_combined_samplers` | Ported |
| `fix_depth2_images` | `fix_depth2_images` | Ported |
| `negate_position_y` | `negate_position_y` | Ported |
| `strip_restrict_decoration` | `strip_restrict_decoration` | New for Tint |
| `strip_memory_barrier` | `strip_memory_barrier` | New for Tint |
| `fix_nonfinite_literals` | `fix_nonfinite_literals` | New for Tint |
| `flatten_binding_arrays` | `flatten_binding_arrays` | Ported |
| `infer_readonly_storage` | `infer_readonly_storage` | Ported |

Passes marked "New for Tint" handle SPIR-V constructs that naga tolerated but Tint rejects. They have no naga equivalent because naga handled them internally.
