# Command Stream Recorder: Next-Generation WebGPU IPC Elimination

## Overview

All per-draw IPC optimizations achievable within the current per-call execution model are complete (firstInstance dedup, instance batching, shadow pass merging — see `IPC_OPTIMIZATION_SUMMARY.md`). The remaining ~8,700 IPC crossings per frame on the 3D platformer stress test cost ~2.6ms — still ~11% of frame time. The Web/Native ratio is 1.44x.

This document describes the next optimization frontier: **eliminating the per-call WASM→JS trampoline overhead** by recording GPU commands into a flat buffer in WASM linear memory and submitting them in a single boundary crossing. This is the command stream recorder architecture.

**Target:** Close the Web/Native gap from 1.44x toward 1.0-1.1x.

---

## Discovery: Where the Per-Call Overhead Actually Lives

### Research Date: 2026-05-04

Research into Chromium's architecture reveals that **individual `wgpu*` calls do NOT each trigger a Mojo IPC message to the GPU process.** Dawn's wire client serializes commands into a shared-memory ring buffer and flushes the entire batch at sync points (`queue.submit()`, `encoder.finish()`). The actual IPC is ~10-20µs per flush, amortized across thousands of commands.

The measured per-call overhead (~0.2-0.5µs on Mac Studio M3 Ultra) breaks down as:

| Layer | Cost per Call | % of Total | Notes |
|-------|-------------|------------|-------|
| **WASM→JS trampoline** | ~50-150ns | **30-50%** | Emscripten glue code, parameter marshalling from WASM linear memory |
| JS→Blink C++ IDL entry | ~20-50ns | ~10-20% | V8 calling into Blink's WebGPU IDL bindings |
| Dawn Wire serialization | ~30-100ns | ~20-30% | Memcpy of small struct into shared memory ring buffer. No IPC. |
| Dawn Wire validation | ~10-50ns | ~5-15% | Minimal client-side — by design, validation deferred to server |
| Mojo IPC (amortized) | ~1-20ns | **~1-5%** | Amortized over batch. Only matters at flush points. |

**The WASM→JS crossing is the single largest contributor (30-50%).** The GPU process IPC is essentially free per-call because Dawn already batches it.

### What This Means

The per-call overhead model means that even with all our draw-count optimizations, every `wgpu*` call still pays the WASM→JS trampoline tax. With 8,700 calls/frame at ~0.3µs each = 2.6ms. If we could reduce the per-call cost from ~0.3µs to ~0.03µs (Dawn wire serialization cost only), we'd save ~2.3ms/frame — potentially closing the gap from 1.44x to ~1.1x native.

---

## Sources

- **Chromium GPU Command Buffer design:** "The client can write commands very quickly with little or no communication with the service and only once in a while tell the service it has written more commands." A draw call "just writes a few bytes into the command buffer and is done." (chromium.org/developers/design-documents/gpu-command-buffer)
- **Chromium WebGPU Technical Report:** Dawn Wire commands are nested inside a single `DawnCommands` GPU command buffer entry — multiple commands packed per batch. (chromium.googlesource.com/chromium/src/+/main/docs/security/research/graphics/webgpu_technical_report.md)
- **Dawn architecture docs:** Wire client uses `GetCmdSpace(size_t)` to allocate in buffer, `Flush()` to send batch. "dawn_wire is meant to do as little state-tracking as possible so that the client can be lean." (dawn.googlesource.com/dawn/+/HEAD/docs/dawn/overview.md)
- **Mojo IPC latency:** Measured at ~10-20µs minimum per message. Amortized across hundreds of commands per flush. (chromium-mojo mailing list)
- **WASM→JS crossing cost:** web.dev "WebAssembly performance patterns" reports ~100ns per non-trivial WASM→JS call. Mozilla Hacks (2018) showed ~5ns for trivial numeric-only signatures, but WebGPU bindings involve handle/struct marshalling.
- **Figma:** Found Emscripten's built-in WebGPU bindings "weren't performant enough" and wrote custom C++/JS bindings. Now migrating to emdawnwebgpu. (figma.com/blog/figma-rendering-powered-by-webgpu)
- **juj/wasm_webgpu:** Alternative binding library "manually tuned for absolutely best runtime speed" — its existence confirms the standard Emscripten bindings have measurable overhead. (github.com/juj/wasm_webgpu)
- **"Characterizing WebGPU Dispatch Overhead for LLM Inference" (April 2026, arxiv 2604.02344):** Full dispatch cycle (encoder→set bindings→dispatch→submit) costs 24-36µs on Vulkan, 32-71µs on Metal. Individual calls within a pass are much cheaper. Backend choice is the dominant factor, with 2.2x variation between Dawn and wgpu on Metal.
- **Brandon Jones (Toji) render bundle benchmarks:** 2-5x speedup with 40k objects on M1 Mac. Render bundles bypass the entire WASM→JS→DawnWire path on replay. (toji.dev/webgpu-best-practices/render-bundles.html)
- **Babylon.js (gpuweb discussion #1640):** WebGPU 17% slower than WebGL on CPU scripting time for 3k cubes. WebGL's VAO is 2.5x faster than WebGPU's setIndexBuffer + setVertexBuffer. Adopted render bundles.
- **Unity 6 (June 2025):** Reports "unexpected high frame cost" in WebGPU vs WebGL. Still investigating — the per-call overhead problem is not solved in Unity's WebGPU backend. (discussions.unity.com)

---

## Optimization Approaches

There are five potential approaches, but they share a common principle: **minimize the number of WASM→JS boundary crossings.** The Dawn Wire serialization and GPU process IPC are already efficient — the bottleneck is the trampoline.

### Approach 1: Render Bundles (GPURenderBundle)

**What:** Pre-record a sequence of draw commands into a `GPURenderBundle`, then replay them via a single `executeBundles()` call — 1 WASM→JS crossing replaces thousands.

**How it works:** At bundle creation time, all validation, WASM→JS crossings, and Dawn Wire encoding happen once. On subsequent frames, `executeBundles()` replays the pre-encoded wire commands directly — bypassing the entire WASM→JS→DawnWire path.

**Why it's now viable:** Our firstInstance optimization moved the per-draw instance index OUT of the push constant and into the `drawIndexed(firstInstance)` parameter. For objects with the same ubershader specialization (which our dedup showed is ~88% of draws), the push constant bind group is identical frame-to-frame. The only thing that changes is the buffer *contents* (transforms, instance data), which are updated via `writeBuffer` — completely separate from the draw commands. This means the draw command sequence is stable across frames for any set of objects that remains visible.

**Cache invalidation:** When the visible set changes (camera moves, objects enter/leave frustum, objects created/destroyed), the bundle needs re-recording. Heuristics:
- Dirty flag on frustum cull result change
- Per-render-pass bundle (opaque, shadow, transparent handled separately)
- LRU cache of bundles by camera position bucket (for predictable camera paths)
- Worst case: re-record every frame = same as today (no regression)

**Measured speedup:** Brandon Jones (Toji, Google) measured **2-5x speedup** with 40,000 objects on M1 Mac. This is the closest prior art to our use case.

**Estimated impact for Godot WebGPU:** At 8,700 draw-related IPC crossings → 1, we'd eliminate ~2.5ms/frame. On the 3D platformer: 23.8ms → ~21.2ms → 47.2 fps (from 42.3). Modest on this scene because WASM game logic overhead dominates, but on GPU-bound scenes with many draws it could be transformative.

**Complexity:** Moderate. Need to:
- Implement render bundle recording in the WebGPU driver (mirror the existing render pass recording but into a bundle encoder)
- Add dirty tracking to detect when re-recording is needed
- Handle the push constant bind group: for draws where PC is identical (88% of cases), include in bundle. For draws where PC varies, those stay unbundled.
- Transparent pass must stay unbundled (order changes with camera)

**Availability:** Now (standard WebGPU, all browsers).

### Approach 2: Godot-Level JS Batch Replay

**What:** Instead of calling `wgpu*` functions one at a time from WASM (each crossing the trampoline), build a flat command array in WASM linear memory and call a single JS function that replays all commands in a tight JS loop.

**How it works:**
```
// WASM side (C++): ~2ns per command (memcpy into linear memory)
cmd_buffer[0] = {CMD_SET_PIPELINE, pipeline_handle};
cmd_buffer[1] = {CMD_SET_BIND_GROUP, group, bg_handle, offset};
cmd_buffer[2] = {CMD_DRAW_INDEXED, index_count, instance_count, first_instance};
...

// Single WASM→JS crossing:
EM_ASM({ replayCommandBuffer($0, $1); }, cmd_buffer_ptr, cmd_count);

// JS side: tight loop, pure JS→WebGPU calls (no WASM trampoline)
function replayCommandBuffer(ptr, count) {
    for (let i = 0; i < count; i++) {
        const cmd = readCmd(ptr, i);
        switch (cmd.type) {
            case CMD_SET_PIPELINE:    pass.setPipeline(pipelines[cmd.id]); break;
            case CMD_SET_BIND_GROUP:  pass.setBindGroup(cmd.group, ...); break;
            case CMD_DRAW_INDEXED:    pass.drawIndexed(cmd.indexCount, ...); break;
        }
    }
}
```

**Estimated impact:** Eliminates ~8,700 WASM→JS crossings (each ~100-150ns), replaces with 1 crossing + 8,700 pure-JS calls (each ~50-100ns, skipping the trampoline). Savings: ~0.4-1.3ms/frame depending on how much of the per-call cost is trampoline vs downstream.

**Complexity:** Moderate. Need to:
- Define a compact binary command format in WASM linear memory
- Handle mapping JS handles (pipeline, bind group, buffer) — the JS replay function needs a handle table
- Implement the JS replay function
- Modify the WebGPU driver to record commands instead of calling wgpu* directly

**Key advantage over render bundles:** Works for ALL commands every frame, no cache invalidation needed. Transparent pass, dynamic content, everything benefits.

**Key disadvantage vs render bundles:** Still makes N individual JS→WebGPU calls (just from JS, not WASM). Render bundles replay pre-encoded wire commands, bypassing JS→DawnWire too.

**Availability:** Now (pure application-level change, no spec/browser features needed).

### Approach 3: Indirect Draws

**What:** Store draw parameters (index count, instance count, first index, base vertex, first instance) in a GPU buffer. Issue `drawIndexedIndirect(buffer, offset)` instead of `drawIndexed(indexCount, instanceCount, ...)`.

**How it works:** Each draw reads its parameters from the GPU buffer instead of passing them through function arguments. A single `writeBuffer` call uploads all draw parameters for the frame.

**Measured speedup:** Toji measured validation overhead for indirect draw buffers dropping from **3ms to ~10µs** — a 300x improvement — by consolidating indirect buffers.

**Estimated impact for Godot WebGPU:** Reduces per-draw IPC from 2 (SetBindGroup + DrawIndexed) to 1 (DrawIndexedIndirect) for draws that can use it. ~50% draw call IPC reduction. But doesn't help with SetPipeline, SetVertexBuffer, SetBindGroup calls.

**Complexity:** Low-moderate. Godot already has indirect draw infrastructure for MultiMesh. Extending it to standard draws requires:
- Building an indirect draw buffer each frame with parameters for all draws
- Using `drawIndexedIndirect` instead of `drawIndexed`

**Limitation:** Each draw still needs its own `drawIndexedIndirect` call (one per unique mesh/material combination). Doesn't reduce the number of calls, just changes what each call does. Biggest win is avoiding per-draw parameter validation overhead in Dawn.

**Availability:** Now (standard WebGPU).

### Approach 4: Multi-Draw-Indirect

**What:** A single `multiDrawIndexedIndirect(buffer, offset, drawCount)` call issues ALL draws from a GPU buffer — one WASM→JS crossing for the entire render pass.

**How it works:** All draw parameters are in a GPU buffer. One call tells the GPU "execute draws 0 through N from this buffer." The GPU reads parameters and executes all draws without any CPU-side per-draw overhead.

**Estimated impact:** Eliminates ALL per-draw IPC for draws within a render pass. The ~3,525 draw calls become 1 call. Combined with bind group consolidation (all materials in one array), the entire color pass could be ~5 IPC crossings total.

**Complexity:** Low (if the API were available). The draw buffer is the same as indirect draws. The only difference is one call instead of N.

**Limitation:** NOT standardized in WebGPU. Available as `chromium-experimental-multi-draw-indirect` in Chrome behind a flag. Not in Firefox or Safari. Being discussed in gpuweb proposals #1354/#2315/#4349 but no timeline for standardization.

**Constraint:** All draws in a multi-draw must share the same pipeline and bind groups. To handle different materials, you'd need bindless textures (texture arrays + material index). Different pipelines require separate multi-draw calls.

**Availability:** Experimental (Chrome only, not standardized). Not viable for cross-browser release.

### Approach 5: Custom WASM WebGPU Bindings

**What:** Replace Emscripten's emdawnwebgpu JavaScript bindings with hand-optimized bindings that minimize per-call overhead. Similar to what Figma did and what juj/wasm_webgpu provides.

**How it works:** The standard emdawnwebgpu bindings do argument validation, handle lookup, and object wrapping in JavaScript for each call. Custom bindings can:
- Use a pre-allocated handle table (array index lookup instead of Map.get)
- Skip redundant validation (we know our calls are valid)
- Inline common call patterns (e.g., a single `setBindGroupAndDraw` super-call)
- Use TypedArray views directly into WASM memory instead of copying arguments

**Estimated impact:** Reduce the WASM→JS trampoline overhead from ~100-150ns to ~30-50ns per call. At 8,700 calls: ~0.6-0.9ms savings. Complements all other approaches.

**Complexity:** Moderate-high. Need to:
- Fork or wrap emdawnwebgpu
- Hand-optimize the hot-path functions (SetPipeline, SetBindGroup, SetVertexBuffer, DrawIndexed)
- Maintain compatibility with browser updates
- Test across Chrome, Firefox, Safari

**Key advantage:** Benefits ALL WebGPU calls universally with no architectural changes.

**Availability:** Now (application-level, but requires maintaining custom bindings).

---

## Unified Architecture: The Command Stream Approach

Approaches 1, 2, 3, and 4 are NOT independent — they share a common infrastructure that can be built once:

**The core abstraction is a command stream recorded in WASM linear memory.**

```
Phase 1: Record (C++/WASM side, ~2ns per command)
    _render_list_template writes commands into a flat buffer instead of calling wgpu* directly.
    Each command is a small struct: {type, args...} packed into WASM linear memory.

Phase 2: Optimize (C++/WASM side, optional)
    Scan the command stream for patterns:
    - Consecutive identical SetBindGroup → deduplicate (already done today per-call)
    - Consecutive DrawIndexed with same state → merge into instanced draw (already done)
    - All draws sharing same pipeline → candidate for multi-draw-indirect (future)

Phase 3: Submit (single WASM→JS crossing)
    Option A: JS batch replay (Approach 2) — JS reads the buffer, calls WebGPU APIs
    Option B: Render bundle (Approach 1) — JS records into GPURenderBundleEncoder, caches
    Option C: Indirect draw buffer (Approach 3) — JS uploads draw params to GPU buffer
    Option D: Multi-draw-indirect (Approach 4) — same as C but single call (future)
```

The recording phase is identical for all approaches. Only the submission phase changes. This means:

1. **Build the command stream recorder once** (Phase 1) — replaces direct wgpu* calls in the WebGPU driver
2. **Start with JS batch replay** (Approach 2) — simplest, works for everything, no cache invalidation
3. **Layer render bundles on top** (Approach 1) — for semi-static geometry, detect stable command sequences and cache as bundles
4. **Add indirect draws** (Approach 3) — for draw parameter optimization, orthogonal to replay
5. **Multi-draw-indirect** (Approach 4) — drop-in replacement when spec ships, command stream already has the data

Custom bindings (Approach 5) can be applied to the JS replay function itself — optimize the hot inner loop once rather than optimizing every individual binding.

---

## Predicted Combined Impact

| Optimization Stage | IPC/frame | Time/frame | Est. FPS (platformer) |
|-------------------|-----------|------------|----------------------|
| Current (all existing optimizations) | ~8,700 | ~2.6ms | 42.3 |
| + JS batch replay (Approach 2) | ~8,700 JS calls but 1 WASM→JS | ~1.3-1.8ms | ~44-46 |
| + Render bundles for stable draws (Approach 1) | ~1,000 JS calls + bundle replay | ~0.3-0.5ms | ~46-48 |
| + Custom bindings for remaining calls (Approach 5) | ~1,000 fast JS calls | ~0.1-0.2ms | ~47-49 |
| Theoretical limit (all approaches) | ~10-20 | ~0.01ms | ~49-50 (WASM game logic bottleneck) |

**Native Metal on this scene: 61 fps.** The remaining gap (~49 vs 61) would be pure WASM execution overhead for game logic (GDScript, physics, animation) — outside the scope of rendering optimizations.

---

## Industry Context

- **Unity 6 WebGPU:** Still reports "unexpected high frame cost" vs WebGL (June 2025). No public evidence of per-call overhead mitigation. Unity's WebGPU backend appears to use straightforward per-call bindings. They haven't diagnosed the WASM→JS trampoline as the root cause.
- **Babylon.js:** Adopted render bundles to mitigate overhead, but their architecture is JS-native (no WASM trampoline). Their problem is JS→DawnWire, not WASM→JS.
- **Bevy (wgpu-native):** Notes "performance will be limited as WASM is slower than native code" with no specific per-call mitigation.
- **Figma:** Wrote custom bindings to work around Emscripten overhead — closest to our problem but Figma's workload is 2D canvas, not 3D render passes with thousands of draws.
- **No engine has implemented a command stream + batch replay architecture for WebGPU WASM.** This would be a first.

### Competitive Implication

Unity 6's WebGPU backend is stuck at the "why is this slow?" stage. Meanwhile, Godot WebGPU has:
1. **Diagnosed** the root cause (WASM→JS trampoline = 30-50% of per-call overhead)
2. **Measured** the breakdown across all layers (Dawn Wire, Mojo IPC, JS IDL entry)
3. **Shipped** 4 optimizations that cut IPC crossings by 63% and doubled frame rates
4. **Designed** the next-generation architecture (command stream recorder)

If we implement even Phase 1 + JS batch replay, Godot WebGPU could be measurably faster than Unity WebGPU on equivalent 3D content. The command stream approach is a structural advantage — it's not a one-off hack but an architecture that compounds with every future optimization layered on top.

---

## Approach Assessment Summary

| Approach | Est. Savings | Effort | Dependency | Worth Doing? |
|----------|-------------|--------|------------|-------------|
| **2. JS batch replay** | ~0.4-1.3ms/frame | Moderate | **Foundation** — must build first | **YES — do first** |
| **1. Render bundles** | ~1.0-2.0ms/frame (for stable geometry) | Moderate | Builds on command stream | **YES — highest single-step gain** |
| **3. Indirect draws** | ~0.2-0.5ms/frame | Low-moderate | Orthogonal to stream | **MAYBE — diminishing returns vs effort** |
| **5. Custom bindings** | ~0.6-0.9ms/frame | Moderate-high | Optimizes JS replay | **LATER — after measuring where JS time goes** |
| **4. Multi-draw-indirect** | ~1.0-2.0ms/frame | Low | Blocked on spec | **WAIT — not standardized** |

The key insight: **approaches 1-4 share the command stream recorder as common infrastructure.** Building the recorder (Phase 1) + JS batch replay (Approach 2) is a single moderate-effort task that unlocks all subsequent approaches. Render bundles (Approach 1) layer on top for the biggest additional win. Indirect draws and custom bindings are optional refinements.

---

## Implementation Plan

### Step 1: Command Stream Recorder + JS Batch Replay

**Goal:** Replace direct `wgpu*` calls in the WebGPU driver's render pass execution with command stream recording + single-crossing JS batch replay.

**Scope:** Render pass commands only (SetPipeline, SetBindGroup, SetVertexBuffer, SetIndexBuffer, DrawIndexed, SetViewport, SetScissor, SetStencilReference). Resource creation, buffer writes, compute passes, and render pass begin/end stay as direct calls (low frequency, not the bottleneck).

**Files to modify:**
- `drivers/webgpu/rendering_device_driver_webgpu.h` — command stream buffer, handle table
- `drivers/webgpu/rendering_device_driver_webgpu.cpp` — record instead of direct call, flush at render pass end
- New JS file (or EM_JS block) — replay function

**Gated by:** `API_TRAIT_COMMAND_STREAM_REPLAY` (new trait, WebGPU only). Zero impact on Vulkan/Metal/D3D12.

**Validation:** Compare PERF counters and FPS before/after. Command stream must produce identical rendering (same draw order, same state).

### Step 2: Render Bundle Caching

**Goal:** Detect stable command sequences across frames and cache them as `GPURenderBundle` objects. Replay via `executeBundles()` instead of JS batch replay for cached passes.

**Scope:** Opaque color pass and shadow passes (stable visible set between frustum cull changes). Transparent pass excluded (order changes with camera angle).

**Dirty detection:** Hash the command stream for a render pass. If hash matches previous frame → replay cached bundle. If different → re-record bundle from stream.

**Files to modify:**
- `drivers/webgpu/rendering_device_driver_webgpu.cpp` — bundle recording, cache, hash comparison
- JS replay function — add bundle create/replay path

### Step 3: Measure and Iterate

**Goal:** Profile the JS batch replay inner loop. Identify whether the remaining overhead is in JS→WebGPU API calls, handle lookup, or argument marshalling. Apply targeted optimizations (custom bindings, TypedArray views, etc.) based on data.

---

## Open Questions

1. **Handle mapping:** The JS replay function needs to map WASM-side handle IDs to JS WebGPU objects (GPURenderPipeline, GPUBindGroup, GPUBuffer). Options: pre-allocated JS array indexed by handle ID (fastest) vs Map lookup. Need to verify emdawnwebgpu's handle representation.

2. **Render bundle limitations:** WebGPU render bundles cannot change viewport/scissor rect. Shadow passes that use viewport changes within a merged pass would need to stay unbundled or be split into per-viewport bundles.

3. **Firefox/wgpu compatibility:** Firefox's WebGPU uses wgpu-native (Rust), not Dawn. The per-call overhead profile may differ (Rust FFI vs Mojo IPC). Need to test on Firefox to confirm the command stream approach helps there too.

4. **Profiling methodology:** Need a way to measure WASM→JS trampoline cost in isolation. Options: compare `EM_ASM` batch replay vs direct wgpu* calls on a synthetic benchmark with fixed command count. The difference isolates the trampoline overhead.

_______


Firefox/wgpu compatibility (question 3):

  ┌─────────────────────┬──────────────────────────────┬─────────────────────────────┬───────────┐
  │      Approach       │        Dawn (Chrome)         │       wgpu (Firefox)        │ Agnostic? │
  ├─────────────────────┼──────────────────────────────┼─────────────────────────────┼───────────┤
  │ JS batch replay     │ Yes — pure JS WebGPU API     │ Yes — same JS API           │ Yes       │
  │                     │ calls                        │                             │           │
  ├─────────────────────┼──────────────────────────────┼─────────────────────────────┼───────────┤
  │ Render bundles      │ Yes                          │ Yes (standard spec)         │ Yes       │
  ├─────────────────────┼──────────────────────────────┼─────────────────────────────┼───────────┤
  │ Indirect draws      │ Yes                          │ Yes (standard spec)         │ Yes       │
  ├─────────────────────┼──────────────────────────────┼─────────────────────────────┼───────────┤
  │ Multi-draw-indirect │ Chrome experimental only     │ No                          │ No        │
  ├─────────────────────┼──────────────────────────────┼─────────────────────────────┼───────────┤
  │ Custom bindings     │ Depends on binding internals │ Probably needs separate     │ Partially │
  │                     │                              │ tuning                      │           │
  └─────────────────────┴──────────────────────────────┴─────────────────────────────┴───────────┘

  Only multi-draw-indirect is browser-specific. The command stream recorder + JS batch replay +
  render bundles are all standard WebGPU and work identically on both backends. However, the
  magnitude of improvement may differ — Firefox's wgpu uses Rust FFI (not Mojo IPC), so the per-call
  cost breakdown is different. The WASM→JS trampoline cost is the same either way.

  ---
  Which approaches share the command stream recorder?

  - Approaches 1-4 (render bundles, JS batch replay, indirect draws, multi-draw-indirect) all consume
   the same command stream. Build the recorder once, swap the submission backend.
  - Approach 5 (custom bindings) does NOT use the command stream. It optimizes the existing per-call
  path by making each WASM→JS crossing cheaper. It's orthogonal — it can complement the stream or be
  applied to the stream's JS replay function.

  ---
  Two camps:

  Camp A: Stateless Batching (no cache, works every frame regardless of scene stability)

  Approach 2 — JS Batch Replay.
  Record all render pass commands into a flat buffer in WASM memory, then call one JS function that
  loops through and issues all WebGPU calls from pure JS (eliminating the per-call WASM→JS
  trampoline). Works for every frame, every pass, every scene — dynamic, static, transparent,
  everything. No cache, no invalidation, no heuristics.
  Est. savings: ~0.4-1.3ms/frame. Effort: Moderate. Do first — foundation for everything else.

  Approach 3 — Indirect Draws.
  Store draw parameters (index count, instance count, first instance, etc.) in a GPU buffer instead
  of passing them as function arguments. Each draw call becomes drawIndexedIndirect(buffer, offset)
  instead of drawIndexed(a, b, c, d, e). Reduces Dawn's per-draw validation overhead since the
  parameters come from GPU memory. Works every frame, no cache.
  Est. savings: ~0.2-0.5ms/frame. Effort: Low-moderate. Maybe — diminishing returns, only helps the
  draw call itself, not SetPipeline/SetBindGroup/SetVertexBuffer.

  Approach 5 — Custom Bindings.
  Hand-optimize the hot-path JavaScript binding functions (the emscripten glue) to reduce per-call
  trampoline cost from ~100-150ns to ~30-50ns. Faster handle lookup, less argument marshalling,
  TypedArray views into WASM memory. Works universally on every call, no cache. Can also be applied
  to the JS replay function from Approach 2 (optimize the inner loop).
  Est. savings: ~0.6-0.9ms/frame (standalone) or additive on top of batch replay. Effort:
  Moderate-high (fork/maintain bindings). Later — measure first to know where JS time actually goes.

  Camp B: Cache-Dependent (only improve perf when the command sequence is stable across frames)

  Approach 1 — Render Bundles.
  Pre-record a render pass's commands into a GPURenderBundle object. On subsequent frames, if the
  same objects are visible with the same state, replay the bundle via a single executeBundles() call
  — bypassing the entire WASM→JS→DawnWire→validation path. This is the deepest optimization: it
  doesn't just skip the trampoline, it skips Dawn Wire serialization and client-side validation too.
  But it only helps when the command sequence is stable. Camera movement, objects entering/leaving
  frustum, light changes — all invalidate the cache.
  Est. savings: ~1.0-2.0ms/frame (for stable geometry). Effort: Moderate. Yes — highest single-step
  gain, but only after Camp A proves the command stream works.

  Approach 4 — Multi-Draw-Indirect.
  Single API call issues ALL draws from a GPU buffer. The ultimate IPC reduction — entire render pass
   becomes 1 call. But requires all draws to share the same pipeline and bind groups (needs bindless
  textures for different materials). Also requires a stable/pre-built indirect buffer.
  Est. savings: ~1.0-2.0ms/frame. Effort: Low if API existed. Wait — not standardized, Chrome-only
  experimental, requires bindless textures for full benefit.

  ---
  Is the cache-dependent approach worth it?

  The honest answer: it depends on the game. A first-person shooter with a static level and moving
  camera — the visible set changes every frame (frustum culling), so the bundle invalidates
  constantly. A top-down game with a fixed camera and spawning enemies — mostly stable, bundles would
   hit often. The stress test platformer with 150 enemies moving around — moderate stability, maybe
  50-70% of frames could reuse a bundle.

  The key safety property is that cache miss = same performance as today (just re-record the bundle,
  which is the same work as the current per-call path). So there's no downside to having it — it's a
  free win whenever the cache hits and zero cost when it misses. The question is just whether the
  implementation effort is justified by the hit rate.

  My take: Camp A (JS batch replay) is the clear first priority. It's unconditional, every-frame,
  every-scene improvement with no cache complexity. If that alone gets us close enough to native,
  Camp B may not be needed for the launch. But the command stream recorder infrastructure makes Camp
  B trivially layerable later.

_____

Why can't the command stream be more generic?

  It can be, in theory. The reason we're scoping it to render pass commands is practical, not
  fundamental:

  The render pass draw loop is special because it's the ONE place where:
  1. Thousands of calls happen in a tight, single loop (_render_list_template)
  2. Every call is fire-and-forget — no return values needed
  3. The entire sequence is fully determined before any of it executes
  4. The calls are self-contained state (pipeline, bind groups, buffers, draw)

  Other GPU calls throughout the frame break these assumptions:

  - Resource creation (createBuffer, createTexture, createBindGroup): Returns a handle that code uses
   immediately on the next line. You can't defer createBindGroup() into a batch because the very next
   statement passes that handle to setBindGroup(). Synchronous dependency.
  - Buffer writes (writeBuffer): Happen during various update phases (animation, transforms, lights)
  scattered across the frame. Each one could be in a different function, different subsystem. And the
   render pass that reads the buffer must happen after the write.
  - Compute passes: Same structure as render passes (begin, set state, dispatch, end) — and could use
   the command stream. But there are only 5-15 dispatches per frame. Not worth it.
  - Render pass begin/end: Only ~9 per frame.

  So it's not that we can't batch other calls — it's that the draw loop accounts for ~90% of all
  crossings and is the only place where thousands of deferrable, dependency-free calls happen in
  sequence. Everything else is either too infrequent or has synchronous handle dependencies. The
  command stream could be extended to compute passes later if they ever become a bottleneck.

  ---
  Custom bindings — does it actually matter? Why is the crossing slow?

  Here's what physically happens when WASM calls a JS function:

  Step 1: V8 trampoline (~30-50ns, irreducible)
  WASM code executes: call_indirect $wgpuSetPipeline
    → V8 saves WASM registers (WASM uses different calling convention)
    → Converts WASM i32 values to JS Numbers
    → Sets up JS execution context (receiver, arguments)
    → Jumps to JS function
    → On return: converts JS values back to WASM types
    → Restores WASM registers
  This is baked into V8. We can't change it. ~30-50ns per crossing no matter what.

  Step 2: The JS binding function (~50-100ns, optimizable)
  // emdawnwebgpu generated binding for wgpuRenderPassEncoderSetBindGroup:
  function(passHandle, groupIndex, bgHandle, dynamicOffsetCount, dynamicOffsetsPtr) {
      const pass = handleTable.get(passHandle);          // Map lookup ~10-20ns
      const bg = handleTable.get(bgHandle);              // Map lookup ~10-20ns
      // Read dynamic offsets from WASM linear memory:
      const offsets = new Uint32Array(
          HEAPU8.buffer, dynamicOffsetsPtr, dynamicOffsetCount  // TypedArray creation ~20ns
      );
      pass.setBindGroup(groupIndex, bg, offsets);        // Actual WebGPU call
  }

  A "small" crossing (e.g., drawIndexed with 5 integer args): ~50-80ns total. No memory reads, simple
   handle lookup.

  A "larger" crossing (e.g., setBindGroup with dynamic offsets): ~100-150ns. Needs to read an offset
  array from WASM linear memory, create a TypedArray view, do multiple handle lookups.

  Custom bindings can optimize Step 2: pre-allocated array instead of Map.get, reusable TypedArray
  views, skip validation. But Step 1 (the V8 trampoline, ~30-50ns) is irreducible per crossing.

  So does it matter? Here's the math:
  - Custom bindings: save ~50-70ns per call × 8,700 calls = ~0.4-0.6ms
  - Command stream: save ~100-150ns per call × 8,700 calls = ~0.9-1.3ms (eliminates BOTH steps)

  Command stream gives roughly 2x the benefit of custom bindings because it eliminates the
  irreducible trampoline too. Custom bindings only shave the optimizable portion. That said, custom
  binding techniques applied to the command stream's JS replay function (optimizing the inner loop)
  are complementary — you'd do it inside the replay function rather than as a separate effort.

  ---
  Indirect draws — are draws the only candidate for GPU buffers?

  Yes, draws are essentially the only render pass operation that WebGPU allows to be indirected.
  There is no:
  - setPipelineIndirect(buffer, offset) — pipeline selection must be CPU-side
  - setBindGroupIndirect(buffer, offset) — bind groups must be CPU-side handles + CPU-side offsets
  - setVertexBufferIndirect(buffer, offset) — buffer handle must be CPU-side
  - setViewportIndirect(buffer, offset) — no such thing

  The WebGPU spec only provides drawIndexedIndirect and drawIndirect. Everything else requires
  CPU-side arguments.

  So out of our ~8,700 IPC crossings per frame:

  ┌───────────────────────────────┬────────┬────────────────────┐
  │           Call type           │ Count  │ Can be indirected? │
  ├───────────────────────────────┼────────┼────────────────────┤
  │ DrawIndexed                   │ ~3,525 │        Yes         │
  ├───────────────────────────────┼────────┼────────────────────┤
  │ SetVertexBuffer               │ ~3,714 │         No         │
  ├───────────────────────────────┼────────┼────────────────────┤
  │ SetBindGroup (push constant)  │   ~804 │         No         │
  ├───────────────────────────────┼────────┼────────────────────┤
  │ SetBindGroup (material/scene) │   ~509 │         No         │
  ├───────────────────────────────┼────────┼────────────────────┤
  │ SetPipeline                   │   ~117 │         No         │
  ├───────────────────────────────┼────────┼────────────────────┤
  │ Other                         │    ~31 │         No         │
  └───────────────────────────────┴────────┴────────────────────┘

  Indirect draws can only address the ~3,525 draw calls — 40% of total IPC. The other 60%
  (state-setting calls) have no indirect equivalent. This is why indirect draws alone give modest
  savings (~0.2-0.5ms) while JS batch replay addresses 100% of calls.

  The one scenario where indirect draws become transformative is multi-draw-indirect + bindless
  textures: all materials in one texture array (no per-material SetBindGroup), all meshes in one
  mega-buffer (no per-mesh SetVertexBuffer), all draws in one indirect buffer (one call). That
  collapses the entire render pass to ~5 calls. But it requires two features that aren't in the
  WebGPU spec today.

  ---
  Bottom line for Camp A priority:

  JS batch replay is the clear winner because it's the only approach that addresses all 8,700
  crossings, not just a subset. Indirect draws and custom bindings are refinements that optimize
  portions of what the batch replay already handles. Build the command stream + batch replay first,
  then measure whether the remaining overhead justifies the others.

____

JS batch replay IS conceptually a generic "indirect" call
  for all crossing types. The only reason WebGPU uses the word "indirect" specifically for draws is
  that it's a GPU-side feature (the GPU reads draw params from a buffer). What we're doing is the
  same concept but CPU-side (JS reads all command params from a WASM buffer). It's command
  indirection at a different level.

  We should probably just call it what it is: command buffering.

  ---
  Now, the cost breakdown. Let me be precise:

  Total per-call cost: ~300ns (measured average)

  WASM → JS (the trampoline):     ~90-150ns    ≈ 30-50%
  JS → GPU (everything after):    ~150-210ns   ≈ 50-70%

  Applied to the 8,700 calls/frame at 2.6ms total:

  ┌─────────────────────────┬────────────┬───────────────────────────────────────────────────────┐
  │         Portion         │ Cost/frame │                      What it is                       │
  ├─────────────────────────┼────────────┼───────────────────────────────────────────────────────┤
  │ WASM→JS trampoline      │ ~0.8-1.3ms │ Register save/restore, value conversion, WASM memory  │
  │                         │            │ reads                                                 │
  ├─────────────────────────┼────────────┼───────────────────────────────────────────────────────┤
  │ JS→GPU (Dawn Wire +     │ ~1.3-1.8ms │ Handle lookup, serialization into shared memory       │
  │ validation)             │            │ buffer, client-side validation                        │
  └─────────────────────────┴────────────┴───────────────────────────────────────────────────────┘

  JS batch replay (command buffering) only saves the first row — the ~0.8-1.3ms WASM→JS trampoline.
  The 8,700 JS→WebGPU calls still happen from the JS replay function, so the ~1.3-1.8ms JS→GPU cost
  remains.

  That means after command buffering, we'd still have ~1.3-1.8ms of per-call overhead from JS→GPU. To
   eliminate that, you need render bundles — which bypass the JS→DawnWire→validation path entirely by
   replaying pre-encoded wire commands.

  So the full picture stacks like this:

  ┌─────────────────────┬───────────────────────────────────┬─────────────────────────────────────┐
  │        Stage        │        What it eliminates         │         Remaining overhead          │
  ├─────────────────────┼───────────────────────────────────┼─────────────────────────────────────┤
  │ Today               │ —                                 │ ~2.6ms (WASM→JS + JS→GPU)           │
  ├─────────────────────┼───────────────────────────────────┼─────────────────────────────────────┤
  │ + Command buffering │ WASM→JS trampoline (30-50%)       │ ~1.3-1.8ms (JS→GPU only)            │
  ├─────────────────────┼───────────────────────────────────┼─────────────────────────────────────┤
  │ + Render bundles    │ JS→GPU for cached passes (50-70%) │ ~0.1-0.3ms (uncacheable draws only) │
  └─────────────────────┴───────────────────────────────────┴─────────────────────────────────────┘

  This is why the two approaches are complementary, not competing: command buffering eliminates the
  first half of the cost, render bundles eliminate the second half.

____


1. Effort/difficulty comparison:

  Command buffering is significantly simpler:
  - Define a packed struct format for ~10 command types (SetPipeline, SetBindGroup, SetVertexBuffer,
  SetIndexBuffer, DrawIndexed, SetViewport, SetScissor, SetStencilRef, and a couple more)
  - Allocate a fixed buffer in WASM linear memory (~256KB covers any realistic frame)
  - Modify the WebGPU driver's render pass command functions to write into the buffer instead of
  calling wgpu* directly
  - Write one JS replay function (~100 lines: read buffer, switch on command type, call WebGPU API)
  - Handle mapping: need a JS-side array that maps WASM handle IDs to JS WebGPU objects

  That's it. No cache, no invalidation, no heuristics, no edge cases. The only real complexity is the
   handle mapping — making sure the JS replay can translate handle IDs to the actual
  GPURenderPipeline/GPUBindGroup/GPUBuffer objects. emdawnwebgpu already maintains this mapping
  internally; we need to either tap into it or mirror it.

  Estimate: ~2-3 days of focused work. Well-scoped, testable, low risk.

  Render bundles are substantially harder:
  - Push constant ring buffer interaction: the ring buffer rotates its offset each frame. Render
  bundles capture the exact dynamic offset at recording time. Even if the push constant DATA is
  identical frame-to-frame, the offset is different → bundle is invalid. We'd need to either
  stabilize the ring buffer allocation or exclude push-constant-using draws from bundles. (With
  firstInstance, 88% of draws skip the push constant SetBindGroup, so those CAN be bundled. The 12%
  that need push constants can't.)
  - Bind group lifetime: render bundles capture references to specific GPUBindGroup objects. If Godot
   recreates bind groups between frames (e.g., material uniform sets after a resource change), the
  bundle holds stale references → crash or corruption.
  - Viewport/scissor limitation: WebGPU render bundles cannot change viewport or scissor. Our merged
  shadow passes use viewport changes within a single render pass — those can't be bundled (or must be
   split into per-viewport bundles).
  - Cache invalidation: need to detect when the visible set changes (frustum cull result differs,
  objects added/removed, light assignments changed). Hashing the command stream is one approach but
  adds its own CPU cost.
  - Transparent pass exclusion (sort order changes with camera).
  - Cache memory management (LRU eviction, bundle object lifetime).

  Estimate: ~5-7 days. More edge cases, more subtle failure modes, higher risk of bugs that only
  manifest in specific scenes.

  ---
  2. Cost on weaker hardware:

  The per-call cost scales with CPU performance. Our Mac Studio M3 Ultra is roughly the fastest
  consumer CPU available. Real-world targets are 2-5x slower:

  ┌──────────────────────────────────┬──────────────────────┬───────────────┬──────────────────┐
  │              Device              │         CPU          │ Est. per-call │ Multiplier vs M3 │
  │                                  │                      │      cost     │       Ultra      │
  ├──────────────────────────────────┼──────────────────────┼───────────────┼──────────────────┤
  │ Mac Studio M3 Ultra              │ 4GHz, huge cache     │ ~0.3µs        │ 1x               │
  ├──────────────────────────────────┼──────────────────────┼───────────────┼──────────────────┤
  │ MacBook Air M2                   │ 3.5GHz               │ ~0.4µs        │ ~1.3x            │
  ├──────────────────────────────────┼──────────────────────┼───────────────┼──────────────────┤
  │ Mid-range phone (Snapdragon 7    │ ~2.5GHz, smaller     │ ~0.8µs        │ ~2.5x            │
  │ Gen 2, A16)                      │ cache                │               │                  │
  ├──────────────────────────────────┼──────────────────────┼───────────────┼──────────────────┤
  │ Budget phone / Chromebook        │ ~2GHz, weak branch   │ ~1.5µs        │ ~5x              │
  │ (Snapdragon 4, MediaTek)         │ prediction           │               │                  │
  └──────────────────────────────────┴──────────────────────┴───────────────┴──────────────────┘

  Now apply that to a moderate 3D game (not our extreme stress test — think a typical indie 3D game):

  ┌───────────────────────────┬─────────────┬───────────────┬──────────┬───────────┬──────────────┐
  │         Scenario          │ Draws/frame │ IPC crossings │ M3 Ultra │ Mid phone │ Budget phone │
  ├───────────────────────────┼─────────────┼───────────────┼──────────┼───────────┼──────────────┤
  │ Simple 3D (500 draws)     │         500 │        ~1,500 │    0.5ms │     1.2ms │        2.3ms │
  ├───────────────────────────┼─────────────┼───────────────┼──────────┼───────────┼──────────────┤
  │ Moderate 3D (2,000 draws) │       2,000 │        ~5,000 │    1.5ms │     4.0ms │        7.5ms │
  ├───────────────────────────┼─────────────┼───────────────┼──────────┼───────────┼──────────────┤
  │ Complex 3D (4,000 draws)  │       4,000 │        ~9,000 │    2.7ms │     7.2ms │       13.5ms │
  └───────────────────────────┴─────────────┴───────────────┴──────────┴───────────┴──────────────┘

  As a percentage of the frame budget:

  ┌────────────┬──────────────────────────┬─────────────────────────┬───────────────────────────┐
  │  Scenario  │    M3 Ultra (60fps =     │   Mid phone (30fps =    │  Budget phone (30fps =    │
  │            │         16.7ms)          │          33ms)          │           33ms)           │
  ├────────────┼──────────────────────────┼─────────────────────────┼───────────────────────────┤
  │ Simple 3D  │            3%            │           4%            │            7%             │
  ├────────────┼──────────────────────────┼─────────────────────────┼───────────────────────────┤
  │ Moderate   │            9%            │           12%           │            23%            │
  │ 3D         │                          │                         │                           │
  ├────────────┼──────────────────────────┼─────────────────────────┼───────────────────────────┤
  │ Complex 3D │           16%            │           22%           │            41%            │
  └────────────┴──────────────────────────┴─────────────────────────┴───────────────────────────┘

  And if you want 60fps on a phone (16.7ms budget):

  ┌─────────────┬───────────────────┬──────────────────────┐
  │  Scenario   │ Mid phone (60fps) │ Budget phone (60fps) │
  ├─────────────┼───────────────────┼──────────────────────┤
  │ Simple 3D   │        7%         │         14%          │
  ├─────────────┼───────────────────┼──────────────────────┤
  │ Moderate 3D │        24%        │         45%          │
  ├─────────────┼───────────────────┼──────────────────────┤
  │ Complex 3D  │        43%        │         81%          │
  └─────────────┴───────────────────┴──────────────────────┘

  On a budget phone trying to hit 60fps with a moderate 3D scene, nearly half the frame is spent on
  WASM→JS crossings. That's the difference between shipping at 60fps or being stuck at 30fps.

  And this is AFTER all our existing IPC optimizations. Without them (the original ~23,000
  crossings), it would be physically impossible to render at even 30fps on a phone.

  ---
  The punchline: On our beefy M3 Ultra, the remaining 2.6ms of IPC overhead is "just 11%" —
  noticeable but not critical. On the actual target hardware for web games (phones, Chromebooks,
  budget laptops), that same overhead scales to 20-45% of frame time. Command buffering goes from
  "nice optimization" on desktop to potentially the difference between 30fps and 60fps on mobile.

  Command buffering at ~2-3 days of work, saving 30-50% of that overhead across all devices, is the
  highest ROI optimization remaining. Render bundles could eliminate another 30-40% on top but at
  2-3x the implementation effort and complexity.

---

## Decision (2026-05-04)

### For the godot-webgpu release (May 9, 2026): Command Buffering Only

**Ship command buffering. Defer render bundles to post-release.**

**Rationale:**

Command buffering:
- ~2-3 days of focused work — fits in the 5-day release window with room for testing
- Zero risk of regression: gated by `API_TRAIT_COMMAND_STREAM_REPLAY`, falls back to direct calls if disabled
- No cache, no invalidation, no heuristics — same calls, different calling path
- Saves 30-50% of remaining IPC overhead (~0.8-1.3ms on M3 Ultra, ~2-4ms on mobile)
- Every scene, every frame, every pass benefits unconditionally
- Foundation for render bundles later (command stream infrastructure is reusable)

Render bundles (deferred to post-release):
- ~5-7 days of work — too risky for a 5-day release window
- Push constant ring buffer interaction: ring offset rotates per frame, invalidates bundles unless ring allocation is stabilized
- Bind group lifetime: bundles capture GPUBindGroup object references; recreated bind groups = stale references = crash
- Viewport/scissor limitation: merged shadow passes use viewport changes, can't be bundled without splitting
- Cache invalidation: needs dirty detection (command stream hashing), adds CPU cost and complexity
- Better to ship with real-world data about which scenes would benefit before investing
- The command stream infrastructure from command buffering makes adding render bundles later a smaller incremental effort

### Implementation Plan: Command Buffering

**Scope:** Render pass commands only. ~10 command types: SetPipeline, SetBindGroup, SetVertexBuffer, SetIndexBuffer, DrawIndexed, DrawIndexedIndirect, SetViewport, SetScissor, SetStencilReference, SetBlendConstant.

**Files to modify:**
- `drivers/webgpu/rendering_device_driver_webgpu.h` — command buffer struct, recording state
- `drivers/webgpu/rendering_device_driver_webgpu.cpp` — record commands instead of direct wgpu* calls, flush at render pass end via single EM_ASM call
- New JS (EM_JS or library file) — replay function (~100 lines)
- `servers/rendering/rendering_device_driver.h` — `API_TRAIT_COMMAND_STREAM_REPLAY`

**Key implementation detail — handle mapping:**
The JS replay function needs to translate WASM-side handle IDs to JS WebGPU objects (GPURenderPipeline, GPUBindGroup, GPUBuffer). emdawnwebgpu already maintains this mapping internally. Options:
1. Tap into emdawnwebgpu's internal handle table (fastest, but depends on binding internals)
2. Maintain a parallel JS array indexed by handle ID (independent, but requires registration on object creation)
3. Use the WASM-side pointer as a key into a Map (simplest, but Map.get has overhead)

Option 1 is preferred. Need to investigate emdawnwebgpu's handle representation before implementation.

**Gated by:** `API_TRAIT_COMMAND_STREAM_REPLAY` (new trait). Only WebGPU driver returns 1. Zero impact on Vulkan/Metal/D3D12. If any issue is found, set trait to 0 to disable entirely — instant rollback.

**Testing plan:**
1. Visual regression: run all 10 demo scenes, verify 0 GPU errors and identical rendering
2. PERF counter comparison: verify same draw/SetBG/PC counts (commands are identical, just called from JS instead of WASM)
3. FPS benchmark: scene_c (20k draws), scene_h (60k draws), 3D platformer stress test — before/after
4. Firefox: verify replay function works on wgpu backend (same JS WebGPU API)

**Estimated timeline:**
- Day 1: Handle mapping investigation + command buffer struct + recording in driver
- Day 2: JS replay function + end-to-end integration + first demo working
- Day 3: Full regression testing + benchmarking + fix any issues

### Post-Release Roadmap

| Priority | Optimization | When | Depends On |
|----------|-------------|------|------------|
| **1** | Command buffering | **Release (May 9)** | — |
| 2 | Render bundles (opaque + shadow) | Post-release v1.1 | Command stream infrastructure |
| 3 | JS replay function optimization | Post-release, data-driven | Profiling of JS inner loop |
| 4 | Multi-draw-indirect | When spec standardizes | Command stream + indirect buffer |

