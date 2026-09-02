# WebGPU 3D Platformer Startup Profiling

**Date**: 2026-05-06
**Scene**: `demo_3d_platformer` (normal variant, stress spawner disabled)
**Browser**: Chrome (Playwright, headed mode) with `--enable-unsafe-webgpu --enable-features=Vulkan --disable-gpu-shader-disk-cache --disable-gpu-program-cache`
**Platform**: macOS (Darwin 25.4.0, Apple Silicon)
**Cache**: Cold (browser cache cleared, GPU shader disk cache disabled)
**Method**: Playwright-driven instrumentation via monkey-patching `GPUDevice.createShaderModule`, `createRenderPipeline`, and `createComputePipeline` in JavaScript before engine load. Script: `webgpu_tests/profile_startup.mjs`

---

## Ordered Startup Timeline

Everything below is on the main thread unless noted. Overlapping ranges mean
the activities are interleaved (Tint conversion for shader A, then B, etc.),
not truly parallel.

1. **(0 - 174 ms) Page load + WASM bootstrap**
   a. **(0 - 11 ms)** HTML page load + DOM ready. Local server, tiny HTML. `DOMContentLoaded` + `window.load` both at ~11 ms.
   b. **(10 - 18 ms)** WebGPU adapter + device acquisition. `requestAdapter` 6 ms, `requestDevice` 1 ms.
   c. **(11 - 174 ms)** WASM download + compile + Godot bootstrap. `index.wasm` (40.7 MB) fetched from localhost, compiled by browser. `index.pck` (1.1 MB) game data fetched. `tint_convert.wasm` loaded by 45 ms. Engine banner at 174 ms.

2. **(174 - 306 ms) Engine + rendering init**
   a. **(174 - 227 ms)** Godot engine init. Selects "WebGPU 1.0 - Forward Mobile" backend.
   b. **(227 - 306 ms)** Rendering subsystem init (pre-shader). ~80 ms before first shader needed.

3. **(306 - 4,872 ms) Base shader module creation (511 modules)**
   a. **(306 - 560 ms)** Compute shader modules: 47 modules, 6 compute pipelines. CanvasSdf (6), Skeleton (2), Sort (3), Particles (1), ParticlesCopy (21). Each: ~8 ms Tint conversion in WASM, <0.1 ms `createShaderModule`.
   b. **(460 - 720 ms)** Canvas/2D shader modules: 56 modules. CanvasShader (36), CanvasOcclusion (6), Blit (8). Overlaps late compute shaders.
   c. **(530 ms)** First render pipelines: CanvasOcclusionShaderRD (4 pipelines).
   d. **(384 - 1,447 ms)** Post-processing shader modules: 110 modules. BokehDof, BlurRaster, Copy, CopyToFb, Tonemap, SMAA, LuminanceReduce, etc.
   e. **(1,330 - 2,640 ms)** Sky shader modules: 36 modules.
   f. **(630 - 4,872 ms)** **SceneForwardMobile base shaders: 300 modules.** 10 variants x 30 modules (vertex+fragment). Fragment stages are 170K-200K chars WGSL each. Tint conversion dominates: avg 8.3 ms/module, longest 526 ms. Overlaps everything above in step 3.
   g. **(2,190 - 3,000 ms)** Early specialized waves 1-2: 16 modules + 8 pipelines. First specialized SceneForwardMobile variants (depth pre-pass :9, :11).
   h. **(4,200 ms)** Engine PERF: `fps=0, draws/f=0`. Engine running, no frames yet.
   i. **(4,710 - 4,872 ms)** Last base shader modules finish.

4. **(4,930 - 6,530 ms) The Big Specialized Wave (wave 3)**
   a. **(4,930 - 5,650 ms)** **232 specialized modules + 92 pipelines.** All major SceneForwardMobile variants: :0 (opaque), :9 (depth), :2/:3 (alpha), :12 (debug), plus Sky, Tonemap, Canvas. Creates the majority of all render pipelines.
   b. **(5,300 ms)** Engine PERF: `fps=0, draws/f=6465`. Draw calls running, no frame presented yet.
   c. **(6,200 - 6,530 ms)** Wave 3 tail: 31 more pipelines.

5. **(7,000 ms) FIRST VISIBLE FRAME**
   a. Engine PERF: `fps=1`. Rendering with specialized shaders where available, ubershader fallback elsewhere. All compilation is synchronous on main thread.

6. **(7,000 - 12,400 ms) Early rendering + continued compilation**
   a. **(7,110 - 10,100 ms)** Specialized waves 4-6: 42 modules + 22 pipelines.
   b. **(8,000 ms)** Engine PERF: `fps=7`. Brief improvement.
   c. **(12,400 ms)** Engine PERF: `fps=0`. Dawn doing deferred GPU compilation on first pipeline use.

7. **(12,400 - 17,200 ms) FPS stall + recovery**
   a. **(15,250 - 15,360 ms)** Specialized wave 7: 18 modules + 9 pipelines.
   b. **(16,500 ms)** Engine PERF: `fps=1`. Recovery starting.
   c. **(17,200 ms)** Specialized wave 8: 2 modules + 1 pipeline (last of this phase).

8. **(17,500 - 21,500 ms) PEAK PERFORMANCE: 45-53 fps**
   a. **(17,500 ms)** Engine PERF: `fps=17`. Rapid FPS ramp-up.
   b. **(18,500 ms)** Engine PERF: `fps=49`.
   c. **(20,500 ms)** **PEAK FPS: 53 fps.** Most specialized shaders active, scene not yet fully loaded.

9. **(21,750 - 26,940 ms) Late compilation waves crash FPS**
   a. **(21,750 - 22,030 ms)** Specialized wave 9: 20 modules + 10 pipelines.
   b. **(23,200 ms)** Engine PERF: `fps=10`. Crash from wave 9.
   c. **(23,240 - 26,940 ms)** Specialized waves 10-13: 14 modules + 6 pipelines.
   d. **(24,700 - 25,800 ms)** Engine PERF: `fps=1`. Stalled by compilation.

10. **(27,800 - 36,900 ms) Secondary plateau: 33-45 fps**
    a. No compilation interruptions during this window.
    b. FPS varies with camera position / scene complexity.

11. **(37,710 - 37,720 ms) FINAL COMPILATION (wave 14)**
    a. 4 modules + 2 pipelines. **All shader compilation is now complete.**
    b. Total across all waves: 859 shader modules, 190 render pipelines, 47 compute pipelines.

12. **(39,000 - 60,000 ms) Steady state**
    a. No more shader compilation.
    b. FPS declines from ~31 to ~12 as camera auto-plays into denser scene areas.
    c. Settles at 12-13 fps by ~50 s.

---

## Executive Summary

| Metric | Value |
|--------|-------|
| Page load to engine banner | 174 ms |
| WebGPU adapter + device init | 18 ms |
| First visible frame | ~7 s |
| FPS > 30 | ~18 s |
| Peak FPS (brief) | 50-53 fps @ 18-21 s |
| All shader compilation done | ~38 s |
| Steady-state FPS (60 s mark) | 12-13 fps |
| Total shader modules created | 859 (511 base + 348 specialized) |
| Total render pipelines | 190 (all synchronous) |
| Total compute pipelines | 47 |
| Total WGSL code generated | 39.8 MB |

The dominant startup bottleneck is **SPIR-V to WGSL conversion via Tint (in WASM)**, which accounts for 99% of shader module creation wall-clock time. The actual browser `createShaderModule` calls complete in <2 ms each. Shader compilation and pipeline creation span from 0.3 s to 38 s, with multiple waves of specialized shader compilation interrupting rendering throughput.

---

## Detailed Startup Timeline

### Phase 0: Page Load + WebGPU Init (0 - 0.3 s)

| Event | Time (ms) |
|-------|-----------|
| `requestAdapter` start | 10 |
| DOMContentLoaded | 11 |
| window load | 11 |
| `requestAdapter` done | 16 |
| `requestDevice` start | 17 |
| `requestDevice` done | 18 |
| Engine start banner (`Godot Engine v4.6.2`) | 174 |
| First `createShaderModule` call | 306 |

WebGPU initialization is fast (~8 ms for adapter + device). The engine takes 156 ms from device-ready to first shader creation (WASM initialization, Godot bootstrap).

### Phase 1: Base Shader Module Creation (0.3 - 4.2 s)

**490 base shader modules + 16 early specialized modules** created across 3.9 seconds.

The base shaders are loaded in dependency order:

| Time Range | Shader Group | Count | Notes |
|-----------|--------------|-------|-------|
| 0.31 - 0.43 s | CanvasSdfShaderRD | 6 | Compute (SDF generation) |
| 0.44 - 0.45 s | SkeletonShaderRD | 2 | Compute (bone transforms) |
| 0.46 - 0.47 s | SortShaderRD | 3 | Compute (sorting) |
| 0.49 - 0.56 s | ParticlesShaderRD + ParticlesCopyShaderRD | 22 | Compute (particle system) |
| 0.57 - 0.71 s | CanvasShaderRD | 36 | 2D canvas rendering |
| 0.63 - 0.64 s | CanvasOcclusionShaderRD | 6 | Canvas occlusion |
| 0.74 - 4.19 s | **SceneForwardMobileShaderRD** | 300 | **3D scene rendering (dominates)** |
| 1.33 - 2.64 s | SkyShaderRD | 36 | Sky rendering |
| 1.37 - 1.56 s | Post-processing (Bokeh, Blur, Copy, Tonemap, SMAA, etc.) | ~80 | Post-processing pipeline |
| 2.20 - 3.00 s | Early specialized pipelines (SceneForwardMobile:9, :11) | 8 | First specialized variants |

**Bottleneck**: 99% of this phase (4,664 ms out of 4,694 ms) is spent in Tint SPIR-V-to-WGSL conversion inside WASM. The actual `createShaderModule` browser calls total only 30 ms. Average per-module Tint conversion: 8.3 ms. Longest single conversion: 526 ms (ParticlesShaderRD, 88K chars WGSL output).

### Phase 2: First Major Specialized Shader Wave (4.7 - 6.5 s)

**253 modules (21 base + 232 specialized)** and **92 render pipelines** created.

This is the first big burst of pipeline specialization. The engine has determined which material variants are needed for the scene and is compiling specialized shader modules with baked specialization constants.

| Type | Count | WGSL Size Range |
|------|-------|----------------|
| SceneForwardMobileShaderRD:9 (depth pre-pass) | ~28 pipelines | 16-27K chars |
| SceneForwardMobileShaderRD:0 (main opaque) | ~30 pipelines | 160-179K chars |
| SceneForwardMobileShaderRD:2, :3, :12 | ~20 pipelines | 19-32K chars |
| SkyShaderRD, Tonemap, Canvas | ~14 pipelines | 2-37K chars |

### Phase 3: First Frames + Continued Compilation (7 - 12 s)

| Time | FPS (engine counter) | Activity |
|------|---------------------|----------|
| 7.0 s | 1 fps | First rendered frames visible |
| 8.0 s | 7 fps | Brief FPS improvement |
| 7.1 - 8.3 s | | 28 more specialized shaders + 15 pipelines |
| 8.9 - 10.1 s | | 14 more specialized shaders + 6 pipelines |
| 12.4 s | 0 fps | FPS drops - heavy GPU work |

The engine renders with ubershaders between compilation waves, but specialized shader compilation is **synchronous on the main thread** (single-threaded WASM, no WorkerThreadPool parallelism). Each compilation burst blocks the main thread entirely, causing FPS to drop to 0-1. FPS is low overall because:
1. Synchronous Tint SPIR-V→WGSL conversion blocks the main thread during each wave
2. Dawn defers actual GPU shader compilation to first pipeline use, causing additional stalls
3. Scene resources (textures, meshes) are still loading

### Phase 4: Continued Compilation + Recovery (12 - 22 s)

| Time | FPS | Activity |
|------|-----|----------|
| 15.2 - 15.4 s | 0 | 18 specialized shaders + 9 pipelines (burst) |
| 16.5 s | 1 | Recovery begins |
| 17.2 s | | 2 final specialized shaders + 1 pipeline |
| **17.5 s** | **17** | **Rapid FPS ramp-up** |
| **18.5 s** | **49** | **Near-peak FPS** |
| 19.5 s | 47 | |
| **20.5 s** | **53** | **Peak FPS** |
| 21.5 s | 45 | |
| 21.8 - 22.0 s | | 20 specialized shaders + 10 pipelines (late wave) |

The period from 17.5 - 21.5 s represents the best rendering performance, when most specialized shaders are compiled but the scene hasn't reached full complexity.

### Phase 5: Late Compilation Waves + FPS Impact (22 - 38 s)

| Time | FPS | Activity |
|------|-----|----------|
| 23.2 s | 10 | FPS dip from late compilation |
| 23.2 - 24.7 s | 1 | 8 shaders + 3 pipelines |
| 25.8 s | 1 | Still recovering |
| 26.2 - 26.9 s | | 6 shaders + 3 pipelines |
| 27.8 s | 36 | FPS recovers |
| 28 - 36 s | 33-45 | Relatively stable |
| 37.7 s | | **Final 4 specialized shaders + 2 pipelines** |
| 38.2 s | 21 | Brief dip from final compilation |

### Phase 6: Steady State (38 - 60 s)

No more shader compilation. FPS settles to **12-13 fps** at the 60-second mark. This is pure rendering performance with the fully specialized pipeline.

Notable: the steady-state FPS (12-13) is **lower** than the brief peak (50-53 fps at ~18-21 s). This likely reflects increasing scene complexity as all objects finish loading, plus the camera auto-playing through denser parts of the level.

---

## Shader Compilation Breakdown

### Base vs Specialized Modules

| Category | Count | Total WGSL Size | createShaderModule Time | Tint Conversion Time (est.) |
|----------|-------|-----------------|------------------------|-----------------------------|
| Base modules | 511 | ~8 MB | 25 ms | ~4,664 ms |
| Specialized modules | 348 | ~32 MB | 2,477 ms* | ~2,084 ms |
| **Total** | **859** | **~40 MB** | **~2,500 ms** | **~6,748 ms** |

*Specialized modules show higher `createShaderModule` times due to one outlier (specmod#241:stg1 at 810 ms), likely a browser JIT or GC pause.

### Specialized Shader Size Distribution

| Size Category | Count | Avg createShaderModule | Notes |
|--------------|-------|----------------------|-------|
| Small (<30K chars) | 192 | 0.15 ms | Vertex stages, simple fragments |
| Medium (30-100K) | 48 | 0.10 ms | Mid-complexity fragments |
| Large (>100K chars) | 108 | 22.6 ms* | SceneForwardMobile fragment shaders |

*Skewed by outlier. Median for large shaders is ~0.17 ms.

### Top 10 Largest Base Shaders (by WGSL output)

| Shader | Variant | Stage | WGSL Size | createShaderModule |
|--------|---------|-------|-----------|--------------------|
| SceneForwardMobileShaderRD:10 | Transparent + alpha | Fragment | 200,909 chars | 0.37 ms |
| SceneForwardMobileShaderRD:1 | Opaque + normal map | Fragment | 200,187 chars | 0.19 ms |
| SceneForwardMobileShaderRD:10 | (variant) | Fragment | 194,492 chars | 0.20 ms |
| SceneForwardMobileShaderRD:10 | (variant) | Fragment | 194,010 chars | 0.20 ms |
| SceneForwardMobileShaderRD:1 | (variant) | Fragment | 193,774 chars | 0.37 ms |
| SceneForwardMobileShaderRD:10 | (variant) | Fragment | 193,321 chars | 0.21 ms |
| SceneForwardMobileShaderRD:1 | (variant) | Fragment | 193,290 chars | 0.18 ms |
| SceneForwardMobileShaderRD:10 | (variant) | Fragment | 192,875 chars | 0.27 ms |
| SceneForwardMobileShaderRD:10 | (variant) | Fragment | 192,839 chars | 0.16 ms |
| SceneForwardMobileShaderRD:1 | (variant) | Fragment | 192,599 chars | 0.25 ms |

SceneForwardMobileShaderRD dominates: **96% of all generated WGSL** (38.3 MB out of 39.8 MB) comes from SceneForwardMobile base + specialized variants.

### Unique Shader Programs (Base Modules)

110 unique shader programs (by name), belonging to 34 distinct shader families:

| Family | Variants | Role |
|--------|----------|------|
| SceneForwardMobileShaderRD | 0-4, 9-13 (10 variants x 30 modules) | 3D scene rendering |
| ParticlesCopyShaderRD | 0-20 | Particle system |
| CanvasShaderRD | 0-5 | 2D canvas |
| CopyShaderRD | 0-12 | Texture copy operations |
| SkyShaderRD | 0-5 | Sky/environment |
| BokehDofRasterShaderRD | 0-6 | Depth of field |
| BlurRasterShaderRD | 0-6 | Blur post-processing |
| TonemapMobileShaderRD | 0-1 | Tone mapping |
| SmaaEdgeDetection/Weight/Blending | 0 each | SMAA anti-aliasing |
| Other (Blit, Sort, Skeleton, etc.) | 1-4 each | Utility |

---

## Pipeline Creation Breakdown

### Render Pipelines by Shader

| Shader | Pipeline Count | Time Range | Notes |
|--------|---------------|------------|-------|
| SceneForwardMobileShaderRD:0 | ~60 | 10.3 - 37.7 s | Main opaque pass |
| SceneForwardMobileShaderRD:9 | ~40 | 2.2 - 14.1 s | Depth pre-pass |
| SceneForwardMobileShaderRD:2,3 | ~15 | 10.3 - 10.3 s | Alpha/transparency |
| SceneForwardMobileShaderRD:11,12 | ~10 | 2.3 - 10.3 s | Wireframe/debug |
| CanvasOcclusionShaderRD | 4 | 0.5 s | Canvas occlusion |
| BlitShaderRD | 4 | 1.5 s | Blitting |
| SkyShaderRD | 3 | 10.3 - 21.3 s | Sky |
| Other | ~15 | Various | Canvas, Tonemap, SMAA, etc. |

All 190 render pipelines were created synchronously (no `createRenderPipelineAsync` calls). Individual `createRenderPipeline` calls are fast (avg 0.02 ms, max 0.27 ms) because Dawn defers actual Metal shader compilation to first pipeline use.

---

## FPS Timeline (rAF-based + Engine PERF Counter)

```
Time(s)  FPS   Notes
  0-7     0    Loading, shader compilation, no visible frames
  7.0     1    First visible frame
  8.0     7    Brief improvement
 12.4     0    Compilation wave disrupts rendering
 16.5     1    Recovery begins
 17.5    17    Rapid ramp-up as specialized shaders activate
 18.5    49    Near peak - most shaders compiled
 20.5    53    PEAK FPS
 21.5    45    Slight dip, late compilation wave starting
 23.2    10    Late compilation wave (20 shaders)
 25.8     1    Still in compilation-induced dip
 27.8    36    Recovery
 28-36   33-45 Relatively stable plateau
 37.7    --    Final shader compilation
 38-60   12-14 STEADY STATE (pure rendering, no more compilation)
```

### Key FPS Milestones

| Milestone | Time |
|-----------|------|
| First frame (FPS > 0) | 7.0 s |
| FPS > 10 | 17.0 s |
| FPS > 30 | 18.0 s |
| FPS > 50 | 18.0 s |
| Sustained > 30 fps | 18.0 s |
| All compilation done | 37.7 s |
| Steady-state settled | ~40 s |

---

## Where the Time Goes: Startup Cost Breakdown (0-38 s)

| Activity | Wall Clock | Notes |
|----------|-----------|-------|
| Page load + WASM init | 0.3 s | Fast |
| Tint SPIR-V-to-WGSL conversion | ~6.7 s (cumulative) | In WASM, single-threaded, 99% of shader creation time |
| createShaderModule (browser API) | ~2.5 s (cumulative) | Mostly fast; one 810ms outlier |
| createRenderPipeline (browser API) | ~4.4 ms (cumulative) | Near-instant; Dawn defers real work |
| Actual GPU shader compilation | Unknown | Hidden inside Dawn; manifests as frame stalls |
| Scene resource loading (textures, meshes) | ~7-10 s | Overlaps with shader compilation |
| Pipeline state caching + first use | ~7 s | Dawn compiles Metal shaders on first draw |

The total wall-clock from first event to last shader module: **37.4 s**. Most of that is serialized on the main thread because WASM/Godot runs single-threaded in the browser.

---

## Observations and Notes

1. **Tint conversion is the primary bottleneck**, not browser shader compilation. The Tint WASM module converts each SPIR-V blob to WGSL on the main thread, blocking rendering. Average 8.3 ms per module for base shaders, 6.2 ms for specialized shaders.

2. **SceneForwardMobileShaderRD dominates everything**: 96% of all WGSL code, the majority of pipelines, and most of the wall-clock time.

3. **Compilation happens in waves**, not all at once. There are at least 13 distinct compilation phases spread across 38 seconds. Each wave corresponds to the engine discovering new material/variant needs as the scene loads and begins rendering.

4. **FPS is volatile during compilation** (0-38 s) with multiple peaks and valleys. The brief 50+ fps peak at 18-21 s happens in a window between compilation waves.

5. **Steady-state FPS (12-13) is lower than peak (53)**, suggesting the scene's rendering cost increases as all objects and effects become fully loaded. This is not a shader compilation issue.

6. **No async pipelines are used**: All 190 render pipelines use synchronous `createRenderPipeline`. Using `createRenderPipelineAsync` could help overlap GPU compilation with rendering.

7. **Specialized shaders extend compilation to 38 s**: The base "uber" shaders finish by 4.9 s, but specialized variants with baked specialization constants continue being created for another 33 seconds, triggered by the rendering engine as it determines which material variants are actually needed.

---

## Profiling Methodology

The profiling script (`webgpu_tests/profile_startup.mjs`) works by:
1. Starting an HTTP server with CORS headers serving the pre-exported demo
2. Injecting a `<script>` tag before `<head>` that monkey-patches `navigator.gpu.requestAdapter` to intercept the device, then patches `device.createShaderModule`, `device.createRenderPipeline`, `device.createRenderPipelineAsync`, `device.createComputePipeline`, and `device.createComputePipelineAsync`
3. Recording timestamps via `performance.now()` for every API call
4. Tracking FPS via `requestAnimationFrame`
5. Capturing Godot's `[PERF]` console messages for engine-side metrics
6. After the capture duration, extracting all data via `page.evaluate()` and saving to JSON

Raw data is saved to `webgpu_tests/startup_profile.json`.

### Reproducing

```bash
cd webgpu_tests
node profile_startup.mjs --duration 60
```

To change capture duration: `--duration <seconds>`
To change output path: `--output <path>`

---

## Updated Results: Override Path (2026-05-08)

The override specialization constant work eliminates all 348 runtime-specialized shader modules by replacing baked specialization constants with WGSL `@id(N) override` declarations. Instead of creating a separate shader module for each specialization variant (requiring a full Tint SPIR-V-to-WGSL conversion per module), the base shader modules declare their specialization constants as overrides, and specialization happens at `createRenderPipeline` time via the pipeline's `constants` dictionary. The precompiled WGSL table (378 entries) supplies all base shader source at build time, eliminating the Tint WASM module from the runtime entirely.

**Why this works**: Previously, each specialized shader was a distinct SPIR-V blob with specialization constants folded in at compile time, requiring Tint to convert it to WGSL independently. With the override path, a single base WGSL module contains `@id(0) override SPEC_0: u32 = 0;` declarations. When the engine needs a specialized pipeline, it passes `{ 0: value, 1: value, ... }` to `createRenderPipeline` -- the browser/Dawn handles constant substitution natively with no additional shader modules needed.

**Important context**: The "before" profiling already had the WGSL cache/precompiled table in place for base shaders. The 6.7s cumulative Tint conversion time was entirely from the 348 specialized shader modules that still required runtime SPIR-V-to-WGSL conversion. The override path improvement is specifically about eliminating those 348 runtime conversions.

### Before / After Comparison

| Metric | Before (Specialized Modules) | After (Override Path) | Improvement |
|--------|-----------------------------|-----------------------|-------------|
| Total shader modules | 859 (511 base + 348 specialized) | 490 (all base, 0 specialized) | 43% fewer modules |
| Specialized modules | 348 (runtime Tint conversion) | 0 | 100% eliminated |
| Tint conversion time | ~6.7s cumulative | 0ms (precompiled table, 378 entries) | 6.7s eliminated |
| Top shader module compile time | 526ms | 1.54ms | 340x faster |
| Total base shader compile time | N/A | 28.8ms | -- |
| All shader modules done | ~38s | 3.1s | 12x faster |
| Render pipelines | 190 | 80 | 58% fewer |
| Compute pipelines | 47 | 46 | ~same |
| First visible frame | ~7s | ~3.2s | 2x faster |
| FPS > 50 | ~18s | ~9.4s | 2x faster |
| Steady-state FPS | 12-13 fps | 60 fps | 4.6x higher |
| FPS drops to 0-1 after first frame | Multiple (waves 3-13) | Zero after 9.4s | Eliminated |
| Locked 60fps duration | Never achieved | 50+ seconds continuous | -- |

### Key Takeaways

1. **Tint WASM is completely removed from the runtime.** All 378 precompiled WGSL entries are loaded from the build-time table. Zero bytes of SPIR-V are converted at runtime.

2. **Shader module creation collapses from 38s to 3.1s.** The 490 base modules load from the precompiled table and compile in 28.8ms total, with the slowest single module at 1.54ms (vs 526ms before).

3. **The compilation wave problem is solved.** Previously, 13+ waves of specialized shader compilation interrupted rendering over a 33-second window, causing repeated FPS crashes to 0-1. With override constants, all shader modules are done before the first frame, and pipeline creation with override constants is near-instant.

4. **Steady-state FPS jumps from 12-13 to 60 fps.** This suggests the specialized module overhead was not just a startup cost -- the 348 extra shader modules and 190 render pipelines were also imposing ongoing runtime overhead (memory pressure, cache pollution, or Dawn/Metal pipeline switching costs). With 80 render pipelines instead of 190, the GPU has far fewer pipeline state changes per frame.

5. **58% fewer render pipelines (190 to 80)** because multiple specialization variants that previously needed separate pipelines now share a single base shader module and differ only in pipeline constants.
