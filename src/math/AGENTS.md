<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Math Package Guide

`src/math` owns math value types and conversion helpers.

## Math Rules

- Keep math types small, deterministic, and independent of editor, UI, render, and platform owners.
- Use explicit conversion helpers for GLM bridging instead of scattering ad hoc casts.
- Preserve coordinate-system and precision semantics when replacing legacy math types.
- Add targeted tests for value behavior, conversions, transforms, and edge cases.
