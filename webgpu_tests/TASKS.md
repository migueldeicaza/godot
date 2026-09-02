# WebGPU Test Suite: Status & Tasks

## Testing Coverage: What Exists vs What's Lacking

| Category | Status | What Exists | What's Missing |
|----------|--------|-------------|----------------|
| **Shader pipeline (SPIR-V -> WGSL)** | Done | 9 hand-crafted GLSL fixtures compiled & validated through Tint WASM | Fuzz testing of the shader pipeline (random/malformed SPIR-V inputs) |
| **Full engine SPIR-V validation** | Done | All 309 engine-compiled shaders validated offline via `validate_spirv_dump.mjs` with expected-failures baseline | — |
| **End-to-end smoke test** | Done | Headless Chrome runs exported Godot project, checks no shader errors / device-lost | Multi-browser smoke test (Firefox/Safari headless) |
| **Resource lifecycle stress** | Done | Rapid create/destroy of buffers, textures, pipelines in standalone browser test | — |
| **Screenshot comparison** | Done | Visual regression across Chrome + Firefox (warning-only in CI) | Merge-blocking visual regression; Safari baselines |
| **Scene smoketest** | Done | Multiple scene exports validated in headless browser (`scene_smoketest/`) | — |
| **CI pipeline** | Done | GitHub Actions workflow with parallel jobs, path-triggered, merge-gating | — |
| **Shader coverage scene** | Done | GDScript scene exercising 100% of RenderingDevice shader paths (all material/lighting/post-process combos) | — |
| **Unit tests (driver)** | Done | 305 JS tests covering ring buffer, shadow buffer, bind group layout, pipeline hashing, command buffer, format mapping, buffer alignment, std140 packing, texture layout, texture conversion, bind group compatibility | — |
| **Unit tests (SPIR-V preprocessing)** | Done | 191 JS tests covering all 12 SPIR-V rewriting passes, end-to-end SPIR-V → WGSL, and error cases | — |
| **Regression test suite** | Missing | No named/tracked regression cases | A suite of known-bug repros that run in CI to prevent recurrence |
| **Fuzz testing** | Done | 3 C++ fuzz targets (spirv_to_wgsl, preprocess_passes, split_samplers) with seeded corpus; found & fixed 4 bugs | — |
| **Multi-browser CI** | Done | Scene smoketest runs 18 scenes on Chrome + Firefox + Safari; CI runs Chrome + Firefox | Safari CI automation (requires macOS runner) |
| **Performance benchmarks** | Missing | *Nothing* | Frame-time / draw-call benchmarks to catch performance regressions (shader compile time, IPC overhead, buffer upload) |
| **Async readback tests** | Missing | *Nothing dedicated* | Test viewport capture, GPU->CPU buffer readback timing, fence/callback correctness |
| **Error path testing** | Missing | *Nothing* | Verify graceful handling of device-lost, OOM, invalid API usage, context loss recovery |

## Task Breakdown

### 1. Unit tests: SPIR-V preprocessing (JS) — DONE

191 JavaScript tests in `webgpu_tests/preprocessing_tests/run_tests.mjs`:

- [x] `freeze_spec_constant_ops` — tests for no-op, rewrite, evaluate IAdd, bool, SpecId stripping, composites
- [x] `rewrite_copy_logical` — tests for no-op, single/multiple replace, preserves others, too-small input
- [x] `rewrite_terminate_invocation` — tests for no-op, single/multiple replace, too-small input
- [x] `infer_readonly_storage` — tests for no storage, adds NonWritable, written var, access-chain write, no duplicate, mixed
- [x] `convert_push_constants_to_uniforms` — tests for no push constants, rewrites storage class, injects decorations
- [x] `split_combined_samplers` — tests for no combined, basic split
- [x] `fix_depth2_images` — tests for depth=0 unchanged, depth=2→1, depth=1 preserved
- [x] `eval_spec_op` — tests for arithmetic, logical, select, comparisons, bitwise, unknown
- [x] `fix_nonfinite_literals` — tests for positive, negative, no-match
- [x] `flatten_binding_arrays` — tests for array flattening, access chain rewriting
- [x] `negate_position_y` — tests for Y-flip insertion
- [x] `strip_restrict_decoration` — tests for restrict removal
- [x] End-to-end and edge cases

### 2. Unit tests: WebGPU driver (JS isolation) — DONE

191 JavaScript unit tests in `webgpu_tests/driver_unit_tests/` (algorithms extracted from C++ driver):

- [x] Ring buffer allocation and wrap-around (17 tests)
- [x] Shadow buffer copy correctness (16 tests)
- [x] Descriptor set / bind group layout generation (25 tests)
- [x] Pipeline state hashing and caching (12 tests)
- [x] Command buffer encoding sequences (28 tests)
- [x] Texture format mapping (Godot format → WebGPU format) (23 tests)
- [x] Buffer alignment and offset calculations (33 tests)
- [x] Uniform buffer packing (std140 layout) (23 tests)

### 3. Fuzz testing (SPIR-V preprocessing) — DONE

3 C++ fuzz targets in `drivers/webgpu/tint_cli/fuzz/`:

- [x] `spirv_to_wgsl` — full pipeline (all 12 preprocessing passes + Tint SPIR-V reader + WGSL writer)
- [x] `preprocess_passes` — all 12 SPIR-V rewriting passes chained, no Tint parsing
- [x] `split_samplers` — most complex pass in isolation
- [x] Seeded corpus from engine shader fixtures
- [x] CI job runs fuzz targets on every push/PR

### 4. Regression test suite

- [ ] Collect known-bug SPIR-V samples that previously caused failures
- [ ] Add a `regressions/` directory with named test cases
- [ ] Each test case: input `.spv` + expected outcome (pass/fail/specific WGSL output)
- [ ] CI validates no regressions reappear

### 5. Multi-browser smoke tests — DONE

- [x] Add Firefox to the scene smoketest (Playwright with `dom.webgpu.enabled`)
- [x] Add Safari to the scene smoketest (real Safari via AppleScript, macOS only)
- [x] Update CI workflow to run scene smoketest on Chrome + Firefox
- [ ] Safari CI automation (requires macOS runner — currently local-only)

### 6. Performance benchmarks

- [ ] Shader compile time benchmark (measure Tint conversion throughput)
- [ ] Frame-time benchmark scene (stable scene, measure ms/frame variance)
- [ ] Buffer upload throughput benchmark
- [ ] CI job that runs benchmarks and reports regressions (not merge-blocking, warning-only)

### 7. Error path testing

- [ ] Device-lost simulation and recovery
- [ ] OOM handling (allocate until failure, verify graceful degradation)
- [ ] Invalid API usage (e.g., submit destroyed buffer) — verify no crash
- [ ] Context loss and re-creation

### 8. Async readback tests

- [ ] Viewport capture correctness (render known scene, read back pixels, verify)
- [ ] GPU->CPU buffer readback timing (verify callbacks fire)
- [ ] Fence/sync correctness under rapid submit cycles
