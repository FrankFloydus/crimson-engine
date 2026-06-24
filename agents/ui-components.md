<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# UI Components

Read this before creating or changing retained UI components, buttons, toolbar controls, status
controls, chrome controls, panel headers, tab controls, or any generated RML markup.

## Component Model

- Use `UIComponent` as the retained component interface. Components produce backend-neutral
  `UIElement` trees from their current presentation state.
- Use `UIElement` and `UIClassList` for generated nodes. Do not concatenate ad-hoc markup when a
  component tree can express the structure.
- Use `writeRmlMarkup` and `escapeRmlText` as the RmlUi projection boundary. Do not add local RML
  escaping helpers.
- Keep static `.rml` resources as shell mount points and stable resource structure. Repeated,
  stateful, or behavior-bearing UI belongs in components.
- Keep component classes narrow. Atom components own primitive behavior such as buttons; organism
  components compose atoms into toolbar groups, chrome controls, panel headers, or status sections.

## Button Components

- Use `UIButtonComponent` as the base class for retained buttons. It owns the `<button>` element,
  optional label/icon content, and semantic button state attributes such as disabled, hovered,
  focused, pressed, active, and toggled.
- Use `UIIconId` for button icons. Do not pass provider-specific classes, file paths, or font glyph
  names through component constructors.
- Add concrete button variants by inheritance when the variant has stable behavior or visual
  contracts. Current window buttons use `UIChromeButtonComponent : UIButtonComponent`.
- Keep behavior attributes in the concrete class that owns that behavior. For chrome buttons, the
  source of truth for native action routing is `data-chrome-window-action`.
- Keep variant class lists exact and tested. For existing chrome buttons, preserve the current IDs,
  classes, and action strings unless the UI contract intentionally changes.
- Add toggle state only for buttons that are actually toggles. Do not introduce speculative full
  button matrices before the app has those controls.

## Ownership Boundaries

- UI components own presentation state and event attributes only. Editor/document mutations still
  route through `QuaderEditorApi`, typed commands, controllers, or the domain owner.
- `UISystem` is the composition root. It mounts component RML into shell slots but should not own
  component internals or duplicate component state.
- `MainLayoutRmlBuilder` remains the MainLayout projection owner. Do not move docking policy,
  resize policy, or `DockLayoutModel` state into generic components.
- User-facing labels must come from `LocalizationService` or locale resources. Stable internal IDs,
  data attributes, and action names are not localized.

## Tests And Build

- Add focused tests for each component's generated tree and behavior attributes.
- Update shell/resource guardrails when moving markup from `.rml` files into components.
- Add new `.cpp` files to `quader_core` in `CMakeLists.txt`.
- When changing `resources/ui/**`, rebuild the `quader` target as well as relevant tests because
  runtime resources are copied by the app target.
