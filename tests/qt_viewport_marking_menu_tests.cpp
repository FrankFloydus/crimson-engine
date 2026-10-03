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
#include "ui/qt_application_theme.hpp"
#include "ui/qt_viewport_marking_menu.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "viewport/viewport_input_router.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPixmap>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>

namespace {

namespace command = quader::quader_command;

constexpr quader::QuaderInputChord kInitiatingChord{
    .device = quader::QuaderInputDevice::Keyboard,
    .key = quader::QuaderKeyCode::Space,
};

quader::LocalizationService loaded_localization() {
  quader::LocalizationService localization;
  EXPECT_TRUE(localization.load(std::filesystem::path(QUADER_SOURCE_DIR) /
                                   "resources" / "locales",
                               "en-US"));
  return localization;
}

void prepare_owner(QWidget &owner) {
  owner.setFocusPolicy(Qt::StrongFocus);
  owner.resize(900, 700);
  owner.show();
  owner.activateWindow();
  owner.setFocus(Qt::ShortcutFocusReason);
  QCoreApplication::processEvents();
}

bool show_and_interact(QWidget &owner, quader::QuaderEditorApi &editor,
                       quader::ViewportInputRouter &input_router,
                       quader::LocalizationService &localization,
                       const std::function<void(QWidget &)> &interact,
                       quader::ViewportRuntimeBindingToken binding = {},
                       const quader::QtUiScaleMetrics &metrics =
                           quader::QtUiScaleMetrics{},
                       const quader::QuaderInputChord &initiating_chord =
                           kInitiatingChord,
                       std::optional<std::uint64_t> initiating_key_identity =
                           std::nullopt) {
  quader::QtViewportMarkingMenuPresenter presenter;
  QTimer interaction;
  interaction.setSingleShot(true);
  QObject::connect(&interaction, &QTimer::timeout, &interaction, [&]() {
    QWidget *popup = QApplication::activePopupWidget();
    if (popup == nullptr ||
        popup->objectName() != QStringLiteral("viewport_marking_menu")) {
      ADD_FAILURE() << "Expected the native viewport marking popup.";
      if (popup != nullptr) {
        popup->close();
      }
      return;
    }
    interact(*popup);
  });
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, &watchdog, []() {
    ADD_FAILURE() << "The marking menu did not finish after the test input.";
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  interaction.start(0);
  watchdog.start(3000);
  const bool result = presenter.show(
      owner, owner.mapToGlobal(owner.rect().center()),
      editor.viewports().focused_pane(), initiating_chord, editor,
      input_router, localization, metrics, binding, initiating_key_identity);
  interaction.stop();
  watchdog.stop();
  return result;
}

void choose_bake_location(QWidget &popup) {
  auto *button = popup.findChild<QToolButton *>(
      QStringLiteral("markingMenu.slot.2"));
  if (button == nullptr) {
    ADD_FAILURE() << "Expected the east Bake Location marking slot.";
    popup.close();
    return;
  }
  ASSERT_TRUE(button->isEnabled());
  const QPoint point = button->mapTo(&popup, button->rect().center());
  const QPoint global_point = popup.mapToGlobal(point);
  QMouseEvent hover(QEvent::MouseMove, QPointF(point), QPointF(global_point),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&popup, &hover);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(&popup, &release);
}

} // namespace

TEST(QtViewportMarkingMenuTests,
     NativePhysicalOpeningKeySurvivesLayoutAndPopupFocusChanges) {
#ifndef Q_OS_WIN
  GTEST_SKIP() << "Windows native key metadata supplies physical key identity.";
#else
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-marking-menu-physical-opening-key-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  auto localization = loaded_localization();
  QWidget owner;
  prepare_owner(owner);
  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Physical Marking Release");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.scene().set_transform(
      box.value().entity, {.position = {3.0F, 4.0F, 5.0F}}).ok());
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Q, Qt::ShiftModifier,
                  0x10U, 0x51U, 0U, QStringLiteral("Q"));
  QCoreApplication::sendEvent(&surface, &press);
  const auto input = surface.take_input_frame(0.0F, true);
  ASSERT_EQ(input.input.pressed_input_chords.size(), 1U);
  const auto initiating_chord = input.input.pressed_input_chords.front();
  const auto initiating_key_identity =
      surface.held_keyboard_key_identity(initiating_chord);
  ASSERT_TRUE(initiating_key_identity.has_value());
  EXPECT_TRUE(show_and_interact(owner, editor, input_router, localization,
                               [&](QWidget &popup) {
    QFocusEvent focus_loss(QEvent::FocusOut);
    QCoreApplication::sendEvent(&surface, &focus_loss);
    EXPECT_FALSE(surface.held_keyboard_key_identity(initiating_chord).has_value());
    auto *button = popup.findChild<QToolButton *>(
        QStringLiteral("markingMenu.slot.2"));
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(button->isEnabled());
    const QPoint point = button->mapTo(&popup, button->rect().center());
    const QPoint global_point = popup.mapToGlobal(point);
    QMouseEvent hover(QEvent::MouseMove, QPointF(point), QPointF(global_point),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&popup, &hover);
    QKeyEvent unrelated_release(QEvent::KeyRelease, Qt::Key_Q, Qt::NoModifier,
                                0x20U, 0x51U, 0U, QStringLiteral("q"));
    QCoreApplication::sendEvent(&popup, &unrelated_release);
    EXPECT_TRUE(popup.isVisible())
        << "An unrelated scan must not close the menu even for the same chord.";
    QKeyEvent changed_layout_release(QEvent::KeyRelease, Qt::Key_A,
                                     Qt::NoModifier, 0x10U, 0x41U, 0U,
                                     QStringLiteral("a"));
    QCoreApplication::sendEvent(&popup, &changed_layout_release);
  }, {}, quader::QtUiScaleMetrics{}, initiating_chord, initiating_key_identity));
  const auto entity = editor.query().entity(box.value().entity);
  ASSERT_TRUE(entity.has_value());
  EXPECT_FLOAT_EQ(entity->transform.position.x, 0.0F);
  EXPECT_FLOAT_EQ(entity->transform.position.y, 0.0F);
  EXPECT_FLOAT_EQ(entity->transform.position.z, 0.0F);
#endif
}

TEST(QtViewportMarkingMenuTests,
     NativeNationalGlyphReleaseCompletesTheCanonicalCustomOpeningChord) {
#ifndef Q_OS_WIN
  GTEST_SKIP() << "Windows native key metadata supplies layout-independent keys.";
#else
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-national-marking-menu-release-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  auto localization = loaded_localization();
  QWidget owner;
  prepare_owner(owner);
  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "National Marking Release");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  /// Supplies native national glyphs and the stable canonical opening key.
  struct ReleaseCase {
    int qt_key;
    Qt::KeyboardModifiers modifiers;
    std::uint32_t scan_code;
    std::uint32_t virtual_key;
    quader::QuaderKeyCode canonical_key;
    QString text;
  };
  const std::array cases{
      ReleaseCase{Qt::Key_sterling, Qt::ShiftModifier, 0x04U, 0x33U,
                  quader::QuaderKeyCode::Digit3, QString(QChar(0x00a3))},
      ReleaseCase{Qt::Key_QuoteDbl, Qt::NoModifier, 0x04U, 0x33U,
                  quader::QuaderKeyCode::Digit3, QStringLiteral("\"")},
      ReleaseCase{Qt::Key_3, Qt::NoModifier, 0x04U, 0x33U,
                  quader::QuaderKeyCode::Digit3, QStringLiteral("3")},
      ReleaseCase{Qt::Key_Question, Qt::ShiftModifier, 0x0cU, 0xbdU,
                  quader::QuaderKeyCode::Minus, QStringLiteral("?")},
  };
  for (const auto &case_data : cases) {
    SCOPED_TRACE(case_data.qt_key);
    ASSERT_TRUE(editor.scene().set_transform(
        box.value().entity, {.position = {3.0F, 4.0F, 5.0F}}).ok());
    ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
    const quader::QuaderInputChord initiating_chord{
        .shift = true,
        .device = quader::QuaderInputDevice::Keyboard,
        .key = case_data.canonical_key};
    EXPECT_TRUE(show_and_interact(owner, editor, input_router, localization,
                                 [&](QWidget &popup) {
      auto *button = popup.findChild<QToolButton *>(
          QStringLiteral("markingMenu.slot.2"));
      ASSERT_NE(button, nullptr);
      ASSERT_TRUE(button->isEnabled());
      const QPoint point = button->mapTo(&popup, button->rect().center());
      const QPoint global_point = popup.mapToGlobal(point);
      QMouseEvent hover(QEvent::MouseMove, QPointF(point), QPointF(global_point),
                        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(&popup, &hover);
      QKeyEvent release(QEvent::KeyRelease, case_data.qt_key,
                        case_data.modifiers, case_data.scan_code,
                        case_data.virtual_key, 0U, case_data.text);
      QCoreApplication::sendEvent(&popup, &release);
    }, {}, quader::QtUiScaleMetrics{}, initiating_chord));
    const auto entity = editor.query().entity(box.value().entity);
    ASSERT_TRUE(entity.has_value());
    EXPECT_FLOAT_EQ(entity->transform.position.x, 0.0F);
    EXPECT_FLOAT_EQ(entity->transform.position.y, 0.0F);
    EXPECT_FLOAT_EQ(entity->transform.position.z, 0.0F);
  }
#endif
}

TEST(QtViewportMarkingMenuTests, FixedDirectionsShareRelevantContextCommands) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_marking_menu_catalog_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStringLiteral("Fusion"));
  quader::apply_qt_obsidian_application_theme(application.palette());
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  auto localization = loaded_localization();
  QWidget owner;
  prepare_owner(owner);

  struct ModeCase final {
    quader::QuaderSelectionMode mode;
    qsizetype command_count;
    quader::QuaderCommandId representative;
  };
  const std::array cases{
      ModeCase{quader::QuaderSelectionMode::Vertex, 3,
               command::kModelingBevelVertices},
      ModeCase{quader::QuaderSelectionMode::Edge, 7,
               command::kModelingBevelEdges},
      ModeCase{quader::QuaderSelectionMode::Face, 7,
               command::kModelingExtractFaces},
      ModeCase{quader::QuaderSelectionMode::Object, 4,
               command::kModelingBakeLocation},
      ModeCase{quader::QuaderSelectionMode::Group, 5,
               command::kUngroupSelection},
  };
  for (const ModeCase &mode_case : cases) {
    SCOPED_TRACE(static_cast<int>(mode_case.mode));
    ASSERT_TRUE(editor.selection().set_mode(mode_case.mode).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    bool opened = false;
    EXPECT_FALSE(show_and_interact(owner, editor, input_router, localization,
                                  [&](QWidget &popup) {
      opened = true;
      const auto actions = popup.findChildren<QAction *>();
      EXPECT_EQ(actions.size(), mode_case.command_count);
      const QAction *unavailable = popup.findChild<QAction *>(QString::fromUtf8(
          mode_case.representative.string().data()));
      ASSERT_NE(unavailable, nullptr);
      EXPECT_FALSE(unavailable->isEnabled());
      EXPECT_EQ(popup.findChild<QAction *>(QStringLiteral("edit.delete")), nullptr);
      for (const QAction *action : actions) {
        const auto descriptor =
            editor.commands().descriptor(action->objectName().toStdString());
        ASSERT_TRUE(descriptor.has_value()) << action->objectName().toStdString();
        EXPECT_EQ(action->isEnabled(), descriptor->enabled)
            << action->objectName().toStdString();
        EXPECT_FALSE(action->text().isEmpty());
        EXPECT_TRUE(action->shortcuts().isEmpty());
      }
      popup.close();
    }));
    EXPECT_TRUE(opened);
  }

  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Marking Availability");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  const QFont baseline_font = application.font();
  for (const float scale : {1.0F, 2.0F}) {
    const quader::QtUiScaleMetrics metrics(scale);
    application.setFont(metrics.font(baseline_font));
    EXPECT_FALSE(show_and_interact(owner, editor, input_router, localization,
                                  [&](QWidget &popup) {
      QAction *extract = popup.findChild<QAction *>(
          QStringLiteral("modeling.extract_faces"));
      QAction *bridge = popup.findChild<QAction *>(
          QStringLiteral("modeling.interpolated_bridge_faces"));
      ASSERT_NE(extract, nullptr);
      ASSERT_NE(bridge, nullptr);
      EXPECT_TRUE(extract->isEnabled());
      EXPECT_FALSE(bridge->isEnabled());
      QAction *remove = popup.findChild<QAction *>(QStringLiteral("edit.delete"));
      ASSERT_NE(remove, nullptr);
      EXPECT_EQ(remove->text(), QStringLiteral("Delete Faces"));
      EXPECT_TRUE(remove->isEnabled());
      auto *lower_menu = popup.findChild<QListWidget *>(
          QStringLiteral("markingMenu.overflow"));
      ASSERT_NE(lower_menu, nullptr);
      EXPECT_EQ(lower_menu->findItems(remove->text(), Qt::MatchExactly).size(), 1);
      EXPECT_EQ(lower_menu->findItems(extract->text(), Qt::MatchExactly).size(), 0);
      EXPECT_EQ(popup.findChild<QAction *>(
                    QStringLiteral("modeling.bake_transforms")), nullptr);
      EXPECT_EQ(popup.findChild<QAction *>(QStringLiteral("edit.copy")), nullptr);
      std::set<QString> command_ids;
      int separators = 0;
      for (const QAction *action : popup.findChildren<QAction *>()) {
        if (action->isSeparator()) {
          ++separators;
          continue;
        }
        EXPECT_TRUE(command_ids.insert(action->objectName()).second);
        EXPECT_FALSE(action->objectName().startsWith(QStringLiteral("tools.")));
      }
      EXPECT_GT(separators, 0);
      for (const QToolButton *button : popup.findChildren<QToolButton *>()) {
        EXPECT_NE(button->accessibleName(), remove->text());
      }
      const QImage capture = popup.grab().toImage();
      ASSERT_FALSE(capture.isNull());
      EXPECT_TRUE(capture.hasAlphaChannel());
      EXPECT_EQ(capture.pixelColor(0, 0).alpha(), 0);
      auto *extract_button = popup.findChild<QToolButton *>(
          QStringLiteral("markingMenu.slot.3"));
      ASSERT_NE(extract_button, nullptr);
      const QPoint sample = extract_button->mapTo(
          &popup, QPoint(metrics.px(6), extract_button->height() / 2));
      EXPECT_GT(capture.pixelColor(
          static_cast<int>(sample.x() * capture.devicePixelRatio()),
          static_cast<int>(sample.y() * capture.devicePixelRatio())).alpha(), 0);
      if (!review_directory.isEmpty()) {
        EXPECT_TRUE(capture.save(QDir(review_directory).filePath(
            QStringLiteral("marking-face-%1.png").arg(scale))));
      }
      popup.close();
    }, {}, metrics));
  }
  application.setFont(baseline_font);

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.selection().select_vertices(box.value(), {1, 2}).ok());
  EXPECT_FALSE(show_and_interact(owner, editor, input_router, localization,
                                [&](QWidget &popup) {
    QAction *merge = popup.findChild<QAction *>(
        QStringLiteral("modeling.merge_vertices_to_active"));
    ASSERT_NE(merge, nullptr);
    EXPECT_EQ(popup.findChild<QAction *>(QStringLiteral("modeling.merge_vertices")),
              nullptr) << "RMB compatibility IDs must not duplicate a radial operation.";
    QAction *remove = popup.findChild<QAction *>(QStringLiteral("edit.delete"));
    ASSERT_NE(remove, nullptr);
    EXPECT_EQ(remove->text(), QStringLiteral("Delete Vertices"));
    auto *south = popup.findChild<QToolButton *>(QStringLiteral("markingMenu.slot.4"));
    ASSERT_NE(south, nullptr);
    EXPECT_EQ(south->accessibleName(), merge->text());
    EXPECT_EQ(popup.findChild<QAction *>(QStringLiteral("modeling.extract_faces")),
              nullptr);
    popup.close();
  }));
}

TEST(QtViewportMarkingMenuTests, ReleaseDispatchesBakeLocationAndUndoRestoresIt) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_marking_menu_dispatch_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  auto localization = loaded_localization();
  QWidget owner;
  prepare_owner(owner);
  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Marking Bake");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor.scene().set_transform(
      box.value().entity, {.position = {3.0F, 4.0F, 5.0F}}).ok());
  EXPECT_TRUE(show_and_interact(owner, editor, input_router, localization,
                               choose_bake_location));
  const auto baked = editor.query().entity(box.value().entity);
  ASSERT_TRUE(baked.has_value());
  EXPECT_EQ(baked->transform.position.x, 0.0F);
  EXPECT_EQ(baked->transform.position.y, 0.0F);
  EXPECT_EQ(baked->transform.position.z, 0.0F);
  ASSERT_TRUE(editor.commands().execute(command::kEditUndo).ok());
  const auto restored = editor.query().entity(box.value().entity);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->transform.position.x, 3.0F);
  EXPECT_EQ(restored->transform.position.y, 4.0F);
  EXPECT_EQ(restored->transform.position.z, 5.0F);
}

TEST(QtViewportMarkingMenuTests, MixedInstancesAndLightsExposeOneAlignToolLauncher) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_marking_menu_align_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  auto localization = loaded_localization();
  QWidget owner;
  prepare_owner(owner);
  const auto source = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Q Align Instance");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  const auto instance = editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const auto light = editor.scene().create_point_light({.name = "Q Align Light"});
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(editor.selection().select_objects(
      {instance.value().occurrence.root.id, light.value().id}).ok());
  bool presented_align = false;
  EXPECT_FALSE(show_and_interact(owner, editor, input_router, localization,
                                [&](QWidget &popup) {
    const auto align_actions = popup.findChildren<QAction *>(
        QStringLiteral("tools.activate.align"));
    ASSERT_EQ(align_actions.size(), 1);
    EXPECT_EQ(align_actions.front()->text(), QStringLiteral("Align Tool"));
    EXPECT_TRUE(align_actions.front()->isEnabled());
    EXPECT_FALSE(align_actions.front()->icon().isNull());
    auto *overflow = popup.findChild<QListWidget *>(
        QStringLiteral("markingMenu.overflow"));
    ASSERT_NE(overflow, nullptr);
    const auto align_items = overflow->findItems(
        QStringLiteral("Align Tool"), Qt::MatchExactly);
    ASSERT_EQ(align_items.size(), 1);
    EXPECT_FALSE(align_items.front()->icon().isNull());
    for (const auto *action : popup.findChildren<QAction *>()) {
      EXPECT_FALSE(action->objectName().startsWith(QStringLiteral("modeling.align.")));
      EXPECT_FALSE(action->objectName().startsWith(QStringLiteral("modeling.distribute.")));
    }
    presented_align = true;
    popup.close();
  }));
  EXPECT_TRUE(presented_align);
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  EXPECT_FALSE(show_and_interact(owner, editor, input_router, localization,
                                [&](QWidget &popup) {
    EXPECT_EQ(popup.findChild<QAction *>(QStringLiteral("tools.activate.align")), nullptr);
    popup.close();
  }));
}

TEST(QtViewportMarkingMenuTests, ChangedSelectionPaneAndRuntimeRejectStaleIntent) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_marking_menu_fences_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  ASSERT_TRUE(session.viewport_workspace().set_layout(quader::ViewportLayoutId::FourQuad));
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  auto localization = loaded_localization();
  QWidget owner;
  prepare_owner(owner);
  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Marking Fences");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.scene().set_transform(
      box.value().entity, {.position = {3.0F, 4.0F, 5.0F}}).ok());
  std::uint64_t runtime_generation = 9;
  for (int changed_owner = 0; changed_owner < 3; ++changed_owner) {
    SCOPED_TRACE(changed_owner);
    (void)session.viewport_workspace().focus_pane(quader::ViewportPaneId::Top);
    ASSERT_EQ(editor.viewports().focused_pane(), quader::ViewportPaneId::Top);
    ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
    const quader::ViewportRuntimeBindingToken binding{
        .current_generation = &runtime_generation,
        .expected_generation = runtime_generation,
    };
    const auto command_revision = editor.commands().last_status_revision();
    EXPECT_FALSE(show_and_interact(owner, editor, input_router, localization,
                                  [&](QWidget &popup) {
      if (changed_owner == 0) {
        EXPECT_TRUE(editor.selection().clear().ok());
      } else if (changed_owner == 1) {
        EXPECT_TRUE(session.viewport_workspace().focus_pane(
            quader::ViewportPaneId::Perspective));
      } else {
        ++runtime_generation;
      }
      choose_bake_location(popup);
    }, binding));
    EXPECT_EQ(editor.commands().last_status_revision(), command_revision);
    const auto retained = editor.query().entity(box.value().entity);
    ASSERT_TRUE(retained.has_value());
    EXPECT_EQ(retained->transform.position.x, 3.0F);
    EXPECT_EQ(retained->transform.position.y, 4.0F);
    EXPECT_EQ(retained->transform.position.z, 5.0F);
  }
}
