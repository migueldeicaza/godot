/**
 * Minimal test harness for driver unit tests.
 * Provides assert/describe/it semantics without external dependencies.
 */

let totalPassed = 0;
let totalFailed = 0;
let totalSkipped = 0;
const failures = [];

export function describe(name, fn) {
    console.log(`\n  ${name}`);
    fn();
}

export function it(name, fn) {
    try {
        fn();
        totalPassed++;
        console.log(`    \x1b[32m\u2713\x1b[0m ${name}`);
    } catch (e) {
        totalFailed++;
        console.log(`    \x1b[31m\u2717\x1b[0m ${name}`);
        console.log(`      ${e.message}`);
        failures.push({ test: name, error: e.message });
    }
}

export function skip(name, _fn) {
    totalSkipped++;
    console.log(`    \x1b[33m-\x1b[0m ${name} (skipped)`);
}

export const assert = {
    equal(actual, expected, msg) {
        if (actual !== expected) {
            throw new Error(msg || `Expected ${expected}, got ${actual}`);
        }
    },
    deepEqual(actual, expected, msg) {
        const a = JSON.stringify(actual);
        const b = JSON.stringify(expected);
        if (a !== b) {
            throw new Error(msg || `Expected ${b}, got ${a}`);
        }
    },
    ok(value, msg) {
        if (!value) {
            throw new Error(msg || `Expected truthy, got ${value}`);
        }
    },
    throws(fn, msg) {
        let threw = false;
        try { fn(); } catch { threw = true; }
        if (!threw) {
            throw new Error(msg || 'Expected function to throw');
        }
    },
    closeTo(actual, expected, tolerance, msg) {
        if (Math.abs(actual - expected) > tolerance) {
            throw new Error(msg || `Expected ${actual} to be within ${tolerance} of ${expected}`);
        }
    },
    greaterThan(actual, expected, msg) {
        if (actual <= expected) {
            throw new Error(msg || `Expected ${actual} > ${expected}`);
        }
    },
    lessThanOrEqual(actual, expected, msg) {
        if (actual > expected) {
            throw new Error(msg || `Expected ${actual} <= ${expected}`);
        }
    },
};

export function summary() {
    console.log('\n' + '='.repeat(60));
    console.log(`  Results: ${totalPassed} passed, ${totalFailed} failed, ${totalSkipped} skipped`);
    console.log('='.repeat(60));
    if (failures.length > 0) {
        console.log('\n  Failures:');
        for (const f of failures) {
            console.log(`    - ${f.test}: ${f.error}`);
        }
    }
    return { passed: totalPassed, failed: totalFailed, skipped: totalSkipped };
}
