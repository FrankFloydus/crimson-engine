<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Modeling Runtime Package

Native app modeling runtime behavior.

This package owns engine-clean editor modeling behavior over `quader_poly::Document` objects. It is
part of the native app, not an external engine adapter or SDK boundary. The namespace
`quader_editor_core` remains as the historical runtime namespace, but ownership is app-native.

Owns:

- polygon object runtime state and selection summaries;
- component and object hover/picking result state;
- source-wire and surface picking policy;
- transform previews and component drag behavior;
- runtime command execution over polygon documents;
- selection overlay frame generation as runtime primitives;
- rectangle selection, pick cycling, and selection-mode conversion helpers;
- tool state data for move, rotate, scale, cut, box, knife, and related modeling workflows;
- portable viewport math that does not own UI, render resources, or platform input capture.

Does not own:

- public API intent or command palette routing;
- scene identity, grouping, hierarchy, or document command history;
- render overlay style, Filament resources, or GPU submission;
- viewport workspace state, native windows, swapchains, or physical input capture;
- UI layout, runtime UI resources, localization, or panel behavior.

Selection and hover behavior should stay inspectable with headless modeling-domain tests. Real
window, GPU, and pixel-output behavior belongs to render/UI smoke checks until render readback
coverage exists.
