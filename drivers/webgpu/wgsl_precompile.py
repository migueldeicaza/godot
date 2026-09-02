"""
Build-time SPIR-V → WGSL precompilation for WebGPU ubershaders.

During `scons platform=web webgpu=yes`, this module:
1. Parses each .glsl shader file (same include resolution as glsl_builders.py)
2. Enumerates all enabled variant defines for each shader class
3. Assembles complete GLSL (raw source + defines + driver defines)
4. Compiles each variant via glslangValidator → SPIR-V
5. Converts each SPIR-V via Tint (native CLI tool) → WGSL
6. Computes MurmurHash3 hash of each SPIR-V (same algorithm as C++ runtime)
7. Generates wgsl_precompiled.gen.h with an embedded hash→WGSL lookup table

At runtime, _spv_to_wgsl_cached() checks the precompiled table before
falling back to Tint (compiled into the engine). This eliminates runtime
SPIR-V → WGSL conversion for ubershaders on every page load.
"""

import json
import os
import struct
import subprocess
import sys
import tempfile

# ---------------------------------------------------------------------------
# MurmurHash3 (x86_32) — matches Godot's hash_murmur3_buffer()
# ---------------------------------------------------------------------------

HASH_MURMUR3_SEED = 0x7F07C65


def _rotl32(x, r):
    return ((x << r) | (x >> (32 - r))) & 0xFFFFFFFF


def _fmix32(h):
    h &= 0xFFFFFFFF
    h ^= h >> 16
    h = (h * 0x85EBCA6B) & 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 0xC2B2AE35) & 0xFFFFFFFF
    h ^= h >> 16
    return h


def hash_murmur3_buffer(data, seed=HASH_MURMUR3_SEED):
    """MurmurHash3_x86_32 matching Godot's implementation."""
    length = len(data)
    nblocks = length // 4
    h1 = seed & 0xFFFFFFFF
    c1 = 0xCC9E2D51
    c2 = 0x1B873593

    # Body — process 4-byte blocks.
    for i in range(nblocks):
        k1 = struct.unpack_from("<I", data, i * 4)[0]
        k1 = (k1 * c1) & 0xFFFFFFFF
        k1 = _rotl32(k1, 15)
        k1 = (k1 * c2) & 0xFFFFFFFF
        h1 ^= k1
        h1 = _rotl32(h1, 13)
        h1 = (h1 * 5 + 0xE6546B64) & 0xFFFFFFFF

    # Tail.
    tail_offset = nblocks * 4
    k1 = 0
    tail_len = length & 3
    if tail_len >= 3:
        k1 ^= data[tail_offset + 2] << 16
    if tail_len >= 2:
        k1 ^= data[tail_offset + 1] << 8
    if tail_len >= 1:
        k1 ^= data[tail_offset]
        k1 = (k1 * c1) & 0xFFFFFFFF
        k1 = _rotl32(k1, 15)
        k1 = (k1 * c2) & 0xFFFFFFFF
        h1 ^= k1

    # Finalize.
    h1 ^= length
    return _fmix32(h1)


def compute_spv_hash(spv_bytes):
    """Compute the 64-bit SPIR-V hash (same as C++ _spv_to_wgsl_cached)."""
    hash_lo = hash_murmur3_buffer(spv_bytes, HASH_MURMUR3_SEED)
    hash_hi = hash_murmur3_buffer(spv_bytes, 0x9E3779B9)
    return (hash_hi << 32) | hash_lo


# ---------------------------------------------------------------------------
# GLSL Processing — reuses glsl_builders.py include resolution logic
# ---------------------------------------------------------------------------


def parse_glsl_file(filepath):
    """Parse a .glsl file into per-stage source lists, resolving #include.

    Returns dict: { 'vertex': [lines], 'fragment': [lines], 'compute': [lines] }
    Stage source is None if not present.
    """
    stages = {"vertex": [], "fragment": [], "compute": []}
    _parse_glsl_recursive(filepath, stages, set(), "")
    # Convert empty lists to None.
    for key in stages:
        if not stages[key]:
            stages[key] = None
    return stages


def _parse_glsl_recursive(filepath, stages, included_files, current_stage):
    """Recursively parse a GLSL file, resolving includes and splitting stages."""
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            lines = f.readlines()
    except FileNotFoundError:
        print(f"WARNING: Could not find GLSL include: {filepath}", file=sys.stderr)
        return current_stage

    for line in lines:
        stripped = line.rstrip("\r\n")

        # Strip C++ style comments (same as glsl_builders.py).
        comment_idx = stripped.find("//")
        if comment_idx != -1:
            stripped = stripped[:comment_idx]

        # Stage markers.
        if "#[vertex]" in stripped:
            current_stage = "vertex"
            continue
        if "#[fragment]" in stripped:
            current_stage = "fragment"
            continue
        if "#[compute]" in stripped:
            current_stage = "compute"
            continue

        if not current_stage:
            continue  # Before any stage marker.

        # Handle #include directives.
        if "#include " in stripped:
            include_name = stripped.replace("#include ", "").strip().strip('"')
            if include_name.startswith("thirdparty/"):
                include_path = os.path.relpath(include_name)
            else:
                include_path = os.path.normpath(
                    os.path.join(os.path.dirname(filepath), include_name)
                )
            stage_key = current_stage + ":" + include_path
            if stage_key not in included_files:
                included_files.add(stage_key)
                current_stage = _parse_glsl_recursive(
                    include_path, stages, included_files, current_stage
                )
            continue

        # Regular line — add to current stage.
        if current_stage in stages:
            stages[current_stage].append(stripped)

    return current_stage


def assemble_glsl(stage_lines, general_defines, variant_defines):
    """Assemble a complete GLSL source for glslangValidator.

    Replaces Godot-specific markers:
    - #VERSION_DEFINES → general_defines + variant_defines + driver defines
    - #MATERIAL_UNIFORMS → empty (ubershader has no material uniforms)
    - #GLOBALS → empty (ubershader has no custom globals)
    - #CODE : ... → empty (ubershader has no custom code)
    """
    driver_defines = (
        "#define RENDER_DRIVER_WEBGPU\n"
        "#define samplerExternalOES sampler2D\n"
        "#define textureExternalOES texture2D\n"
    )

    defines_block = general_defines + variant_defines + driver_defines

    result = []
    for line in stage_lines:
        if line.strip().startswith("#VERSION_DEFINES"):
            result.append(defines_block)
        elif line.strip().startswith("#MATERIAL_UNIFORMS"):
            pass  # Empty for ubershaders.
        elif line.strip().startswith("#GLOBALS"):
            pass  # Empty for ubershaders.
        elif line.strip().startswith("#CODE"):
            pass  # Empty for ubershaders.
        else:
            result.append(line)
    return "\n".join(result)


# ---------------------------------------------------------------------------
# Shader Variant Registry
# ---------------------------------------------------------------------------

# Default general_defines for forward_mobile web renderer.
# Values match the defaults for a standard Godot web export.
GENERAL_DEFINES_FORWARD_MOBILE = (
    "\n#define MAX_ROUGHNESS_LOD 5.0\n"
    "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS 8\n"
    "\n#define MAX_LIGHTMAP_TEXTURES 8\n"
    "\n#define MAX_LIGHTMAPS 8\n"
    "\n#define MATERIAL_UNIFORM_SET 3\n"
)

GENERAL_DEFINES_CANVAS = (
    "#define MAX_LIGHTS 256\n"
    "\n#define SAMPLERS_BINDING_FIRST_INDEX 10\n"
)

# Particles shader defines (particles_storage.cpp:56-61).
GENERAL_DEFINES_PARTICLES = (
    "#define SAMPLERS_BINDING_FIRST_INDEX 3\n"
)

# Sky shader defines (sky.cpp:714-730).
GENERAL_DEFINES_SKY = (
    "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS 4\n"
    "\n#define SAMPLERS_BINDING_FIRST_INDEX 4\n"
)

# Volumetric fog shader defines (fog.cpp:218-226).
GENERAL_DEFINES_FOG = (
    "#define SAMPLERS_BINDING_FIRST_INDEX 3\n"
)

# Volumetric fog process shader defines (fog.cpp:303-323).
GENERAL_DEFINES_FOG_PROCESS = (
    "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS 8\n"
    "\n#define MAX_SKY_LOD 5.0\n"
)

# Voxel GI defines (gi.cpp:3445-3457).
GENERAL_DEFINES_VOXEL_GI = (
    "\n#define MAX_LIGHTS 32\n"
)

# SDFGI defines (gi.cpp).
GENERAL_DEFINES_SDFGI_PREPROCESS = (
    "\n#define OCCLUSION_SIZE 4\n"
)
GENERAL_DEFINES_SDFGI_DIRECT_LIGHT = (
    "\n#define OCT_SIZE 5\n"
)
GENERAL_DEFINES_SDFGI_INTEGRATE = (
    "\n#define OCT_SIZE 5\n"
    "\n#define SH_SIZE 16\n"
)
GENERAL_DEFINES_SDFGI_DEBUG = (
    "\n#define OCT_SIZE 5\n"
)

# Empty general defines for effect shaders that need no special defines.
GENERAL_DEFINES_NONE = ""

# Stage type constants matching glslangValidator -S flags.
VERT = "vert"
FRAG = "frag"
COMP = "comp"

# fmt: off
# Each entry: (glsl_path, general_defines, [(variant_name, variant_defines, [stages])])
# Paths are relative to the repo root.
SHADER_REGISTRY = [
    # ── Scene Forward Mobile ────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl",
     GENERAL_DEFINES_FORWARD_MOBILE, [
        ("color_pass", "", [VERT, FRAG]),
        ("lightmap_color", "\n#define USE_LIGHTMAP\n", [VERT, FRAG]),
        ("shadow", "\n#define MODE_RENDER_DEPTH\n#define SHADOW_PASS\n", [VERT, FRAG]),
        ("shadow_dp", "\n#define MODE_RENDER_DEPTH\n#define MODE_DUAL_PARABOLOID\n#define SHADOW_PASS\n", [VERT, FRAG]),
        ("depth_material", "\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_MATERIAL\n", [VERT, FRAG]),
        ("uber_color_pass", "\n#define UBERSHADER\n", [VERT, FRAG]),
        ("uber_lightmap", "\n#define UBERSHADER\n\n#define USE_LIGHTMAP\n", [VERT, FRAG]),
        ("uber_shadow", "\n#define UBERSHADER\n\n#define MODE_RENDER_DEPTH\n#define SHADOW_PASS\n", [VERT, FRAG]),
        ("uber_shadow_dp", "\n#define UBERSHADER\n\n#define MODE_RENDER_DEPTH\n#define MODE_DUAL_PARABOLOID\n#define SHADOW_PASS\n", [VERT, FRAG]),
        ("uber_depth_material", "\n#define UBERSHADER\n\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_MATERIAL\n", [VERT, FRAG]),
    ]),

    # ── Canvas ──────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/canvas.glsl",
     GENERAL_DEFINES_CANVAS, [
        ("quad", "", [VERT, FRAG]),
        ("ninepatch", "#define USE_NINEPATCH\n", [VERT, FRAG]),
        ("primitive", "#define USE_PRIMITIVE\n", [VERT, FRAG]),
        ("primitive_points", "#define USE_PRIMITIVE\n#define USE_POINT_SIZE\n", [VERT, FRAG]),
        ("attributes", "#define USE_ATTRIBUTES\n", [VERT, FRAG]),
        ("attributes_points", "#define USE_ATTRIBUTES\n#define USE_POINT_SIZE\n", [VERT, FRAG]),
    ]),

    # ── Canvas Occlusion ────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/canvas_occlusion.glsl",
     GENERAL_DEFINES_NONE, [
        ("default", "", [VERT, FRAG]),
    ]),

    # ── Canvas SDF ──────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/canvas_sdf.glsl",
     GENERAL_DEFINES_NONE, [
        ("default", "", [COMP]),
    ]),

    # ── Blit ────────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/blit.glsl",
     GENERAL_DEFINES_NONE, [
        ("default", "", [VERT, FRAG]),
    ]),

    # ── Skeleton ────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/skeleton.glsl",
     GENERAL_DEFINES_NONE, [
        ("default", "", [COMP]),
    ]),

    # ── Particles (particles_storage.cpp:56) ────────────────────────
    ("servers/rendering/renderer_rd/shaders/particles.glsl",
     GENERAL_DEFINES_PARTICLES, [
        ("default", "", [COMP]),
    ]),

    # ── Particles Copy ──────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/particles_copy.glsl",
     GENERAL_DEFINES_NONE, [
        ("default", "", [COMP]),
    ]),

    # ── Copy Effects ────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/blur_raster.glsl",
     GENERAL_DEFINES_NONE, [
        ("mipmap", "\n#define MODE_MIPMAP\n", [VERT, FRAG]),
        ("gaussian_blur", "\n#define MODE_GAUSSIAN_BLUR\n", [VERT, FRAG]),
        ("glow_gather", "\n#define MODE_GLOW_GATHER\n", [VERT, FRAG]),
        ("glow_downsample", "\n#define MODE_GLOW_DOWNSAMPLE\n", [VERT, FRAG]),
        ("glow_upsample", "\n#define MODE_GLOW_UPSAMPLE\n", [VERT, FRAG]),
        ("copy", "\n#define MODE_COPY\n", [VERT, FRAG]),
        ("set_color", "\n#define MODE_SET_COLOR\n", [VERT, FRAG]),
    ]),

    ("servers/rendering/renderer_rd/shaders/effects/copy.glsl",
     GENERAL_DEFINES_NONE, [
        ("gaussian_blur", "\n#define MODE_GAUSSIAN_BLUR\n", [COMP]),
        ("gaussian_blur_8bit", "\n#define MODE_GAUSSIAN_BLUR\n#define DST_IMAGE_8BIT\n", [COMP]),
        ("gaussian_glow", "\n#define MODE_GAUSSIAN_BLUR\n#define MODE_GLOW\n", [COMP]),
        ("gaussian_glow_auto_exposure", "\n#define MODE_GAUSSIAN_BLUR\n#define MODE_GLOW\n#define GLOW_USE_AUTO_EXPOSURE\n", [COMP]),
        ("simple_copy", "\n#define MODE_SIMPLE_COPY\n", [COMP]),
        ("simple_copy_8bit", "\n#define MODE_SIMPLE_COPY\n#define DST_IMAGE_8BIT\n", [COMP]),
        ("simple_copy_depth", "\n#define MODE_SIMPLE_COPY_DEPTH\n", [COMP]),
        ("set_color", "\n#define MODE_SET_COLOR\n", [COMP]),
        ("set_color_8bit", "\n#define MODE_SET_COLOR\n#define DST_IMAGE_8BIT\n", [COMP]),
        ("mipmap", "\n#define MODE_MIPMAP\n", [COMP]),
        ("linearize_depth", "\n#define MODE_LINEARIZE_DEPTH_COPY\n", [COMP]),
        ("octmap_panorama", "\n#define MODE_OCTMAP_TO_PANORAMA\n", [COMP]),
        ("octmap_array_panorama", "\n#define MODE_OCTMAP_ARRAY_TO_PANORAMA\n", [COMP]),
    ]),

    ("servers/rendering/renderer_rd/shaders/effects/copy_to_fb.glsl",
     GENERAL_DEFINES_NONE, [
        ("copy", "\n", [VERT, FRAG]),
        ("panorama_to_dp", "\n#define MODE_PANORAMA_TO_DP\n", [VERT, FRAG]),
        ("two_sources", "\n#define MODE_TWO_SOURCES\n", [VERT, FRAG]),
        ("set_color", "\n#define MODE_SET_COLOR\n", [VERT, FRAG]),
    ]),

    ("servers/rendering/renderer_rd/shaders/effects/cube_to_dp.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [VERT, FRAG])]),

    ("servers/rendering/renderer_rd/shaders/effects/cube_to_octmap.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),

    # ── Tone Mapper ─────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/tonemap_mobile.glsl",
     GENERAL_DEFINES_NONE, [
        ("normal", "\n", [VERT, FRAG]),
        ("1d_lut", "\n#define USE_1D_LUT\n", [VERT, FRAG]),
        ("subpass", "\n#define SUBPASS\n", [VERT, FRAG]),
        ("subpass_1d_lut", "\n#define SUBPASS\n#define USE_1D_LUT\n", [VERT, FRAG]),
    ]),

    ("servers/rendering/renderer_rd/shaders/effects/tonemap.glsl",
     GENERAL_DEFINES_NONE, [
        ("normal", "\n", [VERT, FRAG]),
        ("bicubic", "\n#define USE_GLOW_FILTER_BICUBIC\n", [VERT, FRAG]),
        ("1d_lut", "\n#define USE_1D_LUT\n", [VERT, FRAG]),
        ("bicubic_1d_lut", "\n#define USE_GLOW_FILTER_BICUBIC\n#define USE_1D_LUT\n", [VERT, FRAG]),
    ]),

    # ── Luminance ───────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/luminance_reduce_raster.glsl",
     GENERAL_DEFINES_NONE, [
        ("first", "\n#define FIRST_PASS\n", [VERT, FRAG]),
        ("middle", "\n", [VERT, FRAG]),
        ("final", "\n#define FINAL_PASS\n", [VERT, FRAG]),
    ]),

    ("servers/rendering/renderer_rd/shaders/effects/luminance_reduce.glsl",
     GENERAL_DEFINES_NONE, [
        ("read", "\n#define READ_TEXTURE\n", [COMP]),
        ("reduce", "\n", [COMP]),
        ("write", "\n#define WRITE_LUMINANCE\n", [COMP]),
    ]),

    # ── Bokeh DOF ───────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/bokeh_dof.glsl",
     GENERAL_DEFINES_NONE, [
        ("gen_blur", "\n#define MODE_GEN_BLUR_SIZE\n", [COMP]),
        ("box_weight", "\n#define MODE_BOKEH_BOX\n#define OUTPUT_WEIGHT\n", [COMP]),
        ("hex_weight", "\n#define MODE_BOKEH_HEXAGONAL\n#define OUTPUT_WEIGHT\n", [COMP]),
        ("circular", "\n#define MODE_BOKEH_CIRCULAR\n#define OUTPUT_WEIGHT\n", [COMP]),
        ("composite", "\n#define MODE_COMPOSITE_BOKEH\n", [COMP]),
    ]),

    ("servers/rendering/renderer_rd/shaders/effects/bokeh_dof_raster.glsl",
     GENERAL_DEFINES_NONE, [
        ("gen_blur", "\n#define MODE_GEN_BLUR_SIZE\n", [VERT, FRAG]),
        ("box", "\n#define MODE_BOKEH_BOX\n#define OUTPUT_WEIGHT\n", [VERT, FRAG]),
        ("hex", "\n#define MODE_BOKEH_HEXAGONAL\n#define OUTPUT_WEIGHT\n", [VERT, FRAG]),
        ("circular", "\n#define MODE_BOKEH_CIRCULAR\n#define OUTPUT_WEIGHT\n", [VERT, FRAG]),
        ("composite", "\n#define MODE_COMPOSITE_BOKEH\n", [VERT, FRAG]),
    ]),

    # ── Sort ────────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/sort.glsl",
     GENERAL_DEFINES_NONE, [
        ("block", "\n#define MODE_SORT_BLOCK\n", [COMP]),
        ("step", "\n#define MODE_SORT_STEP\n", [COMP]),
        ("inner", "\n#define MODE_SORT_INNER\n", [COMP]),
    ]),

    # ── Resolve ─────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/resolve.glsl",
     GENERAL_DEFINES_NONE, [
        ("gi", "\n#define MODE_RESOLVE_GI\n", [COMP]),
        ("gi_voxel", "\n#define MODE_RESOLVE_GI\n#define VOXEL_GI_RESOLVE\n", [COMP]),
        ("depth", "\n#define MODE_RESOLVE_DEPTH\n", [COMP]),
    ]),

    ("servers/rendering/renderer_rd/shaders/effects/resolve_raster.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [VERT, FRAG])]),

    # ── Specular Merge ──────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/specular_merge.glsl",
     GENERAL_DEFINES_NONE, [
        ("merge", "\n", [VERT, FRAG]),
        ("merge_ssr", "\n#define MODE_SSR\n", [VERT, FRAG]),
    ]),

    # ── SMAA ────────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/smaa_edge_detection.glsl",
     GENERAL_DEFINES_NONE, [("default", "\n", [VERT, FRAG])]),
    ("servers/rendering/renderer_rd/shaders/effects/smaa_weight_calculation.glsl",
     GENERAL_DEFINES_NONE, [("default", "\n", [VERT, FRAG])]),
    ("servers/rendering/renderer_rd/shaders/effects/smaa_blending.glsl",
     GENERAL_DEFINES_NONE, [("default", "\n", [VERT, FRAG])]),

    # ── TAA ─────────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl",
     GENERAL_DEFINES_NONE, [("default", "\n#define MODE_TAA_RESOLVE", [COMP])]),

    # ── VRS ─────────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/vrs.glsl",
     GENERAL_DEFINES_NONE, [
        ("default", "\n", [VERT, FRAG]),
        ("rg", "\n#define SPLIT_RG\n", [VERT, FRAG]),
    ]),

    # ── Motion Vectors ──────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/motion_vectors.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [VERT, FRAG])]),
    ("servers/rendering/renderer_rd/shaders/effects/motion_vectors_store.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),

    # ── Shadow Frustum ──────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/shadow_frustum.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [VERT, FRAG])]),

    # ── Roughness Limiter ───────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/roughness_limiter.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),

    # ── FSR ─────────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/fsr_upscale.glsl",
     GENERAL_DEFINES_NONE, [
        ("fallback", "\n#define MODE_FSR_UPSCALE_FALLBACK\n", [COMP]),
    ]),

    # ── Octahedral Map Effects ──────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/octmap_downsampler.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/octmap_downsampler_raster.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [VERT, FRAG])]),
    ("servers/rendering/renderer_rd/shaders/effects/octmap_filter.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/octmap_filter_raster.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [VERT, FRAG])]),
    ("servers/rendering/renderer_rd/shaders/effects/octmap_roughness.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/octmap_roughness_raster.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [VERT, FRAG])]),

    # ── SSR ─────────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/screen_space_reflection.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_downsample.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_filter.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_hiz.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_resolve.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),

    # ── SSAO ────────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/ssao.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/ssao_blur.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/ssao_importance_map.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/ssao_interleave.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),

    # ── SSIL ────────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/ssil.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/ssil_blur.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/ssil_importance_map.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/effects/ssil_interleave.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),

    # ── Subsurface Scattering ───────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/subsurface_scattering.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),

    # ── SS Effects Downsample ───────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/effects/ss_effects_downsample.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),

    # ── Cluster ─────────────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/cluster_debug.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [VERT, FRAG])]),
    ("servers/rendering/renderer_rd/shaders/cluster_render.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [VERT, FRAG])]),
    ("servers/rendering/renderer_rd/shaders/cluster_store.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),

    # ── Sky (sky.cpp:714) ──────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/environment/sky.glsl",
     GENERAL_DEFINES_SKY, [("default", "", [VERT, FRAG])]),

    # ── GI (gi.cpp:3573) ───────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/environment/gi.glsl",
     "\n#define SDFGI_OCT_SIZE 5\n", [("default", "", [COMP])]),

    # ── GI Probe Write ──────────────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/giprobe_write.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),

    # ── Volumetric Fog (fog.cpp:218) ────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/environment/volumetric_fog.glsl",
     GENERAL_DEFINES_FOG, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/environment/volumetric_fog_process.glsl",
     GENERAL_DEFINES_FOG_PROCESS, [("default", "", [COMP])]),

    # ── Voxel GI (gi.cpp:3445) ─────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/environment/voxel_gi.glsl",
     GENERAL_DEFINES_VOXEL_GI, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/environment/voxel_gi_debug.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [VERT, FRAG])]),
    ("servers/rendering/renderer_rd/shaders/environment/voxel_gi_sdf.glsl",
     GENERAL_DEFINES_NONE, [("default", "", [COMP])]),

    # ── SDFGI (gi.cpp:3502+) ───────────────────────────────────────
    ("servers/rendering/renderer_rd/shaders/environment/sdfgi_debug.glsl",
     GENERAL_DEFINES_SDFGI_DEBUG, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/environment/sdfgi_debug_probes.glsl",
     GENERAL_DEFINES_SDFGI_DEBUG, [("default", "", [VERT, FRAG])]),
    ("servers/rendering/renderer_rd/shaders/environment/sdfgi_direct_light.glsl",
     GENERAL_DEFINES_SDFGI_DIRECT_LIGHT, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/environment/sdfgi_integrate.glsl",
     GENERAL_DEFINES_SDFGI_INTEGRATE, [("default", "", [COMP])]),
    ("servers/rendering/renderer_rd/shaders/environment/sdfgi_preprocess.glsl",
     GENERAL_DEFINES_SDFGI_PREPROCESS, [("default", "", [COMP])]),
]
# fmt: on


# ---------------------------------------------------------------------------
# Build Pipeline
# ---------------------------------------------------------------------------


def compile_glsl_to_spirv(glsl_source, stage, glslang_path="glslangValidator"):
    """Compile GLSL source to SPIR-V using glslangValidator.

    Args:
        glsl_source: Complete GLSL source string.
        stage: One of 'vert', 'frag', 'comp'.
        glslang_path: Path to glslangValidator binary.

    Returns:
        SPIR-V bytes on success, None on failure.
    """
    with tempfile.NamedTemporaryFile(suffix=f".{stage}", mode="w", delete=False) as f:
        f.write(glsl_source)
        glsl_path = f.name

    spv_path = glsl_path + ".spv"

    try:
        result = subprocess.run(
            [
                glslang_path,
                "-V",
                "-S", stage,
                "-o", spv_path,
                glsl_path,
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )

        if result.returncode != 0:
            return None, result.stderr

        with open(spv_path, "rb") as f:
            spv_bytes = f.read()

        return spv_bytes, None

    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        return None, str(e)
    finally:
        try:
            os.unlink(glsl_path)
        except OSError:
            pass
        try:
            os.unlink(spv_path)
        except OSError:
            pass


def convert_spirv_batch(spv_files, tint_cli_path):
    """Convert multiple SPIR-V files to WGSL using the Tint CLI tool.

    Args:
        spv_files: List of (key, spv_path) tuples.
        tint_cli_path: Path to tint_convert_cli binary.

    Returns:
        Dict mapping key to WGSL string (or None on failure).
    """
    if not spv_files:
        return {}

    paths = [p for _, p in spv_files]
    try:
        result = subprocess.run(
            [tint_cli_path, "--batch"] + paths,
            capture_output=True,
            text=True,
            timeout=120,
        )
    except FileNotFoundError:
        print(f"[WGSL Precompile] ERROR: Tint CLI not found at: {tint_cli_path}", file=sys.stderr)
        sys.exit(1)

    if result.returncode != 0:
        print(f"Tint batch conversion failed: {result.stderr}", file=sys.stderr)
        return {}

    results = json.loads(result.stdout)
    output = {}
    for key, path in spv_files:
        val = results.get(path)
        if isinstance(val, dict) and "error" in val:
            print(f"  Tint error for {key}: {val['error']}", file=sys.stderr)
            output[key] = None
        else:
            output[key] = val

    return output


def generate_precompiled_header(entries, output_path):
    """Generate wgsl_precompiled.gen.h from a list of (hash, wgsl) entries.

    Args:
        entries: List of (spv_hash_u64, wgsl_string) tuples.
        output_path: Path to write the generated header.
    """
    # Sort by hash for binary search at runtime.
    entries.sort(key=lambda e: e[0])

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("// Auto-generated by wgsl_precompile.py — do not edit.\n")
        f.write("// Contains pre-compiled SPIR-V → WGSL translations for ubershaders.\n")
        f.write("// Checked at runtime before falling back to Tint.\n")
        f.write("#pragma once\n\n")
        f.write("#include <cstdint>\n\n")
        f.write("struct WgslPrecompiledEntry {\n")
        f.write("\tuint64_t spv_hash;\n")
        f.write("\tconst char *wgsl;\n")
        f.write("};\n\n")

        if not entries:
            f.write("static const WgslPrecompiledEntry _wgsl_precompiled[] = { {0, nullptr} };\n")
            f.write("static const uint32_t _wgsl_precompiled_count = 0;\n")
            return

        f.write("static const WgslPrecompiledEntry _wgsl_precompiled[] = {\n")
        for spv_hash, wgsl in entries:
            # Use raw string literal to avoid escaping issues.
            # Ensure the WGSL doesn't contain the delimiter )wgsl".
            delimiter = "wgsl"
            while f"){delimiter}\"" in wgsl:
                delimiter += "_"
            f.write(f'\t{{ 0x{spv_hash:016X}ULL, R"{delimiter}({wgsl}){delimiter}" }},\n')
        f.write("};\n\n")
        f.write(
            f"static const uint32_t _wgsl_precompiled_count = {len(entries)};\n"
        )


# ---------------------------------------------------------------------------
# Main build function
# ---------------------------------------------------------------------------


def precompile_wgsl(repo_root, output_path, glslang_path="glslangValidator"):
    """Run the full GLSL → SPIR-V → WGSL precompilation pipeline.

    Args:
        repo_root: Path to the Godot repository root.
        output_path: Path to write wgsl_precompiled.gen.h.
        glslang_path: Path to glslangValidator binary.

    Returns:
        Number of successfully precompiled entries.
    """
    # Find the tint_convert_cli binary (built by build.sh → bin/tint_convert_cli).
    tint_cli = os.path.join(repo_root, "bin", "tint_convert_cli")
    if not os.path.isfile(tint_cli):
        print("[WGSL Precompile] ERROR: bin/tint_convert_cli not found.", file=sys.stderr)
        print("[WGSL Precompile] Run: ./drivers/webgpu/tint_cli/build.sh", file=sys.stderr)
        sys.exit(1)

    total = 0
    compiled = 0
    failed_compile = 0
    failed_convert = 0
    entries = []  # (spv_hash, wgsl)

    # Collect all SPIR-V files for batch Tint conversion.
    spv_batch = []  # (key, spv_path)
    spv_data = {}  # key → spv_bytes

    print(f"[WGSL Precompile] Processing {len(SHADER_REGISTRY)} shader files...")

    for glsl_rel, general_defines, variants in SHADER_REGISTRY:
        glsl_path = os.path.join(repo_root, glsl_rel)
        if not os.path.exists(glsl_path):
            print(f"  SKIP: {glsl_rel} (file not found)")
            continue

        # Parse the GLSL file.
        stages = parse_glsl_file(glsl_path)

        for variant_name, variant_defines, stage_types in variants:
            for stage_type in stage_types:
                total += 1

                # Map stage type to parsed stage.
                stage_key_map = {VERT: "vertex", FRAG: "fragment", COMP: "compute"}
                stage_key = stage_key_map.get(stage_type)
                stage_lines = stages.get(stage_key) if stage_key else None

                if stage_lines is None:
                    print(f"  SKIP: {glsl_rel}:{variant_name}:{stage_type} (no stage)")
                    failed_compile += 1
                    continue

                # Assemble complete GLSL.
                glsl_source = assemble_glsl(stage_lines, general_defines, variant_defines)

                # Compile to SPIR-V.
                spv_bytes, error = compile_glsl_to_spirv(glsl_source, stage_type, glslang_path)
                if spv_bytes is None:
                    shader_name = os.path.basename(glsl_rel)
                    print(f"  FAIL: {shader_name}:{variant_name}:{stage_type} — {error[:120] if error else 'unknown'}")
                    failed_compile += 1
                    continue

                # Save SPIR-V for batch Tint conversion.
                key = f"{glsl_rel}:{variant_name}:{stage_type}"
                spv_path = os.path.join(tempfile.gettempdir(), f"wgsl_pre_{hash(key) & 0xFFFFFFFF:08x}.spv")
                with open(spv_path, "wb") as f:
                    f.write(spv_bytes)
                spv_batch.append((key, spv_path))
                spv_data[key] = spv_bytes

    # Batch convert all SPIR-V to WGSL.
    if spv_batch:
        print(f"[WGSL Precompile] Converting {len(spv_batch)} SPIR-V modules to WGSL...")
        wgsl_results = convert_spirv_batch(spv_batch, tint_cli)

        for key, spv_path in spv_batch:
            wgsl = wgsl_results.get(key)
            if wgsl is None:
                failed_convert += 1
            else:
                spv_hash = compute_spv_hash(spv_data[key])
                # Avoid duplicate hashes (same SPIR-V from different variants).
                if not any(h == spv_hash for h, _ in entries):
                    entries.append((spv_hash, wgsl))
                compiled += 1

            # Clean up temp SPIR-V file.
            try:
                os.unlink(spv_path)
            except OSError:
                pass

    # Generate the output header.
    generate_precompiled_header(entries, output_path)

    print(f"[WGSL Precompile] Results: {compiled} compiled, {failed_compile} glsl failures, {failed_convert} tint failures")
    print(f"[WGSL Precompile] Unique entries: {len(entries)} (from {total} total modules)")
    print(f"[WGSL Precompile] Output: {output_path}")

    return len(entries)


# ---------------------------------------------------------------------------
# scons builder entry point
# ---------------------------------------------------------------------------


def build_wgsl_precompiled(target, source, env):
    """scons builder action for WGSL precompilation.

    Builds the tint_convert_cli host tool (if needed), then runs the full
    GLSL → SPIR-V → WGSL precompilation pipeline to generate
    wgsl_precompiled.gen.h.
    """
    output = str(target[0])
    repo_root = str(env.Dir("#"))

    # Build tint_convert_cli (native host tool) via build.sh.
    build_script = os.path.join(repo_root, "drivers", "webgpu", "tint_cli", "build.sh")
    if not os.path.isfile(build_script):
        print("[WGSL Precompile] ERROR: tint_cli/build.sh not found", file=sys.stderr)
        sys.exit(1)

    print("[WGSL Precompile] Building tint_convert_cli...")
    result = subprocess.run(
        ["bash", build_script],
        cwd=repo_root,
        timeout=600,
    )
    if result.returncode != 0:
        print("[WGSL Precompile] ERROR: tint_convert_cli build failed", file=sys.stderr)
        sys.exit(1)

    glslang = env.get("GLSLANG", "glslangValidator")
    precompile_wgsl(repo_root, output, glslang)


# ---------------------------------------------------------------------------
# Standalone execution (for testing / CI)
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python wgsl_precompile.py <repo_root> [output_path] [glslang_path]")
        sys.exit(1)

    repo_root = sys.argv[1]
    output = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        repo_root, "drivers", "webgpu", "wgsl_precompiled.gen.h"
    )
    glslang = sys.argv[3] if len(sys.argv) > 3 else "glslangValidator"

    count = precompile_wgsl(repo_root, output, glslang)
    if count == 0:
        print("WARNING: No shaders were precompiled!", file=sys.stderr)
        sys.exit(1)
