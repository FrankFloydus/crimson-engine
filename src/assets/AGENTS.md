<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Assets Package Guide

`src/assets` owns asset documents, revisions, registries, typed stores, and baking services.

## Asset Rules

- `AssetRegistry`, typed asset stores, and asset documents are asset truth.
- UI/API code should request asset changes through public APIs, commands, or asset services instead
  of mutating registry internals directly.
- Keep asset revision semantics deterministic and explicit.
- Do not let render caches become the source of asset truth; render resources derive from assets and
  scene state.
- If behavior affects material or mesh asset contracts, add/update focused tests and any public docs
  that describe the contract.
