# Shader Pipeline Review: SPIR-V to WGSL Translation

## Overview

The godot-webgpu shader pipeline converts Godot's SPIR-V bytecode (produced by
glslang from Vulkan-flavour GLSL) into WGSL at runtime in the browser. The
translation is performed by a Rust/WASM library ("naga-converter") that wraps a
patched version of naga v28.0.0. The C++ driver invokes this via a JavaScript
bridge (`window.nagaSpirvToWgsl`), caches results by SPIR-V hash, and applies
additional WGSL string-level post-processing for format remapping and browser
compatibility.

---

## 1. Complete Flow: SPIR-V Input to WGSL Output

```
GLSL (Godot shaders)
    |
    v  [glslang, SPIR-V 1.3]
SPIR-V bytecode (stored in RenderingShaderContainerWebGPU)
    |
    v  [C++: shader_create_from_container / _create_module_with_spec_constants]
    |
    +-- Specialization constant patching (C++ side, for pipeline variants)
    |
    v  [C++: _spv_to_wgsl_cached()]
    |
    +-- 64-bit hash lookup (MurmurHash3 x2) -> cache hit returns WGSL
    |
    v  [JS bridge: MAIN_THREAD_EM_ASM -> window.nagaSpirvToWgsl()]
    |
    v  [WASM: naga-converter spirv_to_wgsl()]
    |
    +-- Pre-processing passes (SPIR-V binary rewriting):
    |     1. freeze_spec_constant_ops (evaluate OpSpecConstantOp)
    |     2. infer_readonly_storage (add NonWritable decorations)
    |     3. convert_push_constants_to_uniforms (PC -> StorageBuffer)
    |     4. split_combined_samplers (combined -> image + sampler)
    |     5. fix_depth2_images (depth=2 -> depth=1)
    |
    +-- naga SPIR-V parser (spv::parse_u8_slice)
    |     - adjust_coordinate_space: true (Y-flip for Vulkan->WebGPU NDC)
    |
    +-- Post-parse module fixups:
    |     1. fix_writeonly_storage (write-only -> read_write)
    |     2. fix_nonfinite_literals (Inf/NaN -> f32::MAX/MIN)
    |     3. strip_point_size (PointSize builtin -> unused @location)
    |     4. flatten_binding_arrays (binding_array<T, N> -> size 1)
    |
    +-- naga validation (Capabilities::all())
    |
    +-- process_overrides (resolve pipeline-overridable constants)
    |
    +-- Clear overrides arena (WGSL writer rejects non-empty)
    |
    +-- naga WGSL writer
    |
    +-- WGSL post-processing:
    |     1. Replace "var<push_constant>" fallback text
    |     2. fix_fmax_literals (f32::MAX decimal -> hex float)
    |     3. Strip "enable f16;" directive
    |     4. Prepend "diagnostic(off, derivative_uniformity);"
    |     5. Prepend SSBO_USED metadata comments
    |
    v  [Returns WGSL string to JS/C++]
    |
    v  [C++: additional WGSL text transforms]
    |
    +-- Format remapping (r8/rg8/r16/rg16 storage formats -> 32-bit)
    +-- read_write storage demote for vertex/fragment stages
    +-- binding_array flattening (WGSL text-level)
    |
    v  [wgpuDeviceCreateShaderModule with WGPUShaderSourceWGSL]
```

---

## 2. Naga Converter Architecture

### 2.1 Entry Point and FFI Boundary

**File:** `drivers/webgpu/naga-converter/src/lib.rs`

The converter is a single-file Rust library compiled to a `cdylib` WASM module
via `wasm-pack`. The only public API is:

```rust
#[wasm_bindgen]
pub fn spirv_to_wgsl(spirv_bytes: &[u8]) -> Result<String, JsError>
```

It uses `wasm_bindgen` for the JS<->WASM interface. Logging goes to
`console.log` via `#[wasm_bindgen] extern "C" { fn log(s: &str); }`.

The compiled WASM binary is committed at:
`drivers/webgpu/naga-converter/out/naga_converter_bg.wasm`

### 2.2 JavaScript Bridge

**File:** `platform/web/js/engine/engine.js` (lines 297-393)

`Engine.loadNagaSpirvToWgsl()` fetches the WASM binary, instantiates it with a
minimal wasm-bindgen runtime (inlined to avoid ES module syntax), and exposes
`window.nagaSpirvToWgsl(Uint8Array) -> string`.

### 2.3 C++ Invocation

**File:** `drivers/webgpu/rendering_device_driver_webgpu.cpp` (lines 83-141)

`_spv_to_wgsl_cached()` wraps the JS call with:
- A 64-bit hash key (two MurmurHash3 passes with different seeds)
- Process-lifetime HashMap cache (~1k entries max)
- EM_ASM_PTR block that calls `window.nagaSpirvToWgsl()`
- malloc'd string return (caller must free)

### 2.4 Pre-processing Passes (SPIR-V Binary Rewriting)

All five passes operate on raw SPIR-V byte buffers before naga parsing:

#### 2.4.1 freeze_spec_constant_ops

**Problem:** Naga does not support `OpSpecConstantOp`. These instructions compute
constant values from other constants via arithmetic/logical/bitwise operations.
Without handling, naga cascades `InvalidId` errors.

**Solution:** Evaluates each `OpSpecConstantOp` using previously-collected
constant values and emits regular `OpConstant` instructions. Strips
`OpDecorate ... SpecId` decorations. Rewrites `OpSpecConstant{True,False}` to
their non-spec variants.

**Evaluator coverage:** Integer arithmetic (negate, add, sub, mul, div, mod),
logical ops, select, integer comparisons, bitwise ops, composite extract, and
type conversions. Returns 0 for unhandled opcodes.

#### 2.4.2 infer_readonly_storage

**Problem:** glslang does NOT emit `OpDecorate NonWritable` for
`restrict readonly buffer` blocks. Naga defaults all SSBOs to
`var<storage, read_write>`, causing writable-aliasing validation errors when
Godot binds a single placeholder buffer in multiple writable slots.

**Solution:** Multi-pass analysis:
1. Collect all StorageBuffer variables
2. Track pointer derivations (OpAccessChain) and stores (OpStore, atomics, 
   function calls passing buffer pointers)
3. Variables never written to get `OpDecorate NonWritable` injected

#### 2.4.3 convert_push_constants_to_uniforms

**Problem:** Naga v28 silently drops `PushConstant` variables from WGSL output.

**Solution:** Rewrites push-constant variables to read-only storage buffers at
`@group(3) @binding(120)`. Specifically:
- Changes OpTypePointer from PushConstant to StorageBuffer
- Changes OpVariable storage class
- Injects DescriptorSet/Binding/NonWritable decorations
- The C++ side later does a text replacement of any remaining
  `var<push_constant>` to `var<storage, read>` as a safety net

The binding 120 is chosen high enough to never collide with split combined-sampler
bindings (max ~41 after doubling).

#### 2.4.4 split_combined_samplers

**Problem:** Naga's SPIR-V frontend doesn't handle `OpLoad` of combined image
sampler variables. It expects separate image/sampler loads followed by
`OpSampledImage`.

**Solution:** Most complex pre-processing pass (~500 lines):
- Identifies combined image sampler variables (pointer to SampledImage type)
- Splits each into separate image variable (reuses original ID) and new sampler
  variable
- Binding convention: original binding N -> sampler at N*2, image at N*2+1
- Rewrites OpLoad to emit separate image + sampler loads + OpSampledImage
- Handles function parameters that receive combined vars via call sites
- Updates OpEntryPoint interface lists
- Doubles all other binding numbers to avoid collision (except PC ring buffer)

**Key design decision:** The original combined var is KEPT as the image var.
Removing it would break OpFunctionCall references.

#### 2.4.5 fix_depth2_images

**Problem:** SPIR-V `OpTypeImage` with depth=2 means "unknown comparison usage."
WGSL requires static typing as either `texture_depth_*` or `texture_*<f32>`.

**Solution:** Rewrites all depth=2 to depth=1, relying on WGSL's
`texture_depth_2d` supporting both `textureSample` (returns f32) and
`textureSampleCompare`.

### 2.5 Post-Parse Module Fixups

Applied to the naga Module before validation:

- **fix_writeonly_storage:** WGSL requires STORE-only buffers to also have LOAD.
  Upgrades write-only to read-write.
- **fix_nonfinite_literals:** Replaces Inf/NaN with f32::MAX/MIN. WGSL has no
  literal representation for infinity.
- **strip_point_size:** WGSL has no `@builtin(point_size)`. Remaps to unused
  `@location` outputs.
- **flatten_binding_arrays:** Reduces all `BindingArray` sizes to 1.
  Dawn/WebGPU doesn't support multi-element binding arrays through multiple bind
  group entries.

### 2.6 WGSL Post-Processing

- **fix_fmax_literals:** Naga writes f32::MAX as a decimal string that can
  overflow the WGSL parser. Replaces known problematic decimal representations
  with hex float `0x1.fffffep+127f`.
- **Strip `enable f16;`:** Chrome doesn't support shader-f16 yet.
- **Prepend `diagnostic(off, derivative_uniformity);`:** Suppresses WGSL errors
  from textureSample in non-uniform control flow.
- **SSBO_USED metadata:** Naga's validator identifies which storage buffers each
  entry point actually uses. Output as WGSL comments for C++ to parse for
  per-stage BGL visibility.

---

## 3. Naga Patches (What We Changed and Why)

The vendored naga is at `naga-patched/` (naga v28.0.0). Key modifications:

### 3.1 IO-Shareable Type Validation Relaxation

**File:** `naga-patched/src/valid/interface.rs` (lines 446-456)

**Change:** Commented out the `NotIOShareableType` check for `@location`
bindings.

**Rationale:** SPIR-V shaders from Godot pass booleans between stages via
`@location` bindings. WGSL requires IO-shareable types (excludes bool), but
Dawn/Tint handles this internally. The validator now allows any type.

### 3.2 Image Class Mismatch Tolerance (Function Arguments)

**File:** `naga-patched/src/valid/function.rs` (lines 354-375)

**Change:** Allows Depth <-> Sampled{Float} type mismatches in function call
arguments without emitting `CallError::ArgumentType`.

**Rationale:** After depth=2 splitting, function parameters may have
`texture_2d<f32>` type but receive a `texture_depth_2d` argument (or vice
versa), depending on how the comparison-type resolution proceeded.

### 3.3 TEXTURE Barrier Support

**File:** `naga-patched/src/ir/mod.rs` (line 1467)

**Change:** Added `const TEXTURE = 1 << 3;` to the `Barrier` bitflags struct.

**Files:** `naga-patched/src/back/wgsl/writer.rs` (line 939-940),
`naga-patched/src/front/spv/next_block.rs` (line 2519-2520),
`naga-patched/src/front/wgsl/lower/mod.rs` (lines 3038-3045)

**Change:** Added `textureBarrier()` emission in the WGSL writer and parsing in
both the SPIR-V frontend (from IMAGE_MEMORY semantics) and WGSL frontend.

**Rationale:** Godot compute shaders use image memory barriers
(`OpControlBarrier` / `OpMemoryBarrier` with `IMAGE_MEMORY` semantics).
Upstream naga does not have a TEXTURE barrier flag or `textureBarrier()` output.

### 3.4 Inconsistent Comparison Sampling Split

**File:** `naga-patched/src/front/spv/mod.rs` (lines 1890-1997)

**Change:** When a texture variable is used for BOTH comparison (depth_ref) and
non-comparison sampling, instead of rejecting it with
`InconsistentComparisonSampling`, the parser now:
1. Keeps the original as Depth type
2. Creates a clone at binding+1 as Float type
3. Redirects non-comparison ImageSample expressions to the clone

This is coordinated with `redirect_non_comparison_sampling()` (line 2083+)
and `fix_depth_access_index()` (line 2020+).

### 3.5 Function Parameter Depth Type Promotion

**File:** `naga-patched/src/front/spv/mod.rs` (lines 1852-1888)

**Change:** Function parameters that have COMPARISON-only sampling flags get
their type promoted from `Sampled{Float}` to `Depth`. Parameters with both
COMPARISON and REGULAR flags are left as `Sampled{Float}`.

### 3.6 Sampling Flags Propagation Through Access Chains

**File:** `naga-patched/src/front/spv/mod.rs` (lines 1620-1648)

**Change:** In `patch_statements`, when propagating sampling flags from function
call arguments to global variables, the code now follows `Access` and
`AccessIndex` expression chains to find the underlying `GlobalVariable`.

**Rationale:** Array-indexed images (e.g., `lightmap_textures[i]`) create
access chain expressions. Without this fix, sampling flags would not propagate
through binding arrays.

### 3.7 Divergence Risk Assessment

The patches modify:
- Validation (2 relaxations: IO-shareable, argument type matching)
- IR (1 addition: TEXTURE barrier flag)
- SPIR-V frontend (complex depth/comparison handling)
- WGSL backend (textureBarrier emission)
- WGSL frontend (textureBarrier parsing -- for round-trip correctness)

**Risk level: MODERATE.** The most complex change is the inconsistent-comparison
splitting logic. It adds ~300 lines of new code to the SPIR-V frontend that
maintains expression arena invariants and handles function call argument
redirection. Future naga upgrades will require careful porting.

The TEXTURE barrier addition is low-risk but will need tracking against upstream
naga's eventual native support for texture barriers.

---

## 4. Shader Container and Caching

### 4.1 RenderingShaderContainerWebGPU

**Files:** `drivers/webgpu/rendering_shader_container_webgpu.{h,cpp}`

Minimal container that stores raw SPIR-V bytes per stage. Key aspects:
- Format identifier: `0x57475055` ("WGPU")
- SPIR-V version: 1.3 (required for StorageClass::StorageBuffer)
- Language version: Vulkan 1.1
- Extra header data: push constant bind group (3) and binding (120)
- No compression applied (code_decompressed_size = 0)

The container comment notes that "Dawn's emdawnwebgpu port natively supports
WGPUShaderSourceSPIRV" -- however, the actual driver code
(rendering_device_driver_webgpu.cpp:3100) contradicts this:
"emdawnwebgpu does NOT support WGPUShaderSourceSPIRV". The container stores
SPIR-V which is then converted to WGSL at runtime.

### 4.2 SPIR-V to WGSL Cache

**Location:** `rendering_device_driver_webgpu.cpp` (lines 95-141)

- Static process-lifetime `HashMap<uint64_t, String>`
- 64-bit key from two MurmurHash3 passes with different seeds (0 and 0x9E3779B9)
- On hit: malloc+memcpy a fresh copy for the caller
- On miss: call JS bridge, store result
- Cache never evicted (~1k entries typical)
- Separate hit/miss counters for diagnostics

### 4.3 Specialization Constants

**Location:** `rendering_device_driver_webgpu.cpp` (lines 6430-6696)

`_patch_spirv_spec_constants()` modifies SPIR-V bytes in place:
- Maps SpecId decoration -> result_id
- Patches OpSpecConstantTrue/False opcodes based on provided values
- Patches OpSpecConstant literal values

`_create_module_with_spec_constants()` then:
1. Patches SPIR-V with specialization values
2. Runs the patched SPIR-V through the same cache/naga/post-processing pipeline
3. Creates a new WGPUShaderModule

This means specialized shaders get their own cache entries (different SPIR-V
bytes = different hash).

---

## 5. Browser Compatibility Layer

### 5.1 Firefox/wgpu Workarounds

1. **Empty bind groups for pipeline layout gaps:** Firefox requires all bind
   group indices to be set before draw calls. The driver creates a persistent
   empty bind group and binds it at gap slots. (lines 353-366, 6108-6112,
   7119-7122)

2. **Per-stage SSBO visibility (8-buffer Metal limit):** Firefox/wgpu enforces
   Metal's limit of 8 storage buffers per shader stage. The converter outputs
   `//SSBO_USED:group,binding` metadata comments which the driver parses to set
   per-stage BGL visibility. (lines 3419-3449, 3744-3791)

3. **Read_write storage demote for render stages:** WebGPU restricts read_write
   storage buffer access in vertex shaders. For vertex/fragment stages, all
   `var<storage, read_write>` are demoted to `var<storage, read>` via in-place
   string replacement. (lines 3188-3200)

### 5.2 Chrome/Dawn Workarounds

1. **binding_array flattening:** Chrome doesn't support the
   `sized_binding_array` WGSL language feature. Naga emits
   `binding_array<T, N>` for GLSL sampler arrays. Fix: WGSL text-level
   replacement of `binding_array<TYPE, N>` with just `TYPE`, and removal of
   array indexing `varname[expr]` -> `varname`. (lines 3202-3285)

   This degrades multi-lightmap scenes to single-element access.

2. **`enable f16;` stripping:** Chrome's shader-f16 feature is not yet
   supported. Naga emits this directive for modules using f16 types. Stripped
   unconditionally.

3. **Diagnostic patch on createShaderModule:** The driver installs a JS monkey-
   patch on `device.createShaderModule` to push an error scope, check
   compilation info, and log validation failures. (lines 552-579)

### 5.3 Format Remapping (All Browsers)

WebGPU only supports a limited set of storage texel formats. Multiple remapping
passes handle unsupported formats:

1. **8-bit formats (r8/rg8):** Remapped to 32-bit equivalents unless
   `texture-formats-tier1` is available.
2. **16-bit SNORM/UNORM:** `r16snorm/unorm` -> `r16float`, etc.
3. **16-bit storage formats:** `rg16*` -> `rg32*`, `r16*` -> `r32*` (WebGPU
   spec 26.1.1 restriction).

All remapping is done via in-place string manipulation on the WGSL text, which
relies on format name lengths being preserved (same char count).

### 5.4 derivative_uniformity Diagnostic Suppression

Prepended to all WGSL output:
```wgsl
diagnostic(off, derivative_uniformity);
```
Godot shaders call `textureSample()` inside non-uniform control flow (e.g.,
per-instance flags in if/else branches). This suppresses the WGSL validation
error.

### 5.5 f32::MAX Literal Fix

Naga writes f32::MAX as a decimal string that can round to a value above
f32::MAX, which the WGSL parser rejects. Known problematic representations are
replaced with hex float `0x1.fffffep+127f`.

---

## 6. Issues, Bugs, and Concerns

### 6.1 Potential Bugs

1. **Container comment contradicts driver code:** The shader container header
   comment says "Dawn's WebGPU implementation supports WGPUShaderSourceSPIRV
   natively; no WGSL/Tint translation step is needed." But the driver actually
   does SPIR-V->WGSL conversion. The comment should be corrected.

2. **Duplicated format remapping logic:** The format remapping code is
   copy-pasted between `shader_create_from_container` (line 3115+) and
   `_create_module_with_spec_constants` (line 6538+). Any fix in one must be
   manually applied to the other. This should be refactored into a shared
   helper.

3. **Cache key collision risk:** While a 64-bit hash is much better than 32-bit,
   at ~1k entries the birthday probability is ~2^-44 -- effectively zero in
   practice. The design is sound.

4. **freeze_spec_constant_ops evaluator gaps:** The evaluator returns 0 for
   unhandled opcodes (line 278). If Godot shaders ever use uncommon
   OpSpecConstantOp operations (e.g., float arithmetic), incorrect constant
   values would silently propagate. Currently only integer/logical/bitwise ops
   are handled.

5. **split_combined_samplers binding doubling:** All non-combined-sampler
   bindings are doubled (`old_binding * 2`). This halves the effective binding
   range. The PC ring buffer binding (120) is exempt. If a shader has bindings
   above ~60, the doubled value could exceed implementation limits.

6. **In-place format remapping relies on string length equality:** The format
   name replacements (e.g., `rg16float` -> `rg32float`, both 9 chars) work
   because the replacement has the same length. If new format names are needed
   that don't have equal-length replacements, the approach breaks.

### 6.2 Architectural Concerns

1. **Single-threaded main-thread conversion:** The naga WASM conversion runs on
   the main thread (~40ms per stage, ~383 stages = ~15s startup). This is noted
   in comments and is the dominant startup cost. A Web Worker approach would
   unblock the main thread but would require async plumbing.

2. **WGSL text scanning for metadata:** The driver scans raw WGSL text with
   `strstr` / `sscanf` to extract texture dimensions, access modes, depth
   types, and alias bindings. This is fragile -- any change in naga's output
   formatting could break parsing. A structured metadata channel (e.g., JSON
   comments or a separate return value from the converter) would be more robust.

3. **Naga patch divergence:** The patched naga is at v28.0.0. Upstream naga
   development continues. The patches touch validation, IR, and frontends --
   areas that change frequently. Each naga upgrade will require re-applying and
   potentially redesigning these patches.

4. **No fallback on conversion failure:** If `spirv_to_wgsl` fails for any
   stage, the entire shader creation fails. There's no graceful degradation
   or fallback shader mechanism.

### 6.3 Missing Error Context

The `dump_spirv_around_error` function logs the entire SPIR-V hex dump on parse
failures (lines 1434-1486). This is useful for debugging but:
- Dumps ALL instructions for shaders under 1000 words (could be large)
- Hex dump of full SPIR-V creates massive console output
- No structured error reporting back to the engine's error system

---

## 7. Summary of Findings

### What Works Well

- The pipeline is thoroughly engineered with clear separation: SPIR-V binary
  transforms handle naga's limitations, module-level fixups handle WGSL gaps,
  and text-level post-processing handles browser quirks.
- The caching strategy is effective: identical SPIR-V bytes (common for
  specialization variants sharing a base) avoid redundant naga invocations.
- The combined sampler splitting is sophisticated and handles edge cases
  (function parameters, access chains, binding collision avoidance).
- SSBO visibility metadata leverages naga's call-graph analysis to stay within
  Metal's per-stage limits.

### Key Risks

- **Maintenance burden:** ~1700 lines of SPIR-V binary manipulation in lib.rs +
  ~500 lines of naga patches. Both need updating if Godot's shader compiler
  output changes or naga is upgraded.
- **Performance:** Main-thread blocking conversion is the #1 startup cost.
- **Fragility:** WGSL text scanning for metadata depends on naga's exact output
  format.

### Recommendations

1. **Extract format remapping into a shared function** to eliminate the
   copy-paste between shader_create_from_container and
   _create_module_with_spec_constants.

2. **Fix the misleading container comment** about WGPUShaderSourceSPIRV support.

3. **Consider moving WGSL metadata extraction** (texture dims, access modes,
   SSBO info) into the naga-converter itself, returned as structured data
   alongside the WGSL string. This would be more robust than text scanning.

4. **Add float OpSpecConstantOp evaluation** to freeze_spec_constant_ops if
   Godot shaders ever use float-typed specialization constant operations.

5. **Track upstream naga TEXTURE barrier support.** When/if naga adds this
   natively, the patch can be dropped.

6. **Consider Web Worker offloading** for the naga conversion to unblock the
   main thread during startup (significant architectural change, may require
   async pipeline creation).

7. **Add validation that doubled bindings don't exceed limits** in
   split_combined_samplers (e.g., assert binding * 2 < 128 or the device's
   maxBindingsPerBindGroup).
