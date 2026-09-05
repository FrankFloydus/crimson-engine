<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Third-Party Guide

Read [THIRD_PARTY_MANIFEST.md](THIRD_PARTY_MANIFEST.md) for dependency pins and license evidence.
Root [AGENTS.md](../AGENTS.md) owns general dependency and build policy.

## Third-Party Rules

- Keep dependency version notes and license evidence under `thirdparty/`.
- Use vcpkg for manifest-managed libraries and explicit external locations for source-built
  dependencies. Do not copy Qt, spdlog, GoogleTest, or an installed Filament SDK into the repo.
- `filament/` holds the maintained patch and source pin, not a vendored SDK. Follow its
  [README](filament/README.md) when rebuilding or upgrading the dependency.
- Avoid GPL, AGPL, LGPL with unclear linking obligations, SSPL, BUSL, copyleft, or source-available
  dependencies unless explicitly approved.
- Any new dependency must have license evidence and a clear CMake/vcpkg integration path.
- Keep local source checkouts optional unless the existing build contract requires them.
- Preserve original license text, asset hashes, and provenance. Update versions, evidence, CMake
  validation and guardrails together when a dependency changes.
