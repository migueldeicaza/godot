# Build-Time SPIR-V → WGSL Pre-compilation

**Date**: 2026-05-07
**Branch**: `webgpu-4.6.2`
**Goal**: Eliminate ~4.7s of main-thread naga SPIR-V-to-WGSL conversion by precompiling translations at engine build time

---

## The Problem

During startup, Godot's WebGPU driver converts ~400 unique SPIR-V modules to WGSL via naga (a Rust library compiled to WASM). This conversion runs synchronously on the main thread via `MAIN_THREAD_EM_ASM_PTR`, blocking all rendering for ~4.7 seconds.

The naga conversion accounts for **99% of base shader module creation time** — the actual browser `createShaderModule` calls total only ~30ms.

The SPIR-V→WGSL translation is a **pure function** — same input always produces same output. For ubershaders (shaders with no custom material code), the SPIR-V is fully determined at build time since the GLSL sources, variant defines, and general defines are all known constants.

---

## Measured Data

| Metric | Value |
|--------|-------|
| Base shader modules | 511 |
| Specialized shader modules | 348 |
| Total unique SPIR-V inputs (after dedup) | ~400 |
| Total SPIR-V input | ~13.5 MB |
| Total WGSL output | ~13.1 MB |
| **SPIR-V:WGSL size ratio** | **~1.0x (no bloat)** |
| Naga conversion time (base shaders) | ~4.7 s |
| Naga conversion time (specialized shaders) | ~2.0 s |
| Avg naga conversion per module | 8.3 ms |
| Longest single conversion | 526 ms (ParticlesShaderRD) |

---

## Architecture

### Why build-time precompilation works

1. **GLSL sources are known at build time.** The Python build scripts (`glsl_builders.py`) embed raw GLSL source as C++ char arrays in `.gen.h` files during `scons`.

2. **Ubershader variant defines are compile-time constants.** Each shader class (`SceneShaderForwardMobile`, `CanvasShaderRD`, etc.) defines a fixed set of variant combinations at the C++ level. These don't change at runtime.

3. **General defines have stable defaults.** Values like `MAX_ROUGHNESS_LOD`, `SAMPLERS_BINDING_FIRST_INDEX`, `MAX_LIGHTS` are set during renderer initialization but use fixed defaults that can be replicated at build time.

4. **The naga conversion is a pure function.** Given identical SPIR-V bytes, naga always produces identical WGSL output. So we can run the conversion during `scons` instead of at runtime.

### What can't be precompiled

- **Specialized shaders** use `_patch_spirv_spec_constants()` to patch SPIR-V with scene-specific specialization constants, producing unique hashes not in the precompiled table. naga still runs at runtime for these (~2.0s).

- **Runtime-dependent general_defines** (e.g., `MAX_ROUGHNESS_LOD` depends on GPU capabilities). We use sensible defaults — if a particular runtime configuration produces different SPIR-V, it falls through to the naga fallback.

---

## Implementation

### Two-Step Build Process

The precompiled table requires SPIR-V hashes that **exactly match** what Godot's built-in glslang produces at runtime. An external `glslangValidator` binary produces different SPIR-V bytes (different version, optimizer passes), so the hashes won't match.

The solution is a two-step process:

#### Step 1: Bootstrap (`wgsl_precompile.py`, automatic)

During `scons platform=web webgpu=yes`, the SCsub builder invokes `wgsl_precompile.py` if no `.gen.h` exists:

1. Parses GLSL files, enumerates variants from a Python registry
2. Compiles via system `glslangValidator` → SPIR-V
3. Converts via `node naga_convert_cli.mjs` → WGSL
4. Generates `wgsl_precompiled.gen.h` (hashes may not match runtime, but the build compiles and naga handles misses)

#### Step 2: Capture (`capture_runtime_wgsl.mjs`, manual/CI)

After building, run the capture script to get the correct hashes:

```bash
cd webgpu_tests/wgsl_cache
node capture_runtime_wgsl.mjs [export_dir]
```

This script:
1. Launches a WebGPU scene export in Chrome via Playwright
2. Injects `window._wgslDump = {}` before the engine loads
3. The engine's naga fallback path stores each `hash → WGSL` mapping in this object
4. After shader compilation stabilizes, extracts all entries
5. Generates `wgsl_precompiled.gen.h` with the correct runtime hashes

Then rebuild (`scons ...`) to compile the captured table into the engine.

#### Why two steps?

The system `glslangValidator` (v16.3) produces different SPIR-V than Godot's bundled glslang (v14.2). The SPIR-V bytes differ → the MurmurHash3 hashes differ → table lookups miss. The capture script uses Godot's own glslang output, so hashes match exactly.

### Runtime Lookup (`rendering_device_driver_webgpu.cpp`)

`_spv_to_wgsl_cached()` uses a three-tier lookup:

```
1. In-memory cache (HashMap<uint64_t, String>)  →  hit: return cached
2. Precompiled table (binary search)              →  hit: return + cache
3. Naga fallback (WASM, main thread)              →  convert + cache
```

The precompiled table is binary-searched via `_lookup_precompiled_wgsl()` — O(log n) on the sorted hash array. The naga fallback also populates `window._wgslDump` (if set by the capture script) for hash capture.

### SCsub Integration

```python
# drivers/webgpu/SCsub
import wgsl_precompile

env.CommandNoCache(
    "#drivers/webgpu/wgsl_precompiled.gen.h",
    "#drivers/webgpu/wgsl_precompile.py",
    env.Run(wgsl_precompile.build_wgsl_precompiled),
)
```

If a capture-generated `.gen.h` already exists, the builder preserves it. Otherwise, it bootstraps from system glslangValidator.

---

## Data Flow

### After capture (production):
```
capture_runtime_wgsl.mjs: engine runs → naga converts → dump hash+WGSL → wgsl_precompiled.gen.h
scons rebuild: .gen.h compiled into engine binary
Runtime:       _spv_to_wgsl_cached() → precompiled table HIT → return WGSL (no naga!)
```
Time: **<1ms** per shader (binary search only)

### Specialized shaders (all visits):
```
Runtime: _patch_spirv_spec_constants() → new SPIR-V → _spv_to_wgsl_cached()
         → precompiled table MISS → naga runs → cache result
```
Time: ~2.0s total for specialized shaders. Results cached for identical specializations.

---

## Build Results

### With runtime capture (recommended):

| Metric | Value |
|--------|-------|
| Unique entries captured | **210** |
| Runtime precompiled hit rate | **100%** (200/200 unique lookups) |
| Naga fallbacks at runtime | **0** |
| Generated header size | ~2.5 MB |
| Capture time | ~30s (one-time, per engine build) |

### Bootstrap only (no capture, fallback):

| Metric | Value |
|--------|-------|
| Shader files in registry | 70 |
| Total modules processed | 193 |
| GLSL compilation failures | 10 |
| Naga conversion failures | 9 |
| Unique entries (after dedup) | 146 |
| **Runtime hit rate** | **0%** (hash mismatch with system glslang) |

The bootstrap table compiles the engine but doesn't produce matching hashes — all lookups fall through to naga at runtime. Use `capture_runtime_wgsl.mjs` after building to generate the correct table.

---

## Verified Impact

| Scenario | Base Shader Time | Improvement |
|----------|-----------------|-------------|
| Without precompilation | ~4.7 s | — |
| With precompiled table | **<0.01 s** | **~99.8% reduction** |

| Metric | Before | After |
|--------|--------|-------|
| First visible frame | ~7 s | ~2.5 s |
| Time to 30 fps | ~18 s | ~14 s |
| naga WASM still needed? | Yes | Yes (for specialized shaders + fallback) |

### Binary Size Overhead

| Metric | Without precompiled | With precompiled (210 entries) | Delta |
|--------|-------------------|-------------------------------|-------|
| `godot.side.wasm` (raw) | 41.5 MB | 46.0 MB | +4.5 MB (+10.8%) |
| `godot.side.wasm` (gzipped) | 10.5 MB | 11.1 MB | +657 KB (+6.1%) |
| `godot.wasm` (loader) | unchanged | unchanged | 0 |

The WGSL strings compress well (repetitive syntax). Actual download cost is ~657 KB gzipped.

---

## Files Modified

| File | Change |
|------|--------|
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | Added `#include "wgsl_precompiled.gen.h"`, `_lookup_precompiled_wgsl()` binary search, three-tier lookup in `_spv_to_wgsl_cached()` |
| `drivers/webgpu/SCsub` | Added `CommandNoCache` builder for `wgsl_precompiled.gen.h` |
| `webgpu_tests/local_ci.sh` | Added WGSL precompile test stages |

## Files Added

| File | Purpose |
|------|---------|
| `drivers/webgpu/wgsl_precompile.py` | Build-time WGSL precompilation pipeline (GLSL parsing, glslang invocation, naga batch conversion, header generation) |
| `drivers/webgpu/naga_convert_cli.mjs` | Node.js CLI wrapper for naga WASM (SPIR-V → WGSL conversion) |
| `drivers/webgpu/wgsl_precompiled.gen.h` | Auto-generated header with 210 precompiled hash→WGSL entries (after capture) |
| `webgpu_tests/wgsl_cache/capture_runtime_wgsl.mjs` | Captures runtime hash→WGSL mappings from a live engine run |
| `webgpu_tests/wgsl_cache/verify_precompiled_hits.mjs` | Verifies precompiled table hits at runtime |
| `webgpu_tests/wgsl_cache/test_wgsl_precompile.py` | Python unit tests (130 tests: MurmurHash3, GLSL parsing, header format, registry validation) |
| `webgpu_tests/wgsl_cache/test_wgsl_cache.mjs` | JS unit tests (36 tests: naga CLI, header validation, binary search contract) |

## Files Removed

| File | Reason |
|------|--------|
| `webgpu_tests/wgsl_cache/test_cache_integration.mjs` | Was for the IndexedDB approach (removed) |
| `webgpu_tests/wgsl_cache/generate_cache_bundle.mjs` | Was for the IndexedDB approach (removed) |

---

## Test Results

```
Python unit tests (test_wgsl_precompile.py):   130 passed, 0 failed
JS unit tests (test_wgsl_cache.mjs):            36 passed, 0 failed
Engine build with precompiled table:            SUCCESS (50s)
Runtime verification (verify_precompiled_hits): 200/200 hits, 0 naga fallbacks (100% hit rate)
Local CI (shader corpus + unit + fuzz + smoke): 11 passed, 0 failed
```

Verified 2026-05-07: After capture, all 200 unique ubershader lookups hit the precompiled table. Zero naga fallbacks for base shaders.

---

## Relationship to Other Optimization Work

This is **Phase 1** of the three-phase shader optimization plan from `finish_async_shader_comp.md`:

1. **Build-time WGSL precompilation** (this document) — eliminates 4.7s of naga conversion for ubershaders
2. **Naga Web Worker** (move remaining runtime naga calls off main thread) — eliminates 2.0s of specialized shader stalls
3. **Async GPU pipeline creation** (fix emdawnwebgpu callback bug) — eliminates Dawn's deferred compilation stalls

Each phase is independent and can be shipped separately.
