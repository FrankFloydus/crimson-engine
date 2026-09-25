<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Features unavailable in public v0.1

This is the reactivation checklist for **quader-0.1.exe**. The internal **quader.exe**
retains the complete feature set except for temporary UI restrictions explicitly
marked as applying to both executables below. Public feature releases are independent
of the internal application version in `VERSION`.

Unchecked items are intentionally unavailable in public 0.1. Unless explicitly
marked **UI only**, they are hidden from the UI and rejected through commands,
shortcuts, tools and native public APIs. UI-only restrictions preserve the underlying
data and programmatic behavior.
The authoritative scheduling table is
[release_features.cpp](src/core/release_features.cpp); native command/tool
requirements live in
[release_feature_requirements.cpp](src/editor/metadata/release_feature_requirements.cpp).
Keep this checklist aligned when scheduling a feature for a later public release.

## Materials and UVs

- [ ] **Material system** — `ReleaseFeature::Materials`
  - Material browser, material editor, material inspector controls and material usage browsing.
  - Creating, editing, importing, saving, assigning, replacing and deleting custom materials.
  - Material folders and project material organization.
  - Authored material textures/images and material color/property previews.
  - Material find/replace and replacement across project scenes.
  - Assigning materials to objects or individual faces; choosing a material for new primitives.
  - Material picking/transferring through viewport gestures and context menus.
  - Selecting faces with the same material.
  - Per-material appearance, including in Shaded mode: public 0.1 uses one shared default material.
- [ ] **UV and face-texture editing** — `ReleaseFeature::UvEditing`
  - UV menu, texture toolbar, Face Texture dock and its interactive UV canvas.
  - UV Peel tool and UV/face-texture previews.
  - Align texture to grid, face or view.
  - Cube, cylinder and sphere texture projection.
  - Texture scaling on U/V, including proportional Link Scale, shifting and rotation.
  - Horizontal/vertical texture flips and face-texture flips.
  - Fit texture on both axes, U only or V only.
  - Justify texture left, right, top, bottom or center.
  - Face-texture transfer, continuation and direct UV/texture transform editing.
  - Texture Lock and Texture Lock Scale controls and shortcuts.

Intrinsic/generated mesh UV data remains stored and exported. Disabling UV editing
does not remove the UV data needed by existing geometry and the default material.

**Settings > Viewport > Default Material remains available.** Its image preview and
PNG/JPG chooser configure the shared default material for existing and future meshes
and tool previews. Apply/OK publishes the change; Cancel discards the draft.
The selected image is copied into the application's settings profile, so moving the
original image does not break the preference. This does not enable authored materials
or UV editing, and canonical scene material references and saved geometry stay unchanged.
The built-in default uses the same image as `resources/textures/default_texture.png`.
Solid retains its existing flat-color appearance; Shaded displays the material texture.

## Viewport, lighting and environment

- [ ] **Mesh Grid** — `ReleaseFeature::MeshGrid`
  - Mesh-surface grid toggle, shader display and minor/major mesh-grid color settings.
- [ ] **Rendered viewport** — `ReleaseFeature::RenderedViewport`
  - Rendered shading mode.
  - Diffuse, Specular, Albedo, Roughness, Metalness and Normal material-debug modes.
- [ ] **Lights** — `ReleaseFeature::Lights`
  - Point-light and directional-light creation, placement and editing.
  - Light component creation through generic APIs and duplication/paste of light objects.
- [ ] **Environment** — `ReleaseFeature::Environment`
  - Environment selection, background/color, intensity and rotation controls.
  - Fog controls and environment/fog color previews.
  - Post-processing controls, including exposure-related rendering settings.
- [ ] **Camera settings dropdown (UI only)** — `ReleaseFeature::CameraSettings`
  - The viewport Camera launcher and its Near/Far controls.
  - Camera clipping values and programmatic clipping updates remain supported.
- [ ] **Mesh Cast Shadow control (UI only)** — `ReleaseFeature::CastShadows`
  - Cast Shadow in the mesh inspector's Lighting section.
  - Double Sided remains available; stored shadow flags and rendering support remain intact.

Wireframe, Solid and Shaded remain available. Ordinary construction grids, camera
navigation and internal camera clipping remain available. Fixed internal lighting,
environment resources and default-material shaders remain implementation support.

## Mesh physics controls

- [ ] **Collision Type and Body type (UI only, both executables)**
  - Temporarily omit these unsupported selectors from the mesh Inspector's Physics section.
  - New meshes retain the existing `MeshCollisionType::Mesh` and `MeshPhysicsBodyType::StaticBody`
    defaults. Hiding the selectors does not rewrite saved physics properties or remove
    programmatic access.
  - Generate Collision and Display Collision remain available.
  - Restore the selectors when the alternate collision/body modes are supported. This is
    a shared Inspector restriction, not a separately scheduled `ReleaseFeature`.

## Primitives

- [ ] **Torus** — `ReleaseFeature::TorusPrimitive`.
- [ ] **Cone** — `ReleaseFeature::ConePrimitive`.
- [ ] **Quad Sphere** — `ReleaseFeature::QuadSpherePrimitive`.

These exclusions cover the primitive toolbar popup, commands, shape options and
direct creation entry points. The public popup exposes Create Box, Create Cylinder
and Create Sphere, with default shortcuts Shift+B, Shift+C and Shift+S respectively.
Sphere retains its Type setting with UV Sphere; Quad Sphere stays excluded.
Cone and Torus remain unassigned by default in Internal Full.

The toolbar button starts with Box and remembers the last successfully activated
shape for the application session, including activation through shortcuts. Shape
selection replaces the generic Primitive Type setting. Switching to a different
shape applies completed primitives, discards an unfinished draw and starts a fresh
creation session; reselecting the active shape leaves the current session unchanged.

## Modeling operations

- [ ] **Bevel Vertices** — `ReleaseFeature::BevelVertices`.
- [ ] **Bevel Edges** — `ReleaseFeature::BevelEdges`, including edge-bevel previews/options.
- [ ] **Slice Quads** — `ReleaseFeature::SliceQuads`.
- [ ] **Thicken Faces** — `ReleaseFeature::ThickenFaces`.
- [ ] **Create Outer Hull** — `ReleaseFeature::OuterHull`.
- [ ] **Half Cut meshes** — `ReleaseFeature::HalfCut`.

Thicken Faces and Create Outer Hull share an internal operation but have separate
public feature entries so they can be released independently.

## Arrangement, placement and baking

- [ ] **Arrange** — `ReleaseFeature::Arrangement`
  - Align minimum, center or maximum on X, Y or Z (nine commands).
  - Distribute centers on X, Y or Z (three commands).
  - Distribute gaps on X, Y or Z (three commands).
- [ ] **Radial Align** — `ReleaseFeature::RadialAlign`
  - Vertex, edge and face variants, including the generic context action.
- [ ] **Snap to Floor** — `ReleaseFeature::SnapToFloor`
  - Snap Meshes to Floor and Snap Vertices to Floor.
- [ ] **Transform baking** — `ReleaseFeature::TransformBaking`
  - Bake Transforms, Bake Location, Bake Rotation and Bake Scale.

## Tools, symmetry and arrays

- [ ] **Mirror Tool** — `ReleaseFeature::MirrorTool`, including its preview/settings.
- [ ] **Mesh mirroring** — `ReleaseFeature::MeshMirror`, on X, Y and Z.
- [ ] **Mesh flips** — `ReleaseFeature::MeshFlip`, horizontal and vertical geometric flips.
- [ ] **Cut Tool** — `ReleaseFeature::CutTool`, including face-plane cutting and cut-mode gestures.
- [ ] **Poly Tool** — `ReleaseFeature::PolyTool`.
- [ ] **Persistent Extrude Tool** — `ReleaseFeature::ExtrudeTool`
  - Extrude toolbar/menu activation and the persistent tool's settings workflow.
  - Standalone extrusion controls and preview entry points.
  - **Quick Shift+drag extrusion remains available**, including its existing
    transform-drag preview, commit and undo behavior.
- [ ] **Linear Array** — `ReleaseFeature::LinearArray`, including direct creation, previews and gestures.
- [ ] **Radial Array** — `ReleaseFeature::RadialArray`, including direct creation, previews and gestures.
- [ ] **Path Array** — `ReleaseFeature::PathArray`, including preview/settings; also requires Curves.
- [ ] **Curves** — `ReleaseFeature::Curves`
  - Curve creation and editing tools.
  - Curve points, handles, point-type conversion, curve settings and solidification.
  - Direct curve creation/editing and curve component insertion through native APIs.
  - Curve duplication/paste and related interactive controls.

Knife, clipboard Cut and Flip Normals remain available. Completed mesh geometry
does not retain a history of which tool produced it: these gates restrict the
workflows, not an equivalent geometric result produced through permitted tools.

## Pivots and selection

- [ ] **Pivot editing** — `ReleaseFeature::Pivot`
  - Persistent Pivot Tool, pivot alignment and snapping settings.
  - Temporary Quick Pivot gesture.
  - Copy Pivot from Active and origin/pivot rebasing operations.
- [ ] **Lasso selection** — `ReleaseFeature::LassoSelection`.
- [ ] **Advanced selection controls (UI only)** — `ReleaseFeature::AdvancedSelectionUi`
  - Select Direction.
  - Select Co-planar Faces.
  - Select By Normal.
  - Select N-gons.
  - Select Similar Meshes.
  - The native selection APIs remain available.

Normal move/rotate/scale, their internal transform centers and camera navigation
pivots remain available. The Inspector's Link Scale control for proportional X/Y/Z
numeric edits remains available; it does not enable UV editing. Box/click selection
remains available.

## Scene instances

- [ ] **Scene instancing** — `ReleaseFeature::Instancing`
  - Create Instance, Flatten Instance and instance-edit entry/exit controls.
  - Linked instance creation, flattening and edit scopes through native APIs.
  - Generic instance-object creation and duplication of hierarchies containing instances.
  - Copying and pasting linked instances through the project clipboard.
  - Opening instance-bearing scenes in the public workspace: rejected explicitly,
    without flattening or silently removing their contents.

Ordinary mesh and group duplicates retain independent geometry and remain available,
along with their normal copy/paste workflows. Internal Full retains instancing support.

## Layers

- [ ] **Layer panel and layer system** — `ReleaseFeature::Layers`
  - Layer panel, workspace toggle and layout restoration.
  - Creating, renaming, deleting and arranging layers.
  - Assigning objects to layers and layer visibility/locking controls.
  - Layer selection and authoring through native APIs.
  - Loading or pasting authored layer records/assignments into the public workspace.

Ordinary objects remain unassigned to a user layer. Internal layer storage and
visibility infrastructure remain available for native scene/rendering support.

## Bridge menu and shortcut

The Face and Edge top menus show only the interpolated Bridge action. The duplicate
normal Bridge menu entry is removed in both executables. **B** invokes the existing
quick linear Bridge with one step, without opening an interpolated preview or
reusing its remembered multi-step setting. Existing shortcut overrides are preserved.
Bridge itself remains available and does not need reactivation in a later release.

## Reactivating a feature in a future public release

1. Choose the next public major/minor release; published releases stay cumulative.
2. In [release_features.cpp](src/core/release_features.cpp), replace that feature's
   `kUnreleasedProductRelease` with its first release, for example `product_release(0u, 2u)`.
   Do not change an existing published feature's first release or the historical 0.1 value.
3. Update the selected public release in
   [release_profile.cpp](src/core/release_profile.cpp) and the public executable's
   `OUTPUT_NAME` in [CMakeLists.txt](CMakeLists.txt), keeping both version labels aligned.
   Update the matching release names in `tools/verify_native_build.ps1`,
   `tools/package_public_release.ps1` and `tools/boot_smoke.ps1`, and update the
   restricted test profile expectations for that release.
4. Enable prerequisite features too; Path Array requires Curves. Exercise the
   newly available workflow and verify that remaining exclusions still hold.
5. Update this checklist with the first public release beside the reactivated entry.
   Run the regression gate and build both Release targets in the canonical tree.

No user setting or license switch changes the compiled profile. The internal Full
build remains available for development. Public builds share a stable separate
settings directory at `%LOCALAPPDATA%/Quader/Public`.

## Save and distribution boundaries

- Public 0.1 opens supported saves marked as requiring public 0.1.
- Later public releases accept earlier public requirements; writing a save stamps
  the writing release conservatively. Requirements are never downgraded automatically.
- Full saves require Internal Full. Untagged legacy scenes/projects require Full.
- Simply opening a public project in Full or storing workspace layout does not
  upgrade its requirement; authoring saves do.
- QDR container 1.2 and project manifest v4 carry the release requirement.
- Distribute the public executable with its required runtime/resources. Do not zip
  the whole development output folder: it also contains the complete `quader.exe`.

After building Release, run
`tools/package_public_release.ps1` to create `build/distributions/quader-0.1`.
The packaging tool copies the public executable, required runtime/resources and
dependency notices, validates dependency closure and records file hashes. It excludes
the Full executable and developer artifacts, and refuses to overwrite an existing
package; use `-Destination` for a new output directory or `-PlanOnly` to inspect it.

Automatic downgrading, content stripping, Patreon authentication and licensing are
not part of this release system.
