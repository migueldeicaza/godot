# Slow Window / Texture Creation on WebGPU Web Export

## Problem

`Window.new()` takes ~9-10ms on WebGPU web export (Chrome). This makes OptionButton
(which internally creates a PopupMenu, which extends Window) unusably slow when many
are constructed during UI setup. On native Vulkan/Metal the same operation is <0.1ms.

## Measured: Where the 9ms lives

### GDScript-level profiling (Shiny Gen web export)

Inheritance chain timing (steady-state, post-warmup):

| Class constructor | Time  | Delta over parent |
|-------------------|-------|-------------------|
| Object.new()      | 0.0ms | -                 |
| Node.new()        | 0.0ms | +0                |
| Control.new()     | 0.0ms | +0                |
| Button.new()      | 0.1ms | +0.1              |
| **Window.new()**  | **9.1ms** | **+9ms**      |
| Popup.new()       | 9.1ms | +0                |
| PopupMenu.new()   | 9.6ms | +0.5              |
| OptionButton.new()| 9.1ms | +0 (embeds PopupMenu) |

First call has additional ~10ms warmup (theme/font system).

### Engine-level trace

```
Window::Window()
  -> Viewport::Viewport()                          [scene/main/viewport.cpp:5344]
       -> viewport_create()                         [viewport.cpp:5348]
            -> viewport_initialize()                [renderer_viewport.cpp:960]
                 -> render_target_create()           [texture_storage.cpp:3761]
                      -> _update_render_target()     [texture_storage.cpp:3606]
                           -> texture_allocate()                  (RID alloc, cheap)
                           -> texture_2d_placeholder_initialize() [line 3614]
                                -> texture_2d_initialize()        [line 847]
                                     -> driver->texture_create()  [line 1071]
                                          -> wgpuDeviceCreateTexture   (crossing #1)
                                          -> wgpuTextureCreateView     (crossing #2)
                                     -> _texture_initialize()     [line 1134]
                                          -> command_copy_buffer_to_texture()
                                               -> wgpuQueueWriteTexture (crossing #3)
                                     -> texture_create_shared()   (sRGB view)
                                          -> wgpuTextureCreateView     (crossing #4)
                           -> EARLY RETURN (size 0x0)  [line 3622]
                 -> shadow_atlas_create()            (RID alloc, cheap)
       -> viewport_get_texture()                    [viewport.cpp:5349]
       -> texture_proxy_create()                    [viewport.cpp:5354]
            -> texture_create_shared()
                 -> wgpuTextureCreateView                  (crossing #5)
```

**5 WASM->JS boundary crossings** per Window construction, all for a 4x4 placeholder
texture that will never be rendered to (the Window starts at size 0x0 with updates
disabled).

### Known cost of wgpuQueueWriteTexture

From `rendering_device_driver_webgpu.cpp:4995`:
> Each wgpuQueueWriteTexture call crosses wasm->JS->WebGPU and incurs ~9-11 ms fixed overhead

### What we DON'T yet know

- **How much of the ~9ms is the WASM->JS Emscripten bridge overhead vs Chrome's
  actual `queue.writeTexture()` / `device.createTexture()` execution time?**
- Is `device.createTexture()` fast (~0.5ms) and `queue.writeTexture()` slow (~9ms)?
  Or are all crossings equally expensive?
- Is Chrome doing a GPU-process IPC roundtrip for each call? Or is it the JS
  descriptor marshalling from WASM memory?

## Key source files

| File | What |
|------|------|
| `scene/main/viewport.cpp:5344` | Viewport constructor (creates render target) |
| `servers/rendering/renderer_viewport.cpp:960` | viewport_initialize (calls render_target_create) |
| `servers/rendering/renderer_rd/storage_rd/texture_storage.cpp:3606` | _update_render_target (placeholder creation) |
| `servers/rendering/renderer_rd/storage_rd/texture_storage.cpp:847` | texture_2d_initialize (GPU texture + data upload) |
| `servers/rendering/rendering_device.cpp:916` | RD::texture_create (orchestrates driver call + data init) |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp:1279` | WebGPU texture_create (wgpuDeviceCreateTexture) |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp:4920` | command_copy_buffer_to_texture (wgpuQueueWriteTexture) |
| Emscripten binding: `emsdk/.../library_webgpu.js:1790` | JS wrapper for wgpuDeviceCreateTexture |

## TODO: Root-cause the per-crossing cost

Need to add JS-side timing inside the Emscripten bindings to measure:
1. Time for `device.createTexture()` alone (the browser API call, excluding marshalling)
2. Time for `texture.createView()` alone
3. Time for `queue.writeTexture()` alone
4. Time for the Emscripten descriptor marshalling (WASM memory -> JS object)

This will tell us whether to fix the bridge (batching) or the browser calls (deferral).
