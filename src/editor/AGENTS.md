<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Editor Package Guide

Follow root `AGENTS.md` domain ownership, public API, modeling, viewport/input, and testing rules
before editor work.

## Editor Rules

- User-triggered mutations belong in command services, editor services, or public API entry points.
- Document mutations should be undoable through document commands or `QuaderSceneBatch` when grouped.
- Modeling operations execute through modeling command/runtime services; do not call raw polygon
  operations from UI/test/script code to bypass missing editor behavior.
- `ModelingSessionService` remains a thin adapter over real owners.
- `delete_selection` is a split-route command. Component deletion goes through
  `DeleteSelectionTool` and `ModelingSessionService::deleteComponentSelection()`. Object/group
  deletion and any selected non-scene-root root entity go through the scene-aware document command route.
- Tool state machines belong under editor tools/interactions, not UI panels.
- Add synthetic behavior coverage for selection, history, tools, transform previews, overlays, and
  modeling workflows whenever those states can be inspected headlessly.
