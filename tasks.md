# USD Load/Save Ledger

This file tracks what the prototype currently loads, what the USD saver writes back, and what is still deferred.

## Status Key

- `Load`: imported into Godot today
- `Save`: authored back out by the current USD saver
- `Deferred`: intentionally not round-tripped yet

## Stage And Hierarchy

| Area | Load | Save | Notes |
| --- | --- | --- | --- |
| `upAxis` | Yes | Yes | Saver writes stage metadata and avoids baking root correction into authored prim transforms. |
| `metersPerUnit` | Yes | Yes | Saver restores stage units and unwraps the synthetic scene root. Runtime unit/scale UX remains open for assets with surprising authored units. |
| Prim hierarchy | Yes | Yes | Saver writes current Godot node hierarchy as USD prim hierarchy. |
| `resetXformStack` | Yes | Yes | Saver preserves `usd:resets_xform_stack` on `Node3D` and writes a resetting matrix xform op. |
| Preview fallback nodes | Yes | Skipped | Synthetic preview sun/environment nodes are not serialized back to USD. |

## Geometry

| Area | Load | Save | Notes |
| --- | --- | --- | --- |
| `UsdGeomMesh` topology | Yes | Yes | Saver flattens Godot mesh surfaces to triangle faces in a single `UsdGeomMesh`. |
| `UsdGeomBasisCurves` | Partial | Deferred | Loader preserves authored curve structure as `Path3D` children and records widths/type/wrap metadata. Future decision: keep this as structural import only, or add visible width-based curve rendering. |
| Normals | Yes | Yes | Saver writes vertex normals when available. |
| UVs (`primvars:st`) | Yes | Yes | Saver writes one vertex-interpolated `st` primvar when available. |
| `displayColor` fallback | Yes | Deferred | Saver does not yet emit material color/displayColor. |
| Material subsets / bindings | Partial | Partial | Loader records subset identity metadata, exact authored subset child paths under the mesh, authored face-order metadata, authored point-index metadata, and generic non-material face/point/edge subsets; inherited material subsets display with the effective mesh material while preserving the lack of an explicit subset binding; saver writes mesh-wide bindings, per-surface face subsets, reuses authored USD materials across shared Godot materials, preserves authored subset paths/names/family info, reuses authored material paths, preserves sparse authored subset face indices when the imported face-order metadata still applies, and re-emits preserved non-material face, point, and edge subset families. Richer USD composition and unsupported subset families remain approximated. |
| Primitive gprims | Yes | Deferred | Loader maps cubes/spheres/etc. to native meshes; saver currently writes generic meshes/xforms only. |

## Animation And Rigging

| Area | Load | Save | Notes |
| --- | --- | --- | --- |
| `UsdSkelSkeleton` | Partial | Partial | Loader imports a structural `Skeleton3D` bone hierarchy with per-bone joint metadata and rest pose data. Saver now writes a first-pass `UsdSkelSkeleton` for imported skeleton nodes, preserving joints, rest transforms, bind transforms, and bound animation-source relationships needed by blend-shape round-trip. |
| `UsdSkelAnimation` joint TRS | Partial | Partial | Loader bakes first-pass Godot `Animation` tracks for bound joint translation/rotation/scale channels onto an `AnimationPlayer`, supports multiple `skel:animationSource` clips on one skeleton, and records the source USD animation timing/default-channel metadata and per-attribute authored time domains needed for higher-fidelity save. Saver now reauthors joint translation/rotation/scale tracks back to `UsdSkelAnimation`, preserves the imported stage time domain (`startTimeCode`, `endTimeCode`, `timeCodesPerSecond`), preserves each authored attribute's original sample domain instead of rebaking everything onto one union time grid, restores authored constant channels and sampled no-op channels that were pruned from visible Godot tracks, preserves multiple saved animation targets on `skel:animationSource`, shares the same saved animation prim with blend-shape weights when both are present, and has a first source-aware rig save path for source-loaded USD scenes that preserves existing `UsdSkelAnimation` prim paths, skeleton prim identity, and stable rig-prim targets when the imported scene structure still matches the source stage. Exact multi-layer opinion structure is still not fully preserved by this path. |
| `UsdSkel` skinning | Partial | Partial | Loader now imports mesh and points joint indices/weights, preserves up to 8 weights per vertex/point when authored, binds skinned `MeshInstance3D` nodes to imported `Skeleton3D` bones with a Godot `Skin`, resolves inherited `skel:skeleton` bindings authored on ancestors, and maps `UsdGeomPoints` to point-primitive meshes. Saver now reauthors first-pass mesh and points skin bindings, `skel:skeleton`, `geomBindTransform`, and vertex/point joint indices/weights, including `elementSize = 8` when the Godot mesh uses 8-weight skinning. |
| `BlendShape` | Partial | Partial | Loader now imports direct mesh and `UsdGeomPoints` `BlendShape` targets as relative Godot blend shapes, carries primary `normalOffsets` into Godot blend-shape normal deltas where the target prim supports normals, preserves authored inbetween metadata, expands authored inbetweens into generated piecewise Godot blend-shape channels, and bakes `blendShapeWeights` into piecewise Godot blend-shape animation tracks that follow USD inbetween thresholds. Saver now reauthors mesh and point-prim `BlendShape` prims, inbetweens, `skel:blendShapes` / `skel:blendShapeTargets`, and first-pass `blendShapeWeights` animation samples. Non-point-based blend-shape targets and exact authored sparsity/layer fidelity remain open. |

## Cameras

| Area | Load | Save | Notes |
| --- | --- | --- | --- |
| Perspective camera | Yes | Yes | Saver writes a `UsdGeomCamera` via `GfCamera`. |
| Orthographic camera | Yes | Yes | Saver writes a `UsdGeomCamera` via `GfCamera`. |
| Clip range | Yes | Yes | Saver preserves near/far clipping. |
| Aperture offsets / advanced camera metadata | Partial | Deferred | Saver only writes the core frustum state. |

## Lights

| Area | Load | Save | Notes |
| --- | --- | --- | --- |
| `UsdLuxDistantLight` | Yes | Yes | Round-trips with color/intensity and a default authored angle. |
| `UsdLuxSphereLight` / omni | Yes | Yes | Round-trips as `UsdLuxSphereLight`. |
| Spot shaping | Approximate | Yes | Saver writes a sphere light plus `UsdLuxShapingAPI`. |
| `UsdLuxRectLight` / area | Approximate | Yes | Saver writes size and core light attributes. |
| Textured area lights | Yes | Deferred | Saver does not yet author `inputs:texture:file`. |
| Dome / disk / cylinder lights | Partial | Deferred | Loader approximates some of these; saver does not yet emit them explicitly. |

## Materials And Metadata

| Area | Load | Save | Notes |
| --- | --- | --- | --- |
| `UsdPreviewSurface` | Partial | Partial | Saver writes a first-pass `UsdPreviewSurface` network from `StandardMaterial3D` scalars and textures, including generated/in-memory textures emitted beside composed USD saves when they do not already have a stable file path. |
| Unmapped authored attributes | Yes | Partial | Loader now preserves a typed subset of common USD value types and saver reapplies those authored attributes. Unsupported value types still remain metadata-only. |
| Unmapped authored relationships | Yes | Partial | Saver now reapplies stored relationship targets, including custom relationships. |
| Custom USD composition data | Partial | Partial | Loader records authored `references`, `payloads`, `inherits`, and `specializes` as structured metadata and saver reapplies them in a read-only preservation mode by authoring the arcs back onto the same prim and skipping subtree flattening. Variant-capable USD files now have a live `UsdStageInstance` path. Source `.usd`, `.usda`, `.usdc`, and `.usdz` stage instances preserve source content when saving back to the same USD family; changed variant selections are authored as new root-layer defaults while preserving inactive variant data. Non-source USD saves still export only the current composed result. |

## Composition Follow-Up

The remaining composition work should not be treated as a small extension of the current metadata path. `variants` now have a first-class live Godot path, and the first read-only arc preservation pass covers references, payloads, inherits, and specializes, but USD authoring and round-trip behavior still need a clear policy before we broaden edit fallbacks or infer new composition structure.

### Live Variant Runtime Path

Current state:

1. Variant-capable USD files load as a ready-to-use `UsdStageInstance` scene root.
2. `UsdStageInstance` owns a `UsdStageResource`, per-instance `variant_selections`, a generated-node baseline cache, runtime overrides keyed by USD prim path, and a replaceable `_Generated` subtree.
3. Inspector dropdowns are generated from the composed stage's variant catalog.
4. Variant selections are authored into an anonymous session layer over the real source root layer so sibling instances are isolated and stage metadata such as `metersPerUnit` remains stable.
5. Changing a selection rebuilds the generated subtree and preserves user-authored nodes outside `_Generated`.
6. `get_node_for_prim_path()` gives users a stable lookup API across rebuilds when the prim still exists.
7. Loading saved Godot scenes adopts an existing `_Generated` child to avoid duplicate generated roots.
8. Runtime `Node3D` transform and visibility edits below `_Generated` are stored by USD prim path, reapplied after rebuilds, and kept dormant when the selected variant no longer composes that prim.
9. The previously observed `UsdStageResource` resolver crash was environmental: the current shared-library OpenUSD install at `/Users/miguel/cvs/usd/install` passes the tracked runtime suite when build and runtime point at the same SDK root.

Known runtime follow-ups:

1. Runtime override storage currently covers only `Node3D` transform and visibility. Materials, mesh assignments, added children under generated nodes, and other property edits still need a policy.
2. Large stages need caching and eventually partial rebuilds. Current behavior rebuilds the generated subtree.
3. Unit/scale UX needs a policy. Some assets author surprising `metersPerUnit`; we may need a Godot-side display/import scale override rather than relying on asset metadata alone.
4. Debug inspector properties should be reviewed before this leaves prototype status.

Ordered follow-up queue:

- [x] Record variant boundary/context metadata on generated nodes: active selected variant stack, variant-owner prim paths, and local variant-set definitions where available.
- [x] Detect edits below composition boundaries before saving. The first pass reports generated-node edits below variant/reference/payload/inherits/specializes boundaries before preserving source USD variant defaults.
- [x] Add explicit save reporting for preservation mode: source layer/package preserved, variant defaults authored, composed result flattened, or inactive branches not preserved.
- [x] Add read-only `inherits` and `specializes` preservation using the same conservative metadata/reapply pattern as references and payloads.
- [x] Add runtime override storage for intentional Godot-side edits to generated nodes, keyed by USD prim path and kept dormant when the selected variant no longer composes that prim. First pass covers `Node3D` transform and visibility.
- [ ] Improve runtime rebuild scalability with cached decoded assets and later partial rebuilds for the affected variant subtree.
- [ ] Define the unit/scale UX policy for assets with surprising `metersPerUnit`.
- [ ] Review and remove or hide prototype debug inspector properties before the feature leaves prototype status.

### Variant USD Save/Round-Trip Policy

Current state:

- Saving to Godot scene formats preserves the live `UsdStageInstance` setup and editable variant selections.
- Saving an unchanged source `.usd`, `.usda`, `.usdc`, or `.usdz` `UsdStageInstance` back to the same USD family preserves the original source file/package instead of baking a reduced composed layer.
- Saving a source `.usdz` after effective variant selection changes extracts the package, authors the selected variant defaults into the root layer, and repackages the asset so inactive variant assets remain present.
- Saving a source `.usd`, `.usda`, or `.usdc` after effective variant selection changes authors the selected variant defaults into a temporary copy of the source root layer and then replaces the destination.
- Saving non-source `.usd`, `.usda`, `.usdc`, or `.usdz` exports the current composed generated result.
- The generic composed-result saver does not recreate authored variant sets, inactive branches, or USD variant opinions.

Implemented source-preserving USD saves use read-only variant default authoring with explicit source structure, not inferred reconstruction.

Current source-preserving behavior:

1. Loader records authored variant-set names, current selections, and the prim paths that sit at or under variant boundaries in structured `usd` metadata.
2. Source-preserving saves author changed variant defaults into a copy of the source root layer/package.
3. Variant preservation is treated as composition authoring, not as generic metadata replay.
4. We do not infer new variants or synthesize variant structure from arbitrary Godot edits in the first pass.
5. For `.usdz`, variant selection authoring preserves the source archive contents, root layer name, and inactive variant assets. For standalone `.usd`, `.usda`, and `.usdc`, default selection authoring preserves the source layer format. Deeper structural edits remain out of scope.

Why this is first-class:

- Variants can change which children, properties, and composition arcs even exist.
- References and payloads inside variants cannot be handled correctly if variant boundaries are invisible or flattened too early.
- Edit-boundary decisions depend on whether a node came from the active branch of a variant set or from non-variant authored structure.

### Open Composition Concerns

These items should stay in the ledger until a concrete policy is chosen and implemented.

#### 1. Composition Editing Boundaries

Current state:

- For preserved `references`, `payloads`, `inherits`, and `specializes`, the saver keeps the arc and does not flatten the imported composed subtree back into local authored children.

Unresolved questions:

- What should happen if a user edits nodes or properties that originated entirely from inside a preserved referenced or payloaded subtree?
- When do we keep the original composition arc and ignore local edits below it?
- When do we warn that the edit cannot be represented without breaking composition?
- When do we intentionally fall back to flattening?

Current bias:

- Stay conservative.
- Preserve the original composition arc unless we explicitly choose a flattening fallback.
- Detect and report composition-breaking edits rather than silently changing authoring structure.

#### 2. Variant Boundaries Drive Policy

This was the main design concern before the first live-variant implementation. The current metadata path now records active branch context, but broader edit policies still depend on it.

Resolved first-pass behavior:

- Active branch context is recorded in generated-node `usd:variant_context` metadata.
- Local variant set definitions are recorded in `usd:variant_sets` metadata on owner prim nodes.
- Generated nodes at variant owners are marked with `usd:variant_boundary`.
- Source-preserving saves preserve inactive branches by editing the source layer/package defaults, not by reconstructing branches from Godot nodes.
- Generated-node edits below variant/reference/payload/inherits/specializes boundaries are detected and reported before source-preserving saves.

Open questions:

- How much of the runtime override storage should eventually become authored USD override data?
- When should unsupported generated edits below a variant boundary trigger flattening rather than warning?
- How should edits to references/payloads/inherits/specializes authored inside inactive variants be represented?

Expected impact:

- The remaining answers determine how we handle intentional authored edits to references, payloads, inherits, and specializes that live inside variants.

#### 3. Asset Path Relocation Policy

Current state:

- Preserved `references` and `payloads` keep their authored asset paths.
- This is correct for round-trip fidelity, but saving to a different directory can produce recomposition warnings until the sidecar assets are also available relative to the new save destination.

Policy options still to decide:

1. Preserve authored asset paths exactly.
2. Rewrite relative paths against the new save location.
3. Optionally copy or export sidecar assets and rewrite to the copied location.

Important note:

- This decision should be made after the variant policy is clearer, because variant-authored composition assets may need different handling from top-level composition assets.

#### 4. Inherits And Specializes

Current state:

- Loader records authored path lists in `usd:inherits` and `usd:specializes` metadata.
- Saver reapplies those path lists on the same prim and treats the node as a read-only composition boundary.
- The first pass does not infer new inherits/specializes from generic Godot edits.

Expected approach:

- Continue validating real-world inherits/specializes assets, especially when those arcs are authored inside variants or layered opinions.
- Reapply them on save only when the prim still maps cleanly.
- Do not infer new inherits/specializes from generic Godot edits in the first pass.

Still constrained by:

- Variant policy.
- Composition edit-boundary policy.

#### 5. Structural Drift Reporting

As composition support expands, the saver needs an explicit way to report when authored structure was preserved exactly versus approximated.

Needed eventually:

- metadata or diagnostics for “exactly preserved”
- metadata or diagnostics for “preserved values but rebuilt authoring structure”
- metadata or diagnostics for “flattened / approximated / not source-preserving”

## Future Task Queue

These are intentionally separate from the current runtime `UsdStageInstance` path. They should stay visible as follow-up work, not be treated as part of the finished dynamic-variant prototype.

### Source-Fidelity Follow-Up

- [ ] Preserve exact multi-layer USD opinion structure for `UsdSkelAnimation` and related rigging data, instead of only preserving the composed authored values and per-attribute sample domains. First source-aware slice now preserves existing animation prim paths, skeleton prim identity, and strongest-authored animation property targets for source-loaded scenes whose imported structure still matches the source stage.
- [ ] Build a source-aware edit model for USD saves so authored edits can be targeted back to the correct layer/opinion site rather than always going through the current composed-scene exporter. First slice now covers source-loaded skeleton, skinned mesh / point, blend-shape, and `UsdSkelAnimation` rig edits only; broader scene/property authoring still falls back to the composed-scene exporter.

### EditorImporter Track

This is a different product surface from the live `UsdStageInstance` runtime path. The importer should be tracked independently so we can make explicit tradeoffs about editor UX, import products, and save/reimport behavior.

- [x] Define the `EditorSceneFormatImporter` scope for USD: first pass targets `.usd`, `.usda`, `.usdc`, and `.usdz`, and produces a regular imported scene rather than a live `UsdStageInstance`. The concrete importer type should be named `UsdSceneFormatImporter` so `Usd` appears in logs, crash traces, and diagnostics. Scene import is the primary product; asset extraction stays incidental to that scene import result.
- [x] Decide how importer-side variant handling differs from the live runtime path: importer variants are fixed import-time selections stored as importer options, one composed result per imported scene, with no live switching and no multi-preset generation in the first pass.
- [x] Define reimport behavior and sidecar state: importer-side variant overrides and importer-specific options live in the import sidecar and drive deterministic full reimport from source. Saving the imported Godot scene does not attempt source-preserving USD authoring.
- [x] Decide which runtime-only features stay out of the importer path: live variant switching, session-layer composition, generated-node runtime override storage, and other `UsdStageInstance` behaviors remain runtime-only.
- [x] Prototype `UsdSceneFormatImporter`, a first USD `EditorSceneFormatImporter` that can import a stage into a regular Godot scene without depending on `UsdStageInstance`. The first pass is editor-only, accepts `.usd`, `.usda`, `.usdc`, and `.usdz`, bakes importer-side variant selections from the import sidecar, and strips the live `UsdStageInstance` wrapper from the imported result.
- [x] Add importer-side UI/policy for variant selections beyond the raw `usd/variant_selections` JSON option, so common variant choices are discoverable without hand-editing sidecar data. First pass now emits one importer dropdown per discovered USD variant set and keeps the raw JSON field hidden for compatibility only.
- [x] Decide importer diagnostics for cases where the baked import path necessarily loses inactive branches or other live-runtime-only behavior. First pass now warns at import time only; importer-only selections remain in the `.import` sidecar and are not copied onto the baked scene root.
- metadata or diagnostics for “preserved selection but lost inactive branches”
- metadata or diagnostics for “flattened fallback required”

This matters most once variants are introduced, because a visually similar save may still have materially different authored composition structure.

## Current Saver Milestone

The current USD saver milestone is:

1. Save a `PackedScene` to `.usd`, `.usda`, `.usdc`, or `.usdz`.
2. Preserve stage units/orientation and scene hierarchy.
3. Re-author transforms, meshes, cameras, and basic lights.
4. Skip synthetic preview nodes so preview-only editor lighting does not become authored USD content.
5. Round-trip first-pass `UsdPreviewSurface`, subset bindings, typed unmapped USD metadata, authored material/subset identity where metadata is available, and read-only `references`/`payloads`/`inherits`/`specializes` preservation.
6. Preserve unchanged source `.usd`, `.usda`, `.usdc`, and `.usdz` stage instances when saving back to the same USD family.
7. Save non-source `UsdStageInstance` roots by exporting the current composed generated result, not the Godot-side control node.
8. Keep package-preserving USD variant authoring/round-trip as the first-class composition policy and avoid aggressive composition rewriting.
