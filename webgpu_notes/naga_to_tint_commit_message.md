# Tint Migration Commit Message Draft

## Short form (for `git commit -m`)

```
feat(webgpu): replace naga WASM with Tint for SPIR-V→WGSL translation
```

## Full form

```
feat(webgpu): replace naga WASM with Tint for SPIR-V→WGSL translation

Replace the Rust/WASM naga-converter with Google's Tint (from Dawn) for
translating SPIR-V shaders to WGSL in the WebGPU rendering backend.

Tint is compiled as native C++20, eliminating the ~2 MB naga WASM blob,
the Emscripten WASM-loading infrastructure in engine.js, and the Rust
build dependency. Translation is faster and Tint's output is more
compatible with browser WebGPU implementations.

New third-party dependencies:
- tint: extracted from Dawn (db49a549, BSD-3-Clause), 815 source files
  with 6 small patches for Godot-specific needs (UBO layout, spec
  constants, point size, abseil removal)
- spirv-tools: SPIR-V validation/optimization required by Tint
  (3605cce5, Apache 2.0)
- spirv-headers: updated to Dawn-pinned version (ad9184e7) with
  additional headers for spirv-tools

Driver changes:
- Add tint_wrapper.h/cpp: C-compatible wrapper isolating C++20/Tint API
- Add 11 SPIR-V preprocessing passes in spirv_preprocess.cpp covering
  spec constants, push constants, combined samplers, Y-flip, binding
  arrays, and other Vulkan→WebGPU fixups
- Update rendering_device_driver_webgpu.cpp with new translation
  pipeline and WGSL binding parser
- Remove ~120 lines of naga WASM loader from engine.js

The naga-converter source is retained for reference and will be removed
in a follow-up commit.
```

## Notes

- The naga code (drivers/webgpu/naga-converter/, platform/web/naga_translator.wasm)
  is intentionally kept in this commit and removed separately
- webgpu_notes/ is only on the working branch, not included in the mainline PR
- Rebuild and full CI pass required before committing
