# USD Scene Loader Prototype

This module adds a direct `ResourceFormatLoader` for `.usd`, `.usda`, `.usdc`, and `.usdz` files so Godot can treat them as `PackedScene` resources without going through the editor import pipeline.

## Current scope

- Loads USD stages directly into a generated `PackedScene`.
- Creates native Godot nodes for the first-pass 3D mappings:
  - `UsdGeomXformable` -> `Node3D`
  - `UsdGeomMesh` -> `MeshInstance3D` with a generated `ArrayMesh`
  - `UsdGeomCube` -> `MeshInstance3D` + `BoxMesh`
  - `UsdGeomSphere` -> `MeshInstance3D` + `SphereMesh`
  - `UsdGeomCapsule` -> `MeshInstance3D` + `CapsuleMesh`
  - `UsdGeomCylinder` -> `MeshInstance3D` + `CylinderMesh`
  - `UsdGeomCone` -> `MeshInstance3D` + tapered `CylinderMesh`
  - `UsdGeomPlane` -> `MeshInstance3D` + `PlaneMesh`
  - `UsdGeomCamera` -> `Camera3D`
  - `UsdLuxDistantLight` -> `DirectionalLight3D`
  - `UsdLuxSphereLight` -> `OmniLight3D`
- Maps first-pass preview materials:
  - `UsdShadeMaterial` + `UsdPreviewSurface` -> `StandardMaterial3D`
  - `UsdUVTexture` -> albedo texture on `StandardMaterial3D`
  - `MaterialBindingAPI` and `GeomSubset` face partitions -> per-surface material assignment
- Creates placeholder `Node` or `Node3D` instances for unsupported prim types instead of dropping them.
- Preserves authored-but-unmapped USD attributes and relationships in node metadata under the `usd` metadata key.

## Metadata shape

Godot metadata keys must be valid ASCII identifiers, which means `usd:foo` cannot be used as the actual metadata key. The loader therefore stores a single metadata dictionary in `meta["usd"]`, and that dictionary uses keys like `usd:prim_path`, `usd:type_name`, and `usd:unmapped_attributes`.

This keeps the requested `usd:` namespacing while still fitting Godot's metadata rules.

## What maps cleanly today

- Stage metrics:
  - `upAxis` maps to a corrective transform on the generated scene root.
  - `metersPerUnit` maps to a uniform scale on the generated scene root.
- Hierarchy:
  - Prim hierarchy maps to node hierarchy.
  - `resetXformStack` maps to a top-level `Node3D` transform that preserves the stage's axis and unit correction instead of inheriting parent USD xforms.
- Visibility:
  - USD visibility maps to `Node3D.visible`.
- Core geometry:
  - Polygonal meshes map to triangulated `ArrayMesh` surfaces.
  - Face-varying UV primvars map to `ARRAY_TEX_UV`.
  - Primitive gprims map to Godot primitive mesh resources.
- Basic materials:
  - Mesh-wide and subset material bindings map to ArrayMesh surface materials.
  - `UsdPreviewSurface` diffuse color and `UsdUVTexture` albedo textures map to `StandardMaterial3D`.
  - `displayColor` maps as a fallback when no supported USD material is bound.
- Cameras:
  - Perspective and orthographic cameras map to `Camera3D`.
  - Clipping range maps to near/far planes.
- Basic lights:
  - Distant lights map to `DirectionalLight3D`.
  - Sphere lights map to `OmniLight3D`.
  - Color, intensity, and exposure map approximately to Godot light parameters.
- Preview fallback:
  - Lightless stages can synthesize a `WorldEnvironment` and `DirectionalLight3D` preview rig.
  - The behavior is controlled by `filesystem/import/usd/preview_lighting_mode` with `Never`, `When Missing`, and `Always`.

## Known gaps

- This is a read-only prototype. There is no `ResourceFormatSaver` yet.
- Mesh import is intentionally conservative:
  - Triangulation, normals, face-varying UVs, display colors, and first-pass preview materials are mapped.
  - Subdivision features, creases, holes, non-preview shader graphs, and advanced texture transforms are preserved as metadata, not converted.
- USD composition data is not reconstructed as editable Godot concepts:
  - references
  - payloads
  - variant sets
  - inherits/specializes
  - layer stacks
- No first-pass mapping yet for:
  - non-preview materials and richer shader networks beyond `UsdPreviewSurface`/`UsdUVTexture`
  - `UsdSkel`
  - blend shapes
  - point instancers
  - curves
  - points
  - volumes
  - physics
  - dome/rect/disk/cylinder lights
- Attribute preservation is currently string-based for unmapped authored properties. That is enough for analysis and planning, but not enough for exact round-trip authoring.

## Engineering plan for save/round-trip

1. Add a `ResourceFormatSaver` for `.usd/.usda/.usdc`.
2. Replace string-only preservation with typed USD property snapshots stored in Godot metadata-compatible dictionaries.
3. Round-trip mapped properties from native Godot nodes first, then reapply preserved unmapped authored properties.
4. Preserve composition structure explicitly:
   - prim path
   - type name
   - specifier
   - variant selections
   - references/payloads
   - relationship targets
5. Add dedicated converters for:
   - materials
   - skeletons
   - animation
   - instancing
   - light types beyond distant/sphere

## Research and environment blockers

- The local OpenUSD copy at `/tmp/openusd-build/install` is the current default module target.
- Another local OpenUSD copy at `~/openusd-build/install` appears to carry stale `LC_RPATH` entries that still point at `/tmp/openusd-build/install/lib`.
- A trivial probe linked against that SDK loaded a mixed set of USD dylibs from both locations and crashed with duplicated debug symbol registration.
- The module build supports overriding the SDK location with `USD_SDK_PATH`, but the dylib install names and rpaths should be cleaned up before this is treated as a stable runtime dependency.

## Build note

The module expects an OpenUSD install prefix in `USD_SDK_PATH` or falls back to `/tmp/openusd-build/install`.
