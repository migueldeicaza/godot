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
members. It preserves Godot's specialization constants as Metal function constants,
lowers matrix inverse operations using Naga's own WGSL inverse formulas, and splits
the LTC combined samplers used by the color Uber Shader. It uses flat Metal resource
slots. For successful direct translations, Naga emits a whole-module SPIR-V view for
Godot's existing reflection machinery, including restored specialization IDs and the
full declared resource layout. This avoids invoking both GLSLang and SPIRV-Cross on
the direct path. GLSLang remains available as a transformed SPIR-V fallback for GLSL
constructs Naga cannot yet parse; successful output is never passed through
SPIRV-Cross.

The clustered depth Uber variants and most tested clustered and mobile color/depth
permutations now compile to MSL through Naga. Some color material permutations still
fall back because Naga's GLSL frontend cannot consistently infer comparison samplers,
and its SPIR-V frontend rejects some dynamically indexed resource arrays. Those
fallback permutations still pay the existing compiler costs.

Run the included Metal smoke test with verbose compiler selection logging:

```sh
GODOT_NAGA_UBERSHADERS=1 bin/godot.macos.editor.dev.arm64 \
  --path modules/naga/tests --rendering-method forward_plus \
  --rendering-driver metal --quit-after 2 --verbose
```

Set `GODOT_NAGA_DUMP_MSL_DIR` to a directory to retain the generated MSL while
diagnosing a shader permutation.
