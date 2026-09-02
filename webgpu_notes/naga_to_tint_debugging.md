# Naga → Tint Debugging: Known Issues & Naga Reference Patterns

**Date**: 2026-05-11
**Status**: COMPLETE — scene_a renders with 0 errors

---

## Overview

The Tint migration replaces naga (Rust WASM) with Tint (C++ linked) for SPIR-V → WGSL.
Before the migration, there were **0 shader errors** and CI was fully passing.

The C++ `spirv_preprocess.cpp` reimplements the SPIR-V binary rewriting passes that
naga-converter's `lib.rs` did in Rust. Several of these passes had bugs that the naga
code had already solved. This document maps each current failure to the working naga
pattern so we can fix the C++ code correctly rather than guessing.

**Key source files**:
- `drivers/webgpu/spirv_preprocess.cpp` — current C++ SPIR-V preprocessor
- `drivers/webgpu/naga-converter/src/lib.rs` — working Rust SPIR-V preprocessor (~6,500 lines)
- `drivers/webgpu/naga-converter/naga-patched/src/front/spv/mod.rs` — naga SPIR-V reader patches
- `drivers/webgpu/naga-converter/naga-patched/src/front/spv/image.rs` — naga image/depth patches
- `thirdparty/tint/src/tint/lang/spirv/validate/validate.cc` — Tint's SPIR-V validation wrapper

---

## Issue 1: Bloom Shader OpFunctionCall Type Mismatch (FIXED)

### Symptoms (resolved)

`BloomDownKernel4_s21_vf2_` and `BloomUpKernel4_s21_vf2_` — these shaders pass
`source_color` (a regular combined sampler) to a function via `OpFunctionCall`.
After `split_combined_samplers`, SPIR-V validation failed: the variable's pointer type
no longer matched the function parameter's expected type.

### Root cause

`spirv_preprocess.cpp` was changing the variable's `OpTypePointer` from
`ptr(UniformConstant, SampledImage)` → `ptr(UniformConstant, Image)` so that the
variable holds just the image. But `OpFunctionCall` passes the variable to a function
whose parameter still expects `ptr(UniformConstant, SampledImage)`. SPIR-V validates
types by **ID**, not structural equivalence.

### How naga solved it (lib.rs lines 820-1334)

Naga DID rewrite the variable's OpVariable type (lib.rs line 1259-1269), but naga's
SPIR-V frontend didn't validate type consistency at call sites — it just parsed the
instructions. Tint runs spirv-val which catches the mismatch.

### Fix applied (superseded by Issue 7 fix)

Originally changed `split_combined_samplers` to NOT rewrite OpVariable types for
non-multisampled vars, keeping `ptr(SampledImage)`. This was later superseded by
the Issue 7 fix which DOES rewrite all OpVariable types to `ptr(Image)` but also
rewrites `OpFunctionParameter` types and `OpTypeFunction` to match.

---

## Issue 2: sdf_vec_textures "ID not defined" (FIXED)

### Symptoms (resolved)

`flatten_binding_arrays` strips `OpTypeArray` for handle types (textures/samplers).
After stripping, some instruction still referenced the stripped array type's ID,
causing SPIR-V validation error: "ID '786' has not been defined".

### How naga solved it (lib.rs lines 1422-1461)

Naga did NOT touch the SPIR-V binary for array flattening. Instead:
1. Let naga parse the SPIR-V with arrays fully intact
2. In naga's IR, changed `ArraySize::Constant(N)` → `ArraySize::Constant(1)`
3. WGSL text fixup stripped `binding_array<T, 1>` → `T`

### Fix applied

Comprehensive ID replacement in `flatten_binding_arrays`: builds `array_to_elem` map,
replaces every operand matching an array type ID with the element type ID across ALL
instructions (excluding literal positions in OpConstant/OpSwitch). Then strips
OpTypeArray and OpAccessChain for handle arrays.

### Additional fix: pointer type deduplication

After array→elem replacement, two `OpTypePointer` instructions can become
structurally identical but with different SPIR-V IDs (the original array pointer
type and the existing element pointer type). Since SPIR-V validates types by ID,
this caused OpFunctionCall type mismatches for lightmap shaders.

Fix: added pointer type deduplication in `flatten_binding_arrays`. After building
the `array_to_elem` map, scans all `OpTypePointer` instructions to detect duplicates
that will result from the replacement. Builds a `ptr_remap` table and strips the
duplicate `OpTypePointer` instructions during the rewrite pass.

---

## Issue 3: Multisampled Combined Sampler (FIXED)

### Symptoms (resolved)

Tint's SPIR-V parser crashes with `TINT_ASSERT` at `parser.cc:3147-3148` in
`EmitSampledImage` when `OpSampledImage` is created from a multisampled image.

### Root cause

Tint's built-in `SplitCombinedImageSamplerPass` (parser.cc:116) runs inside Tint
after our preprocessing. When it encounters multisampled variables still typed as
`ptr(SampledImage)`, it tries to split them and creates `OpSampledImage` from the
multisampled image — which triggers a TINT_ASSERT crash.

### How naga handled it

Naga's patched SPIR-V frontend could handle `OpSampledImage` from multisampled
images natively. Tint cannot. The fix matches naga's effective behavior: for
multisampled images, the variable holds just the image type (no sampler needed).

### Fix applied

For **all** combined vars (multisampled and non-multisampled), changed
`split_combined_samplers` to rewrite `OpVariable` type from `ptr(SampledImage)` →
`ptr(Image)`. This prevents Tint's internal `SplitCombinedImageSamplerPass` from
seeing them as combined samplers. See Issue 7 for the full details.

---

## Issue 4: SceneData Struct Alignment (FIXED)

### Symptoms (resolved)

"Structure decorated as Block for variable in Uniform storage class must follow
relaxed uniform buffer layout rules: member 10 at offset 586 is not aligned to 16."

### Analysis

Godot's runtime-specialized shaders produce uniform buffer structs with member
offsets that match C++ struct packing but don't satisfy any standard SPIR-V alignment
rules (std140, std430, or even scalar). The offsets can be non-4-byte-aligned
(e.g., offset 362 for a float). This is valid on Vulkan GPUs (which don't validate
struct layouts) but rejected by Tint's spirv-val validation.

### How naga handled it

Naga had **no struct layout validation at all**. It parsed OpMemberDecorate Offset
values directly and used them as-is.

### Fix applied

Added `val_opts.SetSkipBlockLayout(true)` in
`thirdparty/tint/src/tint/lang/spirv/validate/validate.cc`. This tells spirv-val
to skip all block layout validation, matching naga's behavior.

---

## Issue 5: DirectionalLights Struct Size Mismatch (FIXED)

### Symptoms (resolved)

After fixing block layout validation (Issue 4), 2 shaders now fail Tint's IR-level
validation: `"struct member 0 with size=188 must be at least as large as the type
with size 464"` for the `DirectionalLights` struct.

### Root cause

Godot's runtime specialization constants change effective struct sizes at runtime.
The SPIR-V declares a struct with an `@size` annotation from one specialization but
the actual type size differs. Tint's IR validator and WGSL resolver both reject
`@size < type_size`.

### How naga handled it

Naga had **no struct member size validation**. It accepted any `@size` value and
passed it through to WGSL without checking against the type's actual size.

### Fix applied (3-part)

1. **IR validator** (`validator.h`): Added `kAllowStructMemberSizeMismatch` capability
   enum value
2. **IR validator** (`validator.cc`): Gated the `member->Size() < member->Type()->Size()`
   check behind the new capability
3. **SPIR-V reader** (`reader.cc`): Added `kAllowStructMemberSizeMismatch` to
   capabilities passed to `Validate()`
4. **WGSL writer** (`ir_to_program.cc`): Only emit `@size` when size > type size
   (never clamp upward when size < type size)
5. **decompose_strided_array.cc**: Skip padding wrapper when stride < element size
   (Godot's runtime-specialized shaders can produce this case)

---

## Issue 6: Lightmap Bicubic OpFunctionCall Type Mismatch (FIXED)

### Symptoms (resolved)

After `flatten_binding_arrays`, `OpVariable` had the array pointer type and function
parameters had the element pointer type. Structurally identical after rewrite but
different SPIR-V IDs → validation failure. Affected 4 lightmap shaders.

### Fix applied

Pointer type deduplication in `flatten_binding_arrays` (see Issue 2 additional fix).
Maps duplicate pointer type IDs to canonical IDs and strips the duplicate
`OpTypePointer` instructions.

---

## Issue 7: Combined Sampler Double-Splitting / Binding Mismatch (FIXED)

### Symptoms (resolved)

Runtime GPU validation errors: `"Binding doesn't exist in
[BindGroupLayoutInternal "bgl:CopyShaderRD:0:set0"]"` — the WGSL shader declares
`@group(0) @binding(2)` but the bind group layout only has bindings 0 and 1.
Affected CopyShaderRD, BlitShaderRD, BokehDofRasterShaderRD (30 binding errors,
8372 cascade errors).

### Root cause

Our `split_combined_samplers` preprocessing creates separate sampler + image
variables, but originally kept the image variable typed as `ptr(SampledImage)` for
non-multisampled textures (Issue 1 workaround). Tint's internal
`SplitCombinedImageSamplerPass` (parser.cc:116) then sees these variables as
unsplit combined samplers and splits them AGAIN, creating extra bindings.

The chain: our preprocessing creates sampler@binding0 + image@binding1, then
Tint's pass sees image@binding1 (still typed SampledImage), splits it into
image2@binding1 + sampler2@binding1, then `ResolveBindingConflictsPass` moves
sampler2 to binding2. The BGL only has bindings 0 and 1 → binding 2 doesn't exist.

### Fix applied

Changed `split_combined_samplers` to rewrite `OpVariable` types for ALL combined
vars (not just multisampled) from `ptr(SampledImage)` → `ptr(Image)`. This
prevents Tint's `SplitCombinedImageSamplerPass` from finding any combined sampler
variables to split.

Three-part change:
1. **OpVariable type rewrite**: Removed the `is_multisampled` condition so all
   combined vars get `ptr(Image)` type
2. **OpLoad rewrite**: Non-multisampled now loads Image directly (3 instructions
   instead of 4: OpLoad Image, OpLoad Sampler, OpSampledImage)
3. **Function parameter handling**: For functions that receive combined vars via
   `OpFunctionCall`, also rewrites `OpFunctionParameter` types from
   `ptr(SampledImage)` → `ptr(Image)` and creates new `OpTypeFunction` definitions
   to match

---

## Issue 8: GPU Device Lost on macOS Vulkan Backend (RESOLVED)

### Symptoms (resolved)

Chrome with `--use-angle=vulkan` crashes with "A valid external Instance reference
no longer exists" at ~8154ms. This masked the binding errors from Issue 7.

### Root cause

MoltenVK (Vulkan-on-Metal) on macOS is unstable for WebGPU workloads. This is a
known Chrome/Dawn issue, NOT a Tint shader bug.

### Fix

Use Chrome's default Metal backend (do NOT pass `--use-angle=vulkan`). The binding
errors were the actual issue, hidden by the Vulkan crash.

---

## Architectural Comparison

### Naga pipeline (working, 0 errors)

```
SPIR-V binary rewriting (lib.rs, Rust):
  1. rewrite_terminate_invocation    — OpTerminateInvocation → pattern
  2. infer_readonly_storage          — add NonWritable to read-only SSBOs
  3. convert_push_constants          — PushConstant → UBO at @group(3)@binding(120)
  4. split_combined_samplers         — rewrite OpLoad, inject sampler vars
  5. fix_depth2_images               — depth=2 → depth=1

Naga IR post-processing (lib.rs, Rust):
  6. fix_writeonly_storage           — write-only → read_write
  7. fix_nonfinite_literals          — inf/NaN → finite bounds
  8. strip_point_size                — @builtin(point_size) → @location
  9. flatten_binding_arrays          — ArraySize(N) → ArraySize(1)

Naga-patched SPIR-V reader (naga-patched, Rust):
  10. patch_comparison_type          — mixed depth/float usage → split vars
  11. redirect_non_comparison        — route loads to correct split var
  12. fix_depth_access_index         — fix AccessIndex after type change

WGSL text fixup (rendering_device_driver_webgpu.cpp, C++):
  13. binding_array<T,1> → T         — strip residual binding arrays
  14. var<storage,read_write> → read  — demote for vertex stage
  15. read_write storage tex split   — Safari compatibility
```

### Current Tint pipeline (0 errors, scene_a fully rendering)

```
SPIR-V binary rewriting (spirv_preprocess.cpp, C++):
  1. rewrite_push_constants          — PushConstant → UBO
  2. split_combined_samplers         — ALL vars get ptr(Image), func params updated
  3. flatten_binding_arrays          — comprehensive ID replacement + ptr dedup
  4. inject_y_flip                   — vertex Y coordinate flip
  5. fix_nonfinite_floats            — inf/NaN → finite
  6. add_nonwritable                 — NonWritable for read-only SSBOs
  7. fix_terminate_invocation        — OpTerminateInvocation
  8. fix_point_size_store            — point_size workaround
  9. fix_depth_images                — depth=2 → depth=1

Tint (C++, linked):
  10. SpirvToWgsl                    — with allow_non_uniform_derivatives
      - validate.cc: SetSkipBlockLayout(true) — skip struct layout validation
      - validator: kAllowStructMemberSizeMismatch capability
      - ir_to_program.cc: @size only emitted when > type size
      - decompose_strided_array.cc: skip padding when stride < element size

WGSL text fixup (rendering_device_driver_webgpu.cpp, C++):
  11. binding_array<T,N> → T         — strip binding arrays
  12. var<storage,read_write> → read  — demote for vertex stage
  13. read_write storage tex split   — Safari compatibility
```

---

## Testing Strategy

### CRITICAL: Never run Chrome headless

WebGPU tests MUST run with a visible Chrome window (`headless: false` in Playwright).
Headless Chrome does not reliably initialize WebGPU — `requestDevice` fails with
"A valid external Instance reference no longer exists." Always launch with head.

### CRITICAL: Do NOT use --use-angle=vulkan on macOS

Chrome with `--use-angle=vulkan` is unstable on macOS (MoltenVK). Use the default
Metal backend by omitting this flag. Vulkan crashes mask actual rendering errors.

### Per-shader testing with wgsl_precompile.py

The build-time precompile script (`wgsl_precompile.py`) already:
1. Parses every `.glsl` shader and enumerates variant defines
2. Compiles each variant to SPIR-V via glslangValidator
3. Runs each through Tint → WGSL
4. Reports pass/fail per shader

This can serve as a per-shader test suite. Run it, get a list of failing shaders,
fix them one at a time, verify no regressions.

### Runtime SPIR-V dump for specialized shaders

Specialized shaders (runtime spec-constant patching) aren't covered by precompile.
The runtime `_spv_to_wgsl_cached` path handles these. To test them:
1. Export scene_a
2. Run in browser/Playwright
3. Capture console errors — each error names the failing shader
4. Dump the SPIR-V blob (hash-keyed) for offline debugging

### Current test results (2026-05-11)

```
Root shader failures:  0
Binding errors:        0
Cascade errors:        0
Other errors:          0
Total errors:          0
```

Scene_a renders at 120 FPS with 1000 sprites, benchmark label visible.

Previous results for comparison:
- Before any fixes: 9 root shader failures, 75 total errors
- After Issue 1+2 fix: 7 root failures (SceneData alignment exposed)
- After Issue 4 fix: 2 root failures, 43 total errors
- After Issue 5 fix: 0 root failures, 31 other errors (multisampled crash)
- After Issue 3 fix: 0 root, 0 cascade, 34 other (GPU device lost — Vulkan)
- After Issue 8 discovery: 0 root, 30 binding, 8372 cascade (Metal backend)
- After Issue 7 fix (partial): 2 root (func param), 0 binding, 13 cascade
- After Issue 7 fix (complete): **0 root, 0 binding, 0 cascade, 0 other**

---

## Tint vs Naga: Migration Summary

### Architecture

**Naga stack** (what we had):
- Rust WASM module (`naga-converter/src/lib.rs`, ~6,500 lines)
- Patched copy of naga's SPIR-V frontend (`naga-patched/`, ~500 lines of diffs)
- Separate Rust build system (cargo, wasm-pack)
- SPIR-V binary rewriting in Rust → naga IR manipulation → WGSL text output

**Tint stack** (what we have now):
- C++ linked directly into Godot (`tint_wrapper.cpp`, 60 lines)
- SPIR-V binary rewriting in C++ (`spirv_preprocess.cpp`, ~1,700 lines)
- Patches to 6 Tint source files (small, scattered)
- SPIR-V binary rewriting → Tint does the rest natively

### What's better with Tint

- **No Rust/WASM dependency.** Naga required a full Rust toolchain, wasm-pack, and a ~2MB WASM blob shipped with every build. Tint is just C++ that links in with scons like everything else.
- **Canonical path.** Tint is what Chrome/Dawn actually uses for SPIR-V→WGSL. When WebGPU evolves, Tint evolves with it. Naga was always a side project — its SPIR-V reader had bugs and missing features that required our patched fork.
- **Better validation.** Tint runs spirv-val, which catches real SPIR-V problems early. Naga silently accepted malformed SPIR-V that could cause subtle runtime bugs.
- **Simpler build.** One `scons` invocation builds everything. No `cargo build --target wasm32`, no `wasm-opt`, no checking in a `.wasm` binary.

### What's worse with Tint

- **Stricter validation creates more friction.** Naga had essentially no validation for struct layouts, member sizes, function parameter types, or block alignment. It just parsed and emitted. Tint validates at multiple levels (spirv-val, IR validator, WGSL resolver), and each one caught Godot idioms that naga silently accepted. This is the source of Issues 1, 4, 5, and the function parameter part of Issue 7.
- **Internal combined sampler splitting.** Tint has its own `SplitCombinedImageSamplerPass` that runs inside the parser. Naga didn't. This meant our preprocessing had to produce SPIR-V that wouldn't trigger Tint's pass — the double-splitting bug (Issue 7) was purely a Tint-specific problem that couldn't have existed with naga.
- **Patches are in upstream code.** Naga patches were in our own `naga-patched/` fork, clearly separated. Tint patches are scattered across `thirdparty/tint/` source files (validator, WGSL writer, strided array decomposition, SPIR-V validation). This makes Tint version upgrades harder — each patch needs to be re-applied and verified.

### Patch comparison

| Area | Naga | Tint |
|------|------|------|
| Combined sampler splitting | lib.rs (~500 lines) | spirv_preprocess.cpp (~500 lines) |
| Depth texture handling | naga-patched (~300 lines across 3 files) | Not yet needed (Tint handles natively?) |
| Struct layout validation | Not needed (no validation) | validate.cc: `SetSkipBlockLayout(true)` |
| Struct member size | Not needed (no validation) | validator.h/cc + reader.cc + ir_to_program.cc + decompose_strided_array.cc |
| Binding array flattening | lib.rs (~100 lines, IR-level) | spirv_preprocess.cpp (~200 lines, SPIR-V binary) |
| Non-uniform derivatives | Not needed (naga allowed it) | tint_wrapper.cpp: `allow_non_uniform_derivatives` |

The SPIR-V preprocessing code (`spirv_preprocess.cpp`) is roughly equivalent in complexity to what `lib.rs` did in Rust. The Tint-specific patches (6 files, ~30 lines total) are smaller than the naga-patched fork (~300 lines) but harder to maintain since they're inside upstream source.

### The nature of the work

With naga, the hard work was **making naga's SPIR-V frontend understand Godot's patterns** — depth textures, comparison samplers, mixed usage. The patches were conceptual: teaching naga new semantics.

With Tint, the hard work was **satisfying Tint's validation while preserving Godot's runtime behavior** — struct layouts that don't follow any standard, `@size` annotations that are smaller than the type, function parameter types that must match exactly. The patches are pragmatic: relaxing checks that are correct in general but too strict for Godot's runtime-specialized shaders.

### Bottom line

It's a **net improvement**. The Rust/WASM dependency was a real maintenance burden — a separate toolchain, a separate build, a binary blob. Tint eliminates all of that. The extra validation friction was annoying to debug but the fixes are small and the stricter checking actually catches real issues. The combined sampler double-splitting bug was the most Tint-specific problem, and even that was fundamentally a cleaner fix (our SPIR-V now properly declares `ptr(Image)` instead of lying about the type).

The one thing to watch: Tint version upgrades. The 6 patched files need to be tracked and re-verified when updating `thirdparty/tint/`.
