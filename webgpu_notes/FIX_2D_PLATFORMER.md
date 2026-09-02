# Fix 2D Platformer — Camera Not Following Player (WebGPU)

## Status
**FIXED** — commit `bbee7439d6` on branch `webgpu_pre_phase7`.

---

## Symptom

- **WebGPU (before fix)**: Game starts showing the middle/upper-right of the scene. Camera does not move when the player moves.
- **WebGL (ground truth)**: Game correctly starts centered on the player in the bottom-left of the scene. Camera follows the player.

---

## Root Cause

**File**: `drivers/webgpu/rendering_device_driver_webgpu.cpp` — `command_copy_buffer()`

The bug was in how `canvas_state_buffer` (a uniform buffer holding `canvas_transform`, `screen_transform`, etc.) was updated each frame.

Godot renders multiple canvases per frame. For the 2D platformer there are 4:
| Order | Layer | `canvas_transform` origin |
|-------|-------|--------------------------|
| 1 | -100 | (0, 0) — parallax/background |
| 2 | 0 | **(336.5, -260) — game world / camera offset** |
| 3 | 0 | (0, 0) — UI |
| 4 | 100 | (0, 0) — top UI |

For each canvas, `buffer_update()` is called on the **shared** `canvas_state_buffer`, which records a copy from a CPU-side staging buffer to the UBO in the draw graph.

In `command_copy_buffer()`, when the source staging buffer had a CPU shadow map (which it always does in the WebGPU driver, since async mapping means GPU buffers are never directly mapped), the code wrote directly to the **destination** (UBO) via `wgpuQueueWriteBuffer`:

```cpp
// OLD — WRONG
wgpuQueueWriteBuffer(queue, dst->handle, region.dst_offset,
                     src->shadow_map + region.src_offset, size);
return;
```

**`wgpuQueueWriteBuffer` is NOT ordered relative to draw commands.** Per the WebGPU spec, all `writeBuffer` calls submitted before `wgpuQueueSubmit` take effect before any GPU commands in that submission, and the last write to a given buffer range wins.

So with 4 canvases, 4 `wgpuQueueWriteBuffer` calls hit the same `canvas_state_buffer`:
1. (0,0)
2. (336.5,-260)
3. (0,0)
4. (0,0) ← **last write wins**

All four canvas draw passes see `canvas_transform.origin = (0,0)`. The camera offset is silently discarded. The scene renders from world origin (0,0) regardless of Camera2D position.

In **Vulkan** this worked correctly because `vkCmdCopyBuffer` IS interleaved with draw commands in the command buffer — each canvas sees the copy that immediately precedes its draws.

---

## Fix

**File**: `drivers/webgpu/rendering_device_driver_webgpu.cpp`

Instead of writing directly to the destination UBO, flush the shadow map to the **staging buffer's own GPU handle** first, then use the command encoder to copy staging→UBO. Encoder copies (`wgpuCommandEncoderCopyBufferToBuffer`) ARE ordered relative to draw commands within the command buffer.

```cpp
// NEW — CORRECT
if (src->shadow_map) {
    for (uint32_t i = 0; i < p_regions.size(); i++) {
        const BufferCopyRegion &region = p_regions[i];
        uint64_t size = (region.size + 3) & ~3ULL;
        // Flush shadow map to the staging buffer's GPU handle.
        wgpuQueueWriteBuffer(queue, src->handle, region.src_offset,
                             src->shadow_map + region.src_offset, size);
    }
    // Fall through to encoder copy below.
}

cmd->end_active_encoder();

for (uint32_t i = 0; i < p_regions.size(); i++) {
    const BufferCopyRegion &region = p_regions[i];
    uint64_t size = (region.size + 3) & ~3ULL;
    wgpuCommandEncoderCopyBufferToBuffer(cmd->encoder,
        src->handle, region.src_offset,
        dst->handle, region.dst_offset, size);
}
```

Now:
- Each `wgpuQueueWriteBuffer` writes to the staging buffer (a different handle each time).
- The encoder copies staging→UBO are interleaved with the draw commands.
- Each canvas's draw commands see their own `canvas_transform`.
- Camera2D correctly scrolls the game world.

---

## Investigation Path

### What Was Confirmed Correct (Dead Ends Ruled Out)

All of the following were verified to be **identical** between WebGL and WebGPU, ruling them out as causes:

1. **GDScript state** (5s after load): `viewport.canvas_transform.origin=(336.5,-260)`, `camera.is_current=true`, player at correct position, 437 nodes.
2. **C++ `viewport_set_canvas_transform`**: Camera2D correctly calls this with `(336.5,-260)` from frame 7 onward (`[VCT]` logging).
3. **C++ `_canvas_get_transform`**: Returns `(336.5,-260)` for the game world canvas (`[CGT]` logging).
4. **C++ render pass loop**: The game world canvas (layer=0) receives `xform_origin=(336.5,-260)` and passes it to `RSG::canvas->render_canvas()` (`[RP]` logging).
5. **C++ `canvas_render_items`**: Receives `p_canvas_transform.columns[2]=(336.5,-260)` and calls `buffer_update` with the correct data (`[RD-NZ]` logging).
6. **Struct layout**: C++ `State::Buffer` and GLSL/WGSL `CanvasData` are identical — `canvas_transform` at offset 0, no padding differences.

The bug was entirely in the **GPU-side buffer update ordering**, not in any transform computation.

---

## History of Prior (Incomplete) Investigation

### Session ~March 2026
- Compared WebGPU vs WebGL screenshots.
- `canvas_xform_origin` was identical → camera math was correct.
- Fixed `blit.glsl` (`color.a = 1.0`) for transparent sprites, and texture swizzle (orange tint).
- **Incorrectly believed the camera issue was also fixed** — likely the test was declared passing without verifying camera centering vs. world origin rendering.

### Session April 9-10, 2026
- Rebuilt from `webgpu_pre_phase7` (commit `e40fa804fe`).
- Still broken: camera not following player.
- Traced through all C++ layers with diagnostic logging.
- Found the root cause in `command_copy_buffer` WebGPU ordering.
- Fixed and verified: **camera now correctly follows the player**.

---

## Key Files

| File | Purpose |
|------|---------|
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | **The fix** — `command_copy_buffer()` |
| `servers/rendering/renderer_rd/renderer_canvas_render_rd.cpp` | Canvas rendering — uploads `canvas_transform` uniform |
| `servers/rendering/renderer_rd/shaders/canvas.glsl` | Canvas vertex shader — applies `canvas_data.canvas_transform` |
| `servers/rendering/renderer_rd/shaders/canvas_uniforms_inc.glsl` | `CanvasData` uniform struct definition |
| `servers/rendering/renderer_viewport.cpp` | Render pass loop — passes per-canvas transform to `canvas_render_items` |
| `godot-demo-projects/2d/platformer/player/player.gd` | Player + Camera2D |
