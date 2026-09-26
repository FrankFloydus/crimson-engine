////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_operation_input_hint_types.hpp"

#include <Qt>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

class QApplication;
class QObject;

namespace quader {

/// Cheap projection of a control's existing interaction owner, not input state.
struct QtContextualInputHintSourceState {
  std::uint64_t revision = 0;
  bool available = true;
  bool active = false;
};

using QtContextualInputHintObserver =
    std::function<QtContextualInputHintSourceState(Qt::KeyboardModifiers)>;
using QtContextualInputHintBuilder =
    std::function<std::vector<QuaderOperationInputHint>(Qt::KeyboardModifiers)>;

/// Register a QWidget or native QWindow surface and its callback lifetime.
/// Observers must not allocate or copy model snapshots. Their revision covers
/// label/eligibility changes; modifiers and source changes are tracked here.
void qt_register_contextual_input_hints(
    QObject &surface, QObject &lifetime, QtContextualInputHintObserver observe,
    QtContextualInputHintBuilder build);

/// Retained source stamp and precedence used by the status bar compositor.
/// Blocking contexts hide viewport help; active contexts own a local gesture.
struct QtContextualInputHintState {
  const QObject *source_owner = nullptr;
  std::uint64_t revision = 0;
  bool overrides_viewport = false;
  bool active = false;
  bool blocks_viewport = false;
};

/// Schedule the status presenter after routed events, including native drag
/// loops that temporarily suspend the ordinary viewport frame callback.
void qt_set_contextual_input_hints_refresh_callback(
    QApplication &application, QObject &lifetime, std::function<void()> refresh);

/// Select only the current capture/focus/hover owner. No control catalog scan.
[[nodiscard]] QtContextualInputHintState
qt_contextual_input_hints_state(QApplication &application);

/// Build once per changed semantic source stamp, after the caller's own gate.
[[nodiscard]] std::span<const QuaderOperationInputHint>
qt_contextual_input_hints(QApplication &application);

} // namespace quader
