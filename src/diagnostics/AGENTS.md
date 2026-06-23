<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Diagnostics Package Guide

`src/diagnostics` is for profiling and diagnostic hooks.

## Diagnostics Rules

- Diagnostic hooks must not change observable editor behavior when disabled.
- Keep dependencies lightweight and gated behind existing build options such as Tracy support.
- Do not introduce UI, render, document, or modeling ownership here.
- Prefer compile-time or no-op disabled paths over runtime overhead in hot loops.
