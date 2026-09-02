# Platform/Web & WASM Integration Review

## Overview

This review covers the WebGPU web platform integration layer for the godot-webgpu
project (branch `webgpu-4.6.2`). The review focuses on:

1. JS engine glue and initialization flow
2. Async patterns and memory safety on WASM
3. Threading model and nothreads build support
4. Build system changes for the web platform

---

## 1. WebGPU Initialization Flow (JS through WASM)

### 1.1 High-Level Sequence

The initialization follows a carefully ordered async pipeline:

```
[Browser]                    [Engine JS]                      [WASM/C++]
    |                            |                                |
    |  Engine.startGame()        |                                |
    |--------------------------->|                                |
    |                            |  if renderingDriver=='webgpu'  |
    |                            |  Promise.all([                 |
    |  navigator.gpu             |    requestWebGPUDevice(),      |
    |<---------------------------|    loadNagaSpirvToWgsl()       |
    |  adapter + device          |  ])                            |
    |--------------------------->|                                |
    |                            |  config.preinitializedWebGPUDevice = device
    |                            |  init(exe) + preloadFile(pack) |
    |                            |  getModuleConfig() includes:   |
    |                            |    { preinitializedWebGPUDevice: device }
    |                            |---> Godot(moduleConfig)        |
    |                            |                                |
    |                            |           Module.callMain()    |
    |                            |------------------------------->|
    |                            |                                |
    |                            |    DisplayServerWeb constructor |
    |                            |    RenderingContextDriverWebGPU::initialize()
    |                            |      EM_ASM: WebGPU.importJsDevice(Module.preinitializedWebGPUDevice)
    |                            |    window_create / screen_create
    |                            |    RendererCompositorRD::make_current()
```

### 1.2 JS Engine Glue (`engine.js` and `config.js`)

**New Config Fields:**
- `renderingDriver` (string): `'webgpu'` or `'opengl3'`. Set by the export plugin
  based on `rendering/renderer/rendering_method.web` project setting.
- `preinitializedWebGPUDevice` (GPUDevice|null): Allows external pre-initialization
  or is populated automatically by `Engine.requestWebGPUDevice()`.

**Module Configuration Pass-Through:**
In `config.js::getModuleConfig()`, the `preinitializedWebGPUDevice` is conditionally
spread into the Emscripten module config object:
```js
...(this.preinitializedWebGPUDevice ? { 'preinitializedWebGPUDevice': this.preinitializedWebGPUDevice } : {})
```
This makes it available to C++ via `Module["preinitializedWebGPUDevice"]`.

### 1.3 WebGPU Device Request (`Engine.requestWebGPUDevice`)

The static method performs feature detection and graceful degradation:

1. **Feature detection**: Checks `navigator.gpu` existence, rejects with clear error if missing.
2. **Adapter request**: Uses `powerPreference: 'high-performance'` by default.
3. **Optional features**: Requests all features the adapter supports from a curated list:
   - Texture format tiers (tier1, tier2)
   - Float32 filterable/blendable
   - Compression families (BC, ETC2, ASTC)
   - Dual-source blending, clip-distances
   - Timestamp-query (for GPU profiling)
4. **Limits maximization**: Requests adapter maximum for key limits
   (maxStorageBuffers, maxBufferSize, maxBindGroups, etc.)
5. **Error monitoring**: Attaches `device.lost` and `uncapturederror` listeners immediately.

**Assessment:** This is well-designed. Optional features are gated by adapter support
so the device request never fails due to unsupported features. The limit maximization
is the correct approach for a game engine that needs headroom.

### 1.4 Naga SPIR-V to WGSL Converter (`Engine.loadNagaSpirvToWgsl`)

A separate WASM module (`naga_wasm_bg.wasm`) is loaded and instantiated inline:

- Derives URL from the engine executable base path
- Implements a mini wasm-bindgen runtime (memory management, string encoding)
- Exposes `window.nagaSpirvToWgsl(Uint8Array) -> string` globally
- Called from C++ via `MAIN_THREAD_EM_ASM_PTR` in `_spv_to_wgsl_cached()`
- Results are cached in a process-lifetime HashMap keyed by 64-bit SPIR-V hash

**Assessment:** Loading naga in parallel with the WebGPU device request (`Promise.all`)
is efficient. The global function exposure pattern is pragmatic for `EM_ASM` interop.

### 1.5 C++ Side: RenderingContextDriverWebGPU

The context driver (`rendering_context_driver_webgpu.cpp`) bootstraps from JS:

1. Creates a `WGPUInstance` for async event processing
2. Calls `EM_ASM` to invoke `WebGPU["importJsDevice"](Module.preinitializedWebGPUDevice)`
   which wraps the JS GPUDevice in a C WGPUDevice handle
3. Gets the device queue via `wgpuDeviceGetQueue`

The surface creation uses the emdawnwebgpu-specific
`WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector` with `"#canvas"` hardcoded.

### 1.6 Display Server Integration

`DisplayServerWeb` (in `display_server_web.cpp`) orchestrates:
1. Creates `RenderingContextDriverWebGPU`
2. Calls `initialize()` to import the device
3. Creates a window/surface (`window_create`)
4. Sets initial resolution and vsync mode
5. Creates and initializes `RenderingDevice`
6. Calls `screen_create` (non-fatal if it fails; swapchain resized on first frame)
7. Registers `RendererCompositorRD` as the active compositor

Cleanup in the destructor properly frees: screen -> RenderingDevice -> RenderingContext.

The canvas resize handler (`check_size_force_redraw`) correctly propagates size
changes to the rendering context via `window_set_size`.

---

## 2. Async Execution Model and Safety Guarantees

### 2.1 The Fundamental Challenge

WebGPU on single-threaded WASM cannot perform synchronous GPU readback. The
`mapAsync()` JS API resolves via microtask, which cannot fire while the C++ stack
holds control. The project documents this thoroughly in `ASYNC_WEBGPU.md` and chose
a frame-delayed readback cache approach over ASYNCIFY (which was abandoned due to
binary size doubling and runtime crashes).

### 2.2 Async Callback Architecture

All async WebGPU operations use `WGPUCallbackMode_AllowSpontaneous`, meaning
callbacks fire during `wgpuInstanceProcessEvents()`. The key callback sites:

| Callback | Trigger | Object |
|----------|---------|--------|
| `_buffer_deferred_map_cb` | `wgpuBufferMapAsync` | WGBuffer |
| `_fence_work_done_callback` | `wgpuQueueOnSubmittedWorkDone` | WGFence |
| `_timestamp_readback_callback` | `wgpuBufferMapAsync` (query pool) | WGQueryPool |
| `_readback_map_cb` | `wgpuBufferMapAsync` (texture readback) | ReadbackEntry |

### 2.3 Use-After-Free Prevention Pattern

Two commits (`0c3988488d` and `caab20dae8`) establish a consistent UAF prevention
pattern across all four callback sites. The pattern:

**For each async object type, add two boolean flags:**
- `pending` / `readback_pending` / `map_pending`: True while callback is in flight
- `freed` / `cancelled`: Set by the free/destroy function when pending is true

**Free function behavior:**
```cpp
void buffer_free(BufferID p_buffer) {
    WGBuffer *buf = ...;
    if (buf->map_pending) {
        buf->freed = true;  // Don't delete; callback will handle it
        return;
    }
    // Normal cleanup...
    delete buf;
}
```

**Callback behavior:**
```cpp
static void _buffer_deferred_map_cb(...) {
    WGBuffer *buf = (WGBuffer *)p_userdata1;
    if (!buf) return;
    buf->map_pending = false;

    if (buf->freed) {
        // Owner freed while we were in flight. Clean up everything.
        if (buf->handle) {
            if (p_status == WGPUMapAsyncStatus_Success) wgpuBufferUnmap(buf->handle);
            wgpuBufferRelease(buf->handle);
        }
        if (buf->shadow_map) memfree(buf->shadow_map);
        delete buf;
        return;
    }
    // Normal success/error handling...
}
```

**Assessment:** This pattern is sound and consistently applied. The key insight is
that on WASM's single-threaded event loop, `AllowSpontaneous` callbacks interleave
with object destruction during `wgpuInstanceProcessEvents` calls, which can happen
at frame boundaries or in `fence_wait`. The deferred deletion ensures no dangling
pointers are accessed.

### 2.4 Readback Cache (HashMap Stability Fix)

The original `HashMap<uint64_t, ReadbackEntry>` stored entries by value. When entries
were passed by pointer to async callbacks, HashMap rehashing could invalidate those
pointers. The fix (`0c3988488d`) heap-allocates entries via `memnew(ReadbackEntry)`
to ensure pointer stability.

The `cancelled` flag on `ReadbackEntry` follows the same pattern: if the source
texture/buffer is freed while a map is pending, the callback detects this and
performs full cleanup including `wgpuBufferRelease` and `memfree`.

### 2.5 Fence Wait and Force-Signal

`fence_wait()` attempts `wgpuInstanceProcessEvents` to deliver the work-done
callback, then force-signals if it hasn't fired:

```cpp
if (!fence->signaled) {
    fence->signaled = true;  // Force-signal to avoid deadlock
}
```

This is correct for the WASM execution model: the engine calls `fence_wait` at frame
start for the previous frame's fence. Since a full frame duration has elapsed, the
GPU work has almost certainly completed even if the callback hasn't been delivered.
The force-signal prevents deadlock in the edge case where ProcessEvents doesn't drain.

### 2.6 Device Lost Handling

Device loss is handled at two levels:
1. **JS level** (`engine.js`): `device.lost.then(...)` logs the reason
2. **C++ level** (`rendering_device_driver_webgpu.cpp:490-495`): `EM_ASM` attaches
   a `.lost.then()` handler during `initialize()` that logs to console

There is no graceful recovery path (no recreating the device or notifying the user
via UI). This is acceptable for the current state of the project, but worth noting
as a future improvement area.

---

## 3. Memory Safety Patterns

### 3.1 OOM Handling (ABORTING_MALLOC=0)

`detect.py` explicitly sets `-sABORTING_MALLOC=0`, ensuring that `malloc()` returns
NULL on failure instead of calling `abort()`. This is critical for a game engine that
may hit memory pressure on memory-constrained mobile browsers.

The comment notes that Emscripten already sets this when `ALLOW_MEMORY_GROWTH=1`, but
making it explicit prevents regressions if defaults change. This is defensive and correct.

### 3.2 Memory Growth

`-sALLOW_MEMORY_GROWTH=1` is set, allowing the WASM linear memory to grow at runtime.
Combined with `ABORTING_MALLOC=0`, this provides graceful degradation: memory grows on
demand, and if growth is exhausted, allocations fail gracefully rather than aborting.

For threaded builds, `-sWASM_MEM_MAX=2048MB` caps growth to prevent runaway allocation.

### 3.3 Stack Size

The WASM stack is set to 5120 KiB (5 MB), and pthread default stacks to 2048 KiB
(2 MB). These are inherited from upstream Godot's web defaults and are adequate.

### 3.4 Shadow Buffer Pattern

The WebGPU driver uses shadow CPU buffers (`shadow_map`) for all upload/download
paths. This avoids holding mapped GPU buffers across frames and provides a stable
memory region for the engine to read from between frames. The shadow buffers are
`memalloc`'d and `memfree`'d with proper null checks in all free/callback paths.

---

## 4. Threading Model and Nothreads Support

### 4.1 Build Variants

The web platform supports two build variants:
- **Threaded** (`threads=yes`): Uses SharedArrayBuffer, pthreads via
  `-sUSE_PTHREADS=1`, requires COOP/COEP headers
- **Non-threaded** (`threads=no`): Single-threaded, no SharedArrayBuffer requirement,
  broader browser compatibility

WebGPU does not require threading — the driver operates entirely on the main thread
with async callbacks. This means WebGPU works in both variants.

### 4.2 WorkerThreadPool Fix (31c01726fa)

**Bug:** On nothreads builds, `WorkerThreadPool::wait_for_group_task_completion()`
had a code path that erased the group from the HashMap but never freed the Group
allocation or incremented the finished counter. This leaked memory proportional to
the number of completed group tasks over the session lifetime.

**Fix:** Added an `#else` block for the non-threaded path that:
1. Validates the group ID
2. Increments the finished counter
3. Frees the Group allocation via `group_allocator.free()` when all users are done
4. Erases from the HashMap

**Assessment:** The fix correctly mirrors the threaded path's cleanup logic while
accounting for the fact that on nothreads builds, tasks execute synchronously in
`_post_tasks()` so no semaphore wait is needed. The `max_users = tasks_used + 1`
accounting matches the threaded path exactly.

### 4.3 Assertions Configuration Fix (8e1bcb9aff)

**Bug:** The original code had inverted logic:
```python
if env.debug_features:
    env.Append(LINKFLAGS=["--profiling-funcs"])
else:
    env["use_assertions"] = True
```
This meant release templates (which lack `debug_features`) always got assertions
enabled, adding ~2 MB to binary size and reducing performance.

**Fix:** Changed `use_assertions` from a boolean to a multi-value option
(`auto`, `no`, `yes`, `extra`):
- `auto`: Assertions on for debug, off for release
- `yes`: `-sASSERTIONS=1`
- `extra`: `-sASSERTIONS=2` (more expensive checks)
- `no`: No assertions

The `--profiling-funcs` flag is now gated on `debug_symbols` (retaining function
names for backtraces when symbols are requested), decoupled from assertions.

**Assessment:** This is a clean fix that restores the expected behavior and provides
user control. The `auto` default matches what most users expect.

### 4.4 WebGPU Driver Behavior: Threaded vs Non-Threaded

The WebGPU driver itself is single-queue/single-thread by design:
- Semaphores are no-ops (single queue)
- Fences use async `wgpuQueueOnSubmittedWorkDone` with force-signal fallback
- All rendering happens on the main thread
- `wgpuInstanceProcessEvents` is the only mechanism for delivering async callbacks

In a threaded build, the rendering thread is still the main Emscripten thread (since
WebGPU JS APIs are only accessible from the main thread). The `proxy_to_pthread`
option moves game logic off the main thread, but rendering stays on main. This is
compatible with the current architecture.

---

## 5. Build System Changes

### 5.1 WebGPU Flag and emdawnwebgpu Port

```python
if env["webgpu"]:
    env.AppendUnique(CPPDEFINES=["WEBGPU_ENABLED", "RD_ENABLED"])
    env.Append(CCFLAGS=["--use-port=emdawnwebgpu"])
    env.Append(LINKFLAGS=["--use-port=emdawnwebgpu"])
```

The `--use-port=emdawnwebgpu` flag is the modern Emscripten 4.0.10+ approach to
WebGPU (replacing the removed `-sUSE_WEBGPU=1`). It provides:
- WebGPU C headers (`<webgpu/webgpu.h>`)
- JS glue that bridges C API calls to browser WebGPU
- The `WebGPU.importJsDevice()` helper for importing JS-created devices

The `"supported": ["webgpu"]` in `get_flags()` declares that the web platform
supports the `webgpu` SCons option.

### 5.2 Naga WASM Bundling

The `emscripten_helpers.py` modification includes `naga_wasm_bg.wasm` in the
export template zip when WebGPU is enabled:
```python
if env.get("webgpu", False):
    in_files.append("#drivers/webgpu/naga-converter/prebuilt/naga_wasm_bg.wasm")
    out_files.append(zip_dir.File("naga_wasm_bg.wasm"))
```

This ensures the SPIR-V to WGSL converter is distributed alongside the engine.

### 5.3 Export Plugin Changes

The export plugin (`export_plugin.cpp`):
1. Injects `renderingDriver` into the JSON config based on the rendering method
2. Maps `forward_plus` and `mobile` to `"webgpu"`, others to `"opengl3"`
3. Adds a non-fatal warning when WebGPU rendering is selected

---

## 6. Issues and Concerns Found

### 6.1 Hardcoded Canvas Selector

In `rendering_context_driver_webgpu.cpp:119`:
```cpp
const char *canvas_selector = "#canvas";
if (p_platform_data != nullptr) {
    // TODO: Extract canvas selector from platform data if provided.
}
```
The canvas selector is hardcoded to `"#canvas"`. The TODO indicates this is
a known limitation. Custom HTML shells that use a different canvas ID will not
work with WebGPU without modifying this code.

**Severity:** Low (standard Godot web exports always use `#canvas`)
**Recommendation:** Wire up `p_platform_data` to allow custom canvas selectors.

### 6.2 No Device Loss Recovery

Device loss is logged but not recovered from. On mobile browsers, GPU process
crashes can cause device loss. The engine will continue running but rendering will
stop. There is no mechanism to:
- Notify the user via UI
- Attempt device re-creation
- Fall back to a software renderer or error screen

**Severity:** Medium (affects reliability on mobile/unstable GPUs)
**Recommendation:** Implement a device-loss callback that either halts with
a user-visible error or attempts recovery.

### 6.3 Duplicate Uncaptured Error Listeners

The `uncapturederror` listener is attached in three places:
1. `engine.js::requestWebGPUDevice()` (line 482)
2. `rendering_device_driver_webgpu.cpp::initialize()` (lines 482-488, via EM_ASM)
3. `rendering_device_driver_webgpu.cpp` WEBGPU_VERBOSE block (lines 514-518)

The C++ side checks `_uncapturedPatched` to avoid duplicates between items 2 and 3,
but item 1 (the JS engine side) always adds its own listener. This means in production,
every uncaptured error will be logged twice: once from the JS listener and once from
the C++ EM_ASM listener.

**Severity:** Low (cosmetic: duplicate console.error lines)
**Recommendation:** Remove one of the listeners, or have the C++ side check
if the JS engine already attached one. The simplest fix: skip the EM_ASM
uncaptured listener and rely solely on the one in engine.js.

### 6.4 Naga Module Import Names Are Fragile

The naga WASM module's import namespace is `'./naga_converter_bg.js'` with
specific function names including hash suffixes (e.g., `__wbg_Error_83742b46f01ce22d`).
These are generated by wasm-bindgen and will change if the naga converter is rebuilt
with a different version of wasm-bindgen.

**Severity:** Low (the prebuilt wasm binary is pinned in the repo)
**Recommendation:** Document the wasm-bindgen version used and ensure the prebuilt
binary and the JS glue stay in sync during updates.

### 6.5 Potential Leak in Readback Cache on Cancelled Entries

In `buffer_free()`, when a readback entry has `!map_complete`, the code sets
`cancelled = true` and erases from `_readback_cache`. The entry pointer remains
valid (heap-allocated) and the callback will clean it up. However, if for some
reason the callback never fires (e.g., device loss cancels all pending operations
without firing callbacks), the entry will leak.

**Severity:** Low (device loss is a terminal condition anyway)
**Recommendation:** In the destructor's readback cache cleanup loop, also check for
and free any entries that were left in the cancelled state but whose callbacks
may never have fired.

### 6.6 Force-Signal in fence_wait May Hide GPU Errors

The force-signal logic in `fence_wait()`:
```cpp
if (!fence->signaled) {
    fence->signaled = true;
}
```
assumes the GPU work completed successfully. If the GPU actually crashed or a
command buffer failed validation, this would mask the error and the engine would
proceed with potentially corrupted state.

**Severity:** Low-Medium (GPU errors in production are rare, and device-lost would
trigger separately)
**Recommendation:** Add a comment explaining this tradeoff. Consider checking
device status after force-signal.

### 6.7 SPIR-V Cache Key Collision Risk

The cache uses a 64-bit key from two MurmurHash3 passes:
```cpp
uint32_t hash_lo = hash_murmur3_buffer(p_spv_ptr, p_spv_size);
uint32_t hash_hi = hash_murmur3_buffer(p_spv_ptr, p_spv_size, 0x9E3779B9);
```
With ~1000 shaders this is astronomically unlikely to collide (birthday bound is
~4 billion entries for a 64-bit space). This is fine.

### 6.8 No Error Propagation from loadNagaSpirvToWgsl

If the naga WASM fails to load (404, network error), `startGame` will reject the
returned promise, but the error message may not clearly indicate to users that the
shader converter is missing. The `fetch` rejection wraps the HTTP status text.

**Severity:** Low (clear enough error for developers, not user-facing)

---

## 7. Cross-Origin Isolation Considerations

WebGPU does NOT require SharedArrayBuffer or cross-origin isolation. This means:
- Non-threaded WebGPU builds work without COOP/COEP headers
- WebGPU is compatible with broader hosting environments than threaded builds
- The existing `ensureCrossOriginIsolationHeaders` service worker in the export
  plugin is only needed for threaded builds

This is an advantage for deployment: WebGPU games can be hosted on static file
hosts (GitHub Pages, etc.) without special server configuration, as long as
threading is not needed.

---

## 8. Summary of Findings

### Strengths

1. **Well-designed initialization pipeline**: The JS engine properly sequences
   WebGPU device creation before WASM module start, with parallel loading of both
   the GPU device and the naga converter for minimum latency.

2. **Robust UAF prevention**: The pending/freed flag pattern is consistently applied
   across all four async callback sites. The heap-allocated ReadbackEntry fix
   correctly addresses the HashMap invalidation issue.

3. **Proper feature negotiation**: The device request maximizes adapter capabilities
   without ever requesting unsupported features. Optional features are gated by
   adapter support.

4. **Correct build system integration**: The emdawnwebgpu port is properly configured
   for both compilation and linking. The naga WASM is bundled in export templates.

5. **Good separation of concerns**: JS handles device creation (which requires async),
   C++ handles rendering (which needs synchronous access to the device handle).

### Areas for Improvement

1. **Canvas selector flexibility** (hardcoded `#canvas`)
2. **Device loss recovery** (log-only, no user notification)
3. **Duplicate error listeners** (minor, cosmetic)
4. **Force-signal may mask GPU errors** (acceptable tradeoff, needs documentation)

### Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| UAF crash on WASM | Low (mitigated) | High | pending/freed pattern |
| OOM abort | Low (mitigated) | High | ABORTING_MALLOC=0 + ALLOW_MEMORY_GROWTH |
| Device loss unhandled | Medium | Medium | Need recovery path |
| Naga wasm-bindgen drift | Low | Medium | Pinned prebuilt binary |
| Thread pool memory leak | Low (fixed) | Low | Nothreads cleanup |

### Recommendations

1. **Priority 1**: Add device-loss notification to the user (at minimum, a visible
   error overlay in the HTML shell).

2. **Priority 2**: Remove the duplicate uncaptured-error listener from either the
   JS or C++ side to avoid double-logging.

3. **Priority 3**: Wire up the canvas selector from platform data for custom HTML
   shell support.

4. **Documentation**: The `ASYNC_WEBGPU.md` notes are excellent internal
   documentation of the async readback challenge and its resolution. Consider
   extracting key architectural decisions into a user-facing section of the README.

---

## Appendix: File Index

| File | Purpose |
|------|---------|
| `platform/web/detect.py` | Build system: assertions fix, ABORTING_MALLOC, WebGPU port |
| `platform/web/js/engine/engine.js` | JS glue: device request, naga loader, startGame flow |
| `platform/web/js/engine/config.js` | Config: renderingDriver, preinitializedWebGPUDevice |
| `platform/web/display_server_web.cpp` | DisplayServer: WebGPU init, window creation, resize |
| `platform/web/display_server_web.h` | Header: rendering_context + rendering_device members |
| `platform/web/emscripten_helpers.py` | Export template: naga WASM bundling |
| `platform/web/export/export_plugin.cpp` | Export: renderingDriver config injection |
| `drivers/webgpu/rendering_context_driver_webgpu.cpp` | Context: device import, surface creation |
| `drivers/webgpu/rendering_context_driver_webgpu.h` | Context: WGPUInstance/Device/Queue ownership |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | Driver: UAF hardening, fence, readback cache |
| `drivers/webgpu/webgpu_objects.h` | Object structs: pending/freed flags |
| `core/object/worker_thread_pool.cpp` | Nothreads fix: group cleanup |
