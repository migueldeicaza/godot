# Shader Corpus Test

Validates the SPIR-V → WGSL conversion pipeline used by the WebGPU rendering backend.

## What it tests

- **Push constant conversion** — SPIR-V `OpVariable PushConstant` → WGSL `var<storage>` at group(3)/binding(120)
- **Combined image-sampler splitting** — GLSL `sampler2D` → separate WGSL `texture_2d` + `sampler`
- **Specialization constants** — `layout(constant_id)` → WGSL `const` declarations
- **Storage buffers** — SSBO read/write and readonly qualifiers
- **Storage images** — `image2D` with format qualifiers
- **Depth textures** — `sampler2DShadow` → `texture_depth_2d`
- **Cube maps** — `samplerCube` → `texture_cube`
- **Compute shaders** — workgroup sizes, atomics, barriers

## Test fixtures

| Shader | Stage | Key Features |
|--------|-------|--------------|
| basic_vertex | vert | Push constants, UBO, varyings |
| basic_fragment | frag | Combined samplers, lighting |
| compute_particles | comp | SSBO read/write, push constants |
| shadow_pass | vert | Minimal push constant MVP |
| depth_sampling | frag | sampler2DShadow, depth comparison |
| storage_image | comp | image2D with r8 format |
| multi_texture | frag | 6 samplers including samplerCube |
| spec_constants | frag | Specialization constants (bool/int/float) |
| readonly_ssbo | comp | restrict readonly buffer |

## Running

```bash
# Compile GLSL → SPIR-V (requires glslangValidator)
./compile_fixtures.sh

# Run SPIR-V → WGSL validation (requires Node.js 18+)
node run_tests.mjs
```

## Output

- `results/*.wgsl` — Generated WGSL for inspection
- `results/report.json` — Machine-readable test results with timing

## Dependencies

- **glslangValidator** — For compiling GLSL fixtures to SPIR-V (`brew install glslang`)
- **Node.js 18+** — For running the test harness
- **tint_convert_cli** — Standalone Tint CLI (optional; tests skip gracefully if not available)
