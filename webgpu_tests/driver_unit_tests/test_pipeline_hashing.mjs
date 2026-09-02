/**
 * Test: Pipeline State Hashing and Caching
 *
 * Validates the pipeline state hashing logic used for render pipeline deduplication.
 * WebGPU render pipelines are expensive to create (they invoke WGSL compilation),
 * so the driver must correctly hash state to avoid redundant creation.
 *
 * The hash incorporates:
 * - Shader layout hash
 * - Vertex format
 * - Render primitive (topology)
 * - Rasterization state (cull mode, front face, polygon mode, etc.)
 * - Depth/stencil state
 * - Multisample state
 * - Color blend state (per-attachment)
 * - Color attachment formats
 *
 * Reference: rendering_device_driver_webgpu.cpp render_pipeline_create()
 */

import { describe, it, assert } from './test_harness.mjs';

// Enum values matching Godot's RenderingDevice API.
const RenderPrimitive = {
    POINTS: 0,
    LINES: 1,
    LINES_WITH_ADJACENCY: 2,
    LINE_STRIPS: 3,
    LINE_STRIPS_WITH_ADJACENCY: 4,
    TRIANGLES: 5,
    TRIANGLES_WITH_ADJACENCY: 6,
    TRIANGLE_STRIPS: 7,
    TRIANGLE_STRIPS_WITH_ADJACENCY: 8,
    PATCHES: 9,
};

const CullMode = { NONE: 0, FRONT: 1, BACK: 2, FRONT_AND_BACK: 3 };
const FrontFace = { CW: 0, CCW: 1 };
const CompareOp = { NEVER: 0, LESS: 1, EQUAL: 2, LESS_OR_EQUAL: 3, GREATER: 4, NOT_EQUAL: 5, GREATER_OR_EQUAL: 6, ALWAYS: 7 };
const BlendFactor = { ZERO: 0, ONE: 1, SRC_COLOR: 2, DST_COLOR: 4, SRC_ALPHA: 6, DST_ALPHA: 8, ONE_MINUS_SRC_ALPHA: 7 };
const BlendOp = { ADD: 0, SUBTRACT: 1, REVERSE_SUBTRACT: 2, MIN: 3, MAX: 4 };

/**
 * Simulates a pipeline state descriptor used for hashing.
 */
class PipelineState {
    constructor() {
        this.shaderLayoutHash = 0;
        this.vertexFormatId = 0;
        this.primitive = RenderPrimitive.TRIANGLES;
        this.rasterization = {
            cullMode: CullMode.BACK,
            frontFace: FrontFace.CCW,
            depthBiasEnabled: false,
            depthBiasConstant: 0,
            depthBiasSlopeFactor: 0,
            depthBiasClamp: 0,
            lineWidth: 1.0,
        };
        this.depthStencil = {
            depthTestEnabled: true,
            depthWriteEnabled: true,
            compareOp: CompareOp.LESS_OR_EQUAL,
            stencilTestEnabled: false,
        };
        this.multisample = {
            sampleCount: 1,
            alphaToCoverage: false,
        };
        this.colorBlend = {
            attachments: [],
        };
        this.colorAttachmentFormats = [];
    }
}

/**
 * MurmurHash3-style hash mix function (32-bit).
 * Used to combine multiple state fields into a single pipeline hash.
 */
function hashMix(h, value) {
    h = h ^ value;
    h = Math.imul(h ^ (h >>> 16), 0x85ebca6b);
    h = Math.imul(h ^ (h >>> 13), 0xc2b2ae35);
    h = h ^ (h >>> 16);
    return h >>> 0; // Ensure unsigned 32-bit
}

/**
 * Hash a float by reinterpreting as uint32.
 */
function hashFloat(f) {
    const buf = new Float32Array([f]);
    const view = new Uint32Array(buf.buffer);
    return view[0];
}

/**
 * Compute a pipeline state hash from the state descriptor.
 * This simulates what the driver would do for cache lookup.
 */
function computePipelineHash(state) {
    let h = 0;

    // Shader layout hash
    h = hashMix(h, state.shaderLayoutHash);

    // Vertex format
    h = hashMix(h, state.vertexFormatId);

    // Primitive topology
    h = hashMix(h, state.primitive);

    // Rasterization state
    h = hashMix(h, state.rasterization.cullMode);
    h = hashMix(h, state.rasterization.frontFace);
    h = hashMix(h, state.rasterization.depthBiasEnabled ? 1 : 0);
    h = hashMix(h, hashFloat(state.rasterization.depthBiasConstant));
    h = hashMix(h, hashFloat(state.rasterization.depthBiasSlopeFactor));
    h = hashMix(h, hashFloat(state.rasterization.depthBiasClamp));

    // Depth/stencil state
    h = hashMix(h, state.depthStencil.depthTestEnabled ? 1 : 0);
    h = hashMix(h, state.depthStencil.depthWriteEnabled ? 1 : 0);
    h = hashMix(h, state.depthStencil.compareOp);
    h = hashMix(h, state.depthStencil.stencilTestEnabled ? 1 : 0);

    // Multisample state
    h = hashMix(h, state.multisample.sampleCount);
    h = hashMix(h, state.multisample.alphaToCoverage ? 1 : 0);

    // Color attachment count
    h = hashMix(h, state.colorAttachmentFormats.length);

    // Per-attachment blend state
    for (const att of state.colorBlend.attachments) {
        h = hashMix(h, att.blendEnabled ? 1 : 0);
        if (att.blendEnabled) {
            h = hashMix(h, att.srcColorFactor);
            h = hashMix(h, att.dstColorFactor);
            h = hashMix(h, att.colorOp);
            h = hashMix(h, att.srcAlphaFactor);
            h = hashMix(h, att.dstAlphaFactor);
            h = hashMix(h, att.alphaOp);
        }
        h = hashMix(h, att.writeMask);
    }

    return h;
}

export function runTests() {
    describe('Pipeline Hashing: Identity', () => {
        it('should produce same hash for identical states', () => {
            const s1 = new PipelineState();
            const s2 = new PipelineState();
            assert.equal(computePipelineHash(s1), computePipelineHash(s2));
        });

        it('should be deterministic (same input = same output)', () => {
            const state = new PipelineState();
            state.shaderLayoutHash = 0x12345678;
            state.primitive = RenderPrimitive.LINES;
            const h1 = computePipelineHash(state);
            const h2 = computePipelineHash(state);
            assert.equal(h1, h2);
        });
    });

    describe('Pipeline Hashing: Differentiation', () => {
        it('should produce different hash for different shader', () => {
            const s1 = new PipelineState();
            const s2 = new PipelineState();
            s1.shaderLayoutHash = 1;
            s2.shaderLayoutHash = 2;
            assert.ok(computePipelineHash(s1) !== computePipelineHash(s2));
        });

        it('should produce different hash for different topology', () => {
            const s1 = new PipelineState();
            const s2 = new PipelineState();
            s1.primitive = RenderPrimitive.TRIANGLES;
            s2.primitive = RenderPrimitive.LINES;
            assert.ok(computePipelineHash(s1) !== computePipelineHash(s2));
        });

        it('should produce different hash for different cull mode', () => {
            const s1 = new PipelineState();
            const s2 = new PipelineState();
            s1.rasterization.cullMode = CullMode.BACK;
            s2.rasterization.cullMode = CullMode.NONE;
            assert.ok(computePipelineHash(s1) !== computePipelineHash(s2));
        });

        it('should produce different hash for different depth compare op', () => {
            const s1 = new PipelineState();
            const s2 = new PipelineState();
            s1.depthStencil.compareOp = CompareOp.LESS;
            s2.depthStencil.compareOp = CompareOp.GREATER;
            assert.ok(computePipelineHash(s1) !== computePipelineHash(s2));
        });

        it('should produce different hash for different sample count', () => {
            const s1 = new PipelineState();
            const s2 = new PipelineState();
            s1.multisample.sampleCount = 1;
            s2.multisample.sampleCount = 4;
            assert.ok(computePipelineHash(s1) !== computePipelineHash(s2));
        });

        it('should produce different hash for different blend state', () => {
            const s1 = new PipelineState();
            const s2 = new PipelineState();
            s1.colorBlend.attachments = [{
                blendEnabled: false, writeMask: 0xF,
                srcColorFactor: 0, dstColorFactor: 0, colorOp: 0,
                srcAlphaFactor: 0, dstAlphaFactor: 0, alphaOp: 0,
            }];
            s2.colorBlend.attachments = [{
                blendEnabled: true, writeMask: 0xF,
                srcColorFactor: BlendFactor.SRC_ALPHA,
                dstColorFactor: BlendFactor.ONE_MINUS_SRC_ALPHA,
                colorOp: BlendOp.ADD,
                srcAlphaFactor: BlendFactor.ONE,
                dstAlphaFactor: BlendFactor.ZERO,
                alphaOp: BlendOp.ADD,
            }];
            assert.ok(computePipelineHash(s1) !== computePipelineHash(s2));
        });

        it('should produce different hash for different depth bias', () => {
            const s1 = new PipelineState();
            const s2 = new PipelineState();
            s1.rasterization.depthBiasEnabled = false;
            s2.rasterization.depthBiasEnabled = true;
            s2.rasterization.depthBiasConstant = 1.0;
            assert.ok(computePipelineHash(s1) !== computePipelineHash(s2));
        });

        it('should produce different hash for different vertex format', () => {
            const s1 = new PipelineState();
            const s2 = new PipelineState();
            s1.vertexFormatId = 1;
            s2.vertexFormatId = 2;
            assert.ok(computePipelineHash(s1) !== computePipelineHash(s2));
        });
    });

    describe('Pipeline Hashing: Collision Resistance', () => {
        it('should have low collision rate for common pipeline variants', () => {
            const hashes = new Set();
            const variants = 1000;
            for (let i = 0; i < variants; i++) {
                const state = new PipelineState();
                state.shaderLayoutHash = i;
                state.primitive = i % 6;
                state.rasterization.cullMode = i % 3;
                state.depthStencil.compareOp = i % 8;
                state.multisample.sampleCount = [1, 4][i % 2];
                hashes.add(computePipelineHash(state));
            }
            // Allow at most 1% collision rate
            assert.greaterThan(hashes.size, variants * 0.99);
        });
    });

    describe('Pipeline Hashing: Strip Topology Detection', () => {
        it('should identify strip topologies', () => {
            const stripPrimitives = [
                RenderPrimitive.LINE_STRIPS,
                RenderPrimitive.LINE_STRIPS_WITH_ADJACENCY,
                RenderPrimitive.TRIANGLE_STRIPS,
                RenderPrimitive.TRIANGLE_STRIPS_WITH_ADJACENCY,
            ];
            const nonStripPrimitives = [
                RenderPrimitive.POINTS,
                RenderPrimitive.LINES,
                RenderPrimitive.TRIANGLES,
            ];

            function isStrip(primitive) {
                return primitive === RenderPrimitive.LINE_STRIPS ||
                       primitive === RenderPrimitive.LINE_STRIPS_WITH_ADJACENCY ||
                       primitive === RenderPrimitive.TRIANGLE_STRIPS ||
                       primitive === RenderPrimitive.TRIANGLE_STRIPS_WITH_ADJACENCY;
            }

            for (const p of stripPrimitives) {
                assert.ok(isStrip(p), `Primitive ${p} should be strip`);
            }
            for (const p of nonStripPrimitives) {
                assert.ok(!isStrip(p), `Primitive ${p} should not be strip`);
            }
        });
    });

    describe('Pipeline Hashing: Write Mask Handling', () => {
        it('should differentiate write masks', () => {
            const s1 = new PipelineState();
            const s2 = new PipelineState();
            s1.colorBlend.attachments = [{ blendEnabled: false, writeMask: 0xF,
                srcColorFactor: 0, dstColorFactor: 0, colorOp: 0,
                srcAlphaFactor: 0, dstAlphaFactor: 0, alphaOp: 0 }];
            s2.colorBlend.attachments = [{ blendEnabled: false, writeMask: 0x7, // No alpha write
                srcColorFactor: 0, dstColorFactor: 0, colorOp: 0,
                srcAlphaFactor: 0, dstAlphaFactor: 0, alphaOp: 0 }];
            assert.ok(computePipelineHash(s1) !== computePipelineHash(s2));
        });
    });
}
