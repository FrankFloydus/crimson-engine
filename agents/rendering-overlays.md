<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Rendering And Overlays

Vulkan is the first runtime backend. Do not introduce OpenGL-only assumptions into shared code.

Backend-specific behavior belongs behind `RenderSystem`, `PlatformWindow`, or explicit backend
adapters so OpenGL and Metal can be added later.

## Render Ownership

- `SceneDocument` is editor truth.
- Filament entities, buffers, materials, and views are render proxies derived from document state.
- UI panels do not own Filament resources directly.
- Route rendering and texture requests through renderer-facing services.
- `SceneRenderBridge` is a renderer facade/composition root, not the owner of new render algorithms.
- Filament resource ownership and render extraction belong in `src/render/**`.

## Overlay Rules

- `src/render/ViewportOverlayPolicy.h` is the native app overlay presentation policy.
- Keep layer draw order, colors, alpha, widths, point sizes, and depth/draw-on-top decisions
  centralized there.
- Do not scatter overlay color constants through unrelated files.
- `SceneRenderBridge` renders face/fill triangle overlays and GPU screen-space wire/vertex overlays.
- Do not reintroduce raw Filament `LINES`/`POINTS`, ImGui wire fallbacks, or unfeathered
  triangle-strip shortcuts for selection overlays.
- Wire and vertex overlays are screen-space quads.
- Overlay materials live at:
  - `resources/materials/quader_overlay_line.mat`
  - `resources/materials/quader_overlay_point.mat`
- Filament device-domain overlay vertices must be normalized device coordinates with `w = 1`.
- Apply overlay depth bias in view space before projection.
- Lines must be clipped against the near plane before pixel expansion.
- Device-domain overlay renderables must disable renderable culling.
- Filament uses reversed depth. For depth-tested overlay passes, use `DepthFunc::GE`.

## Overlay Contract

- Depth-tested: `SourceWire`, `OpenEdge`, `HoverFaceEdge`, `HoverEdge`.
- Draw-on-top: `SelectedFaceEdge`, `SelectedEdge`, `DiagnosticEdge`, selected/hover/snap vertices.
- Face fills use depth-stamped two-pass behavior.
- Native layer order is:
  `SelectedVisibleFace`, `SelectedFaceMask`, `SourceWire`, `SelectedXrayFace`, `HoverFace`,
  `OpenEdge`, `DiagnosticEdge`, `SelectedFaceEdge`, `SelectedEdge`, `HoverFaceEdge`, `HoverEdge`,
  `UnselectedVertex`, `SelectedVertex`, `HoverVertex`, `SnapTargetVertex`.
