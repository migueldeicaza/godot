/**
 * Test: Texture Copyable Layout Computations
 *
 * Validates the logic for computing texture buffer copy layouts, including
 * compressed format block dimensions, row pitch alignment to 256 bytes,
 * mipmap dimension reduction, and full copyable layout calculations.
 *
 * Reference: rendering_device_driver_webgpu.cpp texture_get_copyable_layout()
 */

import { describe, it, assert } from './test_harness.mjs';

// WebGPU requires 256-byte row alignment for buffer <-> texture copies.
const ROW_ALIGNMENT = 256;

// Compressed format block specifications.
// Each entry: { blockW, blockH, blockBytes }
const CompressedFormats = {
    // BCn formats
    BC1:   { blockW: 4, blockH: 4, blockBytes: 8 },
    BC2:   { blockW: 4, blockH: 4, blockBytes: 16 },
    BC3:   { blockW: 4, blockH: 4, blockBytes: 16 },
    BC4:   { blockW: 4, blockH: 4, blockBytes: 8 },
    BC5:   { blockW: 4, blockH: 4, blockBytes: 16 },
    BC6H:  { blockW: 4, blockH: 4, blockBytes: 16 },
    BC7:   { blockW: 4, blockH: 4, blockBytes: 16 },
    // ETC2 / EAC formats
    ETC2_RGB8:   { blockW: 4, blockH: 4, blockBytes: 8 },
    ETC2_RGBA1:  { blockW: 4, blockH: 4, blockBytes: 8 },
    ETC2_RGBA8:  { blockW: 4, blockH: 4, blockBytes: 16 },
    EAC_R11:     { blockW: 4, blockH: 4, blockBytes: 8 },
    EAC_RG11:    { blockW: 4, blockH: 4, blockBytes: 16 },
    // ASTC formats
    ASTC_4x4:    { blockW: 4, blockH: 4, blockBytes: 16 },
    ASTC_5x4:    { blockW: 5, blockH: 4, blockBytes: 16 },
    ASTC_5x5:    { blockW: 5, blockH: 5, blockBytes: 16 },
    ASTC_6x5:    { blockW: 6, blockH: 5, blockBytes: 16 },
    ASTC_6x6:    { blockW: 6, blockH: 6, blockBytes: 16 },
    ASTC_8x5:    { blockW: 8, blockH: 5, blockBytes: 16 },
    ASTC_8x6:    { blockW: 8, blockH: 6, blockBytes: 16 },
    ASTC_8x8:    { blockW: 8, blockH: 8, blockBytes: 16 },
    ASTC_10x5:   { blockW: 10, blockH: 5, blockBytes: 16 },
    ASTC_10x6:   { blockW: 10, blockH: 6, blockBytes: 16 },
    ASTC_10x8:   { blockW: 10, blockH: 8, blockBytes: 16 },
    ASTC_10x10:  { blockW: 10, blockH: 10, blockBytes: 16 },
    ASTC_12x10:  { blockW: 12, blockH: 10, blockBytes: 16 },
    ASTC_12x12:  { blockW: 12, blockH: 12, blockBytes: 16 },
};

// Uncompressed pixel sizes (bytes per pixel) for common formats.
const UncompressedPixelSize = {
    R8Unorm: 1,
    RG8Unorm: 2,
    RGBA8Unorm: 4,
    BGRA8Unorm: 4,
    R16Float: 2,
    RG16Float: 4,
    RGBA16Float: 8,
    R32Float: 4,
    RG32Float: 8,
    RGBA32Float: 16,
    Depth32Float: 4,
};

/**
 * Compute the number of blocks needed to cover a dimension.
 * ceil(dim / blockDim)
 */
function blocksForDimension(dim, blockDim) {
    return Math.ceil(dim / blockDim);
}

/**
 * Compute the aligned row pitch: ceil(rowBytes / 256) * 256.
 * Matches the driver: ((row_bytes + 255) / 256) * 256
 */
function alignRowPitch(rowBytes) {
    return Math.ceil(rowBytes / ROW_ALIGNMENT) * ROW_ALIGNMENT;
}

/**
 * Compute row pitch for a compressed format.
 * row_bytes = ceil(width / blockW) * blockBytes
 * Then aligned to 256 bytes.
 */
function compressedRowPitch(width, fmt) {
    const blocksWide = blocksForDimension(width, fmt.blockW);
    const rowBytes = blocksWide * fmt.blockBytes;
    return alignRowPitch(rowBytes);
}

/**
 * Compute mipmap dimension: max(1, dim >> mipLevel).
 * For compressed formats, the pixel dimension is divided but must cover
 * at least 1 block (the block alignment handles sub-block mips).
 */
function mipDimension(dim, mipLevel) {
    return Math.max(1, dim >> mipLevel);
}

/**
 * Compute full copyable layout for a texture at a given mip level.
 *
 * For compressed formats:
 *   mip_width  = max(1, width >> mip)
 *   mip_height = max(1, height >> mip)
 *   blocks_wide = ceil(mip_width / blockW)
 *   blocks_tall = ceil(mip_height / blockH)
 *   row_bytes  = blocks_wide * blockBytes
 *   row_pitch  = align(row_bytes, 256)
 *   size       = row_pitch * blocks_tall
 *
 * For uncompressed formats:
 *   mip_width  = max(1, width >> mip)
 *   mip_height = max(1, height >> mip)
 *   row_bytes  = mip_width * pixelSize
 *   row_pitch  = align(row_bytes, 256)
 *   size       = row_pitch * mip_height
 */
function computeCopyableLayout(width, height, mipLevel, formatInfo) {
    const mipW = mipDimension(width, mipLevel);
    const mipH = mipDimension(height, mipLevel);

    let rowBytes, rowsForSize;

    if (formatInfo.blockW && formatInfo.blockW > 1) {
        // Compressed format.
        const blocksWide = blocksForDimension(mipW, formatInfo.blockW);
        const blocksTall = blocksForDimension(mipH, formatInfo.blockH);
        rowBytes = blocksWide * formatInfo.blockBytes;
        rowsForSize = blocksTall;
    } else {
        // Uncompressed format.
        rowBytes = mipW * formatInfo.pixelSize;
        rowsForSize = mipH;
    }

    const rowPitch = alignRowPitch(rowBytes);
    const size = rowPitch * rowsForSize;

    return { rowPitch, size, mipW, mipH };
}

export function runTests() {
    describe('Texture Layout: Compressed Format Block Dimensions', () => {
        it('should have BC1 as 4x4 blocks of 8 bytes', () => {
            const fmt = CompressedFormats.BC1;
            assert.equal(fmt.blockW, 4);
            assert.equal(fmt.blockH, 4);
            assert.equal(fmt.blockBytes, 8);
        });

        it('should have BC3 as 4x4 blocks of 16 bytes', () => {
            const fmt = CompressedFormats.BC3;
            assert.equal(fmt.blockW, 4);
            assert.equal(fmt.blockH, 4);
            assert.equal(fmt.blockBytes, 16);
        });

        it('should have ETC2_RGB8 as 4x4 blocks of 8 bytes', () => {
            const fmt = CompressedFormats.ETC2_RGB8;
            assert.equal(fmt.blockW, 4);
            assert.equal(fmt.blockH, 4);
            assert.equal(fmt.blockBytes, 8);
        });

        it('should have ASTC 4x4 as 4x4 blocks of 16 bytes', () => {
            const fmt = CompressedFormats.ASTC_4x4;
            assert.equal(fmt.blockW, 4);
            assert.equal(fmt.blockH, 4);
            assert.equal(fmt.blockBytes, 16);
        });

        it('should have ASTC 8x8 as 8x8 blocks of 16 bytes', () => {
            const fmt = CompressedFormats.ASTC_8x8;
            assert.equal(fmt.blockW, 8);
            assert.equal(fmt.blockH, 8);
            assert.equal(fmt.blockBytes, 16);
        });

        it('should have all ASTC variants at 16 bytes per block', () => {
            for (const [name, fmt] of Object.entries(CompressedFormats)) {
                if (name.startsWith('ASTC')) {
                    assert.equal(fmt.blockBytes, 16, `${name} should be 16 bytes/block`);
                }
            }
        });

        it('should have BC4 and EAC_R11 as 8 bytes per block', () => {
            assert.equal(CompressedFormats.BC4.blockBytes, 8);
            assert.equal(CompressedFormats.EAC_R11.blockBytes, 8);
        });
    });

    describe('Texture Layout: Row Pitch Alignment', () => {
        it('should align to 256 bytes for small row sizes', () => {
            assert.equal(alignRowPitch(1), 256);
            assert.equal(alignRowPitch(100), 256);
            assert.equal(alignRowPitch(255), 256);
        });

        it('should keep exact 256 when row bytes are exactly 256', () => {
            assert.equal(alignRowPitch(256), 256);
        });

        it('should round up to 512 for 257 bytes', () => {
            assert.equal(alignRowPitch(257), 512);
        });

        it('should handle large row sizes', () => {
            // 4096-wide RGBA8: 4096 * 4 = 16384, already aligned
            assert.equal(alignRowPitch(16384), 16384);
            // 4095-wide RGBA8: 4095 * 4 = 16380 -> 16384 (ceil to next 256)
            assert.equal(alignRowPitch(16380), 16384);
        });

        it('should handle zero (edge case)', () => {
            assert.equal(alignRowPitch(0), 0);
        });
    });

    describe('Texture Layout: Compressed Format Row Pitch', () => {
        it('should compute row pitch for 256-wide BC1 texture', () => {
            // 256 / 4 = 64 blocks, 64 * 8 = 512 bytes, align(512,256) = 512
            assert.equal(compressedRowPitch(256, CompressedFormats.BC1), 512);
        });

        it('should compute row pitch for 128-wide BC1 texture', () => {
            // 128 / 4 = 32 blocks, 32 * 8 = 256 bytes, align(256,256) = 256
            assert.equal(compressedRowPitch(128, CompressedFormats.BC1), 256);
        });

        it('should compute row pitch for 100-wide BC1 (non-block-aligned width)', () => {
            // ceil(100/4) = 25 blocks, 25 * 8 = 200, align(200,256) = 256
            assert.equal(compressedRowPitch(100, CompressedFormats.BC1), 256);
        });

        it('should compute row pitch for 256-wide BC3 texture', () => {
            // 256 / 4 = 64 blocks, 64 * 16 = 1024, align(1024,256) = 1024
            assert.equal(compressedRowPitch(256, CompressedFormats.BC3), 1024);
        });

        it('should compute row pitch for 4-wide BC1 (single block wide)', () => {
            // ceil(4/4) = 1 block, 1 * 8 = 8, align(8,256) = 256
            assert.equal(compressedRowPitch(4, CompressedFormats.BC1), 256);
        });

        it('should compute row pitch for sub-block width (2 pixels, BC1)', () => {
            // ceil(2/4) = 1 block, 1 * 8 = 8, align(8,256) = 256
            assert.equal(compressedRowPitch(2, CompressedFormats.BC1), 256);
        });

        it('should compute row pitch for 1-pixel-wide BC3', () => {
            // ceil(1/4) = 1 block, 1 * 16 = 16, align(16,256) = 256
            assert.equal(compressedRowPitch(1, CompressedFormats.BC3), 256);
        });

        it('should compute row pitch for ASTC 8x8 at 256 width', () => {
            // ceil(256/8) = 32 blocks, 32 * 16 = 512, align(512,256) = 512
            assert.equal(compressedRowPitch(256, CompressedFormats.ASTC_8x8), 512);
        });

        it('should compute row pitch for ETC2_RGB8 at 512 width', () => {
            // ceil(512/4) = 128 blocks, 128 * 8 = 1024, align(1024,256) = 1024
            assert.equal(compressedRowPitch(512, CompressedFormats.ETC2_RGB8), 1024);
        });
    });

    describe('Texture Layout: Mipmap Dimension Reduction', () => {
        it('should halve dimensions at each mip level', () => {
            assert.equal(mipDimension(256, 0), 256);
            assert.equal(mipDimension(256, 1), 128);
            assert.equal(mipDimension(256, 2), 64);
            assert.equal(mipDimension(256, 3), 32);
            assert.equal(mipDimension(256, 4), 16);
        });

        it('should clamp to minimum of 1', () => {
            assert.equal(mipDimension(1, 0), 1);
            assert.equal(mipDimension(1, 1), 1);
            assert.equal(mipDimension(2, 1), 1);
            assert.equal(mipDimension(2, 2), 1);
        });

        it('should handle non-power-of-two dimensions', () => {
            // 100 >> 1 = 50, >> 2 = 25, >> 3 = 12, >> 4 = 6, >> 5 = 3, >> 6 = 1
            assert.equal(mipDimension(100, 1), 50);
            assert.equal(mipDimension(100, 2), 25);
            assert.equal(mipDimension(100, 3), 12);
            assert.equal(mipDimension(100, 4), 6);
            assert.equal(mipDimension(100, 5), 3);
            assert.equal(mipDimension(100, 6), 1);
        });

        it('should handle odd dimensions (truncation on shift)', () => {
            // 7 >> 1 = 3, >> 2 = 1
            assert.equal(mipDimension(7, 1), 3);
            assert.equal(mipDimension(7, 2), 1);
        });

        it('should handle rectangular textures independently', () => {
            assert.equal(mipDimension(512, 2), 128);
            assert.equal(mipDimension(256, 2), 64);
        });
    });

    describe('Texture Layout: Full Copyable Layout (Compressed)', () => {
        it('should compute layout for 256x256 BC1 mip 0', () => {
            const layout = computeCopyableLayout(256, 256, 0, CompressedFormats.BC1);
            // blocks: 64x64, row = 64*8 = 512, pitch = 512, size = 512*64 = 32768
            assert.equal(layout.rowPitch, 512);
            assert.equal(layout.size, 512 * 64);
        });

        it('should compute layout for 256x256 BC1 mip 1 (128x128)', () => {
            const layout = computeCopyableLayout(256, 256, 1, CompressedFormats.BC1);
            // mip: 128x128, blocks: 32x32, row = 32*8 = 256, pitch = 256, size = 256*32 = 8192
            assert.equal(layout.mipW, 128);
            assert.equal(layout.mipH, 128);
            assert.equal(layout.rowPitch, 256);
            assert.equal(layout.size, 256 * 32);
        });

        it('should compute layout for 256x256 BC3 mip 0', () => {
            const layout = computeCopyableLayout(256, 256, 0, CompressedFormats.BC3);
            // blocks: 64x64, row = 64*16 = 1024, pitch = 1024, size = 1024*64 = 65536
            assert.equal(layout.rowPitch, 1024);
            assert.equal(layout.size, 1024 * 64);
        });

        it('should handle sub-block mips for compressed formats', () => {
            // 4x4 BC1 at mip 1: mip = 2x2 pixels, but still 1x1 block
            const layout = computeCopyableLayout(4, 4, 1, CompressedFormats.BC1);
            assert.equal(layout.mipW, 2);
            assert.equal(layout.mipH, 2);
            // ceil(2/4)=1 block wide, ceil(2/4)=1 block tall
            // row = 1*8 = 8, pitch = 256, size = 256*1 = 256
            assert.equal(layout.rowPitch, 256);
            assert.equal(layout.size, 256);
        });

        it('should handle 1x1 mip for compressed formats (minimum 1 block)', () => {
            // 256x256 at mip 8: 1x1 pixels, 1x1 block
            const layout = computeCopyableLayout(256, 256, 8, CompressedFormats.BC1);
            assert.equal(layout.mipW, 1);
            assert.equal(layout.mipH, 1);
            assert.equal(layout.rowPitch, 256); // align(8, 256) = 256
            assert.equal(layout.size, 256);     // 256 * 1 block
        });

        it('should compute layout for ASTC 4x4 at 512x512 mip 0', () => {
            const layout = computeCopyableLayout(512, 512, 0, CompressedFormats.ASTC_4x4);
            // blocks: 128x128, row = 128*16 = 2048, pitch = 2048, size = 2048*128 = 262144
            assert.equal(layout.rowPitch, 2048);
            assert.equal(layout.size, 2048 * 128);
        });

        it('should compute layout for non-power-of-two compressed texture', () => {
            // 100x100 ETC2_RGB8 mip 0
            const layout = computeCopyableLayout(100, 100, 0, CompressedFormats.ETC2_RGB8);
            // blocks: ceil(100/4)=25 x ceil(100/4)=25
            // row = 25*8 = 200, pitch = 256, size = 256*25 = 6400
            assert.equal(layout.rowPitch, 256);
            assert.equal(layout.size, 256 * 25);
        });
    });

    describe('Texture Layout: Full Copyable Layout (Uncompressed)', () => {
        it('should compute layout for 256x256 RGBA8 mip 0', () => {
            const layout = computeCopyableLayout(256, 256, 0, { pixelSize: 4 });
            // row = 256*4 = 1024, pitch = 1024, size = 1024*256 = 262144
            assert.equal(layout.rowPitch, 1024);
            assert.equal(layout.size, 1024 * 256);
        });

        it('should compute layout for 256x256 RGBA8 mip 1 (128x128)', () => {
            const layout = computeCopyableLayout(256, 256, 1, { pixelSize: 4 });
            assert.equal(layout.mipW, 128);
            assert.equal(layout.mipH, 128);
            assert.equal(layout.rowPitch, 512);
            assert.equal(layout.size, 512 * 128);
        });

        it('should align small uncompressed rows to 256', () => {
            // 1x1 R8: row = 1, pitch = 256, size = 256
            const layout = computeCopyableLayout(1, 1, 0, { pixelSize: 1 });
            assert.equal(layout.rowPitch, 256);
            assert.equal(layout.size, 256);
        });

        it('should use promoted pixel size for R8 promoted to R32Float', () => {
            // When R8 is promoted to R32Float, gpu pixel size = 4, not 1.
            // 64x64 with R32Float bpp: row = 64*4 = 256, pitch = 256, size = 256*64
            const layout = computeCopyableLayout(64, 64, 0, { pixelSize: 4 });
            assert.equal(layout.rowPitch, 256);
            assert.equal(layout.size, 256 * 64);
        });

        it('should handle 4096x4096 RGBA32Float', () => {
            const layout = computeCopyableLayout(4096, 4096, 0, { pixelSize: 16 });
            // row = 4096*16 = 65536, already aligned, size = 65536*4096
            assert.equal(layout.rowPitch, 65536);
            assert.equal(layout.size, 65536 * 4096);
        });

        it('should handle non-aligned uncompressed row widths', () => {
            // 100x100 RGBA8: row = 100*4 = 400, pitch = 512, size = 512*100
            const layout = computeCopyableLayout(100, 100, 0, { pixelSize: 4 });
            assert.equal(layout.rowPitch, 512);
            assert.equal(layout.size, 512 * 100);
        });
    });
}
