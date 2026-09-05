<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Platform Package Guide

- Own native Windows support, file dialogs, window-placement utilities, raw mouse registration,
  and fatal-error handling. `QApplication`/`QMainWindow` lifecycle remains in app/UI.
- Keep OS details behind platform abstractions. `raw_mouse_input.*` reports motion and capture
  postconditions; Qt adapters own event translation and viewport owners consume semantic input.
  For input changes read [INPUT_KEYBINDINGS.md](../../INPUT_KEYBINDINGS.md).
- `fatal_error_handler.*` owns fatal runtime hooks and native presentation. `src/main.cpp` installs
  them and startup logging; `src/core/logger.*` owns shared logging policy. UI/services must not
  install global hooks or duplicate crash presentation. Do not create an extra visible console.
- Fatal handling writes the text/log report before optional diagnostics. Keep DbgHelp calls in the
  standalone `fatal_diagnostics_main.cpp` helper, with a bounded parent wait. The helper links no
  Quader, Qt, Tracy or Sentry state and ships beside both native executables.

Verification pointers: `tests/fatal_error_handler_behavior_tests.cpp`,
`tests/qt_relative_pointer_input_tests.cpp`, and native source guardrails. Use
`tools/boot_smoke.ps1` for native window/capture behavior that headless tests cannot inspect.
