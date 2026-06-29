<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Resources Guide

Read `../agents/ui-localization.md` for locales and `../agents/rendering-overlays.md` for
materials/overlays.

## Resource Rules

- User-facing UI text belongs in `resources/locales/*.lang`.
- Runtime UI resources include icons, fonts, and ImGui/material outputs copied by the `quader`
  target post-build step. After changing them, rebuild `quader`; tests alone do not refresh
  `build/vs2026/Debug`.
- If the running UI differs from source, inspect copied runtime files under `build/vs2026/Debug`.
- Keep icon and font assets license-backed. Do not add third-party fonts, icons, textures, or
  material assets without license evidence under `thirdparty/`.
- Material source changes under `resources/materials` must compile through configured Filament
  `matc.exe`.
- Keep default material assets under the app-owned resources tree and preserve post-build copy
  paths.
- Update localization, render, or resource tests when resource contracts change.
