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

- UI shell code owns presentation, backend integration, routing, and frame input assembly only.
- User/editor intent should route through `QuaderEditorApi`, `QuaderApi`, commands, or owners.
- The current v1 shell is the RmlUi MainLayout shell: static chrome RML, generated MainLayout body,
  measured viewport frame, and `ViewportHost` rendering/input.
- Keep `UISystem` as composition glue. MainLayout policy belongs in controllers/models/builders;
  generic Rml setup, events, measurement, and rendering belong in `RmlUiContextHost`.
- MainLayout resize must stay on the measured-hit-region path through `ResizableSplitController`.
- Keep panel routing IDs stable and backend-neutral (`viewport`, `outliner`, `inspector`); localized
  panel titles are display text only.
- `UISettings` persists live UI shell preferences only; viewport rendering preferences remain in
  `ViewportSettings`.
- Preserve frame order: measure Rml before viewport input, then prepare viewport render data.
- Do not recreate the deleted docked panel stack unless a new backend-neutral panel architecture is
  intentionally introduced.
- User-facing labels, menu text, panel titles, settings names, and status text must go through
  localization resources.
- Prefer backend-neutral helpers or generated utility classes for shared visual constants.
- Add/update localization and UI guardrail tests when user-facing UI text or placement changes.
- When changing RML, RCSS, UI tokens, or locale resources, rebuild `quader` as well as tests.
