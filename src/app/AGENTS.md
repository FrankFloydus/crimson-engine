<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# App Package Guide

`src/app` is runtime composition and application lifecycle glue.

## App Rules

- Keep domain behavior in editor, scene, asset, render, viewport, UI, or platform owners.
- App code wires services, startup/shutdown, frame flow, and top-level orchestration.
- User/editor mutations should route through `QuaderEditorApi`, command services, or owner services.
- Do not hide modeling, rendering, selection, or document logic in app startup code.
- Preserve the native UI frame order: process window events, draw/update ImGui UI, prepare viewport
  render data, then render viewport surfaces inside the Filament frame.
- Changes to window lifecycle, backend startup, or frame orchestration need a manual smoke check when
  they cannot be covered by headless tests.
