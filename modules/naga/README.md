# Experimental Naga shader backend

This optional module lets the Metal renderer attempt GLSL-to-MSL translation with
[Naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga) for Godot's built-in forward
Uber Shaders. It is intentionally not a replacement for Godot's shader validation or
the general GLSLang/SPIRV-Cross path.

Build on macOS with Rust 1.87 or newer:

```sh
scons platform=macos naga_enabled=yes
```

At runtime, enable `rendering/shader_compiler/metal/use_naga_for_ubershaders` in the
project settings. Translation failures automatically use the existing compiler path.
For one-off testing and benchmarks, `GODOT_NAGA_UBERSHADERS=1` enables the same path
without changing a project file.
`GODOT_NAGA_TEST_ALL_UBER_VARIANTS=1` enables every Forward+ or Mobile shader
group and creates a real Metal render pipeline for every built-in Uber variant.
`GODOT_NAGA_TEST_ALL_FORWARD_VARIANTS=1` additionally routes non-Uber forward
variants through Naga, which is useful only for exercising the fallback boundary.

The bridge applies Godot's Vulkan-to-Metal vertex-Y convention before parsing and
strips desktop-GLSL precision qualifiers that Naga does not accept in structure
members. It preserves Godot's specialization constants as Metal function constants,
lowers matrix inverse operations using Naga's own WGSL inverse formulas, and splits
the LTC combined samplers used by the color Uber Shader. It uses flat Metal resource
slots. For successful direct translations, Godot reflects descriptor layouts, stage
interfaces, push constants, and specialization defaults directly from Naga IR. The
direct path therefore performs no SPIR-V serialization and invokes neither GLSLang,
SPIRV-Reflect, nor SPIRV-Cross. This includes storage-image formats and atomic access.
A Naga-generated SPIR-V reflection view remains as a compatibility fallback for IR
resource types outside the tested built-in shader surface. GLSLang also remains
available as a transformed SPIR-V fallback for GLSL constructs Naga cannot yet parse;
successful output is never passed through SPIRV-Cross.

The exhaustive Metal smoke matrix compiles all 43 unique built-in Uber variants
directly through Naga: 25 Forward+ and 18 Mobile (including both Mobile FP32 and
FP16 groups). It also creates all 43 corresponding Metal render pipelines, with no
GLSLang/SPIRV-Cross fallback. The bridge handles comparison samplers, ordinary depth
reads, fixed resource-binding arrays, subgroup operations, multiview, storage-image
atomics, tightly packed three-component buffer members, buffer boolean layouts, and
explicit-fp16 Mobile lighting. The legacy compiler path remains mandatory for
untested permutations and other shaders.

## Benchmark

`tests/benchmark_metal.py` alternates Naga and legacy runs, disables Godot's shader
cache through the smoke project, and adds a unique harmless comment to generated MSL
for each process so Apple's Metal shader cache cannot satisfy a previous run. It
measures translation work separately from blocking Metal pipeline creation and fails
if a variant falls back or a pipeline cannot be created.

```sh
python3 modules/naga/tests/benchmark_metal.py --iterations 5
```

On an Apple M3 Ultra, a five-run median from the `speed_trace` editor build produced:

| Renderer | Naga translation | Legacy translation | Translation speedup | Naga Metal pipelines | Legacy Metal pipelines |
|---|---:|---:|---:|---:|---:|
| Forward+ (25 variants) | 0.903 s | 4.119 s | 4.56x | 2.289 s | 2.233 s |
| Mobile (18 variants) | 0.748 s | 1.668 s | 2.23x | 2.128 s | 2.078 s |

Translation is the material improvement; cold Metal pipeline compilation is at
parity, as expected. Set `GODOT_NAGA_BENCHMARK_UBER_VARIANTS=1` alongside the
exhaustive-test variables to print parse/validation, reflection, MSL, GLSLang,
SPIRV-Cross/container, and pipeline timings for an individual run.

## Render equivalence

`tests/compare_metal_rendering.py` renders a fixed PBR scene through forced Uber
pipelines using the legacy and direct-Naga Metal paths. It captures raw RGBA8 pixels,
requires a Naga compilation marker with no fallback, and compares maximum, mean, and
RMS channel error. It runs both Forward+ and Mobile by default:

```sh
python3 modules/naga/tests/compare_metal_rendering.py
```

Forward+ is bit-for-bit identical on the reference M3 Ultra. Mobile has a stable
mean channel difference of 0.01/255 and RMS difference of 0.114/255, localized to
shadow and triangle edges; the largest observed channel difference is 7/255 across
9 channel values. The default tolerance admits this compiler-level floating-point
variation while rejecting broader or visibly meaningful divergence. PNG captures,
logs, raw pixels, and an amplified difference image are retained in the printed
temporary output directory. `GODOT_NAGA_FORCE_UBERSHADERS=1` is the test-only switch
used by the harness to prevent specialized pipelines from replacing the code under
test.

Naga 29.0.3 is vendored under `modules/naga/vendor/naga` because the tested Godot
shader surface needs small GLSL frontend and MSL binding-array fixes not yet present
upstream. `vendor/naga/GODOT_PATCHES.md` records their scope.

Run the included exhaustive Metal smoke test for each rendering method:

```sh
GODOT_NAGA_UBERSHADERS=1 GODOT_NAGA_TEST_ALL_UBER_VARIANTS=1 \
  bin/godot.macos.editor.arm64 --path modules/naga/tests/metal_smoke \
  --rendering-method forward_plus --rendering-driver metal --quit-after 2 --verbose

GODOT_NAGA_UBERSHADERS=1 GODOT_NAGA_TEST_ALL_UBER_VARIANTS=1 \
  bin/godot.macos.editor.arm64 --path modules/naga/tests/metal_smoke \
  --rendering-method mobile --rendering-driver metal --quit-after 2 --verbose
```

Set `GODOT_NAGA_DUMP_GLSL_DIR` or `GODOT_NAGA_DUMP_MSL_DIR` to a directory to retain
the preprocessed GLSL or generated MSL while diagnosing a shader permutation.
