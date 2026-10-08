<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Scene transforms

## Editable channels and parenting compensation

`TransformComponent` retains editable position, rotation and scale (`TRS`) and
an identity-default affine `parenting_offset` (`O`). The complete local matrix is
`O * TRS`; the world matrix is `parent_world * O * TRS`. Use
`scene_local_transform_matrix` for placement and
`scene_compensated_parent_matrix` for converting world edits into the editable
channel frame. Ordinary channel edits retain the stored offset.

Keep-world parenting changes the offset while retaining channels and custom
pivots. Offset compensation can remain on a root entity and can include shear
or reflection. Gizmo rotation and scale still reject requested channel edits
that cannot be represented as TRS in the compensated frame. Translation bases
are captured per entity: siblings need not have the same compensation.

Pivot rebasing and baking compensate descendants through their offsets rather
than decomposing inherited affine transforms. All before/after comparisons and
history snapshots include the offset.

## Keyboard nudges

[`make_viewport_nudge_plan`](viewport_nudge_plan.hpp) resolves screen-direction intent against
the captured camera and gizmo frame. Move adds one enabled grid interval, or one unit with grid
snapping disabled, preserving an off-grid offset. Rotate uses 45 degrees independently of angle
snapping. Direction ties retain +X, +Y, +Z, -X, -Y, -Z order. The perspective correction uses an
explicit frame up axis: Y for World and Object/Group Local frames, or Z (the supporting surface
normal) for component Local frames. Hammer's Z-up convention must not become a literal Quader
world-Z rule. Orthographic views skip this correction and use their mapped viewing axis.

The command boundary resolves the focused pane, pivot and World/Local setting. The modeling
transform owner applies the resolved operation through its existing preview and commit path,
including per-participant world-to-local conversion, occurrence compensation and sparse history.
Object Local rotation keeps each participant's authored frame and pivot semantics. A pending
edit rejects the nudge before any preview or history change; the selected tool is preserved.

Shift variants duplicate Object/Group selections through the structural transform transaction,
or extrude Face/Edge/Vertex selections through the portable topology preview before applying the
resolved transform. Each step keeps its creation and transform in one atomic history entry.
Vertex boundary chains extend through shared duplicate endpoints and walls; isolated boundary
vertices extend a quad. Interior vertices retain the selected apex and split its incident faces
around a support ring. The raw active grid size controls the ring setback, independently of move
snapping and also for rotation. Short spokes reuse existing neighbors. These rules are implemented
in the portable surface operation owner, with attribute and rollback regression coverage.

Every changed invocation has one undo entry. Nudge Repeat Last Action uses the same transform
transaction, including native-only and mixed scene selections; a generic modeling memento does
not capture those scene transforms. Repeat retains duplication/extrusion intent and its grid
size. Input repeat lifetime and live Shift ordering are specified in
[`INPUT_KEYBINDINGS.md`](../../../INPUT_KEYBINDINGS.md#mouse-and-held-key-contracts).

Behavioral evidence was inspected statically in the installed CS2 Hammer DLL with SHA-256
`d8c10d4a28665fe68f9186538228c4dbd17cb3e0cb284f8aaef0e02017286c11`; the board task #14 retains
the bounded address and keymap provenance. Quader's implementation is independent. Native OS
repeat and one undo entry per invocation are explicit Quader policy, not verified Hammer timing.
Regression coverage lives in [viewport_nudge_plan_tests.cpp](../../../tests/viewport_nudge_plan_tests.cpp),
[editor_api_tests.cpp](../../../tests/editor_api_tests.cpp),
[component_transform_persistence_tests.cpp](../../../tests/component_transform_persistence_tests.cpp)
and [scene_transform_tests.cpp](../../../tests/scene_transform_tests.cpp).

## Object arrangement

[`MeshArrangementService`](mesh_arrangement_service.hpp) owns Align and Distribute
in Object and Group modes. The existing `QuaderModelingApi::align_selected_meshes` and
`distribute_selected_meshes` entry points also accept instances and native spatial
objects, including lights, in mixed selections. They publish placement through
`DocumentCommandService::set_transforms`; they do not edit instance definitions or
modeling geometry.

Mesh and instance bounds come from displayed world geometry. Spatial leaves without
geometry use their world origins, while native containers combine descendant geometry
and spatial leaf origins without including container pivots. Selected descendants move
only through their topmost selected ancestor.

Alignment targets the minimum, center or maximum of the original aggregate selection
bounds on each requested world axis. The active object and selection order do not
change that reference. Distribution places equal signed gaps within the original
selection extent, retaining boundary-owning objects. It uses original center ordering
with entity IDs to break ties. Reject overlapping arrangements that cannot preserve
both equal spacing and the original outer boundaries; never expand the selection to
make them fit. Legacy center/gap request modes resolve to the same distribution. Use Snapping is
off by default. When enabled, the planner searches every supported world-unit grid
spacing from coarse to fine, independently of the current viewport preset. Quantized
movement must retain exact alignment, equal gaps and fixed outer boundaries; if no
candidate fits, the exact plan remains valid and reports that grid alignment was not
used. The tool presents that fallback as a status-bar warning.

`capture_baseline()` projects eligible current roots into immutable original bounds,
transforms and compensated-parent inverse bases. The pure `plan()` method combines
X/Y/Z intent against that baseline without querying or mutating live scene state.
The Align panel and immediate API operations share this planner; their existing
document command owner publishes texture-lock changes and one history entry per action.
Preflight locks, visibility, bounds and representable translations before publication.
Command availability remains a revision-gated structural query; exact bounds belong
to baseline capture and operation planning.

Observable regression contracts live in
[mesh_arrangement_tests.cpp](../../../tests/mesh_arrangement_tests.cpp).

[`AlignToolService`](align_tool_service.hpp) keeps one native panel active while every
Align or Distribute click acts immediately on the current selection and its current
bounds. Each changed action records one history entry. Undo, valid selection changes
and document saves retain the panel. Leaving Object/Group mode, losing two eligible
selection roots, or encountering hidden, locked, missing or unrepresentable targets
retires the owner and clears its settings and snapping result without changing history.
Its session publication resets the shared pane tool choice to Select, so the next
pointer gesture cannot reacquire the retired Align tool or reopen it when context recovers.
Eligibility and exact active-context validation run only when scene, selection, layer,
asset or mode revisions change; invalidation publishes once through native refresh.
Switching tools or Escape closes the panel without reverting authored actions.
No alignment or distribution choice is retained between clicks.
Use Snapping is the only remembered option and affects the next action without
changing existing placement. Settings remain in the originating viewport while the
shared scene publication updates every visible viewport.
The retained presentation contract is exercised by
[align_tool_tests.cpp](../../../tests/align_tool_tests.cpp).

## Mesh transform baking

[`MeshTransformBakeService`](mesh_transform_bake_service.hpp)
owns Bake Transforms, Bake Location, Bake Rotation,
and Bake Scale. The public entry point is `QuaderModelingApi::bake_transforms`.
These operations are available for editable authored polygon meshes in Mesh
(Object) selection mode.

Baking incorporates the chosen object transform channels into the authored mesh
and resets those channels. Location and rotation reset to zero; scale resets to
one. Untouched channels and parenting compensation retain their exact stored
values. Ordinary Inspector and
gizmo transforms continue to modify object placement without baking geometry.

For a mesh with world matrix `W_before` and its planned matrix `W_after`, transform
the local vertices by `B = inverse(W_after) * W_before`. This preserves world
positions even when selected ancestors are baked in the same operation. With no
changed ancestor, the common parent/compensation prefix cancels, leaving these
partial bake matrices for the editable `T * R * S` channels:

| Operation | Geometry transform |
| --- | --- |
| Location | Translation by `inverse(S) * inverse(R) * location` |
| Rotation | `inverse(S) * R * S` |
| Scale | `S` |
| Transforms | `T * R * S` |

The rotation conjugation is required when retained scale is nonuniform. A bake
must cancel retained placement before rounding the relative matrix to floats;
otherwise large scene coordinates can leak a translation into baked geometry.
The bake matrix may contain shear because mesh vertices can represent it.
An unselected child's offset absorbs the parent's change without decomposing the
child's transform or changing its editable channels. Preflight the entire
operation before publishing changes.

Bake Location and Bake Transforms reset location to zero in the compensated
channel frame; the retained parenting offset can keep that origin away from the
parent's origin.
Bake Rotation and Bake Scale preserve location. Baking must also preserve texture
appearance and corner correspondence, correct reflected winding, and isolate shared
mesh assets so unselected sharers remain unchanged. Geometry, mesh bindings, texture
mapping, and scene transforms form one undoable publication.

The behavior was checked against the local Blender source at
`C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/blender-main`, primarily
`source/blender/editors/object/object_transform.cc`: `apply_objects_internal`
contains rotation/scale conjugation and channel resets; `ignore_parent_tx` handles
child compensation. Quader uses its existing SDK projection and native history
owners for implementation.

Observable regression contracts live in
[mesh_transform_bake_tests.cpp](../../../tests/mesh_transform_bake_tests.cpp).
