#!/usr/bin/env node
/**
 * Safari WebGPU diagnostic — tests override support and captures shader failures.
 * Runs one scene with enhanced instrumentation to identify what's failing.
 */

import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname, dirname } from 'path';
import { fileURLToPath } from 'url';
import { execSync } from 'child_process';

const __dirname = dirname(fileURLToPath(import.meta.url));
const EXPORTS_DIR = join(__dirname, 'exports');

const MIME_TYPES = {
    '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
    '.pck': 'application/octet-stream', '.png': 'image/png', '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon', '.json': 'application/json',
};

// Enhanced diagnostic script that hooks createShaderModule and createRenderPipeline
const DIAG_SCRIPT = `
<script>
(function() {
    window._diag = {
        shaderModules: [],       // { id, codeSnippet, compilationMessages }
        pipelineErrors: [],      // { vertexModuleId, fragmentModuleId, error }
        overrideTest: null,      // result of minimal override test
        gpuErrors: [],
        allErrors: [],
        shaderFails: [],
    };
    window._webgpuDebug = window._diag;

    var origError = console.error;
    console.error = function() {
        var msg = Array.prototype.join.call(arguments, ' ');
        window._diag.allErrors.push(msg.substring(0, 500));
        origError.apply(console, arguments);
    };

    if (typeof navigator !== 'undefined' && navigator.gpu) {
        var origRequestAdapter = navigator.gpu.requestAdapter.bind(navigator.gpu);
        navigator.gpu.requestAdapter = function() {
            return origRequestAdapter.apply(navigator.gpu, arguments).then(function(adapter) {
                if (!adapter) return adapter;
                var origRequestDevice = adapter.requestDevice.bind(adapter);
                adapter.requestDevice = function() {
                    return origRequestDevice.apply(adapter, arguments).then(function(device) {
                        if (!device) return device;

                        // Hook uncaptured errors
                        device.addEventListener('uncapturederror', function(e) {
                            var msg = e.error ? (e.error.message || String(e.error)) : 'unknown';
                            window._diag.gpuErrors.push(msg.substring(0, 500));
                        });

                        // Run a suite of targeted tests to isolate Safari issues
                        window._diag.tests = {};

                        function testPipeline(name, code, constants) {
                            try {
                                var mod = device.createShaderModule({ code: code });
                                mod.getCompilationInfo().then(function(info) {
                                    var errors = info.messages.filter(function(m) { return m.type === 'error'; });
                                    if (errors.length > 0) {
                                        window._diag.tests[name] = { result: 'MODULE_ERROR', errors: errors.map(function(m) { return m.message.substring(0, 200); }) };
                                        return;
                                    }
                                    // Try creating a pipeline
                                    device.pushErrorScope('validation');
                                    try {
                                        var desc = {
                                            layout: 'auto',
                                            vertex: { module: mod, entryPoint: 'main' }
                                        };
                                        if (constants) desc.vertex.constants = constants;
                                        device.createRenderPipeline(desc);
                                    } catch(e) {
                                        device.popErrorScope();
                                        window._diag.tests[name] = { result: 'SYNC_ERROR', error: e.message };
                                        return;
                                    }
                                    device.popErrorScope().then(function(err) {
                                        window._diag.tests[name] = { result: err ? 'ASYNC_ERROR' : 'PASS', error: err ? (err.message || String(err)).substring(0, 300) : null };
                                    });
                                });
                            } catch(e) {
                                window._diag.tests[name] = { result: 'EXCEPTION', error: e.message };
                            }
                        }

                        // Test 1: Minimal override + used
                        testPipeline('1_minimal_override_used',
                            '@id(0) override val: u32 = 42u;\\n@vertex fn main() -> @builtin(position) vec4<f32> { let x = val; return vec4<f32>(0.0, 0.0, 0.0, 1.0); }',
                            {"0": 0});

                        // Test 2: Minimal override + UNUSED
                        testPipeline('2_minimal_override_unused',
                            '@id(0) override val: u32 = 42u;\\n@vertex fn main() -> @builtin(position) vec4<f32> { return vec4<f32>(0.0, 0.0, 0.0, 1.0); }',
                            {"0": 0});

                        // Test 3: gl_PerVertex struct (@builtin in private var) WITHOUT override
                        testPipeline('3_glPerVertex_no_override',
                            'struct PV { @builtin(position) p: vec4<f32>, ps: f32, cd: array<f32,1>, cu: array<f32,1> }\\nvar<private> pv: PV = PV(vec4<f32>(0,0,0,1), 1.0, array<f32,1>(), array<f32,1>());\\n@vertex fn main() -> @builtin(position) vec4<f32> { pv.p = vec4<f32>(0,0,0,1); return pv.p; }',
                            null);

                        // Test 4: gl_PerVertex struct + override + constants
                        testPipeline('4_glPerVertex_with_override',
                            'struct PV { @builtin(position) p: vec4<f32>, ps: f32, cd: array<f32,1>, cu: array<f32,1> }\\n@id(0) override val: u32 = 0u;\\nvar<private> pv: PV = PV(vec4<f32>(0,0,0,1), 1.0, array<f32,1>(), array<f32,1>());\\n@vertex fn main() -> @builtin(position) vec4<f32> { pv.p = vec4<f32>(0,0,0,1); return pv.p; }',
                            {"0": 0});

                        // Test 5: gl_PerVertex WITHOUT clip/cull + override
                        testPipeline('5_glPerVertex_no_clip_with_override',
                            'struct PV { @builtin(position) p: vec4<f32>, ps: f32 }\\n@id(0) override val: u32 = 0u;\\nvar<private> pv: PV = PV(vec4<f32>(0,0,0,1), 1.0);\\n@vertex fn main() -> @builtin(position) vec4<f32> { pv.p = vec4<f32>(0,0,0,1); return pv.p; }',
                            {"0": 0});

                        // Test 6: Override with NO constants passed (should work)
                        testPipeline('6_override_no_constants',
                            '@id(0) override val: u32 = 42u;\\n@vertex fn main() -> @builtin(position) vec4<f32> { let x = val; return vec4<f32>(0.0, 0.0, 0.0, 1.0); }',
                            null);

                        // Test 7: many bindings + override (closer to real shader)
                        testPipeline('7_bindings_override',
                            'struct CD { ct: mat4x4<f32>, st: mat4x4<f32> }\\n@group(0) @binding(2) var<uniform> cd: CD;\\n@id(0) override val: u32 = 0u;\\n@vertex fn main() -> @builtin(position) vec4<f32> { let x = val; return cd.ct * vec4<f32>(0,0,0,1); }',
                            {"0": 0});

                        // Test 8: exact failing module #33 WGSL but with override → const (no constants passed)
                        // This will be set up after module #33 is captured
                        window._diag._pendingConstTest = true;

                        // Hook createShaderModule
                        var moduleId = 0;
                        window._diag.fullWgsl = {};  // moduleId -> full code for failing modules
                        var origCreateShaderModule = device.createShaderModule.bind(device);
                        device.createShaderModule = function(desc) {
                            var mid = moduleId++;
                            var mod = origCreateShaderModule(desc);
                            var snippet = (desc.code || '').substring(0, 200);
                            var hasOverride = (desc.code || '').indexOf('override') >= 0;
                            mod._diagId = mid;
                            mod._diagCode = desc.code || '';
                            mod.getCompilationInfo().then(function(info) {
                                var errors = info.messages.filter(function(m) { return m.type === 'error'; });
                                // Always capture: modules with errors, modules with overrides, first 3 modules
                                if (errors.length > 0 || hasOverride || window._diag.shaderModules.length < 3) {
                                    window._diag.shaderModules.push({
                                        id: mid,
                                        hasOverride: hasOverride,
                                        snippet: snippet,
                                        codeLen: (desc.code || '').length,
                                        errors: errors.map(function(m) { return m.message.substring(0, 300); }),
                                        warnings: info.messages.filter(function(m) { return m.type === 'warning'; }).length
                                    });
                                }
                            });
                            return mod;
                        };

                        // Hook createRenderPipeline with error scopes to catch async validation
                        var pipelineId = 0;
                        var origCreateRP = device.createRenderPipeline.bind(device);
                        device.createRenderPipeline = function(desc) {
                            var pid = pipelineId++;
                            var vtxModId = desc.vertex && desc.vertex.module ? desc.vertex.module._diagId : -1;
                            var fragModId = desc.fragment && desc.fragment.module ? desc.fragment.module._diagId : -1;
                            var vtxConsts = desc.vertex && desc.vertex.constants ? JSON.stringify(desc.vertex.constants).substring(0, 200) : '{}';
                            var fragConsts = desc.fragment && desc.fragment.constants ? JSON.stringify(desc.fragment.constants).substring(0, 200) : '{}';

                            device.pushErrorScope('validation');
                            var pipeline;
                            try {
                                pipeline = origCreateRP(desc);
                            } catch(e) {
                                device.popErrorScope();
                                window._diag.pipelineErrors.push({
                                    id: pid, error: 'SYNC: ' + (e.message || String(e)).substring(0, 300),
                                    vertexModule: vtxModId, fragmentModule: fragModId,
                                    vertexConstants: vtxConsts, fragmentConstants: fragConsts,
                                });
                                throw e;
                            }
                            device.popErrorScope().then(function(err) {
                                if (err) {
                                    var vtxMod = desc.vertex && desc.vertex.module;
                                    var fragMod = desc.fragment && desc.fragment.module;
                                    window._diag.pipelineErrors.push({
                                        id: pid, error: 'ASYNC: ' + (err.message || String(err)).substring(0, 300),
                                        vertexModule: vtxModId, fragmentModule: fragModId,
                                        vertexConstants: vtxConsts, fragmentConstants: fragConsts,
                                        vertexBuffers: desc.vertex.buffers ? desc.vertex.buffers.length : 0,
                                    });
                                    // Save full WGSL of failing modules
                                    if (vtxMod && vtxMod._diagCode) window._diag.fullWgsl[vtxMod._diagId] = vtxMod._diagCode;
                                    if (fragMod && fragMod._diagCode) window._diag.fullWgsl[fragMod._diagId] = fragMod._diagCode;

                                    // Re-test the exact same module with auto layout to isolate shader vs pipeline desc
                                    if (vtxMod && vtxMod._diagCode && !window._diag.tests['retest_failing_vtx_auto_layout']) {
                                        device.pushErrorScope('validation');
                                        try {
                                            var testMod = origCreateShaderModule({ code: vtxMod._diagCode });
                                            var testDesc = { layout: 'auto', vertex: { module: testMod, entryPoint: 'main', constants: desc.vertex.constants || {} } };
                                            origCreateRP(testDesc);
                                        } catch(e) {
                                            device.popErrorScope();
                                            window._diag.tests['retest_failing_vtx_auto_layout'] = { result: 'SYNC_ERROR', error: e.message };
                                            return;
                                        }
                                        device.popErrorScope().then(function(e2) {
                                            window._diag.tests['retest_failing_vtx_auto_layout'] = { result: e2 ? 'ASYNC_ERROR' : 'PASS', error: e2 ? (e2.message || String(e2)).substring(0, 300) : null };
                                        });

                                        // Also test WITHOUT constants
                                        device.pushErrorScope('validation');
                                        try {
                                            var testMod2 = origCreateShaderModule({ code: vtxMod._diagCode });
                                            origCreateRP({ layout: 'auto', vertex: { module: testMod2, entryPoint: 'main' } });
                                        } catch(e) {
                                            device.popErrorScope();
                                            window._diag.tests['retest_failing_vtx_no_constants'] = { result: 'SYNC_ERROR', error: e.message };
                                            return;
                                        }
                                        device.popErrorScope().then(function(e3) {
                                            window._diag.tests['retest_failing_vtx_no_constants'] = { result: e3 ? 'ASYNC_ERROR' : 'PASS', error: e3 ? (e3.message || String(e3)).substring(0, 300) : null };
                                        });

                                        // Test: replace @id(N) override with const and no constants
                                        var constCode = vtxMod._diagCode.replace(/@id\(\d+\)\s+override\s+/g, 'const ');
                                        device.pushErrorScope('validation');
                                        try {
                                            var testMod3 = origCreateShaderModule({ code: constCode });
                                            origCreateRP({ layout: 'auto', vertex: { module: testMod3, entryPoint: 'main' } });
                                        } catch(e) {
                                            device.popErrorScope();
                                            window._diag.tests['retest_override_replaced_with_const'] = { result: 'SYNC_ERROR', error: e.message };
                                            return;
                                        }
                                        device.popErrorScope().then(function(e4) {
                                            window._diag.tests['retest_override_replaced_with_const'] = { result: e4 ? 'ASYNC_ERROR' : 'PASS', error: e4 ? (e4.message || String(e4)).substring(0, 300) : null };
                                        });

                                        // Test: keep override but make it USED in code, with constants
                                        var usedCode = vtxMod._diagCode.replace('return VertexOutput(', 'let _override_use = pso_sc_packed_0_; return VertexOutput(');
                                        device.pushErrorScope('validation');
                                        try {
                                            var testMod4 = origCreateShaderModule({ code: usedCode });
                                            origCreateRP({ layout: 'auto', vertex: { module: testMod4, entryPoint: 'main', constants: desc.vertex.constants || {} } });
                                        } catch(e) {
                                            device.popErrorScope();
                                            window._diag.tests['retest_override_used_with_constants'] = { result: 'SYNC_ERROR', error: e.message };
                                            return;
                                        }
                                        device.popErrorScope().then(function(e5) {
                                            window._diag.tests['retest_override_used_with_constants'] = { result: e5 ? 'ASYNC_ERROR' : 'PASS', error: e5 ? (e5.message || String(e5)).substring(0, 300) : null };
                                        });
                                    }
                                }
                            });
                            return pipeline;
                        };

                        // Also hook createComputePipeline
                        var origCreateCP = device.createComputePipeline.bind(device);
                        device.createComputePipeline = function(desc) {
                            var pid = pipelineId++;
                            var modId = desc.compute && desc.compute.module ? desc.compute.module._diagId : -1;
                            device.pushErrorScope('validation');
                            var pipeline;
                            try { pipeline = origCreateCP(desc); } catch(e) {
                                device.popErrorScope();
                                window._diag.pipelineErrors.push({ id: pid, error: 'SYNC: ' + (e.message || String(e)).substring(0, 300), computeModule: modId });
                                throw e;
                            }
                            device.popErrorScope().then(function(err) {
                                if (err) {
                                    window._diag.pipelineErrors.push({ id: pid, error: 'ASYNC: ' + (err.message || String(err)).substring(0, 300), computeModule: modId });
                                }
                            });
                            return pipeline;
                        };

                        return device;
                    });
                };
                return adapter;
            });
        };
    }
})();
</script>
`;

function safariEval(js) {
    const escaped = js.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    const script = `tell application "Safari" to do JavaScript "${escaped}" in front document`;
    try {
        const raw = execSync(`osascript -e '${script.replace(/'/g, "'\"'\"'")}'`, {
            timeout: 10000, encoding: 'utf-8'
        }).trim();
        try { return JSON.parse(raw); } catch { return raw; }
    } catch { return null; }
}

async function main() {
    const sceneId = process.argv[2] || 'benchmark_sprites';
    const exportDir = join(EXPORTS_DIR, sceneId);

    if (!existsSync(join(exportDir, 'index.html'))) {
        console.error(`Export not found: ${exportDir}`);
        process.exit(1);
    }

    // Start server with diagnostic injection
    const server = await new Promise(resolve => {
        const srv = createServer((req, res) => {
            const url = req.url.split('?')[0];
            const filePath = join(exportDir, url === '/' ? 'index.html' : url);
            if (!existsSync(filePath)) { res.writeHead(404); res.end('Not found'); return; }
            const ext = extname(filePath);
            const headers = {
                'Content-Type': MIME_TYPES[ext] || 'application/octet-stream',
                'Cross-Origin-Opener-Policy': 'same-origin',
                'Cross-Origin-Embedder-Policy': 'require-corp',
            };
            let content = readFileSync(filePath);
            if (ext === '.html') {
                content = Buffer.from(content.toString('utf8').replace('<head>', '<head>' + DIAG_SCRIPT), 'utf8');
            }
            res.writeHead(200, headers);
            res.end(content);
        });
        srv.listen(0, '127.0.0.1', () => resolve(srv));
    });

    const port = server.address().port;
    const url = `http://127.0.0.1:${port}`;
    console.log(`Serving ${sceneId} at ${url}`);

    // Open in Safari
    execSync(`osascript -e 'tell application "Safari"' -e 'activate' -e 'if (count of windows) = 0 then make new document' -e 'set URL of front document to "${url}"' -e 'end tell'`);

    // Wait for scene to load
    console.log('Waiting 15s for scene to load...');
    await new Promise(r => setTimeout(r, 15000));

    // Poll diagnostics
    console.log('\n=== DIAGNOSTIC RESULTS ===\n');

    const result = safariEval(`JSON.stringify(window._diag)`);
    if (!result) {
        console.log('ERROR: Could not communicate with Safari');
        console.log('Enable: Develop > Allow JavaScript from Apple Events');
        server.close();
        process.exit(1);
    }

    console.log('Isolation tests:');
    if (result.tests) {
        for (const [name, t] of Object.entries(result.tests).sort()) {
            const status = t.result === 'PASS' ? 'PASS' : 'FAIL';
            console.log(`  ${status}  ${name}${t.error ? ' — ' + t.error : ''}`);
        }
    }
    console.log(`\nGPU errors (${result.gpuErrors.length}):`);
    for (const e of result.gpuErrors.slice(0, 10)) console.log(`  ${e}`);

    console.log(`\nShader modules captured (${result.shaderModules.length}):`);
    for (const m of result.shaderModules) {
        console.log(`  Module #${m.id}: hasOverride=${m.hasOverride}, errors=${m.errors.length}, warnings=${m.warnings}`);
        if (m.errors.length > 0) {
            for (const e of m.errors) console.log(`    ERROR: ${e}`);
        }
        console.log(`    snippet: ${m.snippet.substring(0, 120)}`);
    }

    console.log(`\nPipeline errors (${result.pipelineErrors.length}):`);
    for (const p of result.pipelineErrors.slice(0, 10)) {
        console.log(`  vtxModule=#${p.vertexModule} fragModule=#${p.fragmentModule} vtxConst=${p.vertexConstants} fragConst=${p.fragmentConstants}`);
        console.log(`    ${p.error}`);
    }

    console.log(`\nAll errors (${result.allErrors.length}):`);
    for (const e of result.allErrors.slice(0, 10)) console.log(`  ${e}`);

    // Fetch full WGSL of failing modules
    const fullWgsl = safariEval(`JSON.stringify(window._diag.fullWgsl)`);
    if (fullWgsl && Object.keys(fullWgsl).length > 0) {
        console.log('\n=== FULL WGSL OF FAILING MODULES ===');
        for (const [id, code] of Object.entries(fullWgsl)) {
            console.log(`\n--- Module #${id} (${code.length} chars) ---`);
            console.log(code);
            console.log('--- END ---');
        }
    }

    server.close();
    process.exit(0);
}

main().catch(e => { console.error('Fatal:', e); process.exit(1); });
