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
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_viewport_context_menu.hpp"
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
#include <QMenu>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QScreen>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <utility>
#include <vector>

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

QMenu *open_marking_submenu(QWidget &popup, const QString &object_name) {
  auto *source = popup.findChild<QMenu *>(object_name);
  auto *overflow = popup.findChild<QListWidget *>(
      QStringLiteral("markingMenu.overflow"));
  if (source == nullptr || overflow == nullptr) {
    ADD_FAILURE() << "Expected the fixed submenu " << object_name.toStdString();
    popup.close();
    return nullptr;
  }
  const auto items = overflow->findItems(source->title(), Qt::MatchExactly);
  if (items.size() != 1) {
    ADD_FAILURE() << "Expected exactly one fixed row for "
                  << source->title().toStdString();
    popup.close();
    return nullptr;
  }
  overflow->scrollToItem(items.front());
  const QPoint global_point = overflow->viewport()->mapToGlobal(
      overflow->visualItemRect(items.front()).center());
  QMouseEvent hover(QEvent::MouseMove,
                    QPointF(popup.mapFromGlobal(global_point)), QPointF(global_point),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&popup, &hover);
  for (auto *submenu : popup.findChildren<QMenu *>(object_name)) {
    if (submenu->isVisible()) {
      return submenu;
    }
  }
  ADD_FAILURE() << "Expected the native submenu to open on its fixed row.";
  popup.close();
  return nullptr;
}

QMenu *hover_bake_location(QWidget &popup) {
  auto *submenu = open_marking_submenu(
      popup, QStringLiteral("viewport_marking_submenu_bake"));
  if (submenu == nullptr) {
    return nullptr;
  }
  auto *action = submenu->findChild<QAction *>(
      QStringLiteral("modeling.bake_location"));
  if (action == nullptr || !action->isEnabled()) {
    ADD_FAILURE() << "Expected the enabled Bake Position submenu command.";
    popup.close();
    return nullptr;
  }
  const QPoint point = submenu->actionGeometry(action).center();
  QMouseEvent hover(QEvent::MouseMove, QPointF(point),
                    QPointF(submenu->mapToGlobal(point)), Qt::NoButton,
                    Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(submenu, &hover);
  return submenu;
}

void choose_bake_location(QWidget &popup) {
  auto *submenu = hover_bake_location(popup);
  if (submenu == nullptr) {
    return;
  }
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(submenu, &release);
}

void capture_marking_widget(QWidget &widget, const QString &filename) {
  const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (directory.isEmpty()) {
    return;
  }
  ASSERT_TRUE(QDir().mkpath(directory));
  const QImage capture = widget.grab().toImage();
  ASSERT_FALSE(capture.isNull());
  EXPECT_TRUE(capture.save(QDir(directory).filePath(filename)));
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
    auto *submenu = hover_bake_location(popup);
    ASSERT_NE(submenu, nullptr);
    QKeyEvent unrelated_release(QEvent::KeyRelease, Qt::Key_Q, Qt::NoModifier,
                                0x20U, 0x51U, 0U, QStringLiteral("q"));
    QCoreApplication::sendEvent(submenu, &unrelated_release);
    EXPECT_TRUE(popup.isVisible())
        << "An unrelated scan must not close the menu even for the same chord.";
    QKeyEvent changed_layout_release(QEvent::KeyRelease, Qt::Key_A,
                                     Qt::NoModifier, 0x10U, 0x41U, 0U,
                                     QStringLiteral("a"));
    QCoreApplication::sendEvent(submenu, &changed_layout_release);
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
      auto *submenu = hover_bake_location(popup);
      ASSERT_NE(submenu, nullptr);
      QKeyEvent release(QEvent::KeyRelease, case_data.qt_key,
                        case_data.modifiers, case_data.scan_code,
                        case_data.virtual_key, 0U, case_data.text);
      QCoreApplication::sendEvent(submenu, &release);
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

  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Marking Availability");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());

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
      ModeCase{quader::QuaderSelectionMode::Object, 3,
               command::kModelingInvertMesh},
      ModeCase{quader::QuaderSelectionMode::Group, 5,
               command::kUngroupSelection},
  };
  for (const ModeCase &mode_case : cases) {
    SCOPED_TRACE(static_cast<int>(mode_case.mode));
    const bool object_mode = mode_case.mode == quader::QuaderSelectionMode::Object ||
                             mode_case.mode == quader::QuaderSelectionMode::Group;
    ASSERT_TRUE(editor.mode().set(object_mode ? quader::QuaderEditorMode::Object
                                              : quader::QuaderEditorMode::Edit).ok());
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

  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
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

TEST(QtViewportMarkingMenuTests,
     MeshDirectionsAndFixedSubmenusKeepTheRequestedCommandsAndNativeLayout) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-mesh-marking-menu-layout-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStringLiteral("Fusion"));
  const QPalette fusion_palette = application.style()->standardPalette();
  quader::apply_qt_obsidian_application_theme(fusion_palette);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  auto localization = loaded_localization();
  QWidget owner;
  prepare_owner(owner);
  const auto first = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}}, "Mesh Menu First");
  const auto second = editor.modeling().create_box(
      {.min = {3.0F, -1.0F, -1.0F}, .max = {5.0F, 1.0F, 1.0F}}, "Mesh Menu Second");
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_objects({first.value(), second.value()}).ok());

  /// Declares the fixed menu's observable child order and localized English labels.
  struct ExpectedSubmenu final {
    QString suffix;
    QString title;
    std::vector<std::pair<quader::QuaderCommandId, QString>> children;
  };
  const std::array submenus{
      ExpectedSubmenu{QStringLiteral("mirror"), QStringLiteral("Mirror"),
          {{command::kModelingMirrorSelectionX, QStringLiteral("Mirror X")},
           {command::kModelingMirrorSelectionY, QStringLiteral("Mirror Y")},
           {command::kModelingMirrorSelectionZ, QStringLiteral("Mirror Z")}}},
      ExpectedSubmenu{QStringLiteral("flip"), QStringLiteral("Flip"),
          {{command::kModelingFlipHorizontal, QStringLiteral("Flip Horizontally")},
           {command::kModelingFlipVertical, QStringLiteral("Flip Vertically")}}},
      ExpectedSubmenu{QStringLiteral("bake"), QStringLiteral("Bake"),
          {{command::kModelingBakeLocation, QStringLiteral("Bake Position")},
           {command::kModelingBakeRotation, QStringLiteral("Bake Rotation")},
           {command::kModelingBakeScale, QStringLiteral("Bake Scale")},
           {command::kModelingBakeTransforms, QStringLiteral("Bake All")}}}};
  /// Exercises constrained launch geometry, scale restoration and native fallback.
  struct ViewCase final {
    float factor;
    int owner_width;
    bool rtl;
    bool high_contrast;
    QString name;
  };
  const std::array views{
      ViewCase{1.0F, 900, false, false, QStringLiteral("100")},
      ViewCase{2.0F, 320, false, false, QStringLiteral("200-narrow")},
      ViewCase{1.0F, 320, false, false, QStringLiteral("100-scale-down")},
      ViewCase{1.0F, 320, true, false, QStringLiteral("rtl")},
      ViewCase{1.0F, 320, false, true, QStringLiteral("high-contrast")}};
  const QFont baseline_font = application.font();
  for (const auto &view : views) {
    SCOPED_TRACE(view.name.toStdString());
    const quader::QtUiScaleMetrics metrics(view.factor);
    application.setFont(metrics.font(baseline_font));
    application.setLayoutDirection(view.rtl ? Qt::RightToLeft : Qt::LeftToRight);
    quader::apply_qt_application_theme_for_contrast_preference(
        fusion_palette, view.high_contrast ? Qt::ContrastPreference::HighContrast
                                          : Qt::ContrastPreference::NoPreference);
    owner.resize(view.owner_width, 700);
    EXPECT_FALSE(show_and_interact(owner, editor, input_router, localization,
                                  [&](QWidget &popup) {
      auto *overflow = popup.findChild<QListWidget *>(QStringLiteral("markingMenu.overflow"));
      ASSERT_NE(overflow, nullptr);
      const std::array<QString, 8> labels{
          QStringLiteral("Create Instance"), {}, QStringLiteral("Flip Normals"), {},
          QStringLiteral("Snap to Floor"), {}, {}, {}};
      for (int slot = 0; slot < 8; ++slot) {
        auto *button = popup.findChild<QToolButton *>(
            QStringLiteral("markingMenu.slot.%1").arg(slot));
        ASSERT_NE(button, nullptr);
        EXPECT_EQ(!button->isHidden(), !labels[slot].isEmpty());
        if (!labels[slot].isEmpty()) {
          EXPECT_EQ(button->accessibleName(), labels[slot]);
          EXPECT_TRUE(button->isEnabled());
          EXPECT_TRUE(overflow->findItems(labels[slot], Qt::MatchExactly).empty());
        }
      }
      for (const auto id : {command::kModelingHalfCut, command::kModelingDuplicateMeshes,
                            command::kCutSelection, command::kCopySelection,
                            command::kDeleteSelection, command::kGroupSelection,
                            command::kModelingRemoveDoubles,
                            command::kModelingRecalculateFaceNormals}) {
        EXPECT_EQ(popup.findChild<QAction *>(QString::fromUtf8(id.string().data())), nullptr)
            << id.string();
      }
      for (const auto id : {command::kDuplicateSelection, command::kModelingCombineMesh,
                            command::kModelingShadeSmoothMesh,
                            command::kModelingShadeFlatMesh, command::kToolsActivateAlign,
                            command::kModelingCopyPivotFromActive,
                            command::kModelingSetOriginToCenter,
                            command::kModelingSetOriginToPivot}) {
        EXPECT_NE(popup.findChild<QAction *>(QString::fromUtf8(id.string().data())), nullptr)
            << id.string();
      }
      // Closed boxes have no open perimeter for the existing Outer Hull operation.
      const auto outer_hull = editor.commands().descriptor(command::kModelingCreateOuterHull);
      ASSERT_TRUE(outer_hull.has_value());
      EXPECT_FALSE(outer_hull->enabled);
      EXPECT_EQ(popup.findChild<QAction *>(QStringLiteral("modeling.create_outer_hull")) != nullptr,
                outer_hull->enabled);
      for (const auto &expected : submenus) {
        const QString name = QStringLiteral("viewport_marking_submenu_") + expected.suffix;
        auto *source = popup.findChild<QMenu *>(name);
        ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->title(), expected.title);
        EXPECT_EQ(overflow->findItems(expected.title, Qt::MatchExactly).size(), 1);
        ASSERT_EQ(source->actions().size(), expected.children.size());
        for (std::size_t index = 0; index < expected.children.size(); ++index) {
          const auto &[id, label] = expected.children[index];
          const auto *action = source->actions()[static_cast<qsizetype>(index)];
          EXPECT_EQ(action->objectName(), QString::fromUtf8(id.string().data()));
          EXPECT_EQ(action->text(), label);
          EXPECT_TRUE(action->shortcuts().isEmpty());
          EXPECT_TRUE(overflow->findItems(label, Qt::MatchExactly).empty());
        }
      }
      capture_marking_widget(popup, QStringLiteral("marking-mesh-%1.png").arg(view.name));
      for (const auto &expected : submenus) {
        auto *submenu = open_marking_submenu(
            popup, QStringLiteral("viewport_marking_submenu_") + expected.suffix);
        ASSERT_NE(submenu, nullptr);
        EXPECT_TRUE(popup.isVisible());
        EXPECT_EQ(submenu->layoutDirection(), application.layoutDirection());
        EXPECT_TRUE(submenu->styleSheet().isEmpty());
        EXPECT_TRUE(submenu->screen()->availableGeometry().contains(submenu->frameGeometry()));
        for (auto *action : submenu->actions()) {
          EXPECT_TRUE(submenu->rect().contains(submenu->actionGeometry(action)));
        }
        capture_marking_widget(*submenu,
            QStringLiteral("marking-mesh-%1-%2.png").arg(expected.suffix, view.name));
      }
      popup.close();
    }, {}, metrics));
  }
  application.setFont(baseline_font);
  application.setLayoutDirection(Qt::LeftToRight);
  quader::apply_qt_application_theme_for_contrast_preference(
      fusion_palette, Qt::ContrastPreference::NoPreference);
  ASSERT_TRUE(editor.selection().select_object(first.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(first.value(), 1).ok());
  const auto opened_mesh = editor.modeling().delete_selection();
  ASSERT_TRUE(opened_mesh.ok()) << opened_mesh.error_message();
  ASSERT_TRUE(opened_mesh.changed());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(first.value()).ok());
  const auto outer_hull = editor.commands().descriptor(command::kModelingCreateOuterHull);
  ASSERT_TRUE(outer_hull.has_value());
  ASSERT_TRUE(outer_hull->enabled);
  EXPECT_FALSE(show_and_interact(owner, editor, input_router, localization,
                                [&](QWidget &popup) {
    const auto *action = popup.findChild<QAction *>(QStringLiteral("modeling.create_outer_hull"));
    ASSERT_NE(action, nullptr);
    EXPECT_TRUE(action->isEnabled());
    EXPECT_EQ(action->text(), QStringLiteral("Create Outer Hull"));
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
  const auto command_revision = editor.commands().last_status_revision();
  int action_trigger_count = 0;
  EXPECT_TRUE(show_and_interact(owner, editor, input_router, localization,
                               [&](QWidget &popup) {
    auto *action = popup.findChild<QAction *>(QStringLiteral("modeling.bake_location"));
    ASSERT_NE(action, nullptr);
    QObject::connect(action, &QAction::triggered, &owner,
                     [&]() { ++action_trigger_count; });
    choose_bake_location(popup);
  }));
  EXPECT_EQ(action_trigger_count, 0);
  EXPECT_EQ(editor.commands().last_status_revision(), command_revision + 1U);
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
  ASSERT_TRUE(editor.history().redo().ok());
  const auto redone = editor.query().entity(box.value().entity);
  ASSERT_TRUE(redone.has_value());
  EXPECT_EQ(redone->transform.position.x, 0.0F);
  EXPECT_EQ(redone->transform.position.y, 0.0F);
  EXPECT_EQ(redone->transform.position.z, 0.0F);
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
    const auto context_commands = quader::qt_viewport_context_commands(
        editor, editor.selection().summary(), localization);
    const auto align_command = std::ranges::find(
        context_commands, quader::quader_command::kToolsActivateAlign.string(),
        &quader::QtViewportContextCommand::command_id);
    ASSERT_NE(align_command, context_commands.end());
    EXPECT_EQ(align_actions.front()->text(), align_command->label);
    EXPECT_TRUE(align_actions.front()->isEnabled());
    EXPECT_FALSE(align_actions.front()->icon().isNull());
    auto *overflow = popup.findChild<QListWidget *>(
        QStringLiteral("markingMenu.overflow"));
    ASSERT_NE(overflow, nullptr);
    const auto align_items = overflow->findItems(
        align_actions.front()->text(), Qt::MatchExactly);
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
  ASSERT_TRUE(editor.modeling().enter_instance_edit(instance.value().occurrence.root.id).ok());
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  EXPECT_FALSE(show_and_interact(owner, editor, input_router, localization,
                                [&](QWidget &popup) {
    EXPECT_EQ(popup.findChild<QAction *>(QStringLiteral("tools.activate.align")), nullptr);
    popup.close();
  }));
}

TEST(QtViewportMarkingMenuTests,
     OriginCommandsUseFixedRowsAndDispatchThroughTheirCommandOwners) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-marking-menu-origin-commands-test";
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
  const auto first = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "First Origin Mesh");
  const auto second = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Second Origin Mesh");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.scene().set_transform(
      first.value().entity, {.position = {-4.0F, 0.0F, 0.0F}}).ok());
  ASSERT_TRUE(editor.scene().set_transform(
      second.value().entity, {.position = {4.0F, 0.0F, 0.0F}}).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_objects({first.value(), second.value()}).ok());
  const auto original_pivot = editor.query().selection_transform_pivot();
  ASSERT_TRUE(original_pivot.has_value());
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  const QFont baseline_font = application.font();
  for (const float factor : {1.0F, 2.0F}) {
    SCOPED_TRACE(factor);
    const quader::QtUiScaleMetrics metrics(factor);
    application.setFont(metrics.font(baseline_font));
    for (const auto command_id : {command::kModelingSetOriginToPivot,
                                  command::kModelingSetOriginToCenter}) {
      SCOPED_TRACE(command_id.string());
      const bool set_center =
          command_id.string() == command::kModelingSetOriginToCenter.string();
      EXPECT_TRUE(show_and_interact(owner, editor, input_router, localization,
                                   [&](QWidget &popup) {
        auto *overflow = popup.findChild<QListWidget *>(
            QStringLiteral("markingMenu.overflow"));
        ASSERT_NE(overflow, nullptr);
        for (const auto &[id, label] : {
                 std::pair{command::kModelingSetOriginToCenter,
                           QStringLiteral("Set Origin to Center")},
                 std::pair{command::kModelingSetOriginToPivot,
                           QStringLiteral("Set Origin to Pivot")}}) {
          const auto actions = popup.findChildren<QAction *>(
              QString::fromUtf8(id.string().data(), id.string().size()));
          ASSERT_EQ(actions.size(), 1);
          EXPECT_EQ(actions.front()->text(), label);
          EXPECT_TRUE(actions.front()->isEnabled());
          EXPECT_TRUE(actions.front()->shortcuts().isEmpty());
          EXPECT_EQ(overflow->findItems(label, Qt::MatchExactly).size(), 1);
          for (const auto *button : popup.findChildren<QToolButton *>()) {
            EXPECT_NE(button->accessibleName(), label);
          }
        }
        const QString selected_label = set_center
            ? QStringLiteral("Set Origin to Center")
            : QStringLiteral("Set Origin to Pivot");
        const auto items = overflow->findItems(selected_label, Qt::MatchExactly);
        ASSERT_EQ(items.size(), 1);
        overflow->scrollToItem(items.front());
        const QPoint global_point = overflow->viewport()->mapToGlobal(
            overflow->visualItemRect(items.front()).center());
        QMouseEvent hover(QEvent::MouseMove,
                          QPointF(popup.mapFromGlobal(global_point)),
                          QPointF(global_point), Qt::NoButton, Qt::NoButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(&popup, &hover);
        if (!review_directory.isEmpty() && !set_center) {
          const QImage capture = popup.grab().toImage();
          ASSERT_FALSE(capture.isNull());
          EXPECT_TRUE(capture.save(QDir(review_directory).filePath(
              QStringLiteral("marking-origin-commands-%1.png")
                  .arg(static_cast<int>(factor * 100.0F)))));
        }
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
        QCoreApplication::sendEvent(&popup, &release);
      }, {}, metrics));
      const auto first_state = editor.query().entity(first.value().entity);
      const auto second_state = editor.query().entity(second.value().entity);
      ASSERT_TRUE(first_state.has_value());
      ASSERT_TRUE(second_state.has_value());
      const quader::QVec3 first_expected = set_center
          ? quader::QVec3{-4.0F, 0.0F, 0.0F} : *original_pivot;
      const quader::QVec3 second_expected = set_center
          ? quader::QVec3{4.0F, 0.0F, 0.0F} : *original_pivot;
      EXPECT_FLOAT_EQ(first_state->transform.position.x, first_expected.x);
      EXPECT_FLOAT_EQ(first_state->transform.position.y, first_expected.y);
      EXPECT_FLOAT_EQ(first_state->transform.position.z, first_expected.z);
      EXPECT_FLOAT_EQ(second_state->transform.position.x, second_expected.x);
      EXPECT_FLOAT_EQ(second_state->transform.position.y, second_expected.y);
      EXPECT_FLOAT_EQ(second_state->transform.position.z, second_expected.z);
    }
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(editor.history().undo().ok());
  }
  application.setFont(baseline_font);
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

TEST(QtViewportMarkingMenuTests, SceneSwitchWhileBakeSubmenuIsOpenRejectsItsLeaf) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-marking-submenu-scene-fence-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto project_root = std::filesystem::path(temporary.path().toStdWString()) / "Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  quader::ProjectSession project;
  ASSERT_TRUE(project.create(project_root).ok);
  quader::QuaderEditorApi editor(project);
  quader::ViewportInputRouter input_router;
  auto localization = loaded_localization();
  QWidget owner;
  prepare_owner(owner);
  const auto first_scene = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(first_scene.has_value());
  const auto first = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}}, "Original Scene Box");
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(editor.scene().set_transform(first.value().entity,
      {.position = {3.0F, 4.0F, 5.0F}}).ok());
  ASSERT_TRUE(editor.selection().select_object(first.value()).ok());
  const auto second_scene = editor.scene_documents().new_scene();
  ASSERT_TRUE(second_scene.ok());
  const auto second = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}}, "New Scene Box");
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(editor.scene().set_transform(second.value().entity,
      {.position = {6.0F, 7.0F, 8.0F}}).ok());
  ASSERT_TRUE(editor.selection().select_object(second.value()).ok());
  ASSERT_TRUE(editor.scene_documents().activate(*first_scene).ok());
  std::uint64_t command_revision_after_switch = 0;
  EXPECT_FALSE(show_and_interact(owner, editor, input_router, localization,
                                [&](QWidget &popup) {
    auto *submenu = hover_bake_location(popup);
    ASSERT_NE(submenu, nullptr);
    ASSERT_TRUE(editor.scene_documents().activate(second_scene.value()).ok());
    command_revision_after_switch = editor.commands().last_status_revision();
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(submenu, &release);
  }));
  EXPECT_EQ(editor.commands().last_status_revision(), command_revision_after_switch);
  EXPECT_EQ(editor.scene_documents().active_scene_id(), second_scene.value());
  const auto second_state = editor.query().entity(second.value().entity);
  ASSERT_TRUE(second_state.has_value());
  EXPECT_FLOAT_EQ(second_state->transform.position.x, 6.0F);
  EXPECT_FLOAT_EQ(second_state->transform.position.y, 7.0F);
  EXPECT_FLOAT_EQ(second_state->transform.position.z, 8.0F);
  ASSERT_TRUE(editor.scene_documents().activate(*first_scene).ok());
  const auto first_state = editor.query().entity(first.value().entity);
  ASSERT_TRUE(first_state.has_value());
  EXPECT_FLOAT_EQ(first_state->transform.position.x, 3.0F);
  EXPECT_FLOAT_EQ(first_state->transform.position.y, 4.0F);
  EXPECT_FLOAT_EQ(first_state->transform.position.z, 5.0F);
}
