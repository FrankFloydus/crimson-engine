<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Resources Guide

Runtime resources belong here. Root [AGENTS.md](../AGENTS.md) owns the build contract;
[src/ui/AGENTS.md](../src/ui/AGENTS.md) and [src/render/AGENTS.md](../src/render/AGENTS.md) own
presentation and rendering integration.

## Resource Rules

- User-facing UI text belongs in `resources/locales/*.lang`.
- Qt starts from Fusion; explicitly requested styling can change fonts, palettes, stylesheets, and
  icons. Keep assets scoped and verify their readable, focused, selected, and disabled states.
- Runtime resource changes need the app build selected by root handoff policy (Release by default,
  Debug/Profile only when explicitly requested); tests alone do not refresh deployed files.
  If runtime differs from source, inspect the selected executable's resource directory. Some
  resources, including the dimension font, are embedded through Qt rather than copied loose.
- Keep logos, renderer-owned icons, textures, and material assets license-backed. Do not add
  third-party assets without license evidence under `thirdparty/`.
- Material source changes under `resources/materials` must compile through configured Filament
  `matc.exe`.
- Both mesh shaders share `mesh_normal_mapping.glsl` for sampled normal orientation. Assign
  tangent-space `material.normal` before `prepareMaterial`; assigning it afterward silently
  bypasses normal-map lighting. Shaded keeps neutral normals for Solid and unlit wire previews.
  Derivative reconstruction must retain Filament's packed-frame convention: normal-map +Y
  follows decreasing sampled V. Test against independent world normals, including half-turn
  tangent frames and identity-to-transformed UV continuity, rather than only comparing shaders.
  The shared vertex helper also selects reconstruction for reflected, nonuniformly scaled or
  sheared occurrences; only positive conformal transforms retain the interpolated packed frame.
- Textured Shaded uses a soft sky/ground fill, curved camera-facing response and a narrow cosine
  sheen. Keep normal-map detail and fixed roughness/metallic-independent shape cues; Solid and
  Rendered retain their separate lighting contracts. Check appearance on the GPU when tuning it.
- Both mesh shaders share `mesh_surface_grid.glsl`. It blends fixed configured colors with
  coverage-scaled alpha; transparent major slots exclude minor lines. Shaded applies it after
  modeling lighting, Rendered through post-lighting composition with instance context/opacity
  preserved. Material diagnostics remain independent of the grid.
- Perspective ground-grid colors are sRGB palette values decoded before ordinary alpha blending.
  Keep palette intensity separate from coverage: the default small/64-unit shades are 76/114,
  with full authored opacity. Verify the blend over black and textured beige in Shaded mode;
  matching grid/background colors do not imply a contrast-adaptation rule.
- The perspective construction grid occupies a fixed 1024-by-1024 world rectangle at the origin.
  Its coverage has no camera-distance or perimeter opacity fade; the visible edge is the grid's
  boundary. Shaded uses the viewport Background Color preference; the scene's Environment Clear
  Color applies only in Rendered mode. Preserve orthographic placement/fades
  separately; verify distant lines and the fixed corner in `RenderGroundGridRuntimeTests`.
- Keep default material assets under the app-owned resources tree and preserve post-build copy
  paths.
- Update localization, render, or resource tests when resource contracts change.
