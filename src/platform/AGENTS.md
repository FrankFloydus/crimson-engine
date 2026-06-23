<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Platform Package Guide

`src/platform` owns native window/platform integration and fatal-error handling.

## Platform Rules

- Keep platform-specific behavior behind platform abstractions.
- Do not leak SDL, OS, or backend details into editor, scene, mesh, or API packages.
- Window/backend lifecycle changes need manual smoke coverage when headless tests cannot inspect
  them.
- Preserve clear fatal-error reporting without creating extra user-visible console windows unless
  explicitly required.
