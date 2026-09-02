/**
 * Test: Uniform Buffer Packing (std140 Layout)
 *
 * Validates std140 layout rules as they apply to the WebGPU driver.
 * WebGPU uses WGSL alignment rules which are similar to (but not identical to)
 * GLSL std140. The push constant emulation uses var<storage, read> which
 * follows WGSL storage buffer layout rules (essentially std430 for scalars/vectors,
 * but the driver documentation references std140 for UBO compatibility).
 *
 * Key rules tested:
 * - Scalar alignment (4 bytes for float/int/uint)
 * - vec2 alignment (8 bytes)
 * - vec3/vec4 alignment (16 bytes)
 * - mat4 alignment (16 bytes per column, 64 bytes total)
 * - Array element stride (rounded up to 16 bytes for std140)
 * - Struct member offset computation
 * - Padding between members
 *
 * Reference: WGSL spec 13.4 (Memory Layout), GLSL std140 rules
 */

import { describe, it, assert } from './test_harness.mjs';

// Type sizes and alignments for std140 layout.
const Std140 = {
    SCALAR_SIZE: 4,
    SCALAR_ALIGN: 4,
    VEC2_SIZE: 8,
    VEC2_ALIGN: 8,
    VEC3_SIZE: 12,
    VEC3_ALIGN: 16, // vec3 aligned to 16 in std140
    VEC4_SIZE: 16,
    VEC4_ALIGN: 16,
    MAT4_SIZE: 64, // 4 columns of vec4
    MAT4_ALIGN: 16,
    MAT3_SIZE: 48, // 3 columns of vec4 (padded) in std140
    MAT3_ALIGN: 16,
};

// WGSL storage buffer layout (std430-like) for scalars/vectors.
const WgslStorage = {
    SCALAR_SIZE: 4,
    SCALAR_ALIGN: 4,
    VEC2_SIZE: 8,
    VEC2_ALIGN: 8,
    VEC3_SIZE: 12,
    VEC3_ALIGN: 16,
    VEC4_SIZE: 16,
    VEC4_ALIGN: 16,
    MAT4_SIZE: 64,
    MAT4_ALIGN: 16,
};

/**
 * Align an offset to the given alignment.
 */
function alignOffset(offset, alignment) {
    return Math.ceil(offset / alignment) * alignment;
}

/**
 * Compute std140 array element stride.
 * In std140, array elements are rounded up to vec4 alignment (16 bytes).
 */
function std140ArrayStride(elementSize, elementAlign) {
    // std140: array element stride must be multiple of vec4 alignment (16)
    const baseStride = alignOffset(elementSize, elementAlign);
    return alignOffset(baseStride, 16);
}

/**
 * Compute WGSL storage buffer array stride (std430-like).
 * Arrays use natural alignment without the vec4 rounding rule.
 */
function wgslStorageArrayStride(elementSize, elementAlign) {
    return alignOffset(elementSize, elementAlign);
}

/**
 * Layout a struct according to std140 rules.
 * members: Array of { type: 'float'|'vec2'|'vec3'|'vec4'|'mat4'|'int'|'uint', name: string }
 * Returns: Array of { name, offset, size, alignment }
 */
function layoutStd140(members) {
    const typeInfo = {
        'float': { size: 4, align: 4 },
        'int': { size: 4, align: 4 },
        'uint': { size: 4, align: 4 },
        'vec2': { size: 8, align: 8 },
        'vec3': { size: 12, align: 16 },
        'vec4': { size: 16, align: 16 },
        'mat3': { size: 48, align: 16 }, // 3x vec4 columns in std140
        'mat4': { size: 64, align: 16 }, // 4x vec4 columns
        'ivec2': { size: 8, align: 8 },
        'ivec3': { size: 12, align: 16 },
        'ivec4': { size: 16, align: 16 },
        'uvec4': { size: 16, align: 16 },
    };

    let currentOffset = 0;
    const layout = [];

    for (const member of members) {
        const info = typeInfo[member.type];
        if (!info) throw new Error(`Unknown type: ${member.type}`);

        // Align current offset to member's alignment requirement.
        currentOffset = alignOffset(currentOffset, info.align);

        layout.push({
            name: member.name,
            offset: currentOffset,
            size: info.size,
            alignment: info.align,
        });

        currentOffset += info.size;
    }

    // Total struct size: aligned up to largest member alignment.
    const maxAlign = Math.max(...layout.map(l => l.alignment));
    const totalSize = alignOffset(currentOffset, maxAlign);

    return { members: layout, totalSize, alignment: maxAlign };
}

export function runTests() {
    describe('std140 Packing: Scalar Types', () => {
        it('should pack consecutive floats at 4-byte intervals', () => {
            const result = layoutStd140([
                { type: 'float', name: 'a' },
                { type: 'float', name: 'b' },
                { type: 'float', name: 'c' },
                { type: 'float', name: 'd' },
            ]);
            assert.equal(result.members[0].offset, 0);
            assert.equal(result.members[1].offset, 4);
            assert.equal(result.members[2].offset, 8);
            assert.equal(result.members[3].offset, 12);
            assert.equal(result.totalSize, 16);
        });

        it('should pack int and uint same as float', () => {
            const result = layoutStd140([
                { type: 'int', name: 'a' },
                { type: 'uint', name: 'b' },
                { type: 'float', name: 'c' },
            ]);
            assert.equal(result.members[0].offset, 0);
            assert.equal(result.members[1].offset, 4);
            assert.equal(result.members[2].offset, 8);
        });
    });

    describe('std140 Packing: Vector Types', () => {
        it('should align vec2 to 8 bytes', () => {
            const result = layoutStd140([
                { type: 'float', name: 'a' },
                { type: 'vec2', name: 'b' },
            ]);
            assert.equal(result.members[0].offset, 0);
            assert.equal(result.members[1].offset, 8); // Padded from 4 to 8
        });

        it('should align vec3 to 16 bytes', () => {
            const result = layoutStd140([
                { type: 'float', name: 'a' },
                { type: 'vec3', name: 'b' },
            ]);
            assert.equal(result.members[0].offset, 0);
            assert.equal(result.members[1].offset, 16); // Padded from 4 to 16
        });

        it('should align vec4 to 16 bytes', () => {
            const result = layoutStd140([
                { type: 'float', name: 'a' },
                { type: 'vec4', name: 'b' },
            ]);
            assert.equal(result.members[0].offset, 0);
            assert.equal(result.members[1].offset, 16);
        });

        it('should pack vec2 after vec2 without padding', () => {
            const result = layoutStd140([
                { type: 'vec2', name: 'a' },
                { type: 'vec2', name: 'b' },
            ]);
            assert.equal(result.members[0].offset, 0);
            assert.equal(result.members[1].offset, 8);
            assert.equal(result.totalSize, 16);
        });

        it('should pack float after vec3 at offset 12 (fills vec3 trailing padding)', () => {
            // In std140, vec3 has alignment 16 but size 12.
            // A scalar after it goes at offset 12 IF it fits (align 4 at 12 is fine).
            // BUT std140 says vec3 actually takes a full 16 bytes (trailing pad).
            // This depends on implementation: GLSL std140 says vec3 consumes 16 bytes.
            // WGSL follows vec3 having size 12, but alignment 16.
            // For practical use, the next member after vec3 starts at offset 12+4=16
            // because vec3 occupies bytes 0-11 and the next alignment kicks in.
            // Actually: vec3 size is 12, next float aligns to 4, so offset 12 is valid.
            const result = layoutStd140([
                { type: 'vec3', name: 'a' },
                { type: 'float', name: 'b' },
            ]);
            assert.equal(result.members[0].offset, 0);
            assert.equal(result.members[1].offset, 12); // float alignment = 4, 12 is multiple of 4
        });
    });

    describe('std140 Packing: Matrix Types', () => {
        it('should align mat4 to 16 bytes and occupy 64 bytes', () => {
            const result = layoutStd140([
                { type: 'mat4', name: 'transform' },
            ]);
            assert.equal(result.members[0].offset, 0);
            assert.equal(result.members[0].size, 64);
            assert.equal(result.totalSize, 64);
        });

        it('should align mat4 after scalar to 16', () => {
            const result = layoutStd140([
                { type: 'float', name: 'scale' },
                { type: 'mat4', name: 'transform' },
            ]);
            assert.equal(result.members[0].offset, 0);
            assert.equal(result.members[1].offset, 16); // Padded 4 -> 16
        });

        it('should handle mat3 (3 columns x vec4 stride in std140)', () => {
            const result = layoutStd140([
                { type: 'mat3', name: 'normal_matrix' },
            ]);
            assert.equal(result.members[0].size, 48); // 3 * 16
            assert.equal(result.totalSize, 48);
        });
    });

    describe('std140 Packing: Common Godot Push Constant Structs', () => {
        it('should layout a typical scene push constant block', () => {
            // Common pattern: model matrix + misc params
            const result = layoutStd140([
                { type: 'mat4', name: 'model_matrix' },       // 0-63
                { type: 'vec4', name: 'color' },              // 64-79
                { type: 'float', name: 'roughness' },         // 80-83
                { type: 'float', name: 'metallic' },          // 84-87
                { type: 'float', name: 'emission_energy' },   // 88-91
                { type: 'uint', name: 'flags' },              // 92-95
            ]);
            assert.equal(result.members[0].offset, 0);
            assert.equal(result.members[1].offset, 64);
            assert.equal(result.members[2].offset, 80);
            assert.equal(result.members[3].offset, 84);
            assert.equal(result.members[4].offset, 88);
            assert.equal(result.members[5].offset, 92);
            assert.equal(result.totalSize, 96);
        });

        it('should layout canvas item push constants', () => {
            // Canvas items typically have:
            const result = layoutStd140([
                { type: 'vec4', name: 'src_rect' },           // 0-15
                { type: 'vec4', name: 'dst_rect' },           // 16-31
                { type: 'vec4', name: 'modulation' },         // 32-47
                { type: 'float', name: 'time' },              // 48-51
                { type: 'uint', name: 'flags' },              // 52-55
                { type: 'vec2', name: 'pixel_size' },         // 56-63
            ]);
            assert.equal(result.members[0].offset, 0);
            assert.equal(result.members[1].offset, 16);
            assert.equal(result.members[2].offset, 32);
            assert.equal(result.members[3].offset, 48);
            assert.equal(result.members[4].offset, 52);
            assert.equal(result.members[5].offset, 56); // vec2 aligns to 8, 56 is multiple of 8
            assert.equal(result.totalSize, 64);
        });

        it('should fit within MAX_PUSH_CONSTANT_SIZE (128 bytes)', () => {
            // Largest typical push constant should be <= 128 bytes
            const result = layoutStd140([
                { type: 'mat4', name: 'transform' },          // 64 bytes
                { type: 'vec4', name: 'params0' },            // 16 bytes
                { type: 'vec4', name: 'params1' },            // 16 bytes
                { type: 'vec4', name: 'params2' },            // 16 bytes
                { type: 'vec4', name: 'params3' },            // 16 bytes = 128 total
            ]);
            assert.lessThanOrEqual(result.totalSize, 128);
        });
    });

    describe('std140 Packing: Array Stride', () => {
        it('should round float array stride to 16 in std140', () => {
            // std140: array of float -> each element takes 16 bytes
            assert.equal(std140ArrayStride(4, 4), 16);
        });

        it('should round vec2 array stride to 16 in std140', () => {
            assert.equal(std140ArrayStride(8, 8), 16);
        });

        it('should keep vec4 array stride at 16', () => {
            assert.equal(std140ArrayStride(16, 16), 16);
        });

        it('should keep mat4 array stride at 64', () => {
            assert.equal(std140ArrayStride(64, 16), 64);
        });
    });

    describe('WGSL Storage Packing: Array Stride (std430-like)', () => {
        it('should use natural alignment for float array in storage', () => {
            // std430: float array stride = 4 (no vec4 rounding)
            assert.equal(wgslStorageArrayStride(4, 4), 4);
        });

        it('should use natural alignment for vec2 array in storage', () => {
            assert.equal(wgslStorageArrayStride(8, 8), 8);
        });

        it('should use 16-byte stride for vec3 array in storage', () => {
            // vec3 size=12, align=16 -> stride=16
            assert.equal(wgslStorageArrayStride(12, 16), 16);
        });

        it('should use 16-byte stride for vec4 array in storage', () => {
            assert.equal(wgslStorageArrayStride(16, 16), 16);
        });
    });

    describe('std140 Packing: Alignment Function', () => {
        it('should align 0 to any alignment as 0', () => {
            assert.equal(alignOffset(0, 4), 0);
            assert.equal(alignOffset(0, 8), 0);
            assert.equal(alignOffset(0, 16), 0);
        });

        it('should not change already-aligned values', () => {
            assert.equal(alignOffset(16, 16), 16);
            assert.equal(alignOffset(32, 8), 32);
            assert.equal(alignOffset(256, 4), 256);
        });

        it('should round up to next alignment boundary', () => {
            assert.equal(alignOffset(1, 4), 4);
            assert.equal(alignOffset(5, 8), 8);
            assert.equal(alignOffset(17, 16), 32);
            assert.equal(alignOffset(100, 16), 112);
        });
    });
}
