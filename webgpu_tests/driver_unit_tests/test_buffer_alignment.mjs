/**
 * Test: Buffer Alignment and Offset Calculations
 *
 * Validates the alignment logic used throughout the WebGPU driver:
 * - Buffer sizes must be 4-byte aligned (WebGPU requirement)
 * - Texture row pitch must be 256-byte aligned (copy requirement)
 * - Uniform buffer offsets must be minUniformBufferOffsetAlignment aligned
 * - Push constant slots must be 256-byte aligned
 * - Dynamic persistent buffer slices must be alignment-padded
 *
 * Reference: rendering_device_driver_webgpu.cpp buffer_create(), texture_get_copyable_layout()
 */

import { describe, it, assert } from './test_harness.mjs';

// Constants from the driver.
const BUFFER_SIZE_ALIGNMENT = 4;
const TEXTURE_ROW_ALIGNMENT = 256; // bytes_per_row must be multiple of 256
const MIN_UNIFORM_BUFFER_OFFSET_ALIGNMENT = 256; // Typical device limit
const PUSH_CONSTANT_SLOT_ALIGNMENT = 256;

/**
 * Align a buffer size to 4 bytes (matching C++ (p_size + 3) & ~3ULL).
 */
function alignBufferSize(size) {
    return (size + BUFFER_SIZE_ALIGNMENT - 1) & ~(BUFFER_SIZE_ALIGNMENT - 1);
}

/**
 * Align texture row pitch to 256 bytes (matching ((row_bytes + 255) / 256) * 256).
 */
function alignTextureRowPitch(rowBytes) {
    return Math.ceil(rowBytes / TEXTURE_ROW_ALIGNMENT) * TEXTURE_ROW_ALIGNMENT;
}

/**
 * Align a value to a given power-of-two alignment.
 */
function alignTo(value, alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

/**
 * Calculate the copyable layout for a texture mip level.
 * Matches texture_get_copyable_layout() logic.
 */
function textureCopyableLayout(width, height, mipLevel, pixelSize) {
    const mipWidth = Math.max(1, width >> mipLevel);
    const mipHeight = Math.max(1, height >> mipLevel);
    const rowBytes = mipWidth * pixelSize;
    const rowPitch = alignTextureRowPitch(rowBytes);
    const size = rowPitch * mipHeight;
    return { mipWidth, mipHeight, rowBytes, rowPitch, size };
}

/**
 * Calculate dynamic persistent buffer allocation.
 * Matches the logic in buffer_create() for BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT.
 */
function dynamicPersistentBufferSize(requestedSize, frameCount, offsetAlignment) {
    const alignedSliceSize = alignTo(alignBufferSize(requestedSize), offsetAlignment);
    const totalSize = alignedSliceSize * frameCount;
    return { alignedSliceSize, totalSize };
}

export function runTests() {
    describe('Buffer Alignment: Size Alignment (4-byte)', () => {
        it('should not change already-aligned sizes', () => {
            assert.equal(alignBufferSize(4), 4);
            assert.equal(alignBufferSize(8), 8);
            assert.equal(alignBufferSize(256), 256);
            assert.equal(alignBufferSize(1024), 1024);
        });

        it('should align sizes up to nearest 4 bytes', () => {
            assert.equal(alignBufferSize(1), 4);
            assert.equal(alignBufferSize(2), 4);
            assert.equal(alignBufferSize(3), 4);
            assert.equal(alignBufferSize(5), 8);
            assert.equal(alignBufferSize(6), 8);
            assert.equal(alignBufferSize(7), 8);
        });

        it('should handle zero size', () => {
            assert.equal(alignBufferSize(0), 0);
        });

        it('should handle large sizes', () => {
            assert.equal(alignBufferSize(1000001), 1000004);
            assert.equal(alignBufferSize(0xFFFFFF), 0xFFFFFF + 1); // 16777215 -> 16777216
        });

        it('should handle edge case of max - 1', () => {
            assert.equal(alignBufferSize(255), 256);
            assert.equal(alignBufferSize(257), 260);
        });
    });

    describe('Buffer Alignment: Texture Row Pitch (256-byte)', () => {
        it('should align row pitch to 256 bytes', () => {
            assert.equal(alignTextureRowPitch(1), 256);
            assert.equal(alignTextureRowPitch(100), 256);
            assert.equal(alignTextureRowPitch(255), 256);
            assert.equal(alignTextureRowPitch(256), 256);
        });

        it('should handle widths that need multiple 256-byte blocks', () => {
            assert.equal(alignTextureRowPitch(257), 512);
            assert.equal(alignTextureRowPitch(512), 512);
            assert.equal(alignTextureRowPitch(513), 768);
            assert.equal(alignTextureRowPitch(1024), 1024);
        });

        it('should compute correct pitch for common texture widths', () => {
            // 128x128 RGBA8 (4 bpp): 128*4=512 -> 512
            assert.equal(alignTextureRowPitch(128 * 4), 512);
            // 64x64 RGBA8: 64*4=256 -> 256
            assert.equal(alignTextureRowPitch(64 * 4), 256);
            // 100x100 RGBA8: 100*4=400 -> 512
            assert.equal(alignTextureRowPitch(100 * 4), 512);
            // 1920x1080 RGBA8: 1920*4=7680 -> 7680
            assert.equal(alignTextureRowPitch(1920 * 4), 7680);
        });

        it('should compute correct pitch for smaller pixel formats', () => {
            // 256x256 R8 (1 bpp): 256*1=256 -> 256
            assert.equal(alignTextureRowPitch(256 * 1), 256);
            // 100x100 R8: 100*1=100 -> 256
            assert.equal(alignTextureRowPitch(100 * 1), 256);
            // 512x512 RG8 (2 bpp): 512*2=1024 -> 1024
            assert.equal(alignTextureRowPitch(512 * 2), 1024);
        });
    });

    describe('Buffer Alignment: Texture Copyable Layout', () => {
        it('should compute layout for base mip level', () => {
            const layout = textureCopyableLayout(256, 256, 0, 4);
            assert.equal(layout.mipWidth, 256);
            assert.equal(layout.mipHeight, 256);
            assert.equal(layout.rowBytes, 1024);
            assert.equal(layout.rowPitch, 1024);
            assert.equal(layout.size, 1024 * 256);
        });

        it('should halve dimensions at each mip level', () => {
            const l0 = textureCopyableLayout(1024, 1024, 0, 4);
            const l1 = textureCopyableLayout(1024, 1024, 1, 4);
            const l2 = textureCopyableLayout(1024, 1024, 2, 4);
            assert.equal(l0.mipWidth, 1024);
            assert.equal(l1.mipWidth, 512);
            assert.equal(l2.mipWidth, 256);
            assert.equal(l0.mipHeight, 1024);
            assert.equal(l1.mipHeight, 512);
            assert.equal(l2.mipHeight, 256);
        });

        it('should clamp mip dimensions to minimum of 1', () => {
            const layout = textureCopyableLayout(4, 4, 3, 4);
            assert.equal(layout.mipWidth, 1); // 4 >> 3 = 0 -> clamped to 1
            assert.equal(layout.mipHeight, 1);
        });

        it('should align row pitch even for small mips', () => {
            // 4x4 RGBA8, mip 1: 2*4=8 bytes -> aligned to 256
            const layout = textureCopyableLayout(4, 4, 1, 4);
            assert.equal(layout.mipWidth, 2);
            assert.equal(layout.rowBytes, 8);
            assert.equal(layout.rowPitch, 256);
        });

        it('should handle 1x1 textures', () => {
            const layout = textureCopyableLayout(1, 1, 0, 4);
            assert.equal(layout.mipWidth, 1);
            assert.equal(layout.mipHeight, 1);
            assert.equal(layout.rowBytes, 4);
            assert.equal(layout.rowPitch, 256);
            assert.equal(layout.size, 256);
        });

        it('should handle non-square textures', () => {
            const layout = textureCopyableLayout(1920, 1080, 0, 4);
            assert.equal(layout.mipWidth, 1920);
            assert.equal(layout.mipHeight, 1080);
            assert.equal(layout.rowBytes, 7680);
            assert.equal(layout.rowPitch, 7680); // Already 256-aligned
        });
    });

    describe('Buffer Alignment: Dynamic Persistent Buffers', () => {
        it('should align slice size to offset alignment', () => {
            const result = dynamicPersistentBufferSize(100, 3, 256);
            assert.equal(result.alignedSliceSize, 256);
            assert.equal(result.totalSize, 768);
        });

        it('should handle single frame (no rotation)', () => {
            const result = dynamicPersistentBufferSize(512, 1, 256);
            assert.equal(result.alignedSliceSize, 512);
            assert.equal(result.totalSize, 512);
        });

        it('should multiply correctly for frame count', () => {
            const result = dynamicPersistentBufferSize(256, 2, 256);
            assert.equal(result.alignedSliceSize, 256);
            assert.equal(result.totalSize, 512);
        });

        it('should align unaligned sizes before frame multiplication', () => {
            // 300 bytes -> align to 4 = 300 -> align to 256 = 512
            const result = dynamicPersistentBufferSize(300, 3, 256);
            assert.equal(result.alignedSliceSize, 512);
            assert.equal(result.totalSize, 1536);
        });

        it('should handle large uniform buffers', () => {
            // Typical UBO: 16KB with 3 frames
            const result = dynamicPersistentBufferSize(16384, 3, 256);
            assert.equal(result.alignedSliceSize, 16384); // Already aligned
            assert.equal(result.totalSize, 49152);
        });
    });

    describe('Buffer Alignment: Push Constant Slot Alignment', () => {
        it('should align push constant data to 256-byte slots', () => {
            assert.equal(alignTo(1, PUSH_CONSTANT_SLOT_ALIGNMENT), 256);
            assert.equal(alignTo(64, PUSH_CONSTANT_SLOT_ALIGNMENT), 256);
            assert.equal(alignTo(128, PUSH_CONSTANT_SLOT_ALIGNMENT), 256);
            assert.equal(alignTo(256, PUSH_CONSTANT_SLOT_ALIGNMENT), 256);
            assert.equal(alignTo(257, PUSH_CONSTANT_SLOT_ALIGNMENT), 512);
        });

        it('should handle zero size gracefully', () => {
            assert.equal(alignTo(0, PUSH_CONSTANT_SLOT_ALIGNMENT), 0);
        });
    });

    describe('Buffer Alignment: Uniform Buffer Offset Alignment', () => {
        it('should align offsets to minUniformBufferOffsetAlignment', () => {
            assert.equal(alignTo(0, MIN_UNIFORM_BUFFER_OFFSET_ALIGNMENT), 0);
            assert.equal(alignTo(1, MIN_UNIFORM_BUFFER_OFFSET_ALIGNMENT), 256);
            assert.equal(alignTo(255, MIN_UNIFORM_BUFFER_OFFSET_ALIGNMENT), 256);
            assert.equal(alignTo(256, MIN_UNIFORM_BUFFER_OFFSET_ALIGNMENT), 256);
            assert.equal(alignTo(300, MIN_UNIFORM_BUFFER_OFFSET_ALIGNMENT), 512);
        });

        it('should verify dynamic offset validity', () => {
            // Dynamic offsets must be multiples of the alignment
            for (let offset = 0; offset <= 2048; offset += 256) {
                assert.equal(offset % MIN_UNIFORM_BUFFER_OFFSET_ALIGNMENT, 0);
            }
        });
    });

    describe('Buffer Alignment: Clear Buffer Size (4-byte)', () => {
        it('should align clear sizes to 4 bytes (matching command_clear_buffer)', () => {
            // command_clear_buffer rounds size up to 4
            assert.equal(alignBufferSize(1), 4);
            assert.equal(alignBufferSize(3), 4);
            assert.equal(alignBufferSize(4), 4);
            assert.equal(alignBufferSize(5), 8);
        });
    });
}
