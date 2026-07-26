# Experimental Naga shader backend

This optional module lets the Metal renderer attempt GLSL-to-MSL translation with
[Naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga) for Godot's built-in forward
Uber Shaders. It is intentionally not a replacement for Godot's shader validation or
the general GLSLang/SPIRV-Cross path.

Build on macOS with Rust 1.87 or newer:

```sh
scons platform=macos naga_enabled=yes
```

At runtime, enable `rendering/shader_compiler/metal/use_naga_for_ubershaders` in the
project settings. Translation failures automatically use the existing compiler path.
For one-off testing and benchmarks, `GODOT_NAGA_UBERSHADERS=1` enables the same path
without changing a project file.
`GODOT_NAGA_TEST_ALL_FORWARD_VARIANTS=1` additionally routes non-Uber forward
variants through Naga, which is useful only for exercising the fallback boundary.

The bridge applies Godot's Vulkan-to-Metal vertex-Y convention before parsing and
strips desktop-GLSL precision qualifiers that Naga does not accept in structure
members. It uses flat Metal resource slots. The current proof of concept retains
GLSLang output as reflection metadata and as a Naga SPIR-V-frontend fallback for
GLSL constructs Naga cannot yet parse; successful output is never passed through
SPIRV-Cross.

Naga 29 and current Naga trunk do not yet emit MSL for two constructs present in
the forward Uber Shaders: specialization overrides and matrix inverse operations.
The smoke test therefore currently demonstrates selection and safe fallback rather
than a fully direct Uber Shader. Those are the next two backend gaps to close (or
polyfill) before enabling this by default.

Run the included Metal smoke test with verbose compiler selection logging:

```sh
GODOT_NAGA_UBERSHADERS=1 bin/godot.macos.editor.dev.arm64 \
  --path modules/naga/tests --rendering-method forward_plus \
  --rendering-driver metal --quit-after 2 --verbose
```
