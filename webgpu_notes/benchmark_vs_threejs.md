# Godot WebGPU vs Three.js WebGPU Benchmark Comparison

## Fairness Assessment

### Scene A: 2D Sprites (40K bouncing circles)
**Fair comparison: No.**
Godot's 2D renderer automatically batches Sprite2D nodes that share the same texture — 40K nodes become a handful of draw calls internally. Three.js has no equivalent auto-batching for individual `Sprite` objects (40K draw calls = 4fps) while `InstancedMesh` (1 draw call = 60fps at 200K) is explicitly optimized beyond what Godot does. The 2D pipelines are architecturally too different.

### Scene A_Optimized: Explicit 2D Batching (1M bouncing circles)
**Fair comparison: Yes.**
Both engines use their explicit batching API: Godot's `MultiMeshInstance2D` vs Three.js `InstancedMesh`. Per-instance color, per-frame position updates. Tests raw 2D instanced rendering throughput.

### Scene B: PBR Materials (10.4K spheres, unique materials)
**Fair comparison: Yes.**
Each sphere has a unique material (different metallic/roughness), so neither engine can batch them. Both issue ~10K individual draw calls with PBR shading. Directly compares: scene graph overhead + PBR shader cost + draw call submission.

### Scene C: Instances (18.7K rotating cubes, unique materials)
**Fair comparison: Yes.**
Same as Scene B — unique materials per object, no batching possible. Adds per-frame transform updates. The Three.js equivalent is straightforward: individual `Mesh` objects with unique `MeshStandardMaterial`, rotating each frame.

### Scene D: GPU Particles (2M)
**Fair comparison: Somewhat fair.**
Both engines have one path for GPU particles — Godot uses `GPUParticles3D` with `ParticleProcessMaterial`, Three.js uses TSL-based compute shaders. The compute shader implementations differ, but there isn't a "more optimized" vs "less optimized" API choice like with sprites. Compares how well each engine's particle compute pipeline performs over WebGPU.

### Scene E: Skeletal Animation (3.4K skeletons)
**Fair comparison: Marginal.**
Both do GPU skinning with per-frame bone uploads, but internal buffer management and skinning shaders differ. Complex to replicate identically. Plus Godot WebGPU has a color flicker bug at high counts.

### Scene F: Post-Processing (25 SubViewports, SSAO + Bloom)
**Fair comparison: No.**
SSAO and Bloom are completely different algorithms between engines — different passes, blur kernels, quality tradeoffs. Benchmarks implementation quality, not WebGPU performance.

### Scene G: Shadows (13.5K meshes, 22 shadow lights)
**Fair comparison: Marginal.**
Both use cube shadow maps for point lights, but shadow resolution, filtering, and culling strategies differ. Could be made somewhat fair if parameters are matched, but internal details still vary.

### Scene H: Auto-Batching (35K cubes, 10 shared materials)
**Fair comparison: Yes.**
Both engines have 35K objects with 10 shared materials. Individual `Mesh` in Three.js matches Godot's individual `MeshInstance3D`. Tests automatic batching efficiency.

### Scene H_Batched: Explicit Batching (120K cubes, 10 colors)
**Fair comparison: Yes.**
Both engines use their explicit batching API: Godot's `MultiMesh` vs Three.js `BatchedMesh`. Single draw call, per-instance color, per-frame rotation. Tests raw instanced rendering throughput.

## Best candidates for comparison
- **Scene A_Optimized** (Explicit 2D Batching) — MultiMesh2D vs InstancedMesh
- **Scene B** (PBR) — unique materials, individual objects, 3D
- **Scene C** (Instances) — unique materials, rotating, 3D
- **Scene D** (Particles) — GPU compute particles, somewhat fair
- **Scene H** (Auto-Batching) — shared materials, automatic batching
- **Scene H_Batched** (Explicit Batching) — MultiMesh vs BatchedMesh

## Results

Machine: Apple M3 Ultra, macOS, Chrome 136

### Summary

| Scene | Godot WebGPU | Three.js WebGPU | Godot/Three.js | Fair? | Notes |
|---|---|---|---|---|---|
| A: Sprites (40K) | ~40 | ~4 | **10x** | Somewhat | Godot auto-batches 2D; Three.js can't |
| A_Opt: Explicit 2D (1M) | ~8 | ~39 | **0.2x** | Yes | MultiMesh2D vs InstancedMesh |
| B: PBR (10.4K spheres) | ~35 | ~29 | **1.2x** | Yes | PBR shading dominates, small gap |
| C: Instances (18.7K cubes) | ~34 | ~17 | **2x** | Yes | Draw call overhead gap |
| D: Particles (2M) | ~34 | ~27 | **1.3x** | Somewhat | Compute + instanced spheres |
| E: Skeletons (3.4K) | bug | — | — | Marginal | Godot has color flicker bug |
| F: PostFX (25 viewports) | ~40 | — | — | No | Different SSAO/Bloom algorithms |
| G: Shadows (13.5K, 22 lights) | ~30 | — | — | Marginal | Different shadow map strategies |
| H: Auto-Batch (35K, 10 mats) | ~36 | ~9 | **4x** | Yes | Godot auto-batches; Three.js does not |
| H_Batched: Explicit (120K) | ~49 | ~60 | **0.8x** | Yes | MultiMesh vs BatchedMesh |

For fair comparisons: Godot WebGPU is faster when auto-batching matters (1.2-4x), but Three.js outperforms Godot on explicit batching APIs (InstancedMesh/BatchedMesh) by 1.2-5x.

### Scene A: 2D Sprites (40,000 bouncing circles)

40K individual sprite objects using idiomatic API: Godot `Sprite2D` (auto-batched) vs Three.js `Sprite` (individual draw calls).

| Backend | FPS |
|---|---|
| Native (Metal/Godot) | ~50 |
| Godot WebGPU | ~40 |
| Three.js WebGPU (individual Sprite) | ~4 |
| Godot WebGL | ~8 |

- Not a fair GPU comparison — tests whether the engine has built-in 2D batching.
- Godot auto-batches Sprite2D nodes sharing a texture. Three.js issues 40K draw calls.

### Scene A_Optimized: Explicit 2D Batching (1,000,000 bouncing circles)

Both engines use explicit batching: Godot `MultiMeshInstance2D` vs Three.js `InstancedMesh`. Per-instance color, per-frame position updates via `set_instance_transform_2d` / `setMatrixAt`.

| Backend | FPS |
|---|---|
| Three.js WebGPU (InstancedMesh, 1M) | ~39 |
| Godot WebGPU (MultiMesh2D, 1M) | ~8 |

- **Three.js is ~5x faster than Godot** for explicit 2D instanced rendering at 1M instances.
- Both hit 60fps at 200K (vsync-capped). At 1M the gap is dramatic.
- Godot's bottleneck is likely the GDScript per-frame loop calling `set_instance_transform_2d` 1M times, while Three.js updates a typed array and uploads in bulk.

### Scene B: PBR Materials (10,404 spheres, 102x102 grid)

Each sphere has a unique material (varying metallic/roughness by grid position, random albedo). 1 shadow-casting directional light. Static scene (no rotation).

| Backend | FPS |
|---|---|
| Native (Metal/Godot) | ~50 |
| Godot WebGPU | ~35 |
| Three.js WebGPU | ~29 |
| Godot WebGL | ~9 |

- **Godot WebGPU is ~1.2x faster than Three.js WebGPU** on PBR spheres with unique materials.
- Closer gap than Scene C — PBR shading cost dominates over draw call overhead.
- Both use individual mesh objects with unique materials — no batching possible.

### Scene C: Instanced Rendering (18,700 rotating cubes, unique materials)

Each cube has a unique material (random color, metallic, roughness). 1 shadow-casting directional light. All cubes rotate every frame.

| Backend | FPS |
|---|---|
| Native (Metal/Godot) | ~50 |
| Godot WebGPU | ~34 |
| Three.js WebGPU | ~17 |
| Godot WebGL | ~8 |

- **Godot WebGPU is ~2x faster than Three.js WebGPU** on unique-material draw calls.
- Both use individual scene objects with unique materials — no batching possible.

### Scene D: GPU Particles (2,000,000 particles)

Godot: `GPUParticles3D` with `ParticleProcessMaterial`, instanced sphere draw pass (radius=0.05, 8 segments, 4 rings). Three.js: TSL compute shader for simulation, `InstancedBufferGeometry` with matching `SphereGeometry(0.05, 8, 4)` draw pass.

| Backend | FPS |
|---|---|
| Native (Metal/Godot) | ~29 |
| Godot WebGPU | ~34 |
| Three.js WebGPU | ~27 |
| Godot WebGL | ~7 |

- **Godot WebGPU is ~1.3x faster than Three.js WebGPU** with matching draw pass (instanced sphere meshes).
- Both use GPU compute for particle simulation + hardware instancing for the draw call.
- Three.js shows high variance (p1=6.5, p99=80.8) — likely shader compilation warmup in early frames.
- Godot WebGPU outperforms native Metal here, likely due to vsync/scheduling differences.

### Scene H: Auto-Batching (35,000 rotating cubes, 10 shared materials)

35K cubes with 10 shared materials (cycle i % 10). All cubes rotate every frame. Tests whether the engine can batch draw calls for objects sharing materials.

| Backend | FPS |
|---|---|
| Native (Metal/Godot) | ~50 |
| Godot WebGPU (auto-batched) | ~36 |
| Three.js WebGPU (individual Mesh) | ~9 |
| Godot WebGL | ~22 |

- **Godot WebGPU is ~4x faster than Three.js WebGPU** — Godot automatically batches objects sharing materials; Three.js issues 35K individual draw calls.
- Three.js has no automatic draw call merging — every `Mesh` is a separate draw call regardless of shared materials.
- Even Godot WebGL (22fps) outperforms unbatched Three.js WebGPU (9fps).

### Scene H_Batched: Explicit Batching (120,000 rotating cubes, 10 colors)

120K cubes using explicit batching APIs: Godot's `MultiMesh` vs Three.js `BatchedMesh`. Per-instance vertex color, per-frame Y-axis rotation. Single material, single draw call.

| Backend | FPS |
|---|---|
| Godot WebGPU (MultiMesh, 120K) | ~49 |
| Three.js WebGPU (BatchedMesh, 120K) | ~60 (vsync) |
| Godot WebGPU (MultiMesh, 200K) | ~29 |
| Three.js WebGPU (BatchedMesh, 200K) | ~38 |

- **Three.js `BatchedMesh` is ~1.2-1.3x faster than Godot `MultiMesh`** at equivalent instance counts.
- At 120K: Three.js is vsync-locked at 60fps while Godot is at 49fps.
- At 200K: Three.js drops to 38fps, Godot to 29fps — Three.js maintains a ~1.3x advantage.
- The per-frame cost is dominated by `setMatrixAt`/`set_instance_transform` calls (120K-200K matrix updates per frame) and GPU instanced rendering.
- Both APIs require scene restructuring (single batched object instead of individual nodes/meshes).
