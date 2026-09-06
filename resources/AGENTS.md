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
- In `quader_mesh_lit.mat`, assign tangent-space `material.normal` before `prepareMaterial`;
  assigning it afterward silently bypasses the intended normal-map lighting input.
- Keep default material assets under the app-owned resources tree and preserve post-build copy
  paths.
- Update localization, render, or resource tests when resource contracts change.
