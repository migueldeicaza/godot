/**
 * Test: Command Buffer Encoding Sequences
 *
 * Validates the command buffer state machine that tracks encoder states,
 * bind group bindings, and pipeline state for the WebGPU driver.
 *
 * The WebGPU driver manages:
 * - Active encoder state (NONE, RENDER, COMPUTE)
 * - Bind group redundancy elimination
 * - Pipeline binding state
 * - Render pass attachment tracking
 * - Push constant dirty tracking
 *
 * Reference: webgpu_objects.h WGCommandBuffer, rendering_device_driver_webgpu.cpp
 */

import { describe, it, assert } from './test_harness.mjs';

// Encoder states matching the C++ enum.
const ActiveEncoder = { NONE: 0, RENDER: 1, COMPUTE: 2 };

const MAX_BIND_GROUPS = 4;
const MAX_VERTEX_BINDINGS = 8;
const MAX_PUSH_CONSTANT_SIZE = 128;
const MAX_ATTACHMENT_TEXTURES = 64;

/**
 * Simulates the WGCommandBuffer state machine.
 */
class CommandBufferState {
    constructor() {
        this.activeEncoder = ActiveEncoder.NONE;
        this.boundBindGroups = new Array(MAX_BIND_GROUPS).fill(null);
        this.boundShader = null;
        this.pushConstantData = new Uint8Array(MAX_PUSH_CONSTANT_SIZE);
        this.pushConstantDataLen = 0;
        this.pushConstantsDirty = false;
        this.currentPcBindGroup = null;
        this.renderState = {
            currentPipeline: null,
            currentIndexBuffer: null,
            currentIndexOffset: 0,
            currentIndexFormat: 'uint32',
            currentVertexBuffers: new Array(MAX_VERTEX_BINDINGS).fill(null),
            currentVertexOffsets: new Array(MAX_VERTEX_BINDINGS).fill(0),
            currentPassAttachments: [],
        };
        // Counters for tracking operations (simulating perf counters).
        this.stats = {
            setBindGroupCalls: 0,
            setBindGroupSkipped: 0,
            setPipelineCalls: 0,
            setVertexBufferCalls: 0,
            drawCalls: 0,
            renderPasses: 0,
        };
    }

    /**
     * Begin a render pass.
     */
    beginRenderPass(attachments = []) {
        this.endActiveEncoder();
        this.activeEncoder = ActiveEncoder.RENDER;
        this.renderState.currentPipeline = null;
        this.renderState.currentPassAttachments = [];
        for (const att of attachments) {
            this.addPassAttachment(att);
        }
        this.stats.renderPasses++;
    }

    /**
     * End the current render pass.
     */
    endRenderPass() {
        if (this.activeEncoder === ActiveEncoder.RENDER) {
            this.activeEncoder = ActiveEncoder.NONE;
            this.renderState.currentPassAttachments = [];
        }
    }

    /**
     * Begin a compute pass.
     */
    beginComputePass() {
        this.endActiveEncoder();
        this.activeEncoder = ActiveEncoder.COMPUTE;
    }

    /**
     * End any active encoder.
     */
    endActiveEncoder() {
        if (this.activeEncoder === ActiveEncoder.RENDER) {
            this.activeEncoder = ActiveEncoder.NONE;
        } else if (this.activeEncoder === ActiveEncoder.COMPUTE) {
            this.activeEncoder = ActiveEncoder.NONE;
        }
    }

    /**
     * Bind a pipeline. Invalidates bind groups if shader changes.
     */
    bindPipeline(pipelineId, shader) {
        this.stats.setPipelineCalls++;
        this.renderState.currentPipeline = pipelineId;

        // If shader changed, invalidate all bind groups.
        if (this.boundShader !== shader) {
            this.invalidateBindGroups();
            this.boundShader = shader;
        }
    }

    /**
     * Bind a bind group at the given slot with redundancy elimination.
     * Returns true if the bind was actually performed (not redundant).
     */
    setBindGroup(slot, bindGroup) {
        if (slot >= MAX_BIND_GROUPS) return false;

        if (this.boundBindGroups[slot] === bindGroup) {
            this.stats.setBindGroupSkipped++;
            return false; // Redundant — skip
        }

        this.boundBindGroups[slot] = bindGroup;
        this.stats.setBindGroupCalls++;
        return true;
    }

    /**
     * Bind a vertex buffer with redundancy elimination.
     */
    setVertexBuffer(slot, buffer, offset) {
        if (slot >= MAX_VERTEX_BINDINGS) return false;

        if (this.renderState.currentVertexBuffers[slot] === buffer &&
            this.renderState.currentVertexOffsets[slot] === offset) {
            return false; // Redundant
        }

        this.renderState.currentVertexBuffers[slot] = buffer;
        this.renderState.currentVertexOffsets[slot] = offset;
        this.stats.setVertexBufferCalls++;
        return true;
    }

    /**
     * Bind an index buffer with redundancy elimination.
     */
    setIndexBuffer(buffer, format, offset) {
        if (this.renderState.currentIndexBuffer === buffer &&
            this.renderState.currentIndexFormat === format &&
            this.renderState.currentIndexOffset === offset) {
            return false; // Redundant
        }

        this.renderState.currentIndexBuffer = buffer;
        this.renderState.currentIndexFormat = format;
        this.renderState.currentIndexOffset = offset;
        return true;
    }

    /**
     * Set push constant data.
     */
    setPushConstants(data) {
        if (data.length > MAX_PUSH_CONSTANT_SIZE) {
            throw new Error('Push constant data exceeds MAX_PUSH_CONSTANT_SIZE');
        }
        for (let i = 0; i < data.length; i++) {
            this.pushConstantData[i] = data[i];
        }
        this.pushConstantDataLen = data.length;
        this.pushConstantsDirty = true;
    }

    /**
     * Invalidate all bind groups (called on shader change).
     */
    invalidateBindGroups() {
        for (let i = 0; i < MAX_BIND_GROUPS; i++) {
            this.boundBindGroups[i] = null;
        }
        this.boundShader = null;
        this.currentPcBindGroup = null;
    }

    /**
     * Add a texture to the current pass attachment list (dedup).
     */
    addPassAttachment(texture) {
        if (!texture) return;
        if (this.renderState.currentPassAttachments.includes(texture)) return;
        if (this.renderState.currentPassAttachments.length < MAX_ATTACHMENT_TEXTURES) {
            this.renderState.currentPassAttachments.push(texture);
        }
    }

    /**
     * Check if a texture is an attachment in the current pass.
     */
    isCurrentPassAttachment(texture) {
        return this.renderState.currentPassAttachments.includes(texture);
    }

    /**
     * Record a draw call.
     */
    draw() {
        this.stats.drawCalls++;
    }
}

export function runTests() {
    describe('Command Buffer: Encoder State Machine', () => {
        it('should start in NONE state', () => {
            const cmd = new CommandBufferState();
            assert.equal(cmd.activeEncoder, ActiveEncoder.NONE);
        });

        it('should transition to RENDER on beginRenderPass', () => {
            const cmd = new CommandBufferState();
            cmd.beginRenderPass();
            assert.equal(cmd.activeEncoder, ActiveEncoder.RENDER);
        });

        it('should transition to COMPUTE on beginComputePass', () => {
            const cmd = new CommandBufferState();
            cmd.beginComputePass();
            assert.equal(cmd.activeEncoder, ActiveEncoder.COMPUTE);
        });

        it('should end render pass and return to NONE', () => {
            const cmd = new CommandBufferState();
            cmd.beginRenderPass();
            cmd.endRenderPass();
            assert.equal(cmd.activeEncoder, ActiveEncoder.NONE);
        });

        it('should end active encoder before starting new pass', () => {
            const cmd = new CommandBufferState();
            cmd.beginRenderPass();
            cmd.beginComputePass(); // Should end render pass first
            assert.equal(cmd.activeEncoder, ActiveEncoder.COMPUTE);
        });

        it('should track render pass count', () => {
            const cmd = new CommandBufferState();
            cmd.beginRenderPass();
            cmd.endRenderPass();
            cmd.beginRenderPass();
            cmd.endRenderPass();
            assert.equal(cmd.stats.renderPasses, 2);
        });
    });

    describe('Command Buffer: Bind Group Redundancy Elimination', () => {
        it('should bind new bind group and increment counter', () => {
            const cmd = new CommandBufferState();
            const result = cmd.setBindGroup(0, 'group_A');
            assert.ok(result);
            assert.equal(cmd.stats.setBindGroupCalls, 1);
        });

        it('should skip redundant bind group', () => {
            const cmd = new CommandBufferState();
            cmd.setBindGroup(0, 'group_A');
            const result = cmd.setBindGroup(0, 'group_A');
            assert.ok(!result);
            assert.equal(cmd.stats.setBindGroupSkipped, 1);
            assert.equal(cmd.stats.setBindGroupCalls, 1);
        });

        it('should not skip when bind group changes', () => {
            const cmd = new CommandBufferState();
            cmd.setBindGroup(0, 'group_A');
            const result = cmd.setBindGroup(0, 'group_B');
            assert.ok(result);
            assert.equal(cmd.stats.setBindGroupCalls, 2);
        });

        it('should track independent slots', () => {
            const cmd = new CommandBufferState();
            cmd.setBindGroup(0, 'group_A');
            cmd.setBindGroup(1, 'group_B');
            cmd.setBindGroup(2, 'group_C');
            assert.equal(cmd.stats.setBindGroupCalls, 3);
            // Rebinding same to slot 0 should skip
            cmd.setBindGroup(0, 'group_A');
            assert.equal(cmd.stats.setBindGroupSkipped, 1);
        });

        it('should reject out-of-range slot', () => {
            const cmd = new CommandBufferState();
            const result = cmd.setBindGroup(MAX_BIND_GROUPS, 'group_A');
            assert.ok(!result);
        });

        it('should invalidate all bind groups on shader change', () => {
            const cmd = new CommandBufferState();
            cmd.setBindGroup(0, 'group_A');
            cmd.setBindGroup(1, 'group_B');
            cmd.bindPipeline('pipeline_1', 'shader_A');
            // Now bind same groups — should NOT skip because invalidated
            cmd.bindPipeline('pipeline_2', 'shader_B'); // Different shader
            const result = cmd.setBindGroup(0, 'group_A');
            assert.ok(result); // Should bind (was invalidated)
        });
    });

    describe('Command Buffer: Vertex/Index Buffer Redundancy', () => {
        it('should bind new vertex buffer', () => {
            const cmd = new CommandBufferState();
            const result = cmd.setVertexBuffer(0, 'vb_A', 0);
            assert.ok(result);
            assert.equal(cmd.stats.setVertexBufferCalls, 1);
        });

        it('should skip redundant vertex buffer binding', () => {
            const cmd = new CommandBufferState();
            cmd.setVertexBuffer(0, 'vb_A', 0);
            const result = cmd.setVertexBuffer(0, 'vb_A', 0);
            assert.ok(!result);
        });

        it('should rebind when offset changes', () => {
            const cmd = new CommandBufferState();
            cmd.setVertexBuffer(0, 'vb_A', 0);
            const result = cmd.setVertexBuffer(0, 'vb_A', 256);
            assert.ok(result);
        });

        it('should skip redundant index buffer binding', () => {
            const cmd = new CommandBufferState();
            cmd.setIndexBuffer('ib_A', 'uint32', 0);
            const result = cmd.setIndexBuffer('ib_A', 'uint32', 0);
            assert.ok(!result);
        });

        it('should rebind index buffer when format changes', () => {
            const cmd = new CommandBufferState();
            cmd.setIndexBuffer('ib_A', 'uint32', 0);
            const result = cmd.setIndexBuffer('ib_A', 'uint16', 0);
            assert.ok(result);
        });
    });

    describe('Command Buffer: Push Constant State', () => {
        it('should mark push constants dirty on write', () => {
            const cmd = new CommandBufferState();
            cmd.setPushConstants([1, 2, 3, 4]);
            assert.ok(cmd.pushConstantsDirty);
            assert.equal(cmd.pushConstantDataLen, 4);
        });

        it('should store push constant data', () => {
            const cmd = new CommandBufferState();
            cmd.setPushConstants([0xAA, 0xBB, 0xCC, 0xDD]);
            assert.equal(cmd.pushConstantData[0], 0xAA);
            assert.equal(cmd.pushConstantData[3], 0xDD);
        });

        it('should reject oversized push constant data', () => {
            const cmd = new CommandBufferState();
            assert.throws(() => cmd.setPushConstants(new Array(129).fill(0)));
        });

        it('should accept exactly MAX_PUSH_CONSTANT_SIZE', () => {
            const cmd = new CommandBufferState();
            cmd.setPushConstants(new Array(128).fill(0));
            assert.equal(cmd.pushConstantDataLen, 128);
        });
    });

    describe('Command Buffer: Attachment Tracking', () => {
        it('should track attachments added to render pass', () => {
            const cmd = new CommandBufferState();
            cmd.beginRenderPass(['tex_color', 'tex_depth']);
            assert.ok(cmd.isCurrentPassAttachment('tex_color'));
            assert.ok(cmd.isCurrentPassAttachment('tex_depth'));
        });

        it('should deduplicate attachment entries', () => {
            const cmd = new CommandBufferState();
            cmd.beginRenderPass(['tex_A', 'tex_A', 'tex_A']);
            assert.equal(cmd.renderState.currentPassAttachments.length, 1);
        });

        it('should clear attachments on pass end', () => {
            const cmd = new CommandBufferState();
            cmd.beginRenderPass(['tex_A']);
            cmd.endRenderPass();
            assert.ok(!cmd.isCurrentPassAttachment('tex_A'));
        });

        it('should not report non-attachment textures', () => {
            const cmd = new CommandBufferState();
            cmd.beginRenderPass(['tex_A']);
            assert.ok(!cmd.isCurrentPassAttachment('tex_B'));
        });

        it('should handle null attachment gracefully', () => {
            const cmd = new CommandBufferState();
            cmd.beginRenderPass([null, 'tex_A', null]);
            assert.equal(cmd.renderState.currentPassAttachments.length, 1);
        });

        it('should cap at MAX_ATTACHMENT_TEXTURES', () => {
            const cmd = new CommandBufferState();
            const attachments = [];
            for (let i = 0; i < MAX_ATTACHMENT_TEXTURES + 10; i++) {
                attachments.push(`tex_${i}`);
            }
            cmd.beginRenderPass(attachments);
            assert.equal(cmd.renderState.currentPassAttachments.length, MAX_ATTACHMENT_TEXTURES);
        });
    });

    describe('Command Buffer: Pipeline Binding', () => {
        it('should track current pipeline', () => {
            const cmd = new CommandBufferState();
            cmd.bindPipeline('pipeline_1', 'shader_A');
            assert.equal(cmd.renderState.currentPipeline, 'pipeline_1');
        });

        it('should not invalidate bind groups if shader is same', () => {
            const cmd = new CommandBufferState();
            cmd.bindPipeline('pipeline_1', 'shader_A');
            cmd.setBindGroup(0, 'group_X');
            cmd.bindPipeline('pipeline_2', 'shader_A'); // Same shader, different pipeline
            // Bind group should still be tracked
            const result = cmd.setBindGroup(0, 'group_X');
            assert.ok(!result); // Should skip — still bound
        });

        it('should invalidate bind groups if shader changes', () => {
            const cmd = new CommandBufferState();
            cmd.bindPipeline('pipeline_1', 'shader_A');
            cmd.setBindGroup(0, 'group_X');
            cmd.bindPipeline('pipeline_2', 'shader_B'); // Different shader
            const result = cmd.setBindGroup(0, 'group_X');
            assert.ok(result); // Should NOT skip — was invalidated
        });
    });
}
