////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMetaObject>
#include <QMouseEvent>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

namespace quader {
namespace {

struct InspectorTransformRuntime {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization;
  UIInspectorPreviewContext preview{.document = session.scene(),
                                    .assets = session.assets()};
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .inspector_preview = &preview,
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
  EntityId entity = kInvalidEntityId;

  void initialize() {
    const auto created = editor.scene().create_empty();
    ASSERT_TRUE(created.ok()) << created.error_message();
    entity = created.value().id;
    ASSERT_TRUE(editor.scene().select(entity).ok());
  }

  void expect_position_x(double expected) {
    const auto snapshot = editor.query().entity(entity);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_NEAR(snapshot->transform.position.x, expected, 0.0001);
  }
};

void send_mouse(QWidget &target, QEvent::Type type, QPoint position,
                Qt::MouseButton button, Qt::MouseButtons buttons) {
  QMouseEvent event(type, QPointF(position),
                   QPointF(target.mapToGlobal(position)), button, buttons,
                   Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &event);
}

QPoint begin_drag(QLineEdit &editor) {
  const QPoint center = editor.rect().center();
  send_mouse(editor, QEvent::MouseButtonPress, center, Qt::LeftButton,
             Qt::LeftButton);
  const QPoint origin =
      center + QPoint(QApplication::startDragDistance() + 1, 0);
  send_mouse(editor, QEvent::MouseMove, origin, Qt::NoButton, Qt::LeftButton);
  return origin;
}

void move_drag(QLineEdit &editor, QPoint origin, int pixels) {
  send_mouse(editor, QEvent::MouseMove, origin + QPoint(pixels, 0),
             Qt::NoButton, Qt::LeftButton);
}

void release_drag(QLineEdit &editor, QPoint origin, int pixels) {
  send_mouse(editor, QEvent::MouseButtonRelease, origin + QPoint(pixels, 0),
             Qt::LeftButton, Qt::NoButton);
}

void deliver_timeout(QTimer &timer) {
  // QTimer deactivates a single-shot timer before naturally emitting timeout.
  // Reproduce that boundary without letting wall-clock scheduling enter tests.
  timer.stop();
  ASSERT_TRUE(
      QMetaObject::invokeMethod(&timer, "timeout", Qt::DirectConnection));
}

class QtInspectorNumericDragTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
  }

  void show(InspectorPanel &panel, InspectorTransformRuntime &runtime) {
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.resize(640, 600);
    panel.refresh(runtime.context);
    panel.show();
    QCoreApplication::processEvents();
  }

  QDoubleSpinBox *position_x(InspectorPanel &panel) {
    return panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("inspector.transform.position.x"));
  }

  QTimer *dispatch_timer(InspectorPanel &panel) {
    return panel.findChild<QTimer *>(
        QStringLiteral("inspector.transform.dispatchTimer"));
  }

private:
  int argc_ = 1;
  char executable_[36] = "quader-inspector-numeric-drag-tests";
  char *argv_[2] = {executable_, nullptr};
  std::unique_ptr<QApplication> application_;
};

TEST_F(QtInspectorNumericDragTests,
       PointerBurstsDispatchTheLatestValueAndReleaseCreatesOneUndo) {
  InspectorTransformRuntime runtime;
  runtime.initialize();
  InspectorPanel panel;
  show(panel, runtime);
  auto *spin = position_x(panel);
  auto *timer = dispatch_timer(panel);
  ASSERT_NE(spin, nullptr);
  ASSERT_NE(timer, nullptr);
  auto *editor = spin->findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  EXPECT_TRUE(timer->isSingleShot());
  EXPECT_EQ(timer->timerType(), Qt::PreciseTimer);
  EXPECT_EQ(timer->interval(), 16);
  const auto revision_before = runtime.session.scene().transform_revision();
  const auto undo_before = runtime.session.history().undo_count();

  const QPoint origin = begin_drag(*editor);
  ASSERT_TRUE(qt_install_numeric_spin_box(*spin).is_dragging());
  for (int pixels : {8, 16, 24, 32}) {
    move_drag(*editor, origin, pixels);
    EXPECT_NEAR(spin->value(), pixels * 0.01, 0.0001);
    EXPECT_EQ(runtime.session.scene().transform_revision(), revision_before);
  }
  EXPECT_TRUE(timer->isActive());
  runtime.expect_position_x(0.0);
  EXPECT_EQ(runtime.session.history().undo_count(), undo_before);

  deliver_timeout(*timer);
  runtime.expect_position_x(0.32);
  const auto dispatched_revision = runtime.session.scene().transform_revision();
  EXPECT_GT(dispatched_revision, revision_before);
  EXPECT_EQ(runtime.session.history().undo_count(), undo_before);
  deliver_timeout(*timer);
  EXPECT_EQ(runtime.session.scene().transform_revision(), dispatched_revision);

  move_drag(*editor, origin, 40);
  move_drag(*editor, origin, 48);
  EXPECT_TRUE(timer->isActive());
  runtime.expect_position_x(0.32);
  EXPECT_EQ(runtime.session.scene().transform_revision(), dispatched_revision);
  release_drag(*editor, origin, 48);
  EXPECT_FALSE(timer->isActive());
  EXPECT_FALSE(qt_install_numeric_spin_box(*spin).is_dragging());
  runtime.expect_position_x(0.48);
  EXPECT_EQ(runtime.session.history().undo_count(), undo_before + 1u);
  const auto committed_revision = runtime.session.scene().transform_revision();
  deliver_timeout(*timer);
  EXPECT_EQ(runtime.session.scene().transform_revision(), committed_revision);

  ASSERT_TRUE(runtime.editor.history().undo().ok());
  runtime.expect_position_x(0.0);
  EXPECT_EQ(runtime.session.history().undo_count(), undo_before);
  ASSERT_TRUE(runtime.editor.history().redo().ok());
  runtime.expect_position_x(0.48);
  panel.abandon_runtime();
}

TEST_F(QtInspectorNumericDragTests,
       EscapeDropsPendingMotionAndRollsBackAlreadyDispatchedPreview) {
  for (bool dispatch_before_cancel : {false, true}) {
    SCOPED_TRACE(dispatch_before_cancel);
    InspectorTransformRuntime runtime;
    runtime.initialize();
    InspectorPanel panel;
    show(panel, runtime);
    auto *spin = position_x(panel);
    auto *timer = dispatch_timer(panel);
    ASSERT_NE(spin, nullptr);
    ASSERT_NE(timer, nullptr);
    auto *editor = spin->findChild<QLineEdit *>();
    ASSERT_NE(editor, nullptr);
    const auto undo_before = runtime.session.history().undo_count();
    const QPoint origin = begin_drag(*editor);
    move_drag(*editor, origin, 16);
    if (dispatch_before_cancel) {
      deliver_timeout(*timer);
      runtime.expect_position_x(0.16);
    }
    move_drag(*editor, origin, 32);
    ASSERT_TRUE(timer->isActive());
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &escape);

    EXPECT_FALSE(timer->isActive());
    EXPECT_FALSE(qt_install_numeric_spin_box(*spin).is_dragging());
    runtime.expect_position_x(0.0);
    EXPECT_EQ(runtime.session.history().undo_count(), undo_before);
    const auto canceled_revision = runtime.session.scene().transform_revision();
    deliver_timeout(*timer);
    release_drag(*editor, origin, 32);
    EXPECT_EQ(runtime.session.scene().transform_revision(), canceled_revision);
    EXPECT_EQ(runtime.session.history().undo_count(), undo_before);
    runtime.expect_position_x(0.0);
    panel.abandon_runtime();
  }
}

TEST_F(QtInspectorNumericDragTests,
       RuntimeAbandonmentDropsPendingMotionWithoutCallingTheOutgoingOwner) {
  InspectorTransformRuntime runtime;
  runtime.initialize();
  InspectorPanel panel;
  show(panel, runtime);
  auto *spin = position_x(panel);
  auto *timer = dispatch_timer(panel);
  ASSERT_NE(spin, nullptr);
  ASSERT_NE(timer, nullptr);
  auto *editor = spin->findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  const QPoint origin = begin_drag(*editor);
  move_drag(*editor, origin, 16);
  deliver_timeout(*timer);
  runtime.expect_position_x(0.16);
  move_drag(*editor, origin, 32);
  ASSERT_TRUE(timer->isActive());
  const auto outgoing_revision = runtime.session.scene().transform_revision();
  const auto undo_before = runtime.session.history().undo_count();

  panel.abandon_runtime();
  EXPECT_FALSE(timer->isActive());
  EXPECT_FALSE(qt_install_numeric_spin_box(*spin).is_dragging());
  EXPECT_EQ(runtime.session.scene().transform_revision(), outgoing_revision);
  runtime.expect_position_x(0.16);
  deliver_timeout(*timer);
  release_drag(*editor, origin, 32);
  EXPECT_EQ(runtime.session.scene().transform_revision(), outgoing_revision);
  EXPECT_EQ(runtime.session.history().undo_count(), undo_before);
  runtime.expect_position_x(0.16);
}

TEST_F(QtInspectorNumericDragTests,
       RuntimeRebindingCannotDispatchOldMotionIntoEitherDocument) {
  InspectorTransformRuntime outgoing;
  InspectorTransformRuntime incoming;
  outgoing.initialize();
  incoming.initialize();
  InspectorPanel panel;
  show(panel, outgoing);
  auto *spin = position_x(panel);
  auto *timer = dispatch_timer(panel);
  ASSERT_NE(spin, nullptr);
  ASSERT_NE(timer, nullptr);
  auto *editor = spin->findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  const QPoint origin = begin_drag(*editor);
  move_drag(*editor, origin, 16);
  deliver_timeout(*timer);
  move_drag(*editor, origin, 32);
  ASSERT_TRUE(timer->isActive());
  const auto outgoing_revision = outgoing.session.scene().transform_revision();
  const auto incoming_revision = incoming.session.scene().transform_revision();
  const auto outgoing_undo = outgoing.session.history().undo_count();
  const auto incoming_undo = incoming.session.history().undo_count();

  panel.refresh(incoming.context);
  EXPECT_FALSE(timer->isActive());
  deliver_timeout(*timer);
  QCoreApplication::processEvents();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  EXPECT_EQ(outgoing.session.scene().transform_revision(), outgoing_revision);
  EXPECT_EQ(incoming.session.scene().transform_revision(), incoming_revision);
  EXPECT_EQ(outgoing.session.history().undo_count(), outgoing_undo);
  EXPECT_EQ(incoming.session.history().undo_count(), incoming_undo);
  outgoing.expect_position_x(0.16);
  incoming.expect_position_x(0.0);

  // Fresh text/keyboard updates remain immediate after the binding changes.
  spin = position_x(panel);
  ASSERT_NE(spin, nullptr);
  ASSERT_DOUBLE_EQ(spin->value(), 0.0);
  spin->setValue(2.0);
  incoming.expect_position_x(2.0);
  EXPECT_FALSE(timer->isActive());
  ASSERT_TRUE(
      QMetaObject::invokeMethod(spin, "editingFinished", Qt::DirectConnection));
  EXPECT_EQ(incoming.session.history().undo_count(), incoming_undo + 1u);
  EXPECT_EQ(outgoing.session.scene().transform_revision(), outgoing_revision);
  panel.abandon_runtime();
}

TEST_F(QtInspectorNumericDragTests,
       ControlledSampleReplayReducesTransformWorkAndRetainsImmediateTextUpdates) {
  InspectorTransformRuntime runtime;
  runtime.initialize();
  InspectorPanel panel;
  show(panel, runtime);
  auto *spin = position_x(panel);
  auto *timer = dispatch_timer(panel);
  ASSERT_NE(spin, nullptr);
  ASSERT_NE(timer, nullptr);
  constexpr int kSamples = 1000;
  const auto undo_before = runtime.session.history().undo_count();
  const auto immediate_revision_before =
      runtime.session.scene().transform_revision();
  const auto immediate_start = std::chrono::steady_clock::now();
  for (int sample = 1; sample <= kSamples; ++sample) {
    spin->setValue(sample * 0.01);
  }
  ASSERT_TRUE(
      QMetaObject::invokeMethod(spin, "editingFinished", Qt::DirectConnection));
  const auto immediate_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - immediate_start)
                                .count();
  const auto immediate_revisions = runtime.session.scene().transform_revision() -
                                   immediate_revision_before;
  EXPECT_FALSE(timer->isActive());
  runtime.expect_position_x(10.0);
  EXPECT_EQ(runtime.session.history().undo_count(), undo_before + 1u);
  ASSERT_TRUE(runtime.editor.history().undo().ok());
  runtime.expect_position_x(0.0);
  panel.refresh(runtime.context);
  // QTreeWidget releases replaced item widgets through DeferredDelete. Find
  // the rebuilt field only after that boundary, as a real next gesture does.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents();
  spin = position_x(panel);
  ASSERT_NE(spin, nullptr);
  ASSERT_DOUBLE_EQ(spin->value(), 0.0);
  auto *editor = spin->findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);

  const auto drag_revision_before = runtime.session.scene().transform_revision();
  const auto drag_start = std::chrono::steady_clock::now();
  const QPoint origin = begin_drag(*editor);
  for (int sample = 1; sample <= kSamples; ++sample) {
    move_drag(*editor, origin, sample);
    if (sample % 16 == 0) {
      deliver_timeout(*timer);
    }
  }
  release_drag(*editor, origin, kSamples);
  const auto drag_us = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - drag_start)
                           .count();
  const auto drag_revisions =
      runtime.session.scene().transform_revision() - drag_revision_before;
  runtime.expect_position_x(10.0);
  EXPECT_EQ(runtime.session.history().undo_count(), undo_before + 1u);
  EXPECT_FALSE(timer->isActive());
  EXPECT_GT(immediate_revisions, 0u);
  EXPECT_GT(drag_revisions, 0u);
  EXPECT_LT(drag_revisions * 8u, immediate_revisions);

  // This controls event count and dispatch opportunities in one scene/build;
  // elapsed times are observations, not a real-time frame-rate assertion.
  RecordProperty("samples", kSamples);
  RecordProperty("immediate_microseconds", std::to_string(immediate_us));
  RecordProperty("scrub_microseconds", std::to_string(drag_us));
  RecordProperty("immediate_transform_revisions",
                 std::to_string(immediate_revisions));
  RecordProperty("scrub_transform_revisions", std::to_string(drag_revisions));
  std::cout << "[ Inspector numeric cadence ] samples=" << kSamples
            << " immediate_us=" << immediate_us << " scrub_us=" << drag_us
            << " immediate_transform_revisions=" << immediate_revisions
            << " scrub_transform_revisions=" << drag_revisions << '\n';
  panel.abandon_runtime();
}

} // namespace
} // namespace quader
