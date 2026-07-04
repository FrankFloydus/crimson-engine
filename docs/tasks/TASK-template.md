<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# TASK - Short Task Title

Internal architecture task template. Use this for migration tasks, owner extractions, guardrails, and
regression-prone fixes. Completed task files should remain internal unless intentionally rewritten as
public documentation.

## Status

Planned | In Progress | Blocked | Complete

## Scope

- Goal:
- Non-goals:
- Allowed files or modules:
- Affected owners:
- Related migration-plan item:

## Required Reading

- `AGENTS.md`
- `docs/AGENTS.md` when docs are affected
- `agents/project-identity.md`
- `agents/public-api-and-scripting.md`
- `agents/domain-ownership.md`
- `agents/testing-build.md`
- Relevant nested `AGENTS.md` files
- Relevant domain guidance:

## Source-Of-Truth Search

Record existing implementations, helpers, command paths, projections, caches, conversions, and tests
that already own the behavior. State which owner will be extended or why a new owner is needed.

## Implementation Checklist

- [ ] Preserve native app identity and current observable behavior.
- [ ] Route user/editor intent through `QuaderEditorApi`, `QuaderApi`, typed commands, or the owning
      service.
- [ ] Avoid duplicate mutation, projection, cache, conversion, command, or validation logic.
- [ ] Add or update guardrail/behavior tests when source-of-truth drift is being fixed.
- [ ] Update internal docs or public docs manifest when docs are added or reclassified.
- [ ] Keep Lua documentation non-executable until the native app has a Lua runtime and bindings.

## Verification Plan

- Affected-check plan:
- Focused build/test:
- Full or final verification:
- Manual smoke, only if GPU/window/pixel behavior requires it:

## Completion Notes

- Files changed:
- Invariants preserved:
- Tests/builds run:
- Follow-up tasks:
