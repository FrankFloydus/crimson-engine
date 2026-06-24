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
- RmlUi shell resources under `resources/ui/**` are copied by the `quader` target post-build step.
  After changing RML, RCSS, UI tokens, or locale files, rebuild `quader`; tests alone do not refresh
  `build/vs2026/Debug/ui/**` or locales.
- If the running UI differs from source, inspect copied runtime files under `build/vs2026/Debug`.
- Keep static RCSS tied to layout mechanics. Prefer generated palette/utility classes for visual
  constants.
- Treat MainLayout RML/RCSS IDs, data attributes, and class names as contracts with
  `MainLayoutRmlBuilder`, `UISystem` measurement, drag/drop registration, and guardrail tests.
- Keep the MainLayout bottom dock inside the editor column with the viewport.
- Keep visual feedback in RCSS/RML; accepted targets, constraints, and collapse policy belong in UI
  controllers/models.
- Do not encode panel-specific behavior in static resources.
- Material source changes under `resources/materials` must compile through configured Filament `matc.exe`.
- Keep default material assets under the app-owned resources tree and preserve post-build copy paths.
- Do not add third-party fonts, icons, textures, or material assets without license evidence under
  `thirdparty/`.
- Update localization, render, or resource tests when resource contracts change.
