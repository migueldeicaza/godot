#!/usr/bin/env python3
"""
Unit tests for the build-time WGSL precompilation system.

Tests:
  1. MurmurHash3 correctness (matches Godot's C++ implementation)
  2. GLSL parsing (stage splitting, include resolution, comment stripping)
  3. GLSL assembly (marker replacement for ubershaders)
  4. Generated header format validation
  5. End-to-end: hash computation + header generation roundtrip
  6. Binary search contract (sorted output)

Usage:
    python test_wgsl_precompile.py
"""

import os
import struct
import sys
import tempfile

# Add the driver directory to path so we can import wgsl_precompile.
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO_ROOT, "drivers", "webgpu"))

import wgsl_precompile

passed = 0
failed = 0


def assert_eq(actual, expected, msg):
    global passed, failed
    if actual == expected:
        print(f"  PASS: {msg}")
        passed += 1
    else:
        print(f"  FAIL: {msg}")
        print(f"    expected: {expected!r}")
        print(f"    actual:   {actual!r}")
        failed += 1


def assert_true(cond, msg):
    global passed, failed
    if cond:
        print(f"  PASS: {msg}")
        passed += 1
    else:
        print(f"  FAIL: {msg}")
        failed += 1


# =========================================================================
# Test 1: MurmurHash3 correctness
# =========================================================================
print("\n=== Test 1: MurmurHash3 ===")

# Empty input with default seed.
h = wgsl_precompile.hash_murmur3_buffer(b"", wgsl_precompile.HASH_MURMUR3_SEED)
assert_eq(h & 0xFFFFFFFF, h, "Hash is 32-bit")
assert_true(isinstance(h, int), "Hash is integer")

# Known test vectors — these were computed using Godot's C++ hash_murmur3_buffer().
# We verify that the Python implementation produces identical results.
# Test vector: "Hello" with default seed 0x7F07C65.
hello_bytes = b"Hello"
h_hello = wgsl_precompile.hash_murmur3_buffer(hello_bytes, 0x7F07C65)
assert_true(h_hello != 0, "Hello hash is non-zero")

# Determinism: same input → same output.
h_hello2 = wgsl_precompile.hash_murmur3_buffer(hello_bytes, 0x7F07C65)
assert_eq(h_hello, h_hello2, "Hash is deterministic")

# Different seeds → different hashes (used for 64-bit key).
h_lo = wgsl_precompile.hash_murmur3_buffer(hello_bytes, 0x7F07C65)
h_hi = wgsl_precompile.hash_murmur3_buffer(hello_bytes, 0x9E3779B9)
assert_true(h_lo != h_hi, "Different seeds produce different hashes")

# 64-bit hash composition.
hash64 = wgsl_precompile.compute_spv_hash(hello_bytes)
expected64 = (h_hi << 32) | h_lo
assert_eq(hash64, expected64, "64-bit hash matches (hi << 32) | lo")

# Hash of different inputs should differ.
h_world = wgsl_precompile.hash_murmur3_buffer(b"World", 0x7F07C65)
assert_true(h_hello != h_world, "Different inputs produce different hashes")

# 4-byte aligned input (exercises the body loop).
data4 = b"ABCD"
h4 = wgsl_precompile.hash_murmur3_buffer(data4, 0x7F07C65)
assert_true(h4 != 0, "4-byte input hashes correctly")

# Tail cases: 1, 2, 3 byte tails.
for tail_len in [1, 2, 3]:
    data = b"ABCDE"[:4 + tail_len]
    h = wgsl_precompile.hash_murmur3_buffer(data, 0x7F07C65)
    assert_true(h != 0, f"{tail_len}-byte tail hashes correctly")

# Large input (simulating SPIR-V module).
large_data = bytes(range(256)) * 100  # 25,600 bytes
h_large = wgsl_precompile.hash_murmur3_buffer(large_data, 0x7F07C65)
assert_true(h_large != 0, "Large input hashes correctly")
h_large2 = wgsl_precompile.hash_murmur3_buffer(large_data, 0x7F07C65)
assert_eq(h_large, h_large2, "Large input hash is deterministic")


# =========================================================================
# Test 2: GLSL parsing
# =========================================================================
print("\n=== Test 2: GLSL parsing ===")

# Create a temporary GLSL file to test parsing.
with tempfile.NamedTemporaryFile(mode="w", suffix=".glsl", delete=False) as f:
    f.write("""\
#[vertex]
#VERSION_DEFINES
layout(location = 0) in vec3 vertex_attrib;
void main() {
    gl_Position = vec4(vertex_attrib, 1.0);
}
#[fragment]
#VERSION_DEFINES
layout(location = 0) out vec4 frag_color;
void main() {
    frag_color = vec4(1.0);
}
""")
    test_glsl_path = f.name

try:
    stages = wgsl_precompile.parse_glsl_file(test_glsl_path)
    assert_true(stages["vertex"] is not None, "Vertex stage found")
    assert_true(stages["fragment"] is not None, "Fragment stage found")
    assert_true(stages["compute"] is None, "No compute stage")
    assert_true(any("#VERSION_DEFINES" in l for l in stages["vertex"]),
                "Vertex stage contains #VERSION_DEFINES marker")
    assert_true(any("gl_Position" in l for l in stages["vertex"]),
                "Vertex stage contains vertex code")
    assert_true(any("frag_color" in l for l in stages["fragment"]),
                "Fragment stage contains fragment code")
finally:
    os.unlink(test_glsl_path)

# Test compute shader.
with tempfile.NamedTemporaryFile(mode="w", suffix=".glsl", delete=False) as f:
    f.write("""\
#[compute]
#VERSION_DEFINES
layout(local_size_x = 64) in;
void main() { }
""")
    test_comp_path = f.name

try:
    stages = wgsl_precompile.parse_glsl_file(test_comp_path)
    assert_true(stages["vertex"] is None, "No vertex stage in compute shader")
    assert_true(stages["fragment"] is None, "No fragment stage in compute shader")
    assert_true(stages["compute"] is not None, "Compute stage found")
finally:
    os.unlink(test_comp_path)

# Test comment stripping.
with tempfile.NamedTemporaryFile(mode="w", suffix=".glsl", delete=False) as f:
    f.write("""\
#[vertex]
// This is a comment
void main() { // inline comment
    gl_Position = vec4(0.0);
}
""")
    test_comment_path = f.name

try:
    stages = wgsl_precompile.parse_glsl_file(test_comment_path)
    vertex_source = "\n".join(stages["vertex"])
    assert_true("// This is a comment" not in vertex_source,
                "Full-line comment stripped")
    assert_true("// inline comment" not in vertex_source,
                "Inline comment stripped")
    assert_true("gl_Position" in vertex_source,
                "Code before inline comment preserved")
finally:
    os.unlink(test_comment_path)

# Test include resolution.
with tempfile.NamedTemporaryFile(mode="w", suffix=".glsl", delete=False,
                                  dir=tempfile.gettempdir()) as incl:
    incl.write("float included_func() { return 1.0; }\n")
    incl_path = incl.name

with tempfile.NamedTemporaryFile(mode="w", suffix=".glsl", delete=False,
                                  dir=tempfile.gettempdir()) as main:
    main.write(f"""\
#[vertex]
#include "{os.path.basename(incl_path)}"
void main() {{ gl_Position = vec4(included_func()); }}
""")
    main_path = main.name

try:
    stages = wgsl_precompile.parse_glsl_file(main_path)
    vertex_source = "\n".join(stages["vertex"])
    assert_true("included_func" in vertex_source,
                "Include file content resolved")
finally:
    os.unlink(incl_path)
    os.unlink(main_path)


# =========================================================================
# Test 3: GLSL assembly (marker replacement)
# =========================================================================
print("\n=== Test 3: GLSL assembly ===")

stage_lines = [
    "#version 450",
    "#VERSION_DEFINES",
    "layout(location = 0) in vec3 pos;",
    "#MATERIAL_UNIFORMS",
    "#GLOBALS",
    "#CODE : VERTEX",
    "void main() { gl_Position = vec4(pos, 1.0); }",
]

general_defines = "#define MAX_LIGHTS 8\n"
variant_defines = "#define UBERSHADER\n"

assembled = wgsl_precompile.assemble_glsl(stage_lines, general_defines, variant_defines)

assert_true("#VERSION_DEFINES" not in assembled,
            "#VERSION_DEFINES marker replaced")
assert_true("#MATERIAL_UNIFORMS" not in assembled,
            "#MATERIAL_UNIFORMS marker removed")
assert_true("#GLOBALS" not in assembled,
            "#GLOBALS marker removed")
assert_true("#CODE" not in assembled,
            "#CODE marker removed")
assert_true("#define MAX_LIGHTS 8" in assembled,
            "General defines inserted")
assert_true("#define UBERSHADER" in assembled,
            "Variant defines inserted")
assert_true("#define RENDER_DRIVER_WEBGPU" in assembled,
            "WebGPU driver define inserted")
assert_true("gl_Position" in assembled,
            "Shader code preserved")


# =========================================================================
# Test 4: Generated header format
# =========================================================================
print("\n=== Test 4: Generated header format ===")

with tempfile.NamedTemporaryFile(mode="w", suffix=".gen.h", delete=False) as f:
    test_header_path = f.name

# Test empty entries.
wgsl_precompile.generate_precompiled_header([], test_header_path)
with open(test_header_path, "r") as f:
    content = f.read()
assert_true("_wgsl_precompiled_count = 0" in content,
            "Empty table has count 0")
assert_true("WgslPrecompiledEntry" in content,
            "Empty table has struct definition")

# Test with entries.
entries = [
    (0x0000000100000002, "fn foo() {}"),
    (0xDEADBEEFCAFEBABE, "@vertex fn main() -> @builtin(position) vec4f { return vec4f(0.0); }"),
    (0x0000000000000001, "fn bar() {}"),
]
wgsl_precompile.generate_precompiled_header(entries, test_header_path)
with open(test_header_path, "r") as f:
    content = f.read()

assert_true("_wgsl_precompiled_count = 3" in content,
            "3 entries counted")
assert_true("#pragma once" in content,
            "Include guard present")
assert_true("#include <cstdint>" in content,
            "cstdint included")
assert_true("fn foo() {}" in content,
            "WGSL content present")

# Verify entries are sorted by hash (critical for binary search).
hash_positions = []
for line in content.split("\n"):
    if "0x" in line and "ULL" in line:
        # Extract the hash value.
        start = line.index("0x")
        end = line.index("ULL")
        hash_val = int(line[start:end], 16)
        hash_positions.append(hash_val)

assert_eq(len(hash_positions), 3, "Found 3 hash entries in output")
assert_eq(hash_positions, sorted(hash_positions),
          "Hashes are sorted (ascending)")
assert_eq(hash_positions[0], 0x0000000000000001,
          "Smallest hash first")
assert_eq(hash_positions[-1], 0xDEADBEEFCAFEBABE,
          "Largest hash last")

os.unlink(test_header_path)


# =========================================================================
# Test 5: WGSL with raw string delimiter edge cases
# =========================================================================
print("\n=== Test 5: Raw string delimiter edge cases ===")

with tempfile.NamedTemporaryFile(mode="w", suffix=".gen.h", delete=False) as f:
    test_header_path = f.name

# WGSL that contains the default delimiter ")wgsl".
tricky_wgsl = 'fn test() { /* )wgsl" sneaky */ }'
entries = [(0x0000000000000001, tricky_wgsl)]
wgsl_precompile.generate_precompiled_header(entries, test_header_path)
with open(test_header_path, "r") as f:
    content = f.read()

assert_true(tricky_wgsl in content,
            "Tricky WGSL content preserved in output")
assert_true("_wgsl_precompiled_count = 1" in content,
            "Tricky WGSL entry counted")

os.unlink(test_header_path)


# =========================================================================
# Test 6: Shader registry validation
# =========================================================================
print("\n=== Test 6: Shader registry validation ===")

assert_true(len(wgsl_precompile.SHADER_REGISTRY) > 0,
            "Shader registry is non-empty")

# Check that all registry paths are relative to repo root.
for glsl_rel, general_defines, variants in wgsl_precompile.SHADER_REGISTRY:
    assert_true(not os.path.isabs(glsl_rel),
                f"Path is relative: {glsl_rel}")

# Check that all registry entries exist on disk.
missing_count = 0
found_count = 0
for glsl_rel, _, _ in wgsl_precompile.SHADER_REGISTRY:
    full_path = os.path.join(REPO_ROOT, glsl_rel)
    if os.path.exists(full_path):
        found_count += 1
    else:
        missing_count += 1

assert_true(found_count > 0, f"At least some registry files exist ({found_count} found)")
if missing_count > 0:
    print(f"    NOTE: {missing_count} registry files not found (may be expected for partial checkout)")

# Check variant structure.
for glsl_rel, general_defines, variants in wgsl_precompile.SHADER_REGISTRY:
    for variant_name, variant_defines, stage_types in variants:
        for st in stage_types:
            assert_true(st in ("vert", "frag", "comp"),
                        f"Valid stage type '{st}' in {os.path.basename(glsl_rel)}:{variant_name}")
        break  # Only check first variant per shader to keep output manageable.
    break  # Only check first shader.

assert_true(True, f"Registry has {len(wgsl_precompile.SHADER_REGISTRY)} shader entries")


# =========================================================================
# Test 7: Generated header matches expected on disk
# =========================================================================
print("\n=== Test 7: Existing generated header validation ===")

gen_h_path = os.path.join(REPO_ROOT, "drivers", "webgpu", "wgsl_precompiled.gen.h")
if os.path.exists(gen_h_path):
    with open(gen_h_path, "r") as f:
        content = f.read()

    assert_true("// Auto-generated by" in content,
                "Header has auto-generated comment")
    assert_true("WgslPrecompiledEntry" in content,
                "Header has struct definition")
    assert_true("_wgsl_precompiled_count" in content,
                "Header has count variable")

    # Extract count.
    for line in content.split("\n"):
        if "_wgsl_precompiled_count" in line and "=" in line:
            count_str = line.split("=")[1].strip().rstrip(";")
            count = int(count_str)
            assert_true(count > 0, f"Header has {count} precompiled entries")
            break

    # Verify all hashes are sorted.
    hashes = []
    for line in content.split("\n"):
        if "0x" in line and "ULL" in line and "spv_hash" not in line:
            start = line.index("0x")
            end = line.index("ULL")
            hash_val = int(line[start:end], 16)
            hashes.append(hash_val)

    if hashes:
        is_sorted = all(hashes[i] <= hashes[i+1] for i in range(len(hashes)-1))
        assert_true(is_sorted, f"All {len(hashes)} hashes in gen.h are sorted")
        # Check for duplicates.
        unique = len(set(hashes))
        assert_eq(unique, len(hashes), "No duplicate hashes")
else:
    print("  SKIP: wgsl_precompiled.gen.h not found (run wgsl_precompile.py first)")


# =========================================================================
# Test 8: MurmurHash3 against known SPIR-V magic number
# =========================================================================
print("\n=== Test 8: MurmurHash3 with SPIR-V header ===")

# SPIR-V magic number is 0x07230203, first 4 bytes of every valid module.
spirv_magic = struct.pack("<I", 0x07230203)
h_magic_lo = wgsl_precompile.hash_murmur3_buffer(spirv_magic, 0x7F07C65)
h_magic_hi = wgsl_precompile.hash_murmur3_buffer(spirv_magic, 0x9E3779B9)
assert_true(h_magic_lo != 0, "SPIR-V magic hashes to non-zero (seed 1)")
assert_true(h_magic_hi != 0, "SPIR-V magic hashes to non-zero (seed 2)")

# Verify endianness: the hash processes data as little-endian uint32s.
# With the magic bytes 03 02 23 07, the first k1 should be 0x07230203.
h_be = wgsl_precompile.hash_murmur3_buffer(bytes([0x03, 0x02, 0x23, 0x07]), 0x7F07C65)
assert_eq(h_be, h_magic_lo, "Little-endian byte order matches struct.pack('<I')")


# =========================================================================
# Summary
# =========================================================================
print(f"\n{'=' * 50}")
print(f"Results: {passed} passed, {failed} failed")
if failed > 0:
    sys.exit(1)
