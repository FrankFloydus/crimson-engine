////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_face_texture_panel.hpp"

#include "ui/qt_face_texture_canvas.hpp"
#include "ui/qt_face_texture_controls.hpp"
#include "ui/qt_material_browser_window.hpp"

#include <QApplication>
#include <QDataStream>
#include <QDockWidget>
#include <QEvent>
#include <QIODevice>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <functional>
#include <utility>

namespace quader {
namespace {

constexpr quint32 kPanelSplitterStateTag = 0x51465450;
constexpr quint16 kPanelSplitterStateVersion = 1;

/// Reserves the compact controls' natural height, with scrolling when the
/// dock or UI scale cannot accommodate their full presentation.
class QtFaceTextureControlsScrollArea final : public QScrollArea {
public:
  using QScrollArea::QScrollArea;
  std::function<void()> natural_height_changed;

  [[nodiscard]] QSize sizeHint() const override {
    if (const QWidget *content = widget()) {
      QSize hint = content->sizeHint().expandedTo(content->minimumSizeHint());
      if (content->hasHeightForWidth()) {
        hint.setHeight(std::max(hint.height(),
            content->heightForWidth(std::max(1, viewport()->width()))));
      }
      return hint + QSize(frameWidth() * 2, frameWidth() * 2);
    }
    return QScrollArea::sizeHint();
  }

  [[nodiscard]] QSize minimumSizeHint() const override {
    return {0, minimumHeight()};
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    const bool handled = QScrollArea::eventFilter(watched, event);
    if (watched == widget() &&
        (event->type() == QEvent::LayoutRequest ||
         event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
      const QSize hint = sizeHint();
      if (natural_hint_ != hint) {
        natural_hint_ = hint;
        // The viewport otherwise isolates child layout changes from the dock:
        // selecting faces would retain the hidden controls' tiny initial slot.
        updateGeometry();
        if (natural_height_changed) {
          natural_height_changed();
        }
      }
    }
    return handled;
  }

private:
  QSize natural_hint_;
};

} // namespace

QtFaceTexturePanel::QtFaceTexturePanel(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("face_texture.panel"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  splitter_ = new QSplitter(Qt::Vertical, this);
  splitter_->setObjectName(QStringLiteral("face_texture.splitter"));
  splitter_->setChildrenCollapsible(false);
  layout->addWidget(splitter_);

  upper_ = new QSplitter(Qt::Vertical, splitter_);
  upper_->setObjectName(QStringLiteral("face_texture.canvas_splitter"));
  upper_->setChildrenCollapsible(false);
  upper_->installEventFilter(this);
  canvas_ = new QtFaceTextureCanvas(upper_);
  upper_->addWidget(canvas_);
  auto *controls_scroll = new QtFaceTextureControlsScrollArea(upper_);
  controls_scroll_ = controls_scroll;
  controls_scroll_->setObjectName(QStringLiteral("face_texture.controls_scroll"));
  controls_scroll_->setWidgetResizable(true);
  controls_scroll_->setFrameShape(QFrame::NoFrame);
  controls_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  controls_scroll_->setMinimumHeight(80);
  controls_scroll_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  controls_ = new QtFaceTextureControls(controls_scroll_);
  controls_scroll_->setWidget(controls_);
  upper_->addWidget(controls_scroll_);
  upper_->setStretchFactor(0, 1);
  upper_->setStretchFactor(1, 0);
  controls_scroll->natural_height_changed = [this]() {
    schedule_controls_height_fit();
  };
  connect(upper_, &QSplitter::splitterMoved, this,
          [this](int, int) { canvas_splitter_adjusted_ = true; });
  splitter_->addWidget(upper_);

  browser_ = new QtMaterialBrowserWindow(splitter_,
                                         QtMaterialBrowserHost::Embedded);
  browser_->setMinimumHeight(96);
  browser_->set_assignment_available(false);
  splitter_->addWidget(browser_);
  // Changing a QMainWindow into embedded content hides it; the dock owns
  // visibility and the integrated browser remains present for any selection.
  browser_->show();
  splitter_->setStretchFactor(0, 3);
  splitter_->setStretchFactor(1, 2);
  splitter_->setSizes({520, 280});
  setFocusProxy(canvas_);
}

void QtFaceTexturePanel::schedule_controls_height_fit() {
  if (controls_height_fit_scheduled_ || canvas_splitter_adjusted_) {
    return;
  }
  controls_height_fit_scheduled_ = true;
  // A scroll area's content resize still uses its captured viewport extent
  // when it updates scroll ranges. Refit after that event has completed.
  QTimer::singleShot(0, this, [this]() {
    controls_height_fit_scheduled_ = false;
    fit_controls_height();
  });
}

void QtFaceTexturePanel::fit_controls_height() {
  if (controls_scroll_ == nullptr || canvas_splitter_adjusted_ ||
      canvas_->interaction_active() ||
      canvas_->presentation().gesture_active) {
    return;
  }
  const int controls_height = std::max(controls_scroll_->minimumHeight(),
                                        controls_scroll_->sizeHint().height());
  const int canvas_height = std::max(
      canvas_->minimumHeight(),
      upper_->height() - upper_->handleWidth() - controls_height);
  upper_->setSizes({canvas_height, controls_height});
}

bool QtFaceTexturePanel::eventFilter(QObject *watched, QEvent *event) {
  const bool handled = QWidget::eventFilter(watched, event);
  if (watched == upper_ && event->type() == QEvent::Resize) {
    schedule_controls_height_fit();
  }
  return handled;
}

QtFaceTexturePanel::~QtFaceTexturePanel() { abandon(); }

void QtFaceTexturePanel::bind(QuaderEditorApi &editor) {
  abandon();
  controls_->bind(editor);
  canvas_->bind(editor);
  refresh();
}

void QtFaceTexturePanel::unbind() {
  canvas_->unbind();
  controls_->unbind();
  browser_->set_assignment_available(false);
}

void QtFaceTexturePanel::abandon() {
  canvas_->abandon();
  controls_->abandon_runtime_binding();
  browser_->set_assignment_available(false);
}

void QtFaceTexturePanel::refresh() {
  canvas_->refresh();
  controls_->refresh();
  browser_->set_assignment_available(
      canvas_->presentation().selected_face_count > 0 &&
      !canvas_->presentation().gesture_active);
}

void QtFaceTexturePanel::set_localization(
    const LocalizationService &localization) {
  canvas_->set_localization(localization);
  controls_->set_localization(localization);
}

void QtFaceTexturePanel::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  if (metrics_.revision() == metrics.revision() &&
      metrics_.factor() == metrics.factor()) {
    return;
  }
  metrics_ = metrics;
  canvas_->apply_ui_scale(metrics);
  controls_->apply_ui_scale(metrics);
  browser_->apply_ui_scale(metrics);
  controls_scroll_->setMinimumHeight(metrics.px(80));
  browser_->setMinimumHeight(metrics.px(96));
  splitter_->setHandleWidth(metrics.px(5));
  upper_->setHandleWidth(metrics.px(5));
  schedule_controls_height_fit();
  updateGeometry();
}

void QtFaceTexturePanel::set_material_presentation(
    std::optional<QtMaterialPresentationItem> material) {
  canvas_->set_material_presentation(std::move(material));
}

QtMaterialBrowserWindow &QtFaceTexturePanel::browser() const {
  return *browser_;
}

QtFaceTextureCanvas &QtFaceTexturePanel::canvas() const { return *canvas_; }

QuaderMaterialReference
QtFaceTexturePanel::canvas_material_reference() const {
  return canvas_->material_reference();
}

bool QtFaceTexturePanel::numeric_interaction_active() const {
  return controls_->numeric_interaction_active();
}

bool QtFaceTexturePanel::interaction_active() const {
  return canvas_->interaction_active();
}

QByteArray QtFaceTexturePanel::save_splitter_state() const {
  QByteArray state;
  QDataStream stream(&state, QIODevice::WriteOnly);
  stream << kPanelSplitterStateTag << kPanelSplitterStateVersion
         << splitter_->saveState() << upper_->saveState()
         << static_cast<quint8>(canvas_splitter_adjusted_ ? 1 : 0);
  return state;
}

bool QtFaceTexturePanel::restore_splitter_state(const QByteArray &state) {
  if (state.isEmpty()) {
    return false;
  }
  QDataStream stream(state);
  quint32 tag = 0;
  stream >> tag;
  if (tag != kPanelSplitterStateTag) {
    // Older preferences contain only the canvas/controls versus browser split.
    return splitter_->restoreState(state);
  }
  quint16 version = 0;
  QByteArray browser_state;
  QByteArray canvas_state;
  quint8 canvas_adjusted = 0;
  stream >> version >> browser_state >> canvas_state >> canvas_adjusted;
  if (stream.status() != QDataStream::Ok || !stream.atEnd() ||
      version != kPanelSplitterStateVersion || browser_state.isEmpty() ||
      canvas_state.isEmpty() || canvas_adjusted > 1) {
    return false;
  }
  const QByteArray previous_browser = splitter_->saveState();
  const QByteArray previous_canvas = upper_->saveState();
  const bool previous_adjusted = canvas_splitter_adjusted_;
  canvas_splitter_adjusted_ = true;
  if (!splitter_->restoreState(browser_state) ||
      !upper_->restoreState(canvas_state)) {
    splitter_->restoreState(previous_browser);
    upper_->restoreState(previous_canvas);
    canvas_splitter_adjusted_ = previous_adjusted;
    return false;
  }
  canvas_splitter_adjusted_ = canvas_adjusted != 0;
  schedule_controls_height_fit();
  return true;
}

QSize QtFaceTexturePanel::sizeHint() const {
  return metrics_.size(QSize(320, 800));
}

bool qt_object_belongs_to_face_texture_panel(const QObject *target) {
  for (const QObject *current = target; current != nullptr;
       current = current->parent()) {
    if (dynamic_cast<const QtFaceTexturePanel *>(current) != nullptr) {
      return true;
    }
  }
  const auto *target_window = qobject_cast<const QWindow *>(target);
  if (target_window == nullptr ||
      qobject_cast<QApplication *>(QCoreApplication::instance()) == nullptr) {
    return false;
  }
  for (const QWidget *top_level : QApplication::topLevelWidgets()) {
    if (top_level->windowHandle() != target_window) {
      continue;
    }
    if (dynamic_cast<const QtFaceTexturePanel *>(top_level) != nullptr) {
      return true;
    }
    const auto *dock = qobject_cast<const QDockWidget *>(top_level);
    return dock != nullptr &&
           dynamic_cast<const QtFaceTexturePanel *>(dock->widget()) != nullptr;
  }
  return false;
}

bool qt_object_belongs_to_face_texture_panel(
    const QObject *target, const QPoint &global_position) {
  const auto *target_window = qobject_cast<const QWindow *>(target);
  if (target_window == nullptr ||
      qobject_cast<QApplication *>(QCoreApplication::instance()) == nullptr) {
    return false;
  }
  for (const QWidget *top_level : QApplication::topLevelWidgets()) {
    if (top_level->windowHandle() != target_window) {
      continue;
    }
    const QPoint local = top_level->mapFromGlobal(global_position);
    if (!top_level->rect().contains(local)) {
      return false;
    }
    // A parent's visibleRegion excludes opaque child controls. Resolve the
    // actual visible child so numeric editors and scroll viewports retain the
    // panel's input ownership without claiming an obscured or inactive tab.
    const QWidget *hit = top_level->childAt(local);
    return qt_object_belongs_to_face_texture_panel(hit != nullptr ? hit
                                                                 : top_level);
  }
  return false;
}

} // namespace quader
