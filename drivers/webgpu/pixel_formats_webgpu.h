/**************************************************************************/
/*  pixel_formats_webgpu.h                                                */
/**************************************************************************/
/*                       This file is part of:                            */
/*                           GODOT ENGINE                                 */
/*                      https://godotengine.org                           */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                 */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef PIXEL_FORMATS_WEBGPU_H
#define PIXEL_FORMATS_WEBGPU_H

#include "servers/rendering/rendering_device_commons.h"
#include <webgpu/webgpu.h>

// ============================================================================
// DataFormat → WGPUTextureFormat mapping table
// ============================================================================
//
// WebGPU supports ~107 of Godot's ~232 DataFormat values.
// Unsupported formats return WGPUTextureFormat_Undefined.
//
// Key unsupported categories:
//   - All 3-component formats (RGB8, RGB16, RGB32) → use RGBA equivalents
//   - All packed small formats (R4G4, R5G6B5, etc.)
//   - All scaled formats (USCALED, SSCALED)
//   - All 64-bit formats
//   - All YCbCr / chroma subsampled formats
//   - All ASTC HDR (SFLOAT) formats
//   - R8_SRGB, RG8_SRGB (no 1/2-channel sRGB in WebGPU)
//
// Optional feature-gated formats:
//   - BC1-BC7: require "texture-compression-bc" device feature
//   - ETC2/EAC: require "texture-compression-etc2" device feature
//   - ASTC LDR: require "texture-compression-astc" device feature
//   - R16/RG16/RGBA16 Unorm: require "unorm16-texture-formats" feature
//   - R16/RG16/RGBA16 Snorm: require "snorm16-texture-formats" feature
//
// Based on the Metal driver pattern (drivers/metal/pixel_formats.h).
// ============================================================================

static constexpr WGPUTextureFormat RD_TO_WGPU_FORMAT[] = {
	// 0: DATA_FORMAT_R4G4_UNORM_PACK8
	WGPUTextureFormat_Undefined,
	// 1: DATA_FORMAT_R4G4B4A4_UNORM_PACK16
	WGPUTextureFormat_Undefined,
	// 2: DATA_FORMAT_B4G4R4A4_UNORM_PACK16
	WGPUTextureFormat_Undefined,
	// 3: DATA_FORMAT_R5G6B5_UNORM_PACK16
	WGPUTextureFormat_Undefined,
	// 4: DATA_FORMAT_B5G6R5_UNORM_PACK16
	WGPUTextureFormat_Undefined,
	// 5: DATA_FORMAT_R5G5B5A1_UNORM_PACK16
	WGPUTextureFormat_Undefined,
	// 6: DATA_FORMAT_B5G5R5A1_UNORM_PACK16
	WGPUTextureFormat_Undefined,
	// 7: DATA_FORMAT_A1R5G5B5_UNORM_PACK16
	WGPUTextureFormat_Undefined,

	// --- R8 ---
	// 8: DATA_FORMAT_R8_UNORM
	WGPUTextureFormat_R8Unorm,
	// 9: DATA_FORMAT_R8_SNORM
	WGPUTextureFormat_R8Snorm,
	// 10: DATA_FORMAT_R8_USCALED
	WGPUTextureFormat_Undefined,
	// 11: DATA_FORMAT_R8_SSCALED
	WGPUTextureFormat_Undefined,
	// 12: DATA_FORMAT_R8_UINT
	WGPUTextureFormat_R8Uint,
	// 13: DATA_FORMAT_R8_SINT
	WGPUTextureFormat_R8Sint,
	// 14: DATA_FORMAT_R8_SRGB — No single-channel sRGB in WebGPU
	WGPUTextureFormat_Undefined,

	// --- RG8 ---
	// 15: DATA_FORMAT_R8G8_UNORM
	WGPUTextureFormat_RG8Unorm,
	// 16: DATA_FORMAT_R8G8_SNORM
	WGPUTextureFormat_RG8Snorm,
	// 17: DATA_FORMAT_R8G8_USCALED
	WGPUTextureFormat_Undefined,
	// 18: DATA_FORMAT_R8G8_SSCALED
	WGPUTextureFormat_Undefined,
	// 19: DATA_FORMAT_R8G8_UINT
	WGPUTextureFormat_RG8Uint,
	// 20: DATA_FORMAT_R8G8_SINT
	WGPUTextureFormat_RG8Sint,
	// 21: DATA_FORMAT_R8G8_SRGB — No 2-channel sRGB in WebGPU
	WGPUTextureFormat_Undefined,

	// --- RGB8 (3-component — NOT supported in WebGPU) ---
	// 22–28: DATA_FORMAT_R8G8B8_*
	WGPUTextureFormat_Undefined, // 22: UNORM
	WGPUTextureFormat_Undefined, // 23: SNORM
	WGPUTextureFormat_Undefined, // 24: USCALED
	WGPUTextureFormat_Undefined, // 25: SSCALED
	WGPUTextureFormat_Undefined, // 26: UINT
	WGPUTextureFormat_Undefined, // 27: SINT
	WGPUTextureFormat_Undefined, // 28: SRGB

	// --- BGR8 (3-component — NOT supported) ---
	// 29–35: DATA_FORMAT_B8G8R8_*
	WGPUTextureFormat_Undefined, // 29: UNORM
	WGPUTextureFormat_Undefined, // 30: SNORM
	WGPUTextureFormat_Undefined, // 31: USCALED
	WGPUTextureFormat_Undefined, // 32: SSCALED
	WGPUTextureFormat_Undefined, // 33: UINT
	WGPUTextureFormat_Undefined, // 34: SINT
	WGPUTextureFormat_Undefined, // 35: SRGB

	// --- RGBA8 ---
	// 36: DATA_FORMAT_R8G8B8A8_UNORM
	WGPUTextureFormat_RGBA8Unorm,
	// 37: DATA_FORMAT_R8G8B8A8_SNORM
	WGPUTextureFormat_RGBA8Snorm,
	// 38: DATA_FORMAT_R8G8B8A8_USCALED
	WGPUTextureFormat_Undefined,
	// 39: DATA_FORMAT_R8G8B8A8_SSCALED
	WGPUTextureFormat_Undefined,
	// 40: DATA_FORMAT_R8G8B8A8_UINT
	WGPUTextureFormat_RGBA8Uint,
	// 41: DATA_FORMAT_R8G8B8A8_SINT
	WGPUTextureFormat_RGBA8Sint,
	// 42: DATA_FORMAT_R8G8B8A8_SRGB
	WGPUTextureFormat_RGBA8UnormSrgb,

	// --- BGRA8 ---
	// 43: DATA_FORMAT_B8G8R8A8_UNORM
	WGPUTextureFormat_BGRA8Unorm,
	// 44: DATA_FORMAT_B8G8R8A8_SNORM — No BGRA8Snorm in WebGPU
	WGPUTextureFormat_Undefined,
	// 45: DATA_FORMAT_B8G8R8A8_USCALED
	WGPUTextureFormat_Undefined,
	// 46: DATA_FORMAT_B8G8R8A8_SSCALED
	WGPUTextureFormat_Undefined,
	// 47: DATA_FORMAT_B8G8R8A8_UINT — No BGRA8Uint in WebGPU
	WGPUTextureFormat_Undefined,
	// 48: DATA_FORMAT_B8G8R8A8_SINT — No BGRA8Sint in WebGPU
	WGPUTextureFormat_Undefined,
	// 49: DATA_FORMAT_B8G8R8A8_SRGB
	WGPUTextureFormat_BGRA8UnormSrgb,

	// --- A8B8G8R8 PACK32 (same memory layout as RGBA8) ---
	// 50: DATA_FORMAT_A8B8G8R8_UNORM_PACK32
	WGPUTextureFormat_RGBA8Unorm,
	// 51: DATA_FORMAT_A8B8G8R8_SNORM_PACK32
	WGPUTextureFormat_RGBA8Snorm,
	// 52: DATA_FORMAT_A8B8G8R8_USCALED_PACK32
	WGPUTextureFormat_Undefined,
	// 53: DATA_FORMAT_A8B8G8R8_SSCALED_PACK32
	WGPUTextureFormat_Undefined,
	// 54: DATA_FORMAT_A8B8G8R8_UINT_PACK32
	WGPUTextureFormat_RGBA8Uint,
	// 55: DATA_FORMAT_A8B8G8R8_SINT_PACK32
	WGPUTextureFormat_RGBA8Sint,
	// 56: DATA_FORMAT_A8B8G8R8_SRGB_PACK32
	WGPUTextureFormat_RGBA8UnormSrgb,

	// --- A2R10G10B10 PACK32 (swizzled — NOT supported) ---
	// 57–62: DATA_FORMAT_A2R10G10B10_*
	WGPUTextureFormat_Undefined, // 57: UNORM
	WGPUTextureFormat_Undefined, // 58: SNORM
	WGPUTextureFormat_Undefined, // 59: USCALED
	WGPUTextureFormat_Undefined, // 60: SSCALED
	WGPUTextureFormat_Undefined, // 61: UINT
	WGPUTextureFormat_Undefined, // 62: SINT

	// --- A2B10G10R10 PACK32 ---
	// 63: DATA_FORMAT_A2B10G10R10_UNORM_PACK32
	WGPUTextureFormat_RGB10A2Unorm,
	// 64: DATA_FORMAT_A2B10G10R10_SNORM_PACK32
	WGPUTextureFormat_Undefined,
	// 65: DATA_FORMAT_A2B10G10R10_USCALED_PACK32
	WGPUTextureFormat_Undefined,
	// 66: DATA_FORMAT_A2B10G10R10_SSCALED_PACK32
	WGPUTextureFormat_Undefined,
	// 67: DATA_FORMAT_A2B10G10R10_UINT_PACK32
	WGPUTextureFormat_RGB10A2Uint,
	// 68: DATA_FORMAT_A2B10G10R10_SINT_PACK32
	WGPUTextureFormat_Undefined,

	// --- R16 ---
	// 69: DATA_FORMAT_R16_UNORM (requires "unorm16-texture-formats" feature)
	WGPUTextureFormat_Undefined, // R16Unorm — not in base WebGPU spec
	// 70: DATA_FORMAT_R16_SNORM (requires "snorm16-texture-formats" feature)
	WGPUTextureFormat_Undefined, // R16Snorm — not in base WebGPU spec
	// 71: DATA_FORMAT_R16_USCALED
	WGPUTextureFormat_Undefined,
	// 72: DATA_FORMAT_R16_SSCALED
	WGPUTextureFormat_Undefined,
	// 73: DATA_FORMAT_R16_UINT
	WGPUTextureFormat_R16Uint,
	// 74: DATA_FORMAT_R16_SINT
	WGPUTextureFormat_R16Sint,
	// 75: DATA_FORMAT_R16_SFLOAT
	WGPUTextureFormat_R16Float,

	// --- RG16 ---
	// 76: DATA_FORMAT_R16G16_UNORM (requires "unorm16-texture-formats" feature)
	WGPUTextureFormat_Undefined, // RG16Unorm — not in base WebGPU spec
	// 77: DATA_FORMAT_R16G16_SNORM (requires "snorm16-texture-formats" feature)
	WGPUTextureFormat_Undefined, // RG16Snorm — not in base WebGPU spec
	// 78: DATA_FORMAT_R16G16_USCALED
	WGPUTextureFormat_Undefined,
	// 79: DATA_FORMAT_R16G16_SSCALED
	WGPUTextureFormat_Undefined,
	// 80: DATA_FORMAT_R16G16_UINT
	WGPUTextureFormat_RG16Uint,
	// 81: DATA_FORMAT_R16G16_SINT
	WGPUTextureFormat_RG16Sint,
	// 82: DATA_FORMAT_R16G16_SFLOAT
	WGPUTextureFormat_RG16Float,

	// --- RGB16 (3-component — NOT supported) ---
	// 83–89: DATA_FORMAT_R16G16B16_*
	WGPUTextureFormat_Undefined, // 83: UNORM
	WGPUTextureFormat_Undefined, // 84: SNORM
	WGPUTextureFormat_Undefined, // 85: USCALED
	WGPUTextureFormat_Undefined, // 86: SSCALED
	WGPUTextureFormat_Undefined, // 87: UINT
	WGPUTextureFormat_Undefined, // 88: SINT
	WGPUTextureFormat_Undefined, // 89: SFLOAT

	// --- RGBA16 ---
	// 90: DATA_FORMAT_R16G16B16A16_UNORM (requires "unorm16-texture-formats" feature)
	WGPUTextureFormat_Undefined, // RGBA16Unorm — not in base WebGPU spec
	// 91: DATA_FORMAT_R16G16B16A16_SNORM (requires "snorm16-texture-formats" feature)
	WGPUTextureFormat_Undefined, // RGBA16Snorm — not in base WebGPU spec
	// 92: DATA_FORMAT_R16G16B16A16_USCALED
	WGPUTextureFormat_Undefined,
	// 93: DATA_FORMAT_R16G16B16A16_SSCALED
	WGPUTextureFormat_Undefined,
	// 94: DATA_FORMAT_R16G16B16A16_UINT
	WGPUTextureFormat_RGBA16Uint,
	// 95: DATA_FORMAT_R16G16B16A16_SINT
	WGPUTextureFormat_RGBA16Sint,
	// 96: DATA_FORMAT_R16G16B16A16_SFLOAT
	WGPUTextureFormat_RGBA16Float,

	// --- R32 ---
	// 97: DATA_FORMAT_R32_UINT
	WGPUTextureFormat_R32Uint,
	// 98: DATA_FORMAT_R32_SINT
	WGPUTextureFormat_R32Sint,
	// 99: DATA_FORMAT_R32_SFLOAT
	WGPUTextureFormat_R32Float,

	// --- RG32 ---
	// 100: DATA_FORMAT_R32G32_UINT
	WGPUTextureFormat_RG32Uint,
	// 101: DATA_FORMAT_R32G32_SINT
	WGPUTextureFormat_RG32Sint,
	// 102: DATA_FORMAT_R32G32_SFLOAT
	WGPUTextureFormat_RG32Float,

	// --- RGB32 (3-component — NOT supported) ---
	// 103–105: DATA_FORMAT_R32G32B32_*
	WGPUTextureFormat_Undefined, // 103: UINT
	WGPUTextureFormat_Undefined, // 104: SINT
	WGPUTextureFormat_Undefined, // 105: SFLOAT

	// --- RGBA32 ---
	// 106: DATA_FORMAT_R32G32B32A32_UINT
	WGPUTextureFormat_RGBA32Uint,
	// 107: DATA_FORMAT_R32G32B32A32_SINT
	WGPUTextureFormat_RGBA32Sint,
	// 108: DATA_FORMAT_R32G32B32A32_SFLOAT
	WGPUTextureFormat_RGBA32Float,

	// --- R64 (NOT supported in WebGPU) ---
	// 109–111: DATA_FORMAT_R64_*
	WGPUTextureFormat_Undefined, // 109: UINT
	WGPUTextureFormat_Undefined, // 110: SINT
	WGPUTextureFormat_Undefined, // 111: SFLOAT

	// --- RG64 (NOT supported) ---
	// 112–114: DATA_FORMAT_R64G64_*
	WGPUTextureFormat_Undefined, // 112: UINT
	WGPUTextureFormat_Undefined, // 113: SINT
	WGPUTextureFormat_Undefined, // 114: SFLOAT

	// --- RGB64 (NOT supported) ---
	// 115–117: DATA_FORMAT_R64G64B64_*
	WGPUTextureFormat_Undefined, // 115: UINT
	WGPUTextureFormat_Undefined, // 116: SINT
	WGPUTextureFormat_Undefined, // 117: SFLOAT

	// --- RGBA64 (NOT supported) ---
	// 118–120: DATA_FORMAT_R64G64B64A64_*
	WGPUTextureFormat_Undefined, // 118: UINT
	WGPUTextureFormat_Undefined, // 119: SINT
	WGPUTextureFormat_Undefined, // 120: SFLOAT

	// --- Special packed floats ---
	// 121: DATA_FORMAT_B10G11R11_UFLOAT_PACK32
	WGPUTextureFormat_RG11B10Ufloat,
	// 122: DATA_FORMAT_E5B9G9R9_UFLOAT_PACK32
	WGPUTextureFormat_RGB9E5Ufloat,

	// --- Depth / Stencil ---
	// 123: DATA_FORMAT_D16_UNORM
	WGPUTextureFormat_Depth16Unorm,
	// 124: DATA_FORMAT_X8_D24_UNORM_PACK32
	WGPUTextureFormat_Depth24Plus,
	// 125: DATA_FORMAT_D32_SFLOAT
	WGPUTextureFormat_Depth32Float,
	// 126: DATA_FORMAT_S8_UINT
	WGPUTextureFormat_Stencil8,
	// 127: DATA_FORMAT_D16_UNORM_S8_UINT — NOT supported in WebGPU
	WGPUTextureFormat_Undefined,
	// 128: DATA_FORMAT_D24_UNORM_S8_UINT
	WGPUTextureFormat_Depth24PlusStencil8,
	// 129: DATA_FORMAT_D32_SFLOAT_S8_UINT
	WGPUTextureFormat_Depth32FloatStencil8,

	// --- BC compressed (require "texture-compression-bc" feature) ---
	// 130: DATA_FORMAT_BC1_RGB_UNORM_BLOCK
	WGPUTextureFormat_BC1RGBAUnorm,
	// 131: DATA_FORMAT_BC1_RGB_SRGB_BLOCK
	WGPUTextureFormat_BC1RGBAUnormSrgb,
	// 132: DATA_FORMAT_BC1_RGBA_UNORM_BLOCK
	WGPUTextureFormat_BC1RGBAUnorm,
	// 133: DATA_FORMAT_BC1_RGBA_SRGB_BLOCK
	WGPUTextureFormat_BC1RGBAUnormSrgb,
	// 134: DATA_FORMAT_BC2_UNORM_BLOCK
	WGPUTextureFormat_BC2RGBAUnorm,
	// 135: DATA_FORMAT_BC2_SRGB_BLOCK
	WGPUTextureFormat_BC2RGBAUnormSrgb,
	// 136: DATA_FORMAT_BC3_UNORM_BLOCK
	WGPUTextureFormat_BC3RGBAUnorm,
	// 137: DATA_FORMAT_BC3_SRGB_BLOCK
	WGPUTextureFormat_BC3RGBAUnormSrgb,
	// 138: DATA_FORMAT_BC4_UNORM_BLOCK
	WGPUTextureFormat_BC4RUnorm,
	// 139: DATA_FORMAT_BC4_SNORM_BLOCK
	WGPUTextureFormat_BC4RSnorm,
	// 140: DATA_FORMAT_BC5_UNORM_BLOCK
	WGPUTextureFormat_BC5RGUnorm,
	// 141: DATA_FORMAT_BC5_SNORM_BLOCK
	WGPUTextureFormat_BC5RGSnorm,
	// 142: DATA_FORMAT_BC6H_UFLOAT_BLOCK
	WGPUTextureFormat_BC6HRGBUfloat,
	// 143: DATA_FORMAT_BC6H_SFLOAT_BLOCK
	WGPUTextureFormat_BC6HRGBFloat,
	// 144: DATA_FORMAT_BC7_UNORM_BLOCK
	WGPUTextureFormat_BC7RGBAUnorm,
	// 145: DATA_FORMAT_BC7_SRGB_BLOCK
	WGPUTextureFormat_BC7RGBAUnormSrgb,

	// --- ETC2 / EAC (require "texture-compression-etc2" feature) ---
	// 146: DATA_FORMAT_ETC2_R8G8B8_UNORM_BLOCK
	WGPUTextureFormat_ETC2RGB8Unorm,
	// 147: DATA_FORMAT_ETC2_R8G8B8_SRGB_BLOCK
	WGPUTextureFormat_ETC2RGB8UnormSrgb,
	// 148: DATA_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK
	WGPUTextureFormat_ETC2RGB8A1Unorm,
	// 149: DATA_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK
	WGPUTextureFormat_ETC2RGB8A1UnormSrgb,
	// 150: DATA_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK
	WGPUTextureFormat_ETC2RGBA8Unorm,
	// 151: DATA_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK
	WGPUTextureFormat_ETC2RGBA8UnormSrgb,
	// 152: DATA_FORMAT_EAC_R11_UNORM_BLOCK
	WGPUTextureFormat_EACR11Unorm,
	// 153: DATA_FORMAT_EAC_R11_SNORM_BLOCK
	WGPUTextureFormat_EACR11Snorm,
	// 154: DATA_FORMAT_EAC_R11G11_UNORM_BLOCK
	WGPUTextureFormat_EACRG11Unorm,
	// 155: DATA_FORMAT_EAC_R11G11_SNORM_BLOCK
	WGPUTextureFormat_EACRG11Snorm,

	// --- ASTC LDR (require "texture-compression-astc" feature) ---
	// 156: DATA_FORMAT_ASTC_4x4_UNORM_BLOCK
	WGPUTextureFormat_ASTC4x4Unorm,
	// 157: DATA_FORMAT_ASTC_4x4_SRGB_BLOCK
	WGPUTextureFormat_ASTC4x4UnormSrgb,
	// 158: DATA_FORMAT_ASTC_5x4_UNORM_BLOCK
	WGPUTextureFormat_ASTC5x4Unorm,
	// 159: DATA_FORMAT_ASTC_5x4_SRGB_BLOCK
	WGPUTextureFormat_ASTC5x4UnormSrgb,
	// 160: DATA_FORMAT_ASTC_5x5_UNORM_BLOCK
	WGPUTextureFormat_ASTC5x5Unorm,
	// 161: DATA_FORMAT_ASTC_5x5_SRGB_BLOCK
	WGPUTextureFormat_ASTC5x5UnormSrgb,
	// 162: DATA_FORMAT_ASTC_6x5_UNORM_BLOCK
	WGPUTextureFormat_ASTC6x5Unorm,
	// 163: DATA_FORMAT_ASTC_6x5_SRGB_BLOCK
	WGPUTextureFormat_ASTC6x5UnormSrgb,
	// 164: DATA_FORMAT_ASTC_6x6_UNORM_BLOCK
	WGPUTextureFormat_ASTC6x6Unorm,
	// 165: DATA_FORMAT_ASTC_6x6_SRGB_BLOCK
	WGPUTextureFormat_ASTC6x6UnormSrgb,
	// 166: DATA_FORMAT_ASTC_8x5_UNORM_BLOCK
	WGPUTextureFormat_ASTC8x5Unorm,
	// 167: DATA_FORMAT_ASTC_8x5_SRGB_BLOCK
	WGPUTextureFormat_ASTC8x5UnormSrgb,
	// 168: DATA_FORMAT_ASTC_8x6_UNORM_BLOCK
	WGPUTextureFormat_ASTC8x6Unorm,
	// 169: DATA_FORMAT_ASTC_8x6_SRGB_BLOCK
	WGPUTextureFormat_ASTC8x6UnormSrgb,
	// 170: DATA_FORMAT_ASTC_8x8_UNORM_BLOCK
	WGPUTextureFormat_ASTC8x8Unorm,
	// 171: DATA_FORMAT_ASTC_8x8_SRGB_BLOCK
	WGPUTextureFormat_ASTC8x8UnormSrgb,
	// 172: DATA_FORMAT_ASTC_10x5_UNORM_BLOCK
	WGPUTextureFormat_ASTC10x5Unorm,
	// 173: DATA_FORMAT_ASTC_10x5_SRGB_BLOCK
	WGPUTextureFormat_ASTC10x5UnormSrgb,
	// 174: DATA_FORMAT_ASTC_10x6_UNORM_BLOCK
	WGPUTextureFormat_ASTC10x6Unorm,
	// 175: DATA_FORMAT_ASTC_10x6_SRGB_BLOCK
	WGPUTextureFormat_ASTC10x6UnormSrgb,
	// 176: DATA_FORMAT_ASTC_10x8_UNORM_BLOCK
	WGPUTextureFormat_ASTC10x8Unorm,
	// 177: DATA_FORMAT_ASTC_10x8_SRGB_BLOCK
	WGPUTextureFormat_ASTC10x8UnormSrgb,
	// 178: DATA_FORMAT_ASTC_10x10_UNORM_BLOCK
	WGPUTextureFormat_ASTC10x10Unorm,
	// 179: DATA_FORMAT_ASTC_10x10_SRGB_BLOCK
	WGPUTextureFormat_ASTC10x10UnormSrgb,
	// 180: DATA_FORMAT_ASTC_12x10_UNORM_BLOCK
	WGPUTextureFormat_ASTC12x10Unorm,
	// 181: DATA_FORMAT_ASTC_12x10_SRGB_BLOCK
	WGPUTextureFormat_ASTC12x10UnormSrgb,
	// 182: DATA_FORMAT_ASTC_12x12_UNORM_BLOCK
	WGPUTextureFormat_ASTC12x12Unorm,
	// 183: DATA_FORMAT_ASTC_12x12_SRGB_BLOCK
	WGPUTextureFormat_ASTC12x12UnormSrgb,

	// --- Chroma subsampled / YCbCr (184–217: all Undefined) ---
	WGPUTextureFormat_Undefined, // 184: G8B8G8R8_422_UNORM
	WGPUTextureFormat_Undefined, // 185: B8G8R8G8_422_UNORM
	WGPUTextureFormat_Undefined, // 186: G8_B8_R8_3PLANE_420_UNORM
	WGPUTextureFormat_Undefined, // 187: G8_B8R8_2PLANE_420_UNORM
	WGPUTextureFormat_Undefined, // 188: G8_B8_R8_3PLANE_422_UNORM
	WGPUTextureFormat_Undefined, // 189: G8_B8R8_2PLANE_422_UNORM
	WGPUTextureFormat_Undefined, // 190: G8_B8_R8_3PLANE_444_UNORM
	WGPUTextureFormat_Undefined, // 191: R10X6_UNORM_PACK16
	WGPUTextureFormat_Undefined, // 192: R10X6G10X6_UNORM_2PACK16
	WGPUTextureFormat_Undefined, // 193: R10X6G10X6B10X6A10X6_UNORM_4PACK16
	WGPUTextureFormat_Undefined, // 194: G10X6B10X6G10X6R10X6_422_UNORM_4PACK16
	WGPUTextureFormat_Undefined, // 195: B10X6G10X6R10X6G10X6_422_UNORM_4PACK16
	WGPUTextureFormat_Undefined, // 196: G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16
	WGPUTextureFormat_Undefined, // 197: G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16
	WGPUTextureFormat_Undefined, // 198: G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16
	WGPUTextureFormat_Undefined, // 199: G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16
	WGPUTextureFormat_Undefined, // 200: G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16
	WGPUTextureFormat_Undefined, // 201: R12X4_UNORM_PACK16
	WGPUTextureFormat_Undefined, // 202: R12X4G12X4_UNORM_2PACK16
	WGPUTextureFormat_Undefined, // 203: R12X4G12X4B12X4A12X4_UNORM_4PACK16
	WGPUTextureFormat_Undefined, // 204: G12X4B12X4G12X4R12X4_422_UNORM_4PACK16
	WGPUTextureFormat_Undefined, // 205: B12X4G12X4R12X4G12X4_422_UNORM_4PACK16
	WGPUTextureFormat_Undefined, // 206: G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16
	WGPUTextureFormat_Undefined, // 207: G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16
	WGPUTextureFormat_Undefined, // 208: G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16
	WGPUTextureFormat_Undefined, // 209: G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16
	WGPUTextureFormat_Undefined, // 210: G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16
	WGPUTextureFormat_Undefined, // 211: G16B16G16R16_422_UNORM
	WGPUTextureFormat_Undefined, // 212: B16G16R16G16_422_UNORM
	WGPUTextureFormat_Undefined, // 213: G16_B16_R16_3PLANE_420_UNORM
	WGPUTextureFormat_Undefined, // 214: G16_B16R16_2PLANE_420_UNORM
	WGPUTextureFormat_Undefined, // 215: G16_B16_R16_3PLANE_422_UNORM
	WGPUTextureFormat_Undefined, // 216: G16_B16R16_2PLANE_422_UNORM
	WGPUTextureFormat_Undefined, // 217: G16_B16_R16_3PLANE_444_UNORM

	// --- ASTC HDR / SFLOAT (218–231: NOT supported in WebGPU) ---
	WGPUTextureFormat_Undefined, // 218: ASTC_4x4_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 219: ASTC_5x4_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 220: ASTC_5x5_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 221: ASTC_6x5_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 222: ASTC_6x6_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 223: ASTC_8x5_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 224: ASTC_8x6_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 225: ASTC_8x8_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 226: ASTC_10x5_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 227: ASTC_10x6_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 228: ASTC_10x8_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 229: ASTC_10x10_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 230: ASTC_12x10_SFLOAT_BLOCK
	WGPUTextureFormat_Undefined, // 231: ASTC_12x12_SFLOAT_BLOCK
};

// Compile-time validation: table must have exactly DATA_FORMAT_MAX entries.
static_assert(
		sizeof(RD_TO_WGPU_FORMAT) / sizeof(RD_TO_WGPU_FORMAT[0]) == RenderingDeviceCommons::DATA_FORMAT_MAX,
		"RD_TO_WGPU_FORMAT table size must match DATA_FORMAT_MAX");

// ============================================================================
// Convenience lookup function
// ============================================================================

static inline WGPUTextureFormat data_format_to_wgpu(RenderingDeviceCommons::DataFormat p_format) {
	if (p_format < 0 || p_format >= RenderingDeviceCommons::DATA_FORMAT_MAX) {
		return WGPUTextureFormat_Undefined;
	}
	return RD_TO_WGPU_FORMAT[p_format];
}

// ============================================================================
// DataFormat → WGPUVertexFormat mapping (for vertex buffers)
// ============================================================================
//
// WebGPU vertex formats are more limited than texture formats but support
// some formats that textures don't (e.g., Float32x3 for RGB32F vertices).
//
// This mapping is used in vertex_format_create() to build WGPUVertexAttribute
// descriptors from Godot VertexAttribute descriptions.
// ============================================================================

static inline WGPUVertexFormat data_format_to_wgpu_vertex(RenderingDeviceCommons::DataFormat p_format) {
	using DF = RenderingDeviceCommons::DataFormat;
	switch (p_format) {
		// 8-bit
		case DF::DATA_FORMAT_R8_UINT: return WGPUVertexFormat_Uint8;
		case DF::DATA_FORMAT_R8_SINT: return WGPUVertexFormat_Sint8;
		case DF::DATA_FORMAT_R8G8_UINT: return WGPUVertexFormat_Uint8x2;
		case DF::DATA_FORMAT_R8G8_SINT: return WGPUVertexFormat_Sint8x2;
		case DF::DATA_FORMAT_R8G8B8A8_UINT: return WGPUVertexFormat_Uint8x4;
		case DF::DATA_FORMAT_R8G8B8A8_SINT: return WGPUVertexFormat_Sint8x4;
		case DF::DATA_FORMAT_R8G8_UNORM: return WGPUVertexFormat_Unorm8x2;
		case DF::DATA_FORMAT_R8G8_SNORM: return WGPUVertexFormat_Snorm8x2;
		case DF::DATA_FORMAT_R8G8B8A8_UNORM: return WGPUVertexFormat_Unorm8x4;
		case DF::DATA_FORMAT_R8G8B8A8_SNORM: return WGPUVertexFormat_Snorm8x4;

		// 16-bit
		case DF::DATA_FORMAT_R16_UINT: return WGPUVertexFormat_Uint16;
		case DF::DATA_FORMAT_R16_SINT: return WGPUVertexFormat_Sint16;
		case DF::DATA_FORMAT_R16G16_UINT: return WGPUVertexFormat_Uint16x2;
		case DF::DATA_FORMAT_R16G16_SINT: return WGPUVertexFormat_Sint16x2;
		case DF::DATA_FORMAT_R16G16B16A16_UINT: return WGPUVertexFormat_Uint16x4;
		case DF::DATA_FORMAT_R16G16B16A16_SINT: return WGPUVertexFormat_Sint16x4;
		case DF::DATA_FORMAT_R16G16_UNORM: return WGPUVertexFormat_Unorm16x2;
		case DF::DATA_FORMAT_R16G16_SNORM: return WGPUVertexFormat_Snorm16x2;
		case DF::DATA_FORMAT_R16G16B16A16_UNORM: return WGPUVertexFormat_Unorm16x4;
		case DF::DATA_FORMAT_R16G16B16A16_SNORM: return WGPUVertexFormat_Snorm16x4;
		case DF::DATA_FORMAT_R16_SFLOAT: return WGPUVertexFormat_Float16;
		case DF::DATA_FORMAT_R16G16_SFLOAT: return WGPUVertexFormat_Float16x2;
		case DF::DATA_FORMAT_R16G16B16A16_SFLOAT: return WGPUVertexFormat_Float16x4;

		// 32-bit float
		case DF::DATA_FORMAT_R32_SFLOAT: return WGPUVertexFormat_Float32;
		case DF::DATA_FORMAT_R32G32_SFLOAT: return WGPUVertexFormat_Float32x2;
		case DF::DATA_FORMAT_R32G32B32_SFLOAT: return WGPUVertexFormat_Float32x3; // 3-comp OK for vertex!
		case DF::DATA_FORMAT_R32G32B32A32_SFLOAT: return WGPUVertexFormat_Float32x4;

		// 32-bit integer
		case DF::DATA_FORMAT_R32_UINT: return WGPUVertexFormat_Uint32;
		case DF::DATA_FORMAT_R32G32_UINT: return WGPUVertexFormat_Uint32x2;
		case DF::DATA_FORMAT_R32G32B32_UINT: return WGPUVertexFormat_Uint32x3; // 3-comp OK for vertex!
		case DF::DATA_FORMAT_R32G32B32A32_UINT: return WGPUVertexFormat_Uint32x4;
		case DF::DATA_FORMAT_R32_SINT: return WGPUVertexFormat_Sint32;
		case DF::DATA_FORMAT_R32G32_SINT: return WGPUVertexFormat_Sint32x2;
		case DF::DATA_FORMAT_R32G32B32_SINT: return WGPUVertexFormat_Sint32x3; // 3-comp OK for vertex!
		case DF::DATA_FORMAT_R32G32B32A32_SINT: return WGPUVertexFormat_Sint32x4;

		// 10-10-10-2 packed (A2B10G10R10)
		case DF::DATA_FORMAT_A2B10G10R10_UNORM_PACK32: return WGPUVertexFormat_Unorm10_10_10_2;

		default:
			return (WGPUVertexFormat)0; // WGPUVertexFormat_Undefined was removed in Dawn
	}
}

// ============================================================================
// Depth format helper: is this a depth or depth-stencil format?
// ============================================================================

static inline bool is_depth_format_wgpu(WGPUTextureFormat p_format) {
	switch (p_format) {
		case WGPUTextureFormat_Depth16Unorm:
		case WGPUTextureFormat_Depth24Plus:
		case WGPUTextureFormat_Depth24PlusStencil8:
		case WGPUTextureFormat_Depth32Float:
		case WGPUTextureFormat_Depth32FloatStencil8:
			return true;
		default:
			return false;
	}
}

static inline bool has_stencil_wgpu(WGPUTextureFormat p_format) {
	switch (p_format) {
		case WGPUTextureFormat_Stencil8:
		case WGPUTextureFormat_Depth24PlusStencil8:
		case WGPUTextureFormat_Depth32FloatStencil8:
			return true;
		default:
			return false;
	}
}

// ============================================================================
// Texture aspect mapping
// ============================================================================

static inline WGPUTextureAspect texture_aspect_to_wgpu(
		RenderingDeviceDriver::TextureAspect p_aspect) {
	using TA = RenderingDeviceDriver::TextureAspect;
	switch (p_aspect) {
		case TA::TEXTURE_ASPECT_COLOR:
			return WGPUTextureAspect_All;
		case TA::TEXTURE_ASPECT_DEPTH:
			return WGPUTextureAspect_DepthOnly;
		case TA::TEXTURE_ASPECT_STENCIL:
			return WGPUTextureAspect_StencilOnly;
		default:
			return WGPUTextureAspect_All;
	}
}

// ============================================================================
// Bytes per pixel for supported formats (used for buffer<->texture copies)
// ============================================================================

static inline uint32_t wgpu_format_pixel_size(WGPUTextureFormat p_format) {
	switch (p_format) {
		// 1 byte
		case WGPUTextureFormat_R8Unorm:
		case WGPUTextureFormat_R8Snorm:
		case WGPUTextureFormat_R8Uint:
		case WGPUTextureFormat_R8Sint:
		case WGPUTextureFormat_Stencil8:
			return 1;

		// 2 bytes
		case WGPUTextureFormat_R16Uint:
		case WGPUTextureFormat_R16Sint:
		case WGPUTextureFormat_R16Float:
		// R16Unorm/R16Snorm not in base WebGPU spec (emdawnwebgpu 4.0.10)
		case WGPUTextureFormat_RG8Unorm:
		case WGPUTextureFormat_RG8Snorm:
		case WGPUTextureFormat_RG8Uint:
		case WGPUTextureFormat_RG8Sint:
		case WGPUTextureFormat_Depth16Unorm:
			return 2;

		// 4 bytes
		case WGPUTextureFormat_R32Uint:
		case WGPUTextureFormat_R32Sint:
		case WGPUTextureFormat_R32Float:
		case WGPUTextureFormat_RG16Uint:
		case WGPUTextureFormat_RG16Sint:
		case WGPUTextureFormat_RG16Float:
		// RG16Unorm/RG16Snorm not in base WebGPU spec (emdawnwebgpu 4.0.10)
		case WGPUTextureFormat_RGBA8Unorm:
		case WGPUTextureFormat_RGBA8UnormSrgb:
		case WGPUTextureFormat_RGBA8Snorm:
		case WGPUTextureFormat_RGBA8Uint:
		case WGPUTextureFormat_RGBA8Sint:
		case WGPUTextureFormat_BGRA8Unorm:
		case WGPUTextureFormat_BGRA8UnormSrgb:
		case WGPUTextureFormat_RGB10A2Unorm:
		case WGPUTextureFormat_RGB10A2Uint:
		case WGPUTextureFormat_RG11B10Ufloat:
		case WGPUTextureFormat_RGB9E5Ufloat:
		case WGPUTextureFormat_Depth32Float:
		case WGPUTextureFormat_Depth24Plus:
			return 4;

		// 8 bytes
		case WGPUTextureFormat_RG32Uint:
		case WGPUTextureFormat_RG32Sint:
		case WGPUTextureFormat_RG32Float:
		case WGPUTextureFormat_RGBA16Uint:
		case WGPUTextureFormat_RGBA16Sint:
		case WGPUTextureFormat_RGBA16Float:
		// RGBA16Unorm/RGBA16Snorm not in base WebGPU spec (emdawnwebgpu 4.0.10)
		case WGPUTextureFormat_Depth24PlusStencil8:
		case WGPUTextureFormat_Depth32FloatStencil8:
			return 8;

		// 16 bytes
		case WGPUTextureFormat_RGBA32Uint:
		case WGPUTextureFormat_RGBA32Sint:
		case WGPUTextureFormat_RGBA32Float:
			return 16;

		default:
			return 0; // Compressed or unknown — use block size calculations instead
	}
}

#endif // PIXEL_FORMATS_WEBGPU_H
