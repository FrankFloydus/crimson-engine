////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <gtest/gtest.h>

#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_viewport_surface.hpp"

#include <QApplication>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QRect>
#include <QResizeEvent>
#include <QSize>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace quader::test_support {

/// Sizes the visible client area used by hidden panel interaction fixtures.
inline void resize_viewport_surface(quader::QtViewportPanel &panel,
                                    quader::QtViewportSurface &surface, QSize size) {
  auto *host = panel.findChild<QWidget *>(
      QStringLiteral("viewportSurfaceContainer_%1").arg(
          QString::fromUtf8(quader::viewport_pane_stable_id(surface.pane_id()).data())));
  ASSERT_NE(host, nullptr);
  const auto *pane = host->parentWidget();
  ASSERT_NE(pane, nullptr);
  // Hidden QWidget resize events are deferred until exposure. Send the real
  // panel size through its Qt owner so later overlay refreshes retain it.
  for (int attempt = 0; attempt < 8 && host->size() != size; ++attempt) {
    const QSize previous_size = panel.size();
    const int width_delta = qRound(
        static_cast<double>(size.width() - host->width()) * panel.width() /
        std::max(1, pane->width()));
    const int height_delta = qRound(
        static_cast<double>(size.height() - host->height()) * panel.height() /
        std::max(1, pane->height()));
    panel.resize(previous_size + QSize{width_delta, height_delta});
    if (host->size() != size) {
      QResizeEvent resized(panel.size(), previous_size);
      QApplication::sendEvent(&panel, &resized);
    }
  }
  ASSERT_EQ(host->size(), size);
  surface.synchronize_render_size();
}

/// Adapts visible client positions to the raw coordinates delivered by QWindow.
inline bool send_visible_viewport_event(quader::QtViewportSurface *surface,
                                        QEvent *event) {
  const auto map_position = [surface](QPointF visible) {
    const QPoint integral = visible.toPoint();
    const QPointF fraction = visible - QPointF{integral};
    return std::pair{
        QPointF{surface->mapFromGlobal(surface->visible_local_to_global(integral))} +
            fraction,
        QPointF{surface->visible_local_to_global(integral)} + fraction};
  };
  switch (event->type()) {
  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonRelease:
  case QEvent::MouseButtonDblClick:
  case QEvent::MouseMove: {
    const auto &mouse = *static_cast<QMouseEvent *>(event);
    const auto [local, global] = map_position(mouse.position());
    QMouseEvent mapped(mouse.type(), local, global, mouse.button(),
                       mouse.buttons(), mouse.modifiers());
    const bool delivered = QApplication::sendEvent(surface, &mapped);
    event->setAccepted(mapped.isAccepted());
    return delivered;
  }
  case QEvent::Wheel: {
    const auto &wheel = *static_cast<QWheelEvent *>(event);
    const auto [local, global] = map_position(wheel.position());
    QWheelEvent mapped(local, global, wheel.pixelDelta(), wheel.angleDelta(),
                       wheel.buttons(), wheel.modifiers(), wheel.phase(),
                       wheel.inverted());
    const bool delivered = QApplication::sendEvent(surface, &mapped);
    event->setAccepted(mapped.isAccepted());
    return delivered;
  }
  case QEvent::Enter: {
    const auto &enter = *static_cast<QEnterEvent *>(event);
    const auto [local, global] = map_position(enter.position());
    QEnterEvent mapped(local, local, global);
    return QApplication::sendEvent(surface, &mapped);
  }
  default:
    return QApplication::sendEvent(surface, event);
  }
}

} // namespace quader::test_support
