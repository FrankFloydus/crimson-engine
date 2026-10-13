# Integrating Quader as Crimson Engine's Modeling Mode

Engineering guide — source snapshot inspected on **13 October 2026**.

**Recommended approach:** keep Quader's portable modeling SDK as the geometry owner, introduce a Modeling mode in Crimson's existing editor, and implement Crimson adapters for interaction, scene/history integration, materials, persistence, and BGFX rendering. The most substantial rendering work is reproducing Quader's overlays and keeping them synchronized with live geometry previews.

The end goal is **the complete Quader modeling toolset inside Crimson**, including creation, component selection, transforms, topology operations, arrangement, curves used for modeling, materials, texturing, UV tools, interactive settings, previews, and visual feedback. The box/extrusion workflow below is the first integration milestone, not the final feature scope. A portable SDK method is only part of a tool: gesture acquisition, settings, eligibility, preview, history, rendering, persistence, and failure behavior must also be delivered.

Editable mesh geometry remains **Quader-authored only**. Imported glTF/GLB meshes remain outside component modeling and UV editing. They continue to render, occlude editable geometry, and participate in ordinary scene operations such as placement and transforms. There is no initial "make imported mesh editable" workflow. Complete modeling parity does not require porting Quader's Qt shell, Filament renderer, lighting editor, account/licensing UI, or every native project-management feature. Modeling-specific curve authoring and linked-instance workflows are part of the final target, even though their host integration comes after the first mesh workflow.

The original request mentioned SDL2; the agreed target is **SDL3**, matching Crimson's current code. Unreal Engine's Modeling mode is the interaction analogy: a specialized editing context inside the engine editor. This document does not prescribe Unreal's implementation or claim exact behavioral parity with it.

## Contents

1. [Evidence and current baseline](#1-evidence-and-current-baseline)
2. [Ownership and integration boundaries](#2-ownership-and-integration-boundaries)
3. [Modeling mode and input lifecycle](#3-modeling-mode-and-input-lifecycle)
4. [Build and consume the portable SDK](#4-build-and-consume-the-portable-sdk)
5. [Modeling operations and live publication](#5-modeling-operations-and-live-publication)
6. [Scene history and persistence](#6-scene-history-and-persistence)
7. [Materials, texturing, and UV operations](#7-materials-texturing-and-uv-operations)
8. [Recreating overlays in BGFX](#8-recreating-overlays-in-bgfx)
9. [Retention, revisions, and resource lifetime](#9-retention-revisions-and-resource-lifetime)
10. [Crimson observations and implementation sequence](#10-crimson-observations-and-implementation-sequence)
11. [Acceptance and regression checklist](#11-acceptance-and-regression-checklist)
12. [Source map and further reading](#12-source-map-and-further-reading)

Detailed navigation: [Tool Settings layout](#35-how-tool-settings-will-look), [all native parameter groups](#38-complete-native-tool-settings-reference), [full tool/operation inventory](#51-coverage-inventory-and-meaning-of-complete), [linked instances](#63-linked-instances-arrays-and-hierarchy-in-the-complete-port), [Face Texture dock](#75-the-face-texture--uv-dock), [all texture actions](#76-complete-texture-action-inventory), [tool-specific visual feedback](#811-tool-specific-visual-feedback), and [completion ledger](#104-a-concrete-completeness-ledger).

## 1. Evidence and current baseline

Two checkouts were inspected:

| Project | Location | HEAD at inspection |
| --- | --- | --- |
| Quader | `C:/Users/Drako/Desktop/quader-windows/quader-app` | `a6a010f9ce7a3fa3ee0dc744e01c91e7831cb289` |
| Crimson | `C:/Users/Drako/Desktop/_CRIMSON_ENGINE/crimson-engine` | `654f992ed0c0304a757749d3aa04d3aa26030be4` |

Both checkouts contained local changes during the original inspection. These commit IDs locate that baseline, but do not identify every inspected working-tree byte. The expanded tool/settings audit on the same date inspected Quader at `70659f038c572859f28155c5ad74e73909524e1b` with a clean working tree. Crimson observations below retain the original snapshot: it was undergoing a resource/material transition, with new source files referenced by consumers while build integration and older callers were being updated. Recheck current interfaces before implementation. The observations below are not a build verdict.

**Evidence labels used throughout:** "current" or "verified" means read in the inspected source; "recommended" or "proposed" means a design for the integration. Code examples are illustrative and were not compiled as part of writing this guide. Proposed adapter names are not existing SDK interfaces. No engine feature or shader was implemented, and no application build or GPU acceptance run was performed for this document.

Quader already exposes a portable C++20 library through `quader/modeling/modeling.hpp` and `quader::modeling_sdk`. Its native application remains Qt/Filament, but those dependencies are outside the SDK-only build. Crimson currently uses SDL3, BGFX with Direct3D 11, Dear ImGui docking, EnTT scene data, engine-owned math, and an existing scene editor. [Q1], [Q2], [C1], [C2]

The inspected installed dependency snapshot includes SDL 3.4.8, BGFX API 129 / package `1.129.8940-496#1`, Dear ImGui 1.92.8, and ImGuizmo 1.10. Crimson's manifest baseline is `d0c191d142b7bc048141ddfb618aa9ae5dba0233`. Compile new shaders with the matching BGFX `shaderc` already selected by Crimson; current online BGFX documentation can describe a newer API than that installed package. [C2], [C3]

## 2. Ownership and integration boundaries

### 2.1 One modeling owner, several derived views

Use **one `quader::modeling::ModelingSession` per resident editable scene**. It survives entry and exit from Modeling mode. Its `PolygonDocument`s own editable topology and component selection. A Crimson binding table connects SDK objects to persistent authored mesh identities and canonical scene members. Initially each independent mesh entity has its own authored identity. For full linked-instance parity, one canonical member document can serve several occurrence entities; see section 6.3. Do not create separately mutable documents for occurrences that are meant to share edits.

Keep the dependency direction explicit:

```text
SDL3 events / ImGui controls
             |
             v
Crimson mode and tool controller
             |
             v
Crimson modeling command + publication adapter
       |                         |
       v                         v
Quader ModelingSession     Crimson scene/assets/history
       |
       v
immutable authored/preview snapshots
       |
       +--> render mesh compilation --> BGFX mesh resources
       +--> semantic overlay extraction --> BGFX overlay resources
       +--> component picking / bounds
```

Renderer and UI caches are disposable projections. They do not become independently editable copies of topology. A candidate document used to prepare an operation is legitimate temporary state; publishing it through the session ends that preparation. Avoid maintaining a second permanent "editor mesh" beside the SDK document.

### 2.2 Responsibility map

| Responsibility | Reuse from Quader | Crimson responsibility |
| --- | --- | --- |
| Polygon topology, IDs, UV corners, normal-edge policy | `PolygonDocument` | Bind authored objects to scene entities |
| Geometry and UV operations | Public SDK operations and preparation functions | Resolve parameters, targets, coordinate spaces, and tool gestures |
| Component selection | SDK document/session selection | Mode eligibility, hit testing, hover, selection intent |
| Preview candidate state | Session preview APIs | Present the same accepted candidate to every consumer |
| Scene hierarchy and placement | No native scene import required | Existing scene/document owners |
| Whole-editor undo | SDK supplies modeling state transitions | Extend the existing chronological scene history |
| Material images and PBR assets | Face material slots and neutral payloads | Asset IDs, texture loading, samplers, GPU materials |
| Editable mesh storage | Authored-topology serialization | Asset envelope, scene references, atomic saves |
| Lines, points, fills, outlines | Native Quader behavior and fixtures | BGFX shaders, passes, buffers, masks, caching |
| Panels and events | Geometry/settings semantics | SDL3 input adapter and ImGui presentation |

In Crimson, compose these responsibilities beside `EditorSession`, `SceneEditor`, and `SceneViewport`; extend `SceneDocument` for history and `SceneExtraction` for immutable renderer input. Put BGFX implementation under its existing renderer boundary. Keep `App` focused on lifetime and the frame loop. The public SDK must not acquire SDL, ImGui, EnTT, or BGFX dependencies. [C1], [C4], [C5], [C6]

Suggested implementation roles are a **modeling mode controller**, a **session/publication adapter**, an **authored mesh asset serializer**, an **overlay extractor**, and a **BGFX overlay cache/pass**. These are ordinary concrete owners with explicit dependencies, not a new plugin framework or service locator.

### 2.3 Why own `ModelingSession` directly

The higher-level `ModelingApi` is useful for small fluent examples, but it does not currently expose its underlying session/documents publicly. The UV preparation APIs need `PolygonDocument` snapshots. Direct ownership of `ModelingSession` therefore provides the required boundary without internal headers or a second parallel session. [Q3], [Q4]

Do not infer implemented functionality from API names alone:

- `ToolsApi::handle_frame` currently returns an unchanged result without processing the supplied viewport/input/camera.
- Box footprint/height drag helpers are placeholders; transform convenience tools do not all provide native-style live previews.
- Knife rays alone do not resolve a stroke; actual `KnifeTarget`s must be supplied.
- The retained pivot/profiling convenience facades do not provide portable native-editor implementations.
- SDK semantic overlays currently emit closed wire loops per face, including duplicated shared edges. They do not supply a complete selected/hovered vertex/edge/face presentation.
- `RenderApi::scene_snapshot` is a convenience output, not Crimson's complete scene/material/identity contract.

Use the SDK for its implemented geometry/session contracts, and write the host interaction around those contracts. [Q4], [Q5], [Q6], [Q35]

### 2.4 Coordinates and precision

Adopt **one SDK unit = one Crimson meter**. Both systems align naturally around right-handed, Y-up geometry. Convert Crimson matrices explicitly to `Transform3`'s X/Y/Z axes and origin; do not reinterpret memory between unrelated math types. Preserve full affine placement, including nonuniform scale and reflection.

SDK public positions and authored UVs are floats. Some UV settings and affine canvas calculations use doubles, which does not make the stored mesh double precision. Keep geometry near its local origin. Test very small and very large local extents before claiming support for them.

Quader's native display conventions include 32 map units per world unit and 64 map units per texture tile; these are not a reason to scale Crimson geometry by 32. UV Peel world mode explicitly starts from 0.5 tiles per world unit. Keep tool distances, snapping, tolerances, and density conversions in one host adapter. [Q7], [Q8]

## 3. Modeling mode and input lifecycle

### 3.1 Separate four pieces of state

| State | Meaning | Lifetime |
| --- | --- | --- |
| Editor mode | Ordinary scene editing or Modeling | Transient editor context |
| Selection kind | Object, vertex, edge, or face; curve controls in their own eligible context | Remembered Modeling preference; object selection stays with Crimson |
| Active tool | Selection, transform, extrusion, UV action, etc. | Mode/tool controller |
| Active preview | One accepted candidate operation and its participants | Explicit begin/update/accept/cancel |

The SDK remains independent of editor mode. Its documents survive mode changes. Mode metadata records participants and context generations, while the document remains the owner of actual component selection. Quader's native mode service is a useful behavioral reference, but it is not part of the installed SDK. [Q9]

### 3.2 Recommended transition contract

- **Enter Modeling with no eligible selection:** enter an empty context and allow primitive creation. Do not require an existing mesh just to open the mode.
- **Enter with authored meshes selected:** resolve visible, unlocked, eligible authored entities. Imported model entities are never component targets. Mixed selections enroll only eligible authored meshes.
- **Create a primitive:** produce a provisional SDK document and matching scene presentation; Accept publishes both as one command, Cancel removes both.
- **Change component kind:** retain the mode, clear incompatible hover/capture, and settle an incompatible pending tool before switching.
- **Exit with an active preview:** require an explicit Accept or Cancel decision before completing the transition. A rejected Accept leaves the mode and accepted draft intact. This is a recommendation for Crimson, not a claim about every Quader native transition.
- **Exit with no pending tool:** retire Modeling overlays and input capture. Keep authored meshes, scene transforms, materials, and history intact. A mode switch itself is not an authored history command.
- **Replace or reload a scene:** use Crimson's existing dirty-document decision, then retire previews, deferred picks, and old bindings before publishing the new scene. Never let a delayed callback mutate the replaced scene.

Mode generation must advance when the context is retired, including a leave-and-reenter round trip. Captured gestures carry scene generation, mode generation, target identity, and the relevant document/preview revision. Reject obsolete results even if an entity with a similar name is now selected.

### 3.3 Input ownership

Forward SDL events to the ImGui platform backend consistently. ImGui's capture flags describe whether underlying application input should be suppressed, but Crimson's docked viewport is itself an ImGui item: route editor viewport interaction through its explicit item/focus/capture owner. A blanket `if (!WantCaptureMouse)` would also disable intended interaction over that viewport. Popups, active text fields, property controls, gizmos, navigation, and modeling tools must have a single arbitration policy. [E3], [C6]

At pointer press, record the owning tool, button/modifiers, viewport rectangle, and drag basis. Keep that ownership through release even if the cursor leaves the viewport. Focus loss releases raw capture and cancels an in-progress pointer gesture back to its pre-gesture state; it need not discard an already accepted multi-step tool draft. Escape invokes the appropriate tool cancellation boundary.

Translate viewport-relative logical mouse coordinates to attachment pixels once. Crimson already sizes its offscreen viewport from ImGui logical dimensions and framebuffer scale. Use the same physical viewport for picking tolerance, line width, and projection. SDL's window pixel size is useful for the native drawable, but it is not a substitute for the docked viewport attachment's dimensions. [C6], [E2]

### 3.4 Component picking

Start with host-side CPU picking against immutable authored/preview snapshots, with a spatial acceleration structure rebuilt or refitted only when its source changes. Faces need a triangulation-to-`FaceId` mapping; edges and vertices use screen-pixel distance thresholds and stable authored IDs. Transform rays into mesh-local space consistently, then compare hits in a common world-space metric under scaled occurrences.

Preserve visibility, backface, and occlusion policy. Imported meshes cannot become component targets, but they still occlude modeled components in ordinary visible-only picking. If the CPU representation cannot reproduce material alpha cutouts accurately, add a depth/pick reconciliation step before claiming visible-pixel parity. Do not substitute an object's bounding box for its actual occluding surface.

Existing Crimson GPU object picking uses dense draw IDs mapped back to persistent entity IDs and asynchronous readback validation. Preserve that system for object selection. A later GPU component path can extend its ID tables and stale-result fences, rather than encoding persistent 64-bit IDs in float colors. Add component kind and preview/mode generation to the existing camera/viewport/scene checks. [C6], [C7]

Keep hover in the Crimson interaction owner, distinct from committed SDK selection. In the inspected convenience facade, `SelectionApi::apply_hover` actually replaces selection, `preview_hover` echoes an already host-resolved target, and `clear_hover` does not implement an independent hover store. These names are not a native picking/hover system. [Q48]

### 3.5 How Tool Settings will look

**Proposed Crimson presentation:** a dockable **Tool Settings** panel shown in Modeling mode, beside the viewport and separate from the scene Inspector. The modeling palette selects a tool; this panel shows only that tool's applicable parameters. Keep Crimson's existing Dear ImGui styling and docking persistence. Quader's native presentation is a retained viewport overlay; reproducing its geometry behavior does not require copying its Qt window layout. [Q36], [Q37], [Q38]

```text
+---------------------+----------------------------------+-------------------------+
| Modeling            | Scene viewport                   | Tool Settings           |
| Object Vertex Edge  |                                  | v Extrude               |
| Face                |  BGFX shaded candidate           | Type       [Region v]   |
|                     |  authored wires                  | Amount (m) [ 0.2500  ]   |
| Create              |  selected-face tint              |                         |
| Transform           |  drag handle / direction guide   | [Accept] [Cancel]       |
| Topology            |                                  |                         |
| Arrange             |  ImGui: numeric HUD / labels     | Face Texture tab/panel  |
| UV / Texture        |                                  | when editing face UVs   |
+---------------------+----------------------------------+-------------------------+
```

This wireframe describes proposed placement, not a rendered or implemented Crimson UI. The scene Inspector continues to expose entity placement and shared material resources. **Tool Settings describes the active operation. Face Texture describes selected faces and their UV mapping.** Avoid placing unrelated object properties between a tool's parameters and its acceptance controls.

Use a compact two-column ImGui table with a stable label column and expanding value column. Use checkboxes for toggles, combos or short button groups for choices, integer editors for counts, signed numeric editors for distances/angles, and grouped XYZ rows for vectors. Use collapsing groups for Path Array's Placement, Rotation, and Scale. Long parameter sets scroll within the body; keep active preview Accept/Cancel reachable below it. A narrow dock must retain usable numeric fields; move vector axes onto separate rows when necessary.

Distances are shown in Crimson meters, angles in degrees, UV repeats/offsets in tiles, and texture shifts in pixels when that control explicitly says pixels. Show useful precision without rounding the authoritative value each frame. A float-range distance must use typed entry/scrubbing, not a slider across its entire numeric range. Sliders are appropriate for meaningful bounded intervals such as bevel profile 0–1.

Not every action needs a panel. Immediate actions such as Flip Normals execute one command. Align has action buttons and options but no required preview footer. A parameterless interactive tool may need Accept/Cancel and a short viewport input hint without an empty property grid. Rotate/Scale do not currently have a native generic XYZ Tool Settings inspector, and Mirror currently has no parameter controls in this native descriptor switch. Additional Crimson numeric transform fields are a host design choice, not an existing portable settings feature.

### 3.6 Settings ownership, descriptors, and widget dispatch

The native reference already separates settings data from widget code. `QuaderToolSettingsTarget` describes groups and `Toggle`, `Number`, `Choice`, `Action`, and `Vector3Number` controls; `QuaderToolsApi` builds it; the Qt panel renders it generically. These native types and services are **not the installed portable SDK UI**. Create a small Crimson-owned equivalent, seeded from SDK operation descriptors where those exist, and extend it for native tool behavior that Crimson must own. [Q36], [Q37], [Q38], [Q39]

The proposed descriptor needs a stable tool/operation ID, activation generation, originating viewport identity, field IDs, labels, value types/units, current accepted values, choice IDs, group IDs, soft display ranges, hard validation limits, enabled/visible state, and owner-provided Accept/Cancel availability. Carry an operation diagnostic separately from the accepted value. Do not infer eligibility from selection counts alone: an apparently valid count can still describe disconnected, degenerate, or incompatible geometry.

Keep these five states distinct:

1. **Remembered tool options:** convenient starting values for the next activation.
2. **Committed authored documents:** persistent modeling truth.
3. **Captured operation baseline:** immutable geometry, participants, selection, placement, and random seed for this activation.
4. **Latest accepted candidate:** the coherent preview resulting from valid parameters.
5. **Uncommitted field draft:** transient text such as `-`, `0.`, or an incomplete expression, owned by the field until interpreted.

An incomplete field draft is legitimate UI state; it is not a valid geometry parameter. Repeated amount edits must extrude the captured source to the requested amount, not extrude the previous preview again. Randomized array previews keep their seed fixed while a field changes, so changing count or spacing does not unpredictably reshuffle all copies.

Illustrative C++/ImGui architecture, **pseudocode using proposed Crimson types**, not a compilable SDK example:

```cpp
// Draw every frame; derive descriptors only when the owner's revision changes.
const ToolSettingsView& view = modeling_ui.cached_settings();
ImGui::PushID(view.activation_widget_key.c_str());
if (ImGui::BeginTable("parameters", 2)) {
    for (const SettingView& field : view.fields) {
        ImGui::PushID(field.id.c_str());
        draw_label_and_typed_editor(field, [&](SettingEdit edit) {
            // Carries scene/tool/activation identity and a per-gesture token.
            modeling_controller.queue_setting_edit(view.context, field.id, edit);
        });
        ImGui::PopID();
    }
    ImGui::EndTable();
}
// The controller resolves cancellation before flushing any pending field edits.
draw_owner_driven_accept_cancel(view, modeling_controller);
ImGui::PopID();

// Outside presentation: consume at most the latest dirty sample per frame.
modeling_controller.evaluate_pending_settings_from_baseline();
// Success publishes one candidate revision to mesh, overlays, depth and picking.
```

The callback only expresses an edit intent. Geometry preparation, parameter normalization, validation, publication, and history belong to the operation controller/session adapter. Cache descriptors by settings/availability/selection revision; ImGui's immediate-mode draw loop is not permission to rebuild the catalog or traverse selected topology every frame.

### 3.7 Field edits, preview, acceptance, and cancellation

At activation, capture targets and assign a new generation. Widget IDs and queued edits include that generation plus field identity. Bind the operation to its originating viewport context; focusing another pane does not silently change the plane/camera basis or start a second operation. A scene/tool replacement invalidates old field buffers, delayed calculations, and callbacks.

During a scrub, queue the latest valid value and evaluate at most once per rendered frame, or at a bounded cadence if evaluation is expensive. Quader's native panel uses a 16 ms sample cadence; that is reference behavior, not a requirement to create a separate timer in Crimson. Coalesce intermediate samples. Always evaluate the final released value. Threaded preparation must pin immutable inputs and publish only if the current operation generation and requested-parameter revision still match.

Text edits commit an interpreted value on Enter or deliberate focus settlement; Escape discards the field draft. Preserve incomplete text while focused. When the owner normalizes a parameter, show its accepted result after the edit settles. When a candidate fails, retain the last accepted geometry and show a concise operation error; never leave the requested value visually presented as accepted when its geometry was rejected.

**Accept** first validates and flushes the focused draft and final queued sample, then rechecks context identity and commits one coherent operation. Invalid text or failed preparation blocks acceptance and preserves the draft. A pending latest calculation cannot be skipped just because an older valid preview exists. **Cancel** discards queued field samples before any focus-loss event can apply them, cancels the preview, and retires provisional entities. This ordering prevents a click on Cancel from committing the very value the user is abandoning. [Q38]

Continuous immediate-property edits, such as changing an already existing curve's radius, need one gesture merge key in Crimson's chronological history. Preview parameter samples usually create no history command until Accept. Discrete immediate actions remain discrete. An invalid/no-op candidate creates none. Collapsing the panel settles its field draft but does not finish the tool. Scene replacement follows the broader dirty-scene lifecycle and cancels stale work.

Remembered options and canceled geometry have different lifetimes. Quader retains extrusion/primitive choices; cancellation does not necessarily reset their defaults. Path Array retains authored options while operation-local sources/path/random context change. Selected curve properties are document data, distinct from defaults for a new curve. Define and test each tool's behavior instead of using one global "reset everything on cancel" rule. The native reset API exists, but its Tool Settings panel does not display a universal Reset Settings button; add one in Crimson only as a deliberate presentation choice.

### 3.8 Complete native Tool Settings reference

The following is the inspected native parameter surface. Defaults mean initial owner defaults; remembered options may replace them. Ranges mean UI ranges unless identified as hard constraints. Re-read owner validation before implementing: geometry can narrow a legal value, and a soft slider range is not a mathematical limit. World-distance defaults require explicit unit policy; do not copy native display numbers without their conversion. [Q37], [Q39], [Q40], [Q41], [Q42], [Q43], [Q44]

| Tool / context | Controls and initial values | Bounds, conditions, and Crimson work |
| --- | --- | --- |
| Box | X/Y/Z Subdivisions = 1; Flip Faces off | Subdivisions 1–128. Viewport construction owns physical dimensions in native Quader. |
| Cylinder | Sides = 32; Caps on; Flip Faces off | Sides 3–128. |
| Cone | Sides = 32; Base Cap on; Flip Faces off | Sides 3–128. |
| Sphere | Type = UV Sphere; Radial Segments = 32; Rings = 16; Hemisphere off; Flip Faces off | UV radial 3–128, rings 2–128. Quad Sphere instead shows Subdivision = 3, range 1–6; native release availability can filter this choice. |
| Torus | Major Segments = 32; Minor Segments = 12; Tube Radius Ratio = 0.2; Flip Faces off | Segment ranges 3–128; ratio 0.01–0.49. |
| Extrude | Type = Region, with Along Normals and Individual choices; Amount = 0 | Signed finite amount, display step 0.0001; native fields disabled during pointer drag. Native displayed map units are divided by 32 before geometry. Crimson displays meters directly. |
| UV Peel | Axis = U; World Density on; U/V Repeat = 1; U/V Offset = 0 tiles | Repeats strictly positive; offsets signed; step 0.1. Apply requires valid preview. |
| Move in Vertex mode | Auto Merge Vertices on | Not shown for other component modes; merge tolerance belongs to actual policy, not an invented universal setting. |
| Lasso Select | Enable select partial intersection off | Host selection hit-policy option. |
| Cut | Keep Mode = Keep Both; Cap off | Other choices Discard Left / Discard Right; owner can disable controls by phase/selection. |
| Bridge | Steps = 1; Start Tension = 1; End Tension = 1 | Steps 1–64; tensions 0–3, step 0.01. |
| Edge Bevel | Width = 1; Segments = 1; Profile = 0.5 | Width nominal 0–512 but accepted geometry may constrain maximum; panel step 0.01, SDK schema step 0.001. Segments 1–16; profile 0–1. No native profile-type selector here. |
| Thicken / Create Outer Hull | Distance = 0.25; From Center off | Distance soft interval −4…4, step 0.01; descriptor `hard_limits=false`. Do not reject larger valid distances solely from that display range. |
| Slice Quad | X = 1; Y = 1 | Each 0–64. |
| Merge by Distance | Tolerance = 0.125 | 0–128, step 0.001; tolerance must follow geometry-unit policy. |
| Half-Cut | Axis = X; Separate Parts off; Fill Parts on | Axis X/Y/Z; structural preview can create more objects. |
| Pivot | Prev / Next actions; Vertex, Edge Midpoint, Face Center, Grid toggles on | Snap-candidate cycling; native Apply/Cancel lifecycle. |
| Align | Align X/Y/Z with Min/Center/Max actions; Distribute X/Y/Z; Use Snapping off | Owner-gated immediate actions; no mandatory preview footer. |
| Linear Array | Num Copies = 4; Bounding Box and Dimensions on | Count 2–128 nominal; owner can lower maximum. |
| Radial Array | Total/Spacing mode = Total; angle = 360°; Number of Copies = 8; Repeats = 1; Repeat Spacing initially owner-derived; Create Instances off; Bounding Box and Dimensions on | Angle 0–360°; copies 2–128; repeats 1–512; spacing 0–16384. Combined output budget reduces allowed counts. Instance mode needs shared-asset semantics, not ordinary duplication. |
| Path Array: Placement, initially open | Num Copies = 4; Distance initially fitted to path; Follow Path on; Forward Axis = +X; Bounding Box and Dimensions on | Count 2–128 and total budget; distance 0…owner maximum, step 0.001. Axis ±X/±Y/±Z disabled when Follow Path is off. |
| Path Array: Rotation, initially collapsed | Offset per Copy XYZ = 0°; Random Minimum XYZ = 0°; Random Maximum XYZ = 0° | Each −360…360°, step 0.1; stable operation seed. |
| Path Array: Scale, initially collapsed | Scale Factor per Copy XYZ = 1; Random Minimum XYZ = 1; Random Maximum XYZ = 1 | Each 0.001–10, step 0.01. |
| Curve while drawing | Curve Type = Draw, Circle, or Oval; Draw has Closed off | Circle radius 1, range 0.001–100000. Oval radius 1 and length 4; radius ≤ length/2, length ≥ 2×radius, length ≤ 200000. Tool phase can lock type. |
| Existing curve / curve after drawing | Closed; Solidify; Resolution = 8; Radius = 0.1; eligible Apply Solidify / New Curve actions | Resolution 3–128; radius 0.001–100000; both disabled unless Solidify enabled. Persist selected curve properties separately from creation defaults. |

Primitive width/height/depth controls are not present in this native settings matrix: they come from viewport creation/resize interactions. For Crimson, adding explicit meter-valued dimension fields is a useful **proposed completion of the same controller**, especially for precise construction. It must update the same primitive draft as the viewport handles. Likewise, expose inset amount or vertex-bevel amount through a Crimson operation panel when building those tools; their SDK existence does not imply a native generic settings descriptor already exists. These additions must preserve the method's actual options and validation rather than guessing an unrelated DCC's parameter set.

## 4. Build and consume the portable SDK

### 4.1 Produce an isolated Release package

The following is a documented recipe, **not a build performed for this guide**. It uses a separate SDK-only output tree; Quader's native `build/vs2026` remains the native app tree. Use the installed Visual Studio 2026 generator and x64 toolchain. [Q2], [Q10]

```powershell
$quader_source = 'C:/Users/Drako/Desktop/quader-windows/quader-app'
$quader_sdk_build = "$quader_source/build/sdk-crimson"
$quader_sdk_install = "$quader_sdk_build/install"

cmake -S "$quader_source" -B "$quader_sdk_build" `
  -G "Visual Studio 18 2026" -A x64 `
  -DQUADER_BUILD_NATIVE_APP=OFF `
  -DQUADER_BUILD_TESTS=OFF `
  -DQUADER_USE_OPENMESH_TOPOLOGY=OFF `
  -DQUADER_ALLOW_EXTERNAL_OPENMESH_FETCH=OFF

cmake --build "$quader_sdk_build" --config Release `
  --target quader_modeling_sdk_runtime

cmake --install "$quader_sdk_build" --config Release `
  --prefix "$quader_sdk_install"
```

Check each command's exit code before continuing. `quader::modeling_sdk` is an interface consumer target, not the concrete build target. The runtime target brings in the portable geometry, polygon, curve, and QDR libraries. The SDK-only configuration does not require Qt, Filament, SDL, ImGui, EnTT, or OpenMesh. Bundled Earcut and its license material are part of the package; tests require GTest only when enabled.

If supplying Quader's vcpkg toolchain, also set `VCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON` so native default features do not install unrelated application dependencies. With SDK tests enabled, explicitly supply the tests feature/GTest. Pin the SDK source revision and keep its installed public headers and libraries together.

Prefer installation and consumption over adding Quader's complete root CMake project into Crimson, because the root applies global compiler/build policy. The existing SDK-only verification fixture is valuable for future package validation, but it recreates its dedicated fixture directory; it is not the ordinary incremental package-build command. [Q10]

### 4.2 Consume it from a Crimson adapter target

Illustrative CMake addition after defining a proposed `crimson-modeling` target:

```cmake
find_package(QuaderModelingSdk CONFIG REQUIRED)
target_compile_features(crimson-modeling PRIVATE cxx_std_20)
target_link_libraries(crimson-modeling PRIVATE quader::modeling_sdk)
```

Supply the installation prefix through Crimson's existing CMake configuration, for example by adding it to `CMAKE_PREFIX_PATH`. Keep SDK types behind that adapter where practical. Public headers under `include/quader/modeling` are the supported consumer boundary; native Quader's source-only `neutral/` and private `internal/` headers are not installed APIs. [Q1], [Q2]

Begin with **Release Crimson consuming Release SDK**. Match architecture, toolset, CRT, and STL ABI. Quader's Debug-named configuration is an optimized Profile build using `/MD`, `NDEBUG`, and unchecked STL. Linking that directly into an ordinary `/MDd` checked-STL Crimson Debug target is not a supported assumption. Decide and verify a compatible Debug producer/consumer configuration separately. The external-consumer fixture documents the exact ABI treatment. [Q11]

### 4.3 Minimal public API example

This illustrative SDK-only program shows real inspected method names. It is not the production scene transaction, GPU adapter, or exception policy, and was not compiled for this document. The local helper checks both the outer `Result` and the operation receipt.

```cpp
#include <quader/modeling/modeling.hpp>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace qm = quader::modeling;

template <typename Value>
Value require_value(qm::Result<Value> result) {
    if (!result.ok()) {
        throw std::runtime_error(result.error().message);
    }
    return std::move(result).value();
}

qm::OperationResult require_operation(qm::Result<qm::OperationResult> result) {
    auto receipt = require_value(std::move(result));
    if (!receipt.success) {
        throw std::runtime_error(receipt.message);
    }
    return receipt;
}

int main() {
    try {
        qm::ModelingSession session;
        auto box = require_value(qm::PolygonDocument::make_box({
            .min = {-1.0F, -1.0F, -1.0F},
            .max = { 1.0F,  1.0F,  1.0F},
        }));
        auto object = require_value(session.add_document(std::move(box), "Box"));
        auto snapshot = require_value(session.document_snapshot(object));
        auto faces = snapshot->face_ids();
        if (faces.empty()) {
            throw std::runtime_error("Box has no authored faces.");
        }
        require_operation(session.select_face(object, faces.front()));

        auto candidate = require_value(session.document_copy(object));
        auto receipt = require_operation(candidate.extrude_selected_faces({
            .mode = qm::FaceExtrudeMode::Region,
            .amount = 0.25F,
        }));
        if (receipt.changed) {
            require_operation(session.commit_document(
                object, std::move(candidate), receipt));
        }

        auto accepted = require_value(session.document_snapshot(object));
        auto mesh = require_value(accepted->compile_mesh());
        std::cout << mesh.vertices.size() << " render vertices, "
                  << mesh.indices.size() << " indices\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

Production commands surface structured diagnostics and use Crimson's publication/history boundary. SDK calls such as selection may create SDK-local history entries; the host must deliberately absorb or retire those entries rather than exposing a second user-visible Undo stack. [Q3], [Q11]

## 5. Modeling operations and live publication

### 5.1 Coverage inventory and meaning of complete

This inventory cross-checks the **20 native viewport tools**, **43 host action descriptors**, **29 instant-operation entries**, **28 texture commands**, native command presentation/registration, native selection/modeling APIs, and installed SDK headers. These sets overlap; their counts must not be added to produce a unique tool count. The 13-entry internal interaction registry and the SDK's 11-entry convenience tool catalog are both smaller than the native user-facing toolset. A main menu alone also omits searchable commands and contextual gestures. [Q45], [Q46], [Q47], [Q62], [Q63]

This is an inventory of current native source behavior, including entries gated by native release features. It is not a claim that every item appears in every Quader public build or that all have been exercised in a running application for this guide. The complete port should deliberately map these capabilities into Crimson, while checking actual owner behavior and its regression fixtures.

Coverage labels below mean:

| Label | Meaning |
| --- | --- |
| **SDK** | A real installed public computation/document/session API exists; Crimson still supplies interaction and publication. |
| **Compose** | Portable building blocks exist, but Crimson must compose the native workflow. |
| **Host** | Native Quader has an editor/scene controller without an installed equivalent high-level controller. Port its behavioral contract through Crimson's owners. |
| **SDK extra** | A portable capability exists without a corresponding native tool/command found in the inspected catalogs. Exposing it needs an explicit host presentation. |
| **Absent** | No matching implemented feature was found in the inspected modeling catalogs; do not invent a parity promise from a suggestive name. |

All native modeling families in the following tables belong to the final parity target. Implementation order is a dependency sequence, not a reason to silently drop later tools. Keep a port ledger with one row per behavior and columns for computation, input, settings, preview, overlays/picking, history, persistence, tests, and delivery status. An SDK symbol being present only satisfies the computation column.

#### Native interactive tools: all 20

`tools.activate.<id>` is the native activation command unless the command column says otherwise. These native command IDs are useful reference identifiers; they are not portable SDK dispatch strings.

| Tool ID / label | Native command | Reuse and work required in Crimson |
| --- | --- | --- |
| `select` / Select | `tools.activate.select` | **Compose:** exact SDK selection; click/rectangle acquisition, active target, hover, occlusion and selection modifiers. |
| `lasso_select` / Lasso Select | `tools.activate.lasso_select` | **Compose:** lasso gesture, projected inclusion, partial-intersection option, SDK batch selection. |
| `paint_select` / Paint Select | `tools.activate.paint_select` | **Compose:** stroke sampling, brush hit policy and accumulated selection; preserve pointer sample order. |
| `move` / Move | `tools.activate.move` | **Compose:** prepared component transforms; gizmo/axis/plane constraints, snapping, duplicate/extrude gesture and auto-merge policy. |
| `extrude` / Extrude | `tools.activate.extrude` | **Compose:** typed extrusion; Region/Along Normals/Individual, signed amount, drag frame and coherent topology preview. |
| `rotate` / Rotate | `tools.activate.rotate` | **Compose:** affine component/object transform, pivot, signed angle and snapping, preview. |
| `scale` / Scale | `tools.activate.scale` | **Compose:** affine transform, axis/uniform constraints, pivot, zero/negative/reflected cases. |
| `create_primitive` / Create Primitive | `tools.activate.create_primitive` | **Compose:** all five primitive families. This compatibility launcher currently aliases Box and labels itself Create Box; it is not a sixth shape. |
| `insert_edge_loop` / Insert Edge Loop | `tools.activate.insert_edge_loop` | **Compose:** SDK loop insertion; seed picking, loop hover, insertion factor, snapped preview. |
| `knife` / Knife | `tools.activate.knife` | **Compose:** SDK segment/stroke execution; resolve topology targets, snap points, display stroke, finish/cancel. |
| `cut` / Cut | `tools.activate.cut` | **Compose:** plane cut/split; construction plane, keep-side/cap settings and object/component scope. |
| `poly` / Poly | `tools.activate.poly` | **Compose:** `make_face`; plane/point acquisition, snapping, polygon closure and candidate validation. |
| `mirror` / Mirror | `tools.activate.mirror` | **Compose:** `reflect_across_plane`; drawn plane, replacement/duplication semantics, occurrence/material compensation. |
| `uv_peel` / UV Peel | **`modeling.uv_peel`** | **Compose:** portable UV Peel preparation; selected-edge guides, settings, UV preview and acceptance. |
| `pivot` / Pivot | `tools.activate.pivot` | **Host:** candidate snapping/cycling, persistent origin/pivot changes, scene/UV compensation; SDK convenience PivotTool is not this controller. |
| `linear_array` / Linear Array | `tools.activate.linear_array` | **Host:** capture sources, span endpoints, count and axis constraints; coherent transform/structural preview. |
| `radial_array` / Radial Array | `tools.activate.radial_array` | **Host:** center, angular mode, copies, repeats/spacing and linked-instance choice. |
| `path_array` / Path Array | `tools.activate.path_array` | **Compose:** `CurveArcLengthPath`; source/path acquisition, distance, orientation, variation, hierarchy and atomic publication. |
| `curve` / Curve | `tools.activate.curve` | **Compose:** portable curve document/tube APIs; control points/handles, Draw/Circle/Oval, close/smooth/solidify workflows. |
| `align` / Align Tool | `tools.activate.align` | **Host:** world-bounds alignment/distribution, action settings and grid policy. |

Additional launchers select a variant of these tools: `tools.activate.create_box`, `create_cylinder`, `create_cone`, `create_sphere`, `create_torus`, `create_curve`, `extrude_individual`, and `extrude_along_normals` (each abbreviated name after the first retains the `tools.activate.` prefix). Shared native lifecycle targets include `tools.primitive.apply`, `tools.primitive.cancel`, `tools.primitive.flip_faces`, `tools.primitive.toggle_caps`, `tools.primitive.toggle_hemisphere`, `modeling.apply_active_preview`, and `modeling.cancel_active_preview`. Route aliases into one owner per behavior rather than creating duplicate tool implementations. [Q63]

#### Creation, component transforms, and extrusion

Unqualified methods below are members of `quader::modeling::PolygonDocument`; native IDs are listed only where a corresponding route was verified. These are real public methods, not promises of complete interactive controllers. [Q3], [Q4], [Q5]

| Behavior | Public SDK entry points | Integration detail |
| --- | --- | --- |
| Box / subdivided box | `make_box`, `make_box_from_corners` | `BoxSpec` bounds, subdivisions, material and winding; host placement and provisional entity. |
| Cylinder / cone | `make_cylinder`, `make_cone` | Side count, dimensions, caps and winding. |
| Sphere / hemisphere | `make_sphere`, `SphereSpec`, `SphereType::Uv`, `SphereType::Quad` | Preserve distinct UV/quad topology and hemisphere options. |
| Torus | `make_torus`, `TorusSpec` | Major/minor segments, outer dimensions and tube ratio. |
| Polygon drawing | `make_face` | Resolve coplanar point sequence and material; draw-point interaction is host work. |
| Exact authored mesh construction | `from_authored_topology`, `authored_topology` | Preserve loops/corner attributes/IDs; do not rebuild from triangles. |
| Move / arbitrary affine transform | `translate_selection`, `transform_selection`, `set_vertex_positions` | Correct selection mode and local/world conversion; object placement may be a Crimson scene operation instead. |
| Stable transform preview | `prepare_selection_transform`, `PreparedSelectionTransform::evaluate_translation`, `evaluate_transform` | Capture baseline once; evaluate requested translation or affine transform repeatedly. |
| Rotate / scale | `transform_selection`; `OperationsApi::rotate_selection`, `scale_selection` conveniences | Host pivot, basis, constraints and gesture. |
| Plane mirror | `reflect_across_plane` | Distinct from view-relative flips and texture flips. |
| Axis mirror / convenience flips | `OperationsApi::mirror_selection`, `flip_horizontal`, `flip_vertical` | Convenience flips use X/Y; native view-relative commands require host conversion. |
| Vertex/edge element extrusion | `extrude_selected_elements`, `transform_extrude_selected_elements` | The transformed variant includes pivot/grid-dependent support-width behavior. |
| Transform-based element inset | `inset_selected_elements` | Distinct from scalar face inset; **SDK extra** if exposed as a separate tool. |
| Face inset | `inset_selected_faces` | **SDK extra:** no standalone native Inset tool/action found in the inspected catalogs. Add a real amount/settings/preview controller if exposed. |
| Face extrusion | `extrude_selected_faces(FaceExtrudeOptions)` | Typed Region/AlongNormals/Individual semantics. |
| Repeated face-extrusion preview | `prepare_face_extrusion`, `PreparedFaceExtrusion::evaluate`, `selected_face_extrude_frame` | Honor preparation identity lifetime and zero/negative amount transitions. |

Use typed face extrusion. The legacy `extrude_selected_faces(float)` intentionally retains a different individual-face/open-base contract. A convenient signature can still select the wrong behavior. Native transform shortcuts can duplicate an object or extrude components before moving/rotating; these are compound host intents, not plain transforms.

#### Vertex operations

Native ID suffixes in this table use `modeling.`. Slashes separate aliases, not additional algorithms. [Q3], [Q46], [Q47]

| Behavior | Public SDK method | Native suffix / port detail |
| --- | --- | --- |
| Snap to active | `snap_selected_vertices_to_active` | `snap_vertices_to_active`; active vertex must belong to eligible context. |
| Merge to active | `merge_selected_vertices_to_active` | `merge_vertices` / `merge_vertices_to_active`. |
| Merge at center | `merge_selected_vertices_to_center` | `merge_vertices_at_center` / `merge_vertices_to_center`. |
| Merge by distance | `merge_selected_vertices_by_distance` | `merge_by_distance`; parameterized preview. |
| Auto Merge | `auto_merge_selected_vertices` | Move/transform completion policy; not a separate replacement for every merge command. |
| Remove Doubles | `remove_double_vertices` | `remove_doubles`; distinct cleanup contract. |
| Bevel | `bevel_selected_vertices` | `bevel_vertices`; do not substitute the edge-bevel parameter schema. |
| Connect | `connect_selected_vertices` | `connect_vertices`. |
| Dissolve | `dissolve_selected_vertices` | `dissolve_vertices`; preserve incident face/attribute semantics. |
| Radial align | `radial_align_selection` | `radial_align_vertices`; uses current component mode. |
| Snap to floor | Host surface query plus vertex transforms | `snap_vertices_to_floor`; hit policy/occluders and atomic publication are host work. |

#### Edge operations

| Behavior | Public SDK method | Native `modeling.` suffix / port detail |
| --- | --- | --- |
| Snap to active | `snap_selected_edges_to_active` | `snap_edges_to_active`. |
| Connect | `connect_selected_edges` | `connect_edges`. |
| Split | `split_selected_edges` | `split_edges`. |
| Dissolve | `dissolve_selected_edges` | `dissolve_edges`. |
| Merge | `merge_selected_edges` | `merge_edges`. |
| Collapse | `collapse_selected_edges` | `collapse_edges`. |
| Fill Hole | `fill_hole_from_selected_edges` | `fill_hole`; selected boundary eligibility matters. |
| Bevel | `bevel_selected_edges(EdgeBevelSpec)` | `bevel_edges`; width, segments, profile and accepted-width feedback. |
| Bridge | `bridge_selected_edges(BridgeOptions)` | `bridge_edges` immediate / `preview_bridge_edges` preview. Steps and independent endpoint tension. |
| Reset normal policy | `reset_selected_edge_normals_to_default` | `default_edge_normals`. |
| Harden / soften normals | `harden_selected_edge_normals`, `soften_selected_edge_normals` | `harden_edge_normals`, `soften_edge_normals`. |
| Radial align | `radial_align_selection` | `radial_align_edges`. |
| Insert loop | `insert_edge_loop` | Interactive tool above; seed and factor are not supplied by a mouse ray alone. |

The facade's `EdgeSelection::bridge_pairs` and `bridge_boundaries` currently both call `bridge(options)`. Their names are not independent mode selectors that force different pairing behavior. Preserve the actual boundary classification/diagnostic contract. [Q5]

#### Face, cut, normal, and structural geometry operations

| Behavior | Public SDK method / composition | Native `modeling.` suffix / port detail |
| --- | --- | --- |
| Bridge faces | `bridge_selected_faces(BridgeOptions)` | `bridge_faces` / `preview_bridge_faces`. |
| Combine / collapse faces | `combine_selected_faces`, `collapse_selected_faces` | `combine_faces`, `collapse_faces`. |
| Radial align | `radial_align_selection` | `radial_align_faces`; generic `radial_align` route also exists. |
| Flip normals | `flip_selected_face_normals` | `invert_faces`, `invert_mesh`, generic `flip_face_normals`; object route selects the intended whole-mesh scope. |
| Recalculate normals | `recalculate_selected_face_normals` | `recalculate_face_normals`. |
| Smooth / flat shading | `shade_selected_faces_smooth`, `shade_selected_faces_flat` | `shade_faces_smooth`, `shade_faces_flat`; object aliases `shade_smooth_mesh`, `shade_flat_mesh`. |
| Slice quads | `slice_selected_quads` | `slice_quad`; X/Y counts, not a general remesher. |
| Thicken | `thicken_selected_faces` | `thicken_faces`; distance and from-center behavior. |
| Create Outer Hull | `OperationsApi::create_outer_hull` delegates to thicken | `create_outer_hull`; whole-object thicken-based behavior, not convex hull or CSG. |
| Detach faces | `detach_selected_faces` | `detach_faces`; topology change within the document. |
| Extract faces | `extract_selected_faces` | `extract_faces`; returned geometry needs a provisional/new authored asset and scene entity. |
| Delete components | `delete_selection` | Native `edit.delete` is context-sensitive and also handles objects. |
| Knife segment / stroke | `knife_segment`, `knife_stroke` | Resolved `KnifeTarget`s, stroke previews and failure diagnostics are required. |
| Split by plane into parts | `split_by_plane(PlaneSplitSpec)` | Reconcile all resulting parts, placement and material slots. |
| Surface plane cut | `cut_surface_by_plane` | Surface-cut contract, distinct from separated parts. |
| Scoped plane cuts | `cut_selected_faces_by_plane`, `cut_selected_edges_by_plane` | Explicit component scope/keep/cap semantics. |
| Half-Cut | `split_by_plane` plus structural session preview | `half_cut`; **Compose**, including axis/separate/fill settings. |
| Face materials | `assign_face_material_slot`, `assign_selected_face_material_slot`, `assign_all_face_material_slot` | Explicit slot mapping and host material intent; section 7 covers full workflows. |

**Half-Cut trap:** the installed `OperationKind::HalfCut` descriptor exists, but the inspected `OperationsApi::catalog()`/`execute()` do not implement a `half_cut` branch. Native Quader composes the workflow in `modeling_sdk_projection_adapter.cpp`. Do not promise that `operations().execute("half_cut")` works merely because metadata contains that string. [Q39], [Q61], [Q66]

#### Session operations and scene publication

| Behavior | Installed session API | Required host composition |
| --- | --- | --- |
| Add / remove / rename authored object | `add_document`, `remove_object`, `rename_object` | Matching Crimson entity/asset/binding changes in the same chronological command. |
| Duplicate object(s) | `duplicate_object`, `duplicate_objects` | Independent geometry versus linked occurrence is an explicit user intent. Native routes include `modeling.duplicate_meshes` and `edit.duplicate`. |
| Combine objects | `combine_objects` | Rebase into chosen survivor coordinates and reconcile material tables before publication; native `modeling.combine_mesh` / `combine_meshes`. |
| Cross-object bridge | `bridge_objects`, `prepare_bridge_documents` | Ordered participants, coordinate/material rebasing and structural outcome. Generic native route `modeling.bridge` dispatches by selection kind. |
| Cross-object merge | `prepare_merge_documents`, `SessionMergeIntent` | ByDistance/ToActive/ToCenter; edge-mode preparation only supports ByDistance. |
| Modeling clipboard | `copy_selection_to_clipboard`, `paste_clipboard` | Compose with scene transforms/materials/instances as needed. Native Cut is copy+delete atomically. |
| Exact component selection | `commit_component_selection_batch`, `invert_component_selection_batch` | Selection universe, visibility, instance edit scope and active occurrence remain host policy. |
| Preview/history/publication | Document and transaction APIs in sections 5.3 and 6 | One editor timeline and coherent render projection. |

Several `OperationsApi` conveniences act on `first_selected()` only. Explicitly enumerate all eligible participants and use session batch/preview APIs for a multi-object native workflow. Do not run independent commands in a loop and call the result atomic. `OperationsApi::repeat_last_action` belongs to its own facade context; Crimson's direct-session adapter needs an accepted-intent replay record containing operation type/options and deliberate target-rebinding policy. Repeating a previous command must not replay stale component IDs into a new topology. [Q4], [Q66]

#### Selection tools and selection-expansion commands

Full modeling parity includes selection behavior, not just mutation operations. Use `set_selection_mode`, `clear_selection`, `select_all_vertices` / `select_all_edges` / `select_all_faces`, singular/plural `select_vertex` / `select_edge` / `select_face` variants, and `apply_selection` with Replace/Add/Remove/Toggle. Use `invert_selection(PolygonSelectionUniverse)` to invert within an explicit eligible universe. Keep component and scene-object selection connected through the adapter, without copying them into competing mutable stores. [Q3], [Q49], [Q50]

| Native command / behavior | Portable computation boundary | Host responsibilities |
| --- | --- | --- |
| `select.all`, `select.inverse`, `edit.clear_selection` | SDK exact selection/inversion | Restrict universe to visible/editable authored targets and active scope. |
| `select.toggle_backface_selection` | Selection-policy option | Define visible versus through/backface selection consistently for clicks/regions/brush and imports as occluders. |
| `select.toggle_group_selection` | Host hierarchy policy | Resolve hierarchy/instance target before enrolling authored components. |
| Click/box/lasso/paint | No complete portable hit tester | Screen geometry, depth/occlusion, region inclusion and ordered input. |
| Connected part / double-click strip | Existing source-only traversal algorithms | Promote a supported neutral query boundary or implement a host read-only selector over authored topology. |
| `select.edge.direction`, `select.edge.loops`, `select.edge.ring` | Native/source-only traversal implementation | Tolerance, seed, active target and topology termination; no installed `PolygonDocument` loop/ring query method. |
| `select.face.same_material` | `face_ids_by_material_slot` is available | Across objects compare resolved material identity, not equal local slot integers. |
| `select.face.coplanar`, `select.face.normal` | `face_ids_by_normal` covers part of the query | Plane/normal tolerance and occurrence-space comparison; coplanar traversal is source-only. |
| `select.mesh.ngons`, `select.mesh.similar` | Authored snapshots supply data; native selectors exist | Match actual Quader criteria, not guessed polygon-count similarity. |
| `select.grow`, `select.shrink` | Existing source-only algorithms | Expose supported queries or host adaptation, then publish exact result. |
| Boundary / hard / soft / default edge queries | `MeshEdges::boundary`, `hard`, `soft`, `defaults` and document queries | Useful portable selectors; do not imply each has a native menu item. |

The source-only `neutral/polygon/document_selection.hpp` declares existing traversal algorithms whose implementations live in internal polygon sources; those declarations are not the installed public SDK contract. Prefer promoting a narrow, tested portable read-only query API if multiple consumers need them. Do not import private headers into Crimson or reclassify implemented algorithms as absent just because they are not exported. Selection-mode native targets are generated as `viewport.selection_mode.<mode>`; their editor context is host-owned.

#### Arrangement, pivots, transforms, and compound commands

| Workflow / native command | Native owner / building block | Crimson parity requirement |
| --- | --- | --- |
| Linear Array | `LinearArrayToolService`, SDK duplication | Capture sources and A/B span; array count and constraints; one accepted transaction. |
| Radial Array | `RadialArrayToolService` | Center, Total/Spacing angular mode, repeat layers/spacing, output budget and Create Instances. |
| Path Array | `PathArrayToolService`, `CurveArcLengthPath` | Source/path acquisition, world-distance distribution, signed forward axis and per-copy rotation/scale/random options. |
| Align X/Y/Z Min/Center/Max | `AlignToolService`, `MeshArrangementService` | World AABB policy, stable participants, per-click history and compatible-grid snapping. |
| Distribute X/Y/Z | Same owners | Preserve equal signed-gap behavior and ordering; validate overlapping bounds. |
| Pivot interactive placement | `PivotToolService` | Snap to vertex/edge midpoint/face center/grid, cycle candidates and accept/cancel. |
| `modeling.copy_pivot_from_active` | Native pivot service | Copy the intended pivot frame into eligible targets. |
| `modeling.set_origin_to_center`, `set_origin_to_pivot` | Native pivot service | Rebase local geometry and scene transforms while preserving world appearance/children/UV sampling. Center uses native area-weighted surface behavior with fallback, not an assumed bounding-box center. |
| `modeling.bake_transforms`, `bake_location`, `bake_rotation`, `bake_scale` | Transform bake service + geometry transform | Bake only requested channels and reset scene channels coherently; preserve hierarchy, handedness and texture lock. |
| `modeling.snap_meshes_to_floor`, `snap_vertices_to_floor` | Snap-to-floor surface query owner | Whole-mesh versus component placement, eligible collision surfaces and transaction policy. |
| `modeling.mirror_selection_x`, `_y`, `_z` | Axis reflection | Distinct from the interactive plane Mirror tool. |
| `modeling.flip_horizontal`, `flip_vertical` | Native view-relative transform intent | Resolve viewport basis; SDK convenience X/Y flips alone do not reproduce it. |
| `viewport.nudge.move_{up,down,left,right}` | Native nudge plan | Camera-relative translation and current grid/distance policy. |
| `viewport.nudge.rotate_{up,down,left,right}` | Same | Camera-relative 45° rotation and pivot. |
| `viewport.nudge.duplicate_or_extrude_move_{up,down,left,right}` | Same | Duplicate objects or extrude components, then translate in one command. |
| `viewport.nudge.duplicate_or_extrude_rotate_{up,down,left,right}` | Same | Duplicate/extrude plus camera-relative 45° rotation in one command. |
| `edit.cut`, `copy`, `paste`, `duplicate`, `delete` | Scene/modeling clipboard and commands | Route by object/component/instance context; do not install a second clipboard/history. |
| `edit.undo`, `redo`, `repeat_last_action` | Existing chronological history + accepted intent | Include modeling and host structural changes. |
| `edit.group`, `ungroup`, `parent_to_active`, `unparent` | Crimson scene hierarchy equivalent | Preserve placement and authored bindings; reuse existing host operations where behavior matches. |
| `edit.create_instance`, `flatten_instance`, `exit_instance_edit` | Native instance semantics, section 6.3 | Canonical definitions, occurrences, explicit edit scope and coherent flattening. |

Brace lists above are compact notation for all four registered directional targets, not literal command strings. The 15 retained `modeling.align.{x,y,z}.{min,center,max}`, `modeling.distribute.centers.{x,y,z}`, and `modeling.distribute.gaps.{x,y,z}` routes are internal compatibility commands. They are not 15 additional visible tools. Both distribution aliases currently use equal signed-gap spacing; do not promise separate center-spacing behavior from the older name. The visible Align Tool keeps its panel active while each accepted button press records its own history command. [Q51], [Q64], [Q65]

#### Curves and mesh-generating helpers

Curves are part of the complete modeling port, scheduled after the first polygon workflow. They need their own authored asset kind, control-point interaction and persistence; they are not reconstructed from their drawn line segments. A solidified Quader-authored curve can become a Quader-authored polygon mesh without introducing imported-mesh conversion. [Q52], [Q53], [Q54], [Q55]

| Behavior | Public entry point | Required integration |
| --- | --- | --- |
| Open/closed Bézier curve | `CurveDocument::make_bezier`, `from_authored_curve` | Stable authored curve and control identities. |
| Append/edit/remove points | `append_point`, `set_point`, `set_points`, `remove_point` | Control-point/handle picking, batch edits and selected-handle feedback. |
| Smooth/corner and closure | `toggle_point_smoothness`, `set_closed` | Preserve actual handle and closure behavior. |
| Split segment | `split_segment` | Shape-preserving split at resolved parameter; preserve new point identity. |
| Transform/evaluate/bounds | `transform`, `evaluate`, `bounds` | Instance/placement conversion and effective preview bounds. |
| Display sampling | `sample(CurveSamplingOptions)` | Samples carry segment/parameter provenance; retain by curve/sampling revision. |
| Circle / Oval presets | `make_xy_circle_curve`, `make_xy_stadium_curve` | Presets are XY-plane geometry; orient the host construction frame explicitly. Oval is stadium-shaped, not an assumed ellipse. |
| Tube render preview | `tessellate_curve_tube` | Uncapped derived mesh; radius/resolution settings and coherent overlay. |
| Apply Solidify | `build_curve_tube_mesh` | Create a new sibling polygon mesh with the source curve's parent, transform, layer and material; retain the original curve and turn off its Solidify display, in one history command. |
| Path distance sampling | `CurveArcLengthPath::build`, `total_length`, `evaluate_at_distance` | World arc-length approximation including nonuniform placement; do not distribute solely by Bézier parameter. |
| Session ownership | `add_curve_document`, `curve_document_snapshot`, `curve_document_copy`, `commit_curve_document` | Same scene session and chronological history. |
| Curve preview | `begin_curve_document_preview`, `update_curve_document_preview` | Same preview registry; all selected handles/curve line/tube/depth/picks use the effective candidate. |
| Curve save/reopen | `serialize_curve_document`, `deserialize_curve_document` | Save control data and IDs; host envelope retains solidify/placement/material properties. |

Curve overlays add control points, tangent handles, handle connector lines, selected segments and provisional construction geometry to the semantic overlay batches. Their identities are curve point/segment identities, not polygon `EdgeKey`s. Do not make the rendering layer infer curve editing state from a tessellated tube.

Apply Solidify is deliberately additive in native Quader. Keep the source curve editable after creating the polygon sibling, and restore both the created mesh and the curve's previous solidify state together on undo/redo. A destructive replacement would change the native workflow. [Q68]

#### Do not claim these as existing Quader parity features

- **Mesh Boolean union/difference/intersection / CSG:** no matching implemented public/native modeling command was found. Combine Meshes, plane cutting and Thicken are different operations.
- **General UV atlas unwrapping/packing:** UV Peel and the face canvas do not provide it.
- **General remeshing, sculpting, or subdivision-surface modifier:** no matching general operation was found; primitive subdivisions and Slice Quad do not imply one.
- **Convex hull generation:** the Create Outer Hull convenience delegates to Thicken.
- **Imported component editing/conversion:** explicitly outside the agreed eligibility scope.

These are boundaries of the inspected source, not claims about future Quader development. Re-run the catalog comparison when changing the SDK revision. A complete port means faithfully implementing the existing toolset, including host behaviors above, without relabeling unrelated operations as unavailable algorithms.

### 5.2 Effective render geometry

For each authored object, compile an owned `MeshPayload` and translate it into a Crimson-owned render payload. The SDK provides float positions/normals, UV0, color, source vertex IDs, UV carriers, uint32 indices, and primitive/material ranges. One authored vertex can generate several render vertices because of seams and normals. Keep that many-to-one mapping. Never weld split GPU vertices back into modeling truth. [Q7]

The compiled payload does not provide a stable `FaceId` per triangle. Do not assume primitive index equals FaceId or survives collapsed faces. For initial component picking/fills, derive a companion face triangulation with explicit IDs from authored snapshots and verify it matches the rendered surface. If exact compiler provenance is required, add a narrow supported public SDK mapping contract; do not infer identity from incidental output ordering or include private compiler headers.

Crimson's imported `ModelVertex`/`ModelPrimitive` path has UV0/UV1, tangent, color, and uint32 indices and is a useful GPU-format reference. Its older procedural `MeshData` uses uint16 indices and a smaller vertex shape. Do not accidentally impose that older limit on arbitrary modeled meshes. SDK-authored output currently supplies UV0; leave UV1 unavailable unless a real second authored channel is added. [C8]

### 5.3 Preview sequence

Use these public session operations for a single document:

```text
capture immutable drag-start document + selection + occurrence transform
evaluate candidate from that baseline
begin_document_preview(object, operation_id, candidate, receipt) -> PreviewId
update_document_preview(PreviewId, next_candidate, next_receipt)
commit_preview(PreviewId) OR cancel_preview(PreviewId)
```

For multi-object or structural edits, use `begin_preview_transaction`, `update_preview_transaction`, and `commit_preview_transaction`. The transaction supports replacement, provisional additions, and removals. Validate every participant before publication; an invalid candidate must leave the last accepted candidate visible. Evaluate drag samples from the captured baseline so rounding and topology changes do not accumulate from sample to sample. [Q4]

`preview_summary()` provides a cheap active/revision/participant view. Cache the preview ID and each participant's revision: its scalar `content_revision` mirrors the first participant and is insufficient as the sole multi-object stamp. Only request `preview_transaction_payload()` when relevant state changes. A replacement suppresses the committed object's render geometry; a provisional addition becomes visible once; a removal suppresses its source. Commit returns structural outcomes that the scene binding table must reconcile.

Every accepted preview must feed the same version of surface triangles, face tint, authored wires, selected edges/points, silhouette masks, depth, picking, and bounds. The native Quader projection contract documents why checking only a fresh mesh getter is insufficient: an outer retained submission gate can still show old data. [Q12]

Prepared extrusion can retain topology and generated IDs across nonzero amount changes. Returning to zero or retiring provisional topology requires respecting the SDK's identity high-water rules before reusing a preparation. Do not resurrect an old component handle for an unrelated new component. [Q5]

## 6. Scene history and persistence

### 6.1 A single chronological editor history

Crimson's current `SceneCommand` stores entity/property/selection patches. An entity reference alone cannot restore the UV or topology of the document it points to. Extend that command boundary with typed modeling state and binding/material-slot changes. [C4]

Recommended first implementation:

1. Capture the affected scene/binding state and a supported SDK checkpoint before the command.
2. Prepare geometry and all host mappings without publishing a partially usable result.
3. Publish the accepted modeling state and coherent CPU render projection atomically.
4. Record one Crimson command with the before/after modeling and host states.
5. Retire SDK-local history entries that have been incorporated into host history, only after no active checkpoint/preview relies on them.

For a small first workflow, `current_workspace_checkpoint()` and `restore_snapshot_as_history_transition()` provide a clear public integration route, including structural edits. Checkpoints share immutable documents but still have workspace metadata costs. Move ordinary edits to `restore_documents_as_history_transition()` and bounded affected-object payloads as scale requires; keep the same externally visible history contract.

Normal undo/redo publishes a new state transition with monotonic revisions and preserved identity high-water. Exact failure compensation is a different path, using the appropriate mutation/preview checkpoint APIs. Reusing exact rollback to implement user Undo can make retained consumers miss the restored state. [Q4]

Selection-only changes should follow Crimson's existing history policy. Since the SDK can record its own selection transitions, the adapter must explicitly manage those internal entries. Mode changes and camera navigation should not appear as mesh edit commands. A continuous drag contributes one command on acceptance; cancellation, rejection, and successful no-op contribute none and preserve unrelated redo.

Validate required CPU publication before acceptance. GPU allocation failure must not silently leave new picking state paired indefinitely with stale visual geometry: retain the coherent old projection or publish an explicit failed-resource presentation while preventing misleading interaction. Do not roll back already accepted authored work merely because an asynchronous upload later needs retrying; define that resource failure boundary separately.

### 6.2 Durable editable assets

Persist an explicit authored-mesh resource category distinct from imported model resources. Each scene occurrence stores its placement and a stable authored asset key; a runtime binding maps that identity to the current session `ObjectId`. Serialized scene identity must not depend on EnTT handles, BGFX handles, or the lifetime of one SDK session.

For the first mesh milestone, use independent authored geometry for independently editable entities; an independent duplicate receives a new authored identity while immutable storage may be shared internally until mutation. This is not the final linked-instance architecture. Full parity includes explicit Create Instance, Flatten Instance, instance editing and the Radial Array Create Instances option, as described below. Material assets may be shared by stable material IDs in either case.

The public functions `serialize_qdr_authored_topology(const PolygonDocument&)` and `deserialize_qdr_authored_topology(span<const byte>)` support an authored geometry blob. Store a format/version tag and material slot references in Crimson's asset envelope, and the occurrence transform in its scene. Selection and runtime revisions are intentionally excluded from this blob; reload recreates runtime bindings. [Q13]

Publish saved geometry revisions first, then atomically publish the scene/reference update. Versioned or immutable geometry blobs make it possible to keep the previously saved scene valid if a later step fails. Preserve the existing project data and atomic-file policy rather than overwriting the imported-model format.

`authored_topology()` preserves loops, per-corner UVs, texture settings, normal-edge state, and allocation cursors. The simplified `authored_payload()` is suitable for overlay extraction but lacks data required for a durable save. Compiled render triangles are also insufficient. Finite collapsed authored faces are intentionally preserved by SDK persistence even when strict geometric validation reports them; save must not silently clean up the document.

The SDK's portable QDR document DTO is not the complete native Quader project format with its hierarchy, lights, assets, and host metadata. The initial integration does not need to load arbitrary native Quader projects or import glTF geometry into the modeling session.

### 6.3 Linked instances, arrays, and hierarchy in the complete port

Native Quader stores a hidden scene-local **definition hierarchy** containing canonical members and **leaf occurrence entities** referencing that definition. Editable documents belong to canonical mesh members. An occurrence contributes placement, while a transient edit context identifies the occurrence through which the shared definition is being edited. This is substantive modeling behavior, not simply GPU instancing. Crimson can use its own scene/resource representation, but must preserve the ownership relationship. [Q58], [Q59], [Q60]

```text
canonical authored mesh/member -> one authoritative SDK document
definition hierarchy           -> member identities and member-local placement
occurrence entity              -> definition reference + occurrence placement
sampling compensation          -> per-occurrence/member texture mapping context
transient instance edit scope  -> occurrence + definition + context generation
```

An independent duplicate/array creates new authored identities. A linked duplicate/array creates occurrences referencing the same definition. Editing a canonical mesh changes every linked occurrence's geometry; component picking and active/hover feedback use the selected editing occurrence and its full composed placement. The instance edit context must not become a second geometry owner or get serialized as if it were asset data.

Native entry currently requires a valid visible/selectable occurrence, object context, no active tool or preview and no existing instance-edit scope. Entry clears selection; exit clears component selection and reselects the occurrence. Crimson's Modeling mode should reproduce the equivalent scope transition while settling pending tools. Imports remain ineligible for component editing even if the host can include them in a scene hierarchy or linked group.

Create Instance captures eligible members under one canonical definition and creates the first occurrence in one history command. Flatten/materialize produces an independent hierarchy with world placement and texture appearance preserved. When other linked occurrences still need the definition, native flatten duplicates its canonical members into independent authored objects. When flattening the last occurrence, it adopts/moves the existing canonical members and removes the unused occurrence/definition; it does not always allocate new authored identities. Batch flatten must account for occurrences processed earlier in the same transaction. Compute each member's world transform from occurrence placement and its definition-relative transform. Reconcile child placement, material slots, canonical identities, component remaps and selection before publication.

Texture sampling requires special care: native `TextureMappingComponent::sampling_from_local` compensates occurrence placement, with identity when absent. Flattening must resolve that sampling effect into the independent result or carry a valid equivalent; merely copying UV0 and dropping sampling compensation changes the texture. Keep geometry/UV document data distinct from occurrence placement and sampling metadata.

Save definition identities, member hierarchy, authored blobs, occurrence references and placement/sampling state. Undo/redo must restore these relationships atomically, including an array's added occurrences and any newly introduced definition. Key render/pick/overlay caches by canonical content plus occurrence placement/sampling and active edit-scope revisions. GPU geometry may be shared, but occurrence selection and occlusion remain distinct.

Scene grouping, parenting and unparenting should reuse Crimson's existing owners where they already provide the intended behavior. They still need modeling-binding and history integration tests. A hierarchy operation is incomplete if it preserves the shaded surface but loses the ability to reopen or edit the canonical mesh.

## 7. Materials, texturing, and UV operations

### 7.1 Keep material identity separate from the SDK slot

Each authored face stores a zero-based material slot. The compiled SDK material identifier is derived from that slot (`slot + 1` in the inspected implementation); it is not a Crimson asset UUID or GPU material handle. Maintain a per-authored-object slot table resolving to stable Crimson material assets. Use a visible fallback material when a reference is unavailable, while retaining the authored reference for recovery. [Q6], [Q7]

The SDK's neutral material payload is not an image decoder, texture library, or Quader material graph. Crimson owns image roles, sampling state, loading, and PBR factors. In the current transition, `ModelTexture.image` is an `AssetRef<DecodedImage>`, and new material/resource owners are being integrated. Bind to their settled API when implementing. [C8]

Face assignment changes primitive/material routing. Scalar material edits should update bindings/uniforms without rebuilding topology. Preserve Crimson's color convention: albedo/emissive color decoding differs from linear scalar/normal data, and the final viewport is encoded to sRGB once.

### 7.2 Numeric texture settings and actions

`FaceTextureTransform` operates on base face UVs by rotation, division by signed scale, then normalized UV shift. Consequently, increasing this scale makes the texture repeat less often; negative scale can mirror it. A shift expressed in pixels must be divided by the appropriate decoded image extent before entering this normalized-UV API. Avoid borrowing a material's differently defined scale UI without conversion. [Q7]

`set_face_texture_transforms` applies per-face property patches. `prepare_face_texture_action` supports alignment, projection, scale/shift/rotate, fitting, justification, and mirror-related actions. The face canvas and transfer/wrap interfaces cover more contextual editing. Texture flips, effective face-UV flips, and geometry reflection are different operations; expose and test the intended one. [Q14], [Q15]

For selection-wide operations, collect immutable document snapshots, stable face/edge IDs, actual texture dimensions, active element, and both transforms:

- `world_from_local`: actual placement used for geometric interpretation.
- `sampling_from_local`: the mapping used to evaluate texture placement/locking.

They can differ. Equating them unconditionally makes texture-lock and occurrence behavior inconsistent. Preparation borrows input snapshots only during the call and returns owned replacement documents plus receipts. Publish all replacements through `commit_documents_batch`, preserving object selection as appropriate, or through preview equivalents for a live drag. Never accept half a multi-object UV operation.

### 7.3 Face canvas and UV Peel

Build a face-texture canvas as an ImGui editor of the SDK's chart and affine map. The physical face outline and UV mapping are separate data. A face with a warped or singular texture mapping can have a valid physical outline but `mapping_usable == false`; preserve its existing UVs and report why the requested manipulation cannot be evaluated. UI gesture math belongs in Crimson's tool/controller layer, not inside repeated ImGui draw calls. [Q15]

`prepare_uv_peel` uses selected authored edges as **flow guides, not cut seams**. It walks opposite quad edges and has a guarded incident-patch fallback for non-quad cases. World mode rebuilds density from 0.5 tiles per world unit before repetition; Fit mode spans the requested repeat counts. Disconnected guides map independently. It does not produce an atlas or pack islands. Invalid or unsafe targets reject the whole prepared batch. [Q8]

The initial UV interface can offer face settings/canvas and the existing SDK actions. A general island unwrap/packing editor would be additional work, not a hidden capability unlocked by drawing a UV panel.

### 7.4 Rendering the UV result faithfully

Preserve per-corner UV seams in render compilation. Generate tangents for the actual normal-map UV basis, using Crimson's existing MikkTSpace support where applicable. Geometry/normal/UV basis changes can invalidate tangents; material-only color changes cannot. Respect nonuniform transforms, reflection handedness, double-sided faces, and Crimson's +Y normal-map convention.

Two additional Quader mechanisms matter for close visual parity:

**Occurrence sampling correction.** `MeshVertexPayload` includes `uv_carrier` and `uv_geometric_normal` in addition to effective `uv0`. Conceptually:

```text
uv_effective = uv0 + carrier_2x2 * delta_generated_chart
```

The chart delta compares original and sampling-transformed geometry. Quader evaluates coefficient differences carefully and uses a cofactor-transformed geometric normal. Identity sampling or a zero carrier leaves UV0 unchanged. The carrier captures an affine mapping component while retaining authored corner residuals. For identity sampling, UV0 needs no occurrence correction; qualifying non-affine quads still need the separate correction below. Preserving placement/texture-lock behavior requires the occurrence correction or a verified equivalent baked into derived streams. Do not bake repeatedly into authored UVs and accumulate the correction. [Q16]

**Inverse-bilinear quad correction.** Quader's renderer derives extra coefficients for well-conditioned planar convex quads with qualifying non-affine UV mappings. This corrects triangle interpolation into a four-corner mapping and avoids a visible diagonal change in distortion. The public mesh payload does not already contain those final coefficients. Derive them from original authored face/corner provenance and actual preview positions before merging draw ranges. Unsupported, degenerate, or nonqualifying faces retain their existing triangle mapping. [Q17]

If UV sampling is changed nonlinearly in the fragment shader, a tangent basis computed only from vertex UV0 may no longer match the sampled normal map. Quader derives a corrected basis from final-UV/world-position derivatives where needed. Translate that mathematical behavior to Crimson's normal convention; do not copy Filament-specific V flips, packed tangent-frame signs, or material callbacks. Use checkerboards and directional normal-map fixtures to verify mirrors, rotations, skewed quads, and reflected placements. [Q16], [Q18]

### 7.5 The Face Texture / UV dock

Full parity needs this surface **in addition to Tool Settings**. Native Quader composes a single-face planar canvas, numeric/action controls, and an embedded material browser. In Crimson, use a dedicated dock with resizable canvas, controls and material-picker regions; let the existing material editor own PBR resource authoring. It is not necessary to duplicate Crimson's material asset system to assign materials to faces. [Q56], [Q57]

```text
Face Texture / UV
+--------------------------------------+
| Selected face canvas                 |
| textured outline + mapping handles   |
| pan / zoom                           |
+--------------------------------------+
| Scale      U [ 1.00 ] V [ 1.00 ] [L] |
| Shift (px) U [  0   ] V [  0   ]     |
| Rotation     [ 0.0 deg ]             |
| Align  [Grid] [Face] [View]           |
| Fit    [Both] [U] [V]                |
| Flip Texture [U] [V]                 |
| Flip Face UV [U] [V]                 |
| Justify  [L] [R] [Top] [Bottom] [C]   |
| [ ] Treat as One                     |
+--------------------------------------+
| Material picker / thumbnails         |
| assign to eligible selected faces    |
+--------------------------------------+
```

This is a proposed Crimson layout. `[L]` means Link Scale, not a literal final icon. The controls implement these distinct contracts:

| Control | Native semantics to preserve | SDK / adapter responsibility |
| --- | --- | --- |
| Scale U/V | Signed, nonzero; direct step 0.01; larger magnitude enlarges the texture; negative mirrors | `set_face_texture_transforms`, explicit density/unit conversion. |
| Link Scale | Initially off; preserve captured U/V ratios | For mixed selections capture each face's original ratio; do not derive all faces from one displayed scalar. |
| Shift U/V | Display image pixels, step 1; numeric shifts require exactly one selected face | Normalize with actual image dimensions before SDK shift; do not show an arbitrary face's value for multi-selection. |
| Rotation | Direct numeric step 1°; action buttons use configured rotation increment | Capture continuous gesture and commit one edit; preserve mixed-value state. |
| Align Grid | Native world-axis/density reset, including zero shift/rotation and native 0.125 map-units-per-texel scale | Convert the density through Crimson meter policy; do not copy the number into a differently defined scale field. |
| Align Face / View | Change projection basis while preserving scale/rotation/shift | View alignment captures view axes when invoked. |
| Treat as One | Combined-bounds/shared treatment for applicable fit/justify/UV-flip actions | An operation option; it does not combine mesh topology. |
| Material assignment | Assign explicit material references to selected eligible faces | Slot resolution and asset history; keep texture mapping unless the requested action also transfers it. |

No selected faces means an empty canvas and disabled numeric/action fields displaying an unavailable/mixed marker; the material browser can remain visible. Selection-wide Scale/Rotation can apply to multiple faces with mixed values; single-face Shift cannot. Lock/visibility/instance scope and incompatible previews are authoritative eligibility conditions. A field should explain a current operation error or blocked edit when necessary, without inventing alternative geometry.

The canvas calls `query_face_texture_canvas` for its immutable view and `prepare_face_texture_canvas` for edits. Left drag translates the texture; width/height handles scale using the opposite handle/edge as pivot; a rotation handle uses its captured pivot. Middle drag pans, wheel zooms. Native translation Ctrl snaps to pixels; scale Ctrl snaps to whole-unit repeats and Shift to grid-sized repeats; rotation Ctrl snaps to 1° and Shift to the configured angle step. Capture these as semantic gestures so Crimson can map its input bindings consistently. [Q15], [Q57]

Canvas begin/update/release is one modeling preview gesture: release commits one history command and Escape cancels it. Numeric controls may use immediate merged edits instead of a persistent tool preview, but must share the same authoritative face-texture owner. Selection/context/instance-scope changes abandon stale field samples and invalidate trailing pointer releases. Do not reset pan/zoom merely because one numeric value changed; do reset an incompatible mapping gesture when its face/chart identity changes.

The canvas is single-face mapping editing, not a general UV-island atlas view. An unusable/singular mapping may still have a valid physical face outline; render that outline and disable unsupported manipulation instead of substituting a fabricated chart.

### 7.6 Complete texture-action inventory

All 28 actions below are represented by `FaceTextureAction` and use `prepare_face_texture_action`. Native command IDs have the literal prefix `modeling.texture.`. The main menu is curated, so some actions are primarily palette/context-accessible. A complete port must account for them even if Crimson chooses a different menu layout. [Q14], [Q56], [Q63]

| Family | Exact native suffixes | Public `FaceTextureAction` values |
| --- | --- | --- |
| Alignment | `align_to_grid`, `align_to_face`, `align_to_view` | `AlignToGrid`, `AlignToFace`, `AlignToView` |
| Projection | `project_cube`, `project_cylinder`, `project_sphere` | `ProjectCube`, `ProjectCylinder`, `ProjectSphere` |
| Scale steps | `scale_x_up`, `scale_x_down`, `scale_y_up`, `scale_y_down` | `ScaleXUp`, `ScaleXDown`, `ScaleYUp`, `ScaleYDown` |
| Shift | `shift_left`, `shift_right`, `shift_up`, `shift_down` | `ShiftLeft`, `ShiftRight`, `ShiftUp`, `ShiftDown` |
| Rotation | `rotate_clockwise`, `rotate_counter_clockwise` | `RotateClockwise`, `RotateCounterClockwise` |
| Texture-axis flips | `flip_texture_horizontal`, `flip_texture_vertical` | `FlipTextureHorizontal`, `FlipTextureVertical` |
| Effective face-UV mirrors | `flip_face_horizontal`, `flip_face_vertical` | `FlipFaceHorizontal`, `FlipFaceVertical` |
| Fit | `fit_both`, `fit_x`, `fit_y` | `FitBoth`, `FitX`, `FitY` |
| Justification | `justify_left`, `justify_right`, `justify_top`, `justify_bottom`, `justify_center` | `JustifyLeft`, `JustifyRight`, `JustifyTop`, `JustifyBottom`, `JustifyCenter` |

Texture-axis flips change signed scalar texture axes about the chart origin while retaining shifts. Face-UV mirrors reflect the effective UV bounds while preserving scalar texture settings and geometry. Their results can differ on shifted/rotated mappings; do not collapse both buttons onto one negative-scale implementation.

Numeric `face_texture_settings` / `set_face_texture_transforms`, canvas queries/preparation, `prepare_face_texture_transfer`, and `prepare_uv_peel` / `validate_uv_peel_options` complete this workflow beyond the 28 discrete actions. UV Peel has its own preview/settings controller because selected edges guide the mapping. Texture Lock and Texture Lock Scale are host transform policies, with native IDs `viewport.toggle_texture_lock` and `viewport.toggle_texture_lock_scale`; they are not additional scalar UV actions.

### 7.7 Material copying and Continue Texture gestures

Native viewport interaction targets distinguish where material or mapping comes from and where it is applied:

| Native target | User intent |
| --- | --- |
| `viewport.material.apply_selected_face_to_face` | Copy the selected face's material to the clicked face. |
| `viewport.material.apply_selected_face_to_mesh` | Apply selected face material to the clicked mesh. |
| `viewport.material.apply_face_to_selection` | Apply the clicked face's material to selected faces. |
| `viewport.material.apply_face_to_mesh` | Apply the clicked face's material across that same clicked mesh. |
| `viewport.material.transfer_face_texture` | Continue the selected source face's material and texture mapping onto the target face. |

These are pointer interactions with captured source/target context, not interchangeable global commands. Material-copy-only gestures preserve target UVs/settings; Continue Texture transfers the source material together with its UV chart/settings through `prepare_face_texture_transfer`. Resolve that source material into the target mesh's slot table before preparation; a source-local slot integer is not a portable material identity. Keep the source-preserving gesture alive without normal selection stealing its source, and cancel it when selection, instance scope, scene or relevant bindings change. Respect authored-only target eligibility even when the ray hits an imported model first. Refer to the native `QuaderModelingApi` methods `apply_selected_face_material_to_face`, `apply_face_material_to_selected_faces`, and `transfer_face_texture_to_face` for host intent, then implement it through Crimson material/selection owners. [Q14], [Q51], [Q63]

Selected-source copy and Continue Texture require exactly one selected source face. Preserve the native click publication boundary: a face-copy click publishes on its first release without waiting for the double-click timer. A following whole-mesh double-click is its own ordinary history step; loss of context cancels the pending interaction, but must not undo the already published first click. Do not implement both as one delayed speculative operation. [Q67]

## 8. Recreating overlays in BGFX

### 8.1 Separate the visual jobs

| Overlay | Source geometry | Rendering strategy |
| --- | --- | --- |
| Authored topology wire | Unique polygon boundary edges | Screen-expanded triangle ribbons |
| Selected/hovered edges | Semantic `EdgeKey`s | Same ribbon geometry with selection style/depth policy |
| Vertex handles | Authored vertex positions and IDs | Screen-expanded marker quads |
| Selected/hovered faces | Face-associated triangulation | Depth-tested fill with explicit overlap priority |
| Tool preview | Accepted candidate and tool annotations | Candidate surface plus appropriate lines/points/fills |
| Object/group silhouette | Selected surface occupancy | Separate mask/depth targets and boundary extraction |
| Labels, dimensions, 2D handles | Projected annotation positions | ImGui HUD drawing with deliberate visibility policy |

Wireframe rasterization of a compiled triangle mesh cannot reproduce authored polygon wires because it exposes triangulation diagonals. A silhouette is also different: it is the screen boundary of selected occupied surface, not all edges within that surface. Keep these passes separate. Quader's `ViewportOverlayFrame` and style policy are useful references for renderer-neutral records, source roles, semantic IDs, layers, and pixel dimensions. [Q19], [Q20]

### 8.2 Extract authored edges once

For each face loop `(v0, v1, ..., vn-1)`, enumerate consecutive pairs including the closing edge. Canonicalize the unordered endpoint IDs with the SDK's `make_edge_key`, and deduplicate within one reusable local-space document payload. Read endpoint positions from the same immutable candidate that produced the surface mesh. At scene submission, retain each object's `ObjectId`/occurrence identity, transform, and selection state: two occurrences sharing immutable geometry still need two presentations. Payload deduplication must never remove one occurrence's wires.

Collect incident faces while deduplicating. A manifold edge usually has one or two incident faces; support depth may need a suitable non-collinear third point from each incident rendered surface. Preserve the distinction between a topology edge and its render-vertex copies at UV/normal seams. An edge bordering two materials is still one authored edge.

Quader's `build_mesh_wire_geometry_payload` uses authored boundaries and only falls back to unique triangle edges when usable authored geometry is absent. Crimson's Modeling mode always has authored geometry, so use that authoritative path. Do not fall back from a temporarily unavailable authored snapshot to triangle diagonals during a preview. [Q21]

A proposed CPU record can retain object ID, `EdgeKey`, endpoint IDs, support bindings, and a style class. Its packed GPU record carries positions and shader controls; persistent identity need not occupy GPU attributes unless picking or indexed position indirection needs it. Explicitly handle degenerate supports and nonmanifold edges: retain the line with a bounded fallback depth policy rather than constructing an invalid plane.

### 8.3 First BGFX implementation: retained ribbons

Begin with **four vertices and six indices per edge**, batched by compatible style and depth policy. Each corner repeats endpoints A/B and contains controls for side, cap direction, and endpoint choice. Up to two support positions can be carried for the advanced depth path. This avoids relying on backend-dependent native wide-line support. [Q22], [Q23]

Proposed attribute contract, not an existing Crimson type:

| Attribute | Meaning |
| --- | --- |
| Endpoint A / B | Local or world positions with an explicitly defined transform convention |
| Corner control | Side ±1, cap ±1, endpoint 0/1, remaining style/support flags |
| Support S0 / S1 | Optional incident-surface support positions |
| Color/class | Resolved linear color or compact style lookup index |
| Optional position indirection | Stable authored-position stream indices for frequently changing previews |

Use 32-bit indices or bounded batches when expanded ribbons exceed 16-bit indexing. Keep camera matrices, physical viewport size, line width, and feather width in uniforms. Retain geometry across camera changes. Batch many edges in one submission; one draw per edge is an unnecessary scaling limit.

Later, a canonical quad with per-edge instance data or shared local edge geometry with per-object transforms can reduce duplication. These are different instancing axes and should be measured separately. BGFX transient instance buffers are frame-lived and can run out of capacity; choose retained/dynamic buffers for persistent geometry and handle any transient allocation limit deliberately. [E1]

### 8.4 Clip before expanding

Use the same right-handed camera and surface view-projection convention as Crimson. In view space, the visible side of the near plane has `z <= -near`.

1. Transform A and B to view space for classification.
2. If both are behind the near plane, emit no ribbon.
3. If one is behind, find the segment intersection with `z = -near` and replace that endpoint.
4. Project the clipped endpoints with the same surface view-projection path.
5. Expand in physical viewport pixels only after valid perspective division.

The ordinary raster clipper still handles lateral/far clipping. Any support calculation must account for the clipped edge and valid incident surface domain. An endpoint at or behind the camera must not produce a huge screen-space quad through division by a nearly zero W. Short/end-on projected segments need a stable fallback direction.

### 8.5 Pixel expansion and analytic coverage

The following is **shader pseudocode**, not a drop-in BGFX `.sc` shader. It describes the data flow; BGFX varying declarations, uniforms, clip-Y handling, fragment-depth output, and shader profile details must be implemented against Crimson's installed toolchain.

```text
A, B = near-clipped endpoints projected to homogeneous clip coordinates
V = actual viewport attachment dimensions in physical pixels
W = resolved line width in physical pixels (at least 1 for Quader-style wires)
F = antialias feather width in physical pixels

a = A.xy / A.w
b = B.xy / B.w
delta_px = (b - a) * V * 0.5
L = length(delta_px)
tangent = (L > epsilon) ? delta_px / L : (1, 0)
normal = (-tangent.y, tangent.x)

corner controls (side, cap, endpoint):
    (-1, -1, 0), (-1, +1, 1), (+1, +1, 1), (+1, -1, 0)

R = (W + F) * 0.5
base_xy = (endpoint == 0) ? a : b
offset_px = R * (side * normal + cap * tangent)
corner_xy = base_xy + 2 * offset_px / V
s = endpoint * L + cap * R

dA, dB = endpoint depths converted to forward [0, 1] framebuffer depth
raw_depth = (L > epsilon) ? dA + (s / L) * (dB - dA) : min(dA, dB)
raster_depth = clamp(raw_depth, min(dA, dB), max(dA, dB))

emit clip position (corner_xy, depth_to_ndc(raster_depth), 1)
emit screen-linear varyings:
    raw_depth
    distance = (side * R, s - L / 2, L / 2)
    endpoint depth bounds and resolved color

fragment:
    q = abs(distance.xy) - (W / 2, distance.z + W / 2)
    sd = length(max(q, 0)) + min(max(q.x, q.y), 0)
    coverage = (F > epsilon) ? saturate(0.5 - sd / F) : (sd <= 0 ? 1 : 0)
    depth = clamp(raw_depth, min(dA, dB), max(dA, dB))
    depth = apply_valid_bounded_support_correction(depth)  // later section
    discard if coverage is zero
    write fragment depth = depth
    alpha = line_alpha * coverage
    output (linear_rgb * alpha, alpha)
```

This uses device-space ribbons with W=1 so distance and depth varyings interpolate linearly in screen space. If a different design retains the original clip W, explicitly provide equivalent screen-linear interpolation; ordinary perspective interpolation changes cap distances and depth behavior.

The extra half-feather apron supplies fragments outside the nominal line boundary for coverage. Square caps extend by half the authored width. Quader's shared coverage shader uses analytic distance for these caps and derivative-aware treatment of dashed spans. Dash phase should use screen distance along the clipped segment, with an explicit stable phase policy if continuity across connected segments is required. [Q23], [Q24]

Keep raw extrapolated centerline depth as a separate varying, while clamping raster-position depth. Clamping both vertex depth values and simply interpolating them can bend the interior depth plane because the square caps extend beyond the source endpoints. The fragment clamps the raw value to the endpoint range again, preventing caps from extending in depth beyond the clipped line. [Q25]

### 8.6 Depth conventions and render state

Crimson currently clears depth to 1 and renders surfaces with `LESS`; Quader's native overlay path uses reversed depth. Adapt the math instead of copying compare flags or bias signs. [C7], [Q25]

| Operation | Crimson's current forward-depth adaptation |
| --- | --- |
| Visible overlay | `BGFX_STATE_DEPTH_TEST_LEQUAL` |
| Explicit hidden/dashed pass | `BGFX_STATE_DEPTH_TEST_GREATER` |
| Deliberately on-top tool handles | Omit depth-test flags |
| All ordinary overlays | Preserve depth; omit `BGFX_STATE_WRITE_Z` |
| Premultiplied blending | `BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA)` |
| Zero-to-one clip depth | `d = clip.z / clip.w` |
| Negative-one-to-one clip depth | `d = 0.5 * clip.z / clip.w + 0.5` |
| W=1 ribbon output | NDC Z is `d` for zero-to-one, `2*d-1` otherwise |

Use `caps->homogeneousDepth` consistently with Crimson's projection adapter. Do not reuse the ImGuizmo projection by accident: its consumer convention can differ from the render projection. Apply framebuffer-origin/Y conversions only at the relevant texture/screen boundary, not as an arbitrary second flip of NDC expansion.

Illustrative BGFX visible-pass state:

```cpp
const std::uint64_t visible_state =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_DEPTH_TEST_LEQUAL |
    BGFX_STATE_BLEND_FUNC(
        BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

bgfx::setState(visible_state); // No depth writes; no ribbon face culling.
```

Use the same depth computation for complementary visible/hidden passes. Equal depth belongs to the visible pass. Hidden-line opacity and whether a particular tool uses an on-top layer are explicit style policies; they must not leak into every overlay.

### 8.7 The difficult case: a wide line on a sloping face

At the centerline, edge depth matches its incident surface. Across a several-pixel ribbon, that face's depth changes, while naive line depth varies only along the segment. The face can therefore cut away half the line. A large camera pull appears to fix it until a genuinely hidden edge leaks through a foreground object.

Quader carries incident support points and evaluates the projected surface depth across the ribbon. A support plane needs endpoints A/B and a suitable third point S. In homogeneous clip coordinates, convert homogeneous Z to framebuffer-depth form:

```text
Zero-to-one:          Zh = clip.z
Negative-one-to-one:  Zh = 0.5 * clip.z + 0.5 * clip.w

hA = (A.x, A.y, A.w), hB = (B.x, B.y, B.w), hS = (S.x, S.y, S.w)
det = dot(hA, cross(hB, hS))

P = (ZA * cross(hB, hS)
   + ZB * cross(hS, hA)
   + ZS * cross(hA, hB)) / det

surface_depth_at_ndc(x, y) = dot(P, (x, y, 1))
```

This solves a depth plane without perspective-dividing the support point, which matters when that point is behind the camera. Rescale homogeneous inputs to avoid numerical extremes and reject nonfinite or scale-relative degenerate systems. This formula is a design explanation; the robust clipping/domain implementation still needs its own tested code. [Q26]

**Finite support is essential.** Evaluate correction only within the valid projected incident surface domain and clipped depth bounds. An infinite support plane from a grazing triangle can extrapolate toward the camera far beyond the actual surface. That creates exactly the foreground leakage that the correction is supposed to avoid. Evaluate the two incident surfaces independently at each fragment rather than selecting one winner at ribbon vertices.

For Crimson's forward-depth convention, a conceptual combination is:

```text
depth = bounded_centerline_depth
for each valid incident support:
    if fragment lies within its finite projected support domain:
        depth = min(depth, bounded_support_depth_at_fragment)
```

Nearer is `min` here; Quader's reverse-depth equivalent uses `max`. Preserve finite-domain tests, clipped depth bounds, cap handling, and the same surface projection evaluation. Only add a tightly bounded precision allowance after testing oblique and distant coplanar surfaces against actual foreground occluders. D24S8 forward depth has different precision behavior from Quader's pinned Vulkan path; its constants and raster-subpixel assumptions are not universal D3D11 constants. [Q25], [Q26]

Neither this support calculation nor `fwidth` of its interpolated depth requires a sampled scene-depth texture. A shader can write the corrected fragment depth and let the existing fixed-function scene-depth test classify it. `fwidth` computes local derivatives; it does not read the depth attachment.

### 8.8 Points and face fills

Vertex markers use four corners around one projected world-space center, with dimensions in physical pixels. Resolve square versus circular/ring coverage explicitly. Use the same accepted preview position mapping as wires. Render selected/active/hover classes through a consistent style/priority policy, retaining ordinary depth occlusion except for tools deliberately defined as on-top. [Q27]

Face fill needs more than drawing every highlighted triangle with alpha blending. Overlapping selected objects or layered hover/active states can accumulate opacity. Quader uses priority/stencil composition to select coverage and consume alpha once. Its native ordering distinguishes selection, active selection, hover, face-plane hover, and remove-selection presentation. [Q28]

For Crimson, reserve stencil bits or a separate classification mask under the renderer's centralized policy. Clear transient bits for each viewport use; accumulate the strongest eligible class before coloring it once. Depth-test the relevant surfaces, and define whether hidden fills are permitted. Do not let a fully transparent fill claim coverage or let a farther active surface replace a nearer visible ordinary one. These are observable rules that CPU geometry tests alone cannot establish.

Quader's full stencil composition is restricted to particular native backend paths because of its Filament integration. That is not an inherent BGFX restriction and should not become a hardcoded Vulkan-only requirement in Crimson.

### 8.9 Object silhouettes

An object/group silhouette is the projected boundary of full selected occupancy, including the outer union of selected surfaces. Other scene objects subsequently determine which parts of that boundary are visible. Drawing authored edges does not reproduce it. Quader has separate ordinary and active selected masks/depth and a screen-space boundary stage, plus a CPU fallback for some paths. [Q29]

Recommended BGFX implementation:

1. Render full selected authored surface occupancy into sampleable masks, with explicit ordinary/active classes and the current preview geometry, independently of nonselected scene occluders.
2. Retain nearest selected-surface depth separately for each class. Occupancy alone is insufficient for overlapping depth layers.
3. Extract an outline band in pixel space using neighboring mask values and the chosen width.
4. Propagate the appropriate selected boundary depth into that band, then composite complementary `LEQUAL` visible and optional `GREATER` hidden passes against shared scene depth. Preserve active boundaries and equal-depth priority without allowing a farther active object to overwrite a nearer ordinary boundary.

Imported geometry occludes the selected authored mesh during composition. This can use fixed-function testing against shared scene depth without sampling that attachment. Do not scene-depth-test away selected occupancy during mask generation: doing so creates a visibility contour at foreground occlusion cuts and loses the hidden true boundary. Propagating selected depth into the band is necessary because an empty neighboring mask pixel has no useful selected depth.

A sampleable selected occupancy/depth target is independent of whether scene depth is sampleable. Start with the resources actually needed by the selected algorithm. Prefer full-resolution masks for the first fidelity implementation; reduced-resolution masks change widths and thin-feature coverage and need separate acceptance.

### 8.10 Insert passes into Crimson's actual pipeline

Current pipeline and proposed additions:

```text
scene PBR -> HDR color + scene depth
tone mapping -> linear display-referred composite, sharing scene depth
grid -> same composite
[proposed Modeling face fills / hidden lines / visible lines / markers]
[proposed silhouette composite / foreground tool overlays]
final sRGB encode -> viewport display texture
ImGui -> window and docked tools
```

Run mask/depth preparations before their consumers using named views. BGFX submission order in C++ is not a substitute for the explicit `setViewOrder` table. Crimson currently reserves views for scene, present, grid, encode, tools, picking/readback, and thumbnails. Allocate the new views centrally and update ordering without collisions. Use deterministic within-view ordering where priority requires it. [C7], [E1]

Quader overlay colors are converted from authored sRGB to linear before premultiplied blending. Crimson's grid already uses that convention. Compositing after tone mapping keeps overlay brightness independent of scene exposure; compositing before final encoding preserves correct linear blending. Do not encode overlay RGB separately and then encode the composite again.

Crimson's HDR and composite framebuffers borrow the same D24S8 texture. It currently has `BGFX_TEXTURE_RT_WRITE_ONLY`. Hardware-tested ribbons, markers, fills, and support-depth correction can share it. Any effect that explicitly texture-samples scene depth needs a capability-checked readable attachment or a dedicated sampleable scene-depth representation. Match viewport, projection, transforms, visibility, and alpha-cutout policy if using a separate depth pass. [C7]

Extend `ViewportTargets`' coherent resource bundle for any mask/depth additions. Build a complete new bundle on resize, swap only after successful creation, cancel stale picks, and destroy borrowing framebuffers before their owned textures. An unchanged viewport size should not allocate again.

### 8.11 Tool-specific visual feedback

The same line/point/fill machinery must carry **tool semantics**, not just the standing wireframe. The following is a proposed BGFX/ImGui allocation of the native tool behaviors. Derive helper geometry from the active controller's accepted state; do not let the renderer independently reconstruct an operation from the latest mouse position.

| Tool family | Depth-aware BGFX presentation | ImGui / screen-space presentation |
| --- | --- | --- |
| Click / box / lasso / paint selection | Eligible hover and selected authored components | Rectangle/lasso stroke, brush cursor and modifier hints clipped to originating viewport. |
| Move / Rotate / Scale / Pivot | Origin/pivot marker, constrained axes/planes and component candidate; depth policy per helper | Numeric readout, axis/angle/scale label. A host gizmo can acquire intent, but its draw-on-top appearance must not replace required depth-aware component overlays. |
| Primitive creation | Footprint, height/construction guides, candidate surface/wires and relevant control markers | Meter dimensions, current creation phase and snapping feedback. |
| Extrude / Inset / Bevel / Thicken / Bridge | Accepted candidate topology, affected components, direction/width/bridge guides where supplied by the controller | Parameter values and concise rejection feedback; settings stay in the dock. |
| Insert Edge Loop | Exact hovered loop path and candidate inserted edges, with stable seed identity | Factor/distance readout and snapping indication. |
| Knife | Resolved point markers, stroke segments and candidate cuts; style unresolved provisional segment separately | Cursor snap hint and completion/cancel hint. A raw mouse ray is not a valid topology endpoint. |
| Cut / Half-Cut | Finite construction-plane patch, intersection/part previews, keep-side indication and cut boundary | Keep/cap settings, axis label and phase hint. Bound the helper plane to useful scene/tool extent. |
| Poly | Placed vertices and open contour; candidate face fill only when valid | Point count or concise invalid-contour diagnostic when useful; no fake accepted face. |
| Mirror | Captured plane/axis and reflected candidate with placement-sensitive UV result | Plane/constraint hints; no duplicate source/candidate draw after source replacement is active. |
| Linear / Radial / Path Array | Candidate occurrences, anchors/center/path guides, optional bounds/dimensions | Counts, angular/span/path-distance readouts. Reuse source geometry for transform-only preview changes. |
| Align / floor snap | Accepted object positions and relevant bounds; optional reference guides from controller | Owner options and immediate-action buttons; never imply an already committed Align click is still provisional. |
| Curve editing | Sampled curve, control vertices, tangent handles/connectors, selected segment, optional effective tube | Radius/resolution/closure settings, control gesture hints. Curve IDs remain distinct from polygon IDs. |
| UV Peel | Selected guide edges plus shaded candidate UVs and ordinary component highlights | Axis/repeat/offset settings; it is not a topological seam-cut display. |
| Face Texture canvas | The 3D surface uses the same candidate mapping/tangent result | 2D textured face, outline and transform handles inside the canvas clip rectangle. |

Use separate semantic style channels for committed selection, hover, construction guides, accepted preview and invalid attempted input. An invalid attempt may produce a small warning marker/label, but must not replace the accepted mesh or feed its invalid positions into depth/picking. Refer to the existing line-layer priority rules to resolve coincident helpers without piling up alpha.

World-anchored helper lines and points use the same framebuffer-pixel expansion and forward-depth policy as standing overlays. Screen-space lasso/canvas strokes use ImGui clipping and do not need scene-depth testing. Text labels project from controller-provided anchors after near-plane checks; labels outside the viewport or behind the camera are suppressed. If a label is intentionally always visible while its world anchor is occluded, make that a deliberate tool presentation policy.

For array spacing/count changes, retain shared source geometry and update only the occurrence transform/identity list unless the actual authored source changed. Do not copy full meshes into a new buffer for every provisional occurrence. Curves can use view-independent sampling for the first implementation; if later using pixel-adaptive tessellation, explicitly key that derived curve-sampling cache by camera/tolerance. This is separate from ordinary authored polygon wires, which should not rebuild on camera-only movement.

Accept/Cancel retires all tool-owned helper batches together. Every retained helper, including dimension endpoints, curve handles, plane patches and array bounds, carries the operation generation and preview/content revision. Otherwise the surface may be correct while the manipulation cues remain one candidate behind.

## 9. Retention, revisions, and resource lifetime

### 9.1 Revision gates precede work

The important performance property is not merely "use a cache." Check a cheap source stamp **before** enumerating topology, hashing a full mesh, formatting UI strings, allocating snapshots, or uploading buffers.

Proposed stamp fields include scene/mode generation, SDK object identity, authored content identity/revision, preview identity and participant revision, component selection revision, host occurrence transform, material-slot revision, and overlay style revision. Use the subset relevant to each consumer; a color change should not invalidate topology.

| Change | Required response | Work that should remain retained |
| --- | --- | --- |
| Camera movement | Update projection/depth-dependent passes and picking context | Authored edge extraction and static wire buffers |
| Hover/selection | Update class/membership/appearance and fills | Unchanged mesh topology |
| Stable-topology position preview | Update effective positions, affected normals/UVs, bounds, depth | Compatible indices and semantic IDs |
| Topology edit | Rebuild affected mesh/edge/face mappings and reconcile remaps | Unrelated objects |
| Material factor | Update material parameters | Geometry and topology |
| UV edit | Update affected attributes/tangent basis | Unchanged authored connectivity |
| Resize | Replace target bundle and pixel uniforms; reject stale picks | Authored geometry |
| Mode exit/cancel | Retire affected presentation and captured input | Committed authored assets |

Masks can require a new raster pass on camera movement even when their source geometry buffers stay retained. Do not confuse "zero geometry upload" with "no rendering work."

### 9.2 Preview freshness must cross every cache boundary

An extrusion amount can change positions while topology, IDs, selection, and committed asset revision remain identical. Keying only by the committed asset revision produces stale lines. The accepted preview revision must reach both the outer submission coordinator and the inner mesh/overlay/depth caches. Preview removal is also a change that retires old buffers or selects the committed projection again. [Q12], [Q30]

Quader's native `MeshGeometryProjection` has effective position/attribute/index streams; a retained `preview.mesh` can be metadata rather than current positions. Crimson does not need to import that native type, but must preserve its behavioral rule: every consumer reads the accepted effective stream, never a convenient stale base array.

A useful regression warms the real submission caches, changes only preview positions several times, and observes the data submitted to the renderer. Calling a fresh overlay getter in a test bypasses the gate that commonly causes the bug. Check both persistent source wires and volatile tool/hover overlays.

### 9.3 Lifetimes and uploads

`document_snapshot()` returns a pinned immutable document; `document_copy()` is suitable for candidate edits. Avoid the deprecated borrowed `document()` pointer across mutations. Materialized SDK render snapshots currently support owned payloads; the `Pinned`/`BorrowedSameFrame` enumeration values do not make those snapshot modes implemented. [Q4], [Q6]

For a simple BGFX bridge, copy upload bytes into BGFX-managed memory. If using a reference-based upload, retain the exact immutable CPU storage until BGFX's release callback completes; returning from `submit` or advancing one frame is not sufficient evidence that the driver has consumed it. Never overwrite a position buffer still pinned by an older accepted frame. [E1]

Give the BGFX cache sole ownership of programs, buffers, uniforms, and target textures. Release those owners before `bgfx::shutdown`. Keep dirty upload ranges bounded and merge excessive fragmentation. Instrument extraction count, full/partial uploads, bytes uploaded, draw count, and buffer capacity so performance claims can be measured.

Quader's retained edge slots and instanced wire backend provide useful scaling references. Introduce stable slots, shared geometry, and instancing after the baseline visual and lifecycle contracts pass; preserve the same regression fixtures throughout. [Q31]

## 10. Crimson observations and implementation sequence

### 10.1 Observations to act on when implementation begins

| Observation from the working tree | Consequence for this integration |
| --- | --- |
| Existing scene/editor separation and full affine transforms | Extend those owners; avoid accumulating modeling logic in `App` |
| Scene history currently stores entity/property patches | Add modeling payloads before exposing mutating tools |
| Imported model GPU format is richer than the procedural sphere format | Reuse its attribute/index capabilities for derived authored meshes |
| Resource/material/cache files are being introduced | Re-read their final ownership and CMake wiring before binding to them |
| Some architecture prose still describes earlier milestones | Current validated source takes precedence over stale status paragraphs |
| Main depth is shared but write-only | Reuse it for hardware-tested overlays; allocate sampled resources only for algorithms that need them |
| View order is explicit and IDs already serve picking/thumbnails | Register Modeling passes centrally |
| Existing selection bounds/light markers use ImGui drawing | Preserve those HUD uses; add BGFX passes for depth-aware component presentation |

At inspection, new `MaterialAsset`, resource manager/streaming, GPU cache, and shader-related implementation files were not all listed in root CMake. `SceneProperties` and some older `EditorSession` references also showed an in-progress transition. This is a request to establish the implementation baseline, not a diagnosis from a failed build: no Crimson build was run for this document. Avoid starting a parallel resource framework to work around a temporary transition. [C2], [C4], [C8], [C9]

### 10.2 Recommended order, with exit criteria

| Stage | Implement | Exit criterion |
| --- | --- | --- |
| 0. Baseline | Recheck current source, settle interacting resource interfaces, build existing Release and focused tests | Known starting state; no port-specific code needed yet |
| 1. Headless SDK bridge | Installed package, one session, box operation, authored serialization | CPU test creates/edits/round-trips a document without SDL/BGFX/ImGui |
| 2. Mode and entity binding | Empty entry, authored-only eligibility, primitive creation, entity binding, creation/deletion history and binding rollback | A modeled box survives leaving/reentering the mode and creation undo/redo; imports remain ineligible |
| 3. Derived rendering | Mesh/material-slot adapter, bounds, effective preview surface | One coherent candidate renders with correct placement and cancellation |
| 4. Component interaction and basic overlays | Host picking, ribbon/marker batches, face fill, shared scene depth | Box has twelve edges; selection and preview coordinates match pixels |
| 5. Complete extrusion workflow | Drag baseline, preview, Accept/Cancel, unified history, atomic persistence | Create/select/extrude/cancel/accept/undo/redo/save/reopen all work coherently |
| 6. Texturing and UV | Material assignment, face settings/canvas, actions, UV Peel, tangent/mapping parity | Checkerboard/normal tests survive edits and reload |
| 7. Advanced visual parity | Finite support-depth correction, overlap priorities, silhouettes, hidden styles | Oblique/near-plane/foreground and overlap GPU fixtures pass |
| 8. Complete component selection and topology | Loop/ring/grow/shrink/connected selectors; remaining vertex/edge/face operations; cut/knife/poly/mirror; settings and native compound transforms | Every covered component operation has correct eligibility, success/no-op/failure, attributes, preview where appropriate, and undo/redo |
| 9. Pivots and arrangement | Pivot/origin/bake/floor workflows; Align; Linear and Radial Array with independent copies first | World appearance, hierarchy and UV sampling remain coherent; each action uses the correct history boundary |
| 10. Curves and Path Array | Curve assets, controls/handles, presets, tube preview/solidify, path distribution and variation | Curve and generated geometry round-trip; control/tube/path previews agree; randomized results remain stable during an edit |
| 11. Linked definitions and occurrences | Create/flatten/edit instance, shared canonical documents, Radial Array Create Instances | Shared edits reach all occurrences; active occurrence picking/overlays, flattening, texture sampling and saved references remain correct |
| 12. Full parity audit and scale | Resolve every section 5/7 ledger row; bounded history/uploads, measured batching/instancing; all parameter surfaces and input gestures | No undocumented missing native modeling family; GPU and workflow fixtures pass on actual Crimson backends and representative scenes |

Stages 4–7 should overlap only where a tool needs the visual feature to be usable. Do not postpone a known incorrect preview/depth contract while adding a large tool catalog. Conversely, object silhouettes need not block the first correctly depth-tested edge renderer.

### 10.3 Handing this to another AI agent

Give the implementer this document and one bounded stage with its exit criteria. Require it to inspect current instructions/source, preserve unrelated WIP, and name proposed versus existing APIs. A useful first assignment is the complete box/extrusion workflow, delivered incrementally through the stages above.

For the user's feasibility question, **8/10 for an AI agent working incrementally with access to both repositories and executable verification** is a reasonable engineering assessment, not a measured probability or delivery-time promise. Much of the difficult geometry already exists. Complete parity still requires substantial host-controller work, scene/history design, selection export gaps, instance/curve integration and real GPU iteration. A one-pass API wiring exercise would not satisfy this document. Use the milestone gates to make each increment independently reviewable while keeping the full catalog as the final target.

Geometry reuse is the most predictable part; advanced overlay fidelity needs actual render inspection. Require evidence from production submission and pixels before accepting appearance claims. A passing compile or an isolated `compile_mesh()` test does not demonstrate correct mode lifecycle, GPU depth, or unchanged-frame behavior.

The implementation can reuse authorized Quader source where appropriate, but should retain package boundaries and source provenance. This document consulted Quader's own implementation and current Crimson code, not foreign DCC source or decompilation. No temporary copied implementation was created for this documentation task.

### 10.4 A concrete completeness ledger

Maintain a machine-readable or ordinary tracked table in the implementation project, keyed by semantic behavior rather than alias. For example:

```text
behavior: edge_bevel
native_routes: modeling.bevel_edges
core: PolygonDocument::bevel_selected_edges(EdgeBevelSpec)
targets: selected eligible authored edges, explicit multi-object policy
settings: width / segments / profile; owner-derived limits
input: activation, numeric edits, optional viewport gesture, accept, cancel
preview: immutable baseline; all retained consumers use candidate revision
history: one accepted command; rejection/cancel/no-op create none
persistence: polygon loops, corner attributes, IDs, material references
tests: ordinary + degenerate topology; UV/normal preservation; actual pixels
status: planned / implemented / verified, with evidence
```

Repeat this for each native tool/action family, including selection-only behaviors and scene helpers. Track SDK extras such as scalar Inset separately so they cannot falsely count toward native parity. Alias routes such as immediate/preview Bridge can share geometry while retaining distinct lifecycle/UI acceptance checks. Newly exported SDK selectors also need installed-consumer verification; compiling a native caller of a source-only header is insufficient.

Record deliberate host adaptations explicitly: meters instead of native map-unit display, dock instead of Qt viewport overlay, Crimson command bindings instead of raw copied shortcuts, and explicit settlement before mode exit. They should preserve the intended modeling result while making the host behavior understandable. Do not call incomplete tools complete merely because a menu entry exists or a batch method produces triangles.

## 11. Acceptance and regression checklist

These are **future implementation checks**, not results claimed by this guide. Add focused CPU tests alongside Crimson's existing scene, extraction, math, model, and renderer tests. Actual GPU verification is required where pixels establish correctness. Follow each repository's current test/build policy; do not impose Quader's entire native build matrix on a Crimson-only change.

### 11.1 Mode, scope, and state

- [ ] Enter Modeling with an empty scene, create a primitive, leave and reenter.
- [ ] Mixed authored/imported selection enrolls only authored targets. Imports never acquire component IDs, UV edits, or modeling previews.
- [ ] Imported models remain visible occluders and retain ordinary scene transform behavior.
- [ ] Switching modes creates no authored history entry; pending tools require settlement.
- [ ] Accept rejection preserves the last accepted draft. Cancel removes provisional additions and restores replacements.
- [ ] Text entry, popup interaction, ImGuizmo, navigation, and modeling do not consume the same press/release.
- [ ] Focus loss, viewport leave, resize, mode round trips, and scene replacement reject stale gestures and picks.
- [ ] Audit all 20 native viewport tools plus the separate selection/action/texture catalogs; aliases share owners and no later family disappears from the ledger.
- [ ] SDK-only extras and absent algorithms are labeled accurately; no CSG/atlas/subdivision claims are inferred from unrelated names.

### 11.2 Geometry, history, materials, and storage

- [ ] Primitive creation, one-face region extrusion, vertex transform, and material/UV edits produce expected topology and attributes.
- [ ] Undo/redo interleaved with ordinary scene transforms follows one chronological timeline.
- [ ] A continuous preview adds one accepted command; rejected/no-op changes preserve redo.
- [ ] Failed multi-object preparation/publication changes no participant partially.
- [ ] Old component IDs never alias newly allocated elements after cancel or undo branching.
- [ ] Save/reopen preserves loops, per-corner seams, hard/soft normals, face material slots, texture settings, transforms, and stable host identities.
- [ ] Missing material resources retain authored references with a fallback; failed saves leave the old scene usable.
- [ ] Large modeled meshes retain uint32 indexing and correct seam splits.
- [ ] UV scale/rotation/mirror, nonuniform/reflected placement, UV Peel density, and eligible quad correction match the intended sampling.
- [ ] Loop/ring/grow/shrink/connected selectors preserve native traversal behavior on boundaries, branches, n-gons and disconnected geometry; hover does not mutate selection.
- [ ] Every vertex/edge/face operation has ordinary, degenerate, rejected and no-op cases with expected ID/selection remaps and corner/material/normal preservation.
- [ ] Multi-object operations reconcile coordinate frames and local material slots before one atomic publication; no convenience call silently edits only the first object.
- [ ] Knife/Cut/Poly/Mirror and primitive variants retain correct phase transitions, snapping, provisional geometry and cancel behavior.
- [ ] Native view-relative nudges and duplicate/extrude variants perform the right compound operation and add one history step.
- [ ] Pivot/origin/bake/floor/Align preserve expected world placement, child transforms and texture sampling; Align clicks remain individual commands.
- [ ] Arrays honor total element budgets and fixed random seed, support zero/invalid/overlapping cases, and discard all provisional additions on Cancel.
- [ ] Curves preserve points/handles/IDs, closure and smoothness across edits and save/reopen; tube preview and solidification match the accepted curve.
- [ ] Path Array follows world arc length under nonuniform placement and correctly handles open/closed paths, forward-axis choices and rotation/scale variation.
- [ ] Linked instances update all occurrences through one canonical document; flatten creates independent data without changing placement or texture appearance.
- [ ] Save/reopen and undo/redo preserve definition/member/occurrence references; transient instance edit scope is not saved as authored data.
- [ ] Material-copy gestures retain target UVs; Continue Texture transfers source material and mapping through target-slot resolution; captured sources survive valid gestures and reject stale context.

### 11.3 Actual overlay pixels

- [ ] A cube has twelve authored edges and no face diagonals; n-gon wires follow only their boundary.
- [ ] Near/far geometry and perspective/orthographic cameras preserve the defined physical-pixel width.
- [ ] Test multiple viewport sizes and 100%/200% framebuffer scale without double-applying DPI.
- [ ] Near-plane crossings, end-on segments, short edges, and caps remain bounded and antialiased.
- [ ] Wires remain continuous on sloping/distant faces and disappear behind real foreground geometry, including imported models.
- [ ] Visible and hidden passes meet without duplicate alpha or depth gaps at equality.
- [ ] Hover/active/selection overlaps respect priority; face tint is consumed once.
- [ ] Silhouette bands follow selected occupancy, including groups, overlaps, thin features, and occlusion.
- [ ] A foreground imported object crossing a selected mesh does not create a false silhouette along the interior occlusion cut; hidden true boundaries appear only when enabled.
- [ ] Exposure changes affect scene shading while overlay appearance stays stable; final color is encoded once.
- [ ] Normal-map direction stays correct after UV mirror/rotation and under reflected placements.

### 11.4 Retention and failure behavior

- [ ] Warm production caches; change only preview positions repeatedly with stable topology and committed revisions; inspect submitted endpoints, support points, markers, depth, and pixels.
- [ ] Commit/cancel/zero displacement/undo/rejected candidate all retire stale preview presentation.
- [ ] Unchanged frames perform no topology extraction, mesh rebuild, or geometry upload.
- [ ] Camera-only motion changes occlusion without uploading ordinary retained wire geometry.
- [ ] Two objects sharing immutable authored geometry render at their separate placements and retain independent selection/preview presentation.
- [ ] Old pinned CPU snapshots remain unchanged during later previews/uploads.
- [ ] Resize/allocation failure preserves a coherent target bundle and rejects old pick results.
- [ ] Measure representative counts, upload bytes, and frame timings before claiming a performance improvement.

Useful Quader regression references include `ModelingSdkConsumerTests`, `ModelingSdkFaceExtrudeTests`, UV precision/action/peel/canvas suites, retained overlay submission tests, and GPU tests named `VulkanShadedWireRemainsContinuousOnObliqueSurfaces`, `VulkanGrazingShadedWireCannotCrossForegroundDepth`, and `VulkanEdgeLoopSelfOcclusionFollowsCameraWithoutRebuildingWorldGeometry`. Their Vulkan names describe Quader's existing fixtures; Crimson's initial pixel verification should run its actual D3D11 path. [Q32], [Q33], [Q34]

### 11.5 Tool Settings and Face Texture interaction

- [ ] Every implemented parameter has the correct type, units, initial/remembered value, hard/soft range and conditional enabled state; a native descriptor is not mistaken for a functioning SDK dispatch route.
- [ ] Numeric scrub evaluates from the captured baseline, coalesces intermediate input, and includes its final value before acceptance.
- [ ] Incomplete/invalid text blocks Accept; Escape discards the field draft; a failed candidate leaves the last accepted geometry intact with accurate displayed status.
- [ ] Cancel discards pending edits before focus-loss settlement; a late worker result cannot resurrect the preview.
- [ ] Switching tool, selection, instance scope or scene rejects old widget buffers and queued edits by generation.
- [ ] Panel collapse, focus movement between viewports, docking and resizing do not restart or silently accept a tool.
- [ ] Extrusion amount in meters is not divided by 32 again; UV pixels/tiles/degrees and primitive dimensions convert once.
- [ ] Direct text input does not quantize authoritative values through formatted display text; repeated scrubs do not accumulate geometry error.
- [ ] Mixed face Scale/Rotation remain mixed until edited; linked scale preserves per-face ratios; Shift fields require one face.
- [ ] All 28 texture actions and Treat as One preserve their distinct semantics on rotated/shifted/skewed UVs.
- [ ] Canvas translate/scale/rotate, modifier snapping, pan/zoom, release commit and Escape cancel use the same authoritative mapping as the 3D surface.
- [ ] Tool Settings and face canvas are inspected in actual Crimson ImGui at narrow docks and multiple framebuffer scales, including keyboard-only numeric entry and scene replacement during a draft.
- [ ] Unchanged UI frames do not rebuild descriptors, enumerate broad command catalogs, traverse topology or prepare geometry.

## 12. Source map and further reading

Local links point to the inspected checkouts. Use symbols and responsibilities to relocate code if active development changes file layout. The references are evidence and navigation, not a request to import native Quader packages into the SDK consumer.

| Reference | Start here |
| --- | --- |
| [Q1] | Portable SDK package guide and public boundary |
| [Q2] | SDK target construction, installation, and dependencies |
| [Q3] | Public polygon document and operation APIs |
| [Q4] | Public session, previews, checkpoints, history, and snapshots |
| [Q5] | Compatibility exceptions, typed extrusion, unsupported conveniences |
| [Q6] | Convenience mesh/render snapshot implementation |
| [Q7] | Mesh, authored topology, material, UV, and overlay payload contracts |
| [Q8] | UV Peel semantics and preparation interface |
| [Q9] | Native mode lifecycle reference |
| [Q10] | SDK-only build/install/consumer isolation fixture |
| [Q11] | Existing installed external consumer and ABI setup |
| [Q12] | Accepted preview projection and retained-consumer contract |
| [Q13] | Portable authored topology and QDR serialization |
| [Q14] | Face texture actions, projection, and transfer |
| [Q15] | Face texture canvas contracts |
| [Q16] | Native surface shader and occurrence texture sampling |
| [Q17] | Renderer-derived inverse-bilinear quad mapping |
| [Q18] | Final-UV normal mapping correction |
| [Q19] | Renderer-neutral native overlay records |
| [Q20] | Native overlay layer/depth policy |
| [Q21] | Authored edge extraction and incident support construction |
| [Q22] | Line vertex layout and support fields |
| [Q23] | Retained four-corner ribbon construction |
| [Q24] | Analytic line/dash coverage shader |
| [Q25] | Shaded topology wire, depth/cap/support treatment |
| [Q26] | Homogeneous incident-surface depth solver |
| [Q27] | GPU-expanded point/marker material |
| [Q28] | Face-fill priority and stencil composition |
| [Q29] | Selected object/group outline rendering |
| [Q30] | Persistent/dynamic retained overlay submission coordinator |
| [Q31] | Shared instanced wire resource backend |
| [Q32] | SDK consumer behavior tests |
| [Q33] | GPU overlay appearance regressions |
| [Q34] | Retained selected-mesh/preview presentation regressions |
| [Q35] | Actual convenience tool implementations and current limitations |
| [Q36] | Native tool-settings descriptor types; reference for the proposed Crimson schema |
| [Q37] | Native per-tool settings, choices, ranges, conditional fields and dispatch |
| [Q38] | Native generic Tool Settings panel, draft flushing and cancel ordering |
| [Q39] | Installed operation descriptors and parameter metadata |
| [Q40] | Primitive creation options and native lifecycle |
| [Q41] | Linear Array owner and default options |
| [Q42] | Radial Array owner and instance/repeat options |
| [Q43] | Path Array owner and grouped placement/rotation/scale options |
| [Q44] | Native Curve tool and creation/solidify settings |
| [Q45] | Complete 20-entry native viewport tool catalog |
| [Q46] | Native 43-entry action catalog and operation/settings presentation |
| [Q47] | Native 29-entry instant-operation catalog |
| [Q48] | Installed selection facade implementation, including hover limitations |
| [Q49] | Native selection API and expansion commands |
| [Q50] | Existing source-only topology selection algorithms, not installed SDK declarations |
| [Q51] | Native modeling intent API, pivots, arrangements and texture gestures |
| [Q52] | Installed authored curve document, operations and persistence |
| [Q53] | Installed world arc-length helper for Path Array |
| [Q54] | Installed curve tube preview and authored solidification |
| [Q55] | Installed Circle/Oval curve preset helpers |
| [Q56] | Face Texture numeric/action controls and mixed-selection behavior |
| [Q57] | Face Texture canvas gestures, snapping and mapping presentation |
| [Q58] | Transient native instance edit scope |
| [Q59] | Create/edit/flatten instance semantics and sampling compensation |
| [Q60] | Canonical definitions, occurrences, texture mapping and curve scene properties |
| [Q61] | Native-to-SDK composition, including structural Half-Cut preview |
| [Q62] | Command domains, visible versus compatibility routes and activation styles |
| [Q63] | Native command registration, texture routes, aliases and pointer targets |
| [Q64] | Native alignment and signed-gap distribution contracts |
| [Q65] | Camera-relative nudge and duplicate/extrude planning |
| [Q66] | Installed convenience operation dispatch and its actual coverage |
| [Q67] | Native input ownership, texture pointer gestures and click publication boundaries |
| [Q68] | Native modeling service composition, including additive Apply Solidify publication |
| [C1] | Crimson architecture and ownership direction |
| [C2] | Crimson build graph and shader compiler setup |
| [C3] | Crimson pinned dependency manifest |
| [C4] | Scene commands, transaction/history, and persistence API |
| [C5] | CPU scene render extraction |
| [C6] | Docked viewport, input arbitration, gizmos, and picking |
| [C7] | BGFX target lifetime, render passes, states, and view order |
| [C8] | Imported-model render vertex/material shape; reference for derived GPU data |
| [C9] | Editor/session composition and document transitions |
| [E1] | Official BGFX API, state, view ordering, and memory lifetime reference |
| [E2] | Official SDL3 drawable pixel-size reference |
| [E3] | Official Dear ImGui input/capture guidance |

[Q1]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/AGENTS.md>
[Q2]: <C:/Users/Drako/Desktop/quader-windows/quader-app/cmake/QuaderModeling.cmake>
[Q3]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/mesh/polygon_document.hpp>
[Q4]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/session/modeling_session.hpp>
[Q5]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/COMPATIBILITY.md>
[Q6]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/mesh_sync_io_api.cpp>
[Q7]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/payloads.hpp>
[Q8]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/mesh/uv_peel.hpp>
[Q9]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/modes/README.md>
[Q10]: <C:/Users/Drako/Desktop/quader-windows/quader-app/cmake/QuaderSdkOnlyBuildCheck.cmake>
[Q11]: <C:/Users/Drako/Desktop/quader-windows/quader-app/tests/sdk_consumer/CMakeLists.txt>
[Q12]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/modeling/runtime/README.md>
[Q13]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/io/qdr_document.hpp>
[Q14]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/mesh/face_texture_actions.hpp>
[Q15]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/mesh/face_texture_canvas.hpp>
[Q16]: <C:/Users/Drako/Desktop/quader-windows/quader-app/resources/materials/quader_mesh.mat>
[Q17]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/render/mesh_quad_uv_interpolation.hpp>
[Q18]: <C:/Users/Drako/Desktop/quader-windows/quader-app/resources/materials/mesh_normal_mapping.glsl>
[Q19]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/render/viewport_overlay_frame.hpp>
[Q20]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/render/viewport_overlay_policy.cpp>
[Q21]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/assets/mesh_wire_geometry.cpp>
[Q22]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/render/overlay/overlay_mesh_types.hpp>
[Q23]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/render/overlay/overlay_line_mesh_builder.cpp>
[Q24]: <C:/Users/Drako/Desktop/quader-windows/quader-app/resources/materials/overlay_line_coverage.glsl>
[Q25]: <C:/Users/Drako/Desktop/quader-windows/quader-app/resources/materials/quader_selected_object_line.mat>
[Q26]: <C:/Users/Drako/Desktop/quader-windows/quader-app/resources/materials/overlay_wire_surface_depth.glsl>
[Q27]: <C:/Users/Drako/Desktop/quader-windows/quader-app/resources/materials/quader_overlay_point.mat>
[Q28]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/render/overlay/overlay_face_fill_composition.cpp>
[Q29]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/render/overlay/selected_mesh_outline_renderer.cpp>
[Q30]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/render/overlay/viewport_overlay_submission_coordinator.hpp>
[Q31]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/render/overlay/instanced_wire_render_backend.hpp>
[Q32]: <C:/Users/Drako/Desktop/quader-windows/quader-app/tests/sdk/modeling_sdk_consumer_tests.cpp>
[Q33]: <C:/Users/Drako/Desktop/quader-windows/quader-app/tests/selection_overlay_runtime_tests.cpp>
[Q34]: <C:/Users/Drako/Desktop/quader-windows/quader-app/tests/selected_mesh_outline_presentation_tests.cpp>
[Q35]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/tools_api.cpp>
[Q36]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/api/quader_editor_api_types.hpp>
[Q37]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/api/quader_tools_api.cpp>
[Q38]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/ui/qt_tool_settings_panel.cpp>
[Q39]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/commands/command.hpp>
[Q40]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/modeling/services/primitive_creation_tool_service.hpp>
[Q41]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/array/linear_array_tool_service.hpp>
[Q42]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/array/radial_array_tool_service.hpp>
[Q43]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/array/path_array_tool_service.hpp>
[Q44]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/curve/curve_tool_service.hpp>
[Q45]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/tools/base/viewport_tool_descriptor_catalog.cpp>
[Q46]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/metadata/host_contract.cpp>
[Q47]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/modeling/services/modeling_instant_operation_catalog.cpp>
[Q48]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/selection.cpp>
[Q49]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/api/quader_selection_api.hpp>
[Q50]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/neutral/include/quader/modeling/neutral/polygon/document_selection.hpp>
[Q51]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/api/quader_modeling_api.hpp>
[Q52]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/curves/curve_document.hpp>
[Q53]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/curves/curve_arc_length_path.hpp>
[Q54]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/curves/curve_tube.hpp>
[Q55]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/curves/curve_shapes.hpp>
[Q56]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/ui/qt_face_texture_controls.cpp>
[Q57]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/ui/qt_face_texture_canvas.cpp>
[Q58]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/instances/instance_edit_context.hpp>
[Q59]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/instances/scene_instance_service.hpp>
[Q60]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/scene/components.hpp>
[Q61]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/modeling/services/modeling_sdk_projection_adapter.cpp>
[Q62]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/metadata/command_presentation_catalog.cpp>
[Q63]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/api/quader_commands_api.cpp>
[Q64]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/transform/mesh_arrangement_service.hpp>
[Q65]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/transform/viewport_nudge_plan.hpp>
[Q66]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/operations_api.cpp>
[Q67]: <C:/Users/Drako/Desktop/quader-windows/quader-app/INPUT_KEYBINDINGS.md>
[Q68]: <C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/modeling/services/modeling_session_service.cpp>
[C1]: <C:/Users/Drako/Desktop/_CRIMSON_ENGINE/crimson-engine/docs/ENGINE_ARCHITECTURE.md>
[C2]: <C:/Users/Drako/Desktop/_CRIMSON_ENGINE/crimson-engine/CMakeLists.txt>
[C3]: <C:/Users/Drako/Desktop/_CRIMSON_ENGINE/crimson-engine/vcpkg.json>
[C4]: <C:/Users/Drako/Desktop/_CRIMSON_ENGINE/crimson-engine/src/scene/SceneDocument.h>
[C5]: <C:/Users/Drako/Desktop/_CRIMSON_ENGINE/crimson-engine/src/scene/SceneExtraction.h>
[C6]: <C:/Users/Drako/Desktop/_CRIMSON_ENGINE/crimson-engine/src/editor/SceneViewport.cpp>
[C7]: <C:/Users/Drako/Desktop/_CRIMSON_ENGINE/crimson-engine/src/crimson-renderer/Renderer.cpp>
[C8]: <C:/Users/Drako/Desktop/_CRIMSON_ENGINE/crimson-engine/src/assets/ModelAsset.h>
[C9]: <C:/Users/Drako/Desktop/_CRIMSON_ENGINE/crimson-engine/src/editor/EditorSession.cpp>
[E1]: https://bkaradzic.github.io/bgfx/bgfx.html
[E2]: https://wiki.libsdl.org/SDL3/SDL_GetWindowSizeInPixels
[E3]: https://github.com/ocornut/imgui/blob/master/docs/FAQ.md

