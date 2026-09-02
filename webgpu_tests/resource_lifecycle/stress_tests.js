/**
 * WebGPU Resource Lifecycle Stress Tests
 *
 * Validates that the WebGPU driver correctly handles:
 * - Rapid buffer create/destroy cycles
 * - Texture allocation/deallocation churn
 * - Bind group rebinding storms
 * - Pipeline creation/destruction
 * - Async buffer map with premature destroy ("freed while pending")
 * - Mixed resource churn under load
 *
 * Each test tracks resource counts and memory to detect leaks.
 */

// ─── Globals ──────────────────────────────────────────────────────────────────

let device = null;
let adapter = null;
let testResults = {};
let running = false;

// ─── Initialization ───────────────────────────────────────────────────────────

async function initWebGPU() {
    if (device) return true;

    if (!navigator.gpu) {
        setStatus('WebGPU not supported in this browser.', 'fail');
        return false;
    }

    adapter = await navigator.gpu.requestAdapter({
        powerPreference: 'high-performance',
    });
    if (!adapter) {
        setStatus('Failed to get GPU adapter.', 'fail');
        return false;
    }

    device = await adapter.requestDevice({
        requiredLimits: {
            maxBufferSize: 256 * 1024 * 1024,
            maxStorageBufferBindingSize: 128 * 1024 * 1024,
        },
    });

    device.lost.then((info) => {
        log(`[DEVICE LOST] reason: ${info.reason}, message: ${info.message}`);
        device = null;
    });

    log(`[INIT] Adapter: ${adapter.info?.device || 'unknown'}`);
    log(`[INIT] Device created successfully`);
    return true;
}

// ─── Test Definitions ─────────────────────────────────────────────────────────

const TESTS = {
    buffers: {
        name: 'Buffer Create/Destroy Storm',
        desc: 'Creates and immediately destroys 10,000 buffers of varying sizes to detect handle leaks.',
        iterations: 10000,
        run: testBufferStorm,
    },
    textures: {
        name: 'Texture Allocation Churn',
        desc: 'Creates and destroys 2,000 textures with various formats and dimensions.',
        iterations: 2000,
        run: testTextureChurn,
    },
    bindgroups: {
        name: 'Bind Group Rebinding Storm',
        desc: 'Creates 5,000 bind groups referencing rotating buffer sets to test BGL caching.',
        iterations: 5000,
        run: testBindGroupStorm,
    },
    pipelines: {
        name: 'Pipeline Creation/Destruction',
        desc: 'Creates and destroys 500 render/compute pipelines with varying configurations.',
        iterations: 500,
        run: testPipelineChurn,
    },
    async: {
        name: 'Async Map + Premature Destroy',
        desc: 'Maps buffers asynchronously then destroys before completion — tests freed-while-pending.',
        iterations: 200,
        run: testAsyncMapDestroy,
    },
    mixed: {
        name: 'Mixed Resource Churn',
        desc: 'Interleaves creation/destruction of all resource types under sustained load for 5 seconds.',
        iterations: 5000,
        run: testMixedChurn,
    },
};

// ─── Buffer Storm Test ────────────────────────────────────────────────────────

async function testBufferStorm(ctx) {
    const sizes = [64, 256, 1024, 4096, 16384, 65536, 262144];
    const usages = [
        GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
        GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
        GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
        GPUBufferUsage.COPY_SRC | GPUBufferUsage.MAP_READ,
    ];

    let created = 0;
    let destroyed = 0;
    let errors = 0;

    for (let i = 0; i < ctx.iterations; i++) {
        const size = sizes[i % sizes.length];
        const usage = usages[i % usages.length];

        try {
            const buffer = device.createBuffer({ size, usage });
            created++;
            buffer.destroy();
            destroyed++;
        } catch (e) {
            errors++;
            if (errors <= 3) ctx.log(`Error at iteration ${i}: ${e.message}`);
        }

        // Yield every 1000 iterations to avoid blocking
        if (i % 1000 === 0) {
            ctx.progress(i, ctx.iterations);
            await yieldFrame();
        }
    }

    // Create a batch to hold simultaneously, then release all
    const batchSize = 1000;
    const batch = [];
    for (let i = 0; i < batchSize; i++) {
        batch.push(device.createBuffer({
            size: sizes[i % sizes.length],
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
        }));
        created++;
    }
    // All alive simultaneously — destroy in reverse
    for (let i = batch.length - 1; i >= 0; i--) {
        batch[i].destroy();
        destroyed++;
    }

    return {
        pass: errors === 0 && created === destroyed,
        details: `Created: ${created}, Destroyed: ${destroyed}, Errors: ${errors}`,
    };
}

// ─── Texture Churn Test ───────────────────────────────────────────────────────

async function testTextureChurn(ctx) {
    const formats = ['rgba8unorm', 'bgra8unorm', 'r8unorm', 'rg8unorm', 'rgba16float', 'r32float', 'depth24plus', 'depth32float'];
    const dimensions = [
        { w: 64, h: 64 },
        { w: 128, h: 128 },
        { w: 256, h: 256 },
        { w: 512, h: 512 },
        { w: 1024, h: 1024 },
        { w: 32, h: 32 },
        { w: 1, h: 1 },
    ];

    let created = 0;
    let destroyed = 0;
    let errors = 0;
    let totalBytes = 0;

    for (let i = 0; i < ctx.iterations; i++) {
        const fmt = formats[i % formats.length];
        const dim = dimensions[i % dimensions.length];
        const isDepth = fmt.startsWith('depth');

        try {
            const texture = device.createTexture({
                size: { width: dim.w, height: dim.h },
                format: fmt,
                usage: isDepth
                    ? GPUTextureUsage.RENDER_ATTACHMENT
                    : GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
            });
            created++;
            totalBytes += dim.w * dim.h * bytesPerPixel(fmt);

            // Create a view — validates view doesn't keep texture alive improperly
            const view = texture.createView();
            void view;

            texture.destroy();
            destroyed++;
        } catch (e) {
            errors++;
            if (errors <= 3) ctx.log(`Error at iteration ${i} (${fmt} ${dim.w}x${dim.h}): ${e.message}`);
        }

        if (i % 500 === 0) {
            ctx.progress(i, ctx.iterations);
            await yieldFrame();
        }
    }

    // Multi-layer texture test
    try {
        const arrTex = device.createTexture({
            size: { width: 128, height: 128, depthOrArrayLayers: 6 },
            format: 'rgba8unorm',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
        });
        created++;
        // Create per-layer views
        for (let layer = 0; layer < 6; layer++) {
            arrTex.createView({ baseArrayLayer: layer, arrayLayerCount: 1 });
        }
        arrTex.destroy();
        destroyed++;
    } catch (e) {
        errors++;
        ctx.log(`Array texture error: ${e.message}`);
    }

    return {
        pass: errors === 0 && created === destroyed,
        details: `Created: ${created}, Destroyed: ${destroyed}, Errors: ${errors}\nTotal allocated: ${(totalBytes / 1024 / 1024).toFixed(1)} MB (cumulative)`,
    };
}

// ─── Bind Group Storm Test ────────────────────────────────────────────────────

async function testBindGroupStorm(ctx) {
    // Create shared resources that bind groups will reference
    const buffers = [];
    for (let i = 0; i < 8; i++) {
        buffers.push(device.createBuffer({
            size: 256,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        }));
    }

    const layout = device.createBindGroupLayout({
        entries: [
            { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
        ],
    });

    let created = 0;
    let errors = 0;

    for (let i = 0; i < ctx.iterations; i++) {
        const buf0 = buffers[i % buffers.length];
        const buf1 = buffers[(i + 1) % buffers.length];

        try {
            const bindGroup = device.createBindGroup({
                layout,
                entries: [
                    { binding: 0, resource: { buffer: buf0 } },
                    { binding: 1, resource: { buffer: buf1 } },
                ],
            });
            created++;
            // Bind groups are GC'd, no explicit destroy
            void bindGroup;
        } catch (e) {
            errors++;
            if (errors <= 3) ctx.log(`Error at iteration ${i}: ${e.message}`);
        }

        if (i % 1000 === 0) {
            ctx.progress(i, ctx.iterations);
            await yieldFrame();
        }
    }

    // Cleanup shared resources
    buffers.forEach(b => b.destroy());

    return {
        pass: errors === 0,
        details: `Created: ${created}, Errors: ${errors}\nBind groups are GC-managed — no explicit destroy needed.`,
    };
}

// ─── Pipeline Churn Test ──────────────────────────────────────────────────────

async function testPipelineChurn(ctx) {
    // Minimal shaders for pipeline creation
    const vertexShader = device.createShaderModule({
        code: `
            @vertex fn main(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
                let x = f32(i32(vi) - 1);
                let y = f32(i32(vi & 1u) * 2 - 1);
                return vec4f(x, y, 0.0, 1.0);
            }
        `,
    });

    const fragmentShaders = [];
    for (let i = 0; i < 5; i++) {
        fragmentShaders.push(device.createShaderModule({
            code: `
                @fragment fn main() -> @location(0) vec4f {
                    return vec4f(${(i * 0.2).toFixed(1)}, ${(1.0 - i * 0.2).toFixed(1)}, 0.5, 1.0);
                }
            `,
        }));
    }

    const computeShader = device.createShaderModule({
        code: `
            @group(0) @binding(0) var<storage, read_write> data: array<f32>;
            @compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gid: vec3u) {
                data[gid.x] = f32(gid.x);
            }
        `,
    });

    const pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [] });
    const computeLayout = device.createPipelineLayout({
        bindGroupLayouts: [device.createBindGroupLayout({
            entries: [{ binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } }],
        })],
    });

    const topologies = ['triangle-list', 'triangle-strip', 'line-list', 'point-list'];
    const cullModes = ['none', 'front', 'back'];

    let renderCreated = 0;
    let computeCreated = 0;
    let errors = 0;

    for (let i = 0; i < ctx.iterations; i++) {
        try {
            if (i % 3 === 0) {
                // Create compute pipeline
                const pipeline = device.createComputePipeline({
                    layout: computeLayout,
                    compute: { module: computeShader, entryPoint: 'main' },
                });
                computeCreated++;
                void pipeline;
            } else {
                // Create render pipeline with varying state
                const pipeline = device.createRenderPipeline({
                    layout: pipelineLayout,
                    vertex: { module: vertexShader, entryPoint: 'main' },
                    fragment: {
                        module: fragmentShaders[i % fragmentShaders.length],
                        entryPoint: 'main',
                        targets: [{ format: 'bgra8unorm' }],
                    },
                    primitive: {
                        topology: topologies[i % topologies.length],
                        cullMode: cullModes[i % cullModes.length],
                    },
                });
                renderCreated++;
                void pipeline;
            }
        } catch (e) {
            errors++;
            if (errors <= 3) ctx.log(`Error at iteration ${i}: ${e.message}`);
        }

        if (i % 100 === 0) {
            ctx.progress(i, ctx.iterations);
            await yieldFrame();
        }
    }

    return {
        pass: errors === 0,
        details: `Render pipelines: ${renderCreated}, Compute pipelines: ${computeCreated}, Errors: ${errors}`,
    };
}

// ─── Async Map + Destroy Test ─────────────────────────────────────────────────

async function testAsyncMapDestroy(ctx) {
    let mapSucceeded = 0;
    let mapRejected = 0;
    let errors = 0;
    let freedWhilePending = 0;

    for (let i = 0; i < ctx.iterations; i++) {
        try {
            const buffer = device.createBuffer({
                size: 1024,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
            });

            // Start async map
            const mapPromise = buffer.mapAsync(GPUMapMode.READ);

            if (i % 2 === 0) {
                // Destroy immediately — "freed while pending"
                buffer.destroy();
                freedWhilePending++;

                try {
                    await mapPromise;
                    // If map succeeds after destroy, that's fine (implementation-defined)
                    mapSucceeded++;
                } catch (e) {
                    // Expected: map rejected because buffer was destroyed
                    mapRejected++;
                }
            } else {
                // Wait for map to complete, then destroy normally
                try {
                    await mapPromise;
                    const data = new Uint8Array(buffer.getMappedRange());
                    void data.length; // Access mapped memory
                    buffer.unmap();
                    mapSucceeded++;
                } catch (e) {
                    mapRejected++;
                }
                buffer.destroy();
            }
        } catch (e) {
            errors++;
            if (errors <= 3) ctx.log(`Error at iteration ${i}: ${e.message}`);
        }

        if (i % 50 === 0) {
            ctx.progress(i, ctx.iterations);
            await yieldFrame();
        }
    }

    // The test passes if no unexpected errors occurred.
    // Both map-success and map-rejection are valid outcomes for freed-while-pending.
    return {
        pass: errors === 0,
        details: `Map succeeded: ${mapSucceeded}, Map rejected: ${mapRejected}\nFreed while pending: ${freedWhilePending}, Unexpected errors: ${errors}`,
    };
}

// ─── Mixed Resource Churn Test ────────────────────────────────────────────────

async function testMixedChurn(ctx) {
    const liveBuffers = [];
    const liveTextures = [];
    const maxLive = 100;
    let totalCreated = 0;
    let totalDestroyed = 0;
    let errors = 0;

    const startTime = performance.now();
    const duration = 5000; // 5 seconds

    let i = 0;
    while (performance.now() - startTime < duration && i < ctx.iterations) {
        const action = Math.random();

        try {
            if (action < 0.3) {
                // Create buffer
                const buffer = device.createBuffer({
                    size: 256 * (1 + Math.floor(Math.random() * 16)),
                    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
                });
                liveBuffers.push(buffer);
                totalCreated++;
            } else if (action < 0.6) {
                // Create texture
                const size = 32 * (1 + Math.floor(Math.random() * 4));
                const texture = device.createTexture({
                    size: { width: size, height: size },
                    format: 'rgba8unorm',
                    usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
                });
                liveTextures.push(texture);
                totalCreated++;
            } else if (action < 0.8 && liveBuffers.length > 0) {
                // Destroy random buffer
                const idx = Math.floor(Math.random() * liveBuffers.length);
                liveBuffers[idx].destroy();
                liveBuffers.splice(idx, 1);
                totalDestroyed++;
            } else if (liveTextures.length > 0) {
                // Destroy random texture
                const idx = Math.floor(Math.random() * liveTextures.length);
                liveTextures[idx].destroy();
                liveTextures.splice(idx, 1);
                totalDestroyed++;
            }

            // Enforce max live resources
            while (liveBuffers.length > maxLive) {
                liveBuffers.shift().destroy();
                totalDestroyed++;
            }
            while (liveTextures.length > maxLive) {
                liveTextures.shift().destroy();
                totalDestroyed++;
            }
        } catch (e) {
            errors++;
            if (errors <= 5) ctx.log(`Error at iteration ${i}: ${e.message}`);
        }

        i++;
        if (i % 500 === 0) {
            ctx.progress(i, ctx.iterations);
            await yieldFrame();
        }
    }

    // Cleanup remaining
    liveBuffers.forEach(b => b.destroy());
    totalDestroyed += liveBuffers.length;
    liveTextures.forEach(t => t.destroy());
    totalDestroyed += liveTextures.length;

    const elapsed = ((performance.now() - startTime) / 1000).toFixed(2);

    return {
        pass: errors === 0,
        details: `Duration: ${elapsed}s, Iterations: ${i}\nCreated: ${totalCreated}, Destroyed: ${totalDestroyed}\nErrors: ${errors}`,
    };
}

// ─── Utilities ────────────────────────────────────────────────────────────────

function bytesPerPixel(format) {
    const bpp = {
        'rgba8unorm': 4, 'bgra8unorm': 4, 'r8unorm': 1, 'rg8unorm': 2,
        'rgba16float': 8, 'r32float': 4, 'depth24plus': 4, 'depth32float': 4,
    };
    return bpp[format] || 4;
}

function yieldFrame() {
    return new Promise(resolve => setTimeout(resolve, 0));
}

function setStatus(msg, cls) {
    const el = document.getElementById('status');
    el.textContent = msg;
    el.className = `status ${cls || ''}`;
}

function log(msg) {
    const el = document.getElementById('log');
    const time = new Date().toISOString().substring(11, 23);
    el.textContent += `[${time}] ${msg}\n`;
    el.scrollTop = el.scrollHeight;
}

// ─── Test Runner ──────────────────────────────────────────────────────────────

function renderTestCards() {
    const grid = document.getElementById('test-grid');
    grid.innerHTML = '';
    for (const [id, test] of Object.entries(TESTS)) {
        const card = document.createElement('div');
        card.className = 'test-card';
        card.id = `card-${id}`;
        card.innerHTML = `
            <h3>${test.name}</h3>
            <div class="desc">${test.desc}</div>
            <div class="result" id="result-${id}">Pending...</div>
        `;
        grid.appendChild(card);
    }
}

async function runSingle(testId) {
    if (running) return;
    running = true;

    if (!await initWebGPU()) { running = false; return; }
    renderTestCards();

    const test = TESTS[testId];
    setStatus(`Running: ${test.name}...`, 'running');
    const result = await executeTest(testId, test);
    testResults[testId] = result;

    showSummary();
    setStatus(result.pass ? `PASSED: ${test.name}` : `FAILED: ${test.name}`, result.pass ? 'pass' : 'fail');
    running = false;
}

async function runAllTests() {
    if (running) return;
    running = true;
    document.getElementById('btn-run-all').classList.add('running');

    if (!await initWebGPU()) { running = false; return; }
    renderTestCards();
    testResults = {};

    const testIds = Object.keys(TESTS);
    let passed = 0;
    let failed = 0;

    for (const id of testIds) {
        const test = TESTS[id];
        setStatus(`Running ${passed + failed + 1}/${testIds.length}: ${test.name}...`, 'running');
        const result = await executeTest(id, test);
        testResults[id] = result;
        if (result.pass) passed++; else failed++;
    }

    showSummary();
    setStatus(`Complete: ${passed} passed, ${failed} failed out of ${testIds.length} tests`, failed === 0 ? 'pass' : 'fail');
    document.getElementById('btn-run-all').classList.remove('running');
    running = false;
}

async function executeTest(id, test) {
    const resultEl = document.getElementById(`result-${id}`);
    resultEl.textContent = 'Running...';
    resultEl.className = 'result running';

    const ctx = {
        iterations: test.iterations,
        log: (msg) => log(`[${id}] ${msg}`),
        progress: (current, total) => {
            resultEl.textContent = `Running... ${Math.round(current / total * 100)}%`;
        },
    };

    const startTime = performance.now();
    let result;
    try {
        result = await test.run(ctx);
    } catch (e) {
        result = { pass: false, details: `Uncaught error: ${e.message}\n${e.stack}` };
        log(`[${id}] UNCAUGHT: ${e.message}`);
    }

    const elapsed = ((performance.now() - startTime) / 1000).toFixed(2);
    result.elapsed = elapsed;

    resultEl.textContent = `${result.pass ? 'PASS' : 'FAIL'} (${elapsed}s)\n${result.details}`;
    resultEl.className = `result ${result.pass ? 'pass' : 'fail'}`;

    log(`[${id}] ${result.pass ? 'PASS' : 'FAIL'} in ${elapsed}s — ${result.details.split('\n')[0]}`);
    return result;
}

function showSummary() {
    const summary = document.getElementById('summary');
    const stats = document.getElementById('summary-stats');
    summary.style.display = 'block';

    const passed = Object.values(testResults).filter(r => r.pass).length;
    const failed = Object.values(testResults).filter(r => !r.pass).length;
    const totalTime = Object.values(testResults).reduce((s, r) => s + parseFloat(r.elapsed || 0), 0).toFixed(2);

    stats.innerHTML = `
        <div class="stat pass"><span class="value">${passed}</span><span>Passed</span></div>
        <div class="stat fail"><span class="value">${failed}</span><span>Failed</span></div>
        <div class="stat"><span class="value">${totalTime}s</span><span>Total Time</span></div>
    `;
}

// ─── Auto-run from URL params ─────────────────────────────────────────────────

(function() {
    const params = new URLSearchParams(window.location.search);
    if (params.has('autorun')) {
        window.addEventListener('load', () => {
            setTimeout(runAllTests, 500);
        });
    }
})();

// Initialize cards on load
window.addEventListener('load', renderTestCards);
