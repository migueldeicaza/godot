/**
 * Test: Shadow Buffer Copy Correctness
 *
 * Validates the shadow buffer mechanism used by the WebGPU driver.
 * Since WebGPU buffer mapping is async-only, the driver maintains a CPU-side
 * "shadow" buffer for each mapped buffer. Dirty range tracking ensures only
 * modified regions are flushed to the GPU via wgpuQueueWriteBuffer.
 *
 * Key behaviors tested:
 * - Dirty range tracking (offset, end)
 * - Range merging on overlapping writes
 * - Flush minimization (only dirty region is written)
 * - Dynamic buffer frame rotation
 *
 * Reference: webgpu_objects.h WGBuffer, rendering_device_driver_webgpu.cpp buffer_unmap()
 */

import { describe, it, assert } from './test_harness.mjs';

/**
 * Simulates the WGBuffer shadow buffer behavior.
 */
class ShadowBuffer {
    constructor(size) {
        this.size = size;
        this.shadow = new Uint8Array(size);
        this.dirtyOffset = 0;
        this.dirtyEnd = 0; // Exclusive end. 0 means "no explicit range set."
        this.mapDirty = false;
        this.flushLog = []; // Records of what was "flushed" to GPU
    }

    /**
     * Simulate a CPU write to the shadow buffer.
     * Updates dirty range tracking.
     */
    write(offset, data) {
        if (offset + data.length > this.size) {
            throw new Error(`Write out of bounds: offset=${offset}, len=${data.length}, size=${this.size}`);
        }
        for (let i = 0; i < data.length; i++) {
            this.shadow[offset + i] = data[i];
        }
        this.mapDirty = true;
        this._updateDirtyRange(offset, offset + data.length);
    }

    /**
     * Update dirty range tracking (matches the driver logic).
     */
    _updateDirtyRange(offset, end) {
        if (this.dirtyEnd === 0) {
            // No previous range — set directly.
            this.dirtyOffset = offset;
            this.dirtyEnd = end;
        } else {
            // Expand range to encompass new write.
            if (offset < this.dirtyOffset) {
                this.dirtyOffset = offset;
            }
            if (end > this.dirtyEnd) {
                this.dirtyEnd = end;
            }
        }
    }

    /**
     * Simulate buffer_unmap() flush logic.
     * Only writes the dirty region to the "GPU" (recorded in flushLog).
     */
    flush() {
        if (!this.mapDirty) {
            return null; // Nothing to flush
        }
        if (this.dirtyEnd === 0) {
            return null; // No dirty range
        }

        const region = {
            offset: this.dirtyOffset,
            size: this.dirtyEnd - this.dirtyOffset,
            data: Array.from(this.shadow.slice(this.dirtyOffset, this.dirtyEnd)),
        };
        this.flushLog.push(region);

        // Reset tracking after flush.
        this.dirtyOffset = 0;
        this.dirtyEnd = 0;
        this.mapDirty = false;

        return region;
    }

    /** Get the total bytes that would be flushed. */
    getDirtySize() {
        if (this.dirtyEnd === 0) return 0;
        return this.dirtyEnd - this.dirtyOffset;
    }
}

/**
 * Simulates dynamic persistent buffer frame rotation.
 */
class DynamicBuffer {
    constructor(perFrameSize, frameCount) {
        this.perFrameSize = perFrameSize;
        this.frameCount = frameCount;
        this.frameIdx = 0;
        this.totalSize = perFrameSize * frameCount;
        this.shadow = new ShadowBuffer(this.totalSize);
    }

    /** Get the current frame's slice offset. */
    getCurrentOffset() {
        return this.frameIdx * this.perFrameSize;
    }

    /** Write to current frame's slice. */
    writeToCurrentSlice(localOffset, data) {
        const globalOffset = this.getCurrentOffset() + localOffset;
        this.shadow.write(globalOffset, data);
    }

    /** Advance to next frame (rotate). */
    advanceFrame() {
        this.frameIdx = (this.frameIdx + 1) % this.frameCount;
    }

    isDynamic() {
        return this.frameIdx !== 0xFFFFFFFF;
    }
}

export function runTests() {
    describe('Shadow Buffer: Basic Dirty Tracking', () => {
        it('should start with no dirty range', () => {
            const buf = new ShadowBuffer(1024);
            assert.equal(buf.getDirtySize(), 0);
            assert.equal(buf.mapDirty, false);
        });

        it('should track a single write', () => {
            const buf = new ShadowBuffer(1024);
            buf.write(100, [1, 2, 3, 4]);
            assert.equal(buf.dirtyOffset, 100);
            assert.equal(buf.dirtyEnd, 104);
            assert.equal(buf.getDirtySize(), 4);
        });

        it('should expand dirty range on adjacent write', () => {
            const buf = new ShadowBuffer(1024);
            buf.write(100, [1, 2, 3, 4]);
            buf.write(104, [5, 6, 7, 8]);
            assert.equal(buf.dirtyOffset, 100);
            assert.equal(buf.dirtyEnd, 108);
            assert.equal(buf.getDirtySize(), 8);
        });

        it('should expand dirty range on non-adjacent write (creates gap)', () => {
            const buf = new ShadowBuffer(1024);
            buf.write(0, [1]);
            buf.write(500, [2]);
            // Dirty range encompasses both (conservative)
            assert.equal(buf.dirtyOffset, 0);
            assert.equal(buf.dirtyEnd, 501);
            assert.equal(buf.getDirtySize(), 501);
        });

        it('should shrink dirty range when write is inside existing range', () => {
            const buf = new ShadowBuffer(1024);
            buf.write(0, new Uint8Array(100)); // 0-100
            buf.write(50, [1, 2, 3]); // Inside existing range
            assert.equal(buf.dirtyOffset, 0);
            assert.equal(buf.dirtyEnd, 100); // No expansion needed
        });
    });

    describe('Shadow Buffer: Flush Behavior', () => {
        it('should flush only dirty region', () => {
            const buf = new ShadowBuffer(1024);
            buf.write(256, [0xAA, 0xBB, 0xCC, 0xDD]);
            const result = buf.flush();
            assert.equal(result.offset, 256);
            assert.equal(result.size, 4);
            assert.deepEqual(result.data, [0xAA, 0xBB, 0xCC, 0xDD]);
        });

        it('should return null when nothing is dirty', () => {
            const buf = new ShadowBuffer(1024);
            const result = buf.flush();
            assert.equal(result, null);
        });

        it('should reset dirty state after flush', () => {
            const buf = new ShadowBuffer(1024);
            buf.write(0, [1, 2, 3, 4]);
            buf.flush();
            assert.equal(buf.mapDirty, false);
            assert.equal(buf.getDirtySize(), 0);
        });

        it('should handle multiple flush cycles', () => {
            const buf = new ShadowBuffer(1024);
            buf.write(0, [1]);
            buf.flush();
            buf.write(512, [2]);
            const result = buf.flush();
            assert.equal(result.offset, 512);
            assert.equal(result.size, 1);
        });

        it('should not flush more than dirty range even with large buffer', () => {
            const buf = new ShadowBuffer(32 * 1024 * 1024); // 32MB staging buffer
            buf.write(1000, [42]); // Write 1 byte
            const result = buf.flush();
            assert.equal(result.size, 1); // Only 1 byte flushed, not 32MB
        });
    });

    describe('Shadow Buffer: Write Data Integrity', () => {
        it('should correctly store written data', () => {
            const buf = new ShadowBuffer(256);
            buf.write(0, [1, 2, 3, 4, 5, 6, 7, 8]);
            for (let i = 0; i < 8; i++) {
                assert.equal(buf.shadow[i], i + 1);
            }
        });

        it('should handle overwriting same region', () => {
            const buf = new ShadowBuffer(256);
            buf.write(0, [1, 2, 3, 4]);
            buf.write(0, [5, 6, 7, 8]);
            assert.deepEqual(Array.from(buf.shadow.slice(0, 4)), [5, 6, 7, 8]);
        });

        it('should handle write at buffer end', () => {
            const buf = new ShadowBuffer(256);
            buf.write(252, [1, 2, 3, 4]);
            assert.equal(buf.shadow[255], 4);
        });

        it('should throw on out-of-bounds write', () => {
            const buf = new ShadowBuffer(256);
            assert.throws(() => buf.write(253, [1, 2, 3, 4])); // 253+4=257 > 256
        });
    });

    describe('Shadow Buffer: Flush Log (GPU Write Batching)', () => {
        it('should record all flushes in order', () => {
            const buf = new ShadowBuffer(1024);
            buf.write(0, [1]);
            buf.flush();
            buf.write(100, [2]);
            buf.flush();
            buf.write(200, [3]);
            buf.flush();
            assert.equal(buf.flushLog.length, 3);
            assert.equal(buf.flushLog[0].offset, 0);
            assert.equal(buf.flushLog[1].offset, 100);
            assert.equal(buf.flushLog[2].offset, 200);
        });
    });

    describe('Dynamic Buffer: Frame Rotation', () => {
        it('should start at frame 0', () => {
            const buf = new DynamicBuffer(256, 3);
            assert.equal(buf.frameIdx, 0);
            assert.equal(buf.getCurrentOffset(), 0);
        });

        it('should advance through frames', () => {
            const buf = new DynamicBuffer(256, 3);
            assert.equal(buf.getCurrentOffset(), 0);
            buf.advanceFrame();
            assert.equal(buf.getCurrentOffset(), 256);
            buf.advanceFrame();
            assert.equal(buf.getCurrentOffset(), 512);
        });

        it('should wrap around after last frame', () => {
            const buf = new DynamicBuffer(256, 3);
            buf.advanceFrame(); // frame 1
            buf.advanceFrame(); // frame 2
            buf.advanceFrame(); // frame 0 (wrap)
            assert.equal(buf.frameIdx, 0);
            assert.equal(buf.getCurrentOffset(), 0);
        });

        it('should isolate writes to current frame slice', () => {
            const buf = new DynamicBuffer(256, 2);
            buf.writeToCurrentSlice(0, [0xAA]);
            buf.advanceFrame();
            buf.writeToCurrentSlice(0, [0xBB]);
            // Frame 0 data at offset 0, frame 1 data at offset 256
            assert.equal(buf.shadow.shadow[0], 0xAA);
            assert.equal(buf.shadow.shadow[256], 0xBB);
        });

        it('should report correct total size', () => {
            const buf = new DynamicBuffer(256, 3);
            assert.equal(buf.totalSize, 768);
        });

        it('should report isDynamic', () => {
            const buf = new DynamicBuffer(256, 3);
            assert.ok(buf.isDynamic());
        });
    });
}
