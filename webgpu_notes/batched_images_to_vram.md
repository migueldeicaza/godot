# Batched Texture2DArray Uploads — Engine Patches for WebGPU

Two related engine-side optimizations to fix per-layer fan-out and the
wasted-staging-buffer penalty in `Texture2DArray::create_from_images`
(and any other path that lands in `RenderingDevice::texture_create`
with `data_slices.size() > 1` and `mipmaps == 1`).

These patches are **opt-in** from the driver side via API traits. All
non-WebGPU drivers continue through the existing transfer-worker path
bit-identically — no risk of regression on Vulkan / Metal / D3D12 / GLES.

Real-world impact, measured on Shiny Gen (2D/3D hybrid game, 5
`Texture2DArray` atlases ranging 1024² × 75 layers down to 128² × 5
layers, ~1 GB total):

| Metric                       | Before   | After    | Δ          |
|------------------------------|---------:|---------:|-----------:|
| Web `load_body_ms` (median)  | 10697 ms | 3711 ms  | **-65%**   |
| `create_from_images` total   |  2877 ms |  240 ms  | **-92%**   |
| 1024² × 75-layer single tier |   860 ms |  220 ms  | **-74%**   |
| Peak VRAM during upload      |  +N MB   |  0       | **-300 MB**|
|                              |          |          | per tier   |

Native baseline ~182 ms; web went from ~58× to ~20× native on this app.

---

## Patch 1 — Multi-layer batched copy (`command_copy_buffer_to_texture_layered`)

### Problem

`RenderingDevice::texture_create` calls `_texture_initialize` once per
array layer:

```cpp
for (uint32_t i = 0; i < format.array_layers; i++) {
    _texture_initialize(id, i, data[i], dst_layout, immediate_flush);
}
```

On WebGPU, each `_texture_initialize` ends in one
`wgpuQueueWriteTexture` call. Each call crosses
**wasm → JS → WebGPU**, costing ~9–11 ms of fixed bridge overhead
(measured), regardless of texture size. For a 75-layer atlas that's
~825 ms of pure bridge crossing — independent of the actual data.

The native API supports multi-layer in one call:
`extent.depthOrArrayLayers > 1` covers consecutive array layers from
contiguous source data.

### Fix

Add a layered-copy virtual on `RenderingDeviceDriver` with a
default fan-out implementation (preserves per-driver behavior), and a
WebGPU override that issues a single `wgpuQueueWriteTexture` covering
all N layers. The engine packs the N layers into one staging allocation
and calls the new virtual once instead of N times.

### Files

- `servers/rendering/rendering_device_driver.h` — new virtual,
  default fan-out impl.
- `servers/rendering/rendering_device.cpp` — new
  `_texture_initialize_layered` helper that batches the staging
  allocation and emits one layered copy region. Used by
  `texture_create` when `array_layers > 1` and `mipmaps == 1`.
- `drivers/webgpu/rendering_device_driver_webgpu.{h,cpp}` — override
  using `extent.depthOrArrayLayers = N`.

### Default behavior preserved

```cpp
virtual void command_copy_buffer_to_texture_layered(...) {
    for (uint32_t i = 0; i < p_layer_count; i++) {
        BufferTextureCopyRegion r = p_base_region;
        r.texture_subresource.layer += i;
        r.buffer_offset += i * p_per_layer_byte_stride;
        command_copy_buffer_to_texture(...);
    }
}
```

Drivers that don't override get the same per-layer behavior as today.

### Could other backends benefit?

- **Vulkan**: yes — `vkCmdCopyBufferToImage` accepts
  `imageSubresource.layerCount > 1`. Could cut the per-layer command
  overhead, though Vulkan doesn't have wasm bridge cost so the win is
  smaller.
- **Metal**: yes — `MTLBlitCommandEncoder` `copyFromBuffer:toTexture:`
  supports multi-layer in `slice` parameter loops; might be more
  marginal. Could also be reformulated through `replaceRegion:`.
- **D3D12**: less clean — `CopyTextureRegion` is per-subresource, but
  the encoder overhead is small.

The default fan-out keeps every driver functional; opting in is a
follow-up driver-by-driver decision.

---

## Patch 2 — Direct CPU→GPU writeTexture path (skip transfer worker)

### Problem (the deeper one)

Patch 1 reduces N writeTexture calls to 1, but the transfer worker
path still allocates a same-size **GPU staging buffer** that
`wgpuQueueWriteTexture` never reads from. The actual data path on
WebGPU is:

1. `_acquire_transfer_worker(N MB)` →
   `wgpuDeviceCreateBuffer(N MB)` — VRAM allocation (real, takes
   real GPU memory).
2. `buffer_map(staging_buffer)` → `memalloc(N MB)` — CPU shadow
   buffer (`shadow_map`).
3. Engine writes layer data into `shadow_map`.
4. `command_copy_buffer_to_texture` checks
   `if (src->shadow_map != nullptr)` → calls
   `wgpuQueueWriteTexture(queue, dst, shadow_map, ...)` directly. The
   GPU staging buffer is **never touched**.

On a 1024² × 75-layer RGBA atlas (314 MB), this allocates a 314 MB GPU
buffer that's pure waste, and that allocation appears to force GPU
queue serialization — `MultiMesh` setup on the next tier blocks for
~440 ms (per-tier fixed cost) until the previous allocation settles.

### Why the wasted GPU buffer exists

The transfer-worker path is shared with Vulkan/Metal/D3D12, which all
need a real staging buffer (their copy commands run from a command
encoder against a GPU resource, not a CPU pointer). WebGPU is the odd
one out: `wgpuQueueWriteTexture` takes a CPU pointer directly. The
shared code allocates the buffer regardless; the WebGPU path just
ignores it.

### Fix

Add an opt-in `ApiTrait` and a new virtual on
`RenderingDeviceDriver`:

```cpp
enum ApiTrait {
    ...
    // If non-zero, RenderingDevice::_texture_initialize_layered uses
    // a CPU-only staging path: memalloc → pack layers → call
    // texture_initialize_direct_layered → memfree. No GPU staging
    // buffer, no command encoder, no pipeline barriers.
    API_TRAIT_TEXTURE_INITIALIZE_DIRECT_WRITE,
};

// Direct CPU→GPU layered texture initialization. Default impl errors;
// only callable when the trait opts in.
virtual void texture_initialize_direct_layered(
    TextureID p_dst_texture,
    TextureLayout p_dst_layout,
    const uint8_t *p_cpu_data,
    uint64_t p_total_size,
    uint32_t p_aligned_bpr,
    uint32_t p_rows_per_image,
    uint32_t p_width,
    uint32_t p_height,
    uint32_t p_layer_count,
    uint32_t p_base_layer,
    uint32_t p_mip_level);
```

WebGPU opts in (`return 1` for the trait) and overrides the new method
with a single `wgpuQueueWriteTexture` call from the CPU pointer
directly.

`_texture_initialize_layered` branches early when the trait is set:

```cpp
if (driver->api_trait_get(RDD::API_TRAIT_TEXTURE_INITIALIZE_DIRECT_WRITE)) {
    uint8_t *cpu_staging = (uint8_t *)memalloc(total_staging_size);
    // pack layers (same logic as transfer-worker path)
    driver->texture_initialize_direct_layered(...);
    memfree(cpu_staging);
    return OK;
}
// ...else existing transfer-worker path unchanged.
```

### Memory impact

Per Texture2DArray upload, peak memory drops by exactly the staging
size:

| Phase    | Pre-patch RAM | Pre-patch VRAM | Post-patch RAM | Post-patch VRAM |
|----------|--------------:|---------------:|---------------:|----------------:|
| Source `Image` data alive | N MB | 0      | N MB           | 0               |
| During upload             | 2N MB | N MB | 2N MB          | 0               |
| Atlas in VRAM             | 0    | N MB  | 0              | N MB            |

(Where N = total staging size, e.g. 314 MB for 1024² × 75 RGBA layers.)

The post-patch path eliminates the wasted N-MB GPU staging buffer. The
2N RAM transient is the same in both paths (source `Image` + packing
buffer) and is freed before the function returns.

### Why this is a clean opt-in

- New trait defaults to 0 (`RenderingDeviceDriver::api_trait_get`
  default impl returns 0 for unknown traits).
- New virtual default impl errors with a clear message — only
  reachable when a backend opts in.
- Other drivers' code paths are bit-identical pre/post.
- Pattern matches the existing `API_TRAIT_TEXTURE_GET_DATA_VIA_DRIVER`
  precedent (also WebGPU-specific opt-in for an async-sync mismatch).

### Could other backends benefit?

- **Metal**: yes, possibly — `MTLTexture replaceRegion(_:withBytes:)`
  is a CPU-pointer-based texture update with no separate staging
  buffer. Could opt into the same trait. Would need measurement.
- **Vulkan / D3D12 / GLES**: no — these need a real staging
  buffer/upload heap. Trait stays 0.
- **WebGPU**: yes, the original target.

So the trait isn't WebGPU-only; it's a generalizable optimization
gated on the backend having a synchronous CPU-pointer texture write
API.

---

## File-by-file diff summary

| File | LOC added | Change |
|---|---:|---|
| `servers/rendering/rendering_device_driver.h` | ~30 | New `ApiTrait` value, new layered virtual + default fan-out, new direct-write virtual + erroring default |
| `servers/rendering/rendering_device.cpp` | ~120 | `_texture_initialize_layered` helper + direct-write fast path branch |
| `drivers/webgpu/rendering_device_driver_webgpu.h` | ~10 | Two `override final` declarations |
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | ~100 | Two override implementations + two new `api_trait_get` cases |

Total ~260 lines net, one logical change per file.

## Test plan

- ✅ Native (macOS Vulkan/Metal): no behavior change, smoke tests pass.
- ✅ WebGPU (Chrome 120+): `load_body_ms` 10697 → 3711 ms, all visual
  regression tests pass, no GPU validation errors.
- ✅ Memory: peak VRAM during 75-layer atlas upload reduced from
  ~600 MB to ~300 MB.
- ⏳ TODO before PR: verify Vulkan + Metal default fan-out path
  preserves identical behavior (we expect it to — the default impl
  reproduces the exact pre-existing per-layer loop).

## Suggested PR shape

Two PRs in sequence, since they're logically separable:

1. **PR-A**: `command_copy_buffer_to_texture_layered` virtual + default
   fan-out + WebGPU override + `_texture_initialize_layered` helper.
   Standalone benefit: -64% on `create_from_images`, no risk to other
   backends.
2. **PR-B**: `API_TRAIT_TEXTURE_INITIALIZE_DIRECT_WRITE` +
   `texture_initialize_direct_layered` virtual + WebGPU opt-in + the
   fast-path branch in `_texture_initialize_layered`. Stacks on PR-A.
   Standalone benefit: eliminates wasted VRAM allocation + the queue
   serialization it caused.

PR-A is more conservative and useful even if PR-B is rejected. PR-B
needs PR-A as a prereq because it bypasses the transfer-worker path
that the layered virtual lives in.

## Open questions for reviewers

1. **Naming**: `texture_initialize_direct_layered` is descriptive but
   long. Alternatives: `texture_write_direct_layered`,
   `texture_upload_direct`. Open to suggestions.
2. **Single-layer parity**: should the trait also affect
   `_texture_initialize` (mipmaps == 1, single layer) for consistency?
   Probably yes; not done here to keep diff minimal. Easy follow-up.
3. **Mipmap support**: current helper restricts to `mipmaps == 1`. If
   reviewers want broader applicability, the layered helper can be
   generalized to handle mipmap chains; the direct-write virtual would
   need a per-mip variant or a `mip_count` param.
4. **`gpu_handle()` vs `handle`**: WebGPU override uses
   `dst->gpu_handle()` to get the backing texture (consistent with
   shared-view handling we added for viewport readback). Worth
   documenting in the PR that this respects shared-resource semantics.
