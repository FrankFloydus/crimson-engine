<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Internal Agent Mistakes

Internal architecture document. This file records recurring implementation mistakes that create
source-of-truth drift in the native Quader app. Use it as a review checklist before changing code or
docs.

## Treating Internal Owners As Public API

Mistake: Starting public docs, examples, scripts, tests, UI workflows, or command palette paths from
`SceneDocument`, `AssetRegistry`, `ModelingSessionService`, `SceneRenderBridge`, `RenderSystem`, or
raw editor runtime internals.

Correction: Public-facing workflows start from `QuaderEditorApi`, `QuaderApi`,
`QuaderObjectHandle`, `QuaderSceneBatch`, `QuaderScript`, typed command IDs, or future Lua `quader`
bindings after Lua exists. Internal owner discussion belongs only in clearly labeled internal docs.

## Adding Lua Examples Before Lua Exists

Mistake: Adding executable Lua snippets or files while the native app has no Lua runtime, binding
layer, script console, startup loader, dependency decision, or sandbox policy.

Correction: Document Lua only as planned behavior or binding design until runtime support exists.
When implemented, Lua must bind public API surfaces only and keep startup scripts disabled until a
sandbox policy exists.

## Duplicating Owner Logic Near A Call Site

Mistake: Copying selection, transform, hierarchy, asset validation, projection, cache invalidation,
command routing, or QDR conversion logic into UI, render, API, or test code because that caller needs
a similar result.

Correction: Search for the existing owner first. Route through the owner when it exists. If no owner
exists, create the narrowest owner in the domain that owns the data and invariants, then update all
call sites to use it.

## Bypassing Public API For Editor Intent

Mistake: Letting UI panels, shortcuts, automation, behavior tests, or command palette work mutate
document, modeling, viewport, preferences, or project state directly because the public API is
missing a convenience method.

Correction: Add the owner behavior first, then expose it through `QuaderEditorApi`, `QuaderApi`, a
typed command, or a focused public API domain. Keep low-level calls only for owner internals,
hot-path mechanics, backend resources, storage, or platform state.

## Widening Broad Facades

Mistake: Adding new ownership to `ModelingSessionService`, `ProjectSession`, `UIContext`,
`SceneRenderBridge`, or `RenderSystem` because the facade is already reachable.

Correction: Treat those broad surfaces as migration risks. Add focused owners for the invariant and
keep facades thin while call sites migrate.

## Splitting Behavior Without Preserving State Transitions

Mistake: Moving code during a refactor but changing undo/redo ordering, revision stamps, cache
invalidation, selection sync, material/mesh side effects, runtime projection, preview state, or QDR
compatibility.

Correction: Preserve observable behavior first. If behavior is being restored, copy the previous
working logic into the current architecture before redesigning. Add behavior coverage for the state
that used to desynchronize.

## Letting UI Or Render Own Canonical State

Mistake: Deriving scene truth, object selection, material bindings, mesh identity, overlay semantics,
or viewport workspace state inside UI or render code.

Correction: UI assembles presentation and routes intent. Render consumes extraction/snapshot data and
owns GPU resources. Canonical scene, asset, modeling, selection, history, and viewport workspace
state stays in the domain owners.

## Ignoring The Dirty Worktree Rule

Mistake: Reverting or overwriting unrelated edits because they make the diff noisy.

Correction: Assume concurrent changes are user or worker edits. Read affected files, adapt when they
overlap with the task, and leave unrelated changes alone unless the user explicitly asks for a
revert.
