/**
 * WebGPU Startup Profiler for Godot 3D Platformer
 *
 * Launches Chrome via Playwright with WebGPU enabled, clears cache,
 * injects timing instrumentation into createShaderModule / createRenderPipeline,
 * and captures a detailed startup timeline (shader compilation, pipeline creation,
 * FPS stabilization, scene load).
 *
 * Usage:
 *   node profile_startup.mjs [--duration <seconds>] [--output <json-path>]
 */

import { createServer } from 'http';
import { readFileSync, existsSync, statSync, writeFileSync } from 'fs';
import { join, extname, dirname } from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const DEMO_DIR = join(__dirname, 'scene_smoketest', 'exports', 'demo_3d_platformer');
const DURATION_SEC = parseInt(process.argv.find((_, i, a) => a[i - 1] === '--duration') || '30');
const OUTPUT_PATH = process.argv.find((_, i, a) => a[i - 1] === '--output') || join(__dirname, 'startup_profile.json');

const MIME_TYPES = {
    '.html': 'text/html',
    '.js': 'text/javascript',
    '.wasm': 'application/wasm',
    '.pck': 'application/octet-stream',
    '.png': 'image/png',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon',
    '.json': 'application/json',
};

// Instrumentation script injected before Godot loads.
// Monkey-patches GPUDevice to time every createShaderModule and createRenderPipeline call.
const INSTRUMENT_SCRIPT = `
<script>
(function() {
    // Timeline: array of {type, label, startMs, durationMs, ...}
    window._profiling = {
        t0: performance.now(),
        events: [],
        shaderModules: [],    // {id, label, startMs, durationMs}
        renderPipelines: [],  // {id, label, startMs, durationMs, isAsync}
        computePipelines: [], // {id, label, startMs, durationMs}
        frames: [],           // {frameNum, timestampMs, deltaMsFromPrev}
        marks: {},            // named timestamps
        fps_samples: [],      // {timestampMs, fps}
    };

    var P = window._profiling;

    // Record a named mark
    P.mark = function(name) {
        P.marks[name] = performance.now() - P.t0;
        P.events.push({type:'mark', name:name, timestampMs: P.marks[name]});
    };

    // --- Patch requestAdapter to intercept device creation ---
    var origRequestAdapter = navigator.gpu.requestAdapter.bind(navigator.gpu);
    navigator.gpu.requestAdapter = async function(opts) {
        P.mark('requestAdapter_start');
        var adapter = await origRequestAdapter(opts);
        P.mark('requestAdapter_done');

        if (!adapter) return adapter;

        // Patch requestDevice
        var origRequestDevice = adapter.requestDevice.bind(adapter);
        adapter.requestDevice = async function(devOpts) {
            P.mark('requestDevice_start');
            var device = await origRequestDevice(devOpts);
            P.mark('requestDevice_done');

            if (!device) return device;

            // --- Patch createShaderModule ---
            var smId = 0;
            var origCSM = device.createShaderModule.bind(device);
            device.createShaderModule = function(desc) {
                var id = smId++;
                var label = (desc && desc.label) || '(unlabeled)';
                var start = performance.now();
                var mod = origCSM(desc);
                var dur = performance.now() - start;
                var entry = {
                    id: id,
                    label: label,
                    startMs: start - P.t0,
                    durationMs: dur,
                    isSpecialized: label.indexOf('specmod') >= 0,
                    codeLength: (desc && desc.code) ? desc.code.length : 0,
                };
                P.shaderModules.push(entry);
                P.events.push(Object.assign({type:'shaderModule'}, entry));
                return mod;
            };

            // --- Patch createRenderPipeline ---
            var rpId = 0;
            var origCRP = device.createRenderPipeline.bind(device);
            device.createRenderPipeline = function(desc) {
                var id = rpId++;
                var label = (desc && desc.label) || '(unlabeled)';
                var start = performance.now();
                var pipeline = origCRP(desc);
                var dur = performance.now() - start;
                var entry = {
                    id: id,
                    label: label,
                    startMs: start - P.t0,
                    durationMs: dur,
                    isAsync: false,
                };
                P.renderPipelines.push(entry);
                P.events.push(Object.assign({type:'renderPipeline'}, entry));
                return pipeline;
            };

            // --- Patch createRenderPipelineAsync ---
            var origCRPA = device.createRenderPipelineAsync.bind(device);
            device.createRenderPipelineAsync = async function(desc) {
                var id = rpId++;
                var label = (desc && desc.label) || '(unlabeled)';
                var start = performance.now();
                var pipeline = await origCRPA(desc);
                var dur = performance.now() - start;
                var entry = {
                    id: id,
                    label: label,
                    startMs: start - P.t0,
                    durationMs: dur,
                    isAsync: true,
                };
                P.renderPipelines.push(entry);
                P.events.push(Object.assign({type:'renderPipelineAsync'}, entry));
                return pipeline;
            };

            // --- Patch createComputePipeline ---
            var cpId = 0;
            var origCCP = device.createComputePipeline.bind(device);
            device.createComputePipeline = function(desc) {
                var id = cpId++;
                var label = (desc && desc.label) || '(unlabeled)';
                var start = performance.now();
                var pipeline = origCCP(desc);
                var dur = performance.now() - start;
                var entry = {
                    id: id,
                    label: label,
                    startMs: start - P.t0,
                    durationMs: dur,
                };
                P.computePipelines.push(entry);
                P.events.push(Object.assign({type:'computePipeline'}, entry));
                return pipeline;
            };

            // --- Patch createComputePipelineAsync ---
            var origCCPA = device.createComputePipelineAsync.bind(device);
            device.createComputePipelineAsync = async function(desc) {
                var id = cpId++;
                var label = (desc && desc.label) || '(unlabeled)';
                var start = performance.now();
                var pipeline = await origCCPA(desc);
                var dur = performance.now() - start;
                var entry = {
                    id: id,
                    label: label,
                    startMs: start - P.t0,
                    durationMs: dur,
                };
                P.computePipelines.push(entry);
                P.events.push(Object.assign({type:'computePipelineAsync'}, entry));
                return pipeline;
            };

            // --- FPS tracker via requestAnimationFrame ---
            var frameNum = 0;
            var lastFrameTime = performance.now();
            var fpsWindowStart = performance.now();
            var fpsFrameCount = 0;

            function onFrame(now) {
                frameNum++;
                var delta = now - lastFrameTime;
                lastFrameTime = now;
                fpsFrameCount++;

                // Record every frame for the first 5 seconds, then every 10th frame
                if ((now - P.t0) < 5000 || frameNum % 10 === 0) {
                    P.frames.push({
                        frameNum: frameNum,
                        timestampMs: now - P.t0,
                        deltaMs: delta,
                    });
                }

                // Sample FPS every 500ms
                if (now - fpsWindowStart >= 500) {
                    var fps = fpsFrameCount / ((now - fpsWindowStart) / 1000);
                    P.fps_samples.push({
                        timestampMs: now - P.t0,
                        fps: Math.round(fps * 10) / 10,
                    });
                    fpsWindowStart = now;
                    fpsFrameCount = 0;
                }

                requestAnimationFrame(onFrame);
            }
            requestAnimationFrame(onFrame);

            return device;
        };

        return adapter;
    };

    // Mark page load events
    window.addEventListener('DOMContentLoaded', function() { P.mark('DOMContentLoaded'); });
    window.addEventListener('load', function() { P.mark('windowLoad'); });

    // Intercept Godot engine start signals
    var origLog = console.log;
    console.log = function() {
        var msg = Array.prototype.join.call(arguments, ' ');
        if (msg.indexOf('Godot Engine v') >= 0) {
            P.mark('engineStartBanner');
        }
        if (msg.indexOf('[PERF]') >= 0) {
            P.events.push({type:'perf', timestampMs: performance.now() - P.t0, msg: msg});
        }
        origLog.apply(console, arguments);
    };

    console.log('[PROFILER] Instrumentation installed at t0=' + P.t0.toFixed(2));
})();
</script>
`;

// --- HTTP Server with CORS and script injection ---

function startServer(dir) {
    return new Promise((resolve) => {
        const server = createServer((req, res) => {
            const url = req.url.split('?')[0];
            const filePath = join(dir, url === '/' ? 'index.html' : url);

            if (!existsSync(filePath) || statSync(filePath).isDirectory()) {
                res.writeHead(404);
                res.end('Not found');
                return;
            }

            const ext = extname(filePath);
            const headers = {
                'Content-Type': MIME_TYPES[ext] || 'application/octet-stream',
                'Cross-Origin-Opener-Policy': 'same-origin',
                'Cross-Origin-Embedder-Policy': 'require-corp',
            };

            let content = readFileSync(filePath);

            // Inject profiling instrumentation + stress config into HTML
            if (ext === '.html') {
                let html = content.toString('utf8');
                // Inject BEFORE <head> so it runs before index.js
                const stressDisable = '<script>window.stressConfig = { enabled: false };</script>';
                html = html.replace('<head>', '<head>' + INSTRUMENT_SCRIPT + stressDisable);
                content = Buffer.from(html, 'utf8');
            }

            res.writeHead(200, headers);
            res.end(content);
        });

        server.listen(0, '127.0.0.1', () => {
            resolve({ server, port: server.address().port, url: `http://127.0.0.1:${server.address().port}` });
        });
    });
}

// --- Main ---

async function main() {
    if (!existsSync(join(DEMO_DIR, 'index.html'))) {
        console.error('Demo not found at', DEMO_DIR);
        process.exit(1);
    }

    console.log('Starting HTTP server...');
    const { server, url } = await startServer(DEMO_DIR);
    console.log(`Server at ${url}`);

    console.log('Launching Chrome via Playwright...');
    const { chromium } = await import(join(__dirname, 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));

    const browser = await chromium.launch({
        headless: false,  // WebGPU often needs headed mode
        args: [
            '--enable-unsafe-webgpu',
            '--enable-features=Vulkan',
            '--disable-gpu-shader-disk-cache',
            '--disable-gpu-program-cache',
            '--gpu-no-context-lost',
        ],
    });

    const context = await browser.newContext({
        viewport: { width: 1280, height: 720 },
    });

    // Clear all caches / storage
    await context.clearCookies();

    const page = await context.newPage();

    // Collect console output
    const consoleMessages = [];
    page.on('console', (msg) => {
        const text = msg.text();
        consoleMessages.push({ timestampMs: Date.now(), type: msg.type(), text: text.substring(0, 500) });
        // Print key messages
        if (text.includes('[PROFILER]') || text.includes('Godot Engine v') || text.includes('[PERF]') ||
            text.includes('DEVICE LOST') || text.includes('[SHADER]')) {
            console.log(`  [browser] ${text.substring(0, 200)}`);
        }
    });

    page.on('pageerror', (err) => {
        console.log(`  [page error] ${err.message.substring(0, 200)}`);
    });

    const pageLoadStart = Date.now();
    console.log(`Navigating to ${url} ...`);
    await page.goto(url, { waitUntil: 'domcontentloaded' });
    console.log(`Page loaded, waiting ${DURATION_SEC}s for profiling data...`);

    // Wait for the profiling duration
    await new Promise(r => setTimeout(r, DURATION_SEC * 1000));

    console.log('Collecting profiling data...');
    const profilingData = await page.evaluate(() => {
        const P = window._profiling;
        if (!P) return null;
        return {
            t0: P.t0,
            marks: P.marks,
            shaderModules: P.shaderModules,
            renderPipelines: P.renderPipelines,
            computePipelines: P.computePipelines,
            frames: P.frames,
            fps_samples: P.fps_samples,
            events: P.events,
            totalShaderModules: P.shaderModules.length,
            totalRenderPipelines: P.renderPipelines.length,
            totalComputePipelines: P.computePipelines.length,
        };
    });

    if (!profilingData) {
        console.error('No profiling data collected!');
    } else {
        profilingData.consoleMessages = consoleMessages;
        profilingData.durationSec = DURATION_SEC;
        profilingData.pageLoadStartEpoch = pageLoadStart;

        writeFileSync(OUTPUT_PATH, JSON.stringify(profilingData, null, 2));
        console.log(`\nProfiling data saved to ${OUTPUT_PATH}`);

        // Print summary
        printSummary(profilingData);
    }

    await page.close();
    await browser.close();
    server.close();
}

function printSummary(data) {
    console.log('\n' + '='.repeat(70));
    console.log('STARTUP PROFILING SUMMARY');
    console.log('='.repeat(70));

    // Marks
    console.log('\n--- Key Timestamps (ms from t0) ---');
    const sortedMarks = Object.entries(data.marks).sort((a, b) => a[1] - b[1]);
    for (const [name, ms] of sortedMarks) {
        console.log(`  ${ms.toFixed(1).padStart(10)}ms  ${name}`);
    }

    // Shader modules
    console.log(`\n--- Shader Modules (${data.totalShaderModules} total) ---`);
    const baseShaders = data.shaderModules.filter(s => !s.isSpecialized);
    const specShaders = data.shaderModules.filter(s => s.isSpecialized);
    console.log(`  Base (non-specialized): ${baseShaders.length}`);
    console.log(`  Specialized:            ${specShaders.length}`);

    if (baseShaders.length > 0) {
        const baseTotalMs = baseShaders.reduce((s, m) => s + m.durationMs, 0);
        const baseFirst = Math.min(...baseShaders.map(m => m.startMs));
        const baseLast = Math.max(...baseShaders.map(m => m.startMs + m.durationMs));
        console.log(`  Base shaders: first at ${baseFirst.toFixed(1)}ms, last done at ${baseLast.toFixed(1)}ms`);
        console.log(`  Base total compile time: ${baseTotalMs.toFixed(1)}ms (sum of individual durations)`);
        console.log(`  Base wall-clock span:    ${(baseLast - baseFirst).toFixed(1)}ms`);
        const baseAvg = baseTotalMs / baseShaders.length;
        const baseMax = Math.max(...baseShaders.map(m => m.durationMs));
        const baseMin = Math.min(...baseShaders.map(m => m.durationMs));
        console.log(`  Base per-shader: min=${baseMin.toFixed(2)}ms avg=${baseAvg.toFixed(2)}ms max=${baseMax.toFixed(2)}ms`);

        // Top 10 slowest base shaders
        console.log('\n  Top 10 slowest base shaders:');
        const sorted = [...baseShaders].sort((a, b) => b.durationMs - a.durationMs);
        for (const s of sorted.slice(0, 10)) {
            console.log(`    ${s.durationMs.toFixed(2).padStart(8)}ms  ${s.label} (${s.codeLength} chars WGSL)`);
        }
    }

    if (specShaders.length > 0) {
        const specTotalMs = specShaders.reduce((s, m) => s + m.durationMs, 0);
        const specFirst = Math.min(...specShaders.map(m => m.startMs));
        const specLast = Math.max(...specShaders.map(m => m.startMs + m.durationMs));
        console.log(`\n  Specialized shaders: first at ${specFirst.toFixed(1)}ms, last done at ${specLast.toFixed(1)}ms`);
        console.log(`  Specialized total compile time: ${specTotalMs.toFixed(1)}ms`);
        console.log(`  Specialized wall-clock span:    ${(specLast - specFirst).toFixed(1)}ms`);
        const specAvg = specTotalMs / specShaders.length;
        const specMax = Math.max(...specShaders.map(m => m.durationMs));
        console.log(`  Specialized per-shader: avg=${specAvg.toFixed(2)}ms max=${specMax.toFixed(2)}ms`);
    }

    // Render pipelines
    console.log(`\n--- Render Pipelines (${data.totalRenderPipelines} total) ---`);
    if (data.renderPipelines.length > 0) {
        const syncPipes = data.renderPipelines.filter(p => !p.isAsync);
        const asyncPipes = data.renderPipelines.filter(p => p.isAsync);
        console.log(`  Sync:  ${syncPipes.length}`);
        console.log(`  Async: ${asyncPipes.length}`);

        if (syncPipes.length > 0) {
            const totalMs = syncPipes.reduce((s, p) => s + p.durationMs, 0);
            const first = Math.min(...syncPipes.map(p => p.startMs));
            const last = Math.max(...syncPipes.map(p => p.startMs + p.durationMs));
            console.log(`  Sync pipelines: first at ${first.toFixed(1)}ms, last done at ${last.toFixed(1)}ms`);
            console.log(`  Sync total create time: ${totalMs.toFixed(1)}ms`);
            console.log(`  Sync wall-clock span:   ${(last - first).toFixed(1)}ms`);
            const avg = totalMs / syncPipes.length;
            const max = Math.max(...syncPipes.map(p => p.durationMs));
            console.log(`  Sync per-pipeline: avg=${avg.toFixed(2)}ms max=${max.toFixed(2)}ms`);

            // Top 10 slowest
            console.log('\n  Top 10 slowest sync render pipelines:');
            const sorted = [...syncPipes].sort((a, b) => b.durationMs - a.durationMs);
            for (const p of sorted.slice(0, 10)) {
                console.log(`    ${p.durationMs.toFixed(2).padStart(8)}ms  ${p.label}`);
            }
        }
    }

    // Compute pipelines
    if (data.totalComputePipelines > 0) {
        console.log(`\n--- Compute Pipelines (${data.totalComputePipelines} total) ---`);
        const totalMs = data.computePipelines.reduce((s, p) => s + p.durationMs, 0);
        const first = Math.min(...data.computePipelines.map(p => p.startMs));
        const last = Math.max(...data.computePipelines.map(p => p.startMs + p.durationMs));
        console.log(`  First at ${first.toFixed(1)}ms, last done at ${last.toFixed(1)}ms`);
        console.log(`  Total create time: ${totalMs.toFixed(1)}ms`);
    }

    // FPS stabilization
    console.log('\n--- FPS Over Time ---');
    if (data.fps_samples.length > 0) {
        for (const s of data.fps_samples) {
            const bar = '#'.repeat(Math.min(Math.round(s.fps), 80));
            console.log(`  ${(s.timestampMs / 1000).toFixed(1).padStart(6)}s  ${s.fps.toFixed(1).padStart(6)} fps  ${bar}`);
        }

        // Find when FPS stabilizes above 50
        const stableIdx = data.fps_samples.findIndex((s, i) => {
            if (i < 2) return false;
            return s.fps >= 50 && data.fps_samples[i - 1].fps >= 50;
        });
        if (stableIdx >= 0) {
            console.log(`\n  FPS stabilizes above 50 at ~${(data.fps_samples[stableIdx].timestampMs / 1000).toFixed(1)}s`);
        }
    }

    // Overall timeline
    console.log('\n--- Overall Timeline ---');
    const allShaderEnd = data.shaderModules.length > 0
        ? Math.max(...data.shaderModules.map(s => s.startMs + s.durationMs))
        : 0;
    const allPipeEnd = data.renderPipelines.length > 0
        ? Math.max(...data.renderPipelines.map(p => p.startMs + p.durationMs))
        : 0;
    console.log(`  All shader modules done by:   ${allShaderEnd.toFixed(1)}ms (${(allShaderEnd / 1000).toFixed(2)}s)`);
    console.log(`  All render pipelines done by:  ${allPipeEnd.toFixed(1)}ms (${(allPipeEnd / 1000).toFixed(2)}s)`);
    if (data.marks.engineStartBanner) {
        console.log(`  Engine start banner at:        ${data.marks.engineStartBanner.toFixed(1)}ms (${(data.marks.engineStartBanner / 1000).toFixed(2)}s)`);
    }
}

main().catch(e => {
    console.error(e);
    process.exit(1);
});
