<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Editor modes

`SceneSession::editor_mode()` owns the transient editor context shared by all of
that scene's viewports. `EditorMode` is independent of component selection kind.
Object permits Mesh/Group selection; Edit permits Vertex/Edge/Face selection.
`EditorModeSupport` supplies the common operation/tool context policy. Public
editor intent belongs at `QuaderEditorApi::mode()`; its `QuaderEditorModeApi`
returns a detached state value and delegates transitions to this owner. The portable modeling SDK does not
depend on native editor modes.

## Reference model

The architecture adapts Blender's separation of explicit
[mode transitions](https://github.com/blender/blender/blob/main/source/blender/editors/object/object_edit.cc),
[participating objects](https://github.com/blender/blender/blob/main/source/blender/blenkernel/intern/layer_utils.cc),
and [mode-aware tool context](https://github.com/blender/blender/blob/main/source/blender/windowmanager/intern/wm_toolsystem.cc).
Blender's [Separate implementation](https://github.com/blender/blender/blob/main/source/blender/editors/mesh/editmesh_tools.cc)
also informs keeping extracted outputs outside the current Edit set. These are
behavioral references; Quader implements the contracts through its existing
scene, SDK, publication and history owners without copying Blender source.

## State and transitions

`EditorModeService::snapshot()` is a borrowed immutable view of mode, revision,
generation, component kind, active entity, participants and structural object
results. Each participant carries its authored UUID and current scene/modeling
identities. Initial entry resolves selected meshes; later committed component
selection synchronizes participants through this owner. Hover never changes
membership. SDK documents remain the sole owners of geometry and component selection.

Object Mode retains the active object's UUID independently of the selected set.
A new nonempty selection updates it to the selected primary; deselection preserves
it, and identity removal clears it. Only an entirely empty object selection may
fall back to that active mesh on Edit entry, subject to ordinary eligibility.
An active group is not expanded by this fallback. Explicit mode entry through
Tab or the mode API with no eligible target is a successful unchanged transition,
without settling drafts or reporting an error. Direct component-kind requests
permit an empty Edit context, so 1/2/3 work before selecting any mesh.

Entry resolves the selected eligible meshes, recursively expanding groups and
skipping hidden, locked, nonselectable and non-mesh objects. Viewport-local
visibility and the current instance-edit scope also constrain admission. An active
instance scope admits authored definition members through their visible, unlocked
occurrence; hidden definition storage alone does not make those members ineligible.
Selecting an ordinary mesh does not recursively enroll its children.

Validate the incoming context before
settling a pending operation. Failed settlement preserves the previous context
and draft. The optional retained-UI settlement callback runs before native tool
settlement. First admission clears that mesh's component selection once per open
scene; later entries reuse the SDK selection. The owner retains only admission
identity metadata, not another component-selection copy.

Object exit projects surviving participants and registered structural results as
whole-object selection. Compatible Select/Move/Rotate/Scale tools survive the
transition; unsupported tools return to Select. Mode switches create no history
entry. Document replacement resets the context and admission metadata; resident
scenes retain independent contexts.

Component-kind transitions also retire incompatible tools to Select across all
panes. Knife and Insert Edge Loop require Edge; Select/Move/Rotate/Scale remain
compatible with every component kind.

Direct Vertex/Edge/Face selection requests enter Edit through this owner when
needed, then select the requested kind. Direct Object selection returns through
the same transition owner. The toolbar exposes all four choices in both contexts
with defaults 1/2/3/4 and retains the independent Groups toggle; it has no separate
mode dropdown. Tab still toggles the scene context with its remembered component
kind. These entry points preserve settlement and the existing first-entry policy.

## Consumers and publication

Component picking and surface acquisition use eligible scene meshes, independently
of the participant list. Point, region, paint and traversal selection can enroll
another eligible mesh. Committed component selection synchronizes participants;
clearing components normally clears that list, while explicit mesh-body adoption
can retain an empty target. Entry from selected objects can also begin with empty
component targets. Outliner clicks retain their Object Mode requirement.

Retained component presentation combines meshes with selected components and the
last eligible hover candidate. Empty-space hover and pointer leave keep that
candidate; hovering another mesh transfers it without changing selection. Clear
the immediate highlight after a blank click or completed region while retaining
the candidate. Gizmo ownership clears both hover and candidate, without removing
selected components. The candidate is presentation state, never an editing lock or
another selection owner. See the [runtime projection contract](../modeling/runtime/README.md#component-candidate-presentation).

Empty component selection must never fall back to whole-object transforms. The
Inspector has a separate object-property intent: it captures the current
participants and may author their object transforms without selected components,
including participants made hidden or nonselectable after admission. Its scene, document-binding, mode
generation and authored-identity fences apply to every sample and commit. Generic
scene transforms and viewport tools retain their ordinary mode requirements.
Object-mode Inspector drafts may finish on their captured objects after a selection
transition; fresh preview samples must still match the current property scope.

Primitive creation is available in every selection kind. Its narrow creation
publication path retains the current Edit context and component selection (or
Groups selection), while the creation tool owns the new live target independently.
Ordinary whole-object creation APIs retain their Object Mode requirement. Persistent
Pivot resolves the mesh owning the active selected component without replacing
component selection; an empty component selection or tentative operation-preview
components supply no durable pivot target. Apply or cancel that preview first. Both
tools fence their retained targets by selection identity and mode generation.

Use the revision to invalidate retained mode-dependent presentation and caches.
Capture the generation with gestures, deferred picks and queued property input;
reject work from an earlier context, including an Edit→Object→Edit round trip.
An active-only revision refresh is not a mode transition: the Inspector must not
abandon valid drafts on that refresh. Fence its callbacks by mode generation and
the existing selection/binding identity instead.
Component geometry previews must also satisfy the existing
[projection contract](../modeling/runtime/README.md#live-preview-projection-contract).

Structural publication remaps consumed participants only to the operation's
survivor and preserves unrelated participants. Extracted outputs are registered
as object results and are not enrolled automatically; subsequent component selection
may target them directly. Include mode state in the existing
publication checkpoint so failure compensation restores it with the other owners.
State restoration alone does not publish selection or presentation; the enclosing
publication owner performs that projection.

Within one generation, structural Undo/Redo restores participant membership.
History from another generation preserves the current set and prunes invalid
identities without enrolling restored objects. Removing the final participant
exits to Object and advances the generation, so a later Undo does not re-enter
Edit. Exact failure compensation is separate from normal monotonic history
restoration.

Lifecycle, admission and remembered-selection coverage lives in
[`editor_mode_service_tests.cpp`](../../../tests/editor_mode_service_tests.cpp);
cross-mesh publication, rollback and history cases live in
[`editor_mode_structural_tests.cpp`](../../../tests/editor_mode_structural_tests.cpp).
