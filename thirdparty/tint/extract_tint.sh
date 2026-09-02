#!/bin/bash
# Extract minimal Tint source from Dawn for vendoring into Godot.
# Usage: ./extract_tint.sh /path/to/dawn
#
# This extracts only the SPIR-V reader + WGSL writer + core IR + utils,
# excluding tests, fuzzers, benchmarks, and unused backends (GLSL, HLSL, MSL).

set -e

DAWN_DIR="${1:?Usage: $0 /path/to/dawn}"
DEST_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ ! -f "$DAWN_DIR/src/tint/api/tint.h" ]; then
    echo "Error: $DAWN_DIR does not appear to be a Dawn checkout"
    exit 1
fi

echo "Extracting Tint from $DAWN_DIR to $DEST_DIR"

# Clean previous extraction
rm -rf "$DEST_DIR/src"
rm -rf "$DEST_DIR/include"

# Directories to include (relative to Dawn's src/tint/)
INCLUDE_DIRS=(
    "api"
    "lang/core"
    "lang/spirv/reader"
    "lang/spirv"          # top-level spirv files
    "lang/wgsl"
    "utils"
)

# Directories to EXCLUDE
EXCLUDE_PATTERNS=(
    "*/cmd/*"
    "*/lang/spirv/writer/*"
    "*/lang/wgsl/reader/*"
    "*/lang/wgsl/ls/*"
    "*/lang/glsl/*"
    "*/lang/hlsl/*"
    "*/lang/msl/*"
    "*_test.cc"
    "*_test.h"
    "*_fuzz.cc"
    "*_bench.cc"
    "*_bench.h"
    "*/fuzz/*"
    "*/bench/*"
)

# Build the find exclude arguments
FIND_EXCLUDES=""
for pat in "${EXCLUDE_PATTERNS[@]}"; do
    FIND_EXCLUDES="$FIND_EXCLUDES ! -path '$pat'"
done

# Copy source files
mkdir -p "$DEST_DIR/src/tint"
for dir in "${INCLUDE_DIRS[@]}"; do
    src="$DAWN_DIR/src/tint/$dir"
    if [ -d "$src" ]; then
        # Copy preserving directory structure
        find "$src" \( -name "*.cc" -o -name "*.h" -o -name "*.inc" \) \
            ! -name "*_test.cc" ! -name "*_test.h" \
            ! -name "*_fuzz.cc" ! -name "*_bench.cc" ! -name "*_bench.h" \
            ! -path "*/lang/spirv/writer/*" \
            ! -path "*/lang/wgsl/reader/*" \
            ! -path "*/lang/wgsl/ls/*" \
            ! -path "*/lang/glsl/*" \
            ! -path "*/lang/hlsl/*" \
            ! -path "*/lang/msl/*" \
            ! -path "*/fuzz/*" \
            ! -path "*/bench/*" \
            | while read f; do
                rel="${f#$DAWN_DIR/}"
                mkdir -p "$DEST_DIR/$(dirname "$rel")"
                cp "$f" "$DEST_DIR/$rel"
            done
    fi
done

# Copy the null writer common (pulled in as dependency even with all writers off)
NULL_DIR="$DAWN_DIR/src/tint/lang/null/writer/common"
if [ -d "$NULL_DIR" ]; then
    find "$NULL_DIR" \( -name "*.cc" -o -name "*.h" \) \
        ! -name "*_test*" | while read f; do
        rel="${f#$DAWN_DIR/}"
        mkdir -p "$DEST_DIR/$(dirname "$rel")"
        cp "$f" "$DEST_DIR/$rel"
    done
fi

# HLSL writer common is also pulled in as dependency
HLSL_COMMON="$DAWN_DIR/src/tint/lang/hlsl/writer/common"
if [ -d "$HLSL_COMMON" ]; then
    find "$HLSL_COMMON" \( -name "*.cc" -o -name "*.h" \) \
        ! -name "*_test*" | while read f; do
        rel="${f#$DAWN_DIR/}"
        mkdir -p "$DEST_DIR/$(dirname "$rel")"
        cp "$f" "$DEST_DIR/$rel"
    done
fi

# Copy LICENSE
cp "$DAWN_DIR/LICENSE" "$DEST_DIR/LICENSE"

# Count results
FILE_COUNT=$(find "$DEST_DIR/src" -name "*.cc" -o -name "*.h" | wc -l)
echo "Extracted $FILE_COUNT source files"
echo "Done."
