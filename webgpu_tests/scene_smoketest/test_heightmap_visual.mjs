/**
 * Visual correctness test for the compute heightmap scene.
 *
 * Checks the [HEIGHTMAP-CHECK] log emitted by GDScript after GPU readback.
 * Captures the FIRST result only (subsequent button presses may succeed
 * even when the initial auto-run fails).
 *
 * Usage:
 *   node test_heightmap_visual.mjs [--browser chrome|firefox|all]
 */

import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname, dirname } from 'path';
import { fileURLToPath } from 'url';
import pw from 'playwright';

const __dirname = dirname(fileURLToPath(import.meta.url));
const SCENE_DIR = join(__dirname, 'exports', 'demo_compute_heightmap');

const MIME = {
  '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
  '.pck': 'application/octet-stream', '.png': 'image/png', '.json': 'application/json',
  '.svg': 'image/svg+xml', '.ico': 'image/x-icon',
};

let nextPort = 9100;

function startServer() {
  const port = nextPort++;
  return new Promise((resolve, reject) => {
    const server = createServer((req, res) => {
      const p = join(SCENE_DIR, req.url === '/' ? 'index.html' : req.url);
      if (!existsSync(p)) { res.writeHead(404); return res.end('Not found'); }
      res.setHeader('Content-Type', MIME[extname(p)] || 'application/octet-stream');
      res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
      res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
      res.end(readFileSync(p));
    });
    server.on('error', reject);
    server.listen(port, () => resolve({ server, port, url: `http://127.0.0.1:${port}` }));
  });
}

async function testBrowser(browserName) {
  const { server, url } = await startServer();

  let browser;
  if (browserName === 'chrome') {
    browser = await pw.chromium.launch({
      headless: false,
      args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer'],
    });
  } else if (browserName === 'firefox') {
    browser = await pw.firefox.launch({
      headless: false,
      firefoxUserPrefs: {
        'dom.webgpu.enabled': true,
        'gfx.webgpu.force-enabled': true,
        'dom.security.https_first': false,
      },
    });
  } else {
    console.log(`  ${browserName}: SKIP`);
    server.close();
    return 'SKIP';
  }

  const page = await browser.newPage();

  let usesWebGPU = false;
  let heightmapResult = null; // capture FIRST result only

  page.on('console', msg => {
    const t = msg.text();
    if (t.includes('WebGPU 1.0')) usesWebGPU = true;
    if (t.includes('[HEIGHTMAP-CHECK]') && !heightmapResult) {
      heightmapResult = t;
    }
  });

  await page.goto(url);

  // Wait for the HEIGHTMAP-CHECK log (up to 30s)
  const deadline = Date.now() + 30000;
  while (!heightmapResult && Date.now() < deadline) {
    await new Promise(r => setTimeout(r, 300));
  }

  await browser.close();
  server.close();

  if (!heightmapResult) {
    console.log(`  ${browserName}: FAIL — no [HEIGHTMAP-CHECK] log within 30s (webgpu=${usesWebGPU})`);
    return 'FAIL';
  }

  const passed = heightmapResult.includes('PASS');
  const line = heightmapResult.replace('[HEIGHTMAP-CHECK] ', '');
  console.log(`  ${browserName}: ${line}`);
  return passed ? 'PASS' : 'FAIL';
}

const args = process.argv.slice(2);
const browserArg = args.includes('--browser') ? args[args.indexOf('--browser') + 1] : 'firefox';
const browsers = browserArg === 'all' ? ['chrome', 'firefox'] : [browserArg];

console.log('Compute Heightmap Visual Correctness Test\n');

let anyFail = false;
for (const b of browsers) {
  const result = await testBrowser(b);
  if (result === 'FAIL') anyFail = true;
}

console.log(anyFail ? '\nRESULT: FAIL' : '\nRESULT: PASS');
process.exit(anyFail ? 1 : 0);
