<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Modeling SDK Guide

This package owns portable polygon and curve documents, modeling operations, sessions,
preview transactions, history, typed identities, neutral payloads, and binary QDR document IO.

## Find the owner

- `include/quader/modeling/modeling.hpp`: public umbrella; declarations under `include/` use
  `quader::modeling`.
- `src/`: public API implementations; `session.cpp` owns workspace/history/preview state,
  `polygon_document.cpp` and `curve_document.cpp` own portable document facades.
- `include/quader/modeling/mesh/stairs.hpp`, `src/stairs.cpp`: stairs/ramp preset
  generation and fitted dimensions; [portable tests](../../../../tests/sdk/modeling_stairs_tests.cpp)
  protect closed boundaries, landings, equal risers and body clearance. The
  [surface topology contract](src/stairs.md) defines flight guides, minimal wall panels and landing patches.
- [neutral/README.md](neutral/README.md): source-only declarations shared with native adapters.
- [internal/mesh/polygon/README.md](internal/mesh/polygon/README.md): polygon implementation.
- [internal/mesh/geometry/README.md](internal/mesh/geometry/README.md): geometry implementation.
- [COMPATIBILITY.md](COMPATIBILITY.md): retained public compatibility contracts.
- `cmake/QuaderModeling.cmake` at repository root: target/source ownership and SDK packaging.

## SDK Rules

- Public headers must not include native app, scene, asset, render, UI, platform, editor API, Qt,
  SDL, ImGui, Filament, EnTT, or JSON owner headers.
- Low-level neutral geometry/polygon headers live under `neutral/include/quader/modeling/neutral`
  for source-only sharing by SDK and native owner targets. They are not installed public SDK
  contracts; native code must not include SDK-private `internal` headers.
- Native app identity, command IDs, scene entities, materials, render bridges, and UI state stay
  outside the SDK. Host behavior adapts around SDK-owned modeling state.
- Public compatibility exceptions are listed in `COMPATIBILITY.md`; do not add another overload,
  placeholder, or switch without an owner, replacement, behavior test, and revisit version.
- SDK consumer tests must include the umbrella header and link SDK targets only.
- Portable QDR is a binary byte/span contract. Do not add JSON readers, writers, migration
  helpers, or native runtime compatibility targets under this boundary.
- `from_authored_topology()` validates modeling imports while retaining finite collapsed faces
  and coincident vertices; invalid connectivity, including nonmanifold edges, remains rejected.
  `from_persisted_topology()` and QDR retain raw
  authored records without modeling validation, including malformed loops, attributes and IDs.
  Persistence enforces binary bounds and representability; it must not reject or clean up bad
  topology. Keep header/version, CSR bounds/coverage, size and checksum checks intact. Raw
  component indexes use generation one because QGEO has no component-generation field.
  Derive only safe rendering/editing views after loading; never replace the retained authored
  payload with compiled triangles or a cleaned document. Strict `PolygonDocument::validate()`
  and validated editing entry points keep their diagnostic and rejection contracts.
- Preserve allocation high-water marks across history and preview rollback; stale IDs must not
  alias newly authored elements. Failed and unchanged edits preserve authored state and history.
- Polygon rejections use `OperationResult::failure()` (or the specialized result's failure
  factory). Success is independent of `changed`; already-applied values remain successful no-ops.
  Preserve failure through SDK and native adapters before publishing any candidate state.
- Cover portable semantics in `tests/sdk/` consumer/parity suites and polygon mechanics in
  `tests/modeling_domain/operation/`; native tool and projection tests belong to their host owners.
