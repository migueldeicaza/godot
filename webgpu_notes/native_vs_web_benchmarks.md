# Native vs Web Benchmarks

## Scene: 3D Platformer
Project: `godot-demo-projects/3d/platformer`

## How to run

All three modes via the benchmark script:
```bash
cd webgpu_tests/benchmark

./run_benchmark.sh native    # Metal (Forward+) on macOS
./run_benchmark.sh webgl     # Stock Godot 4.6 WebGL in Chrome
./run_benchmark.sh webgpu    # Our WebGPU build in Chrome
```

Default scene is the 3D platformer. Pass a custom scene path as the second arg:
```bash
./run_benchmark.sh webgpu ../godot-demo-projects/3d/particles
```

### Native macOS (Metal)
- Backend: Metal 4.0 - Forward+
- Device: Apple M3 Ultra
- Runs the editor binary directly

### WebGL (Chrome)
- Template: Official Godot 4.6 stable (`web_nothreads_release.zip`)
- Location: `~/Library/Application Support/Godot/export_templates/4.6.stable.official.89cea1439/`
- Note: Cannot use our codebase's WebGL build — JS has naga/WebGPU loading code baked in

### WebGPU (Chrome)
- Template: `bin/godot.web.template_release.wasm32.nothreads.webgpu.zip`
- Built with: `scons platform=web target=template_release threads=no use_assertions=no webgpu=yes`

## Results

Machine: Apple M3 Ultra, macOS, Chrome 136

### Summary (all scenes tuned to native ≈ 50fps)

| Scene | What it tests | Native | WebGPU | WebGL | WebGPU/Native | WebGPU/WebGL |
|---|---|---|---|---|---|---|
| A: Sprites (40K) | 2D draw calls | ~50 | ~40 | ~8 | 80% | **5x** |
| B: PBR (10.4K spheres) | 3D materials | ~50 | ~35 | ~9 | 70% | **4x** |
| C: Instances (18.7K cubes) | Unique materials, transforms | ~50 | ~34 | ~8 | 68% | **4x** |
| D: Particles (2M) | GPU compute | ~29 | ~34 | ~7 | 117%* | **5x** |
| E: Skeletons (3K) | GPU skinning | ~50 | ~47 | N/A | 94% | — |
| F: PostFX (25 viewports) | SSAO/Bloom/SubViewport | ~50 | ~40 | N/A | 80% | — |
| G: Shadows (13.5K + 22 lights) | Shadow maps | ~50 | ~30 | ~7 | 60% | **4x** |
| H: Batching (35K, 10 mats) | Batched draws | ~50 | ~36 | ~22 | 72% | **1.6x** |

\* Scene D: native was not tuned to 50fps (particle count bumped to 2M to stress WebGPU below 60fps).

**WebGPU reaches 60-80% of native Metal** across workloads, and is consistently **4-5x faster than WebGL** (except batching where shared materials reduce the draw call overhead gap).

### Scene A: 2D Sprites (40,000 bouncing circles)

Sprite count tuned so native ≈ 50fps.

| Backend | FPS |
|---|---|
| Native (Metal) | ~50 |
| WebGPU (Chrome) | ~40 |
| WebGL (Chrome) | ~8 |

- **WebGPU is ~5x faster than WebGL** on this 2D sprite workload.
- WebGPU reaches ~80% of native performance.

### Scene B: PBR Materials (10,404 spheres, 102x102 grid)

Each sphere has unique StandardMaterial3D (varying metallic/roughness), 1 shadow-casting directional light.

| Backend | FPS |
|---|---|
| Native (Metal) | ~50 |
| WebGPU (Chrome) | ~35 |
| WebGL (Chrome) | ~9 |

- **WebGPU is ~4x faster than WebGL** on this PBR workload.
- WebGPU reaches ~70% of native performance.

### Scene C: Instanced Rendering (18,700 rotating cubes)

Each cube has a unique material, rotated every frame to keep transforms dirty. 1 shadow-casting directional light.

| Backend | FPS |
|---|---|
| Native (Metal) | ~50 |
| WebGPU (Chrome) | ~34 (unstable, flickers 34-56) |
| WebGL (Chrome) | ~8 |

- **WebGPU is ~4x faster than WebGL**.
- WebGPU shows frame pacing instability — some cubes visually skip updates. Needs investigation.

### Scene D: GPU Particles (2,000,000 particles)

Single GPUParticles3D emitter with ParticleProcessMaterial, small sphere draw pass.

| Backend | FPS |
|---|---|
| Native (Metal) | ~29 (highly variable) |
| WebGPU (Chrome) | ~34 (label blinking issue) |
| WebGL (Chrome) | ~7 |

- **WebGPU is ~5x faster than WebGL** on GPU particles.
- WebGPU actually outperforms native here — likely vsync/scheduling differences since native has vsync on.
- Both native and WebGPU show variable frame rates and visual artifacts (blinking FPS label on WebGPU). GPU particle workloads may stress different bottlenecks than geometry.
- Note: particle count was tuned at 550K for native ~50fps, then bumped to 2M to stress WebGPU below 60fps.

### Scene F: Post-Processing (25 SubViewports at 4096x4096, SSAO + Bloom)

Each SubViewport renders a torus with its own SSAO + Glow. Main scene has procedural sky + SSAO + Bloom.

| Backend | FPS |
|---|---|
| Native (Metal) | ~50 (some variance) |
| WebGPU (Chrome) | ~40 (some variance, label flickers) |
| WebGL (Chrome) | N/A (not supported) |

- WebGPU reaches ~80% of native on this post-processing workload.
- WebGL Compatibility renderer doesn't support SSAO/Bloom.

### Scene E: Skeletal Animation (3,025 skeletons x 16 bones)

GPU-skinned meshes with per-frame bone pose updates.

| Backend | FPS |
|---|---|
| Native (Metal) | ~50 |
| WebGPU (Chrome) | ~47 |
| WebGL (Chrome) | N/A (not supported) |

- **WebGPU reaches ~94% of native** on GPU skinning workloads.
- Previous color flickering bug at high counts was caused by timestamp query readback buffer stuck in "mapping pending" state (emdawnwebgpu doesn't reliably cancel pending maps via `wgpuBufferUnmap`). Fixed by disabling GPU timestamp queries.
- WebGL does not support GPU skinning (Compatibility renderer).

### Scene G: Shadow Stress (13,500 meshes, 22 omni shadow lights + 1 directional)

Box meshes scattered in arena with shadow-casting omni lights in a ring.

| Backend | FPS |
|---|---|
| Native (Metal) | ~50 |
| WebGPU (Chrome) | ~30 |
| WebGL (Chrome) | ~7 |

- **WebGPU is ~4x faster than WebGL** on shadow-heavy workloads.
- WebGPU reaches ~60% of native — shadow rendering is GPU-intensive.

### Scene H: Render Batching (35,000 rotating cubes, 10 shared materials)

Same mesh + 10 shared materials = batchable groups. All cubes rotate every frame.

| Backend | FPS |
|---|---|
| Native (Metal) | ~50 |
| WebGPU (Chrome) | ~36 |
| WebGL (Chrome) | ~22 |

- **WebGPU is ~1.6x faster than WebGL** — the gap is smaller here because batching reduces draw call overhead, which is WebGL's main bottleneck.
- WebGPU reaches ~72% of native.

### 3D Platformer (light scene, vsync-locked)

Both web backends hold 60fps — CPU render time is the meaningful metric.

| Metric | Native (Metal) | WebGPU (Chrome) | WebGL (Chrome) |
|---|---|---|---|
| FPS avg | 669.1 | 60.0 | 59.8 |
| **CPU render avg** | 0.07 ms | **0.36 ms** | **0.71 ms** |
| CPU render p99 | 0.16 ms | 0.47 ms | 1.10 ms |
| Draw calls avg | 156 | 234 | 202 |
| Primitives avg | 30,318 | 30,561 | 30,569 |

- **WebGPU CPU render is ~2x faster than WebGL** (0.36ms vs 0.71ms).
- GPU timestamps N/A on all backends: Metal bug (#102968), WebGL has no timer queries, WebGPU needs `chrome://flags/#enable-webgpu-developer-features`.
- WebGPU uses Mobile renderer, WebGL uses Compatibility.
