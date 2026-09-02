/**
 * Test: Bind Group Layout Compatibility
 *
 * Validates the logic for determining whether two bind group layouts are
 * compatible for rebinding purposes. In WebGPU, a bind group created with
 * one layout can only be used with a pipeline whose layout has a "compatible"
 * BGL at that index. Two BGLs are compatible if and only if they have the
 * same set of entries with identical binding numbers, types, visibility flags,
 * and dynamic offset settings.
 *
 * Reference: rendering_device_driver_webgpu.cpp _get_compatible_bind_group()
 */

import { describe, it, assert } from './test_harness.mjs';

// Shader stage flags (matching the driver).
const ShaderStage = {
    VERTEX: 1,
    FRAGMENT: 2,
    COMPUTE: 4,
};

// Buffer binding types.
const BufferBindingType = {
    Uniform: 'uniform',
    Storage: 'storage',
    ReadOnlyStorage: 'read-only-storage',
};

// Sampler binding types.
const SamplerBindingType = {
    Filtering: 'filtering',
    NonFiltering: 'non-filtering',
    Comparison: 'comparison',
};

// Texture sample types.
const TextureSampleType = {
    Float: 'float',
    Depth: 'depth',
};

/**
 * Create a bind group layout entry descriptor.
 */
function bufferEntry(binding, visibility, type, hasDynamicOffset = false, minBindingSize = 0) {
    return {
        binding,
        visibility,
        buffer: { type, hasDynamicOffset, minBindingSize },
    };
}

function samplerEntry(binding, visibility, type = SamplerBindingType.Filtering) {
    return {
        binding,
        visibility,
        sampler: { type },
    };
}

function textureEntry(binding, visibility, sampleType = TextureSampleType.Float, viewDimension = '2d', multisampled = false) {
    return {
        binding,
        visibility,
        texture: { sampleType, viewDimension, multisampled },
    };
}

function storageTextureEntry(binding, visibility, format = 'rgba8unorm', access = 'write-only') {
    return {
        binding,
        visibility,
        storageTexture: { access, format, viewDimension: '2d' },
    };
}

/**
 * Determine if two bind group layout descriptors are compatible.
 *
 * Per the WebGPU spec, two bind group layouts are "group-equivalent" if they
 * have the same number of entries and each entry matches on:
 *   - binding number
 *   - visibility
 *   - resource type and its sub-properties (buffer type, dynamic offset,
 *     sampler type, texture sample type, storage texture format, etc.)
 *
 * This is the key check that determines whether _get_compatible_bind_group()
 * can reuse an existing bind group or must create a new one.
 */
function areLayoutsCompatible(layoutA, layoutB) {
    if (layoutA.length !== layoutB.length) {
        return false;
    }

    // Sort by binding for stable comparison.
    const sortedA = [...layoutA].sort((a, b) => a.binding - b.binding);
    const sortedB = [...layoutB].sort((a, b) => a.binding - b.binding);

    for (let i = 0; i < sortedA.length; i++) {
        const a = sortedA[i];
        const b = sortedB[i];

        // Binding number must match.
        if (a.binding !== b.binding) return false;

        // Visibility must match.
        if (a.visibility !== b.visibility) return false;

        // Resource type must match.
        if (a.buffer && b.buffer) {
            if (a.buffer.type !== b.buffer.type) return false;
            if (a.buffer.hasDynamicOffset !== b.buffer.hasDynamicOffset) return false;
            // minBindingSize of 0 means "no minimum" and is compatible with any size.
            if (a.buffer.minBindingSize !== 0 && b.buffer.minBindingSize !== 0) {
                if (a.buffer.minBindingSize !== b.buffer.minBindingSize) return false;
            }
        } else if (a.sampler && b.sampler) {
            if (a.sampler.type !== b.sampler.type) return false;
        } else if (a.texture && b.texture) {
            if (a.texture.sampleType !== b.texture.sampleType) return false;
            if (a.texture.viewDimension !== b.texture.viewDimension) return false;
            if (a.texture.multisampled !== b.texture.multisampled) return false;
        } else if (a.storageTexture && b.storageTexture) {
            if (a.storageTexture.access !== b.storageTexture.access) return false;
            if (a.storageTexture.format !== b.storageTexture.format) return false;
            if (a.storageTexture.viewDimension !== b.storageTexture.viewDimension) return false;
        } else {
            // Different resource categories (e.g., buffer vs sampler).
            return false;
        }
    }

    return true;
}

export function runTests() {
    describe('Bind Group Compat: Identical Layouts', () => {
        it('should treat two identical single-buffer layouts as compatible', () => {
            const layoutA = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform)];
            const layoutB = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform)];
            assert.ok(areLayoutsCompatible(layoutA, layoutB));
        });

        it('should treat two identical multi-entry layouts as compatible', () => {
            const layoutA = [
                bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform),
                samplerEntry(1, ShaderStage.FRAGMENT),
                textureEntry(2, ShaderStage.FRAGMENT),
            ];
            const layoutB = [
                bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform),
                samplerEntry(1, ShaderStage.FRAGMENT),
                textureEntry(2, ShaderStage.FRAGMENT),
            ];
            assert.ok(areLayoutsCompatible(layoutA, layoutB));
        });

        it('should treat identical storage buffer layouts as compatible', () => {
            const layoutA = [bufferEntry(0, ShaderStage.COMPUTE, BufferBindingType.Storage)];
            const layoutB = [bufferEntry(0, ShaderStage.COMPUTE, BufferBindingType.Storage)];
            assert.ok(areLayoutsCompatible(layoutA, layoutB));
        });

        it('should treat identical storage texture layouts as compatible', () => {
            const layoutA = [storageTextureEntry(0, ShaderStage.COMPUTE, 'rgba8unorm', 'write-only')];
            const layoutB = [storageTextureEntry(0, ShaderStage.COMPUTE, 'rgba8unorm', 'write-only')];
            assert.ok(areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be compatible regardless of entry order (sorted internally)', () => {
            const layoutA = [
                textureEntry(2, ShaderStage.FRAGMENT),
                bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform),
            ];
            const layoutB = [
                bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform),
                textureEntry(2, ShaderStage.FRAGMENT),
            ];
            assert.ok(areLayoutsCompatible(layoutA, layoutB));
        });
    });

    describe('Bind Group Compat: Visibility Differences', () => {
        it('should be incompatible when visibility differs (VERTEX vs FRAGMENT)', () => {
            const layoutA = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform)];
            const layoutB = [bufferEntry(0, ShaderStage.FRAGMENT, BufferBindingType.Uniform)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when visibility differs (VERTEX vs VERTEX|FRAGMENT)', () => {
            const layoutA = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform)];
            const layoutB = [bufferEntry(0, ShaderStage.VERTEX | ShaderStage.FRAGMENT, BufferBindingType.Uniform)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when sampler visibility differs', () => {
            const layoutA = [samplerEntry(0, ShaderStage.FRAGMENT)];
            const layoutB = [samplerEntry(0, ShaderStage.VERTEX | ShaderStage.FRAGMENT)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when one entry has different visibility in multi-entry layout', () => {
            const layoutA = [
                bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform),
                textureEntry(1, ShaderStage.FRAGMENT),
            ];
            const layoutB = [
                bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform),
                textureEntry(1, ShaderStage.VERTEX),  // Changed
            ];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });
    });

    describe('Bind Group Compat: Different Binding Counts', () => {
        it('should be incompatible when one layout has more entries', () => {
            const layoutA = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform)];
            const layoutB = [
                bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform),
                bufferEntry(1, ShaderStage.VERTEX, BufferBindingType.Uniform),
            ];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when one layout is empty and other is not', () => {
            const layoutA = [];
            const layoutB = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible with different counts of mixed entries', () => {
            const layoutA = [
                bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform),
                samplerEntry(1, ShaderStage.FRAGMENT),
            ];
            const layoutB = [
                bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform),
                samplerEntry(1, ShaderStage.FRAGMENT),
                textureEntry(2, ShaderStage.FRAGMENT),
            ];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });
    });

    describe('Bind Group Compat: Empty Layouts', () => {
        it('should treat two empty layouts as compatible', () => {
            assert.ok(areLayoutsCompatible([], []));
        });
    });

    describe('Bind Group Compat: Dynamic Offset Differences', () => {
        it('should be incompatible when dynamic offset differs', () => {
            const layoutA = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform, false)];
            const layoutB = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform, true)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be compatible when both have dynamic offset', () => {
            const layoutA = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform, true)];
            const layoutB = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform, true)];
            assert.ok(areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible for storage buffer with vs without dynamic offset', () => {
            const layoutA = [bufferEntry(0, ShaderStage.COMPUTE, BufferBindingType.Storage, false)];
            const layoutB = [bufferEntry(0, ShaderStage.COMPUTE, BufferBindingType.Storage, true)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });
    });

    describe('Bind Group Compat: Buffer Type Differences', () => {
        it('should be incompatible when buffer types differ (uniform vs storage)', () => {
            const layoutA = [bufferEntry(0, ShaderStage.COMPUTE, BufferBindingType.Uniform)];
            const layoutB = [bufferEntry(0, ShaderStage.COMPUTE, BufferBindingType.Storage)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when buffer types differ (storage vs read-only-storage)', () => {
            const layoutA = [bufferEntry(0, ShaderStage.COMPUTE, BufferBindingType.Storage)];
            const layoutB = [bufferEntry(0, ShaderStage.COMPUTE, BufferBindingType.ReadOnlyStorage)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });
    });

    describe('Bind Group Compat: Resource Type Mismatches', () => {
        it('should be incompatible when one has buffer and other has sampler', () => {
            const layoutA = [bufferEntry(0, ShaderStage.FRAGMENT, BufferBindingType.Uniform)];
            const layoutB = [samplerEntry(0, ShaderStage.FRAGMENT)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when one has texture and other has storage texture', () => {
            const layoutA = [textureEntry(0, ShaderStage.FRAGMENT)];
            const layoutB = [storageTextureEntry(0, ShaderStage.FRAGMENT)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when sampler types differ (filtering vs comparison)', () => {
            const layoutA = [samplerEntry(0, ShaderStage.FRAGMENT, SamplerBindingType.Filtering)];
            const layoutB = [samplerEntry(0, ShaderStage.FRAGMENT, SamplerBindingType.Comparison)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when texture sample types differ (float vs depth)', () => {
            const layoutA = [textureEntry(0, ShaderStage.FRAGMENT, TextureSampleType.Float)];
            const layoutB = [textureEntry(0, ShaderStage.FRAGMENT, TextureSampleType.Depth)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when texture view dimensions differ', () => {
            const layoutA = [textureEntry(0, ShaderStage.FRAGMENT, TextureSampleType.Float, '2d')];
            const layoutB = [textureEntry(0, ShaderStage.FRAGMENT, TextureSampleType.Float, 'cube')];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when storage texture formats differ', () => {
            const layoutA = [storageTextureEntry(0, ShaderStage.COMPUTE, 'rgba8unorm')];
            const layoutB = [storageTextureEntry(0, ShaderStage.COMPUTE, 'rgba16float')];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when storage texture access modes differ', () => {
            const layoutA = [storageTextureEntry(0, ShaderStage.COMPUTE, 'rgba8unorm', 'write-only')];
            const layoutB = [storageTextureEntry(0, ShaderStage.COMPUTE, 'rgba8unorm', 'read-only')];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });
    });

    describe('Bind Group Compat: Binding Number Differences', () => {
        it('should be incompatible when binding numbers differ', () => {
            const layoutA = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform)];
            const layoutB = [bufferEntry(1, ShaderStage.VERTEX, BufferBindingType.Uniform)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible with swapped binding numbers', () => {
            const layoutA = [
                bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform),
                samplerEntry(1, ShaderStage.FRAGMENT),
            ];
            const layoutB = [
                bufferEntry(1, ShaderStage.VERTEX, BufferBindingType.Uniform),
                samplerEntry(0, ShaderStage.FRAGMENT),
            ];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });
    });

    describe('Bind Group Compat: MinBindingSize Handling', () => {
        it('should be compatible when one minBindingSize is 0 (wildcard)', () => {
            const layoutA = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform, false, 0)];
            const layoutB = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform, false, 64)];
            assert.ok(areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be compatible when both minBindingSize are 0', () => {
            const layoutA = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform, false, 0)];
            const layoutB = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform, false, 0)];
            assert.ok(areLayoutsCompatible(layoutA, layoutB));
        });

        it('should be incompatible when both non-zero minBindingSize differ', () => {
            const layoutA = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform, false, 64)];
            const layoutB = [bufferEntry(0, ShaderStage.VERTEX, BufferBindingType.Uniform, false, 128)];
            assert.ok(!areLayoutsCompatible(layoutA, layoutB));
        });
    });
}
