# Root Cause: 32MB Staging Buffer Flush on Every `buffer_unmap()`

## Summary

Every texture upload on WebGPU web export pays ~10ms because `buffer_unmap()` copies
the **entire** 32MB staging buffer from WASM linear memory to the GPU — even when
only 64 bytes were written (e.g., a 4x4 placeholder texture). This is the root cause
of ~10ms per `Window.new()`, `OptionButton.new()`, and `ImageTexture.create_from_image()`.

## How We Found It

### Phase 1: GDScript-level profiling

Measured the cost of constructing UI controls in Shiny Gen's map editor on WebGPU web:

| Operation | Time (web) | Time (native) |
|-----------|-----------|---------------|
| `Window.new()` | 9-10ms | <0.1ms |
| `OptionButton.new()` (contains PopupMenu→Window) | 10-22ms | <0.1ms |
| `ImageTexture.create_from_image()` | 9.5-10.5ms | <0.1ms |
| `CollapsibleControl` (2× load_icon + OptionButton) | 22-25ms | <1ms |

### Phase 2: Instrumenting individual wgpu API calls

Added `EM_ASM_DOUBLE(performance.now())` timing around every `wgpu*` call in
`rendering_device_driver_webgpu.cpp`:

| Call | Avg time | Max time | Count |
|------|----------|----------|-------|
| `wgpuDeviceCreateTexture` | 0.02ms | 0.10ms | 108 |
| `wgpuTextureCreateView` (texture_create) | 0.01ms | 0.10ms | 108 |
| `wgpuTextureCreateView` (shared) | 0.02ms | 0.10ms | 80 |
| `wgpuTextureCreateView` (slice) | 0.00ms | 0.10ms | 117 |
| `wgpuQueueWriteTexture` | 0.10ms | 338.90ms* | 3545 |

*338ms outlier is a one-time GPU warmup/flush; steady-state is 0.00-0.10ms.

**All individual WASM→JS→WebGPU crossings are essentially free (<0.1ms).**
The 5 crossings per Window.new() total <0.5ms combined.

### Phase 3: Instrumenting the RenderingDevice C++ layer

Added timing in `rendering_device.cpp` around `_texture_initialize()` internals:

| Operation | Avg time | Notes |
|-----------|----------|-------|
| `_acquire_transfer_worker` | 0.00-0.30ms | Fast |
| `buffer_map` | 0.00-0.10ms | Fast |
| Data copy to staging | <0.01ms | Just memcpy |
| `command_copy_buffer_to_texture` | 0.00-0.10ms | Uses wgpuQueueWriteTexture directly |
| **`buffer_unmap`** | **9.5-12.5ms** | **32MB wgpuQueueWriteBuffer** |

### Phase 4: Confirming buffer_unmap as the bottleneck

Added size tracking to `buffer_unmap()`:

| Staging buffer size | Avg `wgpuQueueWriteBuffer` time | Call count |
|--------------------|---------------------------------|------------|
| **32MB** | **9.95ms** | 757 |
| 4MB | 1.37ms | 44 |
| 256KB | 0.25ms | 4933 |
| 16KB | 0.01ms | 66 |

**A 4x4 texture (64 bytes) triggers a 32MB copy costing ~10ms.**

## Why This Happens

### On Vulkan/Metal/D3D12

`buffer_map()` returns a **direct pointer to GPU-visible memory** (unified memory
architecture or persistently mapped staging region). The CPU writes directly into
the GPU's address space. `buffer_unmap()` is essentially free:

```cpp
// Vulkan
void buffer_unmap(BufferID p_buffer) {
    vmaUnmapMemory(allocator, buf_info->allocation.handle);
}

// Metal
void buffer_unmap(BufferID p_buffer) {
    // Nothing to do.
}
```

### On WebGPU (web)

WebGPU only offers async `mapAsync()` — incompatible with Godot's synchronous
`buffer_map()`/`buffer_unmap()` contract. So the WebGPU driver uses a **shadow
buffer** pattern:

```cpp
// buffer_map: return CPU-side malloc'd copy
uint8_t *buffer_map(BufferID p_buffer) {
    if (!buf->shadow_map) {
        buf->shadow_map = memalloc(buf->size);  // 32MB for staging buffers
    }
    buf->map_dirty = true;
    return buf->shadow_map;
}

// buffer_unmap: flush ENTIRE shadow → GPU
void buffer_unmap(BufferID p_buffer) {
    if (buf->shadow_map && buf->map_dirty) {
        wgpuQueueWriteBuffer(queue, buf->handle, 0, buf->shadow_map, buf->size);
    }
}
```

The `wgpuQueueWriteBuffer` call copies `buf->size` bytes (32MB) from WASM linear
memory → JavaScript heap → GPU process, regardless of how many bytes were actually
modified.

## Why buffer_unmap Is Redundant (Not Just Oversized)

The data paths that USE staging buffers already handle their own flushing:

### `command_copy_buffer_to_texture` (texture uploads)

When the source buffer has a shadow_map, it uses `wgpuQueueWriteTexture` to upload
directly from CPU memory to the GPU texture. **The GPU staging buffer is never read.**

```cpp
if (src->shadow_map != nullptr) {
    // Direct CPU→GPU texture upload — bypasses GPU staging buffer
    const uint8_t *data_ptr = src->shadow_map + region.buffer_offset;
    wgpuQueueWriteTexture(queue, &dst_copy, data_ptr, data_size, &layout, &extent);
}
```

### `command_copy_buffer` (buffer-to-buffer copies)

Already writes only the specific regions from shadow → GPU staging buffer, then
encoder-copies to the destination:

```cpp
if (src->shadow_map) {
    for (region in regions) {
        wgpuQueueWriteBuffer(queue, src->handle, region.src_offset,
                             src->shadow_map + region.src_offset, region.size);
    }
    // Fall through to encoder copy: staging → destination
}
```

### End-of-frame staging block flush (line 7038)

Calls `buffer_unmap()` on all upload staging blocks. But those blocks were used by
`texture_update()`/`buffer_update()` which go through the above command paths —
which already self-flush. The end-of-frame unmap is also redundant.

## Fix (Implemented)

### Part 1: Clear `map_dirty` in command_copy_* functions

Both `command_copy_buffer_to_texture` (and its `_layered` variant) and
`command_copy_buffer` already handle their own data transfer from the shadow
buffer to the GPU. After they complete, they now set `src->map_dirty = false`
so the subsequent `buffer_unmap()` becomes a no-op instead of redundantly
flushing the entire staging buffer.

### Part 2: Dirty range tracking in WGBuffer

Added `dirty_offset` and `dirty_end` fields to `WGBuffer` (in `webgpu_objects.h`).
When `buffer_unmap()` does need to flush (e.g., for unknown callers or persistent
buffers), it only flushes the dirty range rather than the entire buffer.

### Part 3: Dirty range for persistent dynamic buffers

`buffer_persistent_map_advance()` now sets `dirty_offset`/`dirty_end` to the
current frame's slice, so `buffer_flush()` only writes `per_frame_size` bytes
instead of the entire multi-frame buffer.

`buffer_flush()` also respects the dirty range (with full-buffer fallback).

### Changed functions

| Function | Change |
|----------|--------|
| `buffer_unmap()` | Flush only dirty range (if set), clear map_dirty + range |
| `buffer_flush()` | Flush only dirty range (if set) |
| `buffer_persistent_map_advance()` | Set dirty_offset/dirty_end to frame slice |
| `command_copy_buffer()` | Clear `src->map_dirty` after region flush |
| `command_copy_buffer_to_texture()` | Clear `src->map_dirty` after writeTexture path |
| `command_copy_buffer_to_texture_layered()` | Clear `src->map_dirty` after writeTexture path |

### Measured Results

| Operation | Before (web) | After (web) | Improvement |
|-----------|-------------|-------------|-------------|
| `CollapsibleControl` total | 22-25ms | 3.8-4.6ms | **~5× faster** |
| `OptionButton` (via UIFuncs) | 10-22ms | 1.3-2.2ms | **~8× faster** |
| `ResizedOptionButton._enter_tree` | 9-10ms | 1.0-1.4ms | **~8× faster** |

The remaining ~1-4ms per component is genuine work: SVG icon decode, theme
lookups, and layout calculations — not wasted GPU transfers.

## Key Source Files

| File | What |
|------|------|
| `drivers/webgpu/webgpu_objects.h` | `WGBuffer` struct — dirty_offset/dirty_end fields |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | `buffer_unmap()` — dirty range flush |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | `buffer_flush()` — dirty range flush |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | `buffer_persistent_map_advance()` — sets dirty range |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | `command_copy_buffer()` — clears map_dirty |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | `command_copy_buffer_to_texture()` — clears map_dirty |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | `command_copy_buffer_to_texture_layered()` — clears map_dirty |
| `servers/rendering/rendering_device.cpp:1489` | `_texture_initialize()` — calls map→copy→unmap |
| `servers/rendering/rendering_device.cpp:7038` | End-of-frame staging flush |
