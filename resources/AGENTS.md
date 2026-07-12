<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Resources Guide

Follow root `AGENTS.md` UI, localization, rendering, overlay, dependency, and build/test rules
before changing resources.

## Resource Rules

- User-facing UI text belongs in `resources/locales/*.lang`.
- The Qt Widgets shell uses the stock `Fusion` style and standard Qt controls. Do not add UI theme
  stylesheets, palette files, custom fonts, or theme-specific icon packs under `resources`.
- Runtime presentation resources are limited to localization plus assets with non-theme purposes,
  such as the application logo and renderer-owned overlay icons. After changing them, rebuild
  `quader`; tests alone do not refresh `build/vs2026/Debug`.
- If the running UI differs from source, inspect copied runtime files under `build/vs2026/Debug`.
- Keep logos, renderer-owned icons, textures, and material assets license-backed. Do not add
  third-party assets without license evidence under `thirdparty/`.
- Material source changes under `resources/materials` must compile through configured Filament
  `matc.exe`.
- Keep default material assets under the app-owned resources tree and preserve post-build copy
  paths.
- Update localization, render, or resource tests when resource contracts change.
