<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Scene Ownership

- `SceneDocument` owns entity identity, components, ordered forest hierarchy, document render settings,
  selection, layers, and scene revisions. Parentless entities are authored roots, never children of a
  synthetic Scene entity. `children(kInvalidEntityId)` and sibling placement use the root order.
  Settings edits advance `scene_settings_revision()` and content revisions without invalidating
  geometry or hierarchy projections. Bounded modeling history does not own document settings.
  `SceneContentDocument` aggregates scene and assets and validates their references. Runtime session
  and project workspace lifetime belong to `src/editor/session`.
- `document/scene_entity_state.*` is the shared component inventory for persistence, history,
  clipboard, and rollback. Do not add a second live object-record store or allocator.
- Engine authoring is `EngineComponentsComponent` on these same entities and optional
  `SceneSettings::engine`; identity, names, transforms, hierarchy and history stay native.
  Immutable opaque bytes and declared UUID reference slots travel through the shared inventory.
  The host validates component schema; native code validates only the bounded envelope and
  reference metadata. Optional complete Engine asset UUID inventories drive saved/live/native-history
  retention; missing inventories stay unknown across roundtrip. Native history retains immutable bytes;
  retention observers keep only weak payload roots, never a second history or IO in mutation callbacks.
  Duplicate/paste remap declared references through the complete batch,
  preserving references outside it. Scene-wide selectors continue to target the original entities.
- Authored `SelectableComponent::selectable` is a positive permission, inherited through
  hierarchy and layer restrictions. `VisibilityComponent::selectable` remains a separate
  eligibility gate; selection consumers apply both without merging their authored state.
  `SceneDocument::set_selectable` edits permission; `set_selectability` edits that independent
  eligibility gate. QDR retains historical locked bits through explicit inversion in its codec.
- Authored placement is `parent_world * parenting_offset * editable_TRS`. Use the complete-local
  and compensated-parent helpers in `scene_transform.hpp`; `spatial_transform()` exposes editable
  TRS only. Structural keep-world edits change the affine offset without decomposing or baking it.
  Root entities may retain compensation, including shear or singular axes. World-space channel
  edits retain compensation and reject transforms that their editable TRS cannot represent.
- Keep naming in `SceneEntityNaming`; retain scene-owned hierarchy/occurrence projections and their
  revision gates. Render and UI caches derive from this state.
- [Light authoring](light_properties.md) owns units, temperature conversion, preset defaults,
  and legacy-value compatibility. Reuse `light_properties.*` across native consumers.
- `light_property_revision()` advances only when an existing Light entity's component properties
  change. Equal deltas with scene/render-component revisions prove light-only publication; creation,
  removal and unrelated component edits break that proof. Exact rollback restores this counter too.
- User mutations route through editor services/document commands/public API batches. Protect owner
  invariants with scene/document tests and user workflows with public API behavior tests.
