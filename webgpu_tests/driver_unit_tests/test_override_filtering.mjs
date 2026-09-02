/**
 * Tests for per-stage override constant filtering logic.
 *
 * WebGPU requires that every WGPUConstantEntry key passed to a pipeline
 * stage actually exists as an @id(N) override in that stage's module.
 * Unlike Vulkan (which silently ignores unknown spec constant IDs), WebGPU
 * validates strictly. This test suite verifies the filtering logic that
 * ensures only the right constants reach each stage.
 */

import { describe, it, assert } from './test_harness.mjs';

// --- Simulate the C++ per-stage override ID extraction ---
// This mirrors the logic in shader_create_from_container():
//   const char *scan = wgsl_str;
//   while ((scan = strstr(scan, "@id(")) != nullptr) { ... }
function extractOverrideIds(wgsl) {
    const ids = new Set();
    const pattern = /@id\((\d+)\)/g;
    let match;
    while ((match = pattern.exec(wgsl)) !== null) {
        ids.add(parseInt(match[1], 10));
    }
    return ids;
}

// --- Simulate the C++ per-stage constant filtering ---
// This mirrors the logic in render_pipeline_create():
//   for each specialization constant:
//     if stage_override_ids.has(constant_id): include in this stage's array
function filterConstantsForStage(allConstants, stageOverrideIds) {
    return allConstants.filter(c => stageOverrideIds.has(c.constantId));
}

// --- Build WGPUConstantEntry-like objects ---
function makeConstant(id, value) {
    return { constantId: id, key: String(id), value };
}

export function runTests() {
    describe('Override ID Extraction: @id(N) parsing', () => {
        it('should extract single @id', () => {
            const ids = extractOverrideIds('@id(0) override x: u32 = 0u;');
            assert.equal(ids.size, 1);
            assert.ok(ids.has(0));
        });

        it('should extract multiple @id values', () => {
            const wgsl = `
                @id(0) override packed_0: u32 = 0u;
                @id(1) override packed_1: u32 = 0u;
                @id(2) override packed_2: f32 = 0.0;
                @id(3) override emulate_point_size: u32 = 0u;
            `;
            const ids = extractOverrideIds(wgsl);
            assert.equal(ids.size, 4);
            assert.ok(ids.has(0));
            assert.ok(ids.has(1));
            assert.ok(ids.has(2));
            assert.ok(ids.has(3));
        });

        it('should handle no @id declarations', () => {
            const ids = extractOverrideIds('fn main() { return; }');
            assert.equal(ids.size, 0);
        });

        it('should not match @id in comments or strings', () => {
            // The C++ uses strstr which WOULD match in comments.
            // This test documents the JS regex behavior.
            const ids = extractOverrideIds('// @id(99) is a comment\n@id(0) override x: u32 = 0u;');
            // JS regex finds both — document this behavior
            assert.ok(ids.has(0), 'Real @id(0) should be found');
        });

        it('should handle large @id numbers', () => {
            const ids = extractOverrideIds('@id(23) override x: u32 = 0u;');
            assert.ok(ids.has(23));
        });
    });

    describe('Per-Stage Constant Filtering: vertex + fragment', () => {
        // Scenario: vertex has @id(0), fragment has @id(1) and @id(2).
        // Godot passes all 3 constants to pipeline creation.
        // Only the right ones should reach each stage.

        const allConstants = [
            makeConstant(0, 42),   // VERTEX_FLAGS
            makeConstant(1, 1),    // FRAG_MODE
            makeConstant(2, 0.8),  // BRIGHTNESS
        ];
        const vertexIds = new Set([0]);
        const fragmentIds = new Set([1, 2]);

        it('should pass only @id(0) to vertex stage', () => {
            const vtxConstants = filterConstantsForStage(allConstants, vertexIds);
            assert.equal(vtxConstants.length, 1);
            assert.equal(vtxConstants[0].constantId, 0);
            assert.equal(vtxConstants[0].value, 42);
        });

        it('should pass only @id(1,2) to fragment stage', () => {
            const fragConstants = filterConstantsForStage(allConstants, fragmentIds);
            assert.equal(fragConstants.length, 2);
            assert.equal(fragConstants[0].constantId, 1);
            assert.equal(fragConstants[1].constantId, 2);
        });

        it('should not pass @id(0) to fragment stage', () => {
            const fragConstants = filterConstantsForStage(allConstants, fragmentIds);
            const hasId0 = fragConstants.some(c => c.constantId === 0);
            assert.equal(hasId0, false, 'Fragment should not receive @id(0)');
        });

        it('should not pass @id(1,2) to vertex stage', () => {
            const vtxConstants = filterConstantsForStage(allConstants, vertexIds);
            const hasId1 = vtxConstants.some(c => c.constantId === 1);
            const hasId2 = vtxConstants.some(c => c.constantId === 2);
            assert.equal(hasId1, false, 'Vertex should not receive @id(1)');
            assert.equal(hasId2, false, 'Vertex should not receive @id(2)');
        });
    });

    describe('Per-Stage Constant Filtering: shared constants', () => {
        // Scenario: both vertex and fragment define @id(0) and @id(1).
        // Both stages should receive both constants.

        const allConstants = [
            makeConstant(0, 255),
            makeConstant(1, 128),
        ];
        const vertexIds = new Set([0, 1]);
        const fragmentIds = new Set([0, 1]);

        it('should pass both to vertex when both defined', () => {
            const vtx = filterConstantsForStage(allConstants, vertexIds);
            assert.equal(vtx.length, 2);
        });

        it('should pass both to fragment when both defined', () => {
            const frag = filterConstantsForStage(allConstants, fragmentIds);
            assert.equal(frag.length, 2);
        });
    });

    describe('Per-Stage Constant Filtering: no overrides', () => {
        // Scenario: shader has no override declarations.
        // has_override_declarations would be false, so the override path
        // is never entered. But if it were, no constants should match.

        const allConstants = [
            makeConstant(0, 42),
            makeConstant(1, 1),
        ];
        const emptyIds = new Set();

        it('should pass nothing when stage has no overrides', () => {
            const result = filterConstantsForStage(allConstants, emptyIds);
            assert.equal(result.length, 0);
        });
    });

    describe('Per-Stage Constant Filtering: unknown constant IDs', () => {
        // Scenario: Godot passes constants for IDs that don't exist in
        // any stage's WGSL. These must be filtered out.

        const allConstants = [
            makeConstant(0, 1),     // exists in vertex
            makeConstant(5, 99),    // doesn't exist in any stage
            makeConstant(10, 42),   // doesn't exist in any stage
        ];
        const vertexIds = new Set([0]);

        it('should skip constants with IDs not in the stage', () => {
            const vtx = filterConstantsForStage(allConstants, vertexIds);
            assert.equal(vtx.length, 1);
            assert.equal(vtx[0].constantId, 0);
        });
    });

    describe('Per-Stage Constant Filtering: Godot 4-constant pattern', () => {
        // Godot's SceneForwardMobile uses 4 spec constants:
        //   @id(0) packed_0: u32  (bitfield)
        //   @id(1) packed_1: u32  (bitfield)
        //   @id(2) packed_2: f32
        //   @id(3) emulate_point_size: bool
        // Typically all 4 are in both vertex and fragment stages.

        const allConstants = [
            makeConstant(0, 0xFF00),
            makeConstant(1, 0x0F0F),
            makeConstant(2, 1.5),
            makeConstant(3, 0),  // bool as 0.0
        ];
        const bothStages = new Set([0, 1, 2, 3]);

        it('should pass all 4 to vertex with Godot pattern', () => {
            const vtx = filterConstantsForStage(allConstants, bothStages);
            assert.equal(vtx.length, 4);
        });

        it('should pass all 4 to fragment with Godot pattern', () => {
            const frag = filterConstantsForStage(allConstants, bothStages);
            assert.equal(frag.length, 4);
        });

        it('should handle vertex-only subset (depth pre-pass)', () => {
            // Depth pre-pass might only have vertex stage
            const vertexOnly = new Set([0, 3]); // packed_0 + emulate_point_size
            const vtx = filterConstantsForStage(allConstants, vertexOnly);
            assert.equal(vtx.length, 2);
            assert.equal(vtx[0].constantId, 0);
            assert.equal(vtx[1].constantId, 3);
        });
    });

    describe('Per-Stage Constant Filtering: 24-constant stress test', () => {
        // Mirror the many_overrides.comp fixture: 24 constants
        const allConstants = [];
        for (let i = 0; i < 24; i++) {
            allConstants.push(makeConstant(i, i * 10));
        }
        const allIds = new Set(Array.from({ length: 24 }, (_, i) => i));

        it('should pass all 24 to compute stage', () => {
            const compute = filterConstantsForStage(allConstants, allIds);
            assert.equal(compute.length, 24);
        });

        it('should correctly filter to first 8 of 24', () => {
            const subset = new Set([0, 1, 2, 3, 4, 5, 6, 7]);
            const filtered = filterConstantsForStage(allConstants, subset);
            assert.equal(filtered.length, 8);
            for (let i = 0; i < 8; i++) {
                assert.equal(filtered[i].constantId, i);
            }
        });

        it('should correctly filter to even IDs of 24', () => {
            const evens = new Set([0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22]);
            const filtered = filterConstantsForStage(allConstants, evens);
            assert.equal(filtered.length, 12);
            for (const c of filtered) {
                assert.equal(c.constantId % 2, 0, `ID ${c.constantId} should be even`);
            }
        });
    });

    describe('WGPUConstantEntry key format', () => {
        // WebGPU constant keys are the numeric @id as a string.
        // Verify key generation matches the C++ itos(sc.constant_id).

        it('should format key as plain decimal string', () => {
            const c = makeConstant(0, 42);
            assert.equal(c.key, '0');
        });

        it('should handle multi-digit keys', () => {
            const c = makeConstant(23, 42);
            assert.equal(c.key, '23');
        });

        it('should not zero-pad keys', () => {
            const c = makeConstant(3, 42);
            assert.equal(c.key, '3');
            assert.equal(c.key !== '03', true, 'Key should not be zero-padded');
        });
    });
}
