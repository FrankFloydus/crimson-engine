<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Editor Metadata Module

Descriptor-only schema for Quader-facing editor metadata.

This app-owned module helps host integrations discover Quader-facing metadata
without reading a specific adapter implementation. It describes host intent; it
does not execute modeling commands.

Owns:

- operation descriptors
- operation option descriptors
- action descriptors for menus and operation panels
- operation panel descriptors
- tool descriptors
- selection mode descriptors
- shortcut descriptors
- gesture grammar descriptors
- transform-axis descriptors
- viewport view/default descriptors
- grid preset descriptors
- overlay layer descriptors
- overlay presentation policy descriptors
- preference descriptors
- preference group descriptors
- default material texture asset descriptors
- default generated texture descriptors
- default material recipe descriptors
- UI surface descriptors
- UI group descriptors
- UI placement descriptors
- editor/viewport/preview/input interaction descriptors
- tool group descriptors
- non-operation panel descriptors
- stable command presentation and discoverability descriptors, including
  operand-first Mesh, Vertex, Edge, and Face domains plus a dedicated UV
  workflow domain

Does not own:

- document topology mutation
- modeling runtime operation execution
- runtime command execution
- native ABI exports
- engine UI, input capture, rendering, or mesh upload

Current public entry points:

- `operation_catalog()`
- `action_catalog()`
- `operation_panel_catalog()`
- `tool_catalog()`
- `selection_mode_catalog()`
- `shortcut_catalog()`
- `gesture_catalog()`
- `transform_axis_catalog()`
- `viewport_view_catalog()`
- `grid_preset_catalog()`
- `viewport_defaults()`
- `overlay_layer_catalog()`
- `overlay_presentation()`
- `preference_catalog()`
- `preference_group_catalog()`
- `material_texture_asset_catalog()`
- `material_generated_texture_catalog()`
- `material_recipe_catalog()`
- `ui_surface_catalog()`
- `ui_group_catalog()`
- `ui_placement_catalog()`
- `interaction_catalog()`
- `tool_group_catalog()`
- `panel_catalog()`
- `command_presentation_catalog()`

Custom hosts should consume app-owned modeling domain and host-contract descriptors instead of
workflow-shaped editor adapters.

Preference descriptors are metadata only. They expose labels, value kinds,
defaults, ranges, groups, and operation/overlay links so hosts can build their
own settings UI and persistence without importing editor-specific state.

Overlay presentation and material descriptors are also metadata only. They
describe semantic layer ordering, blocking-preview color overrides, render
biases, default texture assets, generated texture recipes, and default material
values without exposing engine-specific shader names, property names, or resource handles.

Texture asset paths are relative to the native app resource tree. Engine packages
may copy or import those assets into engine-specific package/resource locations,
but the descriptor remains the source mapping for custom hosts.

UI surface, group, placement, interaction, tool-group, and panel descriptors are
metadata only. They name host-owned menus, toolbars, floating panels, menu
groups, toolbar groups, editor actions, viewport actions, preview/input
policies, and preference-backed panels, then describe which action, tool,
selection-mode panel, preference group, or viewport action should appear there,
including stable ordering, separators, and preview/tool visibility conditions.
Hosts still render their own controls and decide docking, animation, focus,
persistence, and engine-specific menu APIs.

`qdr_editor_metadata_consumer_smoke_tests` exercises this package like a tiny
custom host shell inside the `quader_modeling_domain_tests` aggregate: it builds
menus, toolbars, settings panels, shortcut targets, gesture targets, tool groups,
and preference-backed panels from public descriptors only. It is a descriptor
proof, not the final custom-engine modeling-session sample.

`qdr_custom_engine_modeling_runtime_smoke_tests` complements that descriptor
proof from the absorbed modeling-domain side: it links app-owned editor metadata,
modeling runtime, and QDR runtime IO targets directly, creates a session,
executes a modeling operation, compiles meshes, builds overlay/gizmo
frames, and roundtrips QDR without using workflow-shaped editor adapters.
