<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Modeling SDK Guide

`src/sdk/core/modeling` owns the portable modeling SDK boundary.

## SDK Rules

- Public SDK headers live under `include/quader/modeling` and use the `quader::modeling`
  namespace.
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
