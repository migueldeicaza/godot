# Push Constant Ring Buffer — Design & Known Issues

## Background: Why We Have a Ring Buffer

WebGPU has **no native push constants**. Vulkan, Metal, and D3D12 all have push constants (or root constants) as a first-class feature — data is written directly into the command buffer stream, captured per-draw, immutable once recorded. Zero shared state, zero aliasing risk.

The W3C WebGPU working group deliberately omitted push constants from the spec (simpler binding model). So our WebGPU driver **emulates** them using:
- A persistent storage buffer ("the ring") with `WGPUBufferUsage_Storage`
- Dynamic offsets on bind groups to index into it per-draw
- `wgpuQueueWriteBuffer` to upload the CPU-side shadow data before submit

Each "push constant write" copies data into the next slot of the ring buffer and rebinds the bind group with a new dynamic offset.

## Current Implementation

```
PUSH_CONSTANT_RING_SIZE = 512 * 1024  (512KB)
PUSH_CONSTANT_SLOT_ALIGNMENT = 256    (bytes per slot)
Max slots = 2048
```

- `push_constant_shadow[]` — CPU-side mirror of the ring
- `push_constant_ring_offset` — current write position
- `push_constant_shadow_dirty_start/end` — tracks which bytes need flushing

### Write path (`_flush_push_constants`):
1. If `ring_offset + aligned_size > RING_SIZE` → end pass, flush, submit, reset offset to 0, restart pass
2. `memcpy` push constant data into shadow buffer at current offset
3. Expand dirty range
4. `wgpuRenderPassEncoderSetBindGroup` with dynamic offset pointing to this slot
5. Save offset to `last_flushed_pc_offset` (used by material rebind when flush is skipped)
5. Advance `ring_offset`

### Submit path (`command_queue_execute_and_present`):
1. Flush final dirty region via `wgpuQueueWriteBuffer`
2. `wgpuQueueSubmit`

## The Bug: Ring Wrap Data Corruption

### Symptom
Color flickering on skeletons when draw count causes >1024 push constant writes per frame (e.g., 55×55 = 3025 skeletons triggers ~2046 PC writes/frame).

### Root Cause
When the ring wraps, new writes overwrite offsets that earlier draws in the **same un-submitted command buffer** still reference. Since `wgpuQueueWriteBuffer` writes are all resolved before command buffer execution, the GPU sees only the **final** value at each offset — not the value that was there when the draw was recorded.

Example with 2 wraps:
```
Write1: wgpuQueueWriteBuffer(offsets 0..262143, first_wrap_data)    ← at wrap point
Write2: wgpuQueueWriteBuffer(offsets 0..262143, second_wrap_data)   ← at second wrap
Write3: wgpuQueueWriteBuffer(offsets 0..X, remainder_data)          ← at submit
Execute: CommandBuffer

GPU sees: Write1 → Write2 → Write3 → Execute
Offsets 0..X have remainder_data, offsets X+1..end have second_wrap_data.
Draws from the first wrap that use offsets 0..X read WRONG data.
```

### Why It Flickers (Not Stable Wrong Colors)
Draw sort order may vary slightly frame-to-frame (front-to-back with animated bones changing bounding boxes), causing different offsets to be corrupted each frame.

## Correct Ring Buffer Design for GPU

### Core Invariant
**Never overwrite data referenced by an un-submitted command buffer.**

### What "Frees" Ring Space
`wgpuQueueSubmit`. Once submitted, the queue-ordered writes preceding it are locked in. New writes to the same offsets only affect subsequently submitted command buffers.

### Overflow Strategy: Flush-and-Reset

When the ring is full:
1. End current render/compute pass
2. Flush dirty region via `wgpuQueueWriteBuffer`
3. `wgpuQueueSubmit` (non-blocking — just queues work, doesn't wait for GPU)
4. Reset `ring_offset = 0` (safe: submitted command buffer's data is frozen by queue ordering)
5. Create new encoder, begin new render pass with `LoadOp::Load`
6. Rebind pipeline, bind groups, vertex/index buffers, viewport, scissor
7. Continue drawing

### Why This Is NOT "Blocking"
- CPU ring buffer blocking: wait for the *consumer* (GPU) to process data and free space. Thread idles.
- Flush-and-reset: the *producer* (CPU) does quick bookkeeping, then continues immediately. GPU runs in parallel. No wait.

The cost is purely the overhead of ending/restarting the render pass (~0.1ms on Apple Silicon for tile store+load at 1280×720). Not a GPU stall.

### Comparison With Other APIs
| API | Push Constants | Ring Buffer Needed? |
|-----|---------------|-------------------|
| Vulkan | `vkCmdPushConstants` (native, inline in cmd buffer) | No |
| Metal | `setBytes:length:atIndex:` (native, inline in encoder) | No |
| D3D12 | Root constants (native, inline in cmd list) | No |
| WebGPU | Not in spec — must emulate | **Yes** |

## Implemented Fix

**Two-layer approach:**

1. **Increase ring size to 512KB** (2048 slots)
   - Zero runtime cost (same code path when no overflow)
   - Eliminates wraps for most real scenes
   - Overflow handler kicks in for extreme cases (>2048 PC writes/frame)

2. **Mid-frame submit on overflow** (safety net)
   - When ring would overflow, end pass → submit → reset → restart
   - Guarantees correctness for arbitrarily large scenes
   - ~0.1–0.15ms cost per split (negligible on desktop hardware)
   - Handles both render and compute passes

## Related: Timestamp Query Readback Bug (Fixed)

A separate issue was also present: the GPU timestamp query readback buffer (2048 bytes) would get stuck in "mapping pending" state under load, causing `"buffer used in submit while mapped"` validation errors. This was triggered by the benchmark profiler calling `viewport_set_measure_render_time(true)`.

**Fix:** `timestamp_supported = false` in the WebGPU driver (line 697). emdawnwebgpu doesn't reliably cancel pending maps via `wgpuBufferUnmap`. This only affects GPU-side timing profiling (reports "gpu=N/A"); CPU render time and FPS measurement still work. Normal production games without explicit profiling calls are unaffected.

## Separate Bug: Material Color Flickering (Fixed)

### Symptom
Some skeletons intermittently display the wrong color (switching between the 5 material colors frame-to-frame). Not all skeletons affected. Flickering is non-deterministic due to animated bounding boxes changing front-to-back sort order each frame.

### Root Cause
The `API_TRAIT_FIRST_INSTANCE_INDEX` optimization in Forward Mobile skips push constant writes when consecutive draws only differ by element index. When push constants are skipped, `_flush_push_constants` never fires. But when the material changes between draws, `command_bind_render_uniform_sets` rebinds slot 3 (the merged material+PC group) with a hardcoded `PC_offset=0` — expecting `_flush_push_constants` to fix it up. Since `_flush_push_constants` is skipped, the draw executes with `PC_offset=0`, reading stale push constant data from ring offset 0 instead of the correct offset.

### Fix
Track `last_flushed_pc_offset` in `WGCommandBuffer`. Set it in `_flush_push_constants` before advancing the ring. Use it instead of hardcoded 0 when rebinding the merged PC group in both render and compute paths. This ensures the bind group always points to valid push constant data regardless of whether `_flush_push_constants` fires afterward.

### Diagnostic that confirmed
Disabling `API_TRAIT_FIRST_INSTANCE_INDEX` (return 0) eliminated flickering — this forced every draw to write push constants, which always called `_flush_push_constants` and correctly rebound the merged group.

## Status (2026-05-10)

- [x] Timestamp readback bug fixed (validation errors eliminated)
- [x] Push constant ring overflow fix (flush-and-reset on overflow)
- [x] Ring buffer size increase (256KB → 512KB)
- [x] Material color flickering fix (`last_flushed_pc_offset`)
- Benchmark: 3025 skeletons @ ~48 FPS on M3 Ultra, no validation errors, no color flickering
