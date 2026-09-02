**Godot WebGPU Backend for Web Exports – Complete AI Agent Project Brief**
**Target Completion: March 24, 2026 (2 weeks from March 10)**
**Goal:** Add an official WebGPU driver to Godot’s RenderingDevice abstraction so web exports can use the **Forward+** or **Mobile** renderers instead of being locked to the Compatibility (WebGL 2.0) renderer. This is the single biggest performance win for Godot on browsers (closing much of the gap to Three.js).

### 1. Current Status (March 10, 2026)
- Godot 4.6 / 4.7 dev still has **zero official WebGPU support**. Docs state explicitly: “Godot currently does not support WebGPU, which is a prerequisite for allowing Forward+/Mobile to run on the web platform.”
- Proposal #6646 remains **open** and tagged “implementer wanted”. No core PR exists.
- Best starting point: **davnotdev/godot** fork on the `webgpu` branch (uses wgpu + Dawn). It has native 2D working (24/26 samples) in older commits, but browser/WASM support is “heavy WIP” and not functional yet. Last visible progress ~late 2024/early 2025.
- Browser support for WebGPU is now mature across Chrome/Edge/Firefox/Safari.

### 2. High-Level Architecture (How It Must Work)
Godot’s modern renderers already target **RenderingDevice** (abstract API used by Vulkan/Metal).
We need:
1. A new **WebGPU RenderingDevice driver** (C++ + Emscripten glue).
2. Shader translator: Godot’s SPIR-V/GLSL → WGSL (via Tint/Dawn).
3. Integration with Emscripten so `platform=web` builds produce `.wasm` + JS that calls `navigator.gpu`.
4. Fallback path to WebGL 2.0 (Compatibility renderer) when WebGPU is unavailable.
5. Build system updates (SCons) + new export template option.

Result: A project using Forward+ or Mobile will export to web and run with dramatically lower CPU overhead, better batching, and compute shaders.

### 3. Detailed Task Breakdown (Phased – 2-Week Sprint)
**Phase 0 – Setup & Fork (Day 1)**
- Fork `godotengine/godot` and merge in `davnotdev/godot:webgpu` as base.
- Create a clean `webgpu-web` branch.
- Set up CI (GitHub Actions) that builds web templates.

**Phase 1 – RenderingDevice WebGPU Driver Skeleton (Days 2-4)**
- Implement `RenderingDevice` interface for WebGPU (mirroring Vulkan driver).
- Add Emscripten WebGPU bindings (use Dawn or wgpu-native via Rust → WASM).
- Get a blank window + clear color running in browser.
- Success: `scons platform=web target=template_release` produces a working WebGPU template that shows a colored screen.

**Phase 2 – Shader & Resource Translation (Days 5-7)**
- Build shader compiler pipeline (SPIR-V → WGSL).
- Implement core objects: Buffers, Textures, Samplers, Pipelines, Compute shaders.
- Port basic 2D rendering (CanvasItem).
- Success: All official 2D demo projects run at 60 FPS in browser with WebGPU.

**Phase 3 – 3D Forward+ / Mobile Parity (Days 8-10)**
- Port 3D rendering path (clusters, lights, shadows).
- Handle browser-specific limits (max textures, no push constants → use uniform buffers).
- Add async buffer/texture upload support (already partially in Godot).
- Success: Standard 3D test scenes (e.g., godot-demo-projects 3D) run with Forward+ on web.

**Phase 4 – Export Integration & Polish (Days 11-12)**
- Add export preset option “WebGPU (Experimental)”.
- Update HTML shell + JS glue for WebGPU detection/fallback.
- Add documentation and editor warnings.
- Success: One-click export from editor produces WebGPU build.

**Phase 5 – Testing & Verification (Days 13-14)**
- See full Testing Plan below.

### 4. Testing & Verification Plan
**Automated Tests (must pass before merge)**
1. Build tests: `scons platform=web target=template_release` + `template_debug` succeed.
2. Unit tests: Run Godot’s existing RenderingDevice tests against new backend.
3. 2D/3D demo suite: All 26 official 2D samples + 10 standard 3D samples must run at ≥58 FPS on Chrome + Firefox (measure with Godot profiler + browser dev tools).
4. Performance baseline:
   - Compare FPS/draw-call count vs. current WebGL 2.0 export.
   - Target: 1.5–3× higher throughput in GPU-bound scenes.
   - Compare to Three.js WebGPU equivalent scene (same geometry/lights).

**Manual / Real-World Verification**
- Export 3 complex test projects (your spinning cube, a medium 3D scene, a particle-heavy scene).
- Test on: Chrome, Firefox, Safari (desktop + mobile).
- Measure: Load time, steady FPS, memory usage, GPU utilization (browser task manager).
- Edge cases: WebGPU disabled → falls back to WebGL 2.0 cleanly; browser limits hit → graceful degradation.
- Profile CPU time in `RenderingServer` – should be dramatically lower than current web export.

**Success Criteria for “Done by March 24”**
- WebGPU export works for Forward+ projects.
- At least 2× effective performance uplift vs. current web builds on complex scenes.
- Clean PR-ready branch with tests passing and docs updated.
- Video demo of a Forward+ scene running in browser at 60 FPS.

### 5. Setup for AI Agents – Maximum Autonomy
To let agents work 24/7 without you babysitting:

**Hardware / Server (Recommended)**
- Dedicated Linux VM or bare-metal server (Ubuntu 22.04/24.04) with:
  - Modern GPU (NVIDIA/AMD – for native validation; WebGPU testing can be CPU-only but faster with GPU).
  - 32+ GB RAM, 8+ cores.
  - ~100 GB SSD.
- Install once: Emscripten 3.1+, Python 3.12, SCons, Git, Docker.

**Recommended Stack for Agents**
1. **Primary workspace**: GitHub repo (your fork).
2. **Agent tools**:
   - Give each agent full SSH + sudo on the server.
   - Use Cursor.sh, Aider, or Continue.dev with full repo access.
   - Or run Claude/Codeium/Grok agents via API with persistent terminal access.
3. **Docker environment** (highly recommended for reproducibility):
   - Provide a `Dockerfile` + `docker-compose.yml` that includes:
     - Godot build deps
     - Emscripten SDK
     - Dawn/Tint (C++20)
     - Browser testing (Playwright + headless Chrome/Firefox)
   - Agents can spin up fresh containers for each major change.
4. **Persistent state**:
   - Shared volume for builds and test exports.
   - Daily backup script of the branch.

**Orchestration Instructions for You (the Human)**
- Create a private GitHub repo and invite the agents (via GitHub Copilot Workspace, Cursor team, or custom agent accounts).
- Give them a shared Notion/Google Doc or Discord channel for daily stand-ups (auto-generated summary).
- Daily workflow you enforce:
  1. Agent pushes to feature branch.
  2. CI builds web template.
  3. Agent runs automated test suite and posts results.
  4. You review only the 5-minute video/demo at end of day.
- Weekly checkpoint: Full performance comparison video.

### 6. Resources & Starting Links
- Proposal: https://github.com/godotengine/godot-proposals/issues/6646
- davnotdev fork (best base): https://github.com/davnotdev/godot/tree/webgpu
- Godot RenderingDevice docs & Vulkan driver (reference implementation)
- Emscripten WebGPU example: https://github.com/emscripten-core/emscripten/tree/main/test/webgpu
- Dawn & wgpu documentation
- Godot compiling for web: https://docs.godotengine.org/en/stable/development/compiling/compiling_for_web.html
- Three.js WebGPU renderer (for performance target comparison)

### 7. Risks & Blockers (and Mitigations)
- Browser limits (textures, push constants) → Use uniform buffers; add fallback paths.
- Build complexity → Use the existing davnotdev fork + Docker.
- Shader translation bugs → Start with 2D, then 3D.
- Time pressure → Prioritize 2D → basic 3D → full Forward+.

Copy-paste this entire document to your AI agents as the master spec.
They now have everything needed to start autonomously.

I’ll be here on March 24 (or earlier if you ping me with progress).
You’ve got this — 2 weeks is aggressive but achievable with focused agents and the davnotdev foundation already in place.

Let’s make Godot web competitive with Three.js. 🚀
