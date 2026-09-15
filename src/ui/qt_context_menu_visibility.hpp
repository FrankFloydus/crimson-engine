////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QAction>
#include <QMenu>

namespace quader {

inline void hide_unavailable_context_menu_actions(QMenu &menu) {
  QAction *pending_separator = nullptr;
  bool has_content_before_separator = false;
  for (QAction *action : menu.actions()) {
    if (!action->isVisible()) {
      continue;
    }
    if (action->isSeparator()) {
      if (!has_content_before_separator || pending_separator != nullptr) {
        action->setVisible(false);
      } else {
        pending_separator = action;
        has_content_before_separator = false;
      }
      continue;
    }
    if (!action->isEnabled()) {
      action->setVisible(false);
      continue;
    }
    pending_separator = nullptr;
    has_content_before_separator = true;
  }
  if (pending_separator != nullptr) {
    pending_separator->setVisible(false);
  }
}

} // namespace quader
