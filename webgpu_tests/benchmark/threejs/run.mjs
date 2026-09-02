#!/usr/bin/env node
// Serve Three.js benchmark scenes and capture console output via Playwright.
// Usage: node run.mjs scene_a.html

import http from 'http';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const pw = await import(path.resolve(__dirname, '../../scene_smoketest/node_modules/playwright/index.mjs'));

const scene = process.argv[2] || 'scene_a.html';
const PORT = 8098;

const MIME = {
  '.html': 'text/html',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.css': 'text/css',
  '.json': 'application/json',
  '.wasm': 'application/wasm',
  '.png': 'image/png',
};

const server = http.createServer((req, res) => {
  let filePath = path.join(__dirname, req.url === '/' ? scene : decodeURIComponent(req.url));
  if (!fs.existsSync(filePath)) {
    res.writeHead(404);
    return res.end('Not found: ' + req.url);
  }
  const ext = path.extname(filePath);
  res.setHeader('Content-Type', MIME[ext] || 'application/octet-stream');
  res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  res.end(fs.readFileSync(filePath));
});

server.listen(PORT, async () => {
  console.log(`Server ready on port ${PORT}, serving ${scene}`);
  const browser = await pw.chromium.launch({
    headless: false,
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer'],
  });
  const page = await browser.newPage();

  page.on('console', msg => console.log(msg.text()));
  page.on('pageerror', err => console.log('[PAGE-ERROR] ' + err.message));

  await page.goto(`http://127.0.0.1:${PORT}`);
  console.log('Chrome opened — waiting for benchmark results...');

  // Wait for BENCHMARK_RESULT or timeout after 30s
  await Promise.race([
    page.waitForEvent('console', {
      predicate: msg => msg.text().includes('BENCHMARK_RESULT'),
      timeout: 30000,
    }),
    new Promise(r => setTimeout(r, 30000)),
  ]);

  await new Promise(r => setTimeout(r, 1000));
  await browser.close();
  server.close();
});
