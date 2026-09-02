# Async WebGPU — Synchronous Buffer Readback on Single-Threaded WASM

**Status:** S4 (ASYNCIFY) abandoned due to unacceptable costs. Proceeding with S2 (demo rewrite). S4 code preserved on branch `asyncify-experiment`.

**Decision log:**
- Discussed S2 vs S4 tradeoffs. S2 (demo rewrite) is lower risk but leaves a known engine API break. S4 has unknown perf cost but is the correct engine fix and matches native behavior.
- Decided: proceed with S4 as a phased experiment. If build or first-run fails → bail to S2. If benchmark regression >10% → bail to S2. Otherwise commit S4.
- Pre-flight: `fence_wait()` uses force-signal (safe — buffer_map's WaitAny implicitly waits for GPU completion).

**Implementation log (2026-04-11):**

1. **Emsdk version mismatch discovered.** Two emsdks on this machine:
   - `/Users/dwalter/Documents/projects/godot/emsdk/` — Emscripten 4.0.20, default `em++` in PATH
   - `/Users/dwalter/shiny_gen_clones/godot-webgpu-tools/emsdk/` — Emscripten 4.0.10, used by `run_demo.sh --rebuild` for deployment
   - Headers differ: 4.0.20 uses `WGPUInstanceDescriptor.requiredFeatures[]` + `WGPUInstanceFeatureName_TimedWaitAny` enum; 4.0.10 uses `WGPUInstanceDescriptor.capabilities.timedWaitAnyEnable` (embedded struct). Code must match the **deployment** emsdk (4.0.10).
   - Callback signatures also differ: 4.0.20's `WGPUQueueWorkDoneCallback` includes `WGPUStringView p_message`; 4.0.10's does not.

2. **Classic ASYNCIFY (`-sASYNCIFY=1`) crashes Binaryen's wasm-opt.** Error: `UNREACHABLE executed at Asyncify.cpp:1146 — unexpected expression type`. Root cause: Godot's web build uses `-sSUPPORT_LONGJMP='wasm'` which emits Wasm EH instructions (try/catch/throw). Binaryen's Asyncify pass does **not** support Wasm EH instructions. Confirmed by:
   - Crash reproduces with both wasm-opt v123 (emsdk 4.0.10) and v124 (4.0.20)
   - Crash reproduces with `-O0`, without `--enable-exception-handling`, without `--enable-simd`
   - Crash does NOT reproduce when longjmp mode is changed to `'emscripten'` (JS-based)
   - Fix: conditionally set `-sSUPPORT_LONGJMP='emscripten'` for webgpu builds in `detect.py`

3. **Build succeeds with longjmp fix.** Both non-dlink (template_release, 4.0.20) and dlink (template_debug, 4.0.10) build cleanly with ASYNCIFY once `-sSUPPORT_LONGJMP='emscripten'` replaces `'wasm'`.

4. **Binary size impact is severe.** Dlink `godot.side.wasm`: 43.6 MB → 84.3 MB (+93%). This is much worse than the original 10-20% estimate. The doubling is because ASYNCIFY instruments every function that can transitively reach an async import, and Godot's call graph is extremely deep. Non-dlink `godot.wasm`: 47 MB → 78 MB (+66%).

5. **Runtime crash: `RuntimeError: unreachable` during engine init.** This is the ASYNCIFY stack overflow predicted in risk #1. `ASYNCIFY_STACK_SIZE=65536` (64 KB) is too small for Godot's deep call stacks during initialization. SDL's timer code calls `emscripten_sleep()` once ASYNCIFY is detected, and if the call stack is deep enough at that point, the 64 KB Asyncify stack overflows. Fix: bump to 262144 (256 KB). **Not yet rebuilt/tested with this fix.**

6. **256 KB ASYNCIFY stack did NOT fix the runtime crash.** Same `RuntimeError: unreachable` at startup with `ASYNCIFY_STACK_SIZE=262144`. The crash may not be a stack overflow at all — could be dlink (MAIN_MODULE + SIDE_MODULE) incompatibility with ASYNCIFY, or the `emscripten` longjmp mode change breaking something during init. Not investigated further.

**Conclusion:** S4 is blocked by three compounding issues:
1. **Binary size doubles** (43 MB → 84 MB side.wasm) — unacceptable for a demo site.
2. **Requires longjmp mode change** (`'wasm'` → `'emscripten'`) to avoid a Binaryen Asyncify crash on Wasm EH instructions. Behavioral impact on libpng/libjpeg error paths unknown.
3. **Runtime crash persists** even after stack size bump — root cause unclear, likely dlink + ASYNCIFY interaction.

S4 may become viable in the future if: (a) Binaryen fixes its Asyncify pass to handle Wasm EH, removing the longjmp workaround; (b) `ASYNCIFY_ONLY` narrowing is implemented to reduce binary bloat; (c) dlink + ASYNCIFY runtime issues are resolved. Code is preserved on branch `asyncify-experiment` for future reference.

**Decision: proceed with S2 (demo rewrite).** Rewrite `compute_heightmap` and `compute_texture` demos to use `texture_get_data_async`. Document this as a known WebGPU limitation in the PR.

**Context:** The `compute_heightmap` demo on godotwebgpu.com renders a black "Computed Island" panel on first click because `RenderingDevice::texture_get_data()` returns zeros. All WebGPU validation errors for this demo are already fixed (see the R8 storage promotion work in `rendering_device_driver_webgpu.cpp::_promote_storage_format`). What remains is a deeper, architectural issue: the WebGPU driver cannot synchronously complete an async buffer map while C++ holds the WASM call stack.

This note summarizes a four-agent investigation into the root cause, evaluates every plausible fix, and proposes the path forward.

---

## 1. Problem statement

### 1.1 The user-visible symptom

1. User loads `/demos/compute_heightmap/debug.html`.
2. Clicks the "Create (GPU)" button.
3. Expected: "Noise (CPU)" panel on the left and "Computed Island" panel on the right both show a heightmap-derived island.
4. Actual: "Noise (CPU)" panel shows the noise, "Computed Island" panel stays **black**. No GPU validation errors, no console errors.

### 1.2 Why native Godot (Vulkan, Metal, D3D12) works and WebGPU does not

The demo calls `RenderingDevice.texture_get_data()` immediately after `rd.sync()`. On every other driver backend, this is a documented synchronous API: the call returns the current GPU texture contents as bytes. All existing Vulkan/Metal/D3D12 drivers implement it synchronously using a staging buffer + blocking fence wait.

WebGPU from WASM is the outlier. The Dawn JS binding only exposes an async `buffer.mapAsync()` that resolves via a `Promise.then(...)`. On single-threaded WASM without cooperative stack unwinding, a JS microtask cannot run while C++ holds the stack — there is no event-loop tick available between the call to `mapAsync()` and the return from `buffer_map()`.

**The demo is not buggy. The engine-side API contract of `texture_get_data()` is synchronous, and every other driver honors it. WebGPU is the only driver that currently violates it.** Therefore the fix belongs in the WebGPU driver, not in the demo.

### 1.3 The call path

`compute_heightmap/main.gd::compute_island_gpu()` →
`RenderingDevice::texture_get_data()` (`servers/rendering/rendering_device.cpp:2030-2146`) →
the `else` branch at line 2050 (because the demo's heightmap texture does not carry `TEXTURE_USAGE_CPU_READ_BIT`) →
`driver->buffer_create(tmp_buffer, TRANSFER_TO_BIT, CPU)` →
`draw_graph.add_texture_get_data(...)` →
`_flush_and_stall_for_all_frames()` →
`driver->buffer_map(tmp_buffer)` (`drivers/webgpu/rendering_device_driver_webgpu.cpp:719-785`) →
`wgpuBufferMapAsync(..., AllowSpontaneous, _buffer_deferred_map_cb)` →
bounded `for` loop calling `wgpuInstanceProcessEvents` up to 64 times →
loop exits with `buf->map_complete == false` → returns zeroed shadow buffer →
caller copies zeros into the user's `Vector<uint8_t>` →
caller immediately frees `tmp_buffer` on line 2143.

### 1.4 Why `wgpuInstanceProcessEvents` cannot possibly work

Both Agent 1 and Agent 2 independently read the emdawnwebgpu source shipped with the local emsdk (`.../cache/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu/src/webgpu.cpp` and `library_webgpu.js`) and converged on the same conclusion:

1. **`library_webgpu.js:1067-1093`** — `emwgpuBufferMapAsync` stores `buffer.mapAsync(mode, offset, size).then(() => _emwgpuOnMapAsyncCompleted(...))` as a pending future. `.then()` is a JS microtask. On single-threaded WASM, microtasks only drain after the WASM call stack fully unwinds back to the browser event loop.

2. **`webgpu.cpp:508-545`** — `EventManager::ProcessEvents` only completes events with `mMode == WGPUCallbackMode_AllowProcessEvents && mIsReady == true`. The current driver code uses `WGPUCallbackMode_AllowSpontaneous`, which `ProcessEvents` deliberately skips.

3. **`webgpu.cpp:674-700`** — `SetFutureReady` is the only place that flips `mIsReady = true`, and it is only called from the JS-side completion path (`_emwgpuOnMapAsyncCompleted`), which is itself called from inside the `.then()` microtask — which cannot fire while C++ holds the stack.

**Conclusion: The existing 64-iteration poll loop in `buffer_map()` (`rendering_device_driver_webgpu.cpp:767-774`) is architecturally incapable of ever resolving a `mapAsync` future.** It is dead code. Even if you increased the count to 64,000, the result would be the same. The loop is spinning over an empty event bag because the event cannot be marked ready from inside a synchronous C call.

This is not a WebGPU limitation. It is a consequence of building without cooperative stack unwinding (ASYNCIFY / JSPI / pthreads-with-atomics). The emdawnwebgpu authors anticipated this exact case and built a sanctioned solution for it — see option S4 below.

---

## 2. Options evaluated

### S1. Persistent readback cache for the high-level path (Agent 3)

**Idea:** Stash the source texture pointer in the tmp_buffer (add fields to `WGBuffer`), and in `buffer_map()` look up a persistent `_readback_cache` entry keyed by the source texture. On first call, allocate the entry and return zeros. On subsequent calls, return the previous frame's cached data. This is the same pattern already used by `RenderingDeviceDriverWebGPU::texture_get_data` (the driver-level override) and `buffer_get_data_direct`.

**Pros:**
- No Emscripten flag changes.
- No JS/WASM build size impact.
- Matches the pattern already working in the driver for `CPU_READ_BIT` textures.
- Implementation is local (~50 lines in `rendering_device_driver_webgpu.cpp` + 2 fields in `webgpu_objects.h`).

**Cons:**
- **First click still returns zeros.** The fundamental microtask-can't-fire problem is unchanged. The cache only helps second and later calls, and the second call would return the first call's stale data, which — if the user changed the noise seed between clicks — is wrong.
- Doesn't match native behavior. The demo would visibly lag behind by one click.
- Does not fix `buffer_get_data` callers either; any GPU → CPU readback on single-threaded WASM stays broken.

**Verdict:** Not a complete fix. Useful as a complementary latency-reducer if ASYNCIFY is off the table, but it does not close the case.

### S2. Demo rewrite to use `texture_get_data_async` (Agent 4)

**Idea:** Rewrite `compute_heightmap/main.gd::compute_island_gpu()` to use `rd.texture_get_data_async(rid, layer, Callable)` with a pattern like:

```gdscript
rd.texture_get_data_async(heightmap_rid, 0, _on_heightmap_ready)
rd.submit()
for i in range(3):
    await get_tree().process_frame   # let the browser microtask queue drain
rd.sync()                              # callback fires here, data is valid
```

**Pros:**
- Works today, no engine changes.
- Idiomatic Godot 4 async-readback pattern, blessed by `doc/classes/RenderingDevice.xml:992-1008`.
- Works unchanged on every backend.

**Cons:**
- **Modifies upstream demo.** `godot-demo-projects/compute/heightmap/main.gd` is maintained upstream; divergence adds maintenance burden and the change does not apply to the native backends where the sync path is correct.
- Does not fix the underlying engine bug. Any other demo or user project that calls `texture_get_data()` synchronously on WebGPU stays broken (same for `compute_texture`, screenshot capture, any future compute-readback code).
- Silently exports the WebGPU limitation into user code. Every Godot project that targets WebGPU would need to learn this pattern, violating the "write once, run on every driver" promise.

**Verdict:** Correct workaround but wrong layer. Rejected by user policy: fix WebGPU, not the demo.

### S3. JSPI — `-sJSPI=1` (Agent 1)

**Idea:** Enable browser-native stack switching via JavaScript Promise Integration (`WebAssembly.Suspending` / `WebAssembly.promising`). Near-zero runtime overhead. Same API surface as ASYNCIFY from the C side (`emwgpuWaitAny` is marked `__async: true`, the `#if ASYNCIFY` preprocessor guard covers both JSPI mode and classic mode).

**Pros:**
- Near-zero binary size and runtime overhead.
- Cleanest possible mechanism.
- Same C API as classic ASYNCIFY.

**Cons:**
- **Chrome 126+ only.** Firefox does not ship JSPI enabled by default as of early 2026. Safari support is behind a flag.
- Emscripten still marks `-sJSPI=1` as experimental (`tools/link.py:1791-1792`).
- No explicit test coverage for JSPI + `MAIN_MODULE` / dlink builds.

**Verdict:** Not cross-browser. Rejected for godotwebgpu.com which aims to work everywhere.

### S4. Classic ASYNCIFY — `-sASYNCIFY=1` with `wgpuInstanceWaitAny` (Agents 1 + 2)

**Idea:** Enable classic Emscripten Asyncify. Use `wgpuInstanceWaitAny(timeoutNS > 0)` inside `buffer_map()` to block on the map future. Asyncify unwinds the WASM stack, lets the JS event loop tick, the `.then()` microtask fires, `SetFutureReady` marks the event complete, Asyncify rewinds the stack, and `wgpuInstanceWaitAny` returns with valid data.

This is **the sanctioned path**. Agent 2 found that the emdawnwebgpu authors built this solution specifically for this problem:

- **`webgpu.cpp:1636-1658`** — `wgpuCreateInstance` exposes the `TimedWaitAny` feature only if `emscripten_has_asyncify()` returns nonzero.
- **`webgpu.cpp:547-608`** — `EventManager::WaitAny` with `timeoutNS > 0` routes to `emwgpuWaitAny`, the Asyncify-enabled JS helper.
- **`library_webgpu.js:680-711`** — `emwgpuWaitAny` is marked `__async: true` when `ASYNCIFY` is defined; without it, the function is literally `abort('TODO: Implement asyncify-free WaitAny for timeout=0')`.

Classic `-sASYNCIFY=1` is plain Binaryen wasm instrumentation — it works on every WebGPU-capable browser (Chrome, Firefox, Safari). It is compatible with the existing `dlink_enabled=yes` / `MAIN_MODULE`+`SIDE_MODULE` build (`tools/link.py:432-435` automatically passes `--pass-arg=asyncify-relocatable`). The build is single-threaded, so there are no pthreads interactions.

**Pros:**
- **First-click readback returns correct data.** Matches native behavior exactly.
- **Cross-browser.** No Chrome-only / experimental features.
- **Minimal C++ surface area.** Three small, well-scoped changes: a build flag, one instance descriptor tweak, and a ~20-line rewrite of `buffer_map()`.
- **Sanctioned by emdawnwebgpu.** The authors built the API explicitly for this use case.
- **Unlocks other broken paths.** `compute_texture` (still listed as broken in `SCENE_REVIEW.md`), screenshot capture, and any future compute-readback demo work for free.
- **No demo changes.** `compute_heightmap` stays upstream-clean.

**Cons:**
- **Binary size: +93% on `godot.side.wasm` (actual).** 43 MB → 84 MB. Original estimate was +10-20% — actual is far worse. Every C++ function that can be on the stack during an unwind gets a Binaryen-instrumented state machine, and Godot's call graph is very deep/wide. This may be improvable with `ASYNCIFY_ONLY` narrowing but that is fragile with dlink + Godot's virtual dispatch.
- **Runtime CPU: unknown.** Estimated 5-15% but not yet benchmarked.
- **Requires `ASYNCIFY_STACK_SIZE` bump.** 64 KB is too small (confirmed — crashes with `RuntimeError: unreachable` during engine init). Need at least 256 KB, possibly more.
- **Requires `-sSUPPORT_LONGJMP='emscripten'` instead of `'wasm'`.** Binaryen's Asyncify pass crashes on Wasm EH instructions that the `'wasm'` longjmp mode emits. The `'emscripten'` mode uses JS-based exception handling instead, which is slower for longjmp-heavy code (libpng error paths, etc.) but compatible with Asyncify.
- **SDL behavior change.** `thirdparty/sdl/timer/unix/SDL_systimer.c:150` already probes `emscripten_has_asyncify()` at runtime; once Asyncify is on, SDL starts inserting `emscripten_sleep()` calls in timer busy-waits. This is what triggers the Asyncify stack overflow during init if the stack is too small.
- **Must re-verify every demo.** The benchmark scenes in particular care about per-frame CPU cost.

**Verdict (updated):** The correct fix conceptually, but real-world costs are significantly higher than estimated. The ~93% binary size increase may be a dealbreaker for a demo site focused on load performance. Needs further evaluation before committing.

---

## 3. Proposed solution

Enable classic `-sASYNCIFY=1` for the WebGPU web build and replace the broken `processEvents` poll with `wgpuInstanceWaitAny`.

### 3.1 Build flag — `platform/web/detect.py`

Add two LINKFLAGS inside the existing `if env["webgpu"]:` block (currently lines 261-266):

```python
if env["webgpu"]:
    env.AppendUnique(CPPDEFINES=["WEBGPU_ENABLED", "RD_ENABLED"])
    env.Append(CCFLAGS=["--use-port=emdawnwebgpu"])
    env.Append(LINKFLAGS=["--use-port=emdawnwebgpu"])
    # Cooperative stack unwinding — required for synchronous buffer_map()
    # and fence_wait() via wgpuInstanceWaitAny(). See webgpu_notes/ASYNC_WEBGPU.md.
    env.Append(LINKFLAGS=["-sASYNCIFY=1"])
    # Default 4 KB is too small for Godot's GDScript-to-RenderingDevice call depth.
    env.Append(LINKFLAGS=["-sASYNCIFY_STACK_SIZE=65536"])
```

Do **not** enable `ASYNCIFY_IGNORE_INDIRECT` or `ASYNCIFY_ONLY` in the first pass. Agent 1 specifically called out that narrowing via function lists is fragile with dlink + LTO + deep Godot call graphs. Enable globally, measure, narrow later only if the perf hit is unacceptable.

No `SCsub`, `emscripten_helpers.py`, or `SConstruct` changes are required. The dlink branch in `platform/web/SCsub` propagates LINKFLAGS through cleanly.

### 3.2 Enable `TimedWaitAny` at instance creation

The Dawn event manager hard-fails `wgpuInstanceWaitAny(timeoutNS > 0)` with `WGPUWaitStatus_Error` (`webgpu.cpp:1702-1709`) unless the feature is requested at instance creation.

In `drivers/webgpu/rendering_context_driver_webgpu.cpp`, line 70 (the `wgpuCreateInstance` call in `initialize()`), enable TimedWaitAny via the embedded `WGPUInstanceCapabilities` struct:

```cpp
WGPUInstanceDescriptor inst_desc = {};
inst_desc.capabilities.timedWaitAnyEnable = true;
inst_desc.capabilities.timedWaitAnyMaxCount = 1;
instance = wgpuCreateInstance(&inst_desc);
```

**Note:** The original note incorrectly assumed a `requiredFeatures` array API. The actual emdawnwebgpu header (`webgpu.h:1944-1947`) embeds `WGPUInstanceCapabilities` directly in `WGPUInstanceDescriptor` with `timedWaitAnyEnable` (bool) and `timedWaitAnyMaxCount` (size_t) fields.

Emdawnwebgpu silently drops the feature if Asyncify is off, so this is safe to keep in the code on future builds without the flag.

### 3.3 Rewrite `buffer_map()` — `rendering_device_driver_webgpu.cpp:719-785`

Replace the current `is_readback && buf->handle` branch with a single synchronous `wgpuInstanceWaitAny` on the future returned by `wgpuBufferMapAsync`:

```cpp
if (buf->is_readback && buf->handle) {
    if (!buf->shadow_map) {
        buf->shadow_map = (uint8_t *)memalloc(buf->size);
        memset(buf->shadow_map, 0, buf->size);
    }
    buf->map_complete = false;

    WGPUBufferMapCallbackInfo cb = {};
    cb.mode = WGPUCallbackMode_WaitAnyOnly;   // must be WaitAnyOnly or AllowProcessEvents
    cb.callback = _buffer_deferred_map_cb;
    cb.userdata1 = buf;
    WGPUFuture fut = wgpuBufferMapAsync(buf->handle, WGPUMapMode_Read, 0, buf->size, cb);

    WGPUInstance inst = context_driver ? context_driver->get_instance() : nullptr;
    if (inst) {
        WGPUFutureWaitInfo wait_info = { fut, 0 };
        const uint64_t one_second_ns = 1'000'000'000ULL;
        WGPUWaitStatus status = wgpuInstanceWaitAny(inst, 1, &wait_info, one_second_ns);
        if (status != WGPUWaitStatus_Success) {
            WARN_PRINT("WebGPU buffer_map wait failed or timed out");
        }
    }
    // buf->map_complete is now true; _buffer_deferred_map_cb has already run.
    return buf->shadow_map;
}
```

Delete the old `processEvents` loop and the "first call vs. subsequent call" branching — with Asyncify, every call is synchronous and the branching is dead.

### 3.4 `fence_wait()` — verify and convert if needed

Agent 1 pointed out that `fence_wait()` also needs to block on the GPU submitted-work-done future for correctness; today its implementation in `rendering_device_driver_webgpu.cpp:1993-2018` is effectively a `wgpuInstanceProcessEvents` poll (same dead-code problem). Even if the buffer_map future resolves synchronously via WaitAny, the underlying GPU copy must have completed first, or we're reading stale GPU memory.

**Pre-flight finding:** `fence_wait()` (lines 1993-2018) calls `wgpuInstanceProcessEvents` once, then unconditionally force-signals the fence if the callback hasn't fired. This is an "optimistic best-effort" approach. The comment explains: fence_wait is called at frame start for the *previous* frame's fence, so the GPU has had a full frame duration to complete — force-signal is safe for the normal render loop.

**For compute readback specifically**, this is also safe: `buffer_map()`'s `wgpuInstanceWaitAny` on the map future implicitly waits for GPU completion, because the map future cannot resolve until the preceding GPU copy finishes. So `fence_wait()`'s force-signal doesn't cause stale reads.

**Action:** Leave `fence_wait()` as-is for Phase 1. The buffer_map WaitAny is the real synchronization point. If testing reveals issues, convert fence_wait to WaitAny in a follow-up.

### 3.5 Cleanup

- Remove the `_readback_cache`-based workaround branches in the driver's own `RenderingDeviceDriverWebGPU::texture_get_data` and `buffer_get_data_direct` — they become redundant once `buffer_map` is synchronous. Optional; they still work and can stay until we're confident in the new path.
- The promoted-format conversion in `_buffer_deferred_map_cb` (via `_convert_promoted_readback`) stays as is. It runs inside the map callback, which is triggered synchronously from `wgpuInstanceWaitAny`, so no change needed.

---

## 4. Tradeoffs summary

| Metric | Before | After ASYNCIFY | Notes |
|---|---|---|---|
| `compute_heightmap` first-click | Black panel | Correct island | Primary fix |
| `compute_texture` remaining errors | Unknown (likely same root cause) | Fixed for free | Secondary benefit |
| Screenshot capture on WebGPU | Returns zeros synchronously | Correct | Tertiary benefit |
| `godot.side.wasm` size | ~43 MB | ~84 MB | **+93%** (actual, much worse than estimated 10-20%) |
| Rendering CPU overhead | Baseline | Unknown | Not yet benchmarked; estimate was 5-15% |
| Cross-browser support | Chrome/FF/Safari | Chrome/FF/Safari | No change |
| Demo changes required | N/A | None | `compute_heightmap` stays upstream-clean |
| Engine surface area touched | — | 3 files, ~40 lines | `detect.py`, `rendering_context_driver_webgpu.cpp`, `rendering_device_driver_webgpu.cpp` |
| SDL timer behavior | Busy-wait | Cooperative yield | Net positive (better browser responsiveness) |

---

## 5. Risks and gotchas

1. **`ASYNCIFY_STACK_SIZE` trap.** If we see `RuntimeError: unreachable executed` in browser console after the flag is added, it is almost always the Asyncify stack being too small. Bump `65536` → `131072` (128 KB) before anything else.
2. **`ASYNCIFY_IMPORTS` is auto-populated.** New Emscripten versions automatically include `emwgpuWaitAny` and any other JS library function marked `__async: true`. We do not need to manually list it.
3. **Timeout units are nanoseconds.** 1 second = `1'000'000'000ULL`. Use a named constant to avoid integer-literal overflow on misplaced digits.
4. **`WGPUCallbackMode_WaitAnyOnly` vs. `AllowProcessEvents` vs. `AllowSpontaneous`.** Must NOT use `AllowSpontaneous` when we plan to `WaitAny` on the future — check the current emdawnwebgpu semantics before committing. `WaitAnyOnly` or `AllowProcessEvents` is correct here.
5. **`TimedWaitAny` feature enablement.** If step 3.2 is skipped, `wgpuInstanceWaitAny(timeoutNS > 0)` returns `WGPUWaitStatus_Error` and the map callback never fires. Silent failure.
6. **Editor build CPU cost.** The Godot editor runs the whole engine in WASM, so the 5-15% overhead applies there too. This project is the web export template, not the editor, so lower priority; still worth confirming the editor (if built) still loads.
7. **Benchmark regression risk.** The benchmark scenes (`scene_a` sprites, `scene_c` instances, `scene_d` particles) are the most CPU-sensitive test cases. Post-change numbers need a side-by-side comparison.
8. **Spurious double-yielding.** Do NOT add `emscripten_sleep(1)` anywhere else as a "just in case" yield. Every sleep point is a size/perf hit. `wgpuInstanceWaitAny` is the single mechanism.
9. **Callback context for `_buffer_deferred_map_cb`.** The callback fires synchronously inside `wgpuInstanceWaitAny`'s return path. The current implementation (memcpy + promoted-format conversion + flag set) is safe in that context. If we ever extend it to call further async WebGPU APIs, the context matters.
10. **`onSubmittedWorkDone` fence parity.** `fence_wait()` must correctly block on GPU completion or `buffer_map`'s data is stale even though `map_complete` is true. See task 6.2.

---

## 6. Task list

### 6.1 Pre-flight verification

- [x] Read `fence_wait()` — optimistic force-signal approach, safe for Phase 1 (see Section 3.4).
- [x] Read `rendering_context_driver_webgpu.cpp` — `wgpuCreateInstance` at line 71, empty descriptor. Corrected API: `capabilities.timedWaitAnyEnable` (see Section 3.2).
- [ ] Check `godot.side.wasm` current size so we have a baseline for the post-change size delta.
- [ ] Capture baseline benchmark numbers for `scene_a`, `scene_c`, and `scene_d` (fps, frame time).

### 6.2 Implementation

- [ ] Edit `platform/web/detect.py` — add the two `LINKFLAGS` entries (section 3.1).
- [ ] Edit `drivers/webgpu/rendering_context_driver_webgpu.cpp` — request `WGPUInstanceFeatureName_TimedWaitAny` at instance creation (section 3.2).
- [ ] Rewrite `buffer_map()` readback branch to use `wgpuInstanceWaitAny` (section 3.3).
- [ ] If `fence_wait()` is broken, rewrite it to use `wgpuInstanceWaitAny` on the `onSubmittedWorkDone` future (section 3.4).
- [ ] Delete the dead `processEvents` poll loop and its accompanying comment in `buffer_map()`.

### 6.3 Build and first-run verification

- [ ] `scons platform=web target=template_release webgpu=yes opengl3=no threads=no -j$(sysctl -n hw.ncpu)`
- [ ] Record size of new `godot.side.wasm` and compare to baseline.
- [ ] Deploy to `godotwebgpu.com/public/demos/compute_heightmap/`.
- [ ] Run `node tools/web_qa/test_compute_heightmap.mjs`.
  - Expected: zero GPU errors, zero console errors, and the post-click screenshot shows a non-black "Computed Island" panel.
- [ ] Visual side-by-side vs. native (or vs. the 3D platformer WebGL fallback if available) to confirm the island shape is correct.

### 6.4 Regression sweep

- [ ] Re-run every demo in `SCENE_REVIEW.md` to confirm nothing regressed:
  - [ ] `2d_platformer`
  - [ ] `2d_sprite_shaders`
  - [ ] `2d_particles`
  - [ ] `gui_control_gallery`
  - [ ] `3d_platformer`
  - [ ] `3d_lights_and_shadows`
  - [ ] `3d_particles`
  - [ ] `compute_texture` — this one may also flip to ✅ automatically
  - [ ] `viewport_gui_in_3d`
- [ ] Re-run every benchmark and compare fps vs. baseline:
  - [ ] `scene_a` (sprites)
  - [ ] `scene_b` (pbr)
  - [ ] `scene_c` (instances)
  - [ ] `scene_d` (particles)
  - [ ] `scene_e` (animated)
  - [ ] `scene_f` (postfx)
- [ ] Document any perf regression > 10% and decide whether to pursue `ASYNCIFY_IGNORE_INDIRECT` or `ASYNCIFY_ONLY` narrowing.

### 6.5 Cleanup and commit

- [ ] Remove the now-redundant `_readback_cache` workaround in `RenderingDeviceDriverWebGPU::texture_get_data` and `buffer_get_data_direct` — optional; can stay as belt-and-braces.
- [ ] Update `SCENE_REVIEW.md` — flip `compute_heightmap` (and possibly `compute_texture`) from ⚠️ to ✅.
- [ ] Update this note with a `## 7. Post-implementation results` section capturing the real size delta, real benchmark deltas, and any gotchas hit during implementation.
- [ ] Commit in three logical steps:
  1. Build-flag change (`detect.py`).
  2. Engine implementation (`rendering_context_driver_webgpu.cpp` + `rendering_device_driver_webgpu.cpp`).
  3. Deploy + `SCENE_REVIEW.md` update.

---

## 7. Reference: emdawnwebgpu source locations

For future reference during implementation. These are the source files Agents 1 and 2 used for evidence, all inside the local emsdk checkout:

- **webgpu.h** — `.../emsdk/upstream/emscripten/cache/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu/include/webgpu/webgpu.h`
  - Function declarations for `wgpuBufferMapAsync`, `wgpuInstanceWaitAny`, `wgpuInstanceProcessEvents`, and the `WGPUFuture` / `WGPUFutureWaitInfo` structs.
- **webgpu.cpp** — `.../emdawnwebgpu_pkg/webgpu/src/webgpu.cpp`
  - `EventManager::ProcessEvents` at lines 508-545 (only drains `AllowProcessEvents` + `mIsReady==true`).
  - `EventManager::WaitAny` at lines 547-608 (routes to `emwgpuWaitAny` when `timeoutNS > 0`).
  - `SetFutureReady` at lines 674-700 (only entry point for `mIsReady = true`; called from JS).
  - `MapAsyncEvent::Complete` at lines 1121-1141 (flips `mMapState` → `Mapped`).
  - `WGPUInstanceImpl::Create` at lines 1636-1658 (TimedWaitAny gated on `emscripten_has_asyncify()`).
  - `WGPUInstanceImpl::WaitAny` at lines 1698-1726 (requires TimedWaitAny feature when `timeoutNS > 0`).
- **library_webgpu.js** — `.../emdawnwebgpu_pkg/webgpu/src/library_webgpu.js`
  - `emwgpuBufferMapAsync` at lines 1062-1093 (JS `.then()` chain).
  - `emwgpuWaitAny` at lines 680-711 (Asyncify-gated; `abort()` stub without Asyncify).
- **Emscripten build tools:**
  - `tools/link.py:432-465` — ASYNCIFY pass setup, `asyncify-relocatable` auto-enabled for MAIN_MODULE.
  - `tools/link.py:1791-1792` — JSPI experimental warning.
  - `src/settings.js:809-955` — ASYNCIFY / JSPI settings reference.
  - `src/lib/libasync.js:454-485` — `emscripten_sleep` / JSPI implementation.

---

## 8. Related prior notes

- `webgpu_notes/RESEARCH.md:540-578` — early mention of ASYNCIFY as "may be needed for async init," flagged as "adds ~10-20% binary size." That analysis was correct but predates the current readback issue.
- `webgpu_notes/TASKS.md:543-545` — prior decision: "`buffer_map()` / `buffer_unmap()`: WebGPU mapping is async ... Or use ASYNCIFY. Decision: **prefer `wgpuQueueWriteBuffer()` path for uploads**." Correctly handled uploads but never solved downloads.
- `webgpu_notes/TASKS.md:1813-1814` — known-open issue: "This is a fundamental WebGPU limitation on single-threaded WASM — synchronous readback is impossible." **This statement is true for the current build configuration, but it is not a WebGPU limitation per se — it is a consequence of not enabling ASYNCIFY. The emdawnwebgpu authors built a solution specifically for this case.** This note supersedes that characterization.
- `webgpu_notes/DESIGN.md:219` — `fence_wait()` sketch: "In Emscripten, this may need `emscripten_sleep()` or just check a callback flag. Consider: use `wgpuQueueOnSubmittedWorkDone()` to set a flag, then check it." On the right track; once Asyncify is on, the "just check a callback flag" loop can be replaced with `wgpuInstanceWaitAny` on the work-done future.

---

## 9. Post-implementation results (2026-04-12)

**Outcome:** S2 (demo rewrite) implemented and verified. `compute_heightmap` is now ✅ in SCENE_REVIEW.md.

S4 (ASYNCIFY) was abandoned due to three compounding blockers: +93% binary size, longjmp mode change requirement, and persistent runtime crashes (see Implementation log above). S2 was the pragmatic path forward.

### 9.1 What was actually done

**Engine changes (7 files, +713 / -56 lines):**

1. **`platform/web/js/engine/engine.js`** — Added `'readonly-and-readwrite-storage-textures'` to `requestDevice()` `optionalFeatures`. Required for `readonly` storage image access in compute shaders.

2. **`drivers/webgpu/rendering_device_driver_webgpu.cpp`** — Multiple fixes:
   - Fixed `_fence_work_done_callback` signature: added `WGPUStringView p_message` parameter for emdawnwebgpu compatibility (was causing compile errors with newer emsdk).
   - `texture_get_data_async()` implementation: new driver method that issues `wgpuBufferMapAsync` with a callback that memcpys data + runs promoted-format reverse conversion, then invokes the user's GDScript callable.
   - Cleaned up all debug WARN_PRINTs (writeTexture, buffer_unmap FLUSH, WGSL dump, texture create/name logs, SYNC-CONFLICT traces).

3. **`servers/rendering/rendering_device.cpp`** — `texture_get_data_async()` high-level implementation: creates a staging buffer, adds a texture-to-buffer copy to the draw graph, flushes, then delegates to the driver's async map. Re-enabled `RENDER_GRAPH_REORDER` (was disabled during debugging).

4. **`servers/rendering/rendering_device.h`** + **`rendering_device_driver.h`** — Declarations for the new `texture_get_data_async` API surface.

5. **`drivers/webgpu/rendering_device_driver_webgpu.h`** — Driver-side declarations.

**Demo changes (3 files in godot-demo-projects):**

1. **`compute/heightmap/main.gd`** — Two critical fixes:
   - **Manual gradient image construction:** `GradientTexture1D.get_image().get_data()` returns zeros on WebGPU because the texture generates asynchronously and isn't ready when `init_gpu()` runs. Fix: build the gradient image manually pixel-by-pixel from `Gradient.sample(t)`.
   - **Async readback pattern:** Replaced synchronous `rd.texture_get_data()` with two submit/sync cycles:
     ```gdscript
     # First submit/sync: upload + compute
     rd.submit()
     for i in 5: await get_tree().process_frame
     rd.sync()
     # Second submit/sync: readback via texture_get_data_async
     var result_holder := {"data": PackedByteArray()}
     rd.texture_get_data_async(heightmap_rid, 0, func(data): result_holder["data"] = data)
     rd.submit()
     for i in 5: await get_tree().process_frame
     rd.sync()
     ```

2. **`compute/heightmap/compute_shader.glsl`** — Restored to original full shader logic (was left in a debug constant-output state during investigation).

3. **`compute/heightmap/project.godot`** — Renderer setting update.

### 9.2 Root causes identified

Two independent bugs were causing the black "Computed Island" panel:

1. **`imageLoad(gradient, ...)` returned `vec4(0)`** — The gradient texture data was all zeros. This was NOT a WebGPU driver issue. `GradientTexture1D` generates its image asynchronously on the GPU, and on WebGPU, calling `.get_image().get_data()` before the generation completes returns an empty byte array. The fix (manual `Gradient.sample()` loop) is backend-agnostic and would also be correct on Vulkan/Metal.

2. **Synchronous readback returns zeros** — The original `rd.texture_get_data()` call hits `buffer_map()` which cannot resolve `mapAsync` on single-threaded WASM (see Section 1.4). The `texture_get_data_async` pattern with submit/sync cycles lets the browser event loop drain microtasks between frames.

### 9.3 Verification

- 0 GPU validation errors
- 5 console warnings (all `float32-filterable` feature — cosmetic, not functional)
- Screenshot confirms correct island shape in "Computed Island" panel
- Both GPU and CPU paths produce matching output

### 9.4 Remaining work

- [ ] `compute_texture` demo — still ⚠️ in SCENE_REVIEW.md. Uses `RenderingServer.get_rendering_device()` (global RD, not local), `R32_SFLOAT` format (natively supported), and `call_on_render_thread`. Different architecture from compute_heightmap — may have different issues.
- [ ] Commit engine changes on `webgpu_bak_phase_7_fix` branch.
- [ ] Commit demo changes in `godot-demo-projects`.
- [ ] Deploy updated compute_heightmap to godotwebgpu.com.
