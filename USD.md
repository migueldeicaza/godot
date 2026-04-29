The USD module now has two product surfaces:

* a runtime that can load/save USD files as scenes (so your USD can be
  a root node, or you can instantiate assorted USD scenes into a Godot
  scene)

* an importer: where you choose how and what to import, similar to
  other Godot format importers - and you can later control import
  configuration via the importer panel.

I have mostly used and tested the first one, the second is new.

You will need to get yourself a build of OpenUSD from Github first:

https://github.com/PixarAnimationStudios/OpenUSD

And built it for your system, I am using the dynamic loadable version.

This was developed extensively using Codex AI.  See tasks.md for the
list of completed and pending work.

## 1. Runtime USD load/save

Godot can load `.usd`, `.usda`, `.usdc`, and `.usdz` directly as
`PackedScene` resources.

- Non-variant stages load as a generated Godot scene tree.
- Variant-capable stages load through `UsdStageResource` +
  `UsdStageInstance`, so the scene can expose live USD variant
  selections and rebuild the generated subtree when a selection changes.
- The runtime path can save back to USD.

Runtime save has three modes:

- Source-preserving stage save for source-loaded USD stages when only the
  supported source-aware changes were made.
- Source-aware rig save for imported USD rig content when the scene still
  matches the source structure closely enough.
- Composed-scene export fallback for regular Godot scenes or unsupported
  source-preserving edits.

## 2. Editor importer

`UsdSceneFormatImporter` imports `.usd`, `.usda`, `.usdc`, and `.usdz`
into a regular editor-imported Godot scene.

- Import-time variant selections are exposed as importer dropdowns.
- The selected composed result is baked once into the imported scene.
- Variant choices live in the import sidecar, not in the baked scene.
- Live variant switching and source-preserving USD authoring are runtime
  features, not importer features.

# Feature Set

## Stage, hierarchy, and variants

- Preserves stage `upAxis` and `metersPerUnit`.
- Preserves `resetXformStack`.
- Maps prim hierarchy to Godot node hierarchy.
- Exposes discovered USD variant sets as editable properties on
  `UsdStageInstance`.
- Rebuilds generated content when variant selections change.
- Keeps simple runtime `Node3D` local overrides across rebuilds
  (`transform` and `visible`).
- Saves variant default changes back into source `.usd`, `.usda`,
  `.usdc`, and `.usdz` files while preserving inactive variant branches.

## Geometry and scene mapping

- Imports `UsdGeomMesh` as `MeshInstance3D`.
- Imports primitive gprims such as cubes, spheres, capsules, cylinders,
  cones, and planes as native Godot primitive meshes.
- Imports `UsdGeomBasisCurves` structurally as one `Node3D` with one
  `Path3D` child per authored curve.
- Imports `UsdGeomPoints` and now supports point-based skinning and
  blend-shape deformation.
- Reads normals, including authored `primvars:normals`.
- Accepts common UV primvar names including `st`, `map1`, `UVMap`, and
  `uvmap` for Blender compatibility.
- Writes meshes back as `UsdGeomMesh`, including normals and UVs.

## Materials, subsets, and metadata fidelity

- Imports first-pass `UsdPreviewSurface` materials into
  `StandardMaterial3D`.
- Imports file-backed preview textures.
- Preserves mesh-wide material bindings and face subsets.
- Preserves generic non-material `GeomSubset` families for face, point,
  and edge subsets when their imported mapping still applies on save.
- Preserves exact authored subset child paths under the mesh when those
  paths still match.
- Reuses authored USD material paths when metadata already knows them.
- Reuses one authored USD material when multiple Godot surfaces share the
  same material resource.
- Round-trips typed unmapped USD attributes instead of degrading them to
  string-only metadata.
- Reapplies preserved authored relationships, including custom
  relationships.

## Composition preservation

- Preserves authored `references` and `payloads` in a read-only mode.
- Preserves authored `inherits` and `specializes` in a read-only mode.
- Marks composition-preserved nodes as boundaries for source-preserving
  save decisions.
- Reauthors preserved arcs onto the same prim on save instead of
  flattening them into local children when the read-only preservation
  path is in use.
- Emits save-report diagnostics when the save path falls back to
  composed-scene export or when generated edits cross composition
  boundaries.

## Cameras and lights

- Imports and saves perspective and orthographic cameras.
- Preserves clip range.
- Imports and saves distant and sphere lights.
- Saves spot shaping with `UsdLuxShapingAPI`.
- Saves rect lights with first-pass size and core light attributes.
- Can synthesize preview lighting for otherwise unlit stages and skips
  those synthetic preview nodes on save.

## Rigging and animation

- Imports `UsdSkelSkeleton` into `Skeleton3D`.
- Preserves bone hierarchy, joint metadata, rest pose data, and bound
  animation source paths.
- Imports first-pass `UsdSkelAnimation` joint translation, rotation, and
  scale into baked Godot `Animation` tracks.
- Supports multiple `skel:animationSource` clips on one skeleton.
- Saves first-pass joint TRS animation back to `UsdSkelAnimation`.
- Preserves imported animation timing, per-attribute authored sample
  domains, constant channels, and sampled no-op channels needed for
  round-trip fidelity.
- Imports skinned meshes and skinned points, including up to 8 weights
  per vertex/point when authored.
- Saves first-pass mesh and point skin bindings, `skel:skeleton`,
  `geomBindTransform`, and joint indices/weights.
- Imports direct mesh and point `BlendShape` targets.
- Imports and saves `normalOffsets` where the target supports normals.
- Imports authored inbetweens as real deformation data.
- Expands inbetweens into generated Godot blend-shape channels.
- Bakes USD `blendShapeWeights` into piecewise Godot animation tracks so
  interpolation remains USD-correct across inbetween thresholds.
- Saves first-pass blend-shape targets, inbetweens,
  `skel:blendShapes` / `skel:blendShapeTargets`, and
  `blendShapeWeights`.
- Supports a first source-aware rig save path that preserves existing
  `UsdSkelAnimation` prim paths, skeleton identity, and stable rig-prim
  targets when the imported scene still matches the source stage.

# Scope Notes

- The runtime path is the only path with live variants and source-aware
  save behavior.
- The importer path intentionally bakes one composed result and discards
  runtime-only metadata that the sidecar already tracks.
- Composition preservation is currently read-only. We preserve authored
  arcs and structure where possible, but we do not yet infer arbitrary
  new USD composition edits from generic Godot scene edits.
- Rigging round-trip is first-pass and source-aware only for the covered
  skeleton/skinning/blend-shape/animation cases.

# Tests

- `make SCONSFLAGS='module_usd_enabled=yes accesskit=no angle=no -j8'`
- `bin/godot.macos.editor.dev.arm64 --test --test-suite='[SceneTree][USD]' --minimal --no-intro`
- `bash modules/usd/tests/run_usd_test_suite.sh bin/godot.macos.editor.dev.arm64`
- `bin/godot.macos.editor.dev.arm64 --headless --path . --script modules/usd/tests/points_blend_shape_roundtrip_probe.gd -- /tmp/points_blend_shape_roundtrip.usda`
