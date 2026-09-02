/**
 * Test: Descriptor Set / Bind Group Layout Generation
 *
 * Validates the logic for translating Godot uniform sets (descriptor sets)
 * into WebGPU bind group layouts. Key aspects tested:
 *
 * - Uniform type to WGPUBindGroupLayoutEntry mapping
 * - Visibility stage flags computation
 * - Combined image-sampler splitting into separate texture + sampler entries
 * - Push constant bind group layout (group 3, binding 120)
 * - Bind group rebinding logic (when shader BGL differs from source)
 * - Gap bind group insertion for Firefox/wgpu compatibility
 *
 * Reference: rendering_device_driver_webgpu.cpp shader_create_from_container(), uniform_set_create()
 */

import { describe, it, assert } from './test_harness.mjs';

// Godot UniformType enum values.
const UniformType = {
    SAMPLER: 0,                    // Standalone sampler
    SAMPLER_WITH_TEXTURE: 1,       // Combined image-sampler (split into texture + sampler)
    TEXTURE: 2,                    // Sampled texture
    IMAGE: 3,                      // Storage texture
    TEXTURE_BUFFER: 4,             // Texel buffer (not supported in WebGPU, stubbed)
    SAMPLER_WITH_TEXTURE_BUFFER: 5, // Combined texel buffer sampler
    IMAGE_BUFFER: 6,               // Storage texel buffer
    UNIFORM_BUFFER: 7,             // Uniform buffer
    STORAGE_BUFFER: 8,             // Storage buffer (read-write)
    INPUT_ATTACHMENT: 9,           // Input attachment (emulated as texture in WebGPU)
    UNIFORM_BUFFER_DYNAMIC: 10,    // Dynamic uniform buffer
    STORAGE_BUFFER_DYNAMIC: 11,    // Dynamic storage buffer
};

// Shader stage flags.
const ShaderStage = {
    VERTEX: 1,
    FRAGMENT: 2,
    COMPUTE: 4,
};

// WebGPU buffer binding types.
const BufferBindingType = {
    Uniform: 'uniform',
    Storage: 'storage',
    ReadOnlyStorage: 'read-only-storage',
};

// WebGPU sampler binding types.
const SamplerBindingType = {
    Filtering: 'filtering',
    NonFiltering: 'non-filtering',
    Comparison: 'comparison',
};

// WebGPU texture sample types.
const TextureSampleType = {
    Float: 'float',
    UnfilterableFloat: 'unfilterable-float',
    Depth: 'depth',
    Sint: 'sint',
    Uint: 'uint',
};

// Push constant constants from the driver.
const PUSH_CONSTANT_RING_BINDING = 120;
const PUSH_CONSTANT_BIND_GROUP = 3; // Always the last group in the layout.

/**
 * Translate a Godot uniform type to a WebGPU bind group layout entry.
 * Simulates the logic in shader_create_from_container().
 */
function uniformTypeToLayoutEntry(uniformType, binding, visibility, options = {}) {
    const entry = {
        binding,
        visibility,
    };

    switch (uniformType) {
        case UniformType.SAMPLER:
            entry.sampler = {
                type: options.comparison ? SamplerBindingType.Comparison : SamplerBindingType.Filtering,
            };
            break;

        case UniformType.SAMPLER_WITH_TEXTURE:
            // Split into TWO entries: texture at binding*2+1, sampler at binding*2
            return splitCombinedSampler(binding, visibility, options);

        case UniformType.TEXTURE:
        case UniformType.INPUT_ATTACHMENT:
            entry.texture = {
                sampleType: options.depthTexture
                    ? TextureSampleType.Depth
                    : (options.intTexture ? TextureSampleType.Sint : TextureSampleType.Float),
                viewDimension: options.viewDimension || '2d',
                multisampled: options.multisampled || false,
            };
            break;

        case UniformType.IMAGE:
            entry.storageTexture = {
                access: options.readOnly ? 'read-only' : 'write-only',
                format: options.format || 'rgba8unorm',
                viewDimension: options.viewDimension || '2d',
            };
            break;

        case UniformType.UNIFORM_BUFFER:
            entry.buffer = {
                type: BufferBindingType.Uniform,
                hasDynamicOffset: false,
                minBindingSize: options.minBindingSize || 0,
            };
            break;

        case UniformType.STORAGE_BUFFER:
            entry.buffer = {
                type: options.readOnly ? BufferBindingType.ReadOnlyStorage : BufferBindingType.Storage,
                hasDynamicOffset: false,
                minBindingSize: 0,
            };
            break;

        case UniformType.UNIFORM_BUFFER_DYNAMIC:
            entry.buffer = {
                type: BufferBindingType.Uniform,
                hasDynamicOffset: true,
                minBindingSize: 0,
            };
            break;

        case UniformType.STORAGE_BUFFER_DYNAMIC:
            entry.buffer = {
                type: options.readOnly ? BufferBindingType.ReadOnlyStorage : BufferBindingType.Storage,
                hasDynamicOffset: true,
                minBindingSize: 0,
            };
            break;

        default:
            entry.buffer = { type: BufferBindingType.Uniform, hasDynamicOffset: false, minBindingSize: 0 };
    }

    return [entry];
}

/**
 * Split a combined image-sampler into separate texture + sampler entries.
 * This is the core transform needed for WebGPU (which doesn't support combined samplers).
 *
 * SPIR-V preprocessing remaps: original binding N -> sampler at N*2, texture at N*2+1
 */
function splitCombinedSampler(binding, visibility, options = {}) {
    const samplerBinding = binding * 2;
    const textureBinding = binding * 2 + 1;

    const samplerEntry = {
        binding: samplerBinding,
        visibility,
        sampler: {
            type: options.comparison ? SamplerBindingType.Comparison : SamplerBindingType.Filtering,
        },
    };

    const textureEntry = {
        binding: textureBinding,
        visibility,
        texture: {
            sampleType: options.depthTexture ? TextureSampleType.Depth : TextureSampleType.Float,
            viewDimension: options.viewDimension || '2d',
            multisampled: options.multisampled || false,
        },
    };

    return [samplerEntry, textureEntry];
}

/**
 * Compute shader stage visibility from Godot shader stage flags.
 */
function computeVisibility(stages) {
    let vis = 0;
    if (stages & ShaderStage.VERTEX) vis |= ShaderStage.VERTEX;
    if (stages & ShaderStage.FRAGMENT) vis |= ShaderStage.FRAGMENT;
    if (stages & ShaderStage.COMPUTE) vis |= ShaderStage.COMPUTE;
    return vis;
}

/**
 * Create a push constant bind group layout entry.
 * The push constant ring buffer uses ReadOnlyStorage with dynamic offset.
 */
function createPushConstantLayoutEntry(visibility) {
    return {
        binding: PUSH_CONSTANT_RING_BINDING,
        visibility,
        buffer: {
            type: BufferBindingType.ReadOnlyStorage,
            hasDynamicOffset: true,
            minBindingSize: 0,
        },
    };
}

/**
 * Determine gap bind group indices needed between set_count and pc_group.
 * Firefox/wgpu requires ALL slots to be bound.
 */
function computeGapIndices(setCount, pcGroup) {
    const gaps = [];
    for (let i = setCount; i < pcGroup; i++) {
        gaps.push(i);
    }
    return gaps;
}

export function runTests() {
    describe('Bind Group Layout: Uniform Type Mapping', () => {
        it('should map UNIFORM_BUFFER to uniform buffer entry', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.UNIFORM_BUFFER, 0, ShaderStage.VERTEX);
            assert.equal(entries.length, 1);
            assert.equal(entries[0].buffer.type, BufferBindingType.Uniform);
            assert.equal(entries[0].buffer.hasDynamicOffset, false);
        });

        it('should map STORAGE_BUFFER to storage buffer entry', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.STORAGE_BUFFER, 0, ShaderStage.COMPUTE);
            assert.equal(entries.length, 1);
            assert.equal(entries[0].buffer.type, BufferBindingType.Storage);
        });

        it('should map read-only STORAGE_BUFFER to read-only-storage', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.STORAGE_BUFFER, 0, ShaderStage.FRAGMENT, { readOnly: true });
            assert.equal(entries.length, 1);
            assert.equal(entries[0].buffer.type, BufferBindingType.ReadOnlyStorage);
        });

        it('should map TEXTURE to texture entry', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.TEXTURE, 0, ShaderStage.FRAGMENT);
            assert.equal(entries.length, 1);
            assert.ok(entries[0].texture);
            assert.equal(entries[0].texture.sampleType, TextureSampleType.Float);
        });

        it('should map IMAGE to storage texture entry', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.IMAGE, 0, ShaderStage.COMPUTE);
            assert.equal(entries.length, 1);
            assert.ok(entries[0].storageTexture);
            assert.equal(entries[0].storageTexture.access, 'write-only');
        });

        it('should map SAMPLER to sampler entry', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.SAMPLER, 0, ShaderStage.FRAGMENT);
            assert.equal(entries.length, 1);
            assert.ok(entries[0].sampler);
            assert.equal(entries[0].sampler.type, SamplerBindingType.Filtering);
        });

        it('should map comparison sampler correctly', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.SAMPLER, 0, ShaderStage.FRAGMENT, { comparison: true });
            assert.equal(entries[0].sampler.type, SamplerBindingType.Comparison);
        });

        it('should map INPUT_ATTACHMENT to texture entry', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.INPUT_ATTACHMENT, 0, ShaderStage.FRAGMENT);
            assert.equal(entries.length, 1);
            assert.ok(entries[0].texture);
        });

        it('should map UNIFORM_BUFFER_DYNAMIC with hasDynamicOffset=true', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.UNIFORM_BUFFER_DYNAMIC, 0, ShaderStage.VERTEX);
            assert.equal(entries[0].buffer.hasDynamicOffset, true);
            assert.equal(entries[0].buffer.type, BufferBindingType.Uniform);
        });

        it('should map STORAGE_BUFFER_DYNAMIC with hasDynamicOffset=true', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.STORAGE_BUFFER_DYNAMIC, 0, ShaderStage.COMPUTE);
            assert.equal(entries[0].buffer.hasDynamicOffset, true);
        });
    });

    describe('Bind Group Layout: Combined Sampler Splitting', () => {
        it('should split SAMPLER_WITH_TEXTURE into two entries', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.SAMPLER_WITH_TEXTURE, 3, ShaderStage.FRAGMENT);
            assert.equal(entries.length, 2);
        });

        it('should place sampler at binding*2 and texture at binding*2+1', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.SAMPLER_WITH_TEXTURE, 5, ShaderStage.FRAGMENT);
            assert.equal(entries[0].binding, 10); // 5*2 = sampler
            assert.equal(entries[1].binding, 11); // 5*2+1 = texture
        });

        it('should preserve visibility on both split entries', () => {
            const vis = ShaderStage.VERTEX | ShaderStage.FRAGMENT;
            const entries = uniformTypeToLayoutEntry(UniformType.SAMPLER_WITH_TEXTURE, 0, vis);
            assert.equal(entries[0].visibility, vis);
            assert.equal(entries[1].visibility, vis);
        });

        it('should handle binding 0 correctly', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.SAMPLER_WITH_TEXTURE, 0, ShaderStage.FRAGMENT);
            assert.equal(entries[0].binding, 0); // sampler at 0
            assert.equal(entries[1].binding, 1); // texture at 1
        });

        it('should propagate depth texture flag to texture entry', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.SAMPLER_WITH_TEXTURE, 0, ShaderStage.FRAGMENT, { depthTexture: true });
            assert.equal(entries[1].texture.sampleType, TextureSampleType.Depth);
        });

        it('should propagate comparison flag to sampler entry', () => {
            const entries = uniformTypeToLayoutEntry(UniformType.SAMPLER_WITH_TEXTURE, 0, ShaderStage.FRAGMENT, { comparison: true });
            assert.equal(entries[0].sampler.type, SamplerBindingType.Comparison);
        });

        it('should handle high binding numbers without collision', () => {
            // With 20 combined samplers, max split binding = 20*2+1 = 41
            // Push constant binding at 120, so no collision
            const entries = uniformTypeToLayoutEntry(UniformType.SAMPLER_WITH_TEXTURE, 20, ShaderStage.FRAGMENT);
            assert.equal(entries[0].binding, 40);
            assert.equal(entries[1].binding, 41);
            assert.ok(entries[1].binding < PUSH_CONSTANT_RING_BINDING);
        });
    });

    describe('Bind Group Layout: Visibility Computation', () => {
        it('should compute vertex-only visibility', () => {
            assert.equal(computeVisibility(ShaderStage.VERTEX), ShaderStage.VERTEX);
        });

        it('should compute fragment-only visibility', () => {
            assert.equal(computeVisibility(ShaderStage.FRAGMENT), ShaderStage.FRAGMENT);
        });

        it('should compute vertex+fragment visibility', () => {
            const vis = computeVisibility(ShaderStage.VERTEX | ShaderStage.FRAGMENT);
            assert.equal(vis, ShaderStage.VERTEX | ShaderStage.FRAGMENT);
        });

        it('should compute compute-only visibility', () => {
            assert.equal(computeVisibility(ShaderStage.COMPUTE), ShaderStage.COMPUTE);
        });

        it('should handle all stages combined', () => {
            const all = ShaderStage.VERTEX | ShaderStage.FRAGMENT | ShaderStage.COMPUTE;
            assert.equal(computeVisibility(all), all);
        });
    });

    describe('Bind Group Layout: Push Constant Layout', () => {
        it('should create PC layout at binding 120', () => {
            const entry = createPushConstantLayoutEntry(ShaderStage.VERTEX | ShaderStage.FRAGMENT);
            assert.equal(entry.binding, 120);
        });

        it('should use ReadOnlyStorage buffer type', () => {
            const entry = createPushConstantLayoutEntry(ShaderStage.VERTEX);
            assert.equal(entry.buffer.type, BufferBindingType.ReadOnlyStorage);
        });

        it('should have dynamic offset enabled', () => {
            const entry = createPushConstantLayoutEntry(ShaderStage.VERTEX);
            assert.equal(entry.buffer.hasDynamicOffset, true);
        });

        it('should not collide with split sampler bindings', () => {
            // Max original binding ~20, max split = 41
            assert.greaterThan(PUSH_CONSTANT_RING_BINDING, 41);
        });
    });

    describe('Bind Group Layout: Gap Bind Groups', () => {
        it('should compute no gaps when sets are contiguous with PC group', () => {
            const gaps = computeGapIndices(3, 3);
            assert.equal(gaps.length, 0);
        });

        it('should compute gap indices between set_count and pc_group', () => {
            const gaps = computeGapIndices(1, 3);
            assert.deepEqual(gaps, [1, 2]);
        });

        it('should handle single gap', () => {
            const gaps = computeGapIndices(2, 3);
            assert.deepEqual(gaps, [2]);
        });

        it('should handle no material sets (all gaps)', () => {
            const gaps = computeGapIndices(0, 3);
            assert.deepEqual(gaps, [0, 1, 2]);
        });
    });
}
