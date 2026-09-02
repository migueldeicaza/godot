import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname, dirname } from 'path';
import { fileURLToPath } from 'url';
import { execSync } from 'child_process';
import pw from 'playwright';

const __dirname = dirname(fileURLToPath(import.meta.url));
const MIME = {'.html':'text/html','.js':'text/javascript','.wasm':'application/wasm','.pck':'application/octet-stream','.png':'image/png','.json':'application/json','.svg':'image/svg+xml','.ico':'image/x-icon'};
const scene = process.argv[2] || 'demo_compute_heightmap';
const dir = join(__dirname, 'exports', scene);

function serve(port) {
  return new Promise(resolve => {
    const server = createServer((req, res) => {
      let p = join(dir, req.url === '/' ? 'index.html' : req.url);
      if (!existsSync(p)) { res.writeHead(404); return res.end('Not found'); }
      res.setHeader('Content-Type', MIME[extname(p)] || 'application/octet-stream');
      res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
      res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
      res.end(readFileSync(p));
    });
    server.listen(port, () => resolve(server));
  });
}

// Start 3 servers on different ports
await serve(8096);
await serve(8097);
await serve(8098);
console.log('Servers running on 8096 (Safari), 8097 (Chrome), 8098 (Firefox)');

// Chrome via Playwright
const chrome = await pw.chromium.launch({
  headless: false,
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer'],
});
const chromePage = await chrome.newPage();
await chromePage.goto('http://127.0.0.1:8097');
console.log('Chrome open at http://127.0.0.1:8097');

// Firefox via Playwright
const firefox = await pw.firefox.launch({
  headless: false,
  firefoxUserPrefs: {
    'dom.webgpu.enabled': true,
    'gfx.webgpu.force-enabled': true,
    'dom.security.https_first': false,
  },
});
const firefoxPage = await firefox.newPage();
await firefoxPage.goto('http://127.0.0.1:8098');
console.log('Firefox open at http://127.0.0.1:8098');

// Safari via open command
execSync('open -a Safari http://127.0.0.1:8096');
console.log('Safari open at http://127.0.0.1:8096');

console.log('\nAll 3 browsers open. Press Ctrl+C to close.');
await new Promise(() => {});
