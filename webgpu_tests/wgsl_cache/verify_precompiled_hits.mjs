#!/usr/bin/env node
/**
 * verify_precompiled_hits.mjs — Verifies that the build-time precompiled
 * WGSL table actually gets hit at runtime.
 *
 * Launches the test project in headless Chrome, waits for shader compilation,
 * and checks that _spv_to_wgsl_precompiled_hits > 0 in console output.
 */

import { createServer } from 'http';
import { readFileSync, existsSync, statSync } from 'fs';
import { join, dirname, extname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
// Use a WebGPU scene export (not OpenGL3).
const EXPORT_DIR = process.argv[2]
	? join(process.cwd(), process.argv[2])
	: join(__dirname, '..', 'scene_smoketest', 'exports', 'benchmark_pbr');

if (!existsSync(join(EXPORT_DIR, 'index.html'))) {
	console.error('ERROR: No test project export found at', EXPORT_DIR);
	process.exit(1);
}

const MIME_TYPES = {
	'.html': 'text/html',
	'.js': 'text/javascript',
	'.mjs': 'text/javascript',
	'.wasm': 'application/wasm',
	'.pck': 'application/octet-stream',
	'.png': 'image/png',
	'.json': 'application/json',
};

// Start a simple HTTP server for the export.
const server = createServer((req, res) => {
	let urlPath = req.url.split('?')[0];
	if (urlPath === '/') urlPath = '/index.html';
	const filePath = join(EXPORT_DIR, urlPath);

	if (!existsSync(filePath) || !statSync(filePath).isFile()) {
		res.writeHead(404);
		res.end('Not found');
		return;
	}

	const ext = extname(filePath);
	const contentType = MIME_TYPES[ext] || 'application/octet-stream';

	// Required headers for SharedArrayBuffer (COOP/COEP).
	res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
	res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');

	const data = readFileSync(filePath);
	res.writeHead(200, { 'Content-Type': contentType });
	res.end(data);
});

server.listen(0, async () => {
	const port = server.address().port;
	const url = `http://localhost:${port}/`;
	console.log(`Serving test export on ${url}`);

	let browser;
	try {
		const pw = await import('playwright');
		// Use real system Chrome for WebGPU support (Playwright's bundled Chromium lacks it).
		const executablePath = process.platform === 'darwin'
			? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
			: process.platform === 'win32'
				? 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe'
				: '/usr/bin/google-chrome';
		browser = await pw.chromium.launch({
			headless: false,
			executablePath,
			args: [],
		});
		const context = await browser.newContext();
		const page = await context.newPage();

		const shaderLogs = [];
		const allLogs = [];
		let precompiledHits = 0;
		let cacheHits = 0;
		let cacheMisses = 0;

		page.on('console', (msg) => {
			const text = msg.text();
			allLogs.push(`[${msg.type()}] ${text}`);

			if (text.includes('[SHADER]')) {
				shaderLogs.push(text);

				// Parse hit/miss stats from miss lines.
				const match = text.match(/precompiled_hits=(\d+)\s+cache_hits=(\d+)\s+misses=(\d+)/);
				if (match) {
					precompiledHits = parseInt(match[1]);
					cacheHits = parseInt(match[2]);
					cacheMisses = parseInt(match[3]);
				}

				// Parse precompiled hit lines.
				const hitMatch = text.match(/Precompiled WGSL hit #(\d+)/);
				if (hitMatch) {
					precompiledHits = Math.max(precompiledHits, parseInt(hitMatch[1]));
				}
			}
		});

		page.on('pageerror', (err) => {
			console.log(`  PAGE ERROR: ${err.message}`);
			allLogs.push(`[pageerror] ${err.message}`);
		});

		console.log('Loading engine...');
		await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 60000 });

		// Wait for shader compilation to finish (up to 90 seconds).
		// We know it's done when console logs stop appearing for a few seconds.
		console.log('Waiting for shader compilation...');
		let lastLogCount = 0;
		let stableCount = 0;
		for (let i = 0; i < 90; i++) {
			await new Promise(r => setTimeout(r, 1000));
			if (shaderLogs.length > 0 && shaderLogs.length === lastLogCount) {
				stableCount++;
				if (stableCount >= 5) break; // 5 seconds with no new shader logs
			} else {
				stableCount = 0;
			}
			lastLogCount = shaderLogs.length;
			if (i > 0 && i % 10 === 0) {
				console.log(`  ... ${i}s elapsed, ${shaderLogs.length} shader log entries`);
			}
		}

		// Print results.
		console.log('\n' + '='.repeat(60));
		console.log('  PRECOMPILED WGSL TABLE VERIFICATION');
		console.log('='.repeat(60));

		console.log(`\nTotal console entries: ${allLogs.length}`);
		console.log(`Shader log entries captured: ${shaderLogs.length}`);

		// Show all console output if no shader logs found.
		if (shaderLogs.length === 0 && allLogs.length > 0) {
			console.log('\nAll console output (first 30 lines):');
			for (const log of allLogs.slice(0, 30)) {
				console.log(`  ${log}`);
			}
			if (allLogs.length > 30) {
				console.log(`  ... (${allLogs.length - 30} more lines)`);
			}
		}

		// Show first few and last few shader logs.
		if (shaderLogs.length > 0) {
			console.log('\nFirst 10 shader logs:');
			for (const log of shaderLogs.slice(0, 10)) {
				console.log(`  ${log}`);
			}
			if (shaderLogs.length > 10) {
				console.log(`  ... (${shaderLogs.length - 10} more)`);
			}
		}

		console.log(`\n${'─'.repeat(60)}`);
		console.log(`  Precompiled hits:  ${precompiledHits}`);
		console.log(`  In-memory hits:    ${cacheHits}`);
		console.log(`  Tint fallbacks:    ${cacheMisses}`);
		console.log(`${'─'.repeat(60)}`);

		if (precompiledHits > 0) {
			console.log(`\n  RESULT: PASS — Precompiled table is working!`);
			console.log(`  ${precompiledHits} shaders served from build-time precompiled table.`);
			const total = precompiledHits + cacheMisses;
			if (total > 0) {
				const hitRate = ((precompiledHits / total) * 100).toFixed(1);
				console.log(`  Hit rate: ${hitRate}% (${precompiledHits}/${total} unique lookups)`);
			}
		} else {
			console.log(`\n  RESULT: FAIL — No precompiled table hits!`);
			console.log(`  The build-time hashes don't match runtime SPIR-V.`);
			if (cacheMisses > 0) {
				console.log(`  ${cacheMisses} shaders fell through to Tint.`);
			}
		}

		console.log('');

		await browser.close();
		server.close();
		process.exit(precompiledHits > 0 ? 0 : 1);

	} catch (e) {
		console.error('Error:', e.message);
		if (browser) await browser.close();
		server.close();
		process.exit(1);
	}
});
