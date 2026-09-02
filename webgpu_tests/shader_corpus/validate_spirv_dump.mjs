/**
 * SPIR-V Dump Validator
 *
 * Validates all .spv files in a given directory through the Tint CLI.
 * Designed to run after `GODOT_DUMP_SPIRV=<dir>` produces SPIR-V files during
 * engine shader compilation.
 *
 * Known failures are tracked in expected_failures.json — these are Vulkan-only
 * shader variants that the WebGPU runtime never uses (different defines/code paths).
 * CI only fails on NEW failures beyond the baseline (regressions).
 *
 * Usage:
 *   node validate_spirv_dump.mjs <spirv-directory>
 *   node validate_spirv_dump.mjs ./spirv_dump/
 *   node validate_spirv_dump.mjs ./spirv_dump/ --update-baseline
 *
 * Exit codes:
 *   0 = no regressions (all failures are expected)
 *   1 = new failures detected beyond expected baseline
 */

import { readFileSync, readdirSync, writeFileSync, existsSync } from 'fs';
import { join, basename } from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';
import { execFileSync } from 'child_process';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const REPO_ROOT = join(__dirname, '..', '..');
const EXPECTED_FAILURES_PATH = join(__dirname, 'expected_failures.json');

// Search for tint_convert_cli in standard locations.
function findTintCli() {
    const candidates = [
        join(REPO_ROOT, 'bin', 'tint_convert_cli'),
        join(REPO_ROOT, 'drivers', 'webgpu', 'tint_convert_cli'),
    ];
    for (const c of candidates) {
        if (existsSync(c)) return c;
    }
    try {
        execFileSync('which', ['tint_convert_cli'], { encoding: 'utf-8' });
        return 'tint_convert_cli';
    } catch {
        return null;
    }
}

// ─── Main ─────────────────────────────────────────────────────────────────────

function main() {
    const spvDir = process.argv[2];
    const updateBaseline = process.argv.includes('--update-baseline');

    if (!spvDir) {
        console.error('Usage: node validate_spirv_dump.mjs <spirv-directory> [--update-baseline]');
        process.exit(1);
    }

    console.log('╔══════════════════════════════════════════════════════════╗');
    console.log('║   SPIR-V Dump Validator (Tint)                           ║');
    console.log('╚══════════════════════════════════════════════════════════╝\n');

    const tintCli = findTintCli();
    if (!tintCli) {
        console.log('NOTE: tint_convert_cli not found — skipping offline SPIR-V validation.');
        console.log('      Runtime Tint (linked into the engine WASM) handles all conversion.');
        console.log('      To enable this test, build the tint_convert_cli tool.\n');
        console.log('PASS (skipped — no Tint CLI available)\n');
        process.exit(0);
    }

    console.log(`Using Tint CLI: ${tintCli}\n`);

    // Load expected failures baseline
    let expectedFailures = new Set();
    if (existsSync(EXPECTED_FAILURES_PATH) && !updateBaseline) {
        const baseline = JSON.parse(readFileSync(EXPECTED_FAILURES_PATH, 'utf8'));
        expectedFailures = new Set(baseline.expected_failures || []);
        console.log(`Expected failures baseline: ${expectedFailures.size} shaders\n`);
    }

    // Find all .spv files
    let spvFiles;
    try {
        spvFiles = readdirSync(spvDir).filter(f => f.endsWith('.spv')).sort();
    } catch (e) {
        console.error(`ERROR: Cannot read directory: ${spvDir}`);
        console.error(e.message);
        process.exit(1);
    }

    if (spvFiles.length === 0) {
        console.error(`ERROR: No .spv files found in ${spvDir}`);
        console.error('Did you run with GODOT_DUMP_SPIRV set?');
        process.exit(1);
    }

    console.log(`Found ${spvFiles.length} SPIR-V files in ${spvDir}\n`);

    let passed = 0;
    let failed = 0;
    let expectedFailed = 0;
    const failures = [];
    const regressions = [];

    for (const spvFile of spvFiles) {
        const spvPath = join(spvDir, spvFile);

        process.stdout.write(`  ${spvFile.padEnd(55)}`);

        const startTime = performance.now();
        try {
            const wgsl = execFileSync(tintCli, [spvPath], {
                encoding: 'utf-8',
                timeout: 30000,
            });
            const elapsed = (performance.now() - startTime).toFixed(1);
            console.log(`PASS  (${elapsed}ms, ${wgsl.length} chars)`);
            passed++;
        } catch (e) {
            const elapsed = (performance.now() - startTime).toFixed(1);
            const isExpected = expectedFailures.has(spvFile);
            const errorMsg = e.stderr || e.message || String(e);

            if (isExpected) {
                console.log(`XFAIL (${elapsed}ms)  [expected]`);
                expectedFailed++;
            } else {
                console.log(`FAIL  (${elapsed}ms)`);
                console.log(`         ${errorMsg.split('\n')[0]}`);
                regressions.push({ file: spvFile, error: errorMsg });
            }
            failed++;
            failures.push({ file: spvFile, error: errorMsg });
        }
    }

    // Summary
    console.log('\n' + '─'.repeat(60));
    console.log(`\nResults: ${passed} passed, ${failed} failed (${expectedFailed} expected), ${spvFiles.length} total`);
    console.log(`Pass rate: ${((passed / spvFiles.length) * 100).toFixed(1)}%`);

    if (regressions.length > 0) {
        console.log(`\nREGRESSIONS (${regressions.length} new failures):`);
        for (const f of regressions) {
            console.log(`  - ${f.file}: ${f.error.substring(0, 100)}`);
        }
    }

    if (expectedFailed > 0 && !updateBaseline) {
        console.log(`\nExpected failures (${expectedFailed}):`);
        for (const f of failures.filter(f => expectedFailures.has(f.file))) {
            console.log(`  - ${f.file}`);
        }
    }

    // Write report
    const report = {
        timestamp: new Date().toISOString(),
        converter: 'tint',
        spirv_directory: spvDir,
        total: spvFiles.length,
        passed,
        failed,
        expected_failed: expectedFailed,
        regressions: regressions.length,
        failures,
    };

    const reportPath = join(spvDir, 'validation_report.json');
    writeFileSync(reportPath, JSON.stringify(report, null, 2));
    console.log(`\nReport: ${reportPath}`);

    // Update baseline if requested
    if (updateBaseline) {
        const baseline = {
            description: 'Expected SPIR-V validation failures — Vulkan-only shader variants not used by WebGPU runtime',
            updated: new Date().toISOString(),
            expected_failures: failures.map(f => f.file).sort(),
        };
        writeFileSync(EXPECTED_FAILURES_PATH, JSON.stringify(baseline, null, 2) + '\n');
        console.log(`\nBaseline updated: ${EXPECTED_FAILURES_PATH} (${failures.length} expected failures)`);
        process.exit(0);
    }

    // Exit: fail only on regressions
    if (regressions.length > 0) {
        console.error(`\nFAILED: ${regressions.length} new shader(s) cannot be converted.`);
        console.error('If these are expected Vulkan-only variants, run with --update-baseline');
        process.exit(1);
    }

    console.log('\nPASSED: No regressions detected.');
    process.exit(0);
}

main();
