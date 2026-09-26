////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_contextual_input_hints.hpp"
#include "ui/qt_material_preview_surface.hpp"
#include "ui/qt_texture_info_window.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

namespace quader {
namespace {

using Gesture = QuaderOperationInputGesture;

QPointF global_position(QObject &surface, QPointF local_position) {
  if (const auto *widget = qobject_cast<QWidget *>(&surface)) {
    return widget->mapToGlobal(local_position.toPoint());
  }
  if (const auto *window = qobject_cast<QWindow *>(&surface)) {
    return window->mapToGlobal(local_position.toPoint());
  }
  return local_position;
}

void hover(QObject &surface) {
  const QPointF local_position(40, 40);
  QEnterEvent event(local_position, local_position,
                    global_position(surface, local_position));
  QCoreApplication::sendEvent(&surface, &event);
}

void mouse(QObject &surface, QEvent::Type type, Qt::MouseButton button,
           Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers,
           QPointF position = QPointF(40, 40)) {
  QMouseEvent event(type, position, global_position(surface, position), button,
                    buttons, modifiers);
  QCoreApplication::sendEvent(&surface, &event);
}

void lose_input(QObject &surface, QEvent::Type type) {
  if (type == QEvent::FocusOut) {
    QFocusEvent event(type, Qt::ActiveWindowFocusReason);
    QCoreApplication::sendEvent(&surface, &event);
  } else {
    QEvent event(type);
    QCoreApplication::sendEvent(&surface, &event);
  }
}

bool has_hint(std::span<const QuaderOperationInputHint> hints,
              Gesture gesture, std::string_view label_key) {
  return std::ranges::any_of(hints, [&](const auto &hint) {
    return hint.gesture == gesture && hint.label_key == label_key;
  });
}

class QtPreviewInputHintTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_.data());
    application_->setQuitOnLastWindowClosed(false);
  }

  void TearDown() override { application_.reset(); }

  int argc_ = 1;
  char name_[32] = "quader-preview-input-hint-tests";
  std::array<char *, 2> argv_{name_, nullptr};
  std::unique_ptr<QApplication> application_;
};

TEST_F(QtPreviewInputHintTests, MaterialPreviewReportsNativeGesturesWithoutChangingState) {
  QWidget host;
  host.setAttribute(Qt::WA_DontShowOnScreen, true);
  auto *layout = new QVBoxLayout(&host);
  auto *surface = new QtMaterialPreviewSurface;
  layout->addWidget(QWidget::createWindowContainer(surface, &host));
  host.resize(320, 240);
  host.show();
  QCoreApplication::processEvents();
  ASSERT_TRUE(surface->isVisible());
  ASSERT_NE(surface->handle(), nullptr);
  hover(*surface);

  const auto preview_revision = surface->preview_state().revision();
  const auto hint_state = qt_contextual_input_hints_state(*application_);
  EXPECT_TRUE(hint_state.overrides_viewport);
  const auto hints = qt_contextual_input_hints(*application_);
  ASSERT_EQ(hints.size(), 5u);
  EXPECT_TRUE(has_hint(hints, Gesture::MiddleDrag,
                       "operation.hints.preview.rotate"));
  EXPECT_TRUE(has_hint(hints, Gesture::ShiftMiddleDrag,
                       "operation.hints.preview.pan"));
  EXPECT_TRUE(has_hint(hints, Gesture::ControlMiddleDrag,
                       "operation.hints.preview.environment"));
  EXPECT_TRUE(has_hint(hints, Gesture::Wheel,
                       "operation.hints.preview.zoom"));
  EXPECT_TRUE(has_hint(hints, Gesture::MiddleDoubleClick,
                       "operation.hints.preview.reset"));
  for (int count = 0; count < 10; ++count) {
    EXPECT_EQ(qt_contextual_input_hints_state(*application_).revision,
              hint_state.revision);
    EXPECT_EQ(qt_contextual_input_hints(*application_).data(), hints.data());
  }
  EXPECT_EQ(surface->preview_state().revision(), preview_revision);

  surface->hide();
  QCoreApplication::processEvents();
  EXPECT_TRUE(qt_contextual_input_hints(*application_).empty());
}

TEST_F(QtPreviewInputHintTests, MaterialDragKeepsCapturedModeAfterModifiersAndPointerChange) {
  QWidget host;
  host.setAttribute(Qt::WA_DontShowOnScreen, true);
  auto *layout = new QVBoxLayout(&host);
  auto *surface = new QtMaterialPreviewSurface;
  layout->addWidget(QWidget::createWindowContainer(surface, &host));
  host.resize(320, 240);
  host.show();
  QCoreApplication::processEvents();
  ASSERT_TRUE(surface->isVisible());
  ASSERT_NE(surface->handle(), nullptr);

  struct DragCase {
    Qt::KeyboardModifiers modifiers;
    Gesture gesture;
    std::string_view key;
  };
  const std::array cases{
      DragCase{Qt::NoModifier, Gesture::MiddleDrag,
               "operation.hints.preview.rotate"},
      DragCase{Qt::ShiftModifier, Gesture::ShiftMiddleDrag,
               "operation.hints.preview.pan"},
      DragCase{Qt::ControlModifier | Qt::ShiftModifier, Gesture::ControlMiddleDrag,
               "operation.hints.preview.environment"},
      DragCase{Qt::MetaModifier, Gesture::ControlMiddleDrag,
               "operation.hints.preview.environment"},
  };
  for (const auto &entry : cases) {
    SCOPED_TRACE(entry.key);
    mouse(*surface, QEvent::MouseButtonPress, Qt::MiddleButton,
          Qt::MiddleButton, entry.modifiers);
    auto hints = qt_contextual_input_hints(*application_);
    ASSERT_EQ(hints.size(), 1u);
    EXPECT_TRUE(has_hint(hints, entry.gesture, entry.key));
    EXPECT_TRUE(hints.front().active);

    QKeyEvent changed_modifiers(QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
    QCoreApplication::sendEvent(surface, &changed_modifiers);
    hover(host);
    hints = qt_contextual_input_hints(*application_);
    ASSERT_EQ(hints.size(), 1u);
    EXPECT_TRUE(has_hint(hints, entry.gesture, entry.key));
    EXPECT_TRUE(hints.front().active);

    mouse(*surface, QEvent::MouseButtonRelease, Qt::MiddleButton,
          Qt::NoButton, Qt::NoModifier);
    EXPECT_EQ(qt_contextual_input_hints(*application_).size(), 5u);
  }
}

TEST_F(QtPreviewInputHintTests, TextureHintsTrackImageAvailabilityAndMaskMetadataHover) {
  QtTextureInfoWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.show();
  QCoreApplication::processEvents();
  auto *canvas = window.findChild<QWidget *>(QStringLiteral("textureInfo.canvas"));
  auto *table = window.findChild<QTableWidget *>(QStringLiteral("textureInfo.table"));
  ASSERT_NE(canvas, nullptr);
  ASSERT_NE(table, nullptr);
  hover(*canvas);
  EXPECT_TRUE(qt_contextual_input_hints_state(*application_).overrides_viewport);
  EXPECT_TRUE(qt_contextual_input_hints(*application_).empty());

  const std::array pixels{std::byte{20}, std::byte{30}, std::byte{40}, std::byte{255}};
  ASSERT_TRUE(window.present(TextureInfoPresentation{
      .name = "sample.png", .width = 1u, .height = 1u, .rgba8 = pixels}));
  const auto rebuilds = window.masked_image_rebuild_count();
  const auto hint_state = qt_contextual_input_hints_state(*application_);
  const auto hints = qt_contextual_input_hints(*application_);
  ASSERT_EQ(hints.size(), 3u);
  EXPECT_TRUE(has_hint(hints, Gesture::PrimaryOrMiddleDrag,
                       "operation.hints.texture_info.pan"));
  EXPECT_TRUE(has_hint(hints, Gesture::Wheel,
                       "operation.hints.texture_info.zoom"));
  EXPECT_TRUE(has_hint(hints, Gesture::PrimaryOrMiddleDoubleClick,
                       "operation.hints.texture_info.fit"));
  EXPECT_EQ(qt_contextual_input_hints_state(*application_).revision,
            hint_state.revision);
  EXPECT_EQ(qt_contextual_input_hints(*application_).data(), hints.data());
  EXPECT_EQ(window.masked_image_rebuild_count(), rebuilds);

  hover(*table->viewport());
  EXPECT_TRUE(qt_contextual_input_hints_state(*application_).overrides_viewport);
  EXPECT_TRUE(qt_contextual_input_hints(*application_).empty());
  hover(*canvas);
  window.clear();
  EXPECT_TRUE(qt_contextual_input_hints_state(*application_).overrides_viewport);
  EXPECT_TRUE(qt_contextual_input_hints(*application_).empty());
}

TEST_F(QtPreviewInputHintTests, TexturePanKeepsHintsOutsideCanvasUntilReleaseOrClear) {
  QtTextureInfoWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.show();
  QCoreApplication::processEvents();
  auto *canvas = window.findChild<QWidget *>(QStringLiteral("textureInfo.canvas"));
  ASSERT_NE(canvas, nullptr);
  const std::array pixels{std::byte{20}, std::byte{30}, std::byte{40}, std::byte{255}};
  ASSERT_TRUE(window.present(TextureInfoPresentation{
      .name = "sample.png", .width = 1u, .height = 1u, .rgba8 = pixels}));
  for (const auto button : {Qt::LeftButton, Qt::MiddleButton}) {
    mouse(*canvas, QEvent::MouseButtonPress, button, button, Qt::NoModifier);
    auto hints = qt_contextual_input_hints(*application_);
    ASSERT_EQ(hints.size(), 1u);
    EXPECT_TRUE(hints.front().active);
    hover(window);
    hints = qt_contextual_input_hints(*application_);
    ASSERT_EQ(hints.size(), 1u);
    EXPECT_TRUE(has_hint(hints, Gesture::PrimaryOrMiddleDrag,
                         "operation.hints.texture_info.pan"));
    mouse(*canvas, QEvent::MouseButtonRelease, button, Qt::NoButton, Qt::NoModifier);
    hover(window);
    EXPECT_TRUE(qt_contextual_input_hints(*application_).empty());
  }

  mouse(*canvas, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  ASSERT_EQ(qt_contextual_input_hints(*application_).size(), 1u);
  window.clear();
  EXPECT_TRUE(qt_contextual_input_hints(*application_).empty());
}

TEST_F(QtPreviewInputHintTests, MaterialCaptureLossRetiresDragBeforeFurtherHeldMoves) {
  QWidget host;
  host.setAttribute(Qt::WA_DontShowOnScreen, true);
  auto *layout = new QVBoxLayout(&host);
  auto *surface = new QtMaterialPreviewSurface;
  layout->addWidget(QWidget::createWindowContainer(surface, &host));
  host.resize(320, 240);
  host.show();
  QCoreApplication::processEvents();
  ASSERT_TRUE(surface->isVisible());
  ASSERT_NE(surface->handle(), nullptr);

  for (const auto event_type : {QEvent::UngrabMouse, QEvent::FocusOut,
                                QEvent::WindowDeactivate, QEvent::WindowBlocked,
                                QEvent::Hide}) {
    SCOPED_TRACE(static_cast<int>(event_type));
    mouse(*surface, QEvent::MouseButtonPress, Qt::MiddleButton,
          Qt::MiddleButton, Qt::ShiftModifier);
    ASSERT_TRUE(qt_contextual_input_hints_state(*application_).active);
    const auto preview_revision = surface->preview_state().revision();
    if (event_type == QEvent::Hide) {
      surface->hide();
      surface->show();
      QCoreApplication::processEvents();
    } else {
      lose_input(*surface, event_type);
    }
    mouse(*surface, QEvent::MouseMove, Qt::NoButton, Qt::MiddleButton,
          Qt::ShiftModifier, QPointF(70, 90));
    EXPECT_EQ(surface->preview_state().revision(), preview_revision);
    EXPECT_FALSE(qt_contextual_input_hints_state(*application_).active);
    EXPECT_EQ(qt_contextual_input_hints(*application_).size(), 5u);
    if (event_type == QEvent::WindowBlocked) {
      QEvent unblocked(QEvent::WindowUnblocked);
      QCoreApplication::sendEvent(surface, &unblocked);
    }
  }
}

TEST_F(QtPreviewInputHintTests, TextureCaptureLossRetiresPanBeforeFurtherHeldMoves) {
  QtTextureInfoWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.show();
  QCoreApplication::processEvents();
  auto *canvas = window.findChild<QWidget *>(QStringLiteral("textureInfo.canvas"));
  ASSERT_NE(canvas, nullptr);
  const std::array pixels{std::byte{20}, std::byte{30}, std::byte{40}, std::byte{255}};
  ASSERT_TRUE(window.present(TextureInfoPresentation{
      .name = "sample.png", .width = 1u, .height = 1u, .rgba8 = pixels}));
  for (const auto button : {Qt::LeftButton, Qt::MiddleButton}) {
    for (const auto event_type : {QEvent::UngrabMouse, QEvent::FocusOut,
                                  QEvent::WindowDeactivate, QEvent::Hide}) {
      SCOPED_TRACE(static_cast<int>(event_type));
      mouse(*canvas, QEvent::MouseButtonPress, button, button, Qt::NoModifier);
      ASSERT_TRUE(qt_contextual_input_hints_state(*application_).active);
      const auto original_pan_x = canvas->property("panX").toDouble();
      const auto original_pan_y = canvas->property("panY").toDouble();
      if (event_type == QEvent::Hide) {
        canvas->hide();
        canvas->show();
        QCoreApplication::processEvents();
      } else {
        lose_input(*canvas, event_type);
      }
      mouse(*canvas, QEvent::MouseMove, Qt::NoButton, button,
            Qt::NoModifier, QPointF(70, 90));
      EXPECT_DOUBLE_EQ(canvas->property("panX").toDouble(), original_pan_x);
      EXPECT_DOUBLE_EQ(canvas->property("panY").toDouble(), original_pan_y);
      EXPECT_FALSE(qt_contextual_input_hints_state(*application_).active);
      EXPECT_EQ(qt_contextual_input_hints(*application_).size(), 3u);
    }
  }
}

TEST_F(QtPreviewInputHintTests, TextureMoveWithoutPanButtonsCannotContinueOrRevivePan) {
  QtTextureInfoWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.show();
  QCoreApplication::processEvents();
  auto *canvas = window.findChild<QWidget *>(QStringLiteral("textureInfo.canvas"));
  ASSERT_NE(canvas, nullptr);
  const std::array pixels{std::byte{20}, std::byte{30}, std::byte{40}, std::byte{255}};
  ASSERT_TRUE(window.present(TextureInfoPresentation{
      .name = "sample.png", .width = 1u, .height = 1u, .rgba8 = pixels}));
  for (const auto button : {Qt::LeftButton, Qt::MiddleButton}) {
    for (const auto moved_buttons : {Qt::NoButton, Qt::RightButton}) {
      mouse(*canvas, QEvent::MouseButtonPress, button, button, Qt::NoModifier);
      ASSERT_TRUE(qt_contextual_input_hints_state(*application_).active);
      const auto original_pan_x = canvas->property("panX").toDouble();
      const auto original_pan_y = canvas->property("panY").toDouble();
      mouse(*canvas, QEvent::MouseMove, Qt::NoButton, moved_buttons,
            Qt::NoModifier, QPointF(70, 90));
      EXPECT_FALSE(qt_contextual_input_hints_state(*application_).active);
      EXPECT_DOUBLE_EQ(canvas->property("panX").toDouble(), original_pan_x);
      EXPECT_DOUBLE_EQ(canvas->property("panY").toDouble(), original_pan_y);
      mouse(*canvas, QEvent::MouseMove, Qt::NoButton, button,
            Qt::NoModifier, QPointF(100, 120));
      EXPECT_DOUBLE_EQ(canvas->property("panX").toDouble(), original_pan_x);
      EXPECT_DOUBLE_EQ(canvas->property("panY").toDouble(), original_pan_y);
      EXPECT_FALSE(qt_contextual_input_hints_state(*application_).active);

      mouse(*canvas, QEvent::MouseButtonPress, button, button, Qt::NoModifier);
      mouse(*canvas, QEvent::MouseMove, Qt::NoButton, button,
            Qt::NoModifier, QPointF(70, 90));
      EXPECT_GT(canvas->property("panX").toDouble(), original_pan_x);
      EXPECT_GT(canvas->property("panY").toDouble(), original_pan_y);
      mouse(*canvas, QEvent::MouseButtonRelease, button, Qt::NoButton, Qt::NoModifier);
    }
  }
}

} // namespace
} // namespace quader
