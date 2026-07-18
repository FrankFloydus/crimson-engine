<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Modeling Runtime Projection Package

Native host interaction and short-lived modeling projection behavior.

This package operates on ephemeral `quader_poly::Document` views projected from the portable SDK.
It is part of the native app, not an external engine adapter or SDK boundary. The historical
`quader_editor_core` namespace remains for compatibility, but `quader::modeling` owns every durable
modeling document, selection, operation, preview, and history transition.

Owns:

- transient polygon-object projection records and selection summaries;
- component and object hover/picking result projection;
- source-wire and surface picking policy;
- host transform-drag input state and preview routing;
- native command metadata and portable-operation mapping;
- selection overlay frame generation as runtime primitives;
- rectangle selection, pick cycling, and selection-mode conversion helpers;
- tool state data for move, rotate, scale, cut, box, knife, and related modeling workflows;
- portable viewport math that does not own UI, render resources, or platform input capture.

Does not own:

- durable mesh documents, modeling-object/component selection, operations,
  preview transactions, or modeling undo/redo;
- public API intent or command palette routing;
- scene identity, grouping, hierarchy, or document command history;
- render overlay style, Filament resources, or GPU submission;
- viewport workspace state, native windows, swapchains, or physical input capture;
- UI layout, runtime UI resources, localization, or panel behavior.

`EditorSession` values in this package are disposable projections used by retained native picking,
overlay, bounds, and input helpers. Production code must never retain one as a competing source of
truth. Selection and hover behavior stays inspectable with headless modeling-domain tests. Real
window, GPU, and pixel-output behavior belongs to render/UI smoke checks until render readback
coverage exists.
