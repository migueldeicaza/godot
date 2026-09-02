/**
 * Test: Push Constant Ring Buffer Allocation and Wrap-Around
 *
 * Validates the ring buffer allocation logic used for push constant emulation.
 * The driver uses a 256KB ring buffer with 256-byte aligned slots.
 * When the ring runs out of space, it wraps to offset 0 (with a flush).
 *
 * Reference: rendering_device_driver_webgpu.cpp _flush_push_constants()
 */

import { describe, it, assert } from './test_harness.mjs';

// Constants matching the C++ driver.
const PUSH_CONSTANT_RING_SIZE = 256 * 1024; // 256KB
const PUSH_CONSTANT_SLOT_ALIGNMENT = 256;
const MAX_PUSH_CONSTANT_SIZE = 128;

/**
 * Simulates the ring buffer allocator from the driver.
 * Returns { offset, wrapped } for each allocation.
 */
class PushConstantRingBuffer {
    constructor(ringSize = PUSH_CONSTANT_RING_SIZE, slotAlignment = PUSH_CONSTANT_SLOT_ALIGNMENT) {
        this.ringSize = ringSize;
        this.slotAlignment = slotAlignment;
        this.offset = 0;
        this.dirtyStart = 0xFFFFFFFF; // UINT32_MAX
        this.dirtyEnd = 0;
        this.wrapCount = 0;
        this.flushCount = 0;
    }

    /**
     * Allocate a slot for push constant data of the given size.
     * Returns the dynamic offset for binding.
     */
    allocate(dataLen) {
        const alignedSize = (dataLen + this.slotAlignment - 1) & ~(this.slotAlignment - 1);
        let wrapped = false;

        if (this.offset + alignedSize > this.ringSize) {
            // Flush accumulated dirty range before wrapping.
            if (this.dirtyStart < this.dirtyEnd) {
                this.flushCount++;
            }
            this.offset = 0;
            this.dirtyStart = 0xFFFFFFFF;
            this.dirtyEnd = 0;
            this.wrapCount++;
            wrapped = true;
        }

        const dynamicOffset = this.offset;

        // Track dirty range.
        if (this.offset < this.dirtyStart) {
            this.dirtyStart = this.offset;
        }
        const end = this.offset + alignedSize;
        if (end > this.dirtyEnd) {
            this.dirtyEnd = end;
        }

        this.offset += alignedSize;
        return { dynamicOffset, wrapped, alignedSize };
    }

    /** Reset at frame start (matches begin_segment). */
    resetFrame() {
        this.offset = 0;
        this.dirtyStart = 0xFFFFFFFF;
        this.dirtyEnd = 0;
    }
}

export function runTests() {
    describe('Ring Buffer: Basic Allocation', () => {
        it('should allocate first slot at offset 0', () => {
            const ring = new PushConstantRingBuffer();
            const result = ring.allocate(64);
            assert.equal(result.dynamicOffset, 0);
            assert.equal(result.wrapped, false);
        });

        it('should align allocations to 256 bytes', () => {
            const ring = new PushConstantRingBuffer();
            ring.allocate(64); // Takes 256 bytes aligned
            const result = ring.allocate(32);
            assert.equal(result.dynamicOffset, 256);
            assert.equal(result.alignedSize, 256);
        });

        it('should handle exact alignment size', () => {
            const ring = new PushConstantRingBuffer();
            const result = ring.allocate(256);
            assert.equal(result.alignedSize, 256);
            assert.equal(result.dynamicOffset, 0);
        });

        it('should handle max push constant size (128 bytes)', () => {
            const ring = new PushConstantRingBuffer();
            const result = ring.allocate(MAX_PUSH_CONSTANT_SIZE);
            assert.equal(result.alignedSize, 256); // 128 rounds up to 256
            assert.equal(result.dynamicOffset, 0);
        });

        it('should increment offset after each allocation', () => {
            const ring = new PushConstantRingBuffer();
            ring.allocate(64);
            ring.allocate(64);
            ring.allocate(64);
            const result = ring.allocate(64);
            assert.equal(result.dynamicOffset, 768); // 3 * 256
        });
    });

    describe('Ring Buffer: Wrap-Around', () => {
        it('should wrap around when ring is full', () => {
            const ring = new PushConstantRingBuffer(1024, 256); // Small ring for testing
            ring.allocate(64); // offset = 0, takes 256
            ring.allocate(64); // offset = 256, takes 256
            ring.allocate(64); // offset = 512, takes 256
            ring.allocate(64); // offset = 768, takes 256 (ring now full)
            const result = ring.allocate(64); // Should wrap
            assert.equal(result.wrapped, true);
            assert.equal(result.dynamicOffset, 0);
            assert.equal(ring.wrapCount, 1);
        });

        it('should flush before wrap-around when dirty range exists', () => {
            const ring = new PushConstantRingBuffer(512, 256);
            ring.allocate(64); // dirtyStart=0, dirtyEnd=256
            ring.allocate(64); // dirtyStart=0, dirtyEnd=512
            const result = ring.allocate(64); // Should wrap and flush
            assert.equal(result.wrapped, true);
            assert.equal(ring.flushCount, 1);
        });

        it('should handle multiple wraps in a single frame', () => {
            const ring = new PushConstantRingBuffer(512, 256);
            // 512 / 256 = 2 slots per fill
            // Allocs 1-2: fill ring (offset 0->512)
            // Alloc 3: wraps (1st wrap), fills slot 0, offset->256
            // Alloc 4: fills slot 1, offset->512
            // Alloc 5: wraps (2nd wrap), fills slot 0, offset->256
            for (let i = 0; i < 5; i++) ring.allocate(64);
            assert.equal(ring.wrapCount, 2);
        });

        it('should not wrap when exact fit at end', () => {
            const ring = new PushConstantRingBuffer(512, 256);
            ring.allocate(64); // Takes 256
            const result = ring.allocate(256); // Takes exactly remaining 256
            assert.equal(result.wrapped, false);
            assert.equal(result.dynamicOffset, 256);
        });

        it('should wrap when allocation would exceed ring even by 1 byte', () => {
            const ring = new PushConstantRingBuffer(512, 256);
            ring.allocate(64); // offset now 256
            ring.allocate(64); // offset now 512 (full)
            const result = ring.allocate(1); // Even 1 byte triggers wrap (aligned to 256)
            assert.equal(result.wrapped, true);
            assert.equal(result.dynamicOffset, 0);
        });
    });

    describe('Ring Buffer: Dirty Range Tracking', () => {
        it('should track dirty start from first write', () => {
            const ring = new PushConstantRingBuffer();
            ring.allocate(64);
            assert.equal(ring.dirtyStart, 0);
        });

        it('should expand dirty end with each allocation', () => {
            const ring = new PushConstantRingBuffer();
            ring.allocate(64);
            assert.equal(ring.dirtyEnd, 256);
            ring.allocate(64);
            assert.equal(ring.dirtyEnd, 512);
        });

        it('should reset dirty range on frame reset', () => {
            const ring = new PushConstantRingBuffer();
            ring.allocate(64);
            ring.allocate(64);
            ring.resetFrame();
            assert.equal(ring.dirtyStart, 0xFFFFFFFF);
            assert.equal(ring.dirtyEnd, 0);
            assert.equal(ring.offset, 0);
        });

        it('should reset dirty range on wrap', () => {
            const ring = new PushConstantRingBuffer(512, 256);
            ring.allocate(64);
            ring.allocate(64);
            ring.allocate(64); // Wraps here
            // After wrap, dirty range should be reset and then updated with new allocation
            assert.equal(ring.dirtyStart, 0);
            assert.equal(ring.dirtyEnd, 256);
        });
    });

    describe('Ring Buffer: Capacity Calculations', () => {
        it('should support 1024 draw calls at max push constant size', () => {
            // 256KB / 256 bytes per slot = 1024 slots
            const maxSlots = PUSH_CONSTANT_RING_SIZE / PUSH_CONSTANT_SLOT_ALIGNMENT;
            assert.equal(maxSlots, 1024);
        });

        it('should calculate correct number of slots for ring size', () => {
            const ring = new PushConstantRingBuffer();
            let count = 0;
            while (ring.offset + PUSH_CONSTANT_SLOT_ALIGNMENT <= ring.ringSize) {
                ring.allocate(MAX_PUSH_CONSTANT_SIZE);
                count++;
            }
            assert.equal(count, 1024);
            assert.equal(ring.wrapCount, 0); // Should fill exactly
        });

        it('should wrap on allocation 1025', () => {
            const ring = new PushConstantRingBuffer();
            for (let i = 0; i < 1024; i++) {
                ring.allocate(MAX_PUSH_CONSTANT_SIZE);
            }
            const result = ring.allocate(MAX_PUSH_CONSTANT_SIZE);
            assert.equal(result.wrapped, true);
            assert.equal(ring.wrapCount, 1);
        });
    });
}
