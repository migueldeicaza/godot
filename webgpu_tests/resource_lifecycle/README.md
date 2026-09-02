# Resource Lifecycle Stress Test

Browser-based WebGPU stress test that validates the driver handles rapid resource creation/destruction without leaks, crashes, or use-after-free errors.

## Tests

| Test | What it validates |
|------|------------------|
| **Buffer Storm** | 10,000 rapid buffer create/destroy cycles + 1,000 simultaneous allocations |
| **Texture Churn** | 2,000 textures with varying formats/dimensions + array textures with per-layer views |
| **Bind Group Storm** | 5,000 bind group creations testing BGL caching and reference counting |
| **Pipeline Churn** | 500 render/compute pipelines with varying topology/cull state |
| **Async Map + Destroy** | 200 buffers mapped then destroyed mid-flight ("freed while pending" pattern) |
| **Mixed Churn** | 5 seconds of interleaved create/destroy across all resource types |

## Running

### Browser (manual)
```bash
# Start local server
node run_tests.mjs --serve-only
# Open http://127.0.0.1:<port> in Chrome/Edge with WebGPU
```

### Headless (Playwright)
```bash
npm install playwright
npx playwright install chromium
node run_tests.mjs
```

### Auto-run via URL
Append `?autorun` to the URL to run all tests automatically on page load.

## What it catches

- Handle leaks (create count != destroy count)
- Device lost errors from resource exhaustion
- Crashes from use-after-free (async map + destroy race)
- Bind group layout cache invalidation bugs
- Pipeline compilation failures from state permutations
- Memory pressure from sustained allocation churn

## Pass criteria

All 6 tests must complete without uncaught errors. The async map test accepts both map-success and map-rejection as valid outcomes (implementation-defined behavior per the WebGPU spec).
