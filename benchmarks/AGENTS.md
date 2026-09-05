<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Benchmarks Guide

`benchmarks` contains opt-in performance baselines. Enable `QUADER_BUILD_PERFORMANCE_BENCHMARKS`
in the canonical native tree and build the relevant target listed in `CMakeLists.txt` or
`cmake/QuaderModeling.cmake`.

## Benchmark Rules

- Keep benchmarks out of the default build unless explicitly enabled.
- Benchmark code should measure a focused domain path and avoid changing product behavior.
- Prefer deterministic data and clearly named scenarios.
- Record configuration, scene/fixture, workload, and measured before/after values. Debug/Profile
  uses optimized dependencies for Tracy captures; Release represents the shipping artifact. Do not
  compare results across different scenes/configurations as proof of a speed improvement.
- If benchmark support needs a dependency, follow root `AGENTS.md` dependency rules first.
