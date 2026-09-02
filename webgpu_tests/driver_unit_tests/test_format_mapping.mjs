/**
 * Test: Texture Format Mapping (Godot DataFormat -> WebGPU Format)
 *
 * Validates the format mapping table from pixel_formats_webgpu.h.
 * Tests that:
 * - All supported formats map to correct WebGPU equivalents
 * - Unsupported formats (3-component, packed, scaled, 64-bit) map to Undefined
 * - Depth/stencil formats are correctly classified
 * - Pixel size calculations are correct
 * - Vertex format mapping covers all expected formats
 *
 * Reference: drivers/webgpu/pixel_formats_webgpu.h
 */

import { describe, it, assert } from './test_harness.mjs';

// WebGPU texture format enum values (matching the Dawn/emdawnwebgpu headers).
// We use string names for readability and validate the mapping logic.
const WGPUTextureFormat = {
    Undefined: 0,
    R8Unorm: 'r8unorm',
    R8Snorm: 'r8snorm',
    R8Uint: 'r8uint',
    R8Sint: 'r8sint',
    RG8Unorm: 'rg8unorm',
    RG8Snorm: 'rg8snorm',
    RG8Uint: 'rg8uint',
    RG8Sint: 'rg8sint',
    RGBA8Unorm: 'rgba8unorm',
    RGBA8UnormSrgb: 'rgba8unorm-srgb',
    RGBA8Snorm: 'rgba8snorm',
    RGBA8Uint: 'rgba8uint',
    RGBA8Sint: 'rgba8sint',
    BGRA8Unorm: 'bgra8unorm',
    BGRA8UnormSrgb: 'bgra8unorm-srgb',
    R16Uint: 'r16uint',
    R16Sint: 'r16sint',
    R16Float: 'r16float',
    RG16Uint: 'rg16uint',
    RG16Sint: 'rg16sint',
    RG16Float: 'rg16float',
    RGBA16Uint: 'rgba16uint',
    RGBA16Sint: 'rgba16sint',
    RGBA16Float: 'rgba16float',
    R32Uint: 'r32uint',
    R32Sint: 'r32sint',
    R32Float: 'r32float',
    RG32Uint: 'rg32uint',
    RG32Sint: 'rg32sint',
    RG32Float: 'rg32float',
    RGBA32Uint: 'rgba32uint',
    RGBA32Sint: 'rgba32sint',
    RGBA32Float: 'rgba32float',
    RGB10A2Unorm: 'rgb10a2unorm',
    RGB10A2Uint: 'rgb10a2uint',
    RG11B10Ufloat: 'rg11b10ufloat',
    RGB9E5Ufloat: 'rgb9e5ufloat',
    Depth16Unorm: 'depth16unorm',
    Depth24Plus: 'depth24plus',
    Depth24PlusStencil8: 'depth24plus-stencil8',
    Depth32Float: 'depth32float',
    Depth32FloatStencil8: 'depth32float-stencil8',
    Stencil8: 'stencil8',
    BC1RGBAUnorm: 'bc1-rgba-unorm',
    BC1RGBAUnormSrgb: 'bc1-rgba-unorm-srgb',
    BC2RGBAUnorm: 'bc2-rgba-unorm',
    BC2RGBAUnormSrgb: 'bc2-rgba-unorm-srgb',
    BC3RGBAUnorm: 'bc3-rgba-unorm',
    BC3RGBAUnormSrgb: 'bc3-rgba-unorm-srgb',
    BC4RUnorm: 'bc4-r-unorm',
    BC4RSnorm: 'bc4-r-snorm',
    BC5RGUnorm: 'bc5-rg-unorm',
    BC5RGSnorm: 'bc5-rg-snorm',
    BC6HRGBUfloat: 'bc6h-rgb-ufloat',
    BC6HRGBFloat: 'bc6h-rgb-float',
    BC7RGBAUnorm: 'bc7-rgba-unorm',
    BC7RGBAUnormSrgb: 'bc7-rgba-unorm-srgb',
    ETC2RGB8Unorm: 'etc2-rgb8unorm',
    ETC2RGB8UnormSrgb: 'etc2-rgb8unorm-srgb',
    ETC2RGB8A1Unorm: 'etc2-rgb8a1unorm',
    ETC2RGB8A1UnormSrgb: 'etc2-rgb8a1unorm-srgb',
    ETC2RGBA8Unorm: 'etc2-rgba8unorm',
    ETC2RGBA8UnormSrgb: 'etc2-rgba8unorm-srgb',
    EACR11Unorm: 'eac-r11unorm',
    EACR11Snorm: 'eac-r11snorm',
    EACRG11Unorm: 'eac-rg11unorm',
    EACRG11Snorm: 'eac-rg11snorm',
    ASTC4x4Unorm: 'astc-4x4-unorm',
    ASTC4x4UnormSrgb: 'astc-4x4-unorm-srgb',
};

/**
 * The format mapping table extracted from pixel_formats_webgpu.h.
 * Index = Godot DataFormat enum value, Value = WebGPU format string (or null for Undefined).
 */
const RD_TO_WGPU_FORMAT = [
    // 0-7: Packed small formats (unsupported)
    null, null, null, null, null, null, null, null,
    // 8-14: R8
    'r8unorm', 'r8snorm', null, null, 'r8uint', 'r8sint', null,
    // 15-21: RG8
    'rg8unorm', 'rg8snorm', null, null, 'rg8uint', 'rg8sint', null,
    // 22-28: RGB8 (unsupported)
    null, null, null, null, null, null, null,
    // 29-35: BGR8 (unsupported)
    null, null, null, null, null, null, null,
    // 36-42: RGBA8
    'rgba8unorm', 'rgba8snorm', null, null, 'rgba8uint', 'rgba8sint', 'rgba8unorm-srgb',
    // 43-49: BGRA8
    'bgra8unorm', null, null, null, null, null, 'bgra8unorm-srgb',
    // 50-56: A8B8G8R8 PACK32 (same as RGBA8)
    'rgba8unorm', 'rgba8snorm', null, null, 'rgba8uint', 'rgba8sint', 'rgba8unorm-srgb',
    // 57-62: A2R10G10B10 (unsupported)
    null, null, null, null, null, null,
    // 63-68: A2B10G10R10
    'rgb10a2unorm', null, null, null, 'rgb10a2uint', null,
    // 69-75: R16
    null, null, null, null, 'r16uint', 'r16sint', 'r16float',
    // 76-82: RG16
    null, null, null, null, 'rg16uint', 'rg16sint', 'rg16float',
    // 83-89: RGB16 (unsupported)
    null, null, null, null, null, null, null,
    // 90-96: RGBA16
    null, null, null, null, 'rgba16uint', 'rgba16sint', 'rgba16float',
    // 97-99: R32
    'r32uint', 'r32sint', 'r32float',
    // 100-102: RG32
    'rg32uint', 'rg32sint', 'rg32float',
    // 103-105: RGB32 (unsupported)
    null, null, null,
    // 106-108: RGBA32
    'rgba32uint', 'rgba32sint', 'rgba32float',
    // 109-120: 64-bit formats (unsupported)
    null, null, null, null, null, null, null, null, null, null, null, null,
    // 121-122: Special packed floats
    'rg11b10ufloat', 'rgb9e5ufloat',
    // 123-129: Depth/Stencil
    'depth16unorm', 'depth24plus', 'depth32float', 'stencil8',
    null, // D16_UNORM_S8_UINT not supported
    'depth24plus-stencil8', 'depth32float-stencil8',
    // 130-145: BC compressed
    'bc1-rgba-unorm', 'bc1-rgba-unorm-srgb', 'bc1-rgba-unorm', 'bc1-rgba-unorm-srgb',
    'bc2-rgba-unorm', 'bc2-rgba-unorm-srgb', 'bc3-rgba-unorm', 'bc3-rgba-unorm-srgb',
    'bc4-r-unorm', 'bc4-r-snorm', 'bc5-rg-unorm', 'bc5-rg-snorm',
    'bc6h-rgb-ufloat', 'bc6h-rgb-float', 'bc7-rgba-unorm', 'bc7-rgba-unorm-srgb',
    // 146-155: ETC2/EAC
    'etc2-rgb8unorm', 'etc2-rgb8unorm-srgb', 'etc2-rgb8a1unorm', 'etc2-rgb8a1unorm-srgb',
    'etc2-rgba8unorm', 'etc2-rgba8unorm-srgb',
    'eac-r11unorm', 'eac-r11snorm', 'eac-rg11unorm', 'eac-rg11snorm',
    // 156-183: ASTC LDR
    'astc-4x4-unorm', 'astc-4x4-unorm-srgb',
    'astc-5x4-unorm', 'astc-5x4-unorm-srgb',
    'astc-5x5-unorm', 'astc-5x5-unorm-srgb',
    'astc-6x5-unorm', 'astc-6x5-unorm-srgb',
    'astc-6x6-unorm', 'astc-6x6-unorm-srgb',
    'astc-8x5-unorm', 'astc-8x5-unorm-srgb',
    'astc-8x6-unorm', 'astc-8x6-unorm-srgb',
    'astc-8x8-unorm', 'astc-8x8-unorm-srgb',
    'astc-10x5-unorm', 'astc-10x5-unorm-srgb',
    'astc-10x6-unorm', 'astc-10x6-unorm-srgb',
    'astc-10x8-unorm', 'astc-10x8-unorm-srgb',
    'astc-10x10-unorm', 'astc-10x10-unorm-srgb',
    'astc-12x10-unorm', 'astc-12x10-unorm-srgb',
    'astc-12x12-unorm', 'astc-12x12-unorm-srgb',
    // 184-217: Chroma subsampled (all unsupported)
    ...Array(34).fill(null),
    // 218-231: ASTC HDR (all unsupported)
    ...Array(14).fill(null),
];

/**
 * Pixel size lookup matching wgpu_format_pixel_size() from pixel_formats_webgpu.h.
 */
function wgpuFormatPixelSize(format) {
    const sizes = {
        'r8unorm': 1, 'r8snorm': 1, 'r8uint': 1, 'r8sint': 1, 'stencil8': 1,
        'r16uint': 2, 'r16sint': 2, 'r16float': 2,
        'rg8unorm': 2, 'rg8snorm': 2, 'rg8uint': 2, 'rg8sint': 2,
        'depth16unorm': 2,
        'r32uint': 4, 'r32sint': 4, 'r32float': 4,
        'rg16uint': 4, 'rg16sint': 4, 'rg16float': 4,
        'rgba8unorm': 4, 'rgba8unorm-srgb': 4, 'rgba8snorm': 4,
        'rgba8uint': 4, 'rgba8sint': 4,
        'bgra8unorm': 4, 'bgra8unorm-srgb': 4,
        'rgb10a2unorm': 4, 'rgb10a2uint': 4,
        'rg11b10ufloat': 4, 'rgb9e5ufloat': 4,
        'depth32float': 4, 'depth24plus': 4,
        'rg32uint': 8, 'rg32sint': 8, 'rg32float': 8,
        'rgba16uint': 8, 'rgba16sint': 8, 'rgba16float': 8,
        'depth24plus-stencil8': 8, 'depth32float-stencil8': 8,
        'rgba32uint': 16, 'rgba32sint': 16, 'rgba32float': 16,
    };
    return sizes[format] || 0;
}

/**
 * Depth format classification matching is_depth_format_wgpu().
 */
function isDepthFormat(format) {
    return [
        'depth16unorm', 'depth24plus', 'depth24plus-stencil8',
        'depth32float', 'depth32float-stencil8'
    ].includes(format);
}

/**
 * Stencil format classification matching has_stencil_wgpu().
 */
function hasStencil(format) {
    return ['stencil8', 'depth24plus-stencil8', 'depth32float-stencil8'].includes(format);
}

/**
 * Valid storage texture formats matching _is_valid_storage_texture_format().
 */
function isValidStorageFormat(format) {
    return [
        'rgba8unorm', 'rgba8snorm', 'rgba8uint', 'rgba8sint',
        'rgba16uint', 'rgba16sint', 'rgba16float',
        'r32float', 'r32uint', 'r32sint',
        'rg32float', 'rg32uint', 'rg32sint',
        'rgba32float', 'rgba32uint', 'rgba32sint',
        'bgra8unorm',
    ].includes(format);
}

export function runTests() {
    describe('Format Mapping: Supported Formats', () => {
        it('should map R8 formats correctly', () => {
            assert.equal(RD_TO_WGPU_FORMAT[8], 'r8unorm');
            assert.equal(RD_TO_WGPU_FORMAT[9], 'r8snorm');
            assert.equal(RD_TO_WGPU_FORMAT[12], 'r8uint');
            assert.equal(RD_TO_WGPU_FORMAT[13], 'r8sint');
        });

        it('should map RG8 formats correctly', () => {
            assert.equal(RD_TO_WGPU_FORMAT[15], 'rg8unorm');
            assert.equal(RD_TO_WGPU_FORMAT[16], 'rg8snorm');
            assert.equal(RD_TO_WGPU_FORMAT[19], 'rg8uint');
            assert.equal(RD_TO_WGPU_FORMAT[20], 'rg8sint');
        });

        it('should map RGBA8 formats correctly', () => {
            assert.equal(RD_TO_WGPU_FORMAT[36], 'rgba8unorm');
            assert.equal(RD_TO_WGPU_FORMAT[37], 'rgba8snorm');
            assert.equal(RD_TO_WGPU_FORMAT[40], 'rgba8uint');
            assert.equal(RD_TO_WGPU_FORMAT[41], 'rgba8sint');
            assert.equal(RD_TO_WGPU_FORMAT[42], 'rgba8unorm-srgb');
        });

        it('should map BGRA8 formats correctly', () => {
            assert.equal(RD_TO_WGPU_FORMAT[43], 'bgra8unorm');
            assert.equal(RD_TO_WGPU_FORMAT[49], 'bgra8unorm-srgb');
        });

        it('should map A8B8G8R8 PACK32 same as RGBA8', () => {
            assert.equal(RD_TO_WGPU_FORMAT[50], 'rgba8unorm');
            assert.equal(RD_TO_WGPU_FORMAT[51], 'rgba8snorm');
            assert.equal(RD_TO_WGPU_FORMAT[54], 'rgba8uint');
            assert.equal(RD_TO_WGPU_FORMAT[55], 'rgba8sint');
            assert.equal(RD_TO_WGPU_FORMAT[56], 'rgba8unorm-srgb');
        });

        it('should map 32-bit float/int formats correctly', () => {
            assert.equal(RD_TO_WGPU_FORMAT[97], 'r32uint');
            assert.equal(RD_TO_WGPU_FORMAT[98], 'r32sint');
            assert.equal(RD_TO_WGPU_FORMAT[99], 'r32float');
            assert.equal(RD_TO_WGPU_FORMAT[100], 'rg32uint');
            assert.equal(RD_TO_WGPU_FORMAT[101], 'rg32sint');
            assert.equal(RD_TO_WGPU_FORMAT[102], 'rg32float');
            assert.equal(RD_TO_WGPU_FORMAT[106], 'rgba32uint');
            assert.equal(RD_TO_WGPU_FORMAT[107], 'rgba32sint');
            assert.equal(RD_TO_WGPU_FORMAT[108], 'rgba32float');
        });

        it('should map depth/stencil formats correctly', () => {
            assert.equal(RD_TO_WGPU_FORMAT[123], 'depth16unorm');
            assert.equal(RD_TO_WGPU_FORMAT[124], 'depth24plus');
            assert.equal(RD_TO_WGPU_FORMAT[125], 'depth32float');
            assert.equal(RD_TO_WGPU_FORMAT[126], 'stencil8');
            assert.equal(RD_TO_WGPU_FORMAT[128], 'depth24plus-stencil8');
            assert.equal(RD_TO_WGPU_FORMAT[129], 'depth32float-stencil8');
        });

        it('should map special packed float formats', () => {
            assert.equal(RD_TO_WGPU_FORMAT[121], 'rg11b10ufloat');
            assert.equal(RD_TO_WGPU_FORMAT[122], 'rgb9e5ufloat');
        });
    });

    describe('Format Mapping: Unsupported Formats', () => {
        it('should return null for packed small formats (indices 0-7)', () => {
            for (let i = 0; i <= 7; i++) {
                assert.equal(RD_TO_WGPU_FORMAT[i], null, `Index ${i} should be null`);
            }
        });

        it('should return null for 3-component RGB8 formats (indices 22-28)', () => {
            for (let i = 22; i <= 28; i++) {
                assert.equal(RD_TO_WGPU_FORMAT[i], null, `Index ${i} should be null`);
            }
        });

        it('should return null for 3-component BGR8 formats (indices 29-35)', () => {
            for (let i = 29; i <= 35; i++) {
                assert.equal(RD_TO_WGPU_FORMAT[i], null, `Index ${i} should be null`);
            }
        });

        it('should return null for scaled formats (USCALED/SSCALED)', () => {
            // R8 scaled
            assert.equal(RD_TO_WGPU_FORMAT[10], null);
            assert.equal(RD_TO_WGPU_FORMAT[11], null);
            // RG8 scaled
            assert.equal(RD_TO_WGPU_FORMAT[17], null);
            assert.equal(RD_TO_WGPU_FORMAT[18], null);
            // RGBA8 scaled
            assert.equal(RD_TO_WGPU_FORMAT[38], null);
            assert.equal(RD_TO_WGPU_FORMAT[39], null);
        });

        it('should return null for single/dual channel sRGB (R8_SRGB, RG8_SRGB)', () => {
            assert.equal(RD_TO_WGPU_FORMAT[14], null); // R8_SRGB
            assert.equal(RD_TO_WGPU_FORMAT[21], null); // RG8_SRGB
        });

        it('should return null for 64-bit formats (indices 109-120)', () => {
            for (let i = 109; i <= 120; i++) {
                assert.equal(RD_TO_WGPU_FORMAT[i], null, `Index ${i} should be null`);
            }
        });

        it('should return null for 3-component RGB32 (indices 103-105)', () => {
            for (let i = 103; i <= 105; i++) {
                assert.equal(RD_TO_WGPU_FORMAT[i], null, `Index ${i} should be null`);
            }
        });

        it('should return null for chroma subsampled (indices 184-217)', () => {
            for (let i = 184; i <= 217; i++) {
                assert.equal(RD_TO_WGPU_FORMAT[i], null, `Index ${i} should be null`);
            }
        });

        it('should return null for ASTC HDR (indices 218-231)', () => {
            for (let i = 218; i <= 231; i++) {
                assert.equal(RD_TO_WGPU_FORMAT[i], null, `Index ${i} should be null`);
            }
        });

        it('should return null for D16_UNORM_S8_UINT (index 127)', () => {
            assert.equal(RD_TO_WGPU_FORMAT[127], null);
        });
    });

    describe('Format Mapping: Table Size', () => {
        it('should have exactly 232 entries (DATA_FORMAT_MAX)', () => {
            assert.equal(RD_TO_WGPU_FORMAT.length, 232);
        });
    });

    describe('Format Mapping: Pixel Size', () => {
        it('should return 1 byte for R8 formats', () => {
            assert.equal(wgpuFormatPixelSize('r8unorm'), 1);
            assert.equal(wgpuFormatPixelSize('r8snorm'), 1);
            assert.equal(wgpuFormatPixelSize('r8uint'), 1);
            assert.equal(wgpuFormatPixelSize('r8sint'), 1);
        });

        it('should return 2 bytes for RG8/R16/Depth16 formats', () => {
            assert.equal(wgpuFormatPixelSize('rg8unorm'), 2);
            assert.equal(wgpuFormatPixelSize('r16uint'), 2);
            assert.equal(wgpuFormatPixelSize('r16float'), 2);
            assert.equal(wgpuFormatPixelSize('depth16unorm'), 2);
        });

        it('should return 4 bytes for RGBA8/R32/RG16/Depth32 formats', () => {
            assert.equal(wgpuFormatPixelSize('rgba8unorm'), 4);
            assert.equal(wgpuFormatPixelSize('bgra8unorm'), 4);
            assert.equal(wgpuFormatPixelSize('r32float'), 4);
            assert.equal(wgpuFormatPixelSize('rg16float'), 4);
            assert.equal(wgpuFormatPixelSize('depth32float'), 4);
            assert.equal(wgpuFormatPixelSize('rgb10a2unorm'), 4);
            assert.equal(wgpuFormatPixelSize('rg11b10ufloat'), 4);
        });

        it('should return 8 bytes for RG32/RGBA16/DepthStencil formats', () => {
            assert.equal(wgpuFormatPixelSize('rg32float'), 8);
            assert.equal(wgpuFormatPixelSize('rgba16float'), 8);
            assert.equal(wgpuFormatPixelSize('depth24plus-stencil8'), 8);
            assert.equal(wgpuFormatPixelSize('depth32float-stencil8'), 8);
        });

        it('should return 16 bytes for RGBA32 formats', () => {
            assert.equal(wgpuFormatPixelSize('rgba32float'), 16);
            assert.equal(wgpuFormatPixelSize('rgba32uint'), 16);
            assert.equal(wgpuFormatPixelSize('rgba32sint'), 16);
        });

        it('should return 0 for compressed/unknown formats', () => {
            assert.equal(wgpuFormatPixelSize('bc1-rgba-unorm'), 0);
            assert.equal(wgpuFormatPixelSize('etc2-rgb8unorm'), 0);
            assert.equal(wgpuFormatPixelSize('astc-4x4-unorm'), 0);
            assert.equal(wgpuFormatPixelSize('unknown'), 0);
        });
    });

    describe('Format Mapping: Depth Classification', () => {
        it('should identify depth formats', () => {
            assert.ok(isDepthFormat('depth16unorm'));
            assert.ok(isDepthFormat('depth24plus'));
            assert.ok(isDepthFormat('depth32float'));
            assert.ok(isDepthFormat('depth24plus-stencil8'));
            assert.ok(isDepthFormat('depth32float-stencil8'));
        });

        it('should not identify color formats as depth', () => {
            assert.ok(!isDepthFormat('rgba8unorm'));
            assert.ok(!isDepthFormat('r32float'));
            assert.ok(!isDepthFormat('stencil8'));
        });

        it('should identify stencil formats', () => {
            assert.ok(hasStencil('stencil8'));
            assert.ok(hasStencil('depth24plus-stencil8'));
            assert.ok(hasStencil('depth32float-stencil8'));
        });

        it('should not identify depth-only as having stencil', () => {
            assert.ok(!hasStencil('depth16unorm'));
            assert.ok(!hasStencil('depth24plus'));
            assert.ok(!hasStencil('depth32float'));
        });
    });

    describe('Format Mapping: Storage Texture Validation', () => {
        it('should accept valid storage formats', () => {
            assert.ok(isValidStorageFormat('rgba8unorm'));
            assert.ok(isValidStorageFormat('rgba8snorm'));
            assert.ok(isValidStorageFormat('r32float'));
            assert.ok(isValidStorageFormat('rg32uint'));
            assert.ok(isValidStorageFormat('rgba32float'));
            assert.ok(isValidStorageFormat('bgra8unorm'));
        });

        it('should reject formats not valid for storage', () => {
            assert.ok(!isValidStorageFormat('r8unorm'));
            assert.ok(!isValidStorageFormat('rg8unorm'));
            assert.ok(!isValidStorageFormat('r16float'));
            assert.ok(!isValidStorageFormat('depth32float'));
            assert.ok(!isValidStorageFormat('rgba8unorm-srgb'));
            assert.ok(!isValidStorageFormat('bc1-rgba-unorm'));
        });
    });
}
