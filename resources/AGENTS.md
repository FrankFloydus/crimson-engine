<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Resources Guide

Read `../agents/ui-localization.md` for locale changes and `../agents/rendering-overlays.md` for
material/overlay changes.

## Resource Rules

- User-facing UI text belongs in `resources/locales/*.lang`.
- RmlUi shell resources under `resources/ui/**` are copied to the app runtime directory by the
  `quader` target post-build step. After changing RML, RCSS, UI tokens, or locale files, rebuild
  `quader`; building `quader_tests` alone does not refresh `build/vs2026/Debug/ui/**` or copied
  locales.
- When the running UI does not match source resources, inspect the copied runtime files under
  `build/vs2026/Debug` before assuming the source change failed.
- Keep static RCSS tied to layout mechanics. Prefer generated utility classes from palette and
  utility tokens for colors, sizing, spacing, borders, and typography.
- Treat MainLayout RML/RCSS IDs, data attributes, and class names as contracts with
  `MainLayoutRmlBuilder`, `UISystem` measurement, drag/drop registration, and UI guardrail tests.
  Update the owner code and tests in the same change when a resource contract changes.
- Keep the MainLayout bottom dock inside the editor column with the viewport. Resource changes must
  not make the bottom dock, its tab strip, split panes, drop indicator, or height resizer span under
  side docks or the status bar.
- Keep visual feedback separate from behavior. RCSS may style resizer lines, tab insertion lines,
  dock indicators, overlays, and drag previews, but accepted drop targets, non-droppable state,
  split constraints, and collapse policy belong in UI controllers/models.
- Do not encode panel-specific behavior in static resources. Stable panel IDs are data attributes
  projected by the builder; resources should provide reusable presentation classes and tokens.
- Material source changes under `resources/materials` must continue to compile through the configured
  Filament `matc.exe` path.
- Keep default material assets under the app-owned resources tree and preserve post-build copy paths.
- Do not add third-party fonts, icons, textures, or material assets without license evidence under
  `thirdparty/`.
- Update localization, render, or resource tests when resource contracts change.
