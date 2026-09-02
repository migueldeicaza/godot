## WebGL scene A:

fps ~119
scene loads with sprites visible in different colors, as expected

command:
```
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgl/scene_a && open -a "Google Chrome" http://localhost:8080 && python3 -m http.server 8080
```

## WebGPU scene A:
black screen after the initial godot loading screen
console info:
```
Godot Engine v4.6.stable.custom_build.6c3d5c68f (2026-03-13 20:33:46 UTC) - https://godotengine.org
index.js:475 Build configuration: Emscripten 5.0.0, single-threaded, no GDExtension support.
index.js:475 BENCHMARK_RESULT|scene_a_sprites|avg=119.9|p1=120.0|p99=122.0|frames=1313|duration=11.0
```
command:
```
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_a && open -a "Google Chrome" http://localhost:8080 && python3 -m http.server 8080
```


____


Single Sprite Examples

Both exported. Here are the commands:

## WebGL (1 sprite):



```
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgl/scene_minimal && open -a "Google Chrome" http://localhost:8080 && python3 -m http.server 8080
```



## WebGPU (1 sprite):

```
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_minimal && open -a "Google Chrome" http://localhost:8080 && python3 -m http.server 8080
```

You should see a single red circle bouncing around. If WebGPU still shows a black screen, it confirms the rendering pipeline issue (not a scene complexity problem).



___





**IMPORTANT:** `serve.py` now auto-kills any old server on port 8080. Start the server first (background `&`), wait 0.5s, then open Chrome. This ensures the right scene is served.

WebGPU (run one at a time — the next command auto-kills the previous server):
```
# Scene A - Sprites  tmp/benchmarks/scene_a_sprites/benchmark.gd
bin/godot.macos.editor.arm64 --headless --path tmp/benchmarks/scene_a_sprites --export-release WebGPU
# 5k sprites @ 120fps (maxed out)
# 10k sprites @ 120fps (still)
# 20k sprites @ 30fps (vsync cap hit)
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_a && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080

# Scene B - PBR
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_b && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080

# Scene C - Instances  tmp/benchmarks/scene_c_instances/benchmark.gd
bin/godot.macos.editor.arm64 --headless --path tmp/benchmarks/scene_c_instances --export-release WebGPU
# 14.1 fps on 5k cubes
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_c && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080

# Scene D - Particles  tmp/benchmarks/scene_d_particles/benchmark.gd
bin/godot.macos.editor.arm64 --headless --path tmp/benchmarks/scene_d_particles --export-release WebGPU
# 37.7fps on 500k particles
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_d && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080

# Scene E - Skeletal Animation  tmp/benchmarks/scene_e_animated/benchmark.gd
bin/godot.macos.editor.arm64 --headless --path tmp/benchmarks/scene_e_animated --export-release WebGPU /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_e/index.html && cp tmp/benchmarks/exports/webgpu/tint_convert.wasm tmp/benchmarks/exports/webgpu/scene_e/tint_convert.wasm
# Expected: 20 GPU-skinned cylinders visibly bending/swinging ~120fps
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_e && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080

# Scene F - SubViewport + SSAO + Bloom  tmp/benchmarks/scene_f_postfx/benchmark.gd
bin/godot.macos.editor.arm64 --headless --path tmp/benchmarks/scene_f_postfx --export-release WebGPU /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_f/index.html && cp tmp/benchmarks/exports/webgpu/tint_convert.wasm tmp/benchmarks/exports/webgpu/scene_f/tint_convert.wasm
# Expected: 5 spinning PBR cubes, procedural sky, SSAO, bloom, SubViewport torus on quad
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_f && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080
```





WebGL (same pattern):
```
# Scene A - Sprites  tmp/benchmarks/scene_a_sprites/benchmark.gd
bin/godot.macos.editor.arm64 --headless --path tmp/benchmarks/scene_a_sprites --export-release WebGL
# 5k sprites @ 38fps
# 10k sprites @ 17fps (linear drop, vsync-bound)
# 20k sprites @ 7.5fps (still linear drop)
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgl/scene_a && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080

# Scene B - PBR
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgl/scene_b && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080

# Scene C - Instances
bin/godot.macos.editor.arm64 --headless --path tmp/benchmarks/scene_c_instances --export-release WebGL

# 31.5 fps fps on 5k cubes
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgl/scene_c && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080

# Scene D - Particles
bin/godot.macos.editor.arm64 --headless --path tmp/benchmarks/scene_d_particles --export-release WebGL
# 23.4fps on 500k particles
cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgl/scene_d && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080

# Scene E - Skeletal Animation (WebGL/Compatibility renderer does not support GPU skinning the same way — no WebGL equivalent)
# Scene F - SubViewport + SSAO + Bloom (WebGL/Compatibility renderer has different post-FX path — no WebGL equivalent)
```

---

## WebGPU scene E: Skeletal Animation

fps: ~120fps (expected after fix)
visual: 20 GPU-skinned cylinders animating

**Root cause of original bug**: WGSL scanner in `rendering_device_driver_webgpu.cpp` matched `var<storage, read>` (never produced by Tint) but missed `var<storage>` (Tint's actual read-only format). Skeleton shader's `BlendShapeWeights` and `BlendShapeData` fallbacks (`default_rd_storage_buffer`, 16 bytes) were both incorrectly marked as `WGPUBufferBindingType_Storage` (writable) → Chrome rejected the dispatch with "writable storage buffer aliasing".

**Fix**: Added `var<storage>` detection as read-only in the WGSL scanner (March 2026). Rebuild required — see **rebuild note** below.

---

**Rebuild note** (after WGSL scanner fix):
```bash
EMSDK_QUIET=1 source /Users/dwalter/Documents/projects/godot/emsdk/emsdk_env.sh && \
scons platform=web target=template_release webgpu=yes opengl3=no threads=no -j$(sysctl -n hw.ncpu) && \
cp bin/godot.web.template_release.wasm32.nothreads.zip tmp/benchmarks/templates/webgpu_release.zip
```

**Re-export and serve**:

bin/godot.macos.editor.arm64 --headless --path tmp/benchmarks/scene_e_animated --export-release WebGPU /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_e/index.html && cp tmp/benchmarks/exports/webgpu/tint_convert.wasm tmp/benchmarks/exports/webgpu/scene_e/tint_convert.wasm

cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_e && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080

expected: 20 GPU-skinned cylinders visibly bending/swinging. FPS ~120. Zero GPU errors in console.

---

## WebGPU scene F: SubViewport + SSAO + Bloom

fps: 120fps
visual: passing as expected

bin/godot.macos.editor.arm64 --headless --path tmp/benchmarks/scene_f_postfx --export-release WebGPU /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_f/index.html && cp tmp/benchmarks/exports/webgpu/tint_convert.wasm tmp/benchmarks/exports/webgpu/scene_f/tint_convert.wasm

cd /Users/dwalter/Documents/projects/godotwebgpu/godot/tmp/benchmarks/exports/webgpu/scene_f && python3 ../../../serve.py & sleep 0.5 && open -a "Google Chrome" http://localhost:8080

expected: 5 spinning PBR cubes on a ground plane, procedural sky visible, SSAO darkening corners/edges, bloom/glow around bright areas, floating quad showing a live SubViewport torus render. Zero GPU errors in console.


Scene F errors: (only these errors:)
```
installHook.js:1 [Godot] WebGPU uncaptured error: GPUValidationError
overrideMethod @ installHook.js:1Understand this error
installHook.js:1 [UNCAPTURED-GPU-ERROR] [Texture (unlabeled 640x640 px, 7 layer, TextureFormat::RGB10A2Unorm)] usage (TextureBinding|RenderAttachment) includes writable usage and another usage in the same synchronization scope.
 - While validating render pass usage.
 - While finishing [CommandEncoder (unlabeled)].

overrideMethod @ installHook.js:1Understand this error
installHook.js:1 [Godot] WebGPU uncaptured error: GPUValidationError
overrideMethod @ installHook.js:1Understand this error
installHook.js:1 [UNCAPTURED-GPU-ERROR] [Invalid CommandBuffer] is invalid.
 - While calling [Queue].Submit([[Invalid CommandBuffer]])
```

___


Scene E errors: black screen
```
[UNCAPTURED-GPU-ERROR] Writable storage buffer binding aliasing found between [BindGroup (unlabeled)] set at bind group index 0, binding index 1, and [BindGroup (unlabeled)] set at bind group index 1, binding index 2, with overlapping ranges (offset: 0, size: 16) and (offset: 0, size: 16) in [Buffer (unlabeled)].
 - While encoding [ComputePassEncoder (unlabeled)].DispatchWorkgroups(3, 1, 1).
 - While finishing [CommandEncoder (unlabeled)].

(anonymous) @ index.js:1Understand this error
index.js:1 [SUBMIT-ERROR] #1789 bufs=1 err=[Invalid CommandBuffer] is invalid.
 - While calling [Queue].Submit([[Invalid CommandBuffer]])

(anonymous) @ index.js:1
Promise.then
d.queue.submit @ index.js:1
_wgpuQueueSubmit @ index.js:1
$func16169 @ 09cbb00a:0xac83c1
$func47952 @ 09cbb00a:0x16b5b64
$func48083 @ 09cbb00a:0x16db329
$func51387 @ 09cbb00a:0x1835217
$func50056 @ 09cbb00a:0x1789b31
$func50088 @ 09cbb00a:0x178e6a9
$func798 @ 09cbb00a:0x16fedb
$func659 @ 09cbb00a:0xaf044
callUserCallback @ index.js:1
runIter @ index.js:1
MainLoop_runner @ index.js:1
requestAnimationFrame
requestAnimationFrame @ index.js:1
MainLoop_scheduler_rAF @ index.js:1
MainLoop_runner @ index.js:1
requestAnimationFrame
requestAnimationFrame @ index.js:1
MainLoop_scheduler_rAF @ index.js:1
MainLoop_runner @ index.js:1
requestAnimationFrame
requestAnimationFrame @ index.js:1
MainLoop_scheduler_rAF @ index.js:1
MainLoop_runner @ index.js:1
requestAnimationFrame
requestAnimationFrame @ index.js:1
MainLoop_scheduler_rAF @ index.js:1
MainLoop_runner @ index.js:1
requestAnimationFrame
requestAnimationFrame @ index.js:1
MainLoop_scheduler_rAF @ index.js:1
MainLoop_runner @ index.js:1
requestAnimationFrame
requestAnimationFrame @ index.js:1
MainLoop_scheduler_rAF @ index.js:1
MainLoop_runner @ index.js:1
requestAnimationFrame
requestAnimationFrame @ index.js:1
MainLoop_scheduler_rAF @ index.js:1
MainLoop_runner @ index.js:1
requestAnimationFrame
requestAnimationFrame @ index.js:1
MainLoop_scheduler_rAF @ index.js:1
```
