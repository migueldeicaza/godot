# Porting the WebGPU backend from 4.6.2 to 4.7

Status: in progress. Branch `webgpu-4.7`, forked from the 4.7 branch tip
(`f6ab5db28b`, version 4.7.3-rc).

Source of the port: `../godotwebgpu`, branch `webgpu-4.6.2`. Fetched into this
repository as `refs/wgpu/webgpu-4.6.2`:

```
git fetch --no-tags /Users/miguel/cvs/master-godot/godotwebgpu \
    webgpu-4.6.2:refs/wgpu/webgpu-4.6.2
```

## 1. What is being ported

`webgpu-4.6.2` forked at `4.6-stable` and later merged the 4.6.1 and 4.6.2
maintenance cherry-picks. The WebGPU delta proper is the 167 commits on top of
`4.6.2-stable`: 1448 added files, 54 modified upstream files. Only 14 of those
167 commits touch shared engine code.

| Bucket | Size | Port risk |
| --- | --- | --- |
| `drivers/webgpu/` (26 files) | 17K lines; 8856 in `rendering_device_driver_webgpu.cpp` | Must be rebuilt against 4.7 interfaces |
| `thirdparty/{tint,spirv-tools,spirv-headers}` | 1224 files, 363K lines, 580 `.cc` compiled **into the engine** | Drop-in, except the spirv-headers collision below |
| `webgpu_tests/`, `webgpu_notes/`, `webgpu_site/` | ~60K lines | Self-contained; `preprocessing_tests` is the key verification asset |
| 54 modified engine files | +1900 / -250 | The actual porting work |

Tint and SPIRV-Tools are **linked into the engine binary**
(`drivers/webgpu/SCsub` appends `tint_obj` and `spirv_tools_obj` to
`env.drivers_sources`), and the Tint subset is compiled with C++20 in a cloned
SCons environment so its headers never leak into the C++17 Godot build.
`wgsl_precompile.py` generates `wgsl_precompiled.gen.h` at build time using a
host-built `tint_cli`.

## 2. The textual merge is nearly free

`git merge-tree --write-tree --merge-base=4.6.2-stable HEAD
refs/wgpu/webgpu-4.6.2` yields **12 conflicting files, 14 hunks, ~230 lines**.
Every one is a "both sides added at the same place" adjacency; all resolve by
keeping both sides.

| File | Hunks | Resolution |
| --- | --- | --- |
| `servers/rendering/rendering_device.cpp` | 6 | Keep both. 4.7's raytracing `BUFFER_CREATION_ACCELERATION_STRUCTURE_*` flags must be set *before* the WebGPU `API_TRAIT_BUFFER_CREATE_MAPPED_AT_CREATION` if/else that chooses `buffer_create_with_data` vs `buffer_create`. |
| `servers/rendering/rendering_device_driver.h` | 1 | Concatenate 4.7's 4 new raytracing API traits with WebGPU's 8 new traits. Order is irrelevant; both are appended before the closing brace. |
| `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.cpp` | 1 | Keep the `#ifdef WEB_ENABLED` block forcing `using_subpass_post_process = false`, then 4.7's `RSE::ViewportMSAA` lines (not `RS::`). |
| `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp` | 1 | Keep 4.7's `RSE::BLEND_SHAPE_MODE_NORMALIZED` line *and* WebGPU's `push_constant.bone_offset` line; drop `push_constant.pad1 = 0` only if `bone_offset` occupies that slot. |
| `servers/rendering/renderer_rd/storage_rd/light_storage.h` | 1 | Keep `is_force_omni_dual_paraboloid()`, then 4.7's `RSE::LightType light_get_type`. |
| `servers/rendering/renderer_rd/renderer_compositor_rd.cpp` | 1 | Take 4.7's `BlitPipelines` lines, and carry WebGPU's `Color(0, 0, 0, 1)` clear argument onto 4.7's `draw_list_begin_for_screen` call. |
| `servers/rendering/renderer_rd/shaders/environment/sdfgi_direct_light.glsl` | 1 | `1e20` -> `1e6`, keep 4.7's `texture_color` line. |
| `platform/web/detect.py` | 2 | 4.7 has already landed **both** halves of this change upstream, verbatim — the `EnumVariable` option *and* the auto/yes/extra `configure()` block. Both hunks take HEAD; the WebGPU copy is redundant and was dropped. |
| `README.md` | 1 | Took the WebGPU project README; Godot's is preserved as `GODOT_README.md` by the delta. |
| `thirdparty/README.md` | 1 | Rewrote the `## spirv-headers` entry to describe the actual hybrid (see 3.5); kept the new `## spirv-tools` and `## tint` entries. |
| `thirdparty/spirv-headers/include/spirv/unified1/spirv.hpp11` | add/add | See section 3.5. |

## 3. The real work: 4.7 semantic drift

### 3.1 New pure virtuals (blocking; link errors)

4.7 added raytracing and HDR output as **pure virtual** members (24 in
total: 15 + 9). The WebGPU
driver must implement all of them; WebGPU supports neither feature, so every one
is a stub returning an invalid ID / `false` / `ERR_FAIL`.

* `RenderingDeviceDriver` — 15 new `= 0` methods: `blas_create`, `tlas_create`,
  `acceleration_structure_instance_write`, `acceleration_structure_free`,
  `acceleration_structure_get_scratch_size_bytes`,
  `raytracing_pipeline_create`, `raytracing_pipeline_free`,
  `raytracing_pipeline_get_shader_group_handles`, `command_build_blas`,
  `command_build_tlas`, `command_bind_raytracing_pipeline`,
  `command_bind_raytracing_uniform_set`, `command_trace_rays`,
  `swap_chain_get_color_space`, `swap_chain_get_hdr_output_supported`.
* `RenderingContextDriver` — 9 new `= 0` methods: the
  `surface_{set,get}_hdr_output_*` family plus
  `surface_get_hdr_output_max_value`.
* `DisplayServer::VSyncMode` was renamed to `DisplayServerEnums::VSyncMode`
  (7 references in `drivers/webgpu/rendering_context_driver_webgpu.{h,cpp}`).

Authoritative list: `git diff 4.6.2-stable HEAD -- servers/rendering/rendering_device_driver.h servers/rendering/rendering_context_driver.h`.

### 3.2 `SHADER_STAGE_MAX` grew from 6 to 11

`drivers/webgpu/webgpu_objects.h:142` hardcodes
`WGPUShaderModule stage_modules[6]` with a comment asserting
`SHADER_STAGE_MAX = 6`. 4.7 inserted `SHADER_STAGE_{RAYGEN,ANY_HIT,
CLOSEST_HIT,MISS,INTERSECTION}` before `SHADER_STAGE_MAX`. The array is indexed
by stage, so it must become `[RDD::SHADER_STAGE_MAX]`.

### 3.3 Shader container reflection changed shape

`RenderingShaderContainer`: `uint32_t is_compute` was replaced by
`RDC::PipelineType pipeline_type` (`PIPELINE_TYPE_{RASTERIZATION,COMPUTE,
RAYTRACING}`, explicitly 4 bytes for shader alignment), and a `pipeline_type`
field was added to the reflection struct. Affects
`drivers/webgpu/rendering_shader_container_webgpu.{h,cpp}` and the codegen in
`drivers/webgpu/wgsl_precompile.py`.

### 3.4 glslang 1.3.283 -> 1.4.335 (the long pole)

4.7 bumped vendored glslang from `vulkan-sdk-1.3.283.0` (2024) to
`vulkan-sdk-1.4.335.0` (2025). The SPIR-V that Godot now emits differs in
version, capabilities and decoration patterns from what the WebGPU pipeline was
developed against. `drivers/webgpu/spirv_preprocess.cpp` is 2674 lines of
hand-written SPIR-V passes feeding Tint's SPIR-V reader; new constructs may fall
through them or through Tint.

This is the only item that cannot be sized from static analysis. Mitigation:
the branch ships 157 SPIR-V preprocessing tests plus cargo/libFuzzer targets
under `webgpu_tests/preprocessing_tests` — run them first; they localize
failures quickly.

### 3.5 spirv-headers collision — RESOLVED, per file, and neither way round

4.7 vendors 4 files (`vulkan-sdk-1.4.335.0`, 2025); the WebGPU branch vendors 11
(git `ad9184e7`) because SPIRV-Tools needs `DebugInfo.h`, `NonSemantic*.h`,
`OpenCL*.h` and `GLSL.std.450.h`. It took two attempts to get this right, and
the lesson is that "which snapshot is newer" is the wrong question — the two
snapshots diverged, and they diverged *differently per file*.

| File | Enumerants only in 4.7 | Only in WebGPU | Keep |
| --- | --- | --- | --- |
| `spirv.h` | 169 | 0 | **4.7** |
| `spirv.hpp` | 169 | 0 | **4.7** |
| `spirv.hpp11` | 0 | **43** | **WebGPU** |
| the 7 extra headers | — | only copy | WebGPU |

`spirv.h`/`spirv.hpp` carry the INTEL -> ALTERA vendor rename and newer
extensions that `glslang` 1.4.335, `spirv-reflect` and `re-spirv` compile
against; 4.7 also retains all 985 `INTEL` aliases, so nothing regresses.
`spirv.hpp11` is the opposite: it carries `OpTypeBufferEXT`,
`OpMemberDecorateIdEXT`, `OpSpecConstantDataKHR`, `OpBufferPointerEXT`,
`OpUntypedImageTexelPointerEXT` and friends, which the vendored SPIRV-Tools
references directly and 4.7's copy lacks. Both `hpp11` copies declare the same
74 enum-class types and the same SPIR-V version `0x00010600`, and glslang's six
SPIRV translation units — the other consumer of `hpp11` — compile clean against
the WebGPU copy.

Verify a change here by checking all three files independently. Comparing one
and generalising is exactly the mistake that cost a build cycle.

### 3.6 GLSL workaround sweep

The branch patches engine shaders around WGSL gaps. 4.7 added ~2 shaders
(100 -> 102 under `servers/`) and edited many others, so the same classes have
to be re-swept. Known classes and current 4.7 counts:

| Class | Fix | 4.7 occurrences |
| --- | --- | --- |
| `isnan(x)` / `isinf(x)` (absent in WGSL) | `notEqual(x, x)` / `greaterThan(abs(x), vec4(3.0e+10))` | 8, in 3 files |
| `1e20`-scale literals (f32 overflow) | clamp to `1e6` | 4 files |
| `modf()` | `side = floor(v); v = v - side;` | 7 |
| Varying arrays `out vec4 offset[3]` | scalarize to `offset0/1/2` | 4 |
| `set = 3, binding = 0` | reserved for the push-constant emulation UBO; textures start at binding 1 | see `canvas_uniforms_inc.glsl` |

### 3.6b Silent auto-merge casualties (found by building, not by merging)

Two changes merged *cleanly* and were still wrong. Both are the same shape: 4.7
changed a signature, and the WebGPU delta added a **new call site or override**
using the old one, so there was no textual conflict to resolve.

1. `command_pipeline_barrier` gained a 7th parameter in 4.7
   (`VectorView<AccelerationStructureBarrier>`). The WebGPU delta added a call
   in `RenderingDevice::_texture_initialize_layered` with the old 6-argument
   form, and `RenderingDeviceDriverWebGPU` declares/defines its override with
   6 parameters. Both fixed.
2. `RS::LIGHT_OMNI_SHADOW_DUAL_PARABOLOID` in `light_storage.cpp` — the single
   `RS::`-namespaced enum reference the delta adds (see 3.7).

The macOS build catches the first kind only where non-WebGPU code is involved.
For `drivers/webgpu/` itself there is no compiler available without emscripten,
so signatures there must be checked statically: parse every `virtual ...
override` in the WebGPU driver headers and compare its arity against the
matching declaration in `servers/rendering/rendering_device_driver.h`,
`rendering_context_driver.h` and `rendering_shader_container.h`. Doing that
found the `command_pipeline_barrier` override and confirmed the context driver
and shader container have **no** arity drift — only the missing methods of 3.1.
Re-run that check after every rebase onto a newer 4.7.

### 3.3 addendum

The WebGPU shader container never references `is_compute` or `pipeline_type`
directly, so the `RDC::PipelineType` change costs nothing there. Only
`_set_code_from_spirv(const ReflectShader &)` sees the new field, and WebGPU is
never handed raytracing stages.

### 3.7 `RS::` -> `RSE::` enum namespace split

4.7 moved rendering enums into `RenderingServerEnums` (`#define RSE
RenderingServerEnums`). `RS::` now resolves only to `RenderingServer` class
members. Inside `servers/rendering/renderer_rd/` there are 1403 `RSE::` uses and
just 9 remaining `RS::` uses (all `RS::get_singleton()`). The WebGPU delta adds
exactly one offending reference (`RS::LIGHT_OMNI_SHADOW_DUAL_PARABOLOID`), but
auto-merged hunks land beside renamed code, so a compile pass is needed to catch
stragglers.

### 3.8 A third drift axis: emdawnwebgpu

The port has three independent sources of drift, not two:

1. Godot 4.6.2 -> 4.7 (sections 3.1-3.7).
2. Bundled glslang 1.3.283 -> 1.4.335, changing the SPIR-V fed to Tint (3.4, and
   section 7 below).
3. **emdawnwebgpu / Dawn moving with the Emscripten version.** Nothing to do
   with Godot. A newer Dawn added a `WGPUStringView` message parameter to
   `WGPUQueueWorkDoneCallback`, breaking `_fence_work_done_callback`. Only that
   one callback was affected (the map-async callback already had the newer
   shape), but expect more of this whenever the Emscripten floor moves.

## 4. Build wiring

* `SConstruct`: one line — `opts.Add(BoolVariable("webgpu", ...))`.
* `drivers/SCsub`: `if env["webgpu"]: SConscript("webgpu/SCsub")` under
  "Graphics drivers".
* `platform/web/detect.py`: adds `"supported": ["webgpu"]` to `get_flags()`, and
  in `configure()` defines `WEBGPU_ENABLED` + `RD_ENABLED` and passes
  `--use-port=emdawnwebgpu` to both `CCFLAGS` and `LINKFLAGS`.
* `modules/glslang/config.py`: `can_build` must also return true for
  `env["webgpu"]`.

Emscripten 4.0.10+ is required for the `emdawnwebgpu` port; 4.7 already sets a
4.0.0 floor. Verified working with Emscripten **6.0.9**.

Host tool dependencies, neither of which the repository documents:

* **`glslangValidator`** — the WGSL precompile step shells out to it to turn the
  70 engine shaders into SPIR-V. Without it the build dies with
  `glslangValidator: Permission denied`, which does not name the real problem.
  Installed here via `brew install glslang` (16.5.0). See section 7 for why this
  binary is only a bootstrap.
* **`tint_convert_cli`** — built from source by `drivers/webgpu/SCsub` during the
  build; no action needed, but it links ~570 objects and is not cheap.

## 5. Verification strategy

Because there is no local emscripten, the port is verified in two stages.

Stage A — now, without emscripten:

1. macOS editor build (`vulkan=no`, Metal path) must still compile and link.
   **Passing as of `a152908809`**: `scons platform=macos target=editor
   vulkan=no metal=yes webgpu=no` builds and links `bin/godot.macos.editor.arm64`,
   and `--headless --quit` runs clean. Note that the background-task wrapper
   reported exit 0 for a *failed* SCons run, so always confirm with
   `scons: done building targets.` and the presence of the binary rather than
   trusting the exit code.
   This exercises every shared-engine change: `rendering_device.cpp`,
   `rendering_device_driver.h`, `rendering_device_graph.*`, the renderer_rd
   storage classes, and all the patched GLSL. The Metal driver implements the
   new raytracing virtuals, so the RDD interface is exercised for real.
2. `python -m py_compile` / a `scons platform=web ... --dry-run` style parse of
   `platform/web/detect.py` and `drivers/webgpu/SCsub` to catch syntax and
   option-wiring errors.
3. `webgpu_tests/preprocessing_tests` (157 SPIR-V pass tests) built natively.

Stage B — after installing emsdk (>= 4.0.10):

4. `scons platform=web webgpu=yes target=template_debug`.
5. `webgpu_tests/scene_smoketest`, then `screenshot_comparison`.
6. Benchmarks under `webgpu_tests/benchmark`.

## 6. Sequencing

| Step | Work | Status |
| --- | --- | --- |
| 0 | Branch `webgpu-4.7`; fetch `refs/wgpu/webgpu-4.6.2` | **done** |
| 1 | Apply the delta 3-way; resolve the 12 conflicts | **done** `5e065e95cf` |
| 2 | Reconcile `thirdparty/spirv-headers` | **done** `5e065e95cf`, corrected `1cdb49c4b4` |
| 3 | The 24 new pure virtuals; `VSyncMode` rename | **done** `b9729024b6` |
| 4 | `SHADER_STAGE_MAX`; emdawnwebgpu callback; HDR stub hardening | **done** `b9729024b6`, `1cdb49c4b4` |
| 5 | Stage A: macOS build green, **web build green** | **done** `1cdb49c4b4` |
| 6 | GLSL sweep (empty) + precompile registry resync | **done** `579b841bb3` |
| 7 | SPIR-V -> WGSL failures: 14 -> **10**; regenerate the precompiled table | in progress `7ebe73ccdd` |
| 8 | Stage B: scene smoketest, screenshots, benchmarks | open |

Both builds now pass:

```
scons platform=macos target=editor vulkan=no metal=yes webgpu=no   # links, runs
scons platform=web webgpu=yes target=template_debug                # 45MB wasm + template zip
```

## 7. SPIR-V -> WGSL fallout (the remaining work)

### Step 6 was not where the work was

The GLSL workaround sweep the plan budgeted for is essentially empty. Only three
`isnan`/`isinf` uses remained (`environment/volumetric_fog_process.glsl`, new in
4.7); `1e20`-scale literals, `modf` and varying arrays are all already clean.
Rewriting those three changed the conversion results **not at all** (171/11/11
before and after), because Tint handles `OpIsNan`/`OpIsInf` — the GLSL-level
workarounds in this branch were written for the older *naga* pipeline and are
largely obsolete. The change was reverted to avoid pointless divergence from
upstream.

There is a cleanup opportunity here, not taken: several existing GLSL
workarounds (`effects/copy.glsl`, `effects/motion_vectors.glsl`, the `1e20` ->
`1e6` edits) may now be revertible under Tint, which would shrink the branch's
divergence from upstream. Verify one at a time against the precompiler before
removing any.

The actual step-6 work was the **stale precompile shader registry** — see below.

### The registry was stale, and the SPIR-V version was wrong

`drivers/webgpu/wgsl_precompile.py` drives precompilation from a hand-written
`SHADER_REGISTRY`. It was written against 4.6.2 and 4.7 changed shaders to
require defines it did not supply — e.g. 4.7's `effects/octmap_downsampler.glsl`
has `layout(OCTMAP_FORMAT, set = 1, binding = 0)` and the engine supplies that
macro as two variants from `effects/copy_effects.cpp`, while the registry listed
one variant with no defines. Eleven variants failed to compile as GLSL at all.

Resynced against the engine's own `ShaderRD::initialize()` calls. Also fixed
two things found on the way:

* The precompiler compiled with glslang's **default** target, i.e. Vulkan 1.0 /
  SPIR-V 1.0, while the WebGPU shader container declares
  `SHADER_LANGUAGE_VULKAN_VERSION_1_1` + `SHADER_SPIRV_VERSION_1_3`. It now
  passes `--target-env vulkan1.1`. This matters: it is why the measured failure
  set shifted rather than merely shrinking.
* `compile_glsl_to_spirv()` reported only `stderr` and truncated it, turning
  every GLSL error into an unusable `ERROR: /var/folders/...`. It now surfaces
  the full diagnostic.
* `giprobe_write.glsl` is a dead shader in upstream Godot — tracked in both
  4.6.2 and 4.7, but with no C++ consumer and not in any SCsub. Dropped from the
  registry rather than given invented defines.

Result: **195 compiled, 0 GLSL failures, 14 Tint failures** (from 171/11/11).

### Remaining SPIR-V -> WGSL failures: 10

Two of the original 14 were fixed and two were reclassified as not-applicable.

**Fixed — write-only storage buffers** (`7ebe73ccdd`). `skeleton.glsl` and
`particles_copy.glsl` each declare one `writeonly buffer`, which glslang emits
as a SPIR-V `NonReadable` (decoration 25) and Tint maps to a write-only storage
var — which WGSL forbids, as storage buffers may only be `read` or
`read_write`. Both are hot-path (skeletal animation, particle transform copy).

New pass `promote_writeonly_storage_buffers()` strips `NonReadable` from
StorageBuffer variables and their block members, yielding `read_write`. Widening
a write-only buffer to read-write is safe: it only permits more than the shader
does. The pass deliberately does **not** touch storage *images* — WGSL storage
textures legitimately support `write` access, so stripping `NonReadable` there
would be a regression. It filters on two conditions together: storage class
`StorageBuffer` *and* a base type that is an `OpTypeStruct`; storage images are
`UniformConstant` over `OpTypeImage` and fail both. There is a test asserting a
write-only storage image comes back byte-identical.

Wired into both pass lists — `rendering_device_driver_webgpu.cpp` and
`tint_cli/main.cpp` — which must stay in lockstep or the precompiled table stops
matching runtime behaviour. Now 13 passes.

**Reclassified — subpass input attachments.** `effects/tonemap_mobile.glsl`
variants `subpass` and `subpass_1d_lut` fail on
`textureLoad(input_attachment<f32>, ...)`. WebGPU has no subpasses and the port
already forces `using_subpass_post_process = false` under `WEB_ENABLED`, so the
engine never compiles these at runtime. They are now on a documented exclusion
list in `wgsl_precompile.py` and reported as `Skipped: 2 (unsupported on
WebGPU)` rather than counted as failures — an explicit list rather than deleted
registry entries, so the reason survives in the code.

Current state: **195 compiled, 0 GLSL failures, 10 Tint failures, 2 skipped.**

| Cause | Count | Shaders |
| --- | --- | --- |
| `TINT_UNIMPLEMENTED` crash | 5 | `forward_mobile/scene_forward_mobile` (`color_pass`, `uber_color_pass`), `cluster_render` (`SHADER_NORMAL`, `SHADER_USE_ATTACHMENT`), `environment/volumetric_fog` |
| SPIR-V validation: `OpFunctionCall` argument type mismatch | 3 | `effects/tonemap` (`bicubic`, `bicubic_1d_lut`), `effects/taa_resolve` |
| `textureStore` on `texture_storage_2d<undefined, write>` | 1 | `effects/screen_space_reflection_filter` |
| missing `position` on vertex entry point | 1 | `environment/sdfgi_debug_probes` |

`scene_forward_mobile:color_pass` remains the priority — it is the main 3D
shader for the renderer WebGPU uses.

### Most of the 10 are NOT 4.7 regressions

Cross-referencing against `expected_failures.json` (the 4.6.2 baseline, 32
failures over 309 shaders) shows **7 of the 10 were already failing on 4.6.2**
and are accepted as Vulkan-only variants the WebGPU runtime never uses:

| Failure | 4.6.2 baseline entry |
| --- | --- |
| `cluster_render:SHADER_NORMAL` | `ClusterRenderShaderRD:0.frag.spv` |
| `cluster_render:SHADER_USE_ATTACHMENT` | `ClusterRenderShaderRD:1.frag.spv` |
| `effects/tonemap:bicubic` | `TonemapShaderRD:1.frag.spv` |
| `effects/tonemap:bicubic_1d_lut` | `TonemapShaderRD:3.frag.spv` |
| `effects/taa_resolve:default` | `TaaResolveShaderRD:0.comp.spv` |
| `effects/screen_space_reflection_filter:default` | `ScreenSpaceReflectionFilterShaderRD:0.comp.spv` |
| `environment/volumetric_fog:default` | `VolumetricFogShaderRD:2.comp.spv` |

Not in the baseline, i.e. genuinely needing attention:

* `forward_mobile/scene_forward_mobile` — `color_pass` and `uber_color_pass`
* `environment/sdfgi_debug_probes:default:vert` — status unclear; the baseline
  contains no `SdfgiDebug` entry at all, which may simply mean the capture scene
  never compiled this debug shader.

The baseline contains **zero** `Mobile` entries and 18 `SceneForwardClustered`
ones, so it was evidently captured from a Forward+ session. That raised the
possibility that mobile had always failed and simply was not covered — but see
below: it is a real regression.

### sdfgi_debug_probes is a registry defect, not a 4.7 regression

The shader is byte-identical between 4.6.2 and 4.7. Its vertex `main()` writes
`gl_Position` in exactly two places, one under `#ifdef MODE_PROBES` and one
under `#ifdef MODE_VISIBILITY`. The registry lists a single `default` variant
with **no defines**, so `main()` compiles to an empty function that writes no
position — precisely the error Tint reports.

The engine never compiles it that way. `environment/gi.cpp:3782-3788` pushes
four versions: `MODE_PROBES`, `MODE_PROBES + USE_MULTIVIEW`, `MODE_VISIBILITY`,
`MODE_VISIBILITY + USE_MULTIVIEW`.

This is the same class as the `octmap_downsampler` bug. The earlier resync fixed
the eleven shaders that failed *GLSL* compilation; this one compiles fine as
GLSL and only fails at the Tint stage, so it fell outside that sweep.

### The registry likely under-covers MODE_-based variants generally

A heuristic scan (shaders that branch on `MODE_*` vs. the `MODE_*` defines any
registry variant supplies) flags **19 entries**. That number is an upper bound
and must not be quoted as-is — `MODE_*` in Godot shaders is used both for
`ShaderRD` version defines (real variants) and for material/render-mode defines
set elsewhere, and the heuristic cannot tell them apart.

Verified by reading the C++ so far:

* **Genuine** — `environment/sdfgi_debug_probes.glsl` (4 versions, `gi.cpp`),
  `environment/voxel_gi.glsl` (`MODE_COMPUTE_LIGHT`, `MODE_SECOND_BOUNCE`,
  `MODE_UPDATE_MIPMAPS`, `MODE_WRITE_TEXTURE`, `MODE_DYNAMIC_*`),
  `environment/voxel_gi_debug.glsl` (`MODE_DEBUG_{COLOR,LIGHT,EMISSION,LIGHT_FULL}`).
  All three have **no** `MODE_` defines in the registry.
* **False positive** — `canvas.glsl`. Its `MODE_LIGHT_ONLY` / `MODE_UNSHADED`
  are material-level modes; the registry's `USE_NINEPATCH` / `USE_PRIMITIVE` /
  `USE_ATTRIBUTES` variants are correct.

The rest need per-shader verification against the `ShaderRD::initialize()` call.

Why this matters beyond the failure count: where the registry supplies the wrong
defines, the precompiler emits WGSL for **variants the engine never asks for**
while omitting the ones it does. Those entries can never match at runtime, so
the affected shaders silently fall back to in-engine Tint conversion. It also
means "195 compiled" overstates readiness — some of those 195 are phantom
variants. The right fix is to derive the whole registry from the engine's
`initialize()` calls rather than patching the entries that happen to fail.

### scene_forward_mobile: confirmed a 4.7 regression, cause not yet found

Direct A/B, same toolchain and same preprocessing passes, `color_pass` fragment:

* 4.6.2's `scene_forward_mobile.glsl` (from `refs/wgpu/webgpu-4.6.2`) converts —
  `tint_convert_cli` exits 0.
* 4.7's crashes — `TINT_ASSERT(tex_ty)` at
  `tint/lang/spirv/reader/lower/texture.cc:606`.

That assert is in `ProcessCoords()`, reached from the image-sample handlers via
`GetTextureSampler()` returning a value whose type is not a
`core::type::Texture`.

Comparing the two SPIR-V modules, 4.7 introduces three op kinds absent in 4.6.2:
`OpImageSampleImplicitLod` (2), `OpImageQuerySizeLod` (1) and `OpImage` (1).

**Three hypotheses were tested and all disproved.** Each was neutralised in the
GLSL and the shader recompiled; the crash was byte-for-byte identical every
time:

1. Combined image-sampler passed as a function parameter
   (`ltc_evaluate_specular`'s two `sampler2D` params, new with 4.7's area
   lights). Fixed anyway in `12d4ca6447` — the pipeline genuinely cannot
   represent it — but it is not the cause.
2. `textureSize(sampler2D(decal_atlas_srgb, light_projector_sampler), 0)` at
   `scene_forward_lights_inc.glsl:689`, new in 4.7 (4.6.2 used a precomputed
   `shadow_atlas_pixel_size` uniform). This is what produces the `OpImage` +
   `OpImageQuerySizeLod` pair. Replacing it with a constant did not help.
3. The two `texture()` calls on the LTC LUTs, which produce the
   `OpImageSampleImplicitLod` pair. Switching them to `textureLod()` did not
   help.

**Recommended next step: a mechanical bisect rather than more hypotheses.** The
4.6.2 shader tree is easy to obtain in isolation:

```
git archive refs/wgpu/webgpu-4.6.2 servers/rendering/renderer_rd/shaders \
    | tar -x -C <tmpdir>
```

Then swap 4.7's include files for their 4.6.2 counterparts one at a time —
`scene_forward_mobile_inc.glsl`, `scene_forward_lights_inc.glsl`,
`area_lights_inc.glsl`, `decal_data_inc.glsl`, `light_data_inc.glsl`,
`samplers_inc.glsl` — recompiling and re-running `tint_convert_cli` after each,
until the crash disappears. That identifies the file, after which the same
technique narrows to the construct. Note some 4.6.2 files will not compile
against 4.7's, so expect to interpret GLSL errors as "inconclusive" rather than
as results.

Useful context: `scene_forward_clustered` is **not** in the precompile registry
at all (WebGPU uses the mobile renderer), and 18 of its variants are in the
4.6.2 failure baseline — so the clustered shader is not a useful comparison.

### scene_forward_mobile bisect: narrowed, not yet solved

A reproducible harness exists (see the recipe below). Controls verified every
run: the 4.6.2 shader converts (`tint_exit=0`), the 4.7 one crashes.

**Ruled out.** Each was neutralised and produced the identical crash:

| Probe | Result |
| --- | --- |
| Combined sampler as function parameter (LTC LUTs) | still crashes |
| `textureSize(sampler2D(decal_atlas_srgb, ...))`, new in 4.7 | still crashes |
| `texture()` on the LTC LUTs (`OpImageSampleImplicitLod`) | still crashes |
| Area-light loop disabled (`sc_area_lights = 0`) | still crashes |
| Decal loop disabled | still crashes |
| Omni / spot / directional fragment loops, individually | still crashes |
| **All** light loops + decals + area lights disabled together | still crashes |
| Swapping `decal_data_inc`, `scene_data_inc`, `oct_inc`, `half_inc`, `scene_forward_aa_inc`, `scene_forward_vertex_lights_inc` to their 4.6.2 versions | still crashes |

**Inconclusive** (the 4.6.2 file does not compile against 4.7, so these say
nothing): `scene_forward_mobile_inc.glsl`, `scene_forward_lights_inc.glsl`,
`light_data_inc.glsl`. `area_lights_inc.glsl` does not exist in 4.6.2.

**Narrowed by.** These all convert cleanly:

* `#define MODE_UNSHADED`
* `#define USE_LIGHTMAP`
* `#define MODE_RENDER_DEPTH`

Since disabling every light loop does *not* help but `MODE_UNSHADED` does, the
culprit is code guarded by `!defined(MODE_UNSHADED)` that is **outside** the
light loops — i.e. the reflection-probe / GI / octmap-ambient block, roughly
lines 1589-1936 of the fragment stage. Candidates in that block worth examining
first: the `radiance_octmap` sampling (used as both `sampler2DArray` and
`sampler2D` under `USE_RADIANCE_OCTMAP_ARRAY`, though that arrangement is
identical in 4.6.2) and `textureArray_bicubic(lightmap_textures[ofs], ...)`,
which indexes an array of textures by a non-constant and therefore interacts
with the `flatten_binding_arrays` preprocessing pass.

Given `GetTextureSampler()` simply returns the two operands of the recorded
`OpSampledImage`, the assert means some `OpSampledImage` reaching Tint has a
non-texture as operand 0. That points at one of the preprocessing passes
producing a malformed instruction rather than at the GLSL itself.

### Reproduction harness

```bash
SP=<scratch>
git archive HEAD servers/rendering/renderer_rd/shaders | tar -x -C $SP/bisect
git archive refs/wgpu/webgpu-4.6.2 servers/rendering/renderer_rd/shaders \
    | tar -x -C $SP/bisect_old
```

Then, against either tree, assemble the variant with `wgsl_precompile`'s
`parse_glsl_file` / `assemble_glsl` / `compile_glsl_to_spirv`, write the SPIR-V
to a file, and run `bin/tint_convert_cli <file>.spv` in **single-file** mode —
`--batch` forks with stderr to `/dev/null` and hides the assert. Exit 0 means it
converted; exit 133 plus the `TINT_ASSERT` line means it still crashes.

Note: delegating this to Codex failed twice. Its provider-side classifier
rejected the task ("flagged for possible cybersecurity risk"), almost certainly
a false positive on writing SPIR-V binaries and analysing a compiler abort.

### How to debug the TINT_UNIMPLEMENTED crashes

The batch converter reports only a generic "Tint crashed" because
`tint_cli/main.cpp` runs each conversion in a forked child with stdout and
stderr redirected to `/dev/null`, so an `abort()` cannot corrupt the parent's
JSON output. **Single-file mode does not fork or redirect**, so:

```
bin/tint_convert_cli <file.spv>
```

prints the real `TINT_ASSERT` / `TINT_UNIMPLEMENTED` message and source location
to stderr before aborting. Use that per shader.

There is prior art: `webgpu_notes/naga_to_tint_debugging.md` Issue 3 documents a
crash at Tint's `parser.cc:3147` (`EmitSampledImage` on a multisampled image),
fixed by changing `split_combined_samplers` to rewrite the `OpVariable` type.
These crashes have historically been tractable preprocessing-pass problems.

### On getting an authoritative measurement

`webgpu_tests/shader_corpus/validate_spirv_dump.mjs` plus
`GODOT_DUMP_SPIRV=<dir>` looks like the authoritative check — it validates the
engine's *own* compiled SPIR-V — but it **cannot be used from a Metal editor
build**. Attempting it here produced 333/333 failures, all
`Invalid SPIR-V binary version 1.6 for target environment`: the Metal container
requests `SHADER_SPIRV_VERSION_1_6` (in 4.6.2 too — this is not a 4.7 change),
while Tint targets Vulkan 1.1 / SPIR-V 1.3. That result is a pure measurement
artifact and says nothing about the port.

The `expected_failures.json` baseline (32 failures / 309 shaders, 2026-05-05)
was therefore captured from a **Vulkan** editor build. Reproducing it needs
`vulkan=yes`, which needs the MoltenVK SDK — not installed on this machine.
Note also that even with a Vulkan build the baseline is no longer directly
comparable: **4.7 raised the Vulkan container from SPIR-V 1.3 to 1.4**
(`drivers/vulkan/rendering_shader_container_vulkan.cpp`), while WebGPU stays
pinned at 1.3.

So the precompiler run is currently the best available measurement, and since
the `--target-env vulkan1.1` fix it emits exactly the SPIR-V version the WebGPU
container requests. The only remaining discrepancy is the glslang *binary*
(system 16.5.0 vs Godot's bundled 1.4.335). Closing that gap means either
building a Vulkan editor, or running the real web build in a browser.

## 8. Scope decision

The whole `webgpu_tests/`, `webgpu_notes/` and `webgpu_site/` trees are carried
over. They cost nothing at build time, and `webgpu_tests/preprocessing_tests` is
the primary instrument for step 7.
