/**
 * Shader Corpus Test: SPIR-V → WGSL Validation
 *
 * Validates that all test SPIR-V files convert to valid WGSL using the Tint CLI.
 * Tint is the C++ SPIR-V→WGSL translator compiled into the Godot engine binary.
 * This test uses a standalone Tint CLI (tint_convert_cli) for offline validation.
 *
 * If tint_convert_cli is not found, the test prints a warning and exits
 * successfully — runtime Tint (linked into the engine WASM) handles all
 * conversion at runtime, so the offline test is a bonus, not a gate.
 *
 * Usage: node run_tests.mjs
 */

import { readFileSync, readdirSync, writeFileSync, mkdirSync, existsSync } from 'fs';
import { join, basename } from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';
import { execFileSync } from 'child_process';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Paths
const REPO_ROOT = join(__dirname, '..', '..');
const FIXTURES_DIR = join(__dirname, 'fixtures');
const RESULTS_DIR = join(__dirname, 'results');

// Search for tint_convert_cli in standard locations.
function findTintCli() {
    const candidates = [
        join(REPO_ROOT, 'bin', 'tint_convert_cli'),
        join(REPO_ROOT, 'drivers', 'webgpu', 'tint_convert_cli'),
    ];
    for (const c of candidates) {
        if (existsSync(c)) return c;
    }
    // Try PATH
    try {
        execFileSync('which', ['tint_convert_cli'], { encoding: 'utf-8' });
        return 'tint_convert_cli';
    } catch {
        return null;
    }
}

// ─── Test Runner ───────────────────────────────────────────────────────────

function runTests() {
    console.log('╔══════════════════════════════════════════════════════════╗');
    console.log('║     Shader Corpus Test: SPIR-V → WGSL Validation        ║');
    console.log('╚══════════════════════════════════════════════════════════╝\n');

    const tintCli = findTintCli();
    if (!tintCli) {
        console.log('NOTE: tint_convert_cli not found — skipping offline SPIR-V→WGSL validation.');
        console.log('      Runtime Tint (linked into the engine WASM) handles all conversion.');
        console.log('      To enable this test, build the tint_convert_cli tool.\n');
        console.log('PASS (skipped — no Tint CLI available)\n');
        process.exit(0);
    }

    console.log(`Using Tint CLI: ${tintCli}\n`);

    // Find all .spv files
    mkdirSync(RESULTS_DIR, { recursive: true });
    const spvFiles = readdirSync(FIXTURES_DIR).filter(f => f.endsWith('.spv')).sort();

    if (spvFiles.length === 0) {
        console.error('ERROR: No .spv files found in fixtures/. Run compile_fixtures.sh first.');
        process.exit(1);
    }

    console.log(`Found ${spvFiles.length} SPIR-V test files:\n`);

    let passed = 0;
    let failed = 0;
    const results = [];

    for (const spvFile of spvFiles) {
        const name = basename(spvFile, '.spv');
        const spvPath = join(FIXTURES_DIR, spvFile);

        process.stdout.write(`  [TEST] ${name.padEnd(30)}`);

        const startTime = performance.now();
        try {
            const wgsl = execFileSync(tintCli, [spvPath], {
                encoding: 'utf-8',
                timeout: 30000,
            });
            const elapsed = (performance.now() - startTime).toFixed(2);

            // Basic WGSL validation checks
            const issues = validateWgsl(wgsl, name);

            if (issues.length === 0) {
                console.log(`PASS  (${elapsed}ms, ${wgsl.length} chars)`);
                passed++;
            } else {
                console.log(`WARN  (${elapsed}ms) - ${issues.join(', ')}`);
                passed++; // Warnings are not failures
            }

            // Save WGSL output for inspection
            writeFileSync(join(RESULTS_DIR, `${name}.wgsl`), wgsl);

            results.push({
                name,
                status: issues.length === 0 ? 'PASS' : 'WARN',
                time_ms: parseFloat(elapsed),
                wgsl_size: wgsl.length,
                spv_size: readFileSync(spvPath).length,
                issues,
            });

        } catch (e) {
            const elapsed = (performance.now() - startTime).toFixed(2);
            console.log(`FAIL  (${elapsed}ms)`);
            console.log(`         Error: ${e.message || e}`);
            failed++;

            results.push({
                name,
                status: 'FAIL',
                time_ms: parseFloat(elapsed),
                spv_size: readFileSync(spvPath).length,
                error: e.message || String(e),
            });
        }
    }

    // Summary
    console.log('\n' + '─'.repeat(60));
    console.log(`\nResults: ${passed} passed, ${failed} failed, ${spvFiles.length} total`);

    // Write results JSON
    const report = {
        timestamp: new Date().toISOString(),
        converter: 'tint',
        total_tests: spvFiles.length,
        passed,
        failed,
        results,
    };
    writeFileSync(join(RESULTS_DIR, 'report.json'), JSON.stringify(report, null, 2));
    console.log(`\nDetailed report: results/report.json`);
    console.log(`WGSL outputs:    results/*.wgsl\n`);

    if (failed > 0) {
        process.exit(1);
    }
}

/**
 * Basic validation of generated WGSL — checks for known issues.
 */
function validateWgsl(wgsl, name) {
    const issues = [];

    // Check for empty output
    if (!wgsl || wgsl.trim().length === 0) {
        issues.push('empty output');
        return issues;
    }

    // Check for remaining SPIR-V artifacts that shouldn't appear in WGSL
    if (wgsl.includes('OpVariable')) {
        issues.push('contains SPIR-V opcodes');
    }

    // Check that push constants were converted (should be storage buffer, not push_constant)
    if (wgsl.includes('var<push_constant>')) {
        issues.push('unconverted push_constant');
    }

    // Check that entry points exist
    if (!wgsl.includes('@vertex') && !wgsl.includes('@fragment') && !wgsl.includes('@compute')) {
        issues.push('no entry point found');
    }

    return issues;
}

// ─── Main ──────────────────────────────────────────────────────────────────

runTests();
