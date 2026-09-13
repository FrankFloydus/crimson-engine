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
#include <QDockWidget>
#include <QEvent>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWindow>

#include <utility>

namespace quader {
namespace {

/// Reserves the compact controls' natural height, with scrolling when the
/// dock or UI scale cannot accommodate their full presentation.
class QtFaceTextureControlsScrollArea final : public QScrollArea {
public:
  using QScrollArea::QScrollArea;

  [[nodiscard]] QSize sizeHint() const override {
    if (const QWidget *content = widget()) {
      return content->sizeHint() + QSize(frameWidth() * 2, frameWidth() * 2);
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
      const QSize hint = widget()->sizeHint();
      if (natural_hint_ != hint) {
        natural_hint_ = hint;
        // The viewport otherwise isolates child layout changes from the dock:
        // selecting faces would retain the hidden controls' tiny initial slot.
        updateGeometry();
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

  upper_ = new QWidget(splitter_);
  upper_->setObjectName(QStringLiteral("face_texture.upper"));
  auto *upper_layout = new QVBoxLayout(upper_);
  upper_layout->setContentsMargins(0, 0, 0, 0);
  upper_layout->setSpacing(0);
  canvas_ = new QtFaceTextureCanvas(upper_);
  upper_layout->addWidget(canvas_, 1);
  controls_scroll_ = new QtFaceTextureControlsScrollArea(upper_);
  controls_scroll_->setObjectName(QStringLiteral("face_texture.controls_scroll"));
  controls_scroll_->setWidgetResizable(true);
  controls_scroll_->setFrameShape(QFrame::NoFrame);
  controls_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  controls_scroll_->setMinimumHeight(80);
  controls_scroll_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  controls_ = new QtFaceTextureControls(controls_scroll_);
  controls_scroll_->setWidget(controls_);
  upper_layout->addWidget(controls_scroll_);
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
  return splitter_->saveState();
}

bool QtFaceTexturePanel::restore_splitter_state(const QByteArray &state) {
  return !state.isEmpty() && splitter_->restoreState(state);
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
