# SDK And Unity Parity

Treat the Unity plugin at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity` as the source of truth
for Quader modeling behavior, viewport visuals, grid behavior, gizmo visuals, keybinds, overlays,
selection, and tool interaction.

Do not invent native-app behavior when parity is unclear. Inspect the Unity plugin and copied SDK
first, then implement matching behavior.

Important SDK/editor-core files:

- `sdk/packages/editor-core/src/session_selection_overlay.cpp`
- `sdk/packages/editor-core/src/selection_policy.cpp`
- `sdk/packages/editor-core/src/session_picking.cpp`
- `sdk/packages/editor-core/src/overlay_visibility.cpp`
- `sdk/packages/editor-core/include/quader_editor_core/session.h`

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

## SDK Copy

If SDK files are changed in this copied SDK, mirror accepted changes into the active Unity SDK
source of truth at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity\sdk`.
