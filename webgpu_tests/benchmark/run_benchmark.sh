#!/usr/bin/env bash
# Run a Godot scene on native, WebGL, or WebGPU for benchmarking.
#
# Usage:
#   ./run_benchmark.sh native [scene_path]
#   ./run_benchmark.sh webgl  [scene_path]
#   ./run_benchmark.sh webgpu [scene_path]
#
# Default scene: the benchmark project itself (webgpu_tests/benchmark/).
# Pass a path to any Godot project directory to benchmark that instead.
# Injects benchmark_profiler.gd as an autoload to collect CPU/GPU frame times.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
EDITOR_BIN="$REPO_ROOT/bin/godot.macos.editor.arm64"
PROFILER_GD="$SCRIPT_DIR/benchmark_profiler.gd"
WEBGPU_ZIP="$REPO_ROOT/bin/godot.web.template_release.wasm32.nothreads.zip"
# Use official Godot 4.6 WebGL template — our codebase's JS has WebGPU
# loading code baked in even without webgpu=yes, so it can't serve as a
# clean WebGL baseline.
WEBGL_ZIP="$HOME/Library/Application Support/Godot/export_templates/4.6.stable.official.89cea1439/web_nothreads_release.zip"

MODE="${1:-}"
SCENE_PATH="${2:-$SCRIPT_DIR}"
EXPORT_DIR="$SCRIPT_DIR/exports"
PORT=8099

if [ -z "$MODE" ]; then
    echo "Usage: $0 <native|webgl|webgpu> [scene_path]"
    echo ""
    echo "  native  — Run with Metal (Forward+) on macOS"
    echo "  webgl   — Export with WebGL template, serve in Chrome"
    echo "  webgpu  — Export with WebGPU template, serve in Chrome"
    echo ""
    echo "Default scene: benchmark project (webgpu_tests/benchmark/)"
    exit 1
fi

if [ ! -f "$EDITOR_BIN" ]; then
    echo "ERROR: Editor binary not found: $EDITOR_BIN"
    exit 1
fi

# --- Inject profiler autoload into project.godot ---
inject_profiler() {
    local project_dir="$1"
    local project_file="$project_dir/project.godot"
    local profiler_dest="$project_dir/benchmark_profiler.gd"

    # Copy profiler script into the project (skip if same dir)
    if [ "$(cd "$SCRIPT_DIR" && pwd)" != "$(cd "$project_dir" && pwd)" ]; then
        cp "$PROFILER_GD" "$profiler_dest"
    fi

    # Back up project.godot
    cp "$project_file" "$project_file.bak"

    # Add autoload entry if not already present
    if ! grep -q 'BenchmarkProfiler' "$project_file"; then
        # Append autoload section or add to existing one
        if grep -q '^\[autoload\]' "$project_file"; then
            sed -i '' '/^\[autoload\]/a\
BenchmarkProfiler="*res://benchmark_profiler.gd"
' "$project_file"
        else
            echo '' >> "$project_file"
            echo '[autoload]' >> "$project_file"
            echo 'BenchmarkProfiler="*res://benchmark_profiler.gd"' >> "$project_file"
        fi
    fi
}

# --- Restore project.godot after benchmarking ---
restore_project() {
    local project_dir="$1"
    local project_file="$project_dir/project.godot"
    local profiler_dest="$project_dir/benchmark_profiler.gd"

    if [ -f "$project_file.bak" ]; then
        mv "$project_file.bak" "$project_file"
    fi
    # Only remove profiler if we copied it in
    if [ "$(cd "$SCRIPT_DIR" && pwd)" != "$(cd "$project_dir" && pwd)" ]; then
        rm -f "$profiler_dest"
    fi
}

# Resolve scene path to absolute
SCENE_PATH="$(cd "$SCENE_PATH" && pwd)"

# --- Native ---
if [ "$MODE" = "native" ]; then
    echo "=== Native macOS (Metal) ==="
    echo "Scene: $SCENE_PATH"
    inject_profiler "$SCENE_PATH"
    trap "restore_project '$SCENE_PATH'" EXIT
    "$EDITOR_BIN" --path "$SCENE_PATH"
    exit 0
fi

# --- Web (WebGL or WebGPU) ---
if [ "$MODE" = "webgl" ]; then
    TEMPLATE_ZIP="$WEBGL_ZIP"
    PRESET="Web"
    echo "=== WebGL on Chrome ==="
elif [ "$MODE" = "webgpu" ]; then
    TEMPLATE_ZIP="$WEBGPU_ZIP"
    PRESET="WebGPU"
    echo "=== WebGPU on Chrome ==="
else
    echo "ERROR: Unknown mode '$MODE'. Use native, webgl, or webgpu."
    exit 1
fi

if [ ! -f "$TEMPLATE_ZIP" ]; then
    echo "ERROR: Template not found: $TEMPLATE_ZIP"
    echo "Build it with: scons platform=web target=template_release threads=no use_assertions=no [webgpu=yes]"
    exit 1
fi

SCENE_NAME="$(basename "$SCENE_PATH")"
SCENE_EXPORT_DIR="$EXPORT_DIR/${SCENE_NAME}_${MODE}"
EXPORT_HTML="$SCENE_EXPORT_DIR/index.html"

echo "Scene: $SCENE_PATH"
echo "Template: $TEMPLATE_ZIP"
echo "Export to: $SCENE_EXPORT_DIR"

# Patch export_presets.cfg to use the correct template
PRESETS_CFG="$SCENE_PATH/export_presets.cfg"
if [ ! -f "$PRESETS_CFG" ]; then
    echo "ERROR: No export_presets.cfg in $SCENE_PATH"
    exit 1
fi

# Inject profiler and back up project files
inject_profiler "$SCENE_PATH"
cp "$PRESETS_CFG" "$PRESETS_CFG.bak"
sed -i '' "s|custom_template/release=\"[^\"]*\"|custom_template/release=\"$TEMPLATE_ZIP\"|g" "$PRESETS_CFG"

# Ensure cleanup on exit
trap "restore_project '$SCENE_PATH'; [ -f '$PRESETS_CFG.bak' ] && mv '$PRESETS_CFG.bak' '$PRESETS_CFG'" EXIT

# Export
mkdir -p "$SCENE_EXPORT_DIR"
echo "Exporting..."
"$EDITOR_BIN" --headless --path "$SCENE_PATH" --export-release "$PRESET" "$EXPORT_HTML" 2>&1 | tail -5

if [ ! -f "$EXPORT_HTML" ]; then
    echo "ERROR: Export failed — $EXPORT_HTML not created"
    exit 1
fi
echo "Export complete."

# Serve and launch Chrome via Playwright (captures console output)
echo "Launching Chrome via Playwright with console capture..."

# Kill any existing server on this port
lsof -ti:$PORT 2>/dev/null | xargs kill -9 2>/dev/null || true

PW_NODE_MODULES="$SCRIPT_DIR/../scene_smoketest/node_modules"
node -e "
const http = require('http');
const fs = require('fs');
const path = require('path');
const pw = require(path.resolve('$PW_NODE_MODULES', '..', 'node_modules', 'playwright'));

const MIME = {'.html':'text/html','.js':'text/javascript','.wasm':'application/wasm','.pck':'application/octet-stream','.png':'image/png','.json':'application/json','.svg':'image/svg+xml','.ico':'image/x-icon'};
const dir = '$SCENE_EXPORT_DIR';

const server = http.createServer((req, res) => {
    let p = path.join(dir, req.url === '/' ? 'index.html' : req.url);
    if (!fs.existsSync(p)) { res.writeHead(404); return res.end('Not found'); }
    res.setHeader('Content-Type', MIME[path.extname(p)] || 'application/octet-stream');
    res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    res.end(fs.readFileSync(p));
});

server.listen($PORT, async () => {
    console.log('Server ready on port $PORT');
    const browser = await pw.chromium.launch({
        headless: false,
        args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer'],
    });
    const page = await browser.newPage();

    // Forward all console messages to stdout.
    page.on('console', msg => {
        const text = msg.text();
        console.log(text);
    });
    page.on('pageerror', err => console.log('[PAGE-ERROR] ' + err.message));

    await page.goto('http://127.0.0.1:$PORT');
    console.log('Chrome opened — waiting for benchmark results...');

    // Wait until we see the final benchmark line or timeout after 60s.
    await Promise.race([
        page.waitForEvent('console', { predicate: msg => msg.text().includes('================================================'), timeout: 60000 }),
        new Promise(r => setTimeout(r, 60000)),
    ]);

    // Give a moment for any trailing output.
    await new Promise(r => setTimeout(r, 1000));
    await browser.close();
    server.close();
});
"
