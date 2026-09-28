<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Source Package Guide

Read the nearest package `AGENTS.md` before editing source. The
[architecture map](../architecture-review.md) links every package owner; root
[AGENTS.md](../AGENTS.md) owns shared policy, build requirements and verification.

## Source Rules

- Trace the public entry point through its state owner, transaction/publication and consumer before
  changing behavior. Reuse the existing owner; do not add cross-package mutation shortcuts.
- When adding or moving `.cpp` files, update the owning CMake target in `CMakeLists.txt` or
  `cmake/QuaderModeling.cmake`.
- Update the nearest owner guide only when its contracts or navigation change; do not duplicate
  root policy or copy the source's method catalog.

## Naming from the first edit

Read [`.clang-tidy`](../.clang-tidy) before writing C++; it is the naming authority.
Use these conventions immediately in production code, tests and delegated work:

| Declaration | Convention | Example |
| --- | --- | --- |
| Source files and directories | `snake_case` | `qt_asset_browser.cpp` |
| Namespaces, functions, methods, parameters, ordinary locals | `snake_case` | `refresh_assets`, `pane_id` |
| Classes, structs, aliases, template parameters, enum types and values | `CamelCase` | `AssetEntry`, `Scene` |
| Every `constexpr` variable, including function-local ones | `kCamelCase` | `kPaneIds`, `kMaximumBytes` |
| Global, class and static constants, including function-local `static const` | `kCamelCase` | `kDefaultEnvironment` |
| Ordinary non-static local `const` variables | `snake_case` | `const auto asset_path` |
| Private/protected non-static data members | `snake_case_` | `selected_asset_` |
| Mutable global/static data, including `inline static` class data | `snake_case`, no member suffix | `application_argc` |
| Macros | `UPPER_CASE` | `QUADER_SOURCE_DIR` |

Keep required external API/override spellings and configured exceptions, such as axis enum
values and function keys. Do not infer a convention from nearby legacy violations. Review new
declarations as they are written and validate changed translation units/affected headers before
handoff; fix task-introduced violations then, while the owning code is still being implemented.
Record the verified scope so a later commit does not repeat an unchanged naming audit.
