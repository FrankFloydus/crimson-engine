<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Editor Metadata

Native descriptor catalogs describe presentation and discoverability; they do not execute commands
or own modeling, UI, input capture, or render resources.

- [host_contract.hpp](host_contract.hpp) declares the catalogs and narrow descriptor lookups for
  operations/actions/options, tools and input, viewport/overlay defaults, preferences, materials,
  and UI placement. [host_contract.cpp](host_contract.cpp) supplies their values.
- [command_presentation_catalog.hpp](command_presentation_catalog.hpp) owns stable command domains,
  labels, and discoverability, including Mesh, Vertex, Edge, Face, and UV workflows.
- Preferences expose defaults/ranges/groups, not a second settings store. Overlay/material records
  expose semantic values, not shader names or GPU handles. Texture paths are relative to app resources.
- UI owns rendering, docking, focus, and persistence. Use narrow descriptor lookup for retained
  controls; do not enumerate whole catalogs from frame callbacks.
- `release_feature_requirements.*` maps canonical native commands, actions, operations, tools and
  semantic input to the immutable executable release catalog in `core/release_features.hpp`.
  Keep raw identities registered for shortcut persistence; filter presentation and enforce the
  same requirements before invoking owner mutations or canceling competing tools.

Descriptor contracts and a minimal consuming shell are tested in
[tests/modeling_domain/editor_metadata](../../../tests/modeling_domain/editor_metadata), built by
`quader_modeling_domain_tests` (or the focused metadata loop). Native runtime integration coverage is
[the custom-engine smoke test](../../../tests/modeling_domain/qdr_custom_engine_modeling_runtime_smoke_tests.cpp).
External SDK consumers start from `quader/modeling/modeling.hpp`; this native metadata package is
not the portable SDK boundary.
