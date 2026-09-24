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
#include "ui/qt_application_theme.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEnterEvent>
#include <QHoverEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <array>
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

  void initialize_box() {
    const auto created = editor.modeling().create_box(
        QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                     .max = {1.0f, 1.0f, 1.0f}},
        "Box");
    ASSERT_TRUE(created.ok()) << created.error_message();
    entity = created.value().entity;
    ASSERT_TRUE(editor.scene()
                    .set_transform(entity, {.position = {1.0f, 0.5f, -4.5f}})
                    .ok());
    ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
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

void click(QWidget &target, QPoint position) {
  send_mouse(target, QEvent::MouseButtonPress, position, Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(target, QEvent::MouseButtonRelease, position, Qt::LeftButton,
             Qt::NoButton);
}

void send_key(QWidget &target, int key, const QString &text = {}) {
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
  QCoreApplication::sendEvent(&target, &press);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
  QCoreApplication::sendEvent(&target, &release);
}

void type_text(QLineEdit &editor, const QString &text) {
  for (const QChar character : text) {
    send_key(editor, character.unicode(), QString(character));
  }
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

void settle_inspector_layout(InspectorPanel &panel) {
  // Scale changes and numeric minimum-width updates queue native layout work.
  for (int pass = 0; pass < 3; ++pass) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QCoreApplication::processEvents();
    panel.layout()->activate();
  }
}

void export_inspector_image(InspectorPanel &panel, const QString &name) {
  const QString directory =
      qEnvironmentVariable("QUADER_INSPECTOR_SCREENSHOT_DIR");
  if (directory.isEmpty()) {
    return;
  }
  ASSERT_TRUE(QDir().mkpath(directory));
  ASSERT_TRUE(panel.grab().save(QDir(directory).filePath(name)));
}

QRect relative_geometry(const QWidget &widget, const QWidget &ancestor) {
  return {widget.mapTo(&ancestor, QPoint{}), widget.size()};
}

QTreeWidgetItem *transform_section(QTreeWidget &tree) {
  for (int index = 0; index < tree.topLevelItemCount(); ++index) {
    auto *item = tree.topLevelItem(index);
    if (item->data(0, Qt::UserRole).toString() == QStringLiteral("transform")) {
      return item;
    }
  }
  return nullptr;
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

  QApplication &application() { return *application_; }

private:
  int argc_ = 1;
  char executable_[36] = "quader-inspector-numeric-drag-tests";
  char *argv_[2] = {executable_, nullptr};
  std::unique_ptr<QApplication> application_;
};

TEST_F(QtInspectorNumericDragTests,
       TransformAxesStayStackedAlignedAndReadableInNarrowScaledPanels) {
  application().setPalette(
      qt_obsidian_application_palette(application().palette()));
  QtUiScaleApplicationController scale_controller(application());
  InspectorTransformRuntime runtime;
  runtime.initialize_box();
  InspectorPanel panel;
  scale_controller.add_change_listener(
      panel, [&](const QtUiScaleMetrics &metrics) {
        panel.apply_ui_scale(metrics);
      });
  show(panel, runtime);
  auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);
  auto *name = panel.findChild<QLineEdit *>(
      QStringLiteral("inspector.general.name"));
  ASSERT_NE(name, nullptr);
  EXPECT_TRUE(name->hasFrame());
  EXPECT_FALSE(name->isReadOnly());

  for (const float scale : {1.0F, 2.0F}) {
    scale_controller.apply_factor(scale);
    for (const int design_width : {418, 320}) {
      SCOPED_TRACE(::testing::Message()
                   << "scale=" << scale << " design_width=" << design_width);
      panel.resize(scale_controller.metrics().size(QSize(design_width, 760)));
      settle_inspector_layout(panel);
      EXPECT_EQ(panel.width(), scale_controller.metrics().px(design_width));
      for (const char *property : {"position", "rotation", "scale"}) {
        const QString row_name =
            QStringLiteral("inspector.transform.%1")
                .arg(QString::fromLatin1(property));
        auto *row = panel.findChild<QWidget *>(row_name);
        ASSERT_NE(row, nullptr);
        QRect previous_field;
        for (const char *axis : {"x", "y", "z"}) {
          const QString field_name =
              row_name + QLatin1Char('.') + QString::fromLatin1(axis);
          SCOPED_TRACE(field_name.toStdString());
          auto *field = panel.findChild<QDoubleSpinBox *>(field_name);
          auto *label = panel.findChild<QLabel *>(
              field_name + QStringLiteral(".label"));
          ASSERT_NE(field, nullptr);
          ASSERT_NE(label, nullptr);
          auto *editor = field->findChild<QLineEdit *>();
          ASSERT_NE(editor, nullptr);
          EXPECT_TRUE(field->hasFrame());
          EXPECT_TRUE(field->isEnabled());
          EXPECT_FALSE(field->isReadOnly());
          EXPECT_EQ(label->text(), QString::fromLatin1(axis).toUpper());

          const QRect field_rect = relative_geometry(*field, *row);
          const QRect label_rect = relative_geometry(*label, *row);
          EXPECT_TRUE(row->rect().contains(field_rect));
          EXPECT_TRUE(row->rect().contains(label_rect));
          EXPECT_LT(label_rect.right(), field_rect.left());
          EXPECT_NEAR(label_rect.center().y(), field_rect.center().y(), 1);
          EXPECT_GE(label->contentsRect().width(),
                    label->fontMetrics().horizontalAdvance(label->text()));
          if (!previous_field.isNull()) {
            EXPECT_GT(field_rect.top(), previous_field.bottom());
            EXPECT_EQ(field_rect.left(), previous_field.left());
            EXPECT_EQ(field_rect.right(), previous_field.right());
          }
          previous_field = field_rect;

          const QRect viewport_rect =
              relative_geometry(*field, *tree->viewport());
          EXPECT_GE(viewport_rect.left(), tree->columnWidth(0));
          EXPECT_LE(viewport_rect.right(), tree->viewport()->rect().right());
          EXPECT_GE(field->height(), field->minimumSizeHint().height());
          const QMargins margins = editor->textMargins();
          // QLineEdit retains two pixels of internal padding on each side.
          EXPECT_GE(editor->contentsRect().width() - margins.left() -
                        margins.right() - 4,
                    editor->fontMetrics().horizontalAdvance(editor->text()))
              << editor->text().toStdString();
        }
      }
      export_inspector_image(
          panel, QStringLiteral("inspector-box-%1-%2.png")
                     .arg(design_width)
                     .arg(qRound(scale * 100.0F)));
      if (design_width == 418) {
        auto *header = panel.findChild<QToolButton *>(
            QStringLiteral("inspector.transform.header"));
        ASSERT_NE(header, nullptr);
        const auto export_header_state = [&](const QString &state) {
          settle_inspector_layout(panel);
          export_inspector_image(
              panel, QStringLiteral("inspector-header-%1-%2.png")
                         .arg(state)
                         .arg(qRound(scale * 100.0F)));
        };
        application().setActiveWindow(&panel);
        header->clearFocus();
        header->setAttribute(Qt::WA_UnderMouse, false);
        const QPointF center(header->rect().center());
        const QPointF outside(-1.0, -1.0);
        const QPointF global_center(header->mapToGlobal(center.toPoint()));
        const QPointF global_outside(header->mapToGlobal(outside.toPoint()));
        QEvent initial_leave(QEvent::Leave);
        QCoreApplication::sendEvent(header, &initial_leave);
        QHoverEvent initial_hover_leave(QEvent::HoverLeave, outside,
                                        global_outside, center);
        QCoreApplication::sendEvent(header, &initial_hover_leave);
        export_header_state(QStringLiteral("normal"));
        EXPECT_FALSE(header->hasFocus());
        EXPECT_FALSE(header->underMouse());
        const QImage normal_header = header->grab().toImage();
        header->setAttribute(Qt::WA_UnderMouse, true);
        QEnterEvent enter(center, center, global_center);
        QCoreApplication::sendEvent(header, &enter);
        QHoverEvent hover_enter(QEvent::HoverEnter, center, global_center,
                                outside);
        QCoreApplication::sendEvent(header, &hover_enter);
        export_header_state(QStringLiteral("hover"));
        EXPECT_TRUE(header->underMouse());
        EXPECT_FALSE(normal_header == header->grab().toImage())
            << "The expanded header must visibly respond to hover.";
        header->setAttribute(Qt::WA_UnderMouse, false);
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(header, &leave);
        QHoverEvent hover_leave(QEvent::HoverLeave, outside, global_outside,
                                center);
        QCoreApplication::sendEvent(header, &hover_leave);
        application().setActiveWindow(&panel);
        header->setFocus(Qt::TabFocusReason);
        export_header_state(QStringLiteral("focus"));
        EXPECT_TRUE(header->hasFocus());
        EXPECT_FALSE(header->underMouse());
        EXPECT_FALSE(normal_header == header->grab().toImage())
            << "The expanded header must visibly indicate keyboard focus.";
        header->click();
        export_header_state(QStringLiteral("collapsed"));
        header->click();
        header->clearFocus();
        settle_inspector_layout(panel);
      }
    }
  }
  panel.abandon_runtime();
}

TEST_F(QtInspectorNumericDragTests,
       ResizeScaleAndUnchangedRefreshRetainFieldsAndCollapsedSections) {
  QtUiScaleApplicationController scale_controller(application());
  InspectorTransformRuntime runtime;
  runtime.initialize_box();
  InspectorPanel panel;
  scale_controller.add_change_listener(
      panel, [&](const QtUiScaleMetrics &metrics) {
        panel.apply_ui_scale(metrics);
      });
  show(panel, runtime);
  auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);
  auto *section = transform_section(*tree);
  ASSERT_NE(section, nullptr);
  QPointer<QToolButton> header(panel.findChild<QToolButton *>(
      QStringLiteral("inspector.transform.header")));
  ASSERT_NE(header.data(), nullptr);
  ASSERT_TRUE(section->isExpanded());
  const int initial_label_width = tree->columnWidth(0);
  std::array<QPointer<QDoubleSpinBox>, 9> fields;
  std::size_t index = 0;
  for (const char *property : {"position", "rotation", "scale"}) {
    for (const char *axis : {"x", "y", "z"}) {
      fields[index] = panel.findChild<QDoubleSpinBox *>(
          QStringLiteral("inspector.transform.%1.%2")
              .arg(QString::fromLatin1(property), QString::fromLatin1(axis)));
      ASSERT_NE(fields[index].data(), nullptr);
      ++index;
    }
  }
  const auto scene_revision = runtime.session.scene().revision();
  const auto transform_revision = runtime.session.scene().transform_revision();
  const auto undo_count = runtime.session.history().undo_count();
  section->setExpanded(false);

  for (const float scale : {2.0F, 3.0F, 1.0F}) {
    scale_controller.apply_factor(scale);
    for (const int width : {320, 418, 640}) {
      panel.resize(scale_controller.metrics().size(QSize(width, 760)));
      for (int refresh = 0; refresh < 3; ++refresh) {
        panel.refresh(runtime.context);
        panel.refresh_transform_preview(runtime.context);
      }
      settle_inspector_layout(panel);
      ASSERT_EQ(transform_section(*tree), section);
      EXPECT_FALSE(section->isExpanded());
      ASSERT_NE(header.data(), nullptr);
      EXPECT_EQ(panel.findChild<QToolButton *>(header->objectName()),
                header.data());
      EXPECT_FALSE(header->isChecked());
      EXPECT_EQ(header->arrowType(), Qt::RightArrow);
      for (const auto &field : fields) {
        ASSERT_NE(field.data(), nullptr);
        EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(field->objectName()),
                  field.data());
      }
    }
  }
  EXPECT_EQ(tree->columnWidth(0), initial_label_width);
  section->setExpanded(true);
  settle_inspector_layout(panel);
  EXPECT_TRUE(header->isChecked());
  EXPECT_EQ(header->arrowType(), Qt::DownArrow);
  for (const auto &field : fields) {
    EXPECT_TRUE(field->isVisible());
  }
  EXPECT_EQ(runtime.session.scene().revision(), scene_revision);
  EXPECT_EQ(runtime.session.scene().transform_revision(), transform_revision);
  EXPECT_EQ(runtime.session.history().undo_count(), undo_count);
  panel.abandon_runtime();
}

TEST_F(QtInspectorNumericDragTests,
       SectionHeadersUseTheWholeRowAndRetainPointerAndKeyboardDisclosure) {
  InspectorTransformRuntime runtime;
  runtime.initialize_box();
  InspectorPanel panel;
  show(panel, runtime);
  panel.resize(418, 760);
  settle_inspector_layout(panel);
  auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);
  EXPECT_FALSE(tree->rootIsDecorated());
  EXPECT_EQ(tree->indentation(), 0);
  for (int index = 0; index < tree->topLevelItemCount(); ++index) {
    auto *item = tree->topLevelItem(index);
    const QString stable_id = item->data(0, Qt::UserRole).toString();
    auto *header = panel.findChild<QToolButton *>(
        QStringLiteral("inspector.%1.header").arg(stable_id));
    ASSERT_NE(header, nullptr) << stable_id.toStdString();
    EXPECT_TRUE(item->isFirstColumnSpanned());
    EXPECT_EQ(tree->itemWidget(item, 0), header);
    EXPECT_TRUE(header->isCheckable());
    EXPECT_EQ(header->isChecked(), item->isExpanded());
    EXPECT_EQ(header->arrowType(), item->isExpanded() ? Qt::DownArrow
                                                    : Qt::RightArrow);
    EXPECT_EQ(header->accessibleName(), header->text());
    EXPECT_FALSE(header->accessibleName().isEmpty());
    const QRect header_rect = relative_geometry(*header, *tree->viewport());
    EXPECT_LE(header_rect.left(), 1);
    EXPECT_GE(header_rect.right(), tree->viewport()->rect().right() - 1);
  }

  auto *section = transform_section(*tree);
  auto *header = panel.findChild<QToolButton *>(
      QStringLiteral("inspector.transform.header"));
  auto *field = position_x(panel);
  ASSERT_NE(section, nullptr);
  ASSERT_NE(header, nullptr);
  ASSERT_NE(field, nullptr);
  const auto selection_before = runtime.session.scene().selected_entities();
  const auto revision_before = runtime.session.scene().revision();
  const auto undo_before = runtime.session.history().undo_count();

  click(*header, QPoint(header->width() - 3, header->height() / 2));
  settle_inspector_layout(panel);
  EXPECT_FALSE(section->isExpanded());
  EXPECT_FALSE(header->isChecked());
  EXPECT_EQ(header->arrowType(), Qt::RightArrow);
  EXPECT_FALSE(field->isVisible());
  EXPECT_EQ(position_x(panel), field);

  application().setActiveWindow(&panel);
  header->setFocus(Qt::TabFocusReason);
  send_key(*header, Qt::Key_Space);
  EXPECT_TRUE(section->isExpanded());
  EXPECT_TRUE(header->isChecked());
  send_key(*header, Qt::Key_Return);
  EXPECT_FALSE(section->isExpanded());
  send_key(*header, Qt::Key_Right);
  EXPECT_TRUE(section->isExpanded());
  send_key(*header, Qt::Key_Left);
  EXPECT_FALSE(section->isExpanded());

  tree->setCurrentItem(section);
  tree->setFocus(Qt::TabFocusReason);
  send_key(*tree, Qt::Key_Right);
  EXPECT_TRUE(section->isExpanded());
  EXPECT_TRUE(header->isChecked());
  EXPECT_EQ(header->arrowType(), Qt::DownArrow);
  send_key(*tree, Qt::Key_Left);
  EXPECT_FALSE(section->isExpanded());
  EXPECT_FALSE(header->isChecked());
  EXPECT_EQ(header->arrowType(), Qt::RightArrow);

  section->setExpanded(true);
  settle_inspector_layout(panel);
  EXPECT_TRUE(header->isChecked());
  EXPECT_EQ(header->arrowType(), Qt::DownArrow);
  EXPECT_TRUE(field->isVisible());
  EXPECT_EQ(position_x(panel), field);
  EXPECT_EQ(runtime.session.scene().selected_entities(), selection_before);
  EXPECT_EQ(runtime.session.scene().revision(), revision_before);
  EXPECT_EQ(runtime.session.history().undo_count(), undo_before);
  panel.abandon_runtime();
}

TEST_F(QtInspectorNumericDragTests,
       CollapseSettlesTheFocusedNumericDraftOnceAndRetainsSelectionAndUndo) {
  for (const bool pointer_collapse : {false, true}) {
    SCOPED_TRACE(pointer_collapse);
    InspectorTransformRuntime runtime;
    runtime.initialize();
    InspectorPanel panel;
    show(panel, runtime);
    application().setActiveWindow(&panel);
    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("inspector.tree"));
    ASSERT_NE(tree, nullptr);
    auto *section = transform_section(*tree);
    auto *header = panel.findChild<QToolButton *>(
        QStringLiteral("inspector.transform.header"));
    auto *spin = position_x(panel);
    ASSERT_NE(section, nullptr);
    ASSERT_NE(header, nullptr);
    ASSERT_NE(spin, nullptr);
    auto *editor = spin->findChild<QLineEdit *>();
    ASSERT_NE(editor, nullptr);
    auto &numeric = qt_install_numeric_spin_box(*spin);
    const auto selection_before = runtime.session.scene().selected_entities();
    const auto undo_before = runtime.session.history().undo_count();
    int finishes = 0;
    QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel,
                     [&]() { ++finishes; });
    click(*editor, editor->rect().center());
    ASSERT_TRUE(numeric.is_editing());
    ASSERT_TRUE(spin->hasFocus());
    type_text(*editor, spin->locale().toString(2.5, 'f', 1));
    runtime.expect_position_x(2.5);
    EXPECT_EQ(runtime.session.history().undo_count(), undo_before);

    if (pointer_collapse) {
      click(*header, QPoint(header->width() - 3, header->height() / 2));
    } else {
      section->setExpanded(false);
    }
    settle_inspector_layout(panel);
    EXPECT_FALSE(section->isExpanded());
    EXPECT_FALSE(header->isChecked());
    EXPECT_FALSE(numeric.is_editing());
    EXPECT_FALSE(spin->isVisible());
    EXPECT_EQ(position_x(panel), spin);
    EXPECT_EQ(finishes, 1);
    EXPECT_EQ(runtime.session.history().undo_count(), undo_before + 1u);
    EXPECT_EQ(runtime.session.scene().selected_entities(), selection_before);
    EXPECT_EQ(tree->currentItem(), section);
    EXPECT_TRUE(header->hasFocus());
    runtime.expect_position_x(2.5);

    section->setExpanded(true);
    settle_inspector_layout(panel);
    EXPECT_EQ(position_x(panel), spin);
    EXPECT_EQ(finishes, 1);
    EXPECT_EQ(runtime.session.history().undo_count(), undo_before + 1u);
    ASSERT_TRUE(runtime.editor.history().undo().ok());
    runtime.expect_position_x(0.0);
    EXPECT_EQ(runtime.session.scene().selected_entities(), selection_before);
    ASSERT_TRUE(runtime.editor.history().redo().ok());
    runtime.expect_position_x(2.5);
    panel.abandon_runtime();
  }
}

TEST_F(QtInspectorNumericDragTests,
       CollapseDraftCommitCanRebindTheRuntimeWithoutUsingOutgoingControls) {
  InspectorTransformRuntime outgoing;
  InspectorTransformRuntime incoming;
  outgoing.initialize();
  incoming.initialize();
  InspectorPanel panel;
  show(panel, outgoing);
  application().setActiveWindow(&panel);
  auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);
  auto *section = transform_section(*tree);
  auto *spin = position_x(panel);
  ASSERT_NE(section, nullptr);
  ASSERT_NE(spin, nullptr);
  auto *editor = spin->findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  click(*editor, editor->rect().center());
  ASSERT_TRUE(qt_install_numeric_spin_box(*spin).is_editing());
  type_text(*editor, spin->locale().toString(2.5, 'f', 1));
  const auto incoming_revision = incoming.session.scene().revision();
  const auto incoming_undo = incoming.session.history().undo_count();
  const auto outgoing_undo = outgoing.session.history().undo_count();
  bool rebound = false;
  QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel, [&]() {
    if (!rebound) {
      rebound = true;
      panel.abandon_runtime();
      panel.refresh(incoming.context);
    }
  });

  section->setExpanded(false);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  settle_inspector_layout(panel);
  EXPECT_TRUE(rebound);
  EXPECT_EQ(outgoing.session.history().undo_count(), outgoing_undo + 1u);
  EXPECT_EQ(incoming.session.scene().revision(), incoming_revision);
  EXPECT_EQ(incoming.session.history().undo_count(), incoming_undo);
  outgoing.expect_position_x(2.5);
  incoming.expect_position_x(0.0);
  auto *incoming_section = transform_section(*tree);
  auto *incoming_header = panel.findChild<QToolButton *>(
      QStringLiteral("inspector.transform.header"));
  auto *incoming_spin = position_x(panel);
  ASSERT_NE(incoming_section, nullptr);
  ASSERT_NE(incoming_header, nullptr);
  ASSERT_NE(incoming_spin, nullptr);
  EXPECT_EQ(incoming_header->isChecked(), incoming_section->isExpanded());
  incoming_header->click();
  EXPECT_EQ(incoming_header->isChecked(), incoming_section->isExpanded());
  incoming_section->setExpanded(true);
  incoming_spin->setValue(4.0);
  ASSERT_TRUE(QMetaObject::invokeMethod(incoming_spin, "editingFinished",
                                      Qt::DirectConnection));
  incoming.expect_position_x(4.0);
  EXPECT_EQ(incoming.session.history().undo_count(), incoming_undo + 1u);
  outgoing.expect_position_x(2.5);
  panel.abandon_runtime();
}

TEST_F(QtInspectorNumericDragTests,
       MixedAxesAndReadOnlySummariesStayDistinctFromEditableFields) {
  application().setPalette(
      qt_obsidian_application_palette(application().palette()));
  InspectorTransformRuntime runtime;
  runtime.initialize();
  const auto second = runtime.editor.scene().create_empty();
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(runtime.editor.scene()
                  .set_transform(second.value().id,
                                 {.position = {2.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(runtime.editor.scene()
                  .select({runtime.entity, second.value().id}, runtime.entity)
                  .ok());
  InspectorPanel panel;
  show(panel, runtime);
  panel.resize(320, 760);
  settle_inspector_layout(panel);
  auto *mixed = panel.findChild<QLineEdit *>(
      QStringLiteral("inspector.transform.position.x"));
  auto *editable = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("inspector.transform.position.y"));
  auto *summary = panel.findChild<QLabel *>(
      QStringLiteral("inspector.transform.selection"));
  ASSERT_NE(mixed, nullptr);
  ASSERT_NE(editable, nullptr);
  ASSERT_NE(summary, nullptr);
  EXPECT_TRUE(mixed->isReadOnly());
  EXPECT_FALSE(mixed->isEnabled());
  EXPECT_EQ(mixed->text(), QStringLiteral("Mixed"));
  EXPECT_TRUE(editable->isEnabled());
  EXPECT_FALSE(editable->isReadOnly());
  EXPECT_TRUE(editable->hasFrame());
  EXPECT_EQ(summary->frameShape(), QFrame::NoFrame);
  EXPECT_EQ(summary->text(), QStringLiteral("2 spatial entities"));
  const QRect mixed_rect = relative_geometry(*mixed, panel);
  const QRect editable_rect = relative_geometry(*editable, panel);
  EXPECT_LT(mixed_rect.bottom(), editable_rect.top());
  EXPECT_EQ(mixed_rect.left(), editable_rect.left());
  EXPECT_EQ(mixed_rect.right(), editable_rect.right());
  export_inspector_image(panel, QStringLiteral("inspector-mixed-320-100.png"));
  panel.abandon_runtime();
}

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
