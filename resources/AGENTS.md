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
- Material source changes under `resources/materials` must continue to compile through the configured
  Filament `matc.exe` path.
- Keep default material assets under the app-owned resources tree and preserve post-build copy paths.
- Do not add third-party fonts, icons, textures, or material assets without license evidence under
  `thirdparty/`.
- Update localization, render, or resource tests when resource contracts change.
