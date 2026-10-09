////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QComboBox>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QScreen>

namespace quader {

/**
 * Keeps Fusion's native combo popup while preferring a conventional dropdown
 * position below the field whenever the complete popup fits on-screen.
 */
class QtDownwardPopupComboBox final : public QComboBox {
public:
  using QComboBox::QComboBox;

  void showPopup() override {
    QComboBox::showPopup();

    QAbstractItemView *item_view = view();
    if (item_view != nullptr && currentIndex() >= 0) {
      const QModelIndex committed_index = model()->index(
          currentIndex(), modelColumn(), rootModelIndex());
      if (committed_index.isValid()) {
        item_view->setCurrentIndex(committed_index);
        if (QItemSelectionModel *selection = item_view->selectionModel()) {
          selection->select(
              committed_index,
              QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        item_view->scrollTo(committed_index,
                            QAbstractItemView::PositionAtCenter);
      }
    }

    QWidget *popup = item_view != nullptr ? item_view->window() : nullptr;
    if (popup == nullptr || popup == window() || !popup->isVisible() ||
        popup->windowType() != Qt::Popup) {
      return;
    }

    const QPoint below = mapToGlobal(QPoint(0, height()));
    QScreen *popup_screen = QGuiApplication::screenAt(below);
    if (popup_screen == nullptr) {
      popup_screen = screen();
    }
    if (popup_screen == nullptr) {
      return;
    }

    QRect popup_geometry = popup->geometry();
    const QRect available = popup_screen->availableGeometry();
    const int proposed_bottom = below.y() + popup_geometry.height() - 1;
    if (below.y() < available.top() || proposed_bottom > available.bottom()) {
      return;
    }

    popup_geometry.moveTop(below.y());
    popup->setGeometry(popup_geometry);
  }
};

} // namespace quader
