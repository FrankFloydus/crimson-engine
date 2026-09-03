////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_input_types.hpp"

#include <QObject>

#include <functional>

class QEvent;
class QWidget;

namespace quader {

class QuaderEditorApi;

using QtViewportToolStateProvider = std::function<ViewportToolState()>;
using QtApplicationCommandInputOwnerProvider = std::function<bool()>;

[[nodiscard]] ViewportToolState
qt_command_dispatch_tool_state(const QtViewportToolStateProvider &provider,
                               int grid_preset);

[[nodiscard]] bool qt_application_command_input_owner_active(
    const QtApplicationCommandInputOwnerProvider &provider);

/**
 * Returns whether a retained auxiliary window currently owns keyboard input.
 * The focus-chain check covers platforms and headless tests where activation
 * notification can lag behind QWidget focus delivery.
 */
[[nodiscard]] bool qt_window_owns_command_input(const QWidget *window);

/**
 * Routes application-level Qt key presses through Quader's canonical command
 * and keybinding API. Viewport surfaces keep ownership of viewport-scoped
 * input; exact global commands execute at the event boundary so their effects
 * precede later window events. Focused Qt controls retain standard behavior.
 */
class QtCommandEventFilter final : public QObject {
public:
  explicit QtCommandEventFilter(QuaderEditorApi &editor,
                                QtViewportToolStateProvider tool_state_provider,
                                QObject *parent = nullptr);

  QtCommandEventFilter(const QtCommandEventFilter &) = delete;
  QtCommandEventFilter &operator=(const QtCommandEventFilter &) = delete;

  void rebind_editor(QuaderEditorApi &editor) noexcept;
  void set_command_input_owner_provider(
      QtApplicationCommandInputOwnerProvider provider);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  [[nodiscard]] static bool modal_or_popup_owns_input();
  [[nodiscard]] static bool viewport_surface_has_focus(QObject *watched);
  [[nodiscard]] static bool text_input_has_focus();
  [[nodiscard]] static bool is_text_input_widget(const QWidget *widget);
  void reset_pending_chord();

  QuaderEditorApi *editor_ = nullptr;
  QtViewportToolStateProvider tool_state_provider_;
  QtApplicationCommandInputOwnerProvider command_input_owner_provider_;
};

} // namespace quader
