/**
 * Test: Texture Format Read/Write Conversions
 *
 * Validates the format promotion and readback/upload conversion logic used
 * when the GPU texture format differs from the engine's (Godot) format.
 * Key scenarios:
 *
 * - R8_UNORM promoted to R32_SFLOAT for storage (WebGPU lacks R8 storage)
 * - RG8 promoted to RG32Float for storage
 * - R16/RG16 always promoted to 32-bit equivalents
 * - Channel swapping for BGRA8 <-> RGBA8 readback
 * - Pixel size changes during format promotion/demotion
 * - Float32 <-> Float16 downgrade/upgrade conversions
 *
 * Reference: rendering_device_driver_webgpu.cpp
 *   _promote_storage_format(), texture_readback_convert(), texture_upload_convert()
 */

import { describe, it, assert } from './test_harness.mjs';

// Simulated WGPUTextureFormat enum values (string names for clarity).
const WGPUFormat = {
    R8Unorm: 'r8unorm',
    R8Snorm: 'r8snorm',
    R8Uint: 'r8uint',
    R8Sint: 'r8sint',
    RG8Unorm: 'rg8unorm',
    RG8Snorm: 'rg8snorm',
    RG8Uint: 'rg8uint',
    RG8Sint: 'rg8sint',
    R16Float: 'r16float',
    R16Uint: 'r16uint',
    R16Sint: 'r16sint',
    RG16Float: 'rg16float',
    RG16Uint: 'rg16uint',
    RG16Sint: 'rg16sint',
    R32Float: 'r32float',
    R32Uint: 'r32uint',
    R32Sint: 'r32sint',
    RG32Float: 'rg32float',
    RG32Uint: 'rg32uint',
    RG32Sint: 'rg32sint',
    RGBA8Unorm: 'rgba8unorm',
    BGRA8Unorm: 'bgra8unorm',
    RGBA16Float: 'rgba16float',
    RGBA32Float: 'rgba32float',
};

/**
 * Pixel size lookup matching wgpu_format_pixel_size().
 */
function pixelSize(format) {
    const sizes = {
        'r8unorm': 1, 'r8snorm': 1, 'r8uint': 1, 'r8sint': 1,
        'rg8unorm': 2, 'rg8snorm': 2, 'rg8uint': 2, 'rg8sint': 2,
        'r16float': 2, 'r16uint': 2, 'r16sint': 2,
        'rg16float': 4, 'rg16uint': 4, 'rg16sint': 4,
        'r32float': 4, 'r32uint': 4, 'r32sint': 4,
        'rg32float': 8, 'rg32uint': 8, 'rg32sint': 8,
        'rgba8unorm': 4, 'bgra8unorm': 4,
        'rgba16float': 8,
        'rgba32float': 16,
    };
    return sizes[format] || 0;
}

/**
 * Simulate _promote_storage_format() from the driver.
 * When has_texture_formats_tier1 is true, 8-bit formats are kept as-is.
 * When false, they are promoted to 32-bit equivalents.
 * 16-bit formats are always promoted regardless of tier1.
 */
function promoteStorageFormat(format, hasTier1 = false) {
    switch (format) {
        case WGPUFormat.R8Unorm:
        case WGPUFormat.R8Snorm:
            return hasTier1 ? format : WGPUFormat.R32Float;
        case WGPUFormat.R8Uint:
            return hasTier1 ? format : WGPUFormat.R32Uint;
        case WGPUFormat.R8Sint:
            return hasTier1 ? format : WGPUFormat.R32Sint;
        case WGPUFormat.RG8Unorm:
        case WGPUFormat.RG8Snorm:
            return hasTier1 ? format : WGPUFormat.RG32Float;
        case WGPUFormat.RG8Uint:
            return hasTier1 ? format : WGPUFormat.RG32Uint;
        case WGPUFormat.RG8Sint:
            return hasTier1 ? format : WGPUFormat.RG32Sint;
        // 16-bit formats: always promote regardless of tier1.
        case WGPUFormat.R16Float:
            return WGPUFormat.R32Float;
        case WGPUFormat.R16Uint:
            return WGPUFormat.R32Uint;
        case WGPUFormat.R16Sint:
            return WGPUFormat.R32Sint;
        case WGPUFormat.RG16Float:
            return WGPUFormat.RG32Float;
        case WGPUFormat.RG16Uint:
            return WGPUFormat.RG32Uint;
        case WGPUFormat.RG16Sint:
            return WGPUFormat.RG32Sint;
        default:
            return format;
    }
}

/**
 * Simulate readback conversion for R8 promoted to R32Float.
 * GPU has float32 [0.0, 1.0] -> engine expects uint8 [0, 255].
 */
function readbackR8FromR32Float(floatValues) {
    return floatValues.map(f => {
        f = Math.max(0, Math.min(1, f));
        return Math.round(f * 255);
    });
}

/**
 * Simulate upload conversion for R8 to R32Float.
 * Engine has uint8 [0, 255] -> GPU needs float32 [0.0, 1.0].
 */
function uploadR8ToR32Float(uint8Values) {
    return uint8Values.map(v => v / 255.0);
}

/**
 * Simulate readback conversion for R8Uint promoted to R32Uint.
 * GPU has uint32 -> engine expects uint8 (clamped to 255).
 */
function readbackR8FromR32Uint(uint32Values) {
    return uint32Values.map(v => Math.min(v, 255));
}

/**
 * Simulate readback conversion for R8Sint promoted to R32Sint.
 * GPU has int32 -> engine expects int8 (clamped to [0, 127]).
 */
function readbackR8FromR32Sint(int32Values) {
    return int32Values.map(v => Math.max(0, Math.min(127, v)));
}

/**
 * Swap BGRA <-> RGBA channels in a pixel array.
 * Each pixel is [c0, c1, c2, c3]. Swaps c0 and c2 (R <-> B).
 */
function swapBGRAtoRGBA(pixels) {
    return pixels.map(([b, g, r, a]) => [r, g, b, a]);
}

/**
 * Simulate Float16 encoding/decoding (simplified).
 * Uses JavaScript's DataView for actual float16 if available, otherwise approximation.
 */
function float32ToFloat16Approx(f32) {
    // Simplified: clamp and reduce precision.
    // Real implementation uses Math::make_half_float.
    const buf = new ArrayBuffer(4);
    const view = new DataView(buf);
    view.setFloat32(0, f32, true);
    const bits = view.getUint32(0, true);
    const sign = (bits >> 31) & 1;
    let exp = ((bits >> 23) & 0xFF) - 127 + 15;
    let mantissa = (bits >> 13) & 0x3FF;
    if (exp <= 0) { exp = 0; mantissa = 0; }
    if (exp >= 31) { exp = 31; mantissa = 0; }
    return (sign << 15) | (exp << 10) | mantissa;
}

export function runTests() {
    describe('Texture Conversion: Storage Format Promotion (no tier1)', () => {
        it('should promote R8Unorm to R32Float', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.R8Unorm), WGPUFormat.R32Float);
        });

        it('should promote R8Snorm to R32Float', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.R8Snorm), WGPUFormat.R32Float);
        });

        it('should promote R8Uint to R32Uint', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.R8Uint), WGPUFormat.R32Uint);
        });

        it('should promote R8Sint to R32Sint', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.R8Sint), WGPUFormat.R32Sint);
        });

        it('should promote RG8Unorm to RG32Float', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.RG8Unorm), WGPUFormat.RG32Float);
        });

        it('should promote RG8Snorm to RG32Float', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.RG8Snorm), WGPUFormat.RG32Float);
        });

        it('should promote RG8Uint to RG32Uint', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.RG8Uint), WGPUFormat.RG32Uint);
        });

        it('should promote RG8Sint to RG32Sint', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.RG8Sint), WGPUFormat.RG32Sint);
        });

        it('should not promote RGBA8Unorm (already valid storage format)', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.RGBA8Unorm), WGPUFormat.RGBA8Unorm);
        });

        it('should not promote BGRA8Unorm (already valid storage format)', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.BGRA8Unorm), WGPUFormat.BGRA8Unorm);
        });
    });

    describe('Texture Conversion: Storage Format Promotion (with tier1)', () => {
        it('should keep R8Unorm when tier1 is available', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.R8Unorm, true), WGPUFormat.R8Unorm);
        });

        it('should keep R8Uint when tier1 is available', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.R8Uint, true), WGPUFormat.R8Uint);
        });

        it('should keep RG8Unorm when tier1 is available', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.RG8Unorm, true), WGPUFormat.RG8Unorm);
        });

        it('should keep RG8Sint when tier1 is available', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.RG8Sint, true), WGPUFormat.RG8Sint);
        });
    });

    describe('Texture Conversion: 16-bit Always Promoted', () => {
        it('should always promote R16Float to R32Float regardless of tier1', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.R16Float, false), WGPUFormat.R32Float);
            assert.equal(promoteStorageFormat(WGPUFormat.R16Float, true), WGPUFormat.R32Float);
        });

        it('should always promote R16Uint to R32Uint', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.R16Uint, false), WGPUFormat.R32Uint);
            assert.equal(promoteStorageFormat(WGPUFormat.R16Uint, true), WGPUFormat.R32Uint);
        });

        it('should always promote RG16Float to RG32Float', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.RG16Float, false), WGPUFormat.RG32Float);
            assert.equal(promoteStorageFormat(WGPUFormat.RG16Float, true), WGPUFormat.RG32Float);
        });

        it('should always promote RG16Sint to RG32Sint', () => {
            assert.equal(promoteStorageFormat(WGPUFormat.RG16Sint, false), WGPUFormat.RG32Sint);
            assert.equal(promoteStorageFormat(WGPUFormat.RG16Sint, true), WGPUFormat.RG32Sint);
        });
    });

    describe('Texture Conversion: Pixel Size Changes', () => {
        it('should quadruple pixel size for R8->R32Float promotion', () => {
            const rdSize = pixelSize(WGPUFormat.R8Unorm);   // 1 byte
            const gpuSize = pixelSize(WGPUFormat.R32Float);  // 4 bytes
            assert.equal(rdSize, 1);
            assert.equal(gpuSize, 4);
            assert.equal(gpuSize / rdSize, 4);
        });

        it('should quadruple pixel size for RG8->RG32Float promotion', () => {
            const rdSize = pixelSize(WGPUFormat.RG8Unorm);   // 2 bytes
            const gpuSize = pixelSize(WGPUFormat.RG32Float);  // 8 bytes
            assert.equal(rdSize, 2);
            assert.equal(gpuSize, 8);
            assert.equal(gpuSize / rdSize, 4);
        });

        it('should double pixel size for R16Float->R32Float promotion', () => {
            const rdSize = pixelSize(WGPUFormat.R16Float);   // 2 bytes
            const gpuSize = pixelSize(WGPUFormat.R32Float);  // 4 bytes
            assert.equal(rdSize, 2);
            assert.equal(gpuSize, 4);
            assert.equal(gpuSize / rdSize, 2);
        });

        it('should not change pixel size for RGBA8 (no promotion needed)', () => {
            const size = pixelSize(WGPUFormat.RGBA8Unorm);
            assert.equal(size, 4);
            assert.equal(promoteStorageFormat(WGPUFormat.RGBA8Unorm), WGPUFormat.RGBA8Unorm);
        });

        it('should have equal pixel size for BGRA8 and RGBA8', () => {
            assert.equal(pixelSize(WGPUFormat.BGRA8Unorm), pixelSize(WGPUFormat.RGBA8Unorm));
        });
    });

    describe('Texture Conversion: Readback R8 from R32Float', () => {
        it('should convert 0.0 to 0', () => {
            assert.deepEqual(readbackR8FromR32Float([0.0]), [0]);
        });

        it('should convert 1.0 to 255', () => {
            assert.deepEqual(readbackR8FromR32Float([1.0]), [255]);
        });

        it('should convert 0.5 to 128', () => {
            assert.deepEqual(readbackR8FromR32Float([0.5]), [128]);
        });

        it('should clamp negative values to 0', () => {
            assert.deepEqual(readbackR8FromR32Float([-0.5]), [0]);
        });

        it('should clamp values above 1.0 to 255', () => {
            assert.deepEqual(readbackR8FromR32Float([1.5]), [255]);
        });

        it('should convert multiple values', () => {
            const result = readbackR8FromR32Float([0.0, 0.25, 0.5, 0.75, 1.0]);
            assert.equal(result[0], 0);
            assert.equal(result[1], 64);
            assert.equal(result[2], 128);
            assert.equal(result[3], 191);
            assert.equal(result[4], 255);
        });
    });

    describe('Texture Conversion: Upload R8 to R32Float', () => {
        it('should convert 0 to 0.0', () => {
            assert.closeTo(uploadR8ToR32Float([0])[0], 0.0, 0.001);
        });

        it('should convert 255 to 1.0', () => {
            assert.closeTo(uploadR8ToR32Float([255])[0], 1.0, 0.001);
        });

        it('should convert 128 to approximately 0.502', () => {
            assert.closeTo(uploadR8ToR32Float([128])[0], 128 / 255, 0.001);
        });

        it('should round-trip through upload then readback', () => {
            const original = [0, 64, 128, 192, 255];
            const floats = uploadR8ToR32Float(original);
            const recovered = readbackR8FromR32Float(floats);
            assert.deepEqual(recovered, original);
        });
    });

    describe('Texture Conversion: Readback R8 from R32Uint/R32Sint', () => {
        it('should pass through small uint32 values', () => {
            assert.deepEqual(readbackR8FromR32Uint([0, 100, 255]), [0, 100, 255]);
        });

        it('should clamp uint32 values above 255', () => {
            assert.deepEqual(readbackR8FromR32Uint([256, 1000, 65535]), [255, 255, 255]);
        });

        it('should clamp sint32 negative values to 0', () => {
            assert.deepEqual(readbackR8FromR32Sint([-1, -100]), [0, 0]);
        });

        it('should clamp sint32 values above 127', () => {
            assert.deepEqual(readbackR8FromR32Sint([128, 255, 1000]), [127, 127, 127]);
        });

        it('should pass through valid sint32 range [0, 127]', () => {
            assert.deepEqual(readbackR8FromR32Sint([0, 50, 127]), [0, 50, 127]);
        });
    });

    describe('Texture Conversion: BGRA <-> RGBA Channel Swapping', () => {
        it('should swap B and R channels', () => {
            const bgra = [[255, 128, 0, 255]]; // B=255, G=128, R=0, A=255
            const rgba = swapBGRAtoRGBA(bgra);
            assert.deepEqual(rgba, [[0, 128, 255, 255]]); // R=0, G=128, B=255, A=255
        });

        it('should preserve alpha channel', () => {
            const bgra = [[100, 200, 50, 128]];
            const rgba = swapBGRAtoRGBA(bgra);
            assert.equal(rgba[0][3], 128);
        });

        it('should preserve green channel', () => {
            const bgra = [[100, 200, 50, 128]];
            const rgba = swapBGRAtoRGBA(bgra);
            assert.equal(rgba[0][1], 200);
        });

        it('should be its own inverse (double swap returns original)', () => {
            const original = [[10, 20, 30, 40], [50, 60, 70, 80]];
            const swapped = swapBGRAtoRGBA(original);
            const restored = swapBGRAtoRGBA(swapped);
            assert.deepEqual(restored, original);
        });

        it('should handle all-zero pixel', () => {
            const result = swapBGRAtoRGBA([[0, 0, 0, 0]]);
            assert.deepEqual(result, [[0, 0, 0, 0]]);
        });

        it('should handle all-255 pixel', () => {
            const result = swapBGRAtoRGBA([[255, 255, 255, 255]]);
            assert.deepEqual(result, [[255, 255, 255, 255]]);
        });

        it('should swap multiple pixels independently', () => {
            const bgra = [
                [255, 0, 0, 255],   // pure blue in BGRA
                [0, 0, 255, 255],   // pure red in BGRA
            ];
            const rgba = swapBGRAtoRGBA(bgra);
            assert.deepEqual(rgba[0], [0, 0, 255, 255]);   // pure blue in RGBA
            assert.deepEqual(rgba[1], [255, 0, 0, 255]);   // pure red in RGBA
        });
    });
}
