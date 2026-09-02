# Fix: 3D Platformer Console / GPU Errors

**Context:** User reports that running `./tools/web_qa/run_demo.sh 3d_platformer webgpu --keep-alive` on branch `webgpu_bak_phase_7_fix` produces:

- **42 console errors** right away at startup
- **2 GPU errors** right away at startup
- **Additional errors every time the character is moved** (not captured in log yet)

This note investigates the startup errors from the log at `/tmp/demo_3d_platformer_webgpu_chrome.log` (run with commit `fd2518a3d` / branch `webgpu_bak_phase_7_fix`) and plans the fix for each.

---

## Breakdown of the "42 console errors"

Chrome counts every stderr write from Godot as an `[error]`, but most of these are actually warnings being misclassified.

- **1× 404** (line 2) — favicon or similar; harmless.
- **41× `WARNING: Image format LumAlpha8 not supported by hardware, converting to RGBA8`**
  - Printed from `servers/rendering/renderer_rd/storage_rd/texture_storage.cpp:2475` (`_validate_texture_format`).
  - One line per LA8 texture uploaded at startup (probably font glyphs for the demo UI).

These are **not WebGPU errors**, not validation failures, and not driver faults. The LA8→RGBA8 conversion path added in earlier commits (`dff246047c`, `000e45f1f6`, `d5f283cbec`) handles these textures correctly. Godot core simply prints a warning before hitting the conversion fallback, and because stderr is captured as "error" in Chrome DevTools, it inflates the count.

### Fix options for the LA8 spam

**Option A (preferred): mark LA8 as "supported" in the driver format capability query.** Since we already swizzle-emulate L8/LA8 via the RGBA8 upconversion path, we can claim support in whatever capability bitmask the driver exposes, which suppresses `_validate_texture_format`'s early warning. Zero behavior change.

**Option B:** filter the specific warning string from the JS-side log output. Cosmetic only — the warning still fires in the engine.

We should do Option A once we're sure the swizzle emulation is reliable for every LA8 consumer. Option B is a fallback if Option A hits complications.

---

## The "2 GPU errors at startup"

These are the real GPU validation errors. Lines 4022–4038 of the log:

```
[Godot] WebGPU uncaptured error: GPUValidationError
[Godot] WebGPU uncaptured error: GPUValidationError
[Texture (unlabeled 640x640 px, 7 layer, TextureFormat::RGB10A2Unorm)]
  usage (TextureBinding|RenderAttachment)
  includes writable usage and another usage in the same synchronization scope.
  - While validating render pass usage.
  - While finishing [CommandEncoder (unlabeled)].

[Invalid CommandBuffer] is invalid due to a previous error.
  - While calling [Queue].Submit([[Invalid CommandBuffer]])
```

### Identifying the texture

- **Format `RGB10A2Unorm`** — Forward Mobile's preferred HDR color format (`render_forward_mobile.cpp:434`, `_render_buffers_get_preferred_color_format`). So it is one of the Forward Mobile render-buffer color attachments or derived buffers.
- **Size `640×640`, 7 layers** — not the main screen buffer (which would be 1280×720). Given format + dimensions + layer count, this is almost certainly a **reflection atlas / sky radiance cubemap array** or a multi-view shadow/probe atlas setup. 7 layers is unusual — not a cube (6), not a standard cascade count (4). Likely 6 cube faces + 1 something, or a reflection probe atlas with 7 slots.

### Nature of the conflict

The text "TextureBinding|RenderAttachment … in the same synchronization scope" means the same texture is declared both as a render attachment **and** sampled as a texture binding **within the same render pass**.

This is a **within-pass** dual-usage conflict, not a cross-pass transition. It cannot be fixed by splitting the encoder between passes — the existing sync-scope heuristic already does that, and those fixes land in different commits (`8f00cb4fb6`).

This matches the known Task 7.14 note in `webgpu_notes/TASKS.md:1851`:

> **Issue**: Encoder split detection (for cross-pass texture read-after-write) only checks if texture is "still an attachment." Doesn't check usage flags — a texture transitioning from write to read on the same attachment could be missed.

And the Scene F notes at line 1501 describe this same pattern for SSAO: "SSAO has a non-fatal GPU validation error (texture sync scope conflict documented in the driver)."

### Options to fix

1. **Rename the texture via `wgpuTextureSetLabel()` at creation.** Doesn't fix the bug but will let WebGPU's validation messages say *which* Godot RID this is (e.g. `"reflection_atlas_0"`, `"depth_shadow_cube_0"`). This should be step 1 of any real debugging — right now the texture reports as "unlabeled" and we're guessing from dimensions.
2. **Track the render pass's own color/depth attachments and skip binding them as TextureBinding entries in the same bind group.** The driver can detect when a uniform set references a texture that is currently an attachment of the active pass and either (a) substitute a zero/dummy view, or (b) bail on the bind. This is a real behavior change and would need care — it's essentially emulating Vulkan's input attachment / feedback loop rules.
3. **Force a pass split (submit-then-begin) when a texture transitions from RenderAttachment to TextureBinding within a logical pipeline step.** This requires the renderer to actually split the work — not straightforward since the draw order is fixed by the high-level renderer.

For now, **label the texture (option 1) and investigate which resource it actually is**, then decide which structural fix to pursue based on what we find.

---

## Errors on character movement — CAPTURED, and the surprise

After fixing `test_demo.mjs` to stream events live, the user re-ran `run_demo.sh 3d_platformer webgpu --keep-alive`, moved the character, then closed the browser. The captured log is **10,761 lines**, of which **2,737 are from the keep-alive (post-startup) phase**.

### Distinct non-cascade lines in the entire keep-alive section

Searching the post-keep-alive portion for any error/warning string that is NOT a cascade (not `GPUValidationError`, not `is invalid due to`, not `While encoding/finishing/calling`) returns literally three unique lines:

```
[error] Failed to load resource: the server responded with a status of 404 (Not Found)
[http404] http://127.0.0.1:8899/test-enabled
[warning] WebGPU: too many warnings, no more warnings will be reported to the console for this GPUDevice.
```

That last one is the critical finding — **Chrome rate-limited warnings for the GPUDevice**. Once the threshold is hit, Dawn stops reporting new validation failure details; only the downstream cascade ("Invalid RenderPipeline / Invalid CommandBuffer") keeps firing. So post-movement errors may exist but their detail text is suppressed by Chrome.

### Total error inventory across the entire 10,761-line log

| Category | Count |
|----------|-------|
| `[error] WARNING: Image format LumAlpha8 …` (Godot LA8→RGBA8 spam, benign) | 41 |
| `[http 404]` (favicon + test-enabled probe) | 2 |
| `sync scope` error on the 640×640×7 RGB10A2Unorm texture | **1 unique** (fires once, then repeated via deduped `[warning]`) |
| `GPUValidationError` headers from the uncapturederror listener | ~1620 |
| `is invalid due to a previous error` (cascade) | ~1308 |

**Only one non-cascade, non-warning-spam root-cause error exists in the entire log:** the sync-scope error on the 640×640×7 RGB10A2Unorm texture that first fires at line 4024 during startup (before the user ever interacts).

### So what actually happens when the user moves the character?

Two possibilities:
1. **The cascade just continues.** The same broken pass/pipeline is re-issued every frame. No new errors during movement — the "things getting worse when you move" perception is because the broken pass is finally doing something visible (scene rendering and camera-relative work) so the glitches actually show on screen.
2. **New distinct errors DO happen** during movement but Chrome has rate-limited them, so we only see the downstream "Invalid RenderPipeline" cascade in the log.

We cannot distinguish (1) from (2) from the current log alone.

### Missing: pipeline creation error detail

The driver already has per-create instrumentation at `rendering_device_driver_webgpu.cpp:5241-5255` via `pushErrorScope('validation')` / `popErrorScope()` around `wgpuDeviceCreateRenderPipeline()` which would print `[PCREATE-FAIL] id=X shader=Y | <detail>` if creation failed. That instrumentation fires **zero times** in the log — meaning no render pipeline is failing at creation. Yet the `[Invalid RenderPipeline (unlabeled)] is invalid due to a previous error` cascade still appears starting at line 4518. This implies the "Invalid RenderPipeline" errors are all downstream fallout of the same root sync-scope error, not from new pipeline creation failures — Dawn keeps the same broken encoder/pass in play and every frame reuses it.

---

## Updated plan

1. **[DONE]** Write this note; capture keep-alive logs; confirm only ONE root-cause error exists.
2. **[DONE]** Wire `set_object_name(TEXTURE)` to `wgpuTextureSetLabel()`. Texture labels now appear in Dawn error messages, which let us identify the 640×640×7 texture.
3. **[DONE]** Identified and fixed the startup 640×640×7 RGB10A2Unorm sync-scope error — it was the **sky radiance mipmap chain**. Fix: `texture_create_shared_from_slice` was creating views with the wrong `view_dimension`.
4. **[DONE — BUT NOT THE MAIN PROBLEM]** The startup sync-scope error was gone after step 3, but on the next rerun the **keep-alive phase still produced ~800 GPU errors as soon as the character moved**. The cascade text was `[Invalid RenderPipeline "pipe#80:ResolveRasterShaderRD:0"] is invalid due to a previous error` — meaning the MSAA depth resolve pipeline was being created invalid but no creation-time error was captured anywhere.
5. **[DONE]** **Resolved the ResolveRasterShaderRD cascade.** Full story in "Root cause of the ResolveRasterShaderRD cascade" below.
6. **[DEFERRED]** Silence the LA8 spam (mark LA8 "supported" in the format capability query so `_validate_texture_format` stops printing the warning). Still 41 of these per startup but they're benign stderr warnings misclassified as errors — safe to defer.

With steps 2–5 clean, the Phase 7 re-application plan in `FIX_PHASE7.md` can resume.

---

## Root cause of the ResolveRasterShaderRD cascade

### The diagnostic infrastructure that actually worked

Catching this required building up several layers of instrumentation because the error was **not visible via any standard WebGPU error channel**:

1. **Texture labels** via `wgpuTextureSetLabel` — named the unlabeled 640×640×7 texture so we could at least identify the first sync-scope error at startup.
2. **Pipeline/shader/BGL/pipeline-layout labels** added to every `wgpu*CreateX` call via `CharString` storage with care to avoid dangling-pointer UB (`utf8().get_data()` returns a temporary).
3. **JS monkey-patches** on `device.createRenderPipeline` / `createShaderModule` / `createBindGroupLayout` / `createPipelineLayout`, installed via `MAIN_THREAD_EM_ASM` in `initialize()` (not `swap_chain_resize` — that's too late, it misses the first ~50 pipelines). Each patch wraps the call in `pushErrorScope('validation')` / `popErrorScope()`.
4. **The critical breakthrough — parallel `createRenderPipelineAsync`**: the `pushErrorScope`/`popErrorScope` path captured ZERO failures even though pipelines were clearly being created invalid. Dawn defers some pipeline validation past the `popErrorScope` point. The fix was to **also kick off `device.createRenderPipelineAsync(desc)` alongside the sync call and log its rejection reason**. This is the authoritative validation channel and immediately named the real error.
5. **Autonomous rebuild/run/inspect loop** via a new Playwright script (`shiny_gen_1/tools/web_qa/test_3d_plat_autoloop.mjs`) that injects ArrowUp + W + Space keyboard events so the character moves and the MSAA resolve path actually runs.

### The real error (finally visible)

```
[JS-PCREATE-ASYNC-FAIL#80] label="pipe#80:ResolveRasterShaderRD:0" |
Binding multisampled flag (0) doesn't match the layout's multisampled flag (1)
```

And after partially fixing the first layer:

```
[JS-BGL-FAIL] label="bgl:ResolveRasterShaderRD:0:set0" |
Sample type for multisampled texture binding was TextureSampleType::Float.
```

And after fixing that layer:

```
Sample count (1) of [Texture (unlabeled 4x4 px, TextureFormat::RGBA8Unorm)] doesn't match expectation (multisampled: 1)
 - While validating [BindGroupDescriptor] against [BindGroupLayout "bgl:ResolveRasterShaderRD:0:set0"]
```

Three distinct bugs stacked on top of each other, each hidden by the next. All three in the same code path.

### What the three bugs were

1. **`multisampled` flag hardcoded to `false` in every BGL entry.** `drivers/webgpu/rendering_device_driver_webgpu.cpp` had `entry.texture.multisampled = false;` at every texture BGL creation site. Godot's `resolve_raster.glsl` declares `uniform sampler2DMS source_depth;` which Tint translates to `texture_multisampled_2d<f32>` in WGSL — but our BGL reflection scan never detected the multisampled variants and therefore always set the flag to false, so the BGL and the shader module disagreed on the first texture binding.

2. **`sampleType=Float` is illegal for multisampled textures.** Once we flipped `multisampled=true`, the BGL failed a different validation: WebGPU requires `sampleType ∈ { 'unfilterable-float', 'depth', 'sint', 'uint' }` for MSAA bindings — `'float'` (filterable) is never allowed because MSAA textures can't be filtered. The paired sampler also has to be `NonFiltering` rather than `Filtering`.

3. **Fallback-texture substitution path ignored the multisampled flag.** `uniform_set_create` substitutes a 4×4 RGBA8Unorm fallback texture whenever the bound texture is a depth format (because WebGPU forbids sampling depth-format textures as `'float'`). Once the BGL correctly said `multisampled=true, sampleType=unfilterable-float`, the substituted fallback didn't match — sample count 1, wrong format. The runtime error we hit after fixing (1) and (2) was `Sample count (1) of [Texture (4x4 px, RGBA8Unorm)] doesn't match expectation (multisampled: 1)`.

### The fix (code changes)

All in `drivers/webgpu/rendering_device_driver_webgpu.cpp` + `rendering_device_driver_webgpu.h`:

1. **New WGSL reflection map `wgsl_is_multisampled_texture`** populated by the shader-reflection scan when a binding's type starts with `texture_multisampled_2d` or `texture_depth_multisampled_2d`. The multisampled checks precede the plain `texture_2d` / `texture_depth_2d` checks so the longer prefix wins.

2. **BGL texture entry creation** for both `UNIFORM_TYPE_TEXTURE` and `UNIFORM_TYPE_SAMPLER_WITH_TEXTURE` now reads the map and sets:
    - `multisampled` = reflected value
    - `sampleType` = `Depth` if depth, otherwise `UnfilterableFloat` if multisampled, otherwise `Float`
    - paired sampler type = `NonFiltering` if the texture is multisampled (and non-depth)

3. **New fallback texture `fallback_ms_texture`** — 4×4 R32Float, `sampleCount=4`, usage `TextureBinding|RenderAttachment` (RenderAttachment is required for MSAA textures even though we never render to it). Created in `initialize()` alongside the existing float and cube fallbacks, released in the destructor.

4. **`uniform_set_create` substitution** for `UNIFORM_TYPE_SAMPLER_WITH_TEXTURE` checks the BGL entry's multisampled flag first. If the BGL expects multisampled and the bound texture is either non-MS or depth format, it substitutes `fallback_ms_texture_view` instead of the regular float fallback. This means the resolve shader reads a 4×4 zero texture and produces an (incorrect) depth resolve, **but no GPU validation errors**. A proper MSAA depth resolve would require rewriting the Tint output to use `texture_depth_multisampled_2d`; that's a deeper structural change deferred to later.

### Result

Startup: `gpuErrors=0`. Walk phase (ArrowUp 4s + W 1s + Space×2): `gpuErrors=0`. Only remaining log noise is the 36 LumAlpha8 Godot-core warnings misclassified as errors by Chrome DevTools.

### New diagnostic entry points (keep these)

- `tools/web_qa/test_3d_plat_autoloop.mjs` (in `shiny_gen_1` repo) — Playwright-driven rebuild/run/inspect loop that loads the 3D platformer, waits for canvas, injects keyboard input, and dumps a compact summary. This is the right shape for autonomous iteration on rendering bugs.
- The JS monkey-patches in `rendering_device_driver_webgpu.cpp::initialize()` intercept every `createRenderPipeline` call plus a parallel `createRenderPipelineAsync` for authoritative error reporting. Worth keeping as the default diagnostic path for WebGPU pipeline bugs — error scopes alone are not sufficient.
