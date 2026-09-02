# Command Buffering: Eliminating WASM→JS Trampoline Overhead

## Overview

**Goal:** Eliminate ~8,700 per-frame WASM→JS boundary crossings by recording render pass commands into a flat buffer in WASM linear memory and replaying them via a single JS function call.

**Target:** Close the Web/Native gap from 1.44x toward ~1.2x on desktop, and save ~2-4ms/frame on mobile (potentially the difference between 30fps and 60fps).

**Timeline:** Ship with godot-webgpu release (May 9, 2026). Estimated 2-3 days of focused work.

---

## Why This Works

### The Problem

Every `wgpu*` call from WASM crosses the WASM→JS boundary. Each crossing costs ~0.3µs on Mac Studio M3 Ultra. With ~8,700 render pass calls per frame, that's ~2.6ms — 11% of frame time on desktop, and **20-45% on mobile**.

### Where the Cost Lives

Research into Chromium's architecture (May 4, 2026) revealed that individual `wgpu*` calls do NOT each trigger a Mojo IPC message to the GPU process. Dawn's wire client already batches commands into a shared-memory ring buffer. The per-call overhead is dominated by the WASM→JS trampoline:

| Layer | Cost per Call | % of Total |
|-------|-------------|------------|
| **WASM→JS trampoline** | ~90-150ns | **30-50%** |
| JS→GPU (Dawn Wire + validation) | ~150-210ns | 50-70% |

Command buffering eliminates the first row entirely: 8,700 WASM→JS crossings become 1.

### What the Trampoline Actually Does

When WASM calls a JS function, V8 must:
1. Save WASM registers (different calling convention than JS)
2. Convert WASM i32/f32 values to JS Numbers
3. Set up JS execution context
4. Jump to JS function
5. On return: convert back, restore registers

This is ~30-50ns of irreducible overhead per crossing (baked into V8), plus ~50-100ns for the JS binding function (handle lookup, argument validation, WASM memory reads). Command buffering eliminates ALL of it by making the calls from pure JS instead.

### Current vs Proposed Call Path

```
Current (8,700 crossings):
  for each draw:
    WASM → [trampoline ~100ns] → JS binding → WebGPU API → Dawn Wire
    WASM → [trampoline ~100ns] → JS binding → WebGPU API → Dawn Wire
    WASM → [trampoline ~100ns] → JS binding → WebGPU API → Dawn Wire
    ...

Proposed (1 crossing):
  WASM: write 8,700 commands into buffer (~2ns each = ~17µs total)
  WASM → [trampoline ~100ns] → JS replay function:
    for each command:
      pure JS → WebGPU API → Dawn Wire    (no trampoline)
      pure JS → WebGPU API → Dawn Wire    (no trampoline)
      ...
```

The 8,700 WebGPU API calls still happen — but from pure JavaScript, skipping the WASM→JS trampoline on each one.

---

## Estimated Impact

### Desktop (Mac Studio M3 Ultra)

| Metric | Current | After Command Buffering |
|--------|---------|------------------------|
| WASM→JS crossings | ~8,700 | 1 |
| Trampoline overhead | ~0.8-1.3ms | ~0.0001ms |
| Remaining JS→GPU overhead | ~1.3-1.8ms | ~1.3-1.8ms (unchanged) |
| Total IPC overhead | ~2.6ms | ~1.3-1.8ms |
| **Savings** | — | **~0.8-1.3ms/frame** |

### Mobile (where it really matters)

| Device | Per-call cost | IPC overhead (8,700 calls) | Trampoline portion saved |
|--------|-------------|---------------------------|-------------------------|
| Mac Studio M3 Ultra | ~0.3µs | 2.6ms | ~0.8-1.3ms |
| MacBook Air M2 | ~0.4µs | 3.5ms | ~1.1-1.7ms |
| Mid-range phone | ~0.8µs | 7.0ms | ~2.1-3.5ms |
| Budget phone / Chromebook | ~1.5µs | 13.0ms | ~3.9-6.5ms |

On a budget phone targeting 30fps (33ms budget), saving ~4-6ms is a **12-20% frame time reduction** from this single optimization.

### As percentage of frame budget on a mid-range phone

| Scenario | Draws | IPC crossings | Before | After | Saved |
|----------|------:|-------------:|-------:|------:|------:|
| Simple 3D (60fps target) | 500 | ~1,500 | 7% | 4% | 3% |
| Moderate 3D (60fps target) | 2,000 | ~5,000 | **24%** | **14%** | **10%** |
| Complex 3D (30fps target) | 4,000 | ~9,000 | **22%** | **13%** | **9%** |

---

## Implementation

### Scope

**Render pass commands only** — the hot path where ~90% of crossings live. These are the ~10 command types that happen inside a render pass:

| Command | Current function | Args |
|---------|-----------------|------|
| SetPipeline | `wgpuRenderPassEncoderSetPipeline` | pass, pipeline |
| SetBindGroup | `wgpuRenderPassEncoderSetBindGroup` | pass, group_idx, bg, offset_count, offsets |
| SetVertexBuffer | `wgpuRenderPassEncoderSetVertexBuffer` | pass, slot, buffer, offset, size |
| SetIndexBuffer | `wgpuRenderPassEncoderSetIndexBuffer` | pass, buffer, format, offset, size |
| DrawIndexed | `wgpuRenderPassEncoderDrawIndexed` | pass, index_count, instance_count, first_index, base_vertex, first_instance |
| DrawIndexedIndirect | `wgpuRenderPassEncoderDrawIndexedIndirect` | pass, buffer, offset |
| SetViewport | `wgpuRenderPassEncoderSetViewport` | pass, x, y, w, h, min_depth, max_depth |
| SetScissor | `wgpuRenderPassEncoderSetScissorRect` | pass, x, y, w, h |
| SetStencilRef | `wgpuRenderPassEncoderSetStencilReference` | pass, ref |
| SetBlendConstant | `wgpuRenderPassEncoderSetBlendConstant` | pass, color |

**NOT buffered** (low frequency, not the bottleneck):
- Resource creation (createBuffer, createTexture, createBindGroup) — returns handles needed immediately
- Buffer writes (writeBuffer) — scattered across frame, has data payloads
- Compute passes — only 5-15 dispatches/frame
- Render pass begin/end — only ~9/frame

### Binary Command Format

Each command is a packed struct in WASM linear memory. Fixed-size entries for simplicity (pad smaller commands):

```cpp
// In rendering_device_driver_webgpu.h

enum CommandType : uint32_t {
    CMD_SET_PIPELINE = 0,
    CMD_SET_BIND_GROUP = 1,
    CMD_SET_VERTEX_BUFFER = 2,
    CMD_SET_INDEX_BUFFER = 3,
    CMD_DRAW_INDEXED = 4,
    CMD_DRAW_INDEXED_INDIRECT = 5,
    CMD_SET_VIEWPORT = 6,
    CMD_SET_SCISSOR = 7,
    CMD_SET_STENCIL_REF = 8,
    CMD_SET_BLEND_CONSTANT = 9,
};

// Fixed-size command entry — 32 bytes covers all command types
struct alignas(4) RenderPassCommand {
    uint32_t type;       // CommandType
    uint32_t arg0;       // handle or integer arg
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
    uint32_t arg4;
    uint32_t arg5;
    uint32_t arg6;       // padding for smaller commands
};
// sizeof(RenderPassCommand) == 32 bytes

// Command buffer: pre-allocated, reused each render pass
static constexpr uint32_t MAX_COMMANDS_PER_PASS = 16384;
RenderPassCommand cmd_buffer[MAX_COMMANDS_PER_PASS];  // 512KB
uint32_t cmd_count = 0;
```

32 bytes per command × 16,384 max = 512KB. Fits comfortably in WASM linear memory. Realistic frames use 1,000-10,000 commands.

### Recording (C++ side)

Instead of calling `wgpu*` directly, each render pass command function writes into the buffer:

```cpp
// Example: SetPipeline
void RenderingDeviceDriverWebGPU::command_bind_render_pipeline(CommandBufferID p_cmd_buf, PipelineID p_pipeline) {
    if (use_command_buffering && in_render_pass) {
        auto &cmd = cmd_buffer[cmd_count++];
        cmd.type = CMD_SET_PIPELINE;
        cmd.arg0 = (uint32_t)(uintptr_t)pipeline_to_handle(p_pipeline);  // JS-side handle ID
        return;
    }
    // Fallback: direct call (non-WebGPU backends, or if disabled)
    wgpuRenderPassEncoderSetPipeline(render_pass_encoder, ...);
}
```

### Replay (JS side)

Single function reads the buffer from WASM memory and issues all WebGPU calls:

```javascript
// Registered via EM_JS or --js-library
function replayRenderPassCommands(passHandle, bufferPtr, count) {
    const pass = getWebGPUObject(passHandle);  // handle → GPURenderPassEncoder
    const heap = HEAPU32;
    const CMD_SIZE_U32 = 8;  // 32 bytes / 4 bytes per u32

    for (let i = 0; i < count; i++) {
        const base = (bufferPtr >> 2) + i * CMD_SIZE_U32;
        const type = heap[base];

        switch (type) {
            case 0: // SET_PIPELINE
                pass.setPipeline(getWebGPUObject(heap[base + 1]));
                break;
            case 1: // SET_BIND_GROUP
                // arg0=group_idx, arg1=bg_handle, arg2=offset_count, arg3=first_offset
                const groupIdx = heap[base + 1];
                const bg = getWebGPUObject(heap[base + 2]);
                const offsetCount = heap[base + 3];
                if (offsetCount > 0) {
                    // Dynamic offsets stored inline in arg4+
                    const offsets = heap.subarray(base + 4, base + 4 + offsetCount);
                    pass.setBindGroup(groupIdx, bg, offsets);
                } else {
                    pass.setBindGroup(groupIdx, bg);
                }
                break;
            case 2: // SET_VERTEX_BUFFER
                pass.setVertexBuffer(heap[base + 1],
                    getWebGPUObject(heap[base + 2]),
                    heap[base + 3] + heap[base + 4] * 0x100000000,  // 64-bit offset
                    heap[base + 5] + heap[base + 6] * 0x100000000); // 64-bit size
                break;
            case 3: // SET_INDEX_BUFFER
                pass.setIndexBuffer(getWebGPUObject(heap[base + 1]),
                    heap[base + 2] ? 'uint32' : 'uint16',
                    heap[base + 3] + heap[base + 4] * 0x100000000,
                    heap[base + 5] + heap[base + 6] * 0x100000000);
                break;
            case 4: // DRAW_INDEXED
                pass.drawIndexed(heap[base + 1], heap[base + 2],
                    heap[base + 3], heap[base + 4], heap[base + 5]);
                break;
            case 5: // DRAW_INDEXED_INDIRECT
                pass.drawIndexedIndirect(getWebGPUObject(heap[base + 1]), heap[base + 2]);
                break;
            case 6: // SET_VIEWPORT
                // Floats stored as uint32 bit patterns — reinterpret
                const f32 = new Float32Array(heap.buffer, (base + 1) * 4, 6);
                pass.setViewport(f32[0], f32[1], f32[2], f32[3], f32[4], f32[5]);
                break;
            case 7: // SET_SCISSOR
                pass.setScissorRect(heap[base + 1], heap[base + 2],
                    heap[base + 3], heap[base + 4]);
                break;
            case 8: // SET_STENCIL_REF
                pass.setStencilReference(heap[base + 1]);
                break;
            case 9: // SET_BLEND_CONSTANT
                const c = new Float32Array(heap.buffer, (base + 1) * 4, 4);
                pass.setBlendConstant({ r: c[0], g: c[1], b: c[2], a: c[3] });
                break;
        }
    }
}
```

### Flush (C++ side, at render pass end)

```cpp
void RenderingDeviceDriverWebGPU::_flush_command_buffer() {
    if (cmd_count == 0) return;
    EM_ASM({
        replayRenderPassCommands($0, $1, $2);
    }, render_pass_handle, (uintptr_t)cmd_buffer, cmd_count);
    cmd_count = 0;
}
```

Called at `command_end_render_pass()`, just before `wgpuRenderPassEncoderEnd()`.

### Handle Mapping

**The key technical question:** The JS replay function needs `getWebGPUObject(handleId)` to translate WASM-side handle IDs to JS WebGPU objects. Options:

1. **Tap into emdawnwebgpu's internal handle table** (fastest)
   - emdawnwebgpu stores WebGPU objects internally — need to investigate the representation
   - If it uses a simple array indexed by integer ID, we can read it directly
   - If it uses a Map or more complex structure, we may need a wrapper

2. **Maintain a parallel JS array** (independent, predictable)
   - On each `wgpuDeviceCreate*` or `wgpuDeviceCreate*Async`, register the object in our array
   - `getWebGPUObject(id)` is just `objectTable[id]` — O(1) array lookup
   - Need to hook into object creation/destruction for lifetime management

3. **Pass JS object references through WASM** (least indirection)
   - Store the emdawnwebgpu handle ID directly in the command buffer
   - The replay function calls emdawnwebgpu's internal lookup — same path as direct calls but without the WASM→JS crossing per call

**Preferred approach:** Investigate emdawnwebgpu first (option 1). Fall back to option 2 if internals are opaque.

**Action item:** Read emdawnwebgpu source to understand handle representation before implementation.

### Files to Modify

| File | Change |
|------|--------|
| `drivers/webgpu/rendering_device_driver_webgpu.h` | `RenderPassCommand` struct, `cmd_buffer[]`, `cmd_count`, `use_command_buffering` flag |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | Record in each `command_*` function instead of direct wgpu call; `_flush_command_buffer()` at pass end |
| `servers/rendering/rendering_device_driver.h` | `API_TRAIT_COMMAND_STREAM_REPLAY` |
| New: JS library or EM_JS block | `replayRenderPassCommands()` function |

### Safety

**Gated by:** `API_TRAIT_COMMAND_STREAM_REPLAY` (new trait). Only WebGPU driver returns 1. Vulkan/Metal/D3D12 return 0. The command recording code paths are never entered on non-WebGPU backends.

**Instant rollback:** If any issue is found, set the trait to 0 → falls back to direct wgpu* calls. Zero risk to the release.

**Correctness guarantee:** The command buffer records the exact same commands in the exact same order as the direct call path. The JS replay issues the exact same WebGPU API calls. The only difference is WHERE the calls originate (JS loop vs WASM trampoline).

---

## Browser Compatibility

| Browser | WebGPU Backend | WASM→JS trampoline | Command buffering helps? |
|---------|---------------|-------------------|------------------------|
| Chrome | Dawn (Mojo IPC to GPU process) | Same V8 trampoline | **Yes** |
| Firefox | wgpu-native (Rust FFI) | Same SpiderMonkey trampoline | **Yes** (magnitude may differ) |
| Safari | WebKit WebGPU | Same JSC trampoline | **Yes** (magnitude may differ) |

Command buffering uses only standard WebGPU APIs from JavaScript. No browser-specific features. The WASM→JS trampoline cost exists on all engines (V8, SpiderMonkey, JSC) because it's a fundamental consequence of the WASM/JS calling convention difference.

---

## Testing Plan

1. **Visual regression:** Run all 10 demo scenes with command buffering enabled. Verify 0 GPU errors and identical rendering vs direct calls.
2. **PERF counter comparison:** Verify same draw/SetBG/PC/SetVB counts. Commands are identical — only the calling path changes.
3. **FPS benchmarks:**
   - scene_c (20k unique-material draws) — heavy draw count, isolates per-call overhead
   - scene_h (60k shared-material draws) — heavy batched draws
   - 3D platformer stress test (with input, 20s warmup, 15s measure) — real game workload
4. **Firefox:** Verify replay function works on wgpu backend (same JS WebGPU API).
5. **A/B comparison:** Toggle `API_TRAIT_COMMAND_STREAM_REPLAY` between 0 and 1, compare FPS on same scene.

---

## Timeline

| Day | Task |
|-----|------|
| **Day 1** | Investigate emdawnwebgpu handle mapping. Implement `RenderPassCommand` struct and command buffer. Modify `command_*` functions to record. |
| **Day 2** | Write JS replay function. Integrate flush at render pass end. First demo running end-to-end. |
| **Day 3** | Full regression testing (all 10 demos). Benchmarking before/after. Fix any issues. Firefox test. |

---

## Relationship to Future Optimizations

Command buffering is the foundation. The command buffer recorded in WASM memory can be consumed by different submission backends in the future:

| Submission backend | What it does | When |
|-------------------|-------------|------|
| **JS batch replay** | JS loop calls WebGPU APIs from buffer | **Release (May 9)** |
| Render bundles | JS records buffer into GPURenderBundleEncoder, caches for stable frames | Post-release |
| Custom binding optimization | Tune the JS replay inner loop (handle tables, TypedArray reuse) | Post-release, data-driven |
| Multi-draw-indirect | Extract draw params from buffer into GPU indirect buffer | When spec standardizes |

The recording side (C++ structs in WASM memory) stays the same. Only the JS consumption side changes.

---

## Implementation Results (May 4, 2026)

### What Was Implemented

All 12 render pass encoder command types are now recorded into a flat command buffer in WASM linear memory and replayed via a single `EM_JS` function call per render pass:

| Command Type | ID | Args (max) |
|---|---|---|
| SET_PIPELINE | 0 | 1 (pipeline handle) |
| SET_BIND_GROUP | 1 | 3 + up to 8 dynamic offsets |
| SET_VERTEX_BUFFER | 2 | 6 (slot, handle, offset64, size64) |
| SET_INDEX_BUFFER | 3 | 6 (handle, format, offset64, size64) |
| DRAW | 4 | 4 |
| DRAW_INDEXED | 5 | 5 (includes signed base_vertex) |
| DRAW_INDEXED_INDIRECT | 6 | 3 (handle, offset64) |
| DRAW_INDIRECT | 7 | 3 (handle, offset64) |
| SET_VIEWPORT | 8 | 6 (float bit patterns) |
| SET_SCISSOR | 9 | 4 |
| SET_STENCIL_REF | 10 | 1 |
| SET_BLEND_CONSTANT | 11 | 4 (float bit patterns) |

**Command struct:** 12 × uint32 = 48 bytes per command, max 16,384 commands per buffer (768KB).

**Auto-flush:** When the buffer nears capacity (16,368 commands), it auto-flushes to JS and continues recording. In practice, all render passes fit within a single buffer — the stress test maxes at ~5,000 commands per pass.

### Files Modified

| File | Change |
|---|---|
| `drivers/webgpu/rendering_device_driver_webgpu.h` | Added `RPCmdType` enum, `RPCmd` struct, command buffer array, `_rp_*` wrapper methods, `_rp_flush_cmd_buffer()`, `_rp_maybe_flush()`, perf counters |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | Added `EM_JS` replay function, 12 wrapper method implementations, replaced ~19 `wgpuRenderPassEncoder*` call sites, added init/flush at render pass boundaries |

No changes to `rendering_device_driver.h` (base class) or any other files. The optimization is fully self-contained within the WebGPU driver.

### IPC Crossing Reduction

Measured on 3D platformer stress test (Mac Studio M3 Ultra, Chrome):

| Metric | Before | After | Reduction |
|--------|--------|-------|-----------|
| Render pass WASM→JS crossings/frame | ~18,000-23,000 | **9** | **99.95%** |
| Total commands replayed via JS/frame | — | ~18,000-23,000 | (same work, just from JS) |
| Render passes/frame | 9 | 9 | unchanged |
| Draw calls/frame | ~6,000-7,200 | ~6,000-7,200 | unchanged |
| SetBindGroup/frame | ~800-900 | ~800-900 | unchanged |
| SetVertexBuffer/frame | ~5,600-8,100 | ~5,600-8,100 | unchanged |

Every render pass encoder command now flows through the command buffer. The C++ code still handles all state tracking, redundant call elimination, push constant ring management, and BGL compatibility — only the final `wgpuRenderPassEncoder*` calls are recorded instead of directly issued.

### FPS Benchmarks

**Test:** 3D platformer stress test (3,500 mesh instances, 150 skinned enemies, 30 shadow lights, 25 particle emitters, 600 transparent billboards). 20s warmup, 15s measure, with virtual input.

| Metric | Before (Optimization A+B) | After (+ Command Buffering) |
|--------|---------------------------|----------------------------|
| Steady FPS | 42.3 | 41.0 - 44.5 |
| Mean FPS | ~42 | 40.7 - 43.9 |
| Median frame time | ~18ms | 16.7 - 18.7ms |
| GPU errors | 0 | 0 |
| Visual correctness | ✓ | ✓ |

**Result: Performance neutral on Mac Studio M3 Ultra.** The WASM→JS trampoline overhead on M3 Ultra is ~30-50ns per call — fast enough that the JS replay loop overhead (~50-80ns per iteration for HEAPU32 reads + switch dispatch + API call) largely offsets the savings.

### Why Desktop Shows Neutral Results

On the M3 Ultra:
- **Trampoline saved:** ~18,000 crossings × ~40ns = **~0.72ms**
- **JS replay loop cost:** ~18,000 iterations × ~40ns = **~0.72ms**
- **Net:** ~0ms savings (within noise)

The trampoline and the replay loop have similar per-call costs on fast hardware because both involve JavaScript execution machinery. The savings come when the trampoline is disproportionately expensive relative to pure JS execution — which happens on slower CPUs (mobile, Chromebook).

### Android Mobile A/B Test (Samsung Galaxy, Chrome 147)

**Scene:** Scaled-down stress test (400 meshes, 20 enemies, 4 shadow lights, 4 particle emitters, 80 billboards). 1200-frame warmup for thermal steady state, 600-frame measurement.

| Metric | Buffering OFF | Buffering ON | Delta |
|--------|--------------|-------------|-------|
| Mean FPS | 58.0 | **59.3** | **+2.2%** |
| Median FPS | 60.0 | 60.0 | (vsync cap) |
| Mean frame time | 17.25ms | **16.86ms** | **-0.39ms** |
| P95 frame time | 21.59ms | **16.75ms** | **-22%** |
| PERF steady fps | 56-57 | 59-60 | +3-4 fps |
| WASM→JS crossings/f | ~1,200 direct | 10 | -99.2% |

**Key observations:**
- Both variants hit the 60fps vsync cap, partially masking the throughput difference
- P95 improvement is significant: **21.59ms → 16.75ms** — fewer stutter spikes with buffering enabled
- PERF steady-state fps shows 59-60 vs 56-57, suggesting a small but real throughput lift
- This scene has only ~1,200 commands/frame — a light workload. The M3 Ultra stress test has ~18,000-23,000 commands/frame but would be unplayable on this phone

**Note:** The full M3 Ultra stress test (3,500 meshes, 150 enemies, 30 shadow lights) drops to ~11fps on this Samsung phone — well below playable. A scene complex enough to generate >5,000 commands/frame would also be too heavy for this phone to run at playable framerates. See "Practical Impact Assessment" below.

### Practical Impact Assessment

The original estimates assumed 18,000 commands/frame on mobile hardware. In practice, scenes must be scaled down for mobile, which also reduces the command count:

| Device | Playable scene complexity | Commands/frame | Net saved/call | Net saved/frame |
|--------|--------------------------|---------------|---------------|-----------------|
| Mac Studio M3 Ultra | Full stress (3,500 meshes) | ~18,000 | ~0ns | ~0ms |
| Samsung Galaxy (mid-range) | Scaled-down (400 meshes) | ~1,200 | ~15ns | **~0.02ms** |
| Budget phone (hypothetical) | Very light (~200 meshes) | ~600 | ~90ns | **~0.05ms** |

The savings are negligible for scenes that are actually playable on each device class. The per-call savings on weaker hardware are real, but the scenes those devices can run don't have enough commands to accumulate meaningful frame time reduction.

### Full Verification (May 4, 2026)

All demos and benchmark scenes re-exported with the command-buffered engine and tested in Chrome. **0 GPU errors across all 18 scenes.**

| # | Scene | Type | gpuErrors | allErrors | Result |
|---|-------|------|-----------|-----------|--------|
| 1 | 2d_particles | demo | 0 | 0 | PASS |
| 2 | 2d_platformer | demo | 0 | 0 | PASS |
| 3 | 2d_sprite_shaders | demo | 0 | 0 | PASS |
| 4 | 3d_lights_and_shadows | demo | 0 | 2* | PASS |
| 5 | 3d_particles | demo | 0 | 0 | PASS |
| 6 | 3d_platformer | demo | 0 | 0 | PASS |
| 7 | compute_heightmap | demo | 0 | 0 | PASS |
| 8 | compute_texture | demo | 0 | 0 | PASS |
| 9 | gui_control_gallery | demo | 0 | 0 | PASS |
| 10 | scene_e | demo | 0 | 0 | PASS |
| 11 | viewport_gui_in_3d | demo | 0 | 0 | PASS |
| 12 | shiny_gen | demo | 0 | 10* | PASS |
| 13 | scene_a (sprites) | benchmark | 0 | 0 | PASS |
| 14 | scene_b (pbr) | benchmark | 0 | 0 | PASS |
| 15 | scene_c (instances) | benchmark | 0 | 0 | PASS |
| 16 | scene_d (particles) | benchmark | 0 | 0 | PASS |
| 17 | scene_f (postfx) | benchmark | 0 | 0 | PASS |
| 18 | scene_h (batching) | benchmark | 0 | 0 | PASS |

\* Non-zero `allErrors` are pre-existing app-level warnings unrelated to command buffering:
- **3d_lights_and_shadows:** Sky update mode warning (Godot engine warning about real-time sky reflections)
- **shiny_gen:** Asset manager warnings (missing game state files), shader varyings limit (17 used, max 16)

### Safety and Rollback

- The optimization is always active. To disable: set `rp_cmd_active = false` in `command_begin_render_pass`.
- All existing C++ state tracking (redundant call elimination, push constant ring, BGL rebinding) is unchanged.
- The JS replay issues identical WebGPU API calls in identical order — the only difference is WHERE the calls originate (JS loop vs WASM trampoline).
- 0 GPU errors across all 18 demo and benchmark scenes confirms correctness.

---

## Work Summary

| Step | Duration | Notes |
|------|----------|-------|
| Read and understand codebase | 30min | Analyzed all 19 wgpuRenderPassEncoder* call sites, push constant flow, bind group tracking |
| Design command format | 15min | 12 command types, 48-byte fixed-size struct, 16K buffer with auto-flush |
| Implement header changes | 10min | Enum, struct, buffer array, method declarations, perf counters |
| Implement EM_JS replay function | 20min | 12-case switch statement, handle lookup via WebGPU.Internals.jsObjects |
| Implement 12 wrapper methods | 20min | Each method: if(active) → record, else → direct wgpu* call |
| Replace 19 call sites | 15min | Mechanical rename: wgpuRenderPassEncoder* → _rp_* |
| Add render pass boundary hooks | 10min | begin → activate+reset, end → flush+deactivate, next_subpass → flush+reset |
| Fix buffer overflow | 15min | First test hit 16K limit, added auto-flush via _rp_maybe_flush() |
| Build + test + benchmark | 30min | 3 benchmark runs, perf counter verification |
| **Total** | **~2.5 hours** | End-to-end from first code read to verified benchmark |

---

## Sources

- **Chromium GPU Command Buffer:** chromium.org/developers/design-documents/gpu-command-buffer — "The client can write commands very quickly with little or no communication with the service"
- **Dawn Wire architecture:** dawn.googlesource.com/dawn/+/HEAD/docs/dawn/overview.md — "dawn_wire is meant to do as little state-tracking as possible so that the client can be lean"
- **WASM→JS crossing cost:** web.dev "WebAssembly performance patterns" — ~100ns per non-trivial crossing
- **Figma:** figma.com/blog/figma-rendering-powered-by-webgpu — found Emscripten WebGPU bindings insufficient, wrote custom bindings
- **juj/wasm_webgpu:** github.com/juj/wasm_webgpu — alternative bindings "manually tuned for absolutely best runtime speed"
- **Toji render bundles:** toji.dev/webgpu-best-practices/render-bundles.html — 2-5x speedup with 40k objects
- **WebGPU dispatch overhead (arxiv 2604.02344):** 24-71µs per full dispatch cycle depending on backend
