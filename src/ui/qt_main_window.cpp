////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ui/qt_main_window.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <qpa/qwindowsysteminterface_p.h>

#if defined(Q_OS_WIN)
#include <Windows.h>
#endif

namespace quader {
namespace {

static_assert(QT_VERSION == QT_VERSION_CHECK(6, 10, 3),
              "Review Qt's posted repaint and native key queue ordering when "
              "updating Qt.");

/// Observes pending input without changing Qt delivery or Windows key state.
bool keyboard_input_pending() {
  if (QWindowSystemInterfacePrivate::peekWindowSystemEvent(
          QWindowSystemInterfacePrivate::Key) != nullptr) {
    return true;
  }
#if defined(Q_OS_WIN)
  return HIWORD(GetQueueStatus(QS_KEY)) != 0;
#else
  return false;
#endif
}

} // namespace

/// Gives already queued keys a bounded opportunity to run before root painting.
bool QtMainWindow::event(QEvent *event) {
  if (event->type() == QEvent::UpdateRequest) {
    // Qt processes posted repaints before native keys, then before queued QPA
    // keys on the next pass. Bound both deferrals so excluded input cannot
    // starve painting. Repost explicitly: update() retains updateRequestSent.
    constexpr unsigned int kMaximumInputDeferrals = 2u;
    if (deferred_input_update_requests_ < kMaximumInputDeferrals &&
        keyboard_input_pending()) {
      ++deferred_input_update_requests_;
      QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest),
                                  Qt::LowEventPriority);
      return true;
    }
    deferred_input_update_requests_ = 0u;
  }
  return QMainWindow::event(event);
}

} // namespace quader
