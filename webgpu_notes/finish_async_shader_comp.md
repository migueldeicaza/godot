# Finishing Async Shader Compilation

**Date**: 2026-05-07
**Branch with existing work**: `async_shader_pipeline`
**Current branch**: `webgpu-4.6.2`
**Goal**: Eliminate main-thread shader compilation stalls and reduce time-to-interactive

---

## Background: What Was Built

Two-part async shader pipeline was implemented on `async_shader_pipeline` branch (commit `864e936cd3` + `eb9d522e92`):

### Part 1: Tint Web Worker (SPIR-V → WGSL translation off main thread)

**Status: Working correctly. Never merged.**

Files:
- `platform/web/js/engine/tint_worker.js` (new) — standalone Web Worker that loads its own copy of `tint_convert.wasm` and handles `translate` messages
- `platform/web/js/engine/engine.js` — spawns worker, exposes `window._tintWorkerPostTranslation()`, `window._tintWorkerPollCount()`, `window._tintWorkerGetResult()` for C++ to call via EM_ASM
- `drivers/webgpu/rendering_device_driver_webgpu.cpp` — `_post_spirv_to_worker()`, `_poll_tint_worker_results()`, `PendingWorkerPipeline` struct, deferral logic, 60-poll timeout fallback to sync
- `platform/web/emscripten_helpers.py` — bundles `tint_worker.js` in export template zip

How it works:
1. When a specialized pipeline is requested and the WGSL isn't in `_spv_to_wgsl_cache`, the patched SPIR-V bytes are posted to the worker (with 64-bit hash as two 32-bit halves to avoid JS precision loss)
2. Worker translates SPIR-V → WGSL in parallel with main thread rendering
3. Each frame, `_poll_tint_worker_results()` (called from `fence_wait`) drains completed results from a JS queue back into `_spv_to_wgsl_cache`
4. When all WGSL dependencies for a pending pipeline are cached, it proceeds to GPU pipeline creation
5. If results don't arrive within 60 polls, falls back to sync Tint on main thread

Benchmark results (from the branch):
- Max frame spike: 133ms → 67ms (-50%)
- Spikes >65ms: 8 → 2 (-75%)
- P95 frame time: 49.9ms → 34.3ms (-31%)
- No steady-state regression

### Part 2: Async GPU Pipeline Creation (wgpuDeviceCreateRenderPipelineAsync)

**Status: Has a blocking callback delivery bug. Never merged.**

Files:
- `drivers/webgpu/rendering_device_driver_webgpu.cpp` — `render_pipeline_create_async()` override, `AsyncPipelineCallbackData`, `_wgpu_async_pipeline_callback()`
- `drivers/webgpu/rendering_device_driver_webgpu.h` — `PipelineCreatedCallback` typedef, virtual declaration
- `servers/rendering/rendering_device.cpp` — `RenderPipelineAsyncCallback`, `render_pipeline_create_async()`, `AsyncPipelineContext`, `_async_pipeline_created()` (RID wrapping in callback)
- `servers/rendering/rendering_device.h` — public API additions
- `servers/rendering/rendering_device_driver.h` — `API_TRAIT_ASYNC_PIPELINE_COMPILATION`, base class virtual with sync fallback
- `servers/rendering/renderer_rd/forward_mobile/scene_shader_forward_mobile.cpp` — async path in `_create_pipeline()` for non-ubershader pipelines
- `servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.cpp` — same
- `servers/rendering/renderer_rd/renderer_canvas_render_rd.cpp` — same (unused on mobile but wired up)

How it works:
1. `_create_pipeline()` checks `supports_async_pipeline_compilation()` — if true and not ubershader, calls `render_pipeline_create_async()` instead of `render_pipeline_create()`
2. WebGPU driver override builds the full `WGPURenderPipelineDescriptor` and calls `wgpuDeviceCreateRenderPipelineAsync(device, &desc, cb_info)`
3. On completion, callback creates `WGPipelineWrapper`, wraps in RID, and calls `add_compiled_pipeline()` to push into `PipelineHashMapRD::compiled_queue`
4. The render loop's `get_pipeline()` call checks `compiled_queue` for new pipelines each frame and swaps from ubershader to specialized

---

## What Went Wrong: The Callback Delivery Bug

### emdawnwebgpu callback modes

emdawnwebgpu (the Emscripten WebGPU binding) supports two callback modes:

**`WGPUCallbackMode_AllowProcessEvents`** — callback fires when `wgpuInstanceProcessEvents()` is called. Godot calls this from `fence_wait()` every frame. This is the preferred mode because it gives us control over timing.

**`WGPUCallbackMode_AllowSpontaneous`** — callback fires whenever the JS microtask queue resolves (Promise.then). No control over timing; fires during rendering.

### What happened

1. **`AllowProcessEvents` was used (correct choice) but callbacks never fired.** The `wgpuDeviceCreateRenderPipelineAsync` Promise resolved on the JS side, but emdawnwebgpu never delivered the callback to C++ during `wgpuInstanceProcessEvents()`. Async pipeline counters showed `100/0/0` (100 initiated, 0 completed, 0 failed). Likely an emdawnwebgpu bug — `AllowProcessEvents` works fine for buffer map callbacks but not for `CreateRenderPipelineAsync`.

2. **Switched to `AllowSpontaneous` as a test** — callbacks fired correctly (35/35 completed), specialized pipelines compiled, validation passed. But performance regressed catastrophically:

| Metric | Baseline (no async code) | AllowProcessEvents (callbacks never fire) | AllowSpontaneous (callbacks fire) |
|--------|--------------------------|------------------------------------------|----------------------------------|
| Steady FPS (stress scene) | **34.0** | 26.6 | 20.5 |
| Median frame time | 33.3 ms | 33.3 ms | 50.0 ms |
| P5 (best frames) | 16.7 ms | 16.7 ms | 32.5 ms |
| P95 (worst frames) | 34.3 ms | 66.2 ms | 83.2 ms |

### Two separate problems

**Problem A: AllowSpontaneous fires mid-frame** (~6 fps hit, 26.6 → 20.5)
When the Promise resolves, the callback fires during an arbitrary point in the frame. The callback itself is lightweight (creates a `WGPipelineWrapper`, pushes to `compiled_queue`), but JS microtask resolution interleaves with the rendering submission loop, disrupting the GPU command stream timing.

**Problem B: Async pipeline infrastructure overhead** (~7 fps hit, 34.0 → 26.6)
Even with callbacks never firing, just having the async code path costs ~7 fps. Possible causes:
- `_poll_tint_worker_results()` called from `fence_wait()` every frame does multiple EM_ASM calls (check count, dequeue results, read hash halves, read WGSL pointer)
- `_add_new_pipelines_to_map()` called from `get_pipeline()` in the hot draw loop (~3,800 draws/frame on stress scene). This takes a `MutexLock` on `compiled_queue_mutex`, iterates the queue, and does an RBMap insert — executed per-draw even when the queue is empty
- Extra branching in the specialization constant code path
- The `PendingWorkerPipeline` vector iteration in the poll function

### Decision: Park the work

The async pipeline commits were moved to branch `async_shader_pipeline` and removed from `webgpu-4.6.2`. The Tint worker code went with them (same commits). The work is preserved but not merged.

---

## Current State (webgpu-4.6.2, cold-cache startup profiling)

From `STARTUP_PROFILING.md` (2026-05-06, demo_3d_platformer normal variant):

- 859 total shader modules (511 base, 348 specialized)
- 190 render pipelines, 47 compute pipelines — **all synchronous**
- 39.8 MB WGSL generated
- Tint conversion: ~6.7s cumulative wall clock, 99% of shader creation time
- `createShaderModule` browser API: ~2.5s cumulative (mostly fast, one 810ms outlier)
- `createRenderPipeline` browser API: ~4.4ms cumulative (near-instant; Dawn defers to first use)
- First visible frame: 7s
- Peak FPS: 53 fps at 18-21s (between compilation waves)
- All compilation done: 38s
- Steady state: 12-13 fps

Compilation happens in 14+ waves spread across 38 seconds. Each wave triggers FPS crashes (53→10→1→recovery). The engine runs ubershaders during compilation gaps, but Tint translation blocks the main thread during each wave.

---

## The Fix: Three Pieces

### Fix 1: Merge the Tint Web Worker (as-is)

The web worker for SPIR-V → WGSL translation is correct and validated. It eliminates 50-75% of frame spikes during shader compilation. It should be cherry-picked from `async_shader_pipeline` and merged.

The worker offloads Tint translation to a separate thread. The main thread continues rendering with ubershaders while translations complete. Results are polled once per frame during `fence_wait()`.

No changes needed to the worker implementation itself.

### Fix 2: Bypass emdawnwebgpu for Async Pipeline Completion

Instead of using `wgpuDeviceCreateRenderPipelineAsync()` through emdawnwebgpu (with its broken `AllowProcessEvents` and problematic `AllowSpontaneous`), call the WebGPU JS API directly:

**Current broken flow:**
```
C++ render_pipeline_create_async()
  → wgpuDeviceCreateRenderPipelineAsync(desc, cb_info)  [emdawnwebgpu C binding]
    → device.createRenderPipelineAsync(desc)             [JS WebGPU API]
      → Promise resolves                                  [browser GPU process]
        → emdawnwebgpu callback dispatch                  [broken/disruptive]
          → _wgpu_async_pipeline_callback()               [C++ callback]
```

**Proposed fix:**
```
C++ render_pipeline_create_async()
  → EM_ASM: call device.createRenderPipelineAsync(desc) directly in JS
    → Promise resolves → store GPURenderPipeline handle in JS-side Map
      (keyed by pipeline ID, no callback into C++)
  
Once per frame (between frames, NOT during draw loop):
  → EM_ASM: poll JS Map for completed pipelines
    → for each completed: return handle to C++
      → C++ creates WGPipelineWrapper, wraps in RID
      → add_compiled_pipeline() to hash map
```

This approach:
- Avoids emdawnwebgpu's callback mechanism entirely
- Controls exactly when results are consumed (between frames, not mid-draw)
- Uses the same polling pattern already proven by the Tint worker
- The JS Promise still resolves async in the browser GPU process — we just pick up results at a controlled time

**Implementation sketch for the JS side** (in engine.js or a new helper):
```javascript
window._asyncPipelineQueue = new Map(); // id → {desc, promise, result}
var _asyncPipelineNextId = 0;

window._asyncPipelineCreate = function(descJson) {
    var id = _asyncPipelineNextId++;
    var desc = JSON.parse(descJson); // or pass structured data via EM_ASM
    // ... build GPURenderPipelineDescriptor from passed params ...
    var promise = device.createRenderPipelineAsync(desc);
    promise.then(function(pipeline) {
        window._asyncPipelineQueue.set(id, pipeline);
    }).catch(function(err) {
        console.error('[async-pipeline] Failed:', err);
        window._asyncPipelineQueue.set(id, null);
    });
    return id;
};

window._asyncPipelinePollCount = function() {
    return window._asyncPipelineQueue.size;
};

window._asyncPipelineGetResult = function() {
    // Return first completed entry
    for (var [id, pipeline] of window._asyncPipelineQueue) {
        window._asyncPipelineQueue.delete(id);
        return {id: id, pipeline: pipeline};
    }
    return null;
};
```

**Challenge**: The `GPURenderPipelineDescriptor` needs shader modules, vertex layouts, blend states, depth/stencil state, etc. Most of this is already built in the C++ `render_pipeline_create_async()`. The question is whether to:

**(a)** Keep building the descriptor in C++ and use `wgpuDeviceCreateRenderPipelineAsync` but implement our own callback dispatch in JS (intercept the Promise on the JS side before emdawnwebgpu routes it)

**(b)** Build the descriptor entirely in JS from parameters passed via EM_ASM

Option (a) is simpler: we already have the full descriptor building code. We could patch emdawnwebgpu's `createRenderPipelineAsync` wrapper to store Promise results in a JS queue instead of routing through the callback mechanism, then poll that queue from C++. This is essentially a 10-line monkey-patch in engine.js.

Option (b) would require duplicating all the pipeline descriptor building logic in JS, which is complex and fragile.

**Recommended: Option (a)** — Monkey-patch the emdawnwebgpu JS layer. The C++ side calls `wgpuDeviceCreateRenderPipelineAsync()` as before, but on the JS side we intercept the Promise resolution and store results in a queue instead of firing the wgpu callback. The C++ side polls this queue during `fence_wait()`.

### Fix 3: Move Pipeline Pickup Out of the Hot Draw Loop

The `PipelineHashMapRD::get_pipeline()` call is in the hot per-draw loop (~3,800 draws/frame). It calls `_add_new_pipelines_to_map()` which locks `compiled_queue_mutex` and iterates the queue — on every single draw call.

Fix: drain `compiled_queue` once per frame (at the start of the frame or during `fence_wait()`), not per-draw. The `get_pipeline()` fast path should be a simple `hash_map.find()` with no mutex, no queue check.

This could be done by:
1. Adding a `drain_compiled_pipelines()` method to `PipelineHashMapRD` that the renderer calls once per frame
2. Having `get_pipeline()` only check `hash_map` (no `_add_new_pipelines_to_map()` call)
3. The one-frame latency is irrelevant — ubershader renders for one more frame

However, `PipelineHashMapRD` is shared Godot code (not WebGPU-specific). Changes here need to be safe for all backends. Since `_add_new_pipelines_to_map()` is a no-op when `compiled_queue` is empty (just a mutex lock + empty vector check), the overhead might be acceptable after fixing the callback mechanism. **Profile first before changing this.**

---

## Architecture Constraint: Why Pipelines Can't Move to the Web Worker

`GPUDevice` is **not transferable** across Web Worker boundaries per the WebGPU spec. The `device.createRenderPipeline()` and `device.createShaderModule()` calls must happen on the thread that owns the device — the main thread.

This means:
- **Tint translation (SPIR-V → WGSL)**: CAN run in a web worker (CPU-only, no GPU objects needed)
- **`createShaderModule(wgsl)`**: Must run on main thread (needs `GPUDevice`), but is fast (<2ms each)
- **`createRenderPipeline(desc)` / `createRenderPipelineAsync(desc)`**: Must run on main thread (needs `GPUDevice`), but `Async` version offloads the heavy GPU compilation to the browser's GPU process automatically
- **Actual GPU shader compilation**: Already async — happens in a separate browser process regardless of which API is used. The `Async` variant just gives a Promise instead of blocking until compilation is "accepted" (Dawn defers actual Metal/Vulkan compilation to first pipeline use anyway)

The web worker can only handle the Tint CPU work. Everything GPU-touching stays on the main thread but uses async APIs to avoid blocking.

---

## Build Configuration Note

The `async_shader_pipeline` branch was tested with:
```bash
source /Users/dwalter/emsdk/emsdk_env.sh
scons platform=web target=template_debug dlink_enabled=yes threads=no webgpu=yes opengl3=no -j16
```

This produces `bin/godot.web.template_debug.wasm32.nothreads.dlink.zip`. The `template_debug` + `dlink_enabled=yes` combination was noted as potentially causing specialized pipelines to behave differently (4000+ ERR_FAIL messages about draws with pending pipelines). These are cosmetic in debug builds (compiled out in release), but should be tested with `template_release` to confirm.

`GODOT_THREADS_ENABLED = false` — single-threaded WASM. No SharedArrayBuffer. `WorkerThreadPool` degrades to synchronous. This is why Godot's native background pipeline compilation (which works perfectly on desktop) doesn't help on web.

---

## Existing Godot Infrastructure (Already Working)

These pieces are already in place on `webgpu-4.6.2` and don't need changes:

1. **Ubershader fallback system** (`render_forward_mobile.cpp`):
   - Render loop tries specialized pipeline with `p_wait_for_compilation = false`
   - Falls back to ubershader variant with `p_wait_for_compilation = true`
   - Ubershader is pre-compiled at mesh load time
   - Ubershader perf is nearly identical to specialized on tested scenes (34 vs 33 fps)

2. **`PipelineHashMapRD`** (`pipeline_hash_map_rd.h`):
   - `compiled_queue` + `compiled_queue_mutex` for thread-safe pipeline delivery
   - `add_compiled_pipeline(hash, rid)` to push completed pipelines
   - `get_pipeline()` with automatic queue drain and compilation trigger
   - `compile_pipeline()` with `WorkerThreadPool` task management

3. **Specialization constant patching** (`rendering_device_driver_webgpu.cpp`):
   - `_patch_spirv_spec_constants()` — patches SPIR-V bytes with runtime constant values
   - `_spv_to_wgsl_cache` — HashMap<uint64_t, String> keyed by MurmurHash3 of patched SPIR-V
   - `_create_module_with_spec_constants()` — cache lookup, Tint conversion, `createShaderModule`

4. **Strip topology handling**: Falls back to sync for line/triangle strips (need Uint16+Uint32 variants)

---

## Pre-Compilation: Could We Ship WGSL Instead of SPIR-V?

From STARTUP_PROFILING analysis: Tint conversion costs ~6.7s of main-thread time. Could we do this at build/export time?

**For base (uber) shaders: Yes, in theory.** The SPIR-V is deterministic at build time. We could run Tint during `scons` and ship pre-converted WGSL alongside or instead of SPIR-V. This would eliminate the ~4.7s of base shader Tint conversion.

**For specialized shaders: No.** Specialization constants are determined at runtime (material features, light counts, etc.). The SPIR-V must be patched with runtime values, then converted. This can't be done at build time.

**Trade-off**: The base WGSL is ~8 MB (vs SPIR-V which is smaller). Download size increase. But the 4.7s savings on first load is significant. This is orthogonal to the async work — could be done independently.

**However**: With the Tint web worker fix, the 4.7s happens off the main thread, so the user sees smooth ubershader rendering during that time. Pre-compilation would reduce total wall-clock time but the user experience improvement is smaller since the main thread isn't blocked anyway.

---

## Recommended Implementation Order

1. **Cherry-pick Tint web worker from `async_shader_pipeline`** — immediate benefit, proven code, low risk
2. **Implement JS-side async pipeline polling** — monkey-patch emdawnwebgpu's `createRenderPipelineAsync` to store Promise results in a pollable queue; poll from `fence_wait()`
3. **Profile the infrastructure overhead** — with the callback mechanism fixed, measure whether the ~7 fps hit is from the broken callbacks or from the code structure itself
4. **If overhead persists, optimize the hot path** — move `compiled_queue` drain out of per-draw loop, minimize EM_ASM calls in poll functions
5. **Test with `template_release`** — confirm the 4000+ ERR_FAIL messages are debug-only and release builds are clean

---

## Key Files Reference

On `async_shader_pipeline` branch:
- `platform/web/js/engine/tint_worker.js` — Web Worker (106 lines)
- `platform/web/js/engine/engine.js` — Worker spawn + queue APIs (lines ~397-466)
- `drivers/webgpu/rendering_device_driver_webgpu.cpp` — Worker integration (lines ~143-340), async pipeline creation (lines ~7286-7751)
- `drivers/webgpu/rendering_device_driver_webgpu.h` — `PipelineCreatedCallback`, `render_pipeline_create_async()`
- `servers/rendering/rendering_device.cpp` — `render_pipeline_create_async()`, `_async_pipeline_created()` (lines ~4610-4736)
- `servers/rendering/rendering_device.h` — `RenderPipelineAsyncCallback`, `AsyncPipelineContext`
- `servers/rendering/rendering_device_driver.h` — `API_TRAIT_ASYNC_PIPELINE_COMPILATION`, base class virtual
- `servers/rendering/renderer_rd/forward_mobile/scene_shader_forward_mobile.cpp` — async path in `_create_pipeline()` (24-line addition)
- `servers/rendering/renderer_rd/pipeline_hash_map_rd.h` — `compiled_queue`, `add_compiled_pipeline()`, `get_pipeline()`

On `webgpu-4.6.2` (current) for context:
- `drivers/webgpu/rendering_device_driver_webgpu.cpp:101` — `_spv_to_wgsl_cached()` (sync Tint path)
- `drivers/webgpu/rendering_device_driver_webgpu.cpp:117` — `MAIN_THREAD_EM_ASM_PTR` Tint call
- `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.cpp:2387` — hot draw loop
- `webgpu_notes/STARTUP_PROFILING.md` — full cold-cache profiling data
- `webgpu_notes/perf_optimization_may3_2026.md` — optimization history and benchmark data

Commits on `async_shader_pipeline`:
- `864e936cd3` — feat(webgpu): async shader compilation via Web Worker + GPU pipeline async
- `eb9d522e92` — fix(webgpu): async pipeline validation + always-on perf logging

Design docs on `async_shader_pipeline`:
- `webgpu_notes/async_web_worker_shader_compilation.md` — original design doc with implementation log
- `webgpu_notes/review/async_shader_compilation_final_review.md` — correctness review
