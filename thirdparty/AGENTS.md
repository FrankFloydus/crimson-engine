<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Third-Party Guide

Follow root `AGENTS.md` dependency, license, build, and guardrail rules before changing this
directory.

## Third-Party Rules

- Keep dependency version notes and license evidence under `thirdparty/`.
- Do not vendor Qt, UI backend libraries, spdlog, GoogleTest, or a new Filament SDK into this repo;
  use the manifest-managed native dependency path.
- Avoid GPL, AGPL, LGPL with unclear linking obligations, SSPL, BUSL, copyleft, or source-available
  dependencies unless explicitly approved.
- Any new dependency must have license evidence and a clear CMake/vcpkg integration path.
- Keep local source checkouts optional unless the existing build contract requires them.
