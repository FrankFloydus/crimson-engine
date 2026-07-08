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
- Public headers must not include native app, scene, asset, render, UI, platform, editor API,
  SDL, ImGui, Filament, EnTT, or JSON owner headers.
- Implementation files may adapt existing clean mesh and QDR owners during migration, but native
  app identity, command IDs, scene entities, materials, render bridges, and UI state stay outside
  the SDK.
- Prefer additive SDK contracts and tests before moving existing polygon or runtime source files.
- SDK consumer tests must include the umbrella header and link SDK targets only.
