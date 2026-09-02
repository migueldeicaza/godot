# Pre-Launch TODO — Godot WebGPU

Personal reference: items to address before public release (5 days).

---

## Priority 1: Should Fix Before Launch

### Production Logging Left On
- **Issue**: Performance counter logging runs every second unconditionally via `EM_ASM`, printing to browser console in production builds
- **Location**: `drivers/webgpu/rendering_device_driver_webgpu.cpp` (begin_segment / perf counters)
- **Fix**: Gate behind `WEBGPU_VERBOSE` or a runtime project setting, or remove entirely for release
- **Effort**: ~10 minutes

### Stale Comment in Shader Container
- **Issue**: Comment says "Dawn's emdawnwebgpu port natively supports WGPUShaderSourceSPIRV" but driver actually does SPIR-V→WGSL conversion. Contradicts actual behavior.
- **Location**: `rendering_shader_container_webgpu.cpp`
- **Fix**: Update comment to accurately describe runtime conversion
- **Effort**: ~2 minutes

### Stale Path References
- **Issue**: Comments reference `tmp/naga-converter/src/lib.rs` but actual path is `drivers/webgpu/naga-converter/src/lib.rs`
- **Location**: `rendering_shader_container_webgpu.cpp:69`, `rendering_device_driver_webgpu.h:109`
- **Fix**: Update paths
- **Effort**: ~2 minutes

### Remove `out/` Directory
- **Issue**: `drivers/webgpu/naga-converter/out/` is a stale older build output (1,070,564 bytes), superseded by `prebuilt/` (1,053,644 bytes)
- **Fix**: Delete `out/` directory
- **Effort**: ~1 minute

### Remove Viewport Diagnostic Stub
- **Issue**: Empty diagnostic block in `servers/rendering/renderer_viewport.cpp` (remnant of debugging)
- **Fix**: Remove empty braces
- **Effort**: ~1 minute

---

## Priority 2: Nice to Fix, Not Blocking

### Unused `glsl-out` Feature in Cargo.toml
- **Issue**: naga-converter enables `glsl-out` feature but never uses GLSL output. Adds ~50-100KB to WASM binary.
- **Location**: `drivers/webgpu/naga-converter/Cargo.toml`
- **Fix**: Remove `"glsl-out"` from features, rebuild WASM
- **Effort**: ~15 minutes (need to rebuild + test)

### Duplicate Uncaptured Error Listeners
- **Issue**: Both JS engine.js AND C++ EM_ASM attach `uncapturederror` listeners, causing double-logged errors
- **Fix**: Remove the C++ side listener (the JS one is sufficient)
- **Effort**: ~5 minutes

### Emscripten Version Check for emdawnwebgpu
- **Issue**: General minimum is Emscripten 4.0.0, but emdawnwebgpu needs 4.0.10+. Users with 4.0.0-4.0.9 get unhelpful "port not found" error.
- **Location**: `platform/web/detect.py`
- **Fix**: Add `if cc_semver < (4, 0, 10) and env["webgpu"]: print_error(...)`
- **Effort**: ~5 minutes

### Duplicated Format Remapping Logic
- **Issue**: Format remapping code (~150 lines) is copy-pasted between `shader_create_from_container` and `_create_module_with_spec_constants`. Any fix must be applied twice.
- **Fix**: Extract into a shared helper function
- **Effort**: ~30 minutes

---

## Priority 3: Post-Launch / Future Work

### Shader Startup Time (~15s)
- **Status**: Known limitation. 383 shader stages × ~40ms each = ~15s blocking on main thread
- **Future fix**: Pre-compile WGSL at export time, ship directly. Runtime conversion only for spec constant variants.
- **Effort**: Significant architectural change (multi-day)

### Skeleton Atlas Defragmentation
- **Status**: Bump allocator never frees. Long-running dynamic scenes may waste memory.
- **Future fix**: Free-list or generational compaction
- **Effort**: ~half day

### WGSL String Patching → Rust
- **Status**: Format remapping and binding_array removal done via fragile C++ string manipulation
- **Future fix**: Move these transforms into naga-converter as AST operations
- **Effort**: ~2-3 days

### Device Loss Recovery
- **Status**: Logged but not handled gracefully. No user notification.
- **Future fix**: Error overlay in HTML shell, or attempt device re-creation
- **Effort**: ~half day

### `command_render_clear_attachments` Implementation
- **Status**: Silently ignored (WARN_PRINT_ONCE). Rarely needed in Forward Mobile.
- **Future fix**: End pass → clear pass → restart pattern
- **Effort**: ~half day

### CI for Prebuilt Verification
- **Status**: No automated check that prebuilt naga WASM matches source (note: broader test CI exists in `.github/workflows/webgpu_tests.yml`)
- **Future fix**: CI job that rebuilds and verifies (or at least compiles successfully)
- **Effort**: ~half day for CI setup

### Canvas Selector Flexibility
- **Status**: Hardcoded to `#canvas`
- **Future fix**: Wire up `p_platform_data` parameter
- **Effort**: ~1 hour

---

## Documentation for Launch

### Already Done
- [x] Category review docs (8 files in review_v3/)
- [x] Final synthesis docs (4 files in review_v3/)
- [x] DESIGN.md and IMPLEMENTATION.md in webgpu_notes/
- [x] drivers/webgpu/README.md

### For godotwebgpu.com
- [ ] Port FINAL_1 (Architecture) to website format
- [ ] Port FINAL_2 (Technical Reference) to website format
- [ ] Port FINAL_3 (Performance) to website format
- [ ] Port FINAL_4 (Correctness) to website format
- [ ] Create user-facing "Getting Started" guide
- [ ] Browser compatibility table
- [ ] Known limitations page
- [ ] Performance comparison visuals/charts

### For Community/Godot Team
- [ ] PR description (can base on webgpu_notes/PR_DESCRIPTION.md)
- [ ] Release notes highlighting key features
- [ ] Shadow quality trade-off documentation
- [ ] Custom shader compatibility notes (canvas binding shift)

---

## Quick Sanity Checks Before Ship

- [ ] Build succeeds: `scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no`
- [ ] Build succeeds: `scons platform=web target=template_debug dlink_enabled=yes webgpu=yes`
- [ ] 3D scene renders correctly in Chrome
- [ ] 3D scene renders correctly in Firefox
- [ ] 3D scene renders correctly in Safari (if available)
- [ ] 2D scene renders correctly
- [ ] No console errors in production build (after fixing perf logging)
- [ ] Shader startup completes without errors
- [ ] Animation/skeleton rendering works
- [ ] Shadow rendering works (dual-paraboloid)
- [ ] Viewport capture/screenshot works (async readback)
- [ ] No memory growth over time in a static scene (check for leaks)
- [ ] Large scene stress test (many instances) achieves expected performance
