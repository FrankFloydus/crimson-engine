<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# UI Package Guide

Read `../../agents/ui-localization.md` before changing UI code. If UI changes editor state, also
read `../../agents/public-api-and-scripting.md`.

## UI Rules

- UI panels and controllers own presentation, routing, and frame input assembly only.
- User/editor intent should route through `QuaderEditorApi`, `QuaderApi`, typed commands, or owner
  services.
- Keep panel contents in `UIPanel` classes and route frame dependencies through `UIContext`.
- User-facing labels, menu text, panel titles, settings names, and status text must go through
  localization resources.
- Put shared sizes, paddings, colors, and visual constants in `UIStyleTokens` or layout helpers.
- Add/update localization and UI guardrail tests when user-facing UI text or placement changes.
