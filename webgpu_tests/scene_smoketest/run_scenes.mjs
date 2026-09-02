/**
 * Multi-Scene Multi-Browser WebGPU Smoke Test
 *
 * Exports and runs multiple Godot scenes in headless Chrome, Firefox, and/or Safari,
 * validating:
 * - No GPU errors (device-lost, validation errors)
 * - No shader compilation failures
 * - Engine starts and renders (canvas reaches full size)
 *
 * Scenes are specified in scenes.json. Each scene has an allowed error threshold
 * (most should be 0, some have known JS/GPU warnings).
 *
 * Usage:
 *   node run_scenes.mjs [--export] [--export-only] [--skip-export]
 *                       [--scene <name>] [--scenes <name,name,...>]
 *                       [--browser <chrome|firefox|safari|all>]
 *                       [--timeout <ms>] [--frames <n>]
 *
 * Options:
 *   --export           Export scenes before running (requires Godot editor binary)
 *   --export-only      Export scenes and exit without running tests
 *   --skip-export      Only run already-exported scenes (default)
 *   --scene <name>     Run only the named scene (partial match supported)
 *   --scenes <a,b,...> Run comma-separated list of scenes (--scene also works)
 *   --browser <name>   Browser to test: chrome (default), firefox, safari, or all
 *   --timeout <ms>     Per-scene timeout (default: 30000)
 *   --frames <n>       Minimum frames to wait (default: 60)
 *
 * Exit codes:
 *   0 = all scenes pass
 *   1 = one or more scenes exceed error threshold
 */

import { createServer } from 'http';
import { readFileSync, writeFileSync, existsSync, statSync, mkdirSync, readdirSync, appendFileSync } from 'fs';
import { join, extname, resolve, basename, dirname } from 'path';
import { fileURLToPath } from 'url';
import { execSync } from 'child_process';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const SCENES_CONFIG = join(__dirname, 'scenes.json');
const EXPORTS_DIR = join(__dirname, 'exports');
const DEFAULT_TIMEOUT = 30000;
const DEFAULT_FRAMES = 60;

const MIME_TYPES = {
    '.html': 'text/html',
    '.js': 'text/javascript',
    '.wasm': 'application/wasm',
    '.pck': 'application/octet-stream',
    '.png': 'image/png',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon',
    '.json': 'application/json',
};

// Error-capture script injected into pages for Safari (which can't use page.on('console')).
// Also useful for Chrome/Firefox as a secondary error capture via window._webgpuDebug.
const ERROR_CAPTURE_SCRIPT = `
<script>
(function() {
    window._webgpuDebug = { gpuErrors: [], allErrors: [], shaderFails: [], totalShaders: 0, consoleLogs: [] };
    var origLog = console.log;
    console.log = function() {
        var msg = Array.prototype.join.call(arguments, ' ');
        window._webgpuDebug.consoleLogs.push(msg.substring(0, 500));
        origLog.apply(console, arguments);
    };
    var origError = console.error;
    console.error = function() {
        var msg = Array.prototype.join.call(arguments, ' ');
        window._webgpuDebug.allErrors.push(msg.substring(0, 500));
        if (msg.indexOf('[SHADER]') >= 0 || msg.indexOf('TINT') >= 0 || msg.indexOf('spirv') >= 0) {
            window._webgpuDebug.shaderFails.push(msg.substring(0, 500));
        }
        if (msg.indexOf('GPUValidationError') >= 0 || msg.indexOf('UNCAPTURED-GPU-ERROR') >= 0) {
            window._webgpuDebug.gpuErrors.push(msg.substring(0, 500));
        }
        origError.apply(console, arguments);
    };
    // Hook GPUAdapter.requestDevice to capture uncapturedError events on GPU devices.
    // This is critical for Safari where Playwright console listeners aren't available.
    if (typeof navigator !== 'undefined' && navigator.gpu) {
        var origRequestAdapter = navigator.gpu.requestAdapter.bind(navigator.gpu);
        navigator.gpu.requestAdapter = function() {
            return origRequestAdapter.apply(navigator.gpu, arguments).then(function(adapter) {
                if (!adapter) return adapter;
                var origRequestDevice = adapter.requestDevice.bind(adapter);
                adapter.requestDevice = function() {
                    return origRequestDevice.apply(adapter, arguments).then(function(device) {
                        if (device) {
                            device.addEventListener('uncapturederror', function(e) {
                                var msg = e.error ? (e.error.message || String(e.error)) : 'unknown GPU error';
                                window._webgpuDebug.gpuErrors.push('[uncapturederror] ' + msg.substring(0, 500));
                                window._webgpuDebug.allErrors.push('[uncapturederror] ' + msg.substring(0, 500));
                            });
                        }
                        return device;
                    });
                };
                return adapter;
            });
        };
    }
    window.addEventListener('unhandledrejection', function(e) {
        if (e.reason && e.reason.message) {
            window._webgpuDebug.allErrors.push('[unhandledrejection] ' + e.reason.message.substring(0, 500));
        }
    });
})();
</script>
`;

// ─── HTTP Server ─────────────────────────────────────────────────────────────

function startServer(dir, { injectScript = false, sceneInjectScript = null } = {}) {
    return new Promise((resolve) => {
        const server = createServer((req, res) => {
            const url = req.url.split('?')[0];
            const filePath = join(dir, url === '/' ? 'index.html' : url);

            if (!existsSync(filePath) || statSync(filePath).isDirectory()) {
                res.writeHead(404);
                res.end('Not found');
                return;
            }

            const ext = extname(filePath);
            const headers = {
                'Content-Type': MIME_TYPES[ext] || 'application/octet-stream',
                'Cross-Origin-Opener-Policy': 'same-origin',
                'Cross-Origin-Embedder-Policy': 'require-corp',
            };

            let content = readFileSync(filePath);

            // Inject error-capture script and per-scene config into HTML pages
            if (injectScript && ext === '.html') {
                let html = content.toString('utf8');
                let injected = ERROR_CAPTURE_SCRIPT;
                if (sceneInjectScript) {
                    injected += `\n<script>${sceneInjectScript}</script>\n`;
                }
                html = html.replace('<head>', '<head>' + injected);
                content = Buffer.from(html, 'utf8');
            }

            res.writeHead(200, headers);
            res.end(content);
        });

        server.listen(0, '127.0.0.1', () => {
            resolve({ server, port: server.address().port, url: `http://127.0.0.1:${server.address().port}` });
        });
    });
}

// ─── Export ──────────────────────────────────────────────────────────────────

function exportScene(scene, editorBin, templateZip) {
    const exportDir = join(EXPORTS_DIR, scene.export_id || scene.id);
    mkdirSync(exportDir, { recursive: true });

    const projectPath = resolve(__dirname, scene.path);
    if (!existsSync(join(projectPath, 'project.godot'))) {
        return { success: false, error: `project.godot not found at ${projectPath}` };
    }

    // Patch export preset to use our template
    const preset = scene.preset || 'WebGPU';
    const presetsPath = join(projectPath, 'export_presets.cfg');
    if (existsSync(presetsPath)) {
        let content = readFileSync(presetsPath, 'utf8');
        content = content.replace(/custom_template\/release="[^"]*"/g, `custom_template/release="${templateZip}"`);
        content = content.replace(/variant\/extensions_support=true/g, 'variant/extensions_support=false');
        content = content.replace(/vram_texture_compression\/for_mobile=true/g, 'vram_texture_compression/for_mobile=false');
        writeFileSync(presetsPath, content);
    }

    const exportPath = join(exportDir, 'index.html');

    try {
        console.log(`    Exporting ${scene.id}...`);
        execSync(
            `"${editorBin}" --headless --path "${projectPath}" --export-release "${preset}" "${exportPath}"`,
            { timeout: 90000, stdio: 'pipe' }
        );

        if (!existsSync(join(exportDir, 'index.html'))) {
            return { success: false, error: 'Export produced no index.html' };
        }
        return { success: true, exportDir };
    } catch (e) {
        return { success: false, error: e.stderr?.toString().substring(0, 200) || e.message?.substring(0, 200) || 'export failed' };
    }
}

// ─── Blank Canvas Detection ─────────────────────────────────────────────────
// Samples the canvas by drawing it to a temporary 2D canvas and checking if
// any pixels have color. Returns { blank, nonBlackPixels, total } or null on error.
const BLANK_CANVAS_CHECK_JS = `
(function() {
    var canvas = document.getElementById('canvas');
    if (!canvas || canvas.width < 10 || canvas.height < 10) return JSON.stringify({ blank: true, reason: 'no canvas or too small' });
    try {
        var tmp = document.createElement('canvas');
        tmp.width = 64;
        tmp.height = 64;
        var ctx = tmp.getContext('2d');
        ctx.drawImage(canvas, 0, 0, 64, 64);
        var data = ctx.getImageData(0, 0, 64, 64).data;
        var nonBlack = 0;
        for (var i = 0; i < data.length; i += 4) {
            if (data[i] > 10 || data[i+1] > 10 || data[i+2] > 10) nonBlack++;
        }
        var total = data.length / 4;
        return JSON.stringify({ blank: nonBlack < (total * 0.01), nonBlackPixels: nonBlack, total: total });
    } catch(e) {
        return JSON.stringify({ blank: null, reason: e.message });
    }
})()
`;

// ─── Run Scene (Chrome / Firefox via Playwright) ─────────────────────────────

async function runScenePlaywright(scene, browser, timeout) {
    const exportDir = join(EXPORTS_DIR, scene.export_id || scene.id);
    if (!existsSync(join(exportDir, 'index.html'))) {
        return { status: 'SKIP', reason: 'not exported' };
    }

    const { server, url } = await startServer(exportDir, { injectScript: true, sceneInjectScript: scene.inject_script || null });

    const page = await browser.newPage();

    const gpuErrors = [];
    const shaderErrors = [];
    const consoleErrors = [];
    let deviceLost = false;
    let engineStarted = false;

    // Per-scene console output validation (e.g. [HEIGHTMAP-CHECK] PASS).
    const passPatterns = scene.pass_patterns || [];
    const matchedPatterns = new Set();

    page.on('console', (msg) => {
        const text = msg.text();

        if (text.includes('UNCAPTURED-GPU-ERROR') || text.includes('GPUValidationError')) {
            gpuErrors.push(text.substring(0, 200));
        }

        if (text.includes('[SHADER]') || text.includes('Tint conversion') || text.includes('spirv error')) {
            shaderErrors.push(text.substring(0, 200));
        }

        if (text.includes('device lost') || text.includes('Device lost')) {
            deviceLost = true;
        }

        if (text.includes('Godot Engine v')) {
            engineStarted = true;
        }

        if (msg.type() === 'error' && !text.includes('installHook')) {
            consoleErrors.push(text.substring(0, 200));
        }

        for (const pat of passPatterns) {
            if (text.includes(pat)) {
                matchedPatterns.add(pat);
            }
        }
    });

    page.on('pageerror', (err) => {
        consoleErrors.push(err.message.substring(0, 200));
    });

    await page.goto(url);

    // Wait for engine to start (and pass_patterns to match) or timeout
    const startTime = Date.now();
    while ((Date.now() - startTime) < timeout) {
        await new Promise(r => setTimeout(r, 500));
        const patternsReady = passPatterns.length === 0 || matchedPatterns.size >= passPatterns.length;
        if (engineStarted && patternsReady && (Date.now() - startTime) > Math.min(timeout, 10000)) {
            break;
        }
    }

    // Also check injected _webgpuDebug for errors that may not have hit console listener
    try {
        const debugData = await page.evaluate(() => {
            const d = window._webgpuDebug || {};
            return {
                gpuErrors: d.gpuErrors || [],
                shaderFails: d.shaderFails || [],
                allErrors: d.allErrors || [],
                canvasW: document.getElementById('canvas')?.width || 0,
                canvasH: document.getElementById('canvas')?.height || 0,
            };
        });

        // Merge any errors captured by the injected script
        for (const e of debugData.gpuErrors) {
            if (!gpuErrors.some(g => g.includes(e.substring(0, 50)))) {
                gpuErrors.push(e.substring(0, 200));
            }
        }
        for (const e of debugData.shaderFails) {
            if (!shaderErrors.some(s => s.includes(e.substring(0, 50)))) {
                shaderErrors.push(e.substring(0, 200));
            }
        }
    } catch {}

    // Check for blank canvas using Playwright element screenshot.
    // Note: drawImage from a WebGPU canvas returns black (the back buffer is cleared
    // after presentation), so we use Playwright's compositor-level screenshot instead.
    // A solid black canvas compresses to < 2KB as PNG. Rendered scenes with dark
    // backgrounds still produce > 3KB due to anti-aliased edges, UI elements, etc.
    let blankCanvas = false;
    try {
        const canvasEl = await page.$('#canvas');
        if (canvasEl) {
            const buf = await canvasEl.screenshot();
            blankCanvas = buf.length < 2500;
        }
    } catch {}

    await page.close();
    server.close();

    const maxErrors = scene.max_errors || 0;
    const totalErrors = gpuErrors.length + shaderErrors.length + (deviceLost ? 1 : 0);
    const unmatchedPatterns = passPatterns.filter(p => !matchedPatterns.has(p));
    const passed = totalErrors <= maxErrors && !deviceLost && !blankCanvas && unmatchedPatterns.length === 0;

    return {
        status: passed ? 'PASS' : 'FAIL',
        engineStarted,
        gpuErrors: gpuErrors.length,
        shaderErrors: shaderErrors.length,
        consoleErrors: consoleErrors.length,
        deviceLost,
        blankCanvas,
        unmatchedPatterns,
        totalErrors,
        maxErrors,
        details: [...gpuErrors.slice(0, 3), ...shaderErrors.slice(0, 3)],
    };
}

// ─── Run Scene (Safari via AppleScript) ──────────────────────────────────────

function safariEval(js) {
    const escaped = js.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    const script = `tell application "Safari" to do JavaScript "${escaped}" in front document`;
    try {
        const raw = execSync(`osascript -e '${script.replace(/'/g, "'\"'\"'")}'`, {
            timeout: 10000, encoding: 'utf-8'
        }).trim();
        try { return JSON.parse(raw); } catch { return raw; }
    } catch {
        return null;
    }
}

async function runSceneSafari(scene, timeout) {
    const exportDir = join(EXPORTS_DIR, scene.export_id || scene.id);
    if (!existsSync(join(exportDir, 'index.html'))) {
        return { status: 'SKIP', reason: 'not exported' };
    }

    const { server, port, url } = await startServer(exportDir, { injectScript: true, sceneInjectScript: scene.inject_script || null });
    const pageUrl = `${url}/index.html`;

    // Open URL in Safari
    try {
        execSync(`osascript -e 'tell application "Safari"' -e 'activate' -e 'if (count of windows) = 0 then make new document' -e 'set URL of front document to "${pageUrl}"' -e 'end tell'`);
    } catch (e) {
        server.close();
        return { status: 'FAIL', reason: `Could not open Safari: ${e.message}`, gpuErrors: 0, shaderErrors: 0, consoleErrors: 0, deviceLost: false, totalErrors: 1, maxErrors: 0, details: [] };
    }

    // Wait for initial load
    await new Promise(r => setTimeout(r, 3000));

    // Poll until canvas reaches full size or timeout
    let result = null;
    const pollInterval = 5000;
    const maxPolls = Math.ceil(timeout / pollInterval);
    for (let i = 0; i < maxPolls; i++) {
        await new Promise(r => setTimeout(r, pollInterval));
        result = safariEval(`
            (function() {
                var d = window._webgpuDebug || {};
                var canvas = document.getElementById('canvas');
                return JSON.stringify({
                    gpuErrors: d.gpuErrors ? d.gpuErrors.length : 0,
                    shaderFails: d.shaderFails ? d.shaderFails.length : 0,
                    allErrors: (d.allErrors || []).length,
                    canvasW: canvas ? canvas.width : 0,
                    canvasH: canvas ? canvas.height : 0,
                    startError: (d.allErrors || []).find(function(e) { return e.indexOf('Start error') >= 0; }) || null,
                    firstGpuError: d.gpuErrors && d.gpuErrors[0] ? d.gpuErrors[0].substring(0, 200) : null,
                    deviceLost: (d.allErrors || []).some(function(e) { return e.indexOf('device lost') >= 0 || e.indexOf('Device lost') >= 0; })
                });
            })()
        `);
        if (!result) continue;
        if (result.canvasW > 300 && result.canvasH > 300) break;
    }

    // Extra wait for GPU errors to surface
    if (result && result.canvasW > 300) {
        await new Promise(r => setTimeout(r, 5000));
        const r2 = safariEval(`
            (function() {
                var d = window._webgpuDebug || {};
                var canvas = document.getElementById('canvas');
                return JSON.stringify({
                    gpuErrors: d.gpuErrors ? d.gpuErrors.length : 0,
                    shaderFails: d.shaderFails ? d.shaderFails.length : 0,
                    allErrors: (d.allErrors || []).length,
                    canvasW: canvas ? canvas.width : 0,
                    canvasH: canvas ? canvas.height : 0,
                    startError: (d.allErrors || []).find(function(e) { return e.indexOf('Start error') >= 0; }) || null,
                    firstGpuError: d.gpuErrors && d.gpuErrors[0] ? d.gpuErrors[0].substring(0, 200) : null,
                    deviceLost: (d.allErrors || []).some(function(e) { return e.indexOf('device lost') >= 0 || e.indexOf('Device lost') >= 0; })
                });
            })()
        `);
        if (r2) result = r2;
    }

    // Get detailed error info
    let details = [];
    if (result && (result.gpuErrors > 0 || result.shaderFails > 0)) {
        const detailed = safariEval(`
            (function() {
                var d = window._webgpuDebug || {};
                return JSON.stringify({
                    gpuErrors: (d.gpuErrors || []).slice(0, 3).map(function(e) { return e.substring(0, 200); }),
                    shaderFails: (d.shaderFails || []).slice(0, 3).map(function(e) { return e.substring(0, 200); })
                });
            })()
        `);
        if (detailed) {
            details = [...(detailed.gpuErrors || []), ...(detailed.shaderFails || [])];
        }
    }

    // Check pass_patterns against captured console logs.
    // We check inside the eval to avoid pulling the entire (potentially huge)
    // consoleLogs array through AppleScript's size-limited return value.
    const passPatterns = scene.pass_patterns || [];
    let unmatchedPatterns = [...passPatterns];
    if (passPatterns.length > 0) {
        const patsJson = JSON.stringify(passPatterns);
        const matchedData = safariEval(`
            (function() {
                var d = window._webgpuDebug || {};
                var logs = d.consoleLogs || [];
                var pats = ${patsJson};
                var matched = pats.filter(function(p) { return logs.some(function(l) { return l.indexOf(p) >= 0; }); });
                return JSON.stringify(matched);
            })()
        `);
        if (Array.isArray(matchedData)) {
            unmatchedPatterns = passPatterns.filter(pat => !matchedData.includes(pat));
        }
    }

    // Note: blank canvas detection via drawImage doesn't work for WebGPU canvases
    // (the back buffer is cleared after presentation). Safari doesn't support
    // compositor-level screenshots via AppleScript, so we rely on error-based
    // criteria (GPU errors, shader errors, device lost) for Safari pass/fail.
    let blankCanvas = false;

    server.close();

    if (!result) {
        return {
            status: 'FAIL',
            reason: 'Could not communicate with Safari (enable: Develop > Allow JavaScript from Apple Events)',
            engineStarted: false,
            gpuErrors: 0,
            shaderErrors: 0,
            consoleErrors: 0,
            deviceLost: false,
            blankCanvas: false,
            totalErrors: 1,
            maxErrors: 0,
            details: [],
        };
    }

    const loaded = result.canvasW > 300 && result.canvasH > 300;
    const hasFatalError = !!result.startError;
    const deviceLost = !!result.deviceLost;
    const maxErrors = scene.max_errors || 0;
    const totalErrors = result.gpuErrors + result.shaderFails + (deviceLost ? 1 : 0);
    const passed = totalErrors <= maxErrors && !deviceLost && loaded && !hasFatalError && !blankCanvas && unmatchedPatterns.length === 0;

    return {
        status: passed ? 'PASS' : 'FAIL',
        engineStarted: loaded,
        gpuErrors: result.gpuErrors,
        shaderErrors: result.shaderFails,
        consoleErrors: result.allErrors,
        deviceLost,
        blankCanvas,
        unmatchedPatterns,
        totalErrors,
        maxErrors,
        details,
    };
}

// ─── Browser Launchers ───────────────────────────────────────────────────────

async function launchChrome() {
    const pw = await import('playwright');
    const isCI = !!process.env.CI;

    if (isCI) {
        // In CI (headless Linux): use Playwright's bundled Chromium with WebGPU flags.
        const browser = await pw.chromium.launch({
            headless: true,
            args: [
                '--enable-unsafe-webgpu',
                '--enable-features=Vulkan,UseSkiaRenderer',
                '--use-angle=swiftshader',
                '--enable-gpu',
            ],
        });
        return { browser, name: 'chrome', type: 'playwright' };
    }

    // Locally: use real system Chrome — Playwright's bundled Chromium often lacks WebGPU.
    // No special flags needed: modern Chrome (127+) has WebGPU enabled by default.
    const executablePath = process.platform === 'darwin'
        ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
        : process.platform === 'win32'
            ? 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe'
            : '/usr/bin/google-chrome';
    const browser = await pw.chromium.launch({
        headless: false,
        executablePath,
        args: [],
    });
    return { browser, name: 'chrome', type: 'playwright' };
}

async function launchFirefox() {
    const pw = await import('playwright');
    const isCI = !!process.env.CI;
    const browser = await pw.firefox.launch({
        headless: isCI,
        firefoxUserPrefs: {
            'dom.webgpu.enabled': true,
            'gfx.webgpu.force-enabled': true,
        },
    });
    return { browser, name: 'firefox', type: 'playwright' };
}

// Safari doesn't use Playwright — returns a sentinel
function launchSafari() {
    return { browser: null, name: 'safari', type: 'applescript' };
}

// ─── Main ────────────────────────────────────────────────────────────────────

async function main() {
    const args = process.argv.slice(2);
    const doExport = args.includes('--export') || args.includes('--export-only');
    const exportOnly = args.includes('--export-only');
    // Support both --scene and --scenes (common typo)
    const sceneFlag = args.includes('--scenes') ? '--scenes' : args.includes('--scene') ? '--scene' : null;
    const sceneFilter = sceneFlag ? args[args.indexOf(sceneFlag) + 1] : null;
    const timeout = args.includes('--timeout') ? parseInt(args[args.indexOf('--timeout') + 1]) : DEFAULT_TIMEOUT;
    const browserArg = args.includes('--browser') ? args[args.indexOf('--browser') + 1] : 'chrome';

    console.log('╔═══════════════════════════════════════════════════════════╗');
    console.log('║   Multi-Scene Multi-Browser WebGPU Smoke Test            ║');
    console.log('╚═══════════════════════════════════════════════════════════╝\n');

    // Load scene config
    if (!existsSync(SCENES_CONFIG)) {
        console.error(`ERROR: ${SCENES_CONFIG} not found`);
        process.exit(1);
    }

    const config = JSON.parse(readFileSync(SCENES_CONFIG, 'utf8'));
    let scenes = config.scenes;

    if (sceneFilter) {
        const filters = sceneFilter.split(',').map(f => f.trim());
        scenes = scenes.filter(s => filters.some(f => s.id === f || s.id.includes(f)));
        if (scenes.length === 0) {
            console.error(`No scenes matching "${sceneFilter}"`);
            process.exit(1);
        }
    }

    // Determine browsers to test
    const browserNames = browserArg === 'all'
        ? ['chrome', 'firefox', 'safari']
        : [browserArg];

    console.log(`Scenes to test: ${scenes.length}`);
    console.log(`Browsers: ${browserNames.join(', ')}`);
    console.log(`Timeout per scene: ${timeout}ms`);
    console.log(`Export mode: ${doExport ? 'yes' : 'skip (use pre-exported)'}\n`);

    // Export if requested
    if (doExport) {
        const editorBin = resolve(__dirname, config.editor_bin || 'godot');
        const templateZip = resolve(__dirname, config.template_zip || '../../bin/godot.web.template_release.wasm32.nothreads.zip');

        if (!existsSync(editorBin)) {
            console.error(`ERROR: Editor binary not found: ${editorBin}`);
            process.exit(1);
        }
        if (!existsSync(templateZip)) {
            console.error(`ERROR: Template zip not found: ${templateZip}`);
            process.exit(1);
        }

        console.log(`Using editor: ${editorBin}`);
        console.log(`Using template: ${templateZip}\n`);
        mkdirSync(EXPORTS_DIR, { recursive: true });

        const exported = new Set();
        for (const scene of scenes) {
            const eid = scene.export_id || scene.id;
            if (exported.has(eid)) {
                console.log(`    SHARED:   ${scene.id} (uses ${eid} export)`);
                continue;
            }
            const result = exportScene(scene, editorBin, templateZip);
            if (!result.success) {
                console.log(`    EXPORT FAILED: ${scene.id} — ${result.error}`);
            } else {
                console.log(`    EXPORTED: ${scene.id}`);
                exported.add(eid);
            }
        }
        console.log('');
    }

    if (exportOnly) {
        console.log('Export-only mode — skipping tests.');
        process.exit(0);
    }

    // Run tests per browser
    const allResults = [];
    let totalFailed = 0;

    for (const browserName of browserNames) {
        console.log(`\n${'═'.repeat(60)}`);
        console.log(`  Browser: ${browserName.toUpperCase()}`);
        console.log(`${'═'.repeat(60)}\n`);

        let browserHandle;
        try {
            if (browserName === 'chrome') {
                browserHandle = await launchChrome();
            } else if (browserName === 'firefox') {
                browserHandle = await launchFirefox();
            } else if (browserName === 'safari') {
                browserHandle = launchSafari();
            } else {
                console.error(`Unknown browser: ${browserName}`);
                process.exit(1);
            }
        } catch (e) {
            console.error(`ERROR: Could not launch ${browserName}: ${e.message}`);
            console.error(`  Install with: npx playwright install ${browserName === 'chrome' ? 'chromium' : browserName}`);
            totalFailed += scenes.length;
            continue;
        }

        let passed = 0;
        let failed = 0;
        let skipped = 0;

        for (const scene of scenes) {
            process.stdout.write(`  ${scene.id.padEnd(35)}`);

            // Filter pass_patterns by browser if pass_patterns_browsers is set.
            const filteredScene = { ...scene };
            if (scene.pass_patterns && scene.pass_patterns_browsers) {
                if (!scene.pass_patterns_browsers.includes(browserName)) {
                    filteredScene.pass_patterns = [];
                }
            }

            let result;
            if (browserHandle.type === 'applescript') {
                result = await runSceneSafari(filteredScene, timeout);
            } else {
                result = await runScenePlaywright(filteredScene, browserHandle.browser, timeout);
            }

            allResults.push({ scene: scene.id, browser: browserName, ...result });

            if (result.status === 'SKIP') {
                console.log('SKIP  (not exported)');
                skipped++;
            } else if (result.status === 'PASS') {
                const errNote = result.totalErrors > 0 ? ` [${result.totalErrors} errors, ${result.maxErrors} allowed]` : '';
                console.log(`PASS  (gpu=${result.gpuErrors}, shader=${result.shaderErrors})${errNote}`);
                passed++;
            } else {
                const blankNote = result.blankCanvas ? ', blank_canvas=true' : '';
                const patNote = result.unmatchedPatterns?.length ? ', missing_patterns=' + result.unmatchedPatterns.length : '';
                console.log(`FAIL  (gpu=${result.gpuErrors}, shader=${result.shaderErrors}, device_lost=${result.deviceLost}${blankNote}${patNote})`);
                if (result.blankCanvas) {
                    console.log(`         Canvas rendered but is blank/black`);
                }
                if (result.unmatchedPatterns?.length) {
                    for (const p of result.unmatchedPatterns) {
                        console.log(`         Missing required log: "${p}"`);
                    }
                }
                if (result.details && result.details.length > 0) {
                    for (const d of result.details.slice(0, 2)) {
                        console.log(`         ${d.substring(0, 100)}`);
                    }
                }
                if (result.reason) {
                    console.log(`         ${result.reason}`);
                }
                failed++;
            }
        }

        // Close browser
        if (browserHandle.browser) {
            await browserHandle.browser.close();
        }

        console.log(`\n  ${browserName}: ${passed} passed, ${failed} failed, ${skipped} skipped`);
        totalFailed += failed;
    }

    // ─── Final Summary ───────────────────────────────────────────────────────

    console.log('\n' + '═'.repeat(60));
    console.log('  SUMMARY');
    console.log('═'.repeat(60) + '\n');

    // Print matrix table
    const colWidth = 10;
    const sceneWidth = 30;
    const header = '  ' + 'Scene'.padEnd(sceneWidth) + browserNames.map(b => b.padEnd(colWidth)).join('');
    console.log(header);
    console.log('  ' + '─'.repeat(sceneWidth + browserNames.length * colWidth));

    for (const scene of scenes) {
        let row = '  ' + scene.id.substring(0, sceneWidth - 1).padEnd(sceneWidth);
        for (const browserName of browserNames) {
            const r = allResults.find(r => r.scene === scene.id && r.browser === browserName);
            const cell = r ? r.status : '—';
            row += cell.padEnd(colWidth);
        }
        console.log(row);
    }

    console.log('');

    if (totalFailed > 0) {
        console.log('Failed:');
        for (const r of allResults.filter(r => r.status === 'FAIL')) {
            console.log(`  - ${r.scene} [${r.browser}]: ${r.totalErrors} errors (max: ${r.maxErrors})${r.reason ? ' — ' + r.reason : ''}`);
        }
        console.log('');
    }

    const totalPassed = allResults.filter(r => r.status === 'PASS').length;
    const totalSkipped = allResults.filter(r => r.status === 'SKIP').length;
    console.log(`Total: ${totalPassed} passed, ${totalFailed} failed, ${totalSkipped} skipped (${scenes.length} scenes × ${browserNames.length} browsers)\n`);

    process.exit(totalFailed > 0 ? 1 : 0);
}

main().catch((e) => {
    console.error('Fatal:', e);
    process.exit(1);
});
