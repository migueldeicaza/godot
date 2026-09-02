#!/usr/bin/env node
/**
 * WebGPU Driver Unit Tests — Runner
 *
 * Tests the algorithmic logic of the WebGPU rendering driver subsystems
 * without requiring a full engine build or browser environment.
 *
 * Subsystems tested:
 *   1. Ring buffer allocation and wrap-around
 *   2. Shadow buffer copy correctness
 *   3. Descriptor set / bind group layout generation
 *   4. Pipeline state hashing and caching
 *   5. Command buffer encoding sequences
 *   6. Texture format mapping (Godot format -> WebGPU format)
 *   7. Buffer alignment and offset calculations
 *   8. Uniform buffer packing (std140 layout)
 *   9. Texture copyable layout computations
 *  10. Texture format read/write conversions
 *  11. Bind group layout compatibility
 *
 * Usage:
 *   node run_tests.mjs
 */

import { summary } from './test_harness.mjs';
import { runTests as testRingBuffer } from './test_ring_buffer.mjs';
import { runTests as testFormatMapping } from './test_format_mapping.mjs';
import { runTests as testBufferAlignment } from './test_buffer_alignment.mjs';
import { runTests as testPipelineHashing } from './test_pipeline_hashing.mjs';
import { runTests as testBindGroupLayout } from './test_bind_group_layout.mjs';
import { runTests as testShadowBuffer } from './test_shadow_buffer.mjs';
import { runTests as testStd140Packing } from './test_std140_packing.mjs';
import { runTests as testCommandBuffer } from './test_command_buffer.mjs';
import { runTests as testTextureLayout } from './test_texture_layout.mjs';
import { runTests as testTextureConversion } from './test_texture_conversion.mjs';
import { runTests as testBindGroupCompat } from './test_bind_group_compat.mjs';
import { runTests as testOverrideFiltering } from './test_override_filtering.mjs';

console.log('================================================================');
console.log('  WebGPU Driver Unit Tests');
console.log('  Testing algorithmic correctness of driver subsystems');
console.log('================================================================');

// Run all test suites.
testRingBuffer();
testFormatMapping();
testBufferAlignment();
testPipelineHashing();
testBindGroupLayout();
testShadowBuffer();
testStd140Packing();
testCommandBuffer();
testTextureLayout();
testTextureConversion();
testBindGroupCompat();
testOverrideFiltering();

// Print summary and exit.
const results = summary();
process.exit(results.failed > 0 ? 1 : 0);
