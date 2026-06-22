<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# SDK And Unity Parity

Treat the Unity plugin at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity` as the source of truth
for Quader modeling behavior, viewport visuals, grid behavior, gizmo visuals, keybinds, overlays,
selection, and tool interaction.

Do not invent native-app behavior when parity is unclear. Inspect the Unity plugin and original
Unity SDK first, then implement matching behavior in native-app-owned modules.

Former SDK/editor-core reference files after absorption:

- `src/modeling/runtime/session_selection_overlay.cpp`
- `src/modeling/runtime/selection_policy.cpp`
- `src/modeling/runtime/session_picking.cpp`
- `src/modeling/runtime/overlay_visibility.cpp`
- `src/modeling/runtime/session.h`

Important Unity presentation files:

- `Editor/Viewport/QuaderViewportRenderer.cs`
- `Editor/Viewport/QuaderOverlayMeshCache.cs`
- `Editor/Viewport/QuaderViewportSettingsState.cs`
- `Runtime/Shaders/QuaderOverlayLine.shader`
- `Runtime/Shaders/QuaderOverlayPoint.shader`
- `Runtime/Shaders/QuaderOverlayFlat.shader`

## Transform Preview

- Unity Shift+gizmo behavior is the reference.
- Shift duplicates only in object selection mode for Move/Rotate/Scale.
- In component modes Shift maps to topology behavior (`Move -> Extrude`, `Scale -> Inset`) and must
  not duplicate objects.
- SDK transform previews are transactions.
- `begin_transform_preview(true)` creates a zero-offset duplicate, selects the duplicate, and uses
  that post-duplicate state as the preview base.
- Preview update deltas are absolute from drag start, not accumulated frame deltas.
- Native transform component inputs and viewport gizmo transforms should both behave in global/world
  space unless a future product requirement explicitly introduces local-space editing.

## Native-App Ownership

The top-level copied SDK boundary is retired for this native app. Treat absorbed former SDK files as
app-owned implementation and note any cross-repo parity follow-up instead of reintroducing
copied-SDK build semantics.
