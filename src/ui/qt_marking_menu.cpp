////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_marking_menu.hpp"

#include "api/quader_input_types.hpp"
#include "ui/qt_application_style.hpp"
#include "ui/qt_input_adapter.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QAction>
#include <QApplication>
#include <QEventLoop>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QScrollBar>
#include <QStyleOptionToolButton>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace quader {
namespace {

constexpr int kSlotCount = 8;

class MarkingMenuButton final : public QToolButton {
public:
  explicit MarkingMenuButton(QWidget *parent) : QToolButton(parent) {}

  void apply_ui_scale(const QtUiScaleMetrics &metrics) { metrics_ = metrics; }

protected:
  void paintEvent(QPaintEvent *event) override {
    QStyleOptionToolButton option;
    initStyleOption(&option);
    if (!qt_application_style_active(&option)) {
      QToolButton::paintEvent(event);
      return;
    }
    QPainter painter(this);
    (void)qt_draw_application_primitive(QStyle::PE_PanelButtonTool, &option,
                                        &painter, *style(), metrics_, this);
    style()->drawControl(QStyle::CE_ToolButtonLabel, &option, &painter, this);
  }

private:
  QtUiScaleMetrics metrics_;
};

class MarkingMenuList final : public QListWidget {
public:
  explicit MarkingMenuList(QWidget *parent) : QListWidget(parent) {
    setFrameShape(QFrame::NoFrame);
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
    menu_context_.setStyle(&menu_style_);
    setItemDelegate(new MenuItemDelegate(*this));
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    menu_style_.set_metrics(metrics);
    inset_ = metrics.px(4);
    setViewportMargins(inset_, inset_, inset_, inset_);
    menu_context_.setPalette(palette());
    menu_context_.setFont(font());
    menu_context_.setLayoutDirection(layoutDirection());
  }

  int content_inset() const { return inset_; }

  void synchronize_menu_actions(const std::vector<QPointer<QAction>> &actions) {
    menu_context_.clear();
    menu_actions_ = actions;
    for (const auto &action : menu_actions_) {
      if (action != nullptr) {
        menu_context_.addAction(action);
      }
    }
    menu_context_.ensurePolished();
    menu_context_.resize(menu_context_.sizeHint());
  }

  QSize native_menu_size() const { return menu_context_.sizeHint(); }

  QSize menu_row_size(int row) const {
    return menu_context_.actionGeometry(menu_actions_[row]).size();
  }

  int readable_row_width(int row, int row_height) const {
    const int text_width = fontMetrics().horizontalAdvance(item(row)->text());
    // Fusion paints its leading column from row height, while its size hint
    // can retain smaller platform gutters. Reserve full-height leading space
    // and the native trailing arrow gutter for scaled menu rows.
    const int trailing_gutter = menu_style_.pixelMetric(
        QStyle::PM_MenuButtonIndicator, nullptr, &menu_context_);
    return text_width + row_height + trailing_gutter + 2 * inset_;
  }

  void paint_surface(QPainter &painter, const QRect &bounds) {
    QStyleOption option;
    option.initFrom(&menu_context_);
    option.rect = bounds;
    menu_style_.drawPrimitive(QStyle::PE_PanelMenu, &option, &painter,
                              &menu_context_);
    if (!qt_application_style_active(&option)) {
      menu_style_.drawPrimitive(QStyle::PE_FrameMenu, &option, &painter,
                                &menu_context_);
    }
  }

private:
  class MenuPaintContext final : public QMenu {
  public:
    QStyleOptionMenuItem item_option(QAction *action) const {
      QStyleOptionMenuItem option;
      initStyleOption(&option, action);
      return option;
    }
  };

  class MenuItemDelegate final : public QStyledItemDelegate {
  public:
    explicit MenuItemDelegate(MarkingMenuList &view)
        : QStyledItemDelegate(&view), view_(view) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
      QStyleOptionViewItem view_option(option);
      initStyleOption(&view_option, index);
      if (index.row() < 0 ||
          static_cast<std::size_t>(index.row()) >= view_.menu_actions_.size() ||
          view_.menu_actions_[index.row()] == nullptr) {
        return;
      }
      QStyleOptionMenuItem item =
          view_.menu_context_.item_option(view_.menu_actions_[index.row()]);
      item.rect = view_option.rect;
      item.state = view_option.state;
      if (item.menuItemType == QStyleOptionMenuItem::Separator) {
        item.state.setFlag(QStyle::State_Enabled, view_.isEnabled());
        item.state.setFlag(QStyle::State_Selected, false);
      }
      item.direction = view_option.direction;
      item.palette = view_option.palette;
      item.menuRect = view_.rect();
      view_.menu_style_.drawControl(QStyle::CE_MenuItem, &item, painter,
                                    &view_.menu_context_);
    }

  private:
    MarkingMenuList &view_;
  };

  // This never-shown menu supplies the shared native menu painting contract;
  // the QListWidget remains the sole owner of scrolling and row interaction.
  QtUiScaleProxyStyle menu_style_;
  MenuPaintContext menu_context_;
  std::vector<QPointer<QAction>> menu_actions_;
  int inset_ = 4;
};

bool selectable_action(const QAction *action) {
  return action != nullptr && action->isVisible() && action->isEnabled() &&
         !action->isSeparator();
}

QString action_label(const QAction &action) {
  QString text = action.text();
  // QAction text may contain menu mnemonics; preserve escaped literal
  // ampersands.
  text.replace(QStringLiteral("&&"), QString(QChar(0x1f)));
  text.remove(QLatin1Char('&'));
  text.replace(QChar(0x1f), QLatin1Char('&'));
  return text;
}

QRect fitted_rect(const QRect &requested, const QRect &available) {
  const QSize size = requested.size().boundedTo(available.size());
  return QRect(QPoint(std::clamp(requested.left(), available.left(),
                                 available.right() - size.width() + 1),
                      std::clamp(requested.top(), available.top(),
                                 available.bottom() - size.height() + 1)),
               size);
}

} // namespace

struct QtMarkingMenu::Impl {
  explicit Impl(QtMarkingMenu &menu) : menu_(menu) {
    menu_.setObjectName(QStringLiteral("markingMenu"));
    menu_.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint |
                         Qt::NoDropShadowWindowHint);
    menu_.setAttribute(Qt::WA_TranslucentBackground);
    menu_.setAutoFillBackground(false);
    menu_.setFocusPolicy(Qt::StrongFocus);
    menu_.setMouseTracking(true);
    title_ = new QLabel(&menu_);
    title_->setObjectName(QStringLiteral("markingMenu.title"));
    title_->setAlignment(Qt::AlignCenter);
    title_->setWordWrap(true);
    title_->setAttribute(Qt::WA_TransparentForMouseEvents);
    title_->hide();
    for (int index = 0; index < kSlotCount; ++index) {
      auto *button = new MarkingMenuButton(&menu_);
      buttons_[index] = button;
      button->setObjectName(QStringLiteral("markingMenu.slot.%1").arg(index));
      button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
      button->setCheckable(true);
      button->setMouseTracking(true);
      QObject::connect(button, &QToolButton::clicked, &menu_, [this, index]() {
        set_active_slot(index);
        activate_selection();
      });
    }
    overflow_ = new MarkingMenuList(&menu_);
    overflow_->setObjectName(QStringLiteral("markingMenu.overflow"));
    overflow_->setSelectionMode(QAbstractItemView::SingleSelection);
    overflow_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    overflow_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    overflow_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    overflow_->setMouseTracking(true);
    overflow_->viewport()->setMouseTracking(true);
    overflow_->hide();
    QObject::connect(
        overflow_, &QListWidget::itemClicked, &menu_,
        [this](QListWidgetItem *) { finish(overflow_selection()); });
  }

  bool has_overflow() const { return !overflow_actions_.empty(); }

  bool slot_available(int index) const {
    return index >= 0 && index < kSlotCount &&
           selectable_action(actions_[index]);
  }

  void prepare(const QPoint &global_anchor,
               const QuaderInputChord &initiating_chord,
               const std::array<QAction *, 8> &slot_actions,
               const std::vector<QAction *> &overflow_actions,
               const QString &title,
               std::optional<std::uint64_t> initiating_key_identity) {
    global_anchor_ = global_anchor;
    radial_center_ = global_anchor;
    last_pointer_ = global_anchor;
    initiating_chord_ = initiating_chord;
    initiating_key_identity_ = initiating_key_identity;
    selected_action_ = nullptr;
    active_slot_ = -1;
    mouse_pressed_ = false;
    overflow_->hide();
    overflow_->clear();
    overflow_actions_.clear();
    for (QAction *action : overflow_actions) {
      if (action == nullptr || !action->isVisible()) {
        continue;
      }
      overflow_actions_.push_back(action);
      auto *item =
          new QListWidgetItem(action->icon(), action_label(*action), overflow_);
      item->setFlags(selectable_action(action)
                         ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                         : Qt::NoItemFlags);
      item->setSizeHint(QSize(
          0, action->isSeparator()
                 ? metrics_.px(8)
                 : std::max(metrics_.px(28),
                            menu_.fontMetrics().height() + metrics_.px(8))));
    }
    title_->setText(title);
    menu_.setAccessibleName(title);
    for (int index = 0; index < kSlotCount; ++index) {
      actions_[index] = slot_actions[index];
      auto *button = buttons_[index];
      QAction *action = slot_actions[index];
      const bool present =
          action != nullptr && action->isVisible() && !action->isSeparator();
      button->setVisible(present);
      button->setEnabled(selectable_action(action));
      button->setChecked(false);
      button->setIcon(action == nullptr ? QIcon{} : action->icon());
      button->setIconSize(metrics_.size(QSize(20, 20)));
      button->setAccessibleName(action != nullptr ? action_label(*action)
                                                  : QString{});
      button->apply_ui_scale(metrics_);
    }
    overflow_->setIconSize(metrics_.size(QSize(20, 20)));
    overflow_->apply_ui_scale(metrics_);
    overflow_->synchronize_menu_actions(overflow_actions_);
    overflow_->setCurrentRow(-1);
    layout_radial();
  }

  void layout_radial() {
    QScreen *screen = QGuiApplication::screenAt(global_anchor_);
    if (screen == nullptr) {
      screen = menu_.screen();
    }
    available_geometry_ = screen->availableGeometry();
    const int button_height = std::max(
        metrics_.px(28), menu_.fontMetrics().height() + metrics_.px(8));
    const int gap = metrics_.px(6);
    std::array<int, kSlotCount> widths{};
    for (int index = 0; index < kSlotCount; ++index) {
      const auto *button = buttons_[index];
      const int padding = metrics_.px(button->icon().isNull() ? 20 : 44);
      widths[index] = std::min(std::clamp(menu_.fontMetrics().horizontalAdvance(
                                              button->accessibleName()) +
                                              padding,
                                          metrics_.px(48), metrics_.px(220)),
                               available_geometry_.width());
      const int text_width = std::max(1, widths[index] - padding);
      buttons_[index]->setText(menu_.fontMetrics().elidedText(
          buttons_[index]->accessibleName(), Qt::ElideRight, text_width));
    }
    const auto place_circle = [&](int radius) {
      for (int index = 0; index < kSlotCount; ++index) {
        const double angle = index * std::numbers::pi / 4.0;
        const QPoint center =
            radial_center_ + QPoint(qRound(std::sin(angle) * radius),
                                    -qRound(std::cos(angle) * radius));
        button_geometry_[index] = QRect(
            center - QPoint((widths[index] - 1) / 2, (button_height - 1) / 2),
            QSize(widths[index], button_height));
      }
    };
    const auto has_overlap = [&]() {
      for (int first = 0; first < kSlotCount; ++first) {
        if (buttons_[first]->isHidden()) {
          continue;
        }
        for (int second = first + 1; second < kSlotCount; ++second) {
          if (!buttons_[second]->isHidden() &&
              button_geometry_[first]
                  .adjusted(-gap, -gap, gap, gap)
                  .intersects(button_geometry_[second])) {
            return true;
          }
        }
      }
      return false;
    };
    int radius = metrics_.px(90);
    place_circle(radius);
    while (has_overlap() && radius < metrics_.px(480)) {
      radius += std::max(1, metrics_.px(1));
      place_circle(radius);
    }
    radial_geometry_ = QRect(radial_center_ - QPoint(6, 6), QSize(13, 13));
    for (int index = 0; index < kSlotCount; ++index) {
      if (buttons_[index]->isHidden()) {
        continue;
      }
      radial_geometry_ = radial_geometry_.united(button_geometry_[index]);
    }
    dead_zone_ = metrics_.px(20);
    radial_geometry_ = radial_geometry_.adjusted(-2, -2, 2, 2);
    layout_overflow();
    const QRect content_geometry =
        has_overflow() ? radial_geometry_.united(overflow_geometry_)
                       : radial_geometry_;
    const QPoint translation =
        fitted_rect(content_geometry, available_geometry_).topLeft() -
        content_geometry.topLeft();
    radial_center_ += translation;
    radial_geometry_.translate(translation);
    for (QRect &geometry : button_geometry_) {
      geometry.translate(translation);
    }
    if (has_overflow()) {
      overflow_geometry_.translate(translation);
    }
    title_geometry_ = QRect(radial_center_, QSize(1, 1));
    apply_geometry();
  }

  void apply_geometry() {
    // Moving a visible native popup while processing its mouse event can feed
    // another position change back through Qt. Keep one transparent host for
    // the gesture; only its guide and individual controls repaint afterward.
    const QRect geometry = available_geometry_;
    menu_.setGeometry(geometry);
    for (int index = 0; index < kSlotCount; ++index) {
      buttons_[index]->setGeometry(
          button_geometry_[index].translated(-geometry.topLeft()));
    }
    title_->setGeometry(title_geometry_.translated(-geometry.topLeft()));
    if (!overflow_->isHidden()) {
      overflow_->setGeometry(
          overflow_geometry_.translated(-geometry.topLeft()));
      overflow_->raise();
    }
    menu_.update();
  }

  void layout_overflow() {
    if (!has_overflow()) {
      overflow_->hide();
      return;
    }
    const int gap = metrics_.px(8);
    int row_height = std::max(metrics_.px(28),
                              menu_.fontMetrics().height() + metrics_.px(8));
    int width =
        std::max(metrics_.px(180), overflow_->native_menu_size().width());
    for (int row = 0; row < overflow_->count(); ++row) {
      const QSize native_size = overflow_->menu_row_size(row);
      width =
          std::max(width, native_size.width() + 2 * overflow_->content_inset());
      if (!overflow_actions_[row]->isSeparator()) {
        row_height = std::max(row_height, native_size.height());
      }
    }
    int natural_height = 2 * overflow_->content_inset();
    for (int row = 0; row < overflow_->count(); ++row) {
      const bool separator = overflow_actions_[row]->isSeparator();
      const int height = separator ? metrics_.px(8) : row_height;
      overflow_->item(row)->setSizeHint(QSize(0, height));
      natural_height += height;
      if (!separator) {
        width = std::max(width, overflow_->readable_row_width(row, row_height));
      }
    }
    width = std::min(width, available_geometry_.width());
    int height = natural_height;
    QPoint origin(radial_center_.x() - (width - 1) / 2,
                  radial_geometry_.bottom() + gap);
    if (natural_height <= available_geometry_.height() &&
        radial_geometry_.height() + gap + natural_height >
            available_geometry_.height()) {
      // Preserve full-size rows beside the ring only when the monitor itself
      // cannot contain the complete vertical arrangement.
      origin = QPoint(radial_geometry_.right() + gap, radial_geometry_.top());
    } else if (natural_height > available_geometry_.height()) {
      // Authored rows taller than the monitor remain keyboard reachable;
      // only this physically oversized case needs an internally clipped view.
      height = std::max(row_height + 2 * overflow_->content_inset(),
                        available_geometry_.height() -
                            radial_geometry_.height() - gap);
    }
    overflow_geometry_ = QRect(origin, QSize(width, height));
    overflow_->setCurrentRow(-1);
    overflow_->show();
  }

  void set_active_slot(int index) {
    if (index >= 0 && overflow_->currentRow() >= 0) {
      overflow_->setCurrentRow(-1);
    }
    if (active_slot_ != index) {
      active_slot_ = index;
      for (int slot = 0; slot < kSlotCount; ++slot) {
        buttons_[slot]->setChecked(slot == index && slot_available(slot));
      }
    }
  }

  void update_pointer(const QPoint &global_position,
                      bool explicit_target = false) {
    const QPoint previous_pointer = last_pointer_;
    last_pointer_ = global_position;
    if (previous_pointer != last_pointer_) {
      const int padding = metrics_.px(6);
      const QRect dirty =
          QRect(radial_center_, previous_pointer)
              .normalized()
              .united(QRect(radial_center_, last_pointer_).normalized())
              .adjusted(-padding, -padding, padding, padding)
              .translated(-menu_.pos());
      menu_.update(dirty.intersected(menu_.rect()));
    }
    // Fitting the initial layout may place a row beneath the opening cursor.
    // A stationary release still cancels; an explicit click may select it.
    if (!explicit_target && global_position == global_anchor_) {
      overflow_->setCurrentRow(-1);
      set_active_slot(-1);
      return;
    }
    if (!overflow_->isHidden() &&
        overflow_geometry_.contains(global_position)) {
      set_active_slot(-1);
      const QModelIndex index = overflow_->indexAt(
          overflow_->viewport()->mapFromGlobal(global_position));
      overflow_->setCurrentRow(
          index.isValid() && (index.flags() & Qt::ItemIsEnabled) ? index.row()
                                                                 : -1);
      return;
    }
    overflow_->setCurrentRow(-1);
    const QPoint delta = global_position - radial_center_;
    if (QPoint::dotProduct(delta, delta) <= dead_zone_ * dead_zone_) {
      set_active_slot(-1);
      return;
    }
    for (int index = 0; index < kSlotCount; ++index) {
      if (!buttons_[index]->isHidden() &&
          button_geometry_[index].contains(global_position)) {
        set_active_slot(index);
        return;
      }
    }
    const double angle = std::atan2(static_cast<double>(delta.x()),
                                    -static_cast<double>(delta.y()));
    const int index =
        (qRound(angle * 4.0 / std::numbers::pi) + kSlotCount) % kSlotCount;
    set_active_slot(index);
  }

  QAction *overflow_selection() const {
    const int row = overflow_->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= overflow_actions_.size()) {
      return nullptr;
    }
    QAction *action = overflow_actions_[row];
    return selectable_action(action) ? action : nullptr;
  }

  QAction *selection() const {
    if (overflow_->currentRow() >= 0) {
      return overflow_selection();
    }
    return slot_available(active_slot_) ? actions_[active_slot_].data()
                                        : nullptr;
  }

  void activate_selection() { finish(selection()); }

  void step_selection(int direction, bool prefer_overflow) {
    const int count_radial = kSlotCount;
    const int total = count_radial + overflow_->count();
    int index = overflow_->currentRow() >= 0
                    ? count_radial + overflow_->currentRow()
                    : active_slot_;
    const bool from_center_to_list =
        index < 0 && prefer_overflow && has_overflow();
    if (from_center_to_list) {
      for (int offset = 0; offset < overflow_->count(); ++offset) {
        const int row =
            direction > 0 ? offset : overflow_->count() - 1 - offset;
        if (selectable_action(overflow_actions_[row])) {
          set_active_slot(-1);
          overflow_->setCurrentRow(row);
          overflow_->scrollToItem(overflow_->item(row));
          return;
        }
      }
      return;
    }
    if (index < 0) {
      index = direction > 0 ? total - 1 : 0;
    }
    for (int count = 0; count < total; ++count) {
      index = (index + direction + total) % total;
      if (index < count_radial && slot_available(index)) {
        overflow_->setCurrentRow(-1);
        set_active_slot(index);
        buttons_[index]->setFocus(Qt::OtherFocusReason);
        return;
      } else if (index >= count_radial &&
                 selectable_action(overflow_actions_[index - count_radial])) {
        set_active_slot(-1);
        overflow_->setCurrentRow(index - count_radial);
        overflow_->scrollToItem(overflow_->item(index - count_radial));
        return;
      }
    }
  }

  void finish(QAction *action) {
    if (!running_) {
      return;
    }
    selected_action_ = selectable_action(action) ? action : nullptr;
    running_ = false;
    menu_.hide();
    if (loop_ != nullptr) {
      loop_->quit();
    }
  }

private:
  friend class QtMarkingMenu;

  QtMarkingMenu &menu_;
  QtUiScaleMetrics metrics_;
  QuaderInputChord initiating_chord_;
  std::optional<std::uint64_t> initiating_key_identity_;
  std::array<QPointer<QAction>, kSlotCount> actions_{};
  std::array<MarkingMenuButton *, kSlotCount> buttons_{};
  std::array<QRect, kSlotCount> button_geometry_{};
  std::vector<QPointer<QAction>> overflow_actions_;
  QLabel *title_ = nullptr;
  MarkingMenuList *overflow_ = nullptr;
  QEventLoop *loop_ = nullptr;
  QPointer<QAction> selected_action_;
  QPoint global_anchor_;
  QPoint radial_center_;
  QPoint last_pointer_;
  QRect available_geometry_;
  QRect radial_geometry_;
  QRect overflow_geometry_;
  QRect title_geometry_;
  int active_slot_ = -1;
  int dead_zone_ = 24;
  bool running_ = false;
  bool mouse_pressed_ = false;
};

QtMarkingMenu::QtMarkingMenu(QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this)) {}

QtMarkingMenu::~QtMarkingMenu() = default;

void QtMarkingMenu::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  impl_->metrics_ = metrics;
}

QAction *QtMarkingMenu::exec(const QPoint &global_anchor,
                             const QuaderInputChord &initiating_chord,
                             const std::array<QAction *, 8> &slot_actions,
                             const std::vector<QAction *> &overflow_actions,
                             const QString &title,
                             std::optional<std::uint64_t> initiating_key_identity) {
  if (impl_->running_) {
    return nullptr;
  }
  impl_->prepare(global_anchor, initiating_chord, slot_actions,
                 overflow_actions, title, initiating_key_identity);
  QEventLoop loop;
  impl_->loop_ = &loop;
  impl_->running_ = true;
  qApp->installEventFilter(this);
  show();
  raise();
  activateWindow();
  setFocus(Qt::PopupFocusReason);
  if (impl_->running_) {
    loop.exec(QEventLoop::DialogExec);
  }
  qApp->removeEventFilter(this);
  impl_->loop_ = nullptr;
  impl_->running_ = false;
  return impl_->selected_action_;
}

bool QtMarkingMenu::eventFilter(QObject *watched, QEvent *event) {
  if (!impl_->running_) {
    return QWidget::eventFilter(watched, event);
  }
  if (event->type() == QEvent::ShortcutOverride) {
    event->accept();
    return true;
  }
  if (event->type() == QEvent::KeyPress ||
      event->type() == QEvent::KeyRelease) {
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->isAutoRepeat()) {
      return true;
    }
    if (event->type() == QEvent::KeyRelease) {
      if (impl_->initiating_chord_.device == QuaderInputDevice::Keyboard) {
        const auto chord = quader_input_chord_from_qt_event(*key);
        const bool initiating_key_released =
            impl_->initiating_key_identity_.has_value()
                ? quader_keyboard_event_key_identity(*key) ==
                      *impl_->initiating_key_identity_
                : !chord.empty() && chord.key == impl_->initiating_chord_.key;
        if (initiating_key_released) {
          impl_->finish(impl_->selection());
        }
      }
      return true;
    }
    switch (key->key()) {
    case Qt::Key_Escape:
      impl_->finish(nullptr);
      return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
      impl_->activate_selection();
      return true;
    case Qt::Key_Right:
      impl_->step_selection(1, false);
      return true;
    case Qt::Key_Down:
      impl_->step_selection(1, true);
      return true;
    case Qt::Key_Left:
      impl_->step_selection(-1, false);
      return true;
    case Qt::Key_Up:
      impl_->step_selection(-1, true);
      return true;
    default:
      return true;
    }
  }
  if (event->type() == QEvent::MouseMove ||
      event->type() == QEvent::MouseButtonPress ||
      event->type() == QEvent::MouseButtonRelease) {
    auto *mouse = static_cast<QMouseEvent *>(event);
    const QPoint global_position = mouse->globalPosition().toPoint();
    if (event->type() == QEvent::MouseButtonRelease &&
        impl_->initiating_chord_.device == QuaderInputDevice::Mouse &&
        quader_mouse_button_from_qt_button(mouse->button()) ==
            impl_->initiating_chord_.mouse_button) {
      impl_->update_pointer(global_position);
      impl_->finish(impl_->selection());
      return true;
    }
    if (qobject_cast<QScrollBar *>(watched) != nullptr) {
      impl_->update_pointer(global_position);
      return QWidget::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseButtonPress) {
      const bool inside_targets =
          impl_->radial_geometry_.contains(global_position) ||
          (!impl_->overflow_->isHidden() &&
           impl_->overflow_geometry_.contains(global_position));
      if (!inside_targets || mouse->button() != Qt::LeftButton) {
        impl_->finish(nullptr);
      } else {
        impl_->mouse_pressed_ = true;
        impl_->update_pointer(global_position, true);
      }
      return true;
    }
    impl_->update_pointer(global_position, impl_->mouse_pressed_);
    if (event->type() == QEvent::MouseButtonRelease &&
        mouse->button() == Qt::LeftButton && impl_->mouse_pressed_) {
      impl_->mouse_pressed_ = false;
      impl_->activate_selection();
      return true;
    }
  }
  if (event->type() == QEvent::ApplicationDeactivate ||
      (event->type() == QEvent::WindowDeactivate && watched == this)) {
    impl_->finish(nullptr);
    return false;
  }
  if (event->type() == QEvent::FocusOut &&
      (watched == this || isAncestorOf(qobject_cast<QWidget *>(watched)))) {
    QTimer::singleShot(0, this, [this]() {
      QWidget *focused = QApplication::focusWidget();
      if (impl_->running_ && focused != this && !isAncestorOf(focused)) {
        impl_->finish(nullptr);
      }
    });
  }
  return QWidget::eventFilter(watched, event);
}

void QtMarkingMenu::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.fillRect(rect(), Qt::transparent);
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  painter.setRenderHint(QPainter::Antialiasing);
  const auto group =
      isEnabled() ? (isActiveWindow() ? QPalette::Active : QPalette::Inactive)
                  : QPalette::Disabled;
  QPen pen(palette().color(group, QPalette::Text));
  pen.setCosmetic(true);
  pen.setWidthF(1.0);
  pen.setCapStyle(Qt::RoundCap);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  const QPointF anchor = mapFromGlobal(impl_->radial_center_);
  const QPointF pointer = mapFromGlobal(impl_->last_pointer_);
  if (pointer != anchor) {
    painter.drawLine(anchor, pointer);
  }
  const qreal radius = impl_->metrics_.px(4.0);
  painter.drawEllipse(anchor, radius, radius);
  if (!impl_->overflow_->isHidden()) {
    impl_->overflow_->paint_surface(painter, impl_->overflow_->geometry());
  }
}

void QtMarkingMenu::hideEvent(QHideEvent *event) {
  impl_->finish(nullptr);
  QWidget::hideEvent(event);
}

} // namespace quader
