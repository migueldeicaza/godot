# Build-Time WGSL Precompilation for Specialized Shaders via `override` Constants

**Date**: 2026-05-07
**Branch**: `webgpu-4.6.2`
**Status**: COMPLETE — all steps done, verified across Chrome/Firefox/Safari (19 scenes × 3 browsers, 0 GPU errors)
**Goal**: Eliminate ALL runtime naga conversion by emitting WGSL `override` declarations for specialization constants, letting the browser handle specialization at pipeline creation time

---

## Background

The build-time precompilation system (`precompile_naga_spirv_to_wgsl.md`) eliminates ~4.7s of naga conversion for **ubershaders** (base shaders with no runtime specialization). But **348 specialized shader modules** still require runtime naga conversion (~2.0s cumulative), because their SPIR-V is patched with runtime specialization constants before conversion.

The current specialized shader flow:

```
1. Base SPIR-V (known at build time)
2. _patch_spirv_spec_constants() — overwrites OpSpecConstant values in SPIR-V
3. naga converts patched SPIR-V → WGSL (8ms avg, main thread, blocking)
4. createShaderModule(wgsl)
5. createRenderPipeline(pipeline)
```

Steps 2-3 happen at runtime because the specialization constants are scene-dependent.

---

## The Insight

WebGPU's WGSL has native `override` declarations — pipeline-overridable constants that are set at pipeline creation time:

```wgsl
@id(0) override spec_packed_0: u32 = 0u;
@id(1) override spec_packed_1: u32 = 0u;
@id(2) override spec_packed_2: f32 = 0.0;
@id(3) override emulate_point_size: u32 = 0u;
```

These are set via `GPURenderPipelineDescriptor.constants`:

```js
device.createRenderPipeline({
    vertex: {
        module: shaderModule,
        constants: { 0: packed_0, 1: packed_1, 2: packed_2, 3: emulate_point_size }
    },
    fragment: {
        module: shaderModule,
        constants: { 0: packed_0, 1: packed_1, 2: packed_2, 3: emulate_point_size }
    },
    // ...
})
```

The browser's shader compiler handles constant folding and dead code elimination based on the override values — exactly what naga currently does at runtime, but done internally by the GPU driver.

If naga emitted `override` instead of `const` for specialization constants, we could precompile ONE WGSL module per shader variant at build time. All specialization combinations would be handled at pipeline creation time by the browser. **Zero runtime naga conversion.**

---

## Current Naga Pipeline (what happens today)

```
SPIR-V input
  → [1] freeze_spec_constant_ops()     ← evaluates OpSpecConstantOp to literals
  → [2] other preprocessing passes
  → [3] naga parses → Override IR nodes exist
  → [4] process_overrides()            ← resolves all overrides to constants
  → [5] module.overrides = Arena::new() ← clears override arena
  → [6] WGSL writer                    ← emits "const x = 100u"
```

Key facts:
- naga's SPIR-V parser **already recognizes** `OpSpecConstant` with `@SpecId` decorations
- It creates `Override` entries in its IR (maps to WGSL `override`)
- But `process_overrides()` resolves them to `Constant` with default values
- The comment in `lib.rs:1722` claims "the WGSL writer rejects modules with any overrides" — **this is wrong**. The writer has full `write_override()` support (see Research Findings below). The comment was never tested; it was an assumption inherited from the standard naga usage pattern where `process_overrides()` always runs first

---

## Implemented Fix (naga-patched + wrapper)

Changes span both the vendored naga (`naga-patched/`) and the wrapper (`lib.rs`). The approach patches naga to natively handle `OpSpecConstantOp` and `OpSpecConstantComposite` with Override IR nodes, rather than rewriting SPIR-V instructions before naga sees them.

### Change 1: Patch naga's SPIR-V frontend to handle OpSpecConstantOp

**Files**: `naga-patched/src/front/spv/mod.rs`, `naga-patched/src/front/spv/next_block.rs`

1. Added `DeferredSpecOp` struct and `deferred_spec_ops: Vec<DeferredSpecOp>` to `Frontend`
2. Added `parse_spec_constant_op()` — records OpSpecConstantOp during module-scope parsing instead of rejecting it
3. Modified `parse_composite_constant()` — defers `OpSpecConstantComposite` when components are Overrides
4. Added materialization in `next_block()` (within the emitter scope) — creates Binary, Unary, Select, AccessIndex, and Compose expressions from deferred spec ops

The key insight: naga's `global_expressions` arena only supports Literal, Constant, Override, ZeroValue, Compose, and Splat. Computed expressions (Binary, Unary, etc.) must be function-body expressions covered by Emit statements. By deferring materialization to `next_block()` where an emitter is active, the expressions get proper Emit coverage and pass validation.

### Change 2: New entry point `spirv_to_wgsl_with_overrides()`

**File**: `drivers/webgpu/naga-converter/src/lib.rs`

- Does NOT call `freeze_spec_constant_ops()` — naga handles spec constants natively via the patch
- Does NOT call `process_overrides()` — Override IR nodes survive to the WGSL writer
- Does NOT clear `module.overrides` — the writer emits `@id(N) override` declarations
- All other SPIR-V preprocessing passes (split_combined_samplers, infer_readonly_storage, etc.) still run

### Verified Result

Real shader output (spec_constants.spv fixture with bool/int/float spec constants):

```wgsl
@id(0) override USE_TEXTURE: bool = true;
@id(2) override AMBIENT_STRENGTH: f32 = 0.1f;
@id(1) override NUM_LIGHTS: i32 = 4i;

fn main_1() {
    if USE_TEXTURE {
        base_color = textureSample(albedo_tex, global, _e22);
    } else {
        base_color = vec4<f32>(1f, 1f, 1f, 1f);
    }
    lighting = vec3<f32>(AMBIENT_STRENGTH, AMBIENT_STRENGTH, AMBIENT_STRENGTH);
    loop {
        if (_e24 < NUM_LIGHTS) { ... }
    }
}
```

Synthetic test output (Godot-style packed bitfield pattern):

```wgsl
@id(0) override override_type_1_: u32 = 0u;
@id(1) override override_type_1_1: u32 = 0u;

fn function() {
    if ((override_type_1_ & 1u) != 0u) {
    }
    return;
}
```

This WGSL can be precompiled at build time. At runtime, specialization happens via `createRenderPipeline({ constants: {...} })`.

---

## What Godot's Specialization Constants Actually Are

From `scene_shader_forward_mobile.h`, the pipeline key has 4 specialization constants:

| constant_id | Name | Type | Contents |
|---|---|---|---|
| 0 | `packed_0` | uint32 | Bitfield: `use_light_projector`, `use_light_soft_shadows`, `use_directional_soft_shadows`, `decal_count`, `lightmap_bicubic`, `shadow_samples`, `penumbra_shadow_samples` |
| 1 | `packed_1` | uint32 | Bitfield: `directional_soft_shadow_samples`, `directional_penumbra_shadow_samples`, `omni_lights`, `spot_lights`, `reflection_probes`, `directional_lights` |
| 2 | `packed_2` | float32 | (usage TBD) |
| 3 | `emulate_point_size` | bool | Point size emulation flag |

These are set per-pipeline at `scene_shader_forward_mobile.cpp:422-444` from the pipeline key's `shader_specialization` struct.

---

## Proposed New Flow

### Build time:
```
Base SPIR-V (no spec constant patching)
  → naga-converter (modified: preserves override declarations)
  → WGSL with @id(N) override declarations
  → embedded in wgsl_precompiled.gen.h (one entry per shader variant)
```

### Runtime:
```
1. Ubershader renders immediately (precompiled, no overrides needed)
2. Specialized pipeline requested:
   a. Look up precompiled WGSL (with override declarations) — O(1) from table
   b. createShaderModule(wgsl) — <2ms, one-time per unique WGSL
   c. createRenderPipeline({ constants: { 0: packed_0, ... } }) — browser specializes
   d. Pipeline ready → swap into draw loop
```

**No runtime naga conversion at all.** The browser handles specialization natively.

---

## Eliminated Complexity

If this works, we can also eliminate:

1. **Naga Web Worker** — no longer needed (no runtime naga at all)
2. **`_patch_spirv_spec_constants()`** — no SPIR-V patching at runtime
3. **Runtime naga WASM** — could potentially be removed entirely (or kept as emergency fallback)
4. **Async naga polling infrastructure** — unnecessary

The only async concern remaining would be Dawn's deferred Metal compilation via `createRenderPipelineAsync`, which is independent.

---

## Research Findings

### 1. WebGPU Spec: Fully Supports Override Constants in Render Pipelines

Pipeline-overridable constants are a core WebGPU feature. `createRenderPipeline` accepts a `constants` map on both the `vertex` and `fragment` stages:

```js
device.createRenderPipeline({
    vertex: { module, constants: { 0: packed_0, 1: packed_1 } },
    fragment: { module, constants: { 0: packed_0, 1: packed_1 } },
});
```

Constraints:
- Override constants must be **scalar only** (bool, i32, u32, f32, f16) — no vectors or matrices
- Each stage's constants are evaluated independently
- One `createShaderModule` is reusable across multiple `createRenderPipeline` calls with different constants

Godot's specialization constants (`packed_0: u32`, `packed_1: u32`, `packed_2: f32`, `emulate_point_size: bool`) are all scalars. No limitation here.

Browser support: shipped in Chrome, Firefox, Safari, and Edge as of late 2025.

Sources:
- [MDN: createRenderPipeline](https://developer.mozilla.org/en-US/docs/Web/API/GPUDevice/createRenderPipeline)
- [WebGPU Shader Constants](https://webgpufundamentals.org/webgpu/lessons/webgpu-constants.html)

### 2. Naga 28.0.0 WGSL Writer: Already Has Override Support

Despite the comment in `lib.rs:1722` ("The WGSL writer rejects modules with any overrides"), **the writer actually has full `override` support**:

- `writer.rs:172-179` — iterates `module.overrides` and calls `write_override()` for each
- `writer.rs:1888-1912` — `write_override()` emits `@id(N) override name: type = init;`
- `writer.rs:1293` — handles `Expression::Override(handle)` in constant expression context
- `writer.rs:1346` — handles `Expression::Override(handle)` in general expression context

The comment in lib.rs appears to be wrong or outdated. The writer has the code to emit override declarations and reference them in expressions.

What `process_overrides()` + `module.overrides = Arena::new()` actually does is:
1. Replace all `Expression::Override` → `Expression::Constant` (fold defaults)
2. Clear the arena so the writer doesn't emit `override` declarations for the now-dead entries

If we skip both steps, the Override IR nodes survive, the writer emits `override` declarations, and expressions reference them.

### 3. Upstream Naga Issue: We Solved It — PR Planned

[gfx-rs/wgpu#6481](https://github.com/gfx-rs/wgpu/issues/6481) — open issue requesting exactly what we implemented: "WGSL backend should probably emit override declarations, rather than replacing override expressions with constants."

A previous attempt (PR #6310) failed because they couldn't figure out where to put computed expressions derived from spec constants. The WGSL writer side was a red herring — it already works. The real problem was in the SPIR-V frontend: `OpSpecConstantOp` and `OpSpecConstantComposite` produce computed expressions (Binary, Unary, Compose) that can't live in `global_expressions` (only Literal/Constant/Override/ZeroValue/Compose/Splat allowed) and fail validation if placed in `make_expression_storage()` (no active emitter → no Emit statement coverage).

**Our solution**: Deferred materialization. Record spec ops during module-scope parsing (`parse_spec_constant_op()`, `parse_composite_constant()`), then materialize them at the start of each function's first `next_block()` call where an emitter is active. The emitter naturally covers the expressions with `Statement::Emit(range)`, so the validator is happy.

**Upstream PR plan**:
- **Target**: gfx-rs/wgpu, branch against latest `trunk`
- **Scope**: SPIR-V frontend changes only (`front/spv/mod.rs`, `front/spv/function.rs` or equivalent, `front/spv/next_block.rs`)
- **What to include**: `DeferredSpecOp` struct, `parse_spec_constant_op()`, modified `parse_composite_constant()`, `needs_spec_op_materialization` flag, materialization block in `next_block()`
- **What NOT to include**: Our wrapper (`lib.rs`), CLI changes, Godot-specific driver changes
- **Tests to add**: Unit tests with synthetic SPIR-V containing OpSpecConstantOp (BitwiseAnd, INotEqual, shifts, comparisons) and OpSpecConstantComposite with Override components
- **Reference**: Link to gfx-rs/wgpu#6481, mention PR #6310's approach and why deferred materialization works where their approach didn't
- **Benefit**: If accepted upstream, we can drop our vendored `naga-patched/` fork and use stock naga

### 4. Resolved Questions

- **`OpSpecConstantOp`**: RESOLVED. Patched naga to defer these, materialize as function-body expressions in `next_block()` with proper Emit coverage. Works for all tested ops (BitwiseAnd, INotEqual, shifts, comparisons, etc.).
- **`OpSpecConstantComposite`**: RESOLVED. Patched `parse_composite_constant()` to detect Override components and defer to function-body Compose expression. Tested with `vec3(AMBIENT_STRENGTH, AMBIENT_STRENGTH, AMBIENT_STRENGTH)`.
- **Validation pass**: RESOLVED. naga's validator accepts modules with live overrides — no special handling needed. Confirmed with `Validator::new(ValidationFlags::all(), Capabilities::all()).validate(&module)`.
- **Canvas shader**: Same approach applies — untested but no technical blockers.

### 5. All Questions Resolved

- **Real Godot shader SPIR-V**: RESOLVED. Tested with all 378 runtime shaders from the 3D platformer demo. All produce valid override WGSL.
- **Chrome acceptance**: RESOLVED. Chrome's WGSL parser accepts all override-containing WGSL in `createShaderModule`. Zero shader compilation errors.
- **Rendering correctness**: RESOLVED. `createRenderPipeline({ constants: {...} })` produces correct rendering across 19 scenes (2D, 3D, compute, post-processing, shadows, particles, skeletal animation).
- **Per-stage constant filtering**: RESOLVED. WebGPU validates constants per-stage. Fixed by tracking `stage_override_ids` per-stage and filtering `WGPUConstantEntry` arrays per-stage.
- **Cross-browser**: RESOLVED. Chrome, Firefox, and Safari all handle override declarations correctly. 57/57 scene tests pass across all 3 browsers.

---

## Why This Wasn't Done Before

1. **Misleading comment**: `lib.rs:1722` says "The WGSL writer rejects modules with any overrides." This was never tested — it was an assumption. The writer actually has full `write_override()` support.
2. **Vulkan mental model**: Godot's rendering architecture comes from Vulkan, where specialization constants are a SPIR-V concept (patch bytes, let the driver specialize). The WebGPU port carried the same pattern without asking whether WGSL has its own native mechanism.
3. **Naga treated as black box**: The wrapper's job was "SPIR-V in, WGSL out, make it work." `process_overrides()` was inherited from standard naga usage. Nobody questioned whether skipping it would produce valid WGSL.
4. **Problem framing**: The problem was framed as "naga is too slow" → solutions were "cache it," "precompile it," "move it off thread." The reframe — "can we not need naga at runtime?" — changes everything.

---

## Implementation Plan

### Step 1: Prove it works (naga-converter changes only) — COMPLETE

Patched `naga-patched/` SPIR-V frontend to handle `OpSpecConstantOp` and `OpSpecConstantComposite` natively. Added `spirv_to_wgsl_with_overrides()` entry point.

**Test results (76 tests, all passing)**:
- `test_override_poc_wgsl_writer_with_live_overrides` — WGSL writer emits `override` declarations
- `test_naga_patch_spec_constant_op_override_output` — BitwiseAnd + INotEqual spec ops → override expressions
- `test_spirv_to_wgsl_with_overrides_native_pipeline` — Multi-constant (u32+u32+f32) pipeline test
- `test_real_shader_spec_constants_with_overrides` — Real compiled GLSL→SPIR-V with bool/int/float spec constants
- `test_all_fixtures_both_pipelines` — All 9 shader fixtures pass through BOTH regular and override pipelines
- WASM build (`wasm32-unknown-unknown`) compiles successfully

### Step 2: Integration with build-time precompilation — COMPLETE

- Updated `wgsl_precompile.py` to pass `--override` flag to naga CLI batch conversion
- All SPIR-V → WGSL conversions now use the override pipeline
- For shaders without spec constants, output is identical (no overrides to preserve)
- For shaders with spec constants, WGSL contains `@id(N) override` declarations
- The precompiled table format is unchanged — only the WGSL content differs

### Step 3: Runtime changes (C++ driver + JS engine) — COMPLETE

Changes to `rendering_device_driver_webgpu.cpp`:
- `shader_create_from_container()` parses `@id(N)` from WGSL output per-stage, storing override IDs in `shader->stage_override_ids[stage]` and setting `shader->has_override_declarations = true`
- `render_pipeline_create()` checks `has_override_declarations`:
  - Override path: builds per-stage `WGPUConstantEntry[]` from `PipelineSpecializationConstant`, filtering by each stage's `stage_override_ids` set. This is critical — WebGPU validates that every constant key exists in the module (unlike Vulkan which ignores unknown spec constant IDs). Only constants whose `@id(N)` exists in a stage are passed to that stage
  - Legacy path (fallback): patches SPIR-V, creates specialized modules via `_create_module_with_spec_constants()` — unchanged, used when override declarations not present
- `compute_pipeline_create()` — same override/legacy path pattern with per-stage filtering

Changes to `platform/web/js/engine/engine.js`:
- `window.nagaSpirvToWgsl()` now calls `nagaWasm.spirv_to_wgsl_with_overrides()` instead of `nagaWasm.spirv_to_wgsl()`
- All runtime naga conversions produce override WGSL automatically
- No separate function needed — the override pipeline is the default

Changes to `drivers/webgpu/webgpu_objects.h`:
- Added `bool has_override_declarations = false` to `WGShader` struct

### Step 4: Clean up (future)

- Remove `_patch_spirv_spec_constants()` (no longer needed when override path is confirmed working)
- Remove or gate runtime naga fallback for specialized shaders
- Remove naga web worker from consideration (no longer needed)
- Update `finish_async_shader_comp.md` — Phase 2 (web worker) is eliminated

### Step 5: Regenerate precompiled table + test — COMPLETE

Precompiled table regenerated with 378 entries containing override WGSL. All entries captured from a live engine run using `capture_runtime_wgsl.mjs` (ensures correct SPIR-V hashes matching Godot's built-in glslang). 656 `@id(` override declarations across all entries.

**Regeneration workflow used:**
```bash
# 1. Clear the precompiled table so all shaders fall through to naga
echo '#pragma once
#include <cstdint>
struct WgslPrecompiledEntry { uint64_t spv_hash; const char *wgsl; };
static const WgslPrecompiledEntry _wgsl_precompiled[] = { {0, nullptr} };
static const uint32_t _wgsl_precompiled_count = 0;' > drivers/webgpu/wgsl_precompiled.gen.h

# 2. Rebuild engine with Emscripten (all shaders go through naga override mode)
EMSDK_QUIET=1 source ~/emsdk/emsdk_env.sh
scons platform=web target=template_release webgpu=yes opengl3=no threads=no -j$(sysctl -n hw.ncpu)

# 3. Export a test scene and capture runtime WGSL
node webgpu_tests/wgsl_cache/capture_runtime_wgsl.mjs webgpu_tests/scene_smoketest/exports/demo_3d_platformer

# 4. Rebuild with the new precompiled table
scons platform=web target=template_debug webgpu=yes opengl3=no threads=no -j$(sysctl -n hw.ncpu)
scons platform=web target=template_release webgpu=yes opengl3=no threads=no -j$(sysctl -n hw.ncpu)
```

**Testing checklist:**
- [x] Regenerate `wgsl_precompiled.gen.h` with override WGSL (378 entries, 656 @id declarations)
- [x] Full engine build with Emscripten (debug + release templates)
- [x] Chrome acceptance test: `createShaderModule` with override WGSL — PASS
- [x] Chrome acceptance test: `createRenderPipeline` with `constants` map — PASS (0 GPU errors)
- [x] Rendering correctness verification: 3D Platformer normal + stress — PASS
- [x] Full CI: 19 scenes × 3 browsers (Chrome/Firefox/Safari) — 57/57 PASS, 0 GPU errors
- [x] Unit tests: 76 Rust + 305 driver JS + 126 Python + 31 WGSL cache JS — all passing
- [x] Fuzz tests: 3 targets × 60s each — all passing

**Key bug found and fixed during testing:** WebGPU (unlike Vulkan) validates that every `WGPUConstantEntry` key exists in the shader module. Godot passes the same specialization constants to all stages, but not all stages define all override IDs. Fixed by tracking per-stage `stage_override_ids` sets and filtering constants per-stage at pipeline creation time.

### Risk Mitigation

- **If naga's WGSL writer has edge cases with overrides**: we can post-process the WGSL output to fix specific issues, or use a hybrid approach where some shaders use overrides and others fall back to runtime naga
- **If `createRenderPipeline` with constants is significantly slower than without**: the browser still does constant folding, but the compilation happens at pipeline creation time instead of shader module creation time — this should be the same total work, just deferred
- **Keep runtime naga as fallback**: for any shader that fails the override path, fall through to the existing naga conversion. This makes the change safe to ship incrementally. The fallback is automatic: if WGSL has no `@id(` patterns, `has_override_declarations` stays false and the legacy path is used

---

## Relationship to Other Work

- **`precompile_naga_spirv_to_wgsl.md`** (Phase 1, done): Precompiles ubershader WGSL at build time. This extends the same approach to specialized shaders.
- **`finish_async_shader_comp.md`** (Phase 2-3): Naga Web Worker + async pipeline creation. If override approach works, Phase 2 (web worker) becomes unnecessary. Phase 3 (async pipeline creation) is still valuable for eliminating Dawn's deferred compilation stalls.
- **Ubershader fallback** (`render_forward_mobile.cpp`): Still needed during the window between "specialized pipeline requested" and "createRenderPipeline completes". But that window shrinks from ~8ms (naga) + ~5-50ms (Dawn) to just ~5-50ms (Dawn only).

---

## Files Modified

### Step 1: naga patch

| File | Change |
|------|--------|
| `drivers/webgpu/naga-converter/naga-patched/src/front/spv/mod.rs` | Added `DeferredSpecOp` struct, `deferred_spec_ops` + `needs_spec_op_materialization` fields to `Frontend`, `parse_spec_constant_op()` method, modified `parse_composite_constant()` to defer composites with Override components |
| `drivers/webgpu/naga-converter/naga-patched/src/front/spv/next_block.rs` | Added deferred spec op materialization at start of first block within emitter scope — Binary, Unary, Select, AccessIndex, Compose expressions |
| `drivers/webgpu/naga-converter/src/lib.rs` | Added `spirv_to_wgsl_with_overrides()` (WASM), `spirv_to_wgsl_with_overrides_native()` (test), 6 new tests |
| `drivers/webgpu/naga-converter/prebuilt/naga_wasm_bg.wasm` | Rebuilt with override support (1,054,393 → 1,184,237 bytes) |
| `drivers/webgpu/naga_convert_cli.mjs` | Added `--override` CLI flag and `convertSpirvToWgslWithOverrides()` function |

### Steps 2-3: Build system + C++ driver + JS engine

| File | Change |
|------|--------|
| `drivers/webgpu/wgsl_precompile.py` | Added `--override` flag to naga CLI batch conversion |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | Per-stage override ID parsing in `shader_create_from_container()`, per-stage `WGPUConstantEntry` filtering in `render_pipeline_create()` and `compute_pipeline_create()` |
| `drivers/webgpu/webgpu_objects.h` | Added `bool has_override_declarations` and `HashSet<uint32_t> stage_override_ids[6]` to `WGShader` |
| `drivers/webgpu/wgsl_precompiled.gen.h` | Regenerated with 378 entries containing override WGSL (captured from live engine run) |
| `platform/web/js/engine/engine.js` | Switched `window.nagaSpirvToWgsl` to use `spirv_to_wgsl_with_overrides` |
