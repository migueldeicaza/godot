# WebGPU Test Suite

Automated tests for the Godot WebGPU rendering backend. Validates the full shader pipeline from GLSL compilation through SPIR-V → WGSL conversion to in-browser execution.

## Test Categories

| Test | What it validates | Runtime | Needs engine build? |
|------|------------------|---------|---------------------|
| [Shader Corpus](shader_corpus/) | SPIR-V → WGSL conversion via Tint CLI | ~1s | No (needs Tint CLI) |
| [SPIR-V Validation](shader_corpus/validate_spirv_dump.mjs) | ALL engine-compiled SPIR-V through Tint | ~5s | Yes (editor) |
| [Smoke Test](test_project/smoke_test.mjs) | Full runtime in headless Chrome — no shader errors, no device lost | ~60s | Yes (editor + web template) |
| [Scene Smoketest](scene_smoketest/) | 18 demo/benchmark scenes across Chrome, Firefox, and Safari | ~8min | Yes (pre-exported) |
| [Resource Lifecycle](resource_lifecycle/) | Rapid create/destroy of buffers, textures, pipelines | ~30s | No (standalone) |
| [Screenshot Comparison](screenshot_comparison/) | Visual regression across Chrome and Firefox | ~60s | No (standalone) |

## How It Works

The WebGPU shader pipeline is:

```
GLSL → SPIR-V (glslang, at editor build time)
     → 7 binary rewriting passes (C++ spirv_preprocess, at runtime)
     → WGSL (Tint C++ library, linked into engine, at runtime)
     → GPU (browser's WebGPU implementation)
```

The SPIR-V preprocessing passes (C++):
1. **freeze_spec_constant_ops** — Evaluates `OpSpecConstantOp` into plain constants
2. **rewrite_copy_logical** — `OpCopyLogical` → `OpCopyObject` (SPIR-V 1.4+ struct copy)
3. **rewrite_terminate_invocation** — `OpTerminateInvocation` → `OpKill` (modern discard)
4. **infer_readonly_storage** — Adds `NonWritable` to read-only SSBOs
5. **convert_push_constants_to_uniforms** — PushConstant → StorageBuffer at group(3)/binding(120)
6. **split_combined_samplers** — Combined image samplers → separate texture + sampler
7. **fix_depth2_images** — depth=2 (unknown) → depth=1 for comparison sampling

## Running Locally

### Prerequisites

- **Node.js 20+** — All test runners
- **glslangValidator** — Shader corpus fixture compilation (`brew install glslang` / `apt install glslang-tools`)
- **Playwright** — Browser-based tests (`npm install playwright`)
- **Emscripten 4.0.11** — Web template build (`~/emsdk`)
- **SCons + Python** — Godot build system

### 1. Shader Corpus (fast, no build needed)

Tests 9 hand-crafted GLSL fixtures through the Tint CLI (if available):

```bash
cd webgpu_tests/shader_corpus
./compile_fixtures.sh    # GLSL → SPIR-V (requires glslangValidator)
node run_tests.mjs       # SPIR-V → WGSL validation (skips gracefully if no Tint CLI)
```

### 2. SPIR-V Dump Validation (requires editor build)

Validates all 300+ engine-compiled shaders through Tint offline:

```bash
# Build the editor (macOS example)
scons platform=macos target=editor dev_build=yes -j$(sysctl -n hw.ncpu)

# Run the test scene to trigger shader compilation (needs a window)
GODOT_DUMP_SPIRV=/tmp/spirv_dump bin/godot.macos.editor.dev.arm64 \
    --path webgpu_tests/test_project --quit-after 10

# Validate all dumped SPIR-V
node webgpu_tests/shader_corpus/validate_spirv_dump.mjs /tmp/spirv_dump/
```

The validator uses an **expected failures baseline** (`shader_corpus/expected_failures.json`) — shader variants compiled by the Vulkan editor that the WebGPU runtime never uses (different code paths). CI fails only on **regressions** (new failures beyond the baseline).

To update the baseline after intentional changes:
```bash
node webgpu_tests/shader_corpus/validate_spirv_dump.mjs /tmp/spirv_dump/ --update-baseline
```

### 3. Smoke Test (requires full build + export)

End-to-end validation: exports the test project, serves it in headless Chrome, verifies no shader errors:

```bash
# Build the web template
source ~/emsdk/emsdk_env.sh
scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j$(sysctl -n hw.ncpu)

# Install template (macOS — adjust path for Linux)
mkdir -p ~/Library/Application\ Support/Godot/export_templates/4.6.2.stable
cp bin/godot.web.template_release.wasm32.nothreads.dlink.zip \
   ~/Library/Application\ Support/Godot/export_templates/4.6.2.stable/web_nothreads_release.zip

# Export
bin/godot.macos.editor.arm64 --headless --path webgpu_tests/test_project \
    --export-release "WebGPU" export/index.html

# Run smoke test
cd webgpu_tests/test_project
npm install playwright && npx playwright install chromium
node smoke_test.mjs ./export/
```

**Pass criteria:** Engine starts, all shaders compile (no `[SHADER]` errors), no device-lost, GDScript reports `[ShaderCoverage] PASS`.

### 4. Scene Smoketest — Multi-Browser (requires pre-exported scenes)

Runs 18 demo and benchmark scenes across Chrome, Firefox, and Safari:

```bash
cd webgpu_tests/scene_smoketest
npm install playwright && npx playwright install chromium firefox --with-deps
node run_scenes.mjs --browser all         # All 3 browsers
node run_scenes.mjs --browser chrome      # Chrome only (default)
node run_scenes.mjs --browser firefox     # Firefox only
node run_scenes.mjs --browser safari      # Safari only (macOS, requires "Allow JS from Apple Events")
node run_scenes.mjs --scene benchmark_pbr # Single scene, default browser
```

**Pass criteria:** Engine starts (canvas > 300px), no GPU validation errors, no shader failures, no device-lost.

**Safari prerequisite:** Enable Safari → Develop → "Allow JavaScript from Apple Events" (uses real Safari via AppleScript since Playwright's safaridriver disables WebGPU).

**Exporting scenes** (requires editor + web template):
```bash
node run_scenes.mjs --export --browser chrome
```

### 5. Resource Lifecycle (standalone, needs Playwright)

```bash
cd webgpu_tests/resource_lifecycle
npm install playwright && npx playwright install chromium --with-deps
node run_tests.mjs
```

### 6. Screenshot Comparison (standalone, needs Playwright)

```bash
cd webgpu_tests/screenshot_comparison
npm install playwright && npx playwright install chromium firefox --with-deps
node screenshot_tests.mjs --update-baselines  # first run creates baselines
node screenshot_tests.mjs                      # subsequent runs compare
```

## CI Pipeline

Defined in `.github/workflows/webgpu_tests.yml`. Runs on push/PR to `webgpu-4.6.2` when `drivers/webgpu/`, `servers/rendering/`, or `webgpu_tests/` are modified.

```
┌─────────────────────────────────────────────────────────────────┐
│  CI Pipeline (.github/workflows/webgpu_tests.yml)               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────┐  (parallel, no build needed)               │
│  │ shader-corpus   │  9 GLSL fixtures → SPIR-V → WGSL          │
│  └─────────────────┘                                            │
│                                                                 │
│  ┌─────────────────┐  (parallel, no build needed)               │
│  │ resource-       │  GPU resource stress test in Chrome        │
│  │ lifecycle       │                                            │
│  └─────────────────┘                                            │
│                                                                 │
│  ┌─────────────────┐  (parallel, no build needed)               │
│  │ screenshot-     │  Visual regression Chrome + Firefox        │
│  │ comparison      │                                            │
│  └─────────────────┘                                            │
│                                                                 │
│  ┌─────────────────┐  (parallel, no build needed)               │
│  │ scene-smoketest │  18 scenes × Chrome + Firefox              │
│  │                 │  (pre-exported in repo)                    │
│  └─────────────────┘                                            │
│                                                                 │
│  ┌─────────────────┐                                            │
│  │ build-webgpu    │  Web template + Linux editor + export      │
│  │ (~60 min)       │  + SPIR-V dump (309 shaders)               │
│  └────────┬────────┘                                            │
│           │                                                     │
│           ├──────────────┐                                      │
│           ▼              ▼                                      │
│  ┌────────────────┐  ┌──────────────┐                           │
│  │ validate-spirv │  │ smoke-test   │                           │
│  │ All SPIR-V     │  │ Headless     │                           │
│  │ through Tint   │  │ Chrome run   │                           │
│  └────────────────┘  └──────────────┘                           │
│                                                                 │
│  ┌─────────────────────────────────────────────────────┐        │
│  │ test-summary   │  Aggregates results, gates merge   │        │
│  └─────────────────────────────────────────────────────┘        │
└─────────────────────────────────────────────────────────────────┘
```

### CI Jobs Detail

| Job | Depends on | Timeout | Blocks merge? |
|-----|-----------|---------|---------------|
| `shader-corpus` | — | 10 min | Yes |
| `build-webgpu` | — | 90 min | Yes |
| `validate-spirv` | build-webgpu | 10 min | Yes |
| `smoke-test` | build-webgpu | 15 min | Yes |
| `scene-smoketest` | — | 20 min | Yes |
| `resource-lifecycle` | — | 15 min | Yes |
| `screenshot-comparison` | — | 20 min | No (warning only) |
| `test-summary` | all above | — | — |

### Trigger Paths

CI runs when any of these paths change:
- `drivers/webgpu/**`
- `webgpu_tests/**`
- `servers/rendering/**`

Can also be triggered manually via `workflow_dispatch`.

## SPIR-V Dump (Engine Integration)

The engine includes a `GODOT_DUMP_SPIRV` environment variable (added in `servers/rendering/rendering_device.cpp`) that causes all compiled SPIR-V to be written to disk during shader compilation:

```bash
GODOT_DUMP_SPIRV=/tmp/spirv_dump godot --path webgpu_tests/test_project --quit-after 10
node webgpu_tests/shader_corpus/validate_spirv_dump.mjs /tmp/spirv_dump/
```

**Important:** `--headless` does NOT trigger shader compilation (shaders compile lazily during rendering). You need to run with a window (`--quit-after N`) to get the full shader dump.

The dump produces ~309 `.spv` files named `<ShaderRD>:<variant>.<stage>.spv` (e.g., `SceneForwardClusteredShaderRD:5.frag.spv`).

## Test Project (Shader Coverage Scene)

`test_project/` is a Godot 4.6 project with a GDScript (`scripts/shader_coverage.gd`) that programmatically creates a scene exercising **100% of RenderingDevice shader paths**:

- **Environment:** Sky, SSAO, SSIL, SSR, volumetric fog, SDFGI, VoxelGI, glow, DOF, tonemap, TAA, FSR2
- **Materials:** 20+ StandardMaterial3D variants (normal maps, emission, clearcoat, anisotropy, SSS, refraction, parallax, rim, backlight, alpha scissor/hash/depth-prepass, unshaded, billboard, proximity/distance fade)
- **Lighting:** 7 lights (for cluster fill), directional with 4-split shadows, omni + spot shadows
- **Particles:** GPU particles with trails, turbulence, collision, attractors
- **Canvas 2D:** ColorRect, Label (MSDF), NinePatchRect, PointLight2D
- **Instancing:** MultiMesh (64 instances), Skeleton mesh
- **Post-processing:** Decals, reflection probes, fog volumes, motion vectors, luminance reduction

See [test_project/README.md](test_project/README.md) for the full shader list.

## Expected Failures

`shader_corpus/expected_failures.json` tracks shader variants that fail Tint validation offline but work at runtime. These are Vulkan-only variants the WebGPU path never uses:

| Category | Count | Reason |
|----------|-------|--------|
| ComparisonSamplingMismatch | 14 | Soft shadow variants with depth comparison pattern |
| UnsupportedStorageClass(11) | 3 | Image storage class (Vulkan-only) |
| UnsupportedBuiltIn(23) | 2 | PointSize (not in WebGPU) |
| InvalidId | 3 | Spec constant cascade in Tonemap/TAA |
| InvalidImage | 3 | SDFGI image type issues |
| InvalidBinaryOperandTypes | 2 | 64-bit integer multiply in SDFGI/Cluster |
| Other | 5 | InvalidTypeWidth (16-bit FSR), IncompleteData, BuiltinArgs |

The validator **passes** as long as no new failures appear beyond this baseline. If you add new shaders that legitimately can't convert offline, update the baseline:

```bash
node webgpu_tests/shader_corpus/validate_spirv_dump.mjs /tmp/spirv_dump/ --update-baseline
```

## Troubleshooting

**"No .spv files found"** — You ran with `--headless`. Use `--quit-after 10` instead (shaders need rendering to compile).

**"tint_convert_cli not found"** — The shader corpus and SPIR-V dump validator use a standalone Tint CLI for offline validation. If it's not available, the tests skip gracefully — runtime Tint (linked into the engine WASM) handles all conversion.

**New shader failures after engine changes** — Run validation, review the new errors. If they're Vulkan-only variants, update the baseline. If they affect WebGPU runtime, fix the GLSL or SPIR-V preprocessing.

**Smoke test timeout** — The engine has 2 minutes to start and report PASS. If it hangs, check Chrome console output with `VERBOSE=1 node smoke_test.mjs ./export/`.

**Export fails with "No export template found"** — Template must be installed at the path matching the editor's version string. Check `bin/godot --version` and install to the corresponding `export_templates/<version>/` directory.
