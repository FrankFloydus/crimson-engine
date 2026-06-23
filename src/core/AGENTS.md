<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Core Package Guide

`src/core` contains shared primitives such as logging, localization, command results, undo/redo, and
small value helpers.

## Core Rules

- Keep core code independent of UI, render, platform windowing, and specific editor workflows.
- Shared helpers should be narrow and reusable; do not turn core into a dumping ground for domain
  behavior.
- User-facing text belongs behind `LocalizationService` and locale resources.
- Undoable behavior should preserve command ordering, state transitions, and error handling.
- Add focused core tests for new parsing, formatting, localization, command-result, or undo/redo
  behavior.
