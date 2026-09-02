/**
 * Deterministic WebGPU Render Scene for Screenshot Comparison
 *
 * Renders a series of test patterns designed to exercise the WebGPU driver
 * and produce pixel-reproducible output across browsers (within tolerance).
 *
 * Test scenes:
 * 1. Color gradient triangle (vertex color interpolation)
 * 2. Textured quad (texture sampling, UV mapping)
 * 3. Instanced cubes (instance buffer, depth testing)
 * 4. Compute-generated pattern (compute → render pipeline)
 *
 * Signals completion by setting window.__renderComplete = true.
 */

const SCENES = ['triangle', 'textured_quad', 'instanced', 'compute_pattern'];
let currentScene = 0;

async function init() {
    const canvas = document.getElementById('canvas');
    const errorEl = document.getElementById('error');

    // Determine which scene from URL params
    const params = new URLSearchParams(window.location.search);
    const sceneName = params.get('scene') || 'triangle';
    currentScene = SCENES.indexOf(sceneName);
    if (currentScene === -1) currentScene = 0;

    if (!navigator.gpu) {
        errorEl.textContent = 'WebGPU not supported';
        errorEl.style.display = 'block';
        window.__renderComplete = true;
        window.__renderError = 'WebGPU not supported';
        return;
    }

    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) {
        errorEl.textContent = 'No GPU adapter';
        errorEl.style.display = 'block';
        window.__renderComplete = true;
        window.__renderError = 'No GPU adapter';
        return;
    }

    const device = await adapter.requestDevice();
    const context = canvas.getContext('webgpu');
    const format = navigator.gpu.getPreferredCanvasFormat();

    context.configure({
        device,
        format,
        alphaMode: 'opaque',
    });

    const renderers = {
        triangle: renderTriangle,
        textured_quad: renderTexturedQuad,
        instanced: renderInstanced,
        compute_pattern: renderComputePattern,
    };

    try {
        await renderers[SCENES[currentScene]](device, context, format, canvas);
    } catch (e) {
        errorEl.textContent = `Render error: ${e.message}`;
        errorEl.style.display = 'block';
        window.__renderError = e.message;
    }

    window.__renderComplete = true;
}

// ─── Scene 1: Color Gradient Triangle ─────────────────────────────────────────

async function renderTriangle(device, context, format) {
    const shader = device.createShaderModule({
        code: `
            struct VSOutput {
                @builtin(position) pos: vec4f,
                @location(0) color: vec3f,
            };

            @vertex fn vs(@builtin(vertex_index) vi: u32) -> VSOutput {
                var positions = array<vec2f, 3>(
                    vec2f(0.0, 0.7),
                    vec2f(-0.7, -0.5),
                    vec2f(0.7, -0.5),
                );
                var colors = array<vec3f, 3>(
                    vec3f(1.0, 0.0, 0.0),
                    vec3f(0.0, 1.0, 0.0),
                    vec3f(0.0, 0.0, 1.0),
                );
                var out: VSOutput;
                out.pos = vec4f(positions[vi], 0.0, 1.0);
                out.color = colors[vi];
                return out;
            }

            @fragment fn fs(in: VSOutput) -> @location(0) vec4f {
                return vec4f(in.color, 1.0);
            }
        `,
    });

    const pipeline = device.createRenderPipeline({
        layout: 'auto',
        vertex: { module: shader, entryPoint: 'vs' },
        fragment: { module: shader, entryPoint: 'fs', targets: [{ format }] },
        primitive: { topology: 'triangle-list' },
    });

    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
        colorAttachments: [{
            view: context.getCurrentTexture().createView(),
            clearValue: { r: 0.1, g: 0.1, b: 0.15, a: 1.0 },
            loadOp: 'clear',
            storeOp: 'store',
        }],
    });

    pass.setPipeline(pipeline);
    pass.draw(3);
    pass.end();

    device.queue.submit([encoder.finish()]);
    await device.queue.onSubmittedWorkDone();
}

// ─── Scene 2: Textured Quad ───────────────────────────────────────────────────

async function renderTexturedQuad(device, context, format) {
    // Generate a 64x64 checkerboard texture procedurally
    const texSize = 64;
    const texData = new Uint8Array(texSize * texSize * 4);
    for (let y = 0; y < texSize; y++) {
        for (let x = 0; x < texSize; x++) {
            const i = (y * texSize + x) * 4;
            const checker = ((x >> 3) ^ (y >> 3)) & 1;
            texData[i + 0] = checker ? 255 : 50;
            texData[i + 1] = checker ? 200 : 80;
            texData[i + 2] = checker ? 50 : 200;
            texData[i + 3] = 255;
        }
    }

    const texture = device.createTexture({
        size: { width: texSize, height: texSize },
        format: 'rgba8unorm',
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });

    device.queue.writeTexture(
        { texture },
        texData,
        { bytesPerRow: texSize * 4, rowsPerImage: texSize },
        { width: texSize, height: texSize },
    );

    const sampler = device.createSampler({
        magFilter: 'linear',
        minFilter: 'linear',
    });

    const shader = device.createShaderModule({
        code: `
            struct VSOutput {
                @builtin(position) pos: vec4f,
                @location(0) uv: vec2f,
            };

            @vertex fn vs(@builtin(vertex_index) vi: u32) -> VSOutput {
                var positions = array<vec2f, 6>(
                    vec2f(-0.8, -0.8), vec2f(0.8, -0.8), vec2f(-0.8, 0.8),
                    vec2f(-0.8, 0.8), vec2f(0.8, -0.8), vec2f(0.8, 0.8),
                );
                var uvs = array<vec2f, 6>(
                    vec2f(0.0, 1.0), vec2f(1.0, 1.0), vec2f(0.0, 0.0),
                    vec2f(0.0, 0.0), vec2f(1.0, 1.0), vec2f(1.0, 0.0),
                );
                var out: VSOutput;
                out.pos = vec4f(positions[vi], 0.0, 1.0);
                out.uv = uvs[vi];
                return out;
            }

            @group(0) @binding(0) var tex: texture_2d<f32>;
            @group(0) @binding(1) var samp: sampler;

            @fragment fn fs(in: VSOutput) -> @location(0) vec4f {
                return textureSample(tex, samp, in.uv);
            }
        `,
    });

    const pipeline = device.createRenderPipeline({
        layout: 'auto',
        vertex: { module: shader, entryPoint: 'vs' },
        fragment: { module: shader, entryPoint: 'fs', targets: [{ format }] },
        primitive: { topology: 'triangle-list' },
    });

    const bindGroup = device.createBindGroup({
        layout: pipeline.getBindGroupLayout(0),
        entries: [
            { binding: 0, resource: texture.createView() },
            { binding: 1, resource: sampler },
        ],
    });

    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
        colorAttachments: [{
            view: context.getCurrentTexture().createView(),
            clearValue: { r: 0.05, g: 0.05, b: 0.1, a: 1.0 },
            loadOp: 'clear',
            storeOp: 'store',
        }],
    });

    pass.setPipeline(pipeline);
    pass.setBindGroup(0, bindGroup);
    pass.draw(6);
    pass.end();

    device.queue.submit([encoder.finish()]);
    await device.queue.onSubmittedWorkDone();
}

// ─── Scene 3: Instanced Colored Quads ────────────────────────────────────────

async function renderInstanced(device, context, format) {
    const shader = device.createShaderModule({
        code: `
            struct Instance {
                @location(2) offset: vec2f,
                @location(3) color: vec4f,
                @location(4) scale: f32,
            };

            struct VSOutput {
                @builtin(position) pos: vec4f,
                @location(0) color: vec4f,
            };

            @vertex fn vs(
                @builtin(vertex_index) vi: u32,
                inst: Instance,
            ) -> VSOutput {
                var positions = array<vec2f, 6>(
                    vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                    vec2f(-1.0, 1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0),
                );
                var out: VSOutput;
                let p = positions[vi] * inst.scale + inst.offset;
                out.pos = vec4f(p, 0.0, 1.0);
                out.color = inst.color;
                return out;
            }

            @fragment fn fs(in: VSOutput) -> @location(0) vec4f {
                return in.color;
            }
        `,
    });

    // Generate deterministic instances in a grid
    const gridSize = 8;
    const instanceCount = gridSize * gridSize;
    const instanceData = new Float32Array(instanceCount * 8); // offset(2) + color(4) + scale(1) + pad(1)

    for (let y = 0; y < gridSize; y++) {
        for (let x = 0; x < gridSize; x++) {
            const i = (y * gridSize + x) * 8;
            instanceData[i + 0] = (x / (gridSize - 1)) * 1.6 - 0.8; // offset.x
            instanceData[i + 1] = (y / (gridSize - 1)) * 1.6 - 0.8; // offset.y
            instanceData[i + 2] = x / gridSize;                       // color.r
            instanceData[i + 3] = y / gridSize;                       // color.g
            instanceData[i + 4] = 0.5;                                // color.b
            instanceData[i + 5] = 1.0;                                // color.a
            instanceData[i + 6] = 0.08;                               // scale
            instanceData[i + 7] = 0.0;                                // pad
        }
    }

    const instanceBuffer = device.createBuffer({
        size: instanceData.byteLength,
        usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
    });
    device.queue.writeBuffer(instanceBuffer, 0, instanceData);

    const pipeline = device.createRenderPipeline({
        layout: 'auto',
        vertex: {
            module: shader,
            entryPoint: 'vs',
            buffers: [{
                arrayStride: 32,
                stepMode: 'instance',
                attributes: [
                    { shaderLocation: 2, offset: 0, format: 'float32x2' },   // offset
                    { shaderLocation: 3, offset: 8, format: 'float32x4' },   // color
                    { shaderLocation: 4, offset: 24, format: 'float32' },    // scale
                ],
            }],
        },
        fragment: { module: shader, entryPoint: 'fs', targets: [{ format }] },
        primitive: { topology: 'triangle-list' },
    });

    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
        colorAttachments: [{
            view: context.getCurrentTexture().createView(),
            clearValue: { r: 0.05, g: 0.02, b: 0.1, a: 1.0 },
            loadOp: 'clear',
            storeOp: 'store',
        }],
    });

    pass.setPipeline(pipeline);
    pass.setVertexBuffer(0, instanceBuffer);
    pass.draw(6, instanceCount);
    pass.end();

    device.queue.submit([encoder.finish()]);
    await device.queue.onSubmittedWorkDone();
}

// ─── Scene 4: Compute-Generated Pattern ──────────────────────────────────────

async function renderComputePattern(device, context, format) {
    const texSize = 256;

    // Compute shader generates a procedural pattern
    const computeShader = device.createShaderModule({
        code: `
            @group(0) @binding(0) var output: texture_storage_2d<rgba8unorm, write>;

            @compute @workgroup_size(8, 8) fn main(@builtin(global_invocation_id) gid: vec3u) {
                let size = vec2f(textureDimensions(output));
                let uv = vec2f(gid.xy) / size;

                // Deterministic fractal-like pattern
                let cx = uv.x * 3.0 - 2.0;
                let cy = uv.y * 2.0 - 1.0;
                var zx = 0.0;
                var zy = 0.0;
                var iter = 0u;
                let maxIter = 64u;

                loop {
                    if (iter >= maxIter) { break; }
                    let tmp = zx * zx - zy * zy + cx;
                    zy = 2.0 * zx * zy + cy;
                    zx = tmp;
                    if (zx * zx + zy * zy > 4.0) { break; }
                    iter++;
                }

                let t = f32(iter) / f32(maxIter);
                let color = vec4f(
                    t * 0.8 + 0.1,
                    t * t * 0.6,
                    (1.0 - t) * 0.9,
                    1.0
                );
                textureStore(output, gid.xy, color);
            }
        `,
    });

    // Render shader displays the computed texture
    const renderShader = device.createShaderModule({
        code: `
            struct VSOutput {
                @builtin(position) pos: vec4f,
                @location(0) uv: vec2f,
            };

            @vertex fn vs(@builtin(vertex_index) vi: u32) -> VSOutput {
                var positions = array<vec2f, 6>(
                    vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                    vec2f(-1.0, 1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0),
                );
                var uvs = array<vec2f, 6>(
                    vec2f(0.0, 1.0), vec2f(1.0, 1.0), vec2f(0.0, 0.0),
                    vec2f(0.0, 0.0), vec2f(1.0, 1.0), vec2f(1.0, 0.0),
                );
                var out: VSOutput;
                out.pos = vec4f(positions[vi], 0.0, 1.0);
                out.uv = uvs[vi];
                return out;
            }

            @group(0) @binding(0) var tex: texture_2d<f32>;
            @group(0) @binding(1) var samp: sampler;

            @fragment fn fs(in: VSOutput) -> @location(0) vec4f {
                return textureSample(tex, samp, in.uv);
            }
        `,
    });

    const computeTexture = device.createTexture({
        size: { width: texSize, height: texSize },
        format: 'rgba8unorm',
        usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
    });

    const computePipeline = device.createComputePipeline({
        layout: 'auto',
        compute: { module: computeShader, entryPoint: 'main' },
    });

    const computeBindGroup = device.createBindGroup({
        layout: computePipeline.getBindGroupLayout(0),
        entries: [{ binding: 0, resource: computeTexture.createView() }],
    });

    // Run compute
    const encoder = device.createCommandEncoder();
    const computePass = encoder.beginComputePass();
    computePass.setPipeline(computePipeline);
    computePass.setBindGroup(0, computeBindGroup);
    computePass.dispatchWorkgroups(texSize / 8, texSize / 8);
    computePass.end();

    // Render the result
    const sampler = device.createSampler({ magFilter: 'linear', minFilter: 'linear' });

    const renderPipeline = device.createRenderPipeline({
        layout: 'auto',
        vertex: { module: renderShader, entryPoint: 'vs' },
        fragment: { module: renderShader, entryPoint: 'fs', targets: [{ format }] },
        primitive: { topology: 'triangle-list' },
    });

    const renderBindGroup = device.createBindGroup({
        layout: renderPipeline.getBindGroupLayout(0),
        entries: [
            { binding: 0, resource: computeTexture.createView() },
            { binding: 1, resource: sampler },
        ],
    });

    const renderPass = encoder.beginRenderPass({
        colorAttachments: [{
            view: context.getCurrentTexture().createView(),
            clearValue: { r: 0.0, g: 0.0, b: 0.0, a: 1.0 },
            loadOp: 'clear',
            storeOp: 'store',
        }],
    });

    renderPass.setPipeline(renderPipeline);
    renderPass.setBindGroup(0, renderBindGroup);
    renderPass.draw(6);
    renderPass.end();

    device.queue.submit([encoder.finish()]);
    await device.queue.onSubmittedWorkDone();
}

// ─── Init ─────────────────────────────────────────────────────────────────────

init();
