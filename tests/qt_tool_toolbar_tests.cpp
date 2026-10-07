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
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QToolButton>

#include <array>
#include <filesystem>
#include <string>

namespace {

constexpr std::array<quader::ViewportToolId, 14> kModelingTools{
    quader::ViewportToolId::CreatePrimitive,
    quader::ViewportToolId::Pivot,
    quader::ViewportToolId::Align,
    quader::ViewportToolId::Extrude,
    quader::ViewportToolId::InsertEdgeLoop,
    quader::ViewportToolId::Knife,
    quader::ViewportToolId::Cut,
    quader::ViewportToolId::Mirror,
    quader::ViewportToolId::Poly,
    quader::ViewportToolId::LinearArray,
    quader::ViewportToolId::RadialArray,
    quader::ViewportToolId::Curve,
    quader::ViewportToolId::PathArray,
    quader::ViewportToolId::UvPeel};

constexpr std::array<qsizetype, 3> kSeparatorPositions{9, 12, 15};

} // namespace

TEST(QtToolToolbarTests, RetainsAllModelingLaunchersAcrossSelectionModes) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_tool_toolbar_membership_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::QtToolToolbar toolbar;
  EXPECT_EQ(toolbar.objectName(), QStringLiteral("toolToolbar"));
  ASSERT_EQ(toolbar.actions().size(),
            kModelingTools.size() + kSeparatorPositions.size());
  const auto mesh = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Toolbar component target");
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  for (const auto mode : {quader::QuaderSelectionMode::Vertex,
                          quader::QuaderSelectionMode::Edge,
                          quader::QuaderSelectionMode::Face,
                          quader::QuaderSelectionMode::Object,
                          quader::QuaderSelectionMode::Group}) {
    SCOPED_TRACE(static_cast<int>(mode));
    const bool object_mode = mode == quader::QuaderSelectionMode::Object ||
                             mode == quader::QuaderSelectionMode::Group;
    ASSERT_TRUE(editor.mode().set(object_mode ? quader::QuaderEditorMode::Object
                                              : quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    toolbar.synchronize(editor, editor.selection().summary());
    const auto actions = toolbar.actions();
    std::size_t tool_index = 0;
    std::size_t separator_index = 0;
    for (qsizetype index = 0; index < actions.size(); ++index) {
      if (separator_index < kSeparatorPositions.size() &&
          index == kSeparatorPositions[separator_index]) {
        EXPECT_TRUE(actions[index]->isSeparator());
        ++separator_index;
        continue;
      }
      ASSERT_LT(tool_index, kModelingTools.size());
      QAction *action = toolbar.tool_action(kModelingTools[tool_index++]);
      ASSERT_NE(action, nullptr);
      EXPECT_EQ(actions[index], action);
      EXPECT_FALSE(action->isSeparator());
      EXPECT_TRUE(action->isVisible());
      EXPECT_TRUE(action->isCheckable());
      EXPECT_TRUE(action->shortcuts().isEmpty());
      EXPECT_NE(toolbar.widgetForAction(action), nullptr);
    }
    EXPECT_EQ(tool_index, kModelingTools.size());
    EXPECT_EQ(separator_index, kSeparatorPositions.size());
  }
  for (const auto tool : {quader::ViewportToolId::Select,
                          quader::ViewportToolId::LassoSelect,
                          quader::ViewportToolId::PaintSelect,
                          quader::ViewportToolId::Move,
                          quader::ViewportToolId::Rotate,
                          quader::ViewportToolId::Scale}) {
    QAction *source = toolbar.tool_action(tool);
    ASSERT_NE(source, nullptr);
    EXPECT_FALSE(toolbar.actions().contains(source));
    EXPECT_EQ(toolbar.widgetForAction(source), nullptr);
  }
  EXPECT_EQ(toolbar.findChild<QAction *>(QStringLiteral("command_edit.delete")),
            nullptr);
  auto *align = toolbar.tool_action(quader::ViewportToolId::Align);
  ASSERT_NE(align, nullptr);
  EXPECT_EQ(align->text(), QStringLiteral("Align Tool"));
  auto *align_button = qobject_cast<QToolButton *>(toolbar.widgetForAction(align));
  ASSERT_NE(align_button, nullptr);
  EXPECT_FALSE(align->icon().isNull());
  EXPECT_EQ(align_button->toolButtonStyle(), Qt::ToolButtonIconOnly);
}

TEST(QtToolToolbarTests, SelectionChangesRefreshCanonicalToolRequirements) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_tool_toolbar_requirements_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::QtToolToolbar toolbar;
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  toolbar.synchronize(editor, editor.selection().summary());
  QAction *cut = toolbar.tool_action(quader::ViewportToolId::Cut);
  QAction *mirror = toolbar.tool_action(quader::ViewportToolId::Mirror);
  ASSERT_NE(cut, nullptr);
  ASSERT_NE(mirror, nullptr);
  EXPECT_FALSE(cut->isEnabled());
  EXPECT_FALSE(mirror->isEnabled());

  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Toolbar Requirements");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_TRUE(cut->isEnabled());
  EXPECT_TRUE(mirror->isEnabled());
  for (const auto tool : kModelingTools) {
    if (tool == quader::ViewportToolId::CreatePrimitive) {
      continue;
    }
    const auto command_id = editor.commands().command_id_for_tool_activation(
        quader::viewport_tool_catalog_id(tool));
    const auto descriptor = editor.commands().descriptor(command_id);
    ASSERT_TRUE(descriptor.has_value()) << command_id;
    EXPECT_EQ(toolbar.tool_action(tool)->isEnabled(), descriptor->enabled)
        << command_id;
  }
  ASSERT_TRUE(editor.selection().clear().ok());
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_FALSE(cut->isEnabled());
  EXPECT_FALSE(mirror->isEnabled());
}

TEST(QtToolToolbarTests, DispatchesActivationAndRetainsUnchangedPresentation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_tool_toolbar_dispatch_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::QtToolToolbar toolbar;
  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Toolbar Activation");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  int requests = 0;
  toolbar.set_tool_requested_callback([&](std::string_view tool_id) {
    ++requests;
    EXPECT_EQ(tool_id, "cut");
    const auto command_id = editor.commands().command_id_for_tool_activation(tool_id);
    EXPECT_EQ(command_id, quader::quader_command::kToolsActivateCut.string());
    const auto result = editor.commands().execute(command_id);
    EXPECT_TRUE(result.ok()) << result.error_message();
  });
  toolbar.synchronize(editor, editor.selection().summary());
  QAction *cut = toolbar.tool_action(quader::ViewportToolId::Cut);
  ASSERT_NE(cut, nullptr);
  ASSERT_TRUE(cut->isEnabled());
  cut->trigger();
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(editor.viewports().selected_tool(editor.viewports().focused_pane()),
            quader::ViewportToolId::Cut);
  EXPECT_TRUE(cut->isChecked());

  int presentation_changes = 0;
  for (QAction *action : toolbar.findChildren<QAction *>()) {
    QObject::connect(action, &QAction::changed, &toolbar,
                     [&]() { ++presentation_changes; });
  }
  const auto selection = editor.selection().summary();
  const auto before = session.modeling().modeling_sdk_projection_stats_for_tests();
  for (int frame = 0; frame < 10; ++frame) {
    toolbar.synchronize(editor, selection);
  }
  const auto after = session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(presentation_changes, 0);
  EXPECT_EQ(after.selection_summary_build_count, before.selection_summary_build_count);
  EXPECT_EQ(after.selection_topology_build_count, before.selection_topology_build_count);
  EXPECT_EQ(after.mesh_compile_count, before.mesh_compile_count);

  ASSERT_TRUE(editor.commands().execute(
      quader::quader_command::kToolsActivateSelect).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_FALSE(cut->isEnabled());
  cut->trigger();
  EXPECT_EQ(requests, 1);
}

TEST(QtToolToolbarTests, AlignIconButtonDispatchesForLightsAndScalesWithNativeTools) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_align_toolbar_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  quader::apply_qt_obsidian_application_theme(application.palette());
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::QtToolToolbar toolbar;
  toolbar.setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto first = editor.scene().create_point_light({.name = "Toolbar First Light"});
  const auto second = editor.scene().create_directional_light({.name = "Toolbar Second Light"});
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection().select_objects({first.value().id, second.value().id}).ok());
  toolbar.set_tool_requested_callback([&](std::string_view tool_id) {
    EXPECT_EQ(tool_id, "align");
    const auto command_id = editor.commands().command_id_for_tool_activation(tool_id);
    EXPECT_EQ(command_id, quader::quader_command::kToolsActivateAlign.string());
    EXPECT_TRUE(editor.commands().execute(command_id).ok());
  });
  toolbar.synchronize(editor, editor.selection().summary());
  auto *align = toolbar.tool_action(quader::ViewportToolId::Align);
  ASSERT_NE(align, nullptr);
  ASSERT_TRUE(align->isEnabled());
  align->trigger();
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_EQ(editor.tools().active_tool(), "align");
  EXPECT_TRUE(align->isChecked());
  const QFont baseline_font = application.font();
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "en-US"));
  for (const auto *locale : {"en-US", "it-IT"}) {
    ASSERT_TRUE(localization.select_locale(locale));
    toolbar.set_localization(localization);
    for (const float scale : {1.0f, 2.0f, 1.0f}) {
      SCOPED_TRACE(locale);
      SCOPED_TRACE(scale);
      const quader::QtUiScaleMetrics metrics(scale);
      toolbar.setFont(metrics.font(baseline_font));
      toolbar.apply_ui_scale(metrics);
      toolbar.resize(metrics.px(400), metrics.px(60));
      toolbar.show();
      QCoreApplication::processEvents();
      auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(align));
      ASSERT_NE(button, nullptr);
      EXPECT_EQ(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
      EXPECT_FALSE(align->icon().isNull());
      EXPECT_EQ(button->iconSize(), metrics.size(QSize(32, 32)));
      EXPECT_GE(button->width(), metrics.px(40));
      EXPECT_GE(button->height(), metrics.px(40));
      for (const auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Active}) {
        const auto pixmap = align->icon().pixmap(button->iconSize(), 1.0, mode);
        ASSERT_FALSE(pixmap.isNull());
        EXPECT_EQ(pixmap.size(), button->iconSize());
        EXPECT_TRUE(pixmap.hasAlphaChannel());
      }
      const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
      if (!directory.isEmpty()) {
        ASSERT_TRUE(QDir().mkpath(directory));
        QImage image(toolbar.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        toolbar.render(&image);
        EXPECT_TRUE(image.save(QDir(directory).filePath(
            QStringLiteral("align-toolbar-%1-%2.png")
                .arg(QString::fromUtf8(locale))
                .arg(static_cast<int>(scale * 100.0f)))));
      }
    }
  }
  auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(align));
  ASSERT_NE(button, nullptr);
  EXPECT_EQ(button->minimumSize(), QSize(40, 40));
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_FALSE(align->isEnabled());
}
