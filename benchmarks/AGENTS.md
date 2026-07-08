<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Benchmarks Guide

`benchmarks` contains opt-in performance baselines.

## Benchmark Rules

- Keep benchmarks out of the default build unless explicitly enabled.
- Benchmark code should measure a focused domain path and avoid changing product behavior.
- Prefer deterministic data and clearly named scenarios.
- If benchmark support needs a dependency, follow root `AGENTS.md` dependency rules first.
