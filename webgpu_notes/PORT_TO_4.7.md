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

4.7 added raytracing and HDR output as **pure virtual** members. The WebGPU
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
* `RenderingContextDriver` — 10 new `= 0` methods: the
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

### 3.5 spirv-headers collision — RESOLVED, and the opposite of expected

4.7 vendors 4 files (`vulkan-sdk-1.4.335.0`, `b824a462`, 2025). The WebGPU
branch vendors 11 (git `ad9184e7`, labelled 2026) because SPIRV-Tools needs
`DebugInfo.h`, `NonSemantic*.h`, `OpenCL*.h` and `GLSL.std.450.h`.

The first draft of this plan assumed the WebGPU snapshot was newer and should be
taken wholesale. That is wrong. Comparing the two `spirv.h` copies:

* 169 enumerants exist only in 4.7's copy; **zero** exist only in the WebGPU
  branch's copy.
* The 145 lines that differ in the other direction are the INTEL -> ALTERA
  vendor rename, and 4.7's copy retains all 985 `INTEL` aliases.
* The vendored SPIRV-Tools sources reference none of those names directly.

So 4.7's `spirv.{h,hpp,hpp11}` and `LICENSE` are kept, and only the 7 extra
headers are taken from the WebGPU branch. Taking the WebGPU copy wholesale would
have silently downgraded the headers that `glslang` 1.4.335 and `spirv-reflect`
compile against.

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

### 3.7 `RS::` -> `RSE::` enum namespace split

4.7 moved rendering enums into `RenderingServerEnums` (`#define RSE
RenderingServerEnums`). `RS::` now resolves only to `RenderingServer` class
members. Inside `servers/rendering/renderer_rd/` there are 1403 `RSE::` uses and
just 9 remaining `RS::` uses (all `RS::get_singleton()`). The WebGPU delta adds
exactly one offending reference (`RS::LIGHT_OMNI_SHADOW_DUAL_PARABOLOID`), but
auto-merged hunks land beside renamed code, so a compile pass is needed to catch
stragglers.

## 4. Build wiring

* `SConstruct`: one line — `opts.Add(BoolVariable("webgpu", ...))`.
* `drivers/SCsub`: `if env["webgpu"]: SConscript("webgpu/SCsub")` under
  "Graphics drivers".
* `platform/web/detect.py`: adds `"supported": ["webgpu"]` to `get_flags()`, and
  in `configure()` defines `WEBGPU_ENABLED` + `RD_ENABLED` and passes
  `--use-port=emdawnwebgpu` to both `CCFLAGS` and `LINKFLAGS`.
* `modules/glslang/config.py`: `can_build` must also return true for
  `env["webgpu"]`.

Emscripten 4.0.10+ is required for the `emdawnwebgpu` port. 4.7 already sets a
4.0.0 floor. **No emscripten toolchain is currently installed on this machine**,
so the web build cannot be exercised locally yet — see section 5.

## 5. Verification strategy

Because there is no local emscripten, the port is verified in two stages.

Stage A — now, without emscripten:

1. macOS editor build (`vulkan=no`, Metal path) must still compile and link.
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

| Step | Work | Estimate |
| --- | --- | --- |
| 0 | Branch `webgpu-4.7` from the 4.7 tip; fetch `refs/wgpu/webgpu-4.6.2` | **done** |
| 1 | Apply the `4.6.2-stable..webgpu-4.6.2` delta 3-way; resolve the 12 conflicts | **done** (`5e065e95cf`) |
| 2 | Reconcile `thirdparty/spirv-headers`; confirm glslang + spirv-reflect build | **done** (`5e065e95cf`); build confirmation pending step 5 |
| 3 | Stub the 25 new pure virtuals; `VSyncMode` rename | ~4h |
| 4 | `SHADER_STAGE_MAX`, `pipeline_type`, container fixups, `RS::`->`RSE::` | ~2h |
| 5 | Stage A verification (macOS build green) | ~2h |
| 6 | GLSL workaround sweep against the 4.7 shader set | ~2h |
| 7 | glslang 1.4.335 SPIR-V fallout in `spirv_preprocess.cpp` / Tint | unbounded |
| 8 | Stage B verification (needs emsdk) | — |

Steps 1-6 are well understood. Step 7 is the genuine unknown and is the reason
steps 1-5 are front-loaded: they get us to that answer fastest.

## 7. Scope decision

The whole `webgpu_tests/`, `webgpu_notes/` and `webgpu_site/` trees are carried
over. They cost nothing at build time, and `webgpu_tests/preprocessing_tests` is
the primary instrument for step 7.
