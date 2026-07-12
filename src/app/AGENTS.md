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
- `QApplication` owns the event loop. The app-owned `QTimer` schedules refresh work, scene/render
  synchronization, viewport preparation, and submission to each visible native-child Filament
  surface; do not create a competing event or frame loop.
- `QMainWindow` construction and retained-widget composition belong to `UISystem`, not app
  startup. App code owns only lifecycle, service wiring, persisted top-level placement, and frame
  orchestration.
- Changes to window lifecycle, backend startup, or frame orchestration need a manual smoke check when
  they cannot be covered by headless tests.
