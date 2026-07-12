////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QObject>

class QEvent;
class QWidget;

namespace quader {

class QuaderEditorApi;

/**
 * Routes application-level Qt key presses through Quader's canonical command
 * and keybinding API. Viewport surfaces keep ownership of viewport-focused
 * input, while text editors retain their normal editing shortcuts.
 */
class QtCommandEventFilter final : public QObject {
public:
  explicit QtCommandEventFilter(QuaderEditorApi &editor,
                                QObject *parent = nullptr);

  QtCommandEventFilter(const QtCommandEventFilter &) = delete;
  QtCommandEventFilter &operator=(const QtCommandEventFilter &) = delete;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  [[nodiscard]] static bool modal_or_popup_owns_input();
  [[nodiscard]] static bool viewport_surface_has_focus(QObject *watched);
  [[nodiscard]] static bool text_input_has_focus();
  [[nodiscard]] static bool is_text_input_widget(const QWidget *widget);
  void reset_pending_chord();

  QuaderEditorApi *editor_ = nullptr;
};

} // namespace quader
