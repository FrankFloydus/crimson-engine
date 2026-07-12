////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/guardrail_file_scan.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_path;

void expect_contains(std::string_view source, std::string_view needle) {
  EXPECT_NE(source.find(needle), std::string_view::npos) << needle;
}

void expect_missing(std::string_view source, std::string_view needle) {
  EXPECT_EQ(source.find(needle), std::string_view::npos) << needle;
}

std::size_t count_occurrences(std::string_view source,
                              std::string_view needle) {
  std::size_t count = 0;
  std::size_t offset = 0;
  while ((offset = source.find(needle, offset)) != std::string_view::npos) {
    ++count;
    offset += needle.size();
  }
  return count;
}

const std::vector<std::string_view> &qt_shell_source_paths() {
  static const std::vector<std::string_view> kPaths = {
      "src/main.cpp",
      "src/ui/ui_system.hpp",
      "src/ui/ui_system.cpp",
      "src/ui/ui_panel_manager.hpp",
      "src/ui/ui_panel_manager.cpp",
      "src/ui/outliner_panel.hpp",
      "src/ui/outliner_panel.cpp",
      "src/ui/inspector_panel.hpp",
      "src/ui/inspector_panel.cpp",
      "src/ui/console_panel.hpp",
      "src/ui/console_panel.cpp",
      "src/ui/qt_color_swatch_button.hpp",
      "src/ui/qt_color_swatch_button.cpp",
      "src/ui/qt_settings_dialog.hpp",
      "src/ui/qt_settings_dialog.cpp",
      "src/ui/qt_settings_controller.hpp",
      "src/ui/qt_settings_controller.cpp",
      "src/ui/qt_tool_settings_window.hpp",
      "src/ui/qt_tool_settings_window.cpp",
      "src/ui/qt_viewport_context_menu.hpp",
      "src/ui/qt_viewport_context_menu.cpp",
      "src/ui/qt_navigation_gizmo.hpp",
      "src/ui/qt_navigation_gizmo.cpp",
      "src/ui/qt_semantic_axis_colors.hpp",
      "src/ui/qt_viewport_panel.hpp",
      "src/ui/qt_viewport_panel.cpp",
      "src/ui/qt_viewport_surface.hpp",
      "src/ui/qt_viewport_surface.cpp",
      "src/ui/qt_main_menu.hpp",
      "src/ui/qt_main_menu.cpp",
      "src/ui/qt_input_adapter.hpp",
      "src/ui/qt_input_adapter.cpp",
  };
  return kPaths;
}

std::string joined_sources(const std::vector<std::string_view> &paths) {
  std::string joined;
  for (std::string_view path : paths) {
    joined.append(read_source_file(std::filesystem::path(std::string(path))));
    joined.push_back('\n');
  }
  return joined;
}

void expect_sources_missing(const std::vector<std::string_view> &paths,
                            const std::vector<std::string_view> &needles) {
  for (std::string_view path : paths) {
    const std::string source =
        read_source_file(std::filesystem::path(std::string(path)));
    for (std::string_view needle : needles) {
      EXPECT_EQ(source.find(needle), std::string::npos)
          << path << ": " << needle;
    }
  }
}

} // namespace

TEST(UiLayoutGuardrailTests, RetiredImGuiPresentationFilesStayRemoved) {
  for (std::string_view retired_path : {
           "src/render/imgui_filament_renderer.cpp",
           "src/render/imgui_filament_renderer.hpp",
           "src/render/ui_icon_texture_service.cpp",
           "src/render/ui_icon_texture_service.hpp",
           "src/ui/main_menu_bar.cpp",
           "src/ui/main_menu_bar.hpp",
           "src/ui/phosphor_icons.hpp",
           "src/ui/tool_settings_panel.cpp",
           "src/ui/tool_settings_panel.hpp",
           "src/ui/ui_dock_panel.hpp",
           "src/ui/ui_floating_panel.hpp",
           "src/ui/ui_icon_set.hpp",
           "src/ui/ui_layout.hpp",
           "src/ui/ui_platform_adapter.hpp",
           "src/ui/ui_style_tokens.cpp",
           "src/ui/ui_style_tokens.hpp",
           "src/ui/viewport_settings_panel.cpp",
           "src/ui/viewport_settings_panel.hpp",
       }) {
    EXPECT_FALSE(std::filesystem::exists(source_path(
        std::filesystem::path(std::string(retired_path)))))
        << retired_path;
  }
}

TEST(UiLayoutGuardrailTests,
     QtShellSelectsStockFusionExactlyOnceWithoutThemeOverrides) {
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string shell_sources = joined_sources(qt_shell_source_paths());

  expect_contains(main_source, "#include <QApplication>");
  expect_contains(main_source, "QApplication qt_application(argc, argv)");
  expect_contains(main_source,
                   "QStyleFactory::create(QStringLiteral(\"Fusion\"))");
  expect_contains(main_source, "QApplication::setStyle(fusion_style)");
  expect_contains(
      main_source,
      "QApplication::setPalette(fusion_style->standardPalette())");
  EXPECT_EQ(count_occurrences(shell_sources, "QStyleFactory::create("), 1u);
  EXPECT_EQ(count_occurrences(shell_sources, "QApplication::setStyle("), 1u);
  EXPECT_EQ(count_occurrences(shell_sources, "standardPalette()"), 1u);

  expect_sources_missing(
      qt_shell_source_paths(),
      {"setStyleSheet(", ".qss", "QProxyStyle", "QFontDatabase", "setFont(",
       "Qt::FramelessWindowHint", "Qt::CustomizeWindowHint",
       "Qt::WA_TranslucentBackground", "ImGui::", "#include <imgui",
       "#include <SDL", "SDL_"});

  for (const std::string_view path : qt_shell_source_paths()) {
    if (path == "src/main.cpp" ||
        path == "src/ui/qt_color_swatch_button.cpp" ||
        path == "src/ui/qt_color_swatch_button.hpp" ||
        path == "src/ui/qt_semantic_axis_colors.hpp") {
      continue;
    }
    const std::string source = read_source_file(path);
    expect_missing(source, "QPalette");
    expect_missing(source, "setPalette(");
  }
}

TEST(UiLayoutGuardrailTests,
     MainWindowOwnsCentralViewportAndStandardDockPanels) {
  const std::string ui_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");
  const std::string panel_manager_header =
      read_source_file("src/ui/ui_panel_manager.hpp");
  const std::string panel_manager_source =
      read_source_file("src/ui/ui_panel_manager.cpp");

  expect_contains(ui_header, "std::unique_ptr<QMainWindow> main_window_");
  expect_contains(ui_source, "std::make_unique<QMainWindow>()");
  expect_contains(ui_source, "QStringLiteral(\"quaderMainWindow\")");
  expect_contains(ui_source, "setCentralWidget(viewport_panel_)");
  expect_contains(ui_source, "QMainWindow::AllowNestedDocks");
  expect_contains(ui_source, "QMainWindow::AllowTabbedDocks");

  expect_contains(ui_source, "new QDockWidget(");
  expect_contains(ui_source, "ui_panel_stable_id(id)");
  expect_contains(ui_source, "QStringLiteral(\"dock_%1\")");
  expect_contains(ui_source, "QDockWidget::DockWidgetClosable");
  expect_contains(ui_source, "QDockWidget::DockWidgetMovable");
  expect_contains(ui_source, "QDockWidget::DockWidgetFloatable");
  expect_contains(ui_source, "Qt::AllDockWidgetAreas");
  expect_contains(ui_source, "Qt::LeftDockWidgetArea");
  expect_contains(ui_source, "Qt::RightDockWidgetArea");
  expect_contains(ui_source, "Qt::BottomDockWidgetArea");
  expect_contains(ui_source, "addDockWidget(");

  expect_contains(ui_source, "saveState(kQtWindowStateVersion)");
  expect_contains(ui_source, "restoreState(state, kQtWindowStateVersion)");
  expect_contains(panel_manager_header,
                  "std::array<QDockWidget *, 4> docks_");
  expect_contains(panel_manager_header,
                  "std::array<bool, 4> explicit_visibility_");
  expect_contains(panel_manager_source, "UIPanelManager::add(");
  expect_contains(panel_manager_source, "UIPanelManager::set_visible(");
  expect_contains(panel_manager_source, "UIPanelManager::dock(");
  expect_contains(panel_manager_source, "&QAction::triggered");
  expect_contains(panel_manager_source, "event->type() == QEvent::Close");
  expect_contains(panel_manager_source, "explicit_visibility_[slot]");
  expect_missing(panel_manager_source, "panel->isVisible()");
  expect_missing(ui_source, "&QDockWidget::visibilityChanged");
}

TEST(UiLayoutGuardrailTests,
     StartupSuppressesWholeShellUntilFinalDockLayoutAndNativePresentation) {
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string ui_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");
  const std::string viewport_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string viewport_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string startup_policy =
      read_source_file("src/ui/qt_startup_presentation.hpp");

  const std::size_t ui_initialize =
      ui_source.find("bool UISystem::initialize(");
  const std::size_t ui_install_hooks =
      ui_source.find("void UISystem::install_runtime_hooks(", ui_initialize);
  ASSERT_NE(ui_initialize, std::string::npos);
  ASSERT_NE(ui_install_hooks, std::string::npos);
  const std::string_view initialize_body(ui_source.data() + ui_initialize,
                                         ui_install_hooks - ui_initialize);
  const std::size_t create_docks = initialize_body.find("create_docks();");
  const std::size_t restore_state =
      initialize_body.find("restore_window_state();", create_docks);
  ASSERT_NE(create_docks, std::string_view::npos);
  ASSERT_NE(restore_state, std::string_view::npos);
  EXPECT_LT(create_docks, restore_state);
  EXPECT_EQ(count_occurrences(initialize_body, "create_docks();"), 1u);

  const std::size_t create_docks_owner =
      ui_source.find("void UISystem::create_docks()");
  const std::size_t create_dock_factory =
      ui_source.find("QDockWidget &UISystem::create_dock(",
                     create_docks_owner);
  ASSERT_NE(create_docks_owner, std::string::npos);
  ASSERT_NE(create_dock_factory, std::string::npos);
  const std::string_view create_docks_body(
      ui_source.data() + create_docks_owner,
      create_dock_factory - create_docks_owner);
  for (std::string_view panel : {"UIPanelId::Outliner", "UIPanelId::Inspector",
                                 "UIPanelId::Console"}) {
    EXPECT_EQ(count_occurrences(create_docks_body, panel), 1u) << panel;
  }

  const std::size_t apply_window_settings =
      app_source.find("void App::apply_window_settings()");
  const std::size_t save_window_settings =
      app_source.find("void App::save_window_settings()", apply_window_settings);
  ASSERT_NE(apply_window_settings, std::string::npos);
  ASSERT_NE(save_window_settings, std::string::npos);
  const std::string_view apply_body(
      app_source.data() + apply_window_settings,
      save_window_settings - apply_window_settings);
  expect_contains(apply_body, "window->setGeometry(");
  expect_contains(
      apply_body,
      "window->setWindowState(window->windowState() | Qt::WindowMaximized)");
  expect_missing(apply_body, "showMaximized(");
  expect_missing(apply_body, "showNormal(");
  expect_missing(apply_body, "window->show(");

  const std::size_t initialize_app =
      app_source.find("bool App::initialize(");
  const std::size_t apply_placement =
      app_source.find("apply_window_settings();", initialize_app);
  const std::size_t reveal = app_source.find("ui_.show();", apply_placement);
  ASSERT_NE(initialize_app, std::string::npos);
  ASSERT_NE(apply_placement, std::string::npos);
  ASSERT_NE(reveal, std::string::npos);
  EXPECT_LT(apply_placement, reveal);

  const std::size_t show_owner = ui_source.find("void UISystem::show()");
  const std::size_t shutdown_owner =
      ui_source.find("void UISystem::shutdown()", show_owner);
  ASSERT_NE(show_owner, std::string::npos);
  ASSERT_NE(shutdown_owner, std::string::npos);
  const std::string_view show_body(ui_source.data() + show_owner,
                                   shutdown_owner - show_owner);
  EXPECT_EQ(count_occurrences(show_body, "main_window_->show()"), 1u);
  const std::size_t prepare_shell =
      show_body.find("prepare_startup_presentation()");
  const std::size_t show_shell = show_body.find("main_window_->show()");
  ASSERT_NE(prepare_shell, std::string_view::npos);
  ASSERT_NE(show_shell, std::string_view::npos);
  EXPECT_LT(prepare_shell, show_shell);

  expect_contains(ui_header, "QtStartupPresentationState");
  expect_contains(ui_source, "main_window_->setUpdatesEnabled(false)");
  expect_contains(ui_source,
                  "viewport_panel_->set_startup_presentation_blocked(true)");
  expect_contains(ui_source, "platform_window_is_maximized(");
  expect_contains(ui_source, "platform_window_client_extent(");
  expect_contains(ui_source, "qt_startup_client_extents_match(");
  expect_contains(ui_source, "startup_layout_ready()");
  expect_contains(ui_source, "startup_layout_geometry_signature(");
  expect_contains(ui_source, "main_window_->devicePixelRatioF()");
  expect_contains(ui_source, "main_window_->setUpdatesEnabled(true)");
  const std::size_t timeout_guard =
      ui_source.find("kStartupPresentationTimeout");
  const std::size_t timeout_hard_invariants =
      ui_source.find("hard_invariants_ready", timeout_guard);
  const std::size_t timeout_finish =
      ui_source.find("finish_startup_presentation(true)", timeout_guard);
  ASSERT_NE(timeout_guard, std::string::npos);
  ASSERT_NE(timeout_hard_invariants, std::string::npos);
  ASSERT_NE(timeout_finish, std::string::npos);
  EXPECT_LT(timeout_hard_invariants, timeout_finish);
  const std::size_t finish_shell =
      ui_source.find("void UISystem::finish_startup_presentation(");
  const std::size_t release_panes = ui_source.find(
      "viewport_panel_->set_startup_presentation_blocked(false)",
      finish_shell);
  const std::size_t enable_updates =
      ui_source.find("main_window_->setUpdatesEnabled(true)", release_panes);
  const std::size_t repaint_shell =
      ui_source.find("main_window_->repaint()", enable_updates);
  ASSERT_NE(finish_shell, std::string::npos);
  ASSERT_NE(release_panes, std::string::npos);
  ASSERT_NE(enable_updates, std::string::npos);
  ASSERT_NE(repaint_shell, std::string::npos);
  EXPECT_LT(release_panes, enable_updates);
  EXPECT_LT(enable_updates, repaint_shell);
  expect_missing(ui_source, "startupCurtain");
  expect_missing(ui_source, "prepare_startup_curtain");

  expect_contains(startup_policy, "consecutive_equal_samples");
  expect_contains(startup_policy,
                  "QtStartupPresentationAction::GeometryStabilized");
  expect_contains(startup_policy,
                  "QtStartupPresentationAction::PresentationReady");
  expect_contains(ui_source, "main_window_->updatesEnabled()");
  expect_contains(ui_source,
                  "!viewport_panel_->startup_presentation_blocked()");
  expect_contains(viewport_header, "set_startup_presentation_blocked(");
  expect_contains(viewport_source,
                  "visible && !startup_presentation_blocked_");

  const std::size_t create_pane =
      viewport_source.find("void QtViewportPanel::create_pane(");
  const std::size_t refresh_controls =
      viewport_source.find("void QtViewportPanel::refresh_pane_controls(",
                           create_pane);
  ASSERT_NE(create_pane, std::string::npos);
  ASSERT_NE(refresh_controls, std::string::npos);
  const std::string_view create_pane_body(
      viewport_source.data() + create_pane, refresh_controls - create_pane);
  expect_contains(create_pane_body, "surface_container->hide()");
  expect_contains(viewport_source, "setAutoFillBackground(true)");

  const std::size_t render_viewports =
      viewport_source.find("std::size_t QtViewportPanel::render_viewports()");
  const std::size_t startup_gate = viewport_source.find(
      "qt_startup_presentation_action(", render_viewports);
  const std::size_t reveal_surface =
      viewport_source.find("surface_container->show()", startup_gate);
  const std::size_t exposure_turn =
      viewport_source.find("return 0u;", reveal_surface);
  const std::size_t begin_presentation = viewport_source.find(
      "render_system_->begin_presentation_frame()", reveal_surface);
  ASSERT_NE(render_viewports, std::string::npos);
  ASSERT_NE(startup_gate, std::string::npos);
  ASSERT_NE(reveal_surface, std::string::npos);
  ASSERT_NE(exposure_turn, std::string::npos);
  ASSERT_NE(begin_presentation, std::string::npos);
  EXPECT_LT(startup_gate, reveal_surface);
  EXPECT_LT(reveal_surface, exposure_turn);
  EXPECT_LT(exposure_turn, begin_presentation);
  expect_contains(viewport_source, "devicePixelRatioF()");
  expect_contains(viewport_source,
                  "qt_invalidate_startup_presentation_geometry(");
  expect_contains(viewport_source,
                  "qt_startup_native_child_should_be_visible(");
  const std::size_t invalidated_surface_hide =
      viewport_source.find("surface_container->hide()", startup_gate);
  ASSERT_NE(invalidated_surface_hide, std::string::npos);
  EXPECT_LT(invalidated_surface_hide, reveal_surface);
}

TEST(UiLayoutGuardrailTests,
     OutlinerInspectorAndConsoleUseStockWidgetsAndStableNames) {
  const std::string outliner_header =
      read_source_file("src/ui/outliner_panel.hpp");
  const std::string outliner = read_source_file("src/ui/outliner_panel.cpp");
  const std::string inspector_header =
      read_source_file("src/ui/inspector_panel.hpp");
  const std::string inspector =
      read_source_file("src/ui/inspector_panel.cpp");
  const std::string console_header =
      read_source_file("src/ui/console_panel.hpp");
  const std::string console = read_source_file("src/ui/console_panel.cpp");

  expect_contains(outliner_header,
                  "class OutlinerPanel final : public QWidget");
  expect_contains(outliner, "public QTreeView");
  expect_contains(outliner, "new QStandardItemModel");
  expect_contains(outliner, "ui_panel_stable_id(UIPanelId::Outliner)");
  expect_contains(outliner, "QStringLiteral(\"outliner.tree\")");
  expect_contains(outliner, "QStringLiteral(\"outliner.model\")");
  expect_contains(outliner, "QStringLiteral(\"outliner.context_menu\")");
  expect_contains(outliner, "tree_->setHeaderHidden(false)");
  expect_contains(outliner, "tree_->setAlternatingRowColors(true)");
  expect_contains(outliner,
                  "header->setSectionResizeMode(column_index(OutlinerColumn::Name),");
  expect_contains(outliner, "QHeaderView::Stretch");
  EXPECT_EQ(count_occurrences(outliner, "QHeaderView::ResizeToContents"), 2u);
  expect_contains(outliner,
                  "QAbstractItemView::SelectedClicked |\n"
                  "                           QAbstractItemView::EditKeyPressed");
  expect_contains(outliner, "Qt::PartiallyChecked");
  expect_contains(outliner, "editor_->scene().rename(");
  expect_contains(outliner, "editor_->scene().set_visible(");
  expect_contains(outliner, "editor_->scene().set_locked(");
  expect_contains(outliner,
                  "QStringLiteral(\"outliner.action.rename\")");
  expect_contains(outliner,
                  "editor.commands().execute(quader_command::kDeleteSelection)");
  expect_contains(
      outliner,
      "editor.commands().execute(quader_command::kDuplicateSelection)");

  expect_contains(inspector_header,
                  "class InspectorPanel final : public QWidget");
  expect_contains(inspector, "new QTreeWidget");
  expect_contains(inspector, "InspectorDoubleSpinBox");
  expect_contains(inspector, "new QCheckBox");
  expect_contains(inspector, "new QComboBox");
  expect_contains(inspector, "ui_panel_stable_id(UIPanelId::Inspector)");
  expect_contains(inspector, "QStringLiteral(\"inspector.\")");
  expect_contains(inspector, "QStringLiteral(\"inspector.tree\")");
  expect_contains(inspector, "editor_->scene().set_scene_settings(");
  expect_contains(inspector, "editor_->scene().set_light_component(");
  expect_missing(inspector, "document_->set_scene_settings(");
  expect_missing(inspector, "document_->set_light(");

  expect_contains(console_header,
                  "class ConsolePanel final : public QWidget");
  expect_contains(console, "new QListView");
  expect_contains(console, "new QStandardItemModel");
  expect_contains(console, "ui_panel_stable_id(UIPanelId::Console)");
  expect_contains(console, "QStringLiteral(\"console.search\")");
  expect_contains(console, "QStringLiteral(\"console.entries\")");
  expect_contains(console, "QStringLiteral(\"console.entries.model\")");
}

TEST(UiLayoutGuardrailTests,
     OutlinerCollapseExpandOnlyUpdatesRetainedPresentationState) {
  const std::string outliner = read_source_file("src/ui/outliner_panel.cpp");
  const std::size_t collapse_connection =
      outliner.find("QObject::connect(tree_, &QTreeView::collapsed");
  const std::size_t refresh_method =
      outliner.find("  void refresh(UIContext &context)", collapse_connection);
  ASSERT_NE(collapse_connection, std::string::npos);
  ASSERT_NE(refresh_method, std::string::npos);

  const std::string signal_wiring = outliner.substr(
      collapse_connection, refresh_method - collapse_connection);
  expect_contains(signal_wiring, "collapsed_entities_.insert(entity_id(index))");
  expect_contains(signal_wiring, "collapsed_entities_.erase(entity_id(index))");
  expect_missing(signal_wiring, "refresh_model_if_needed()");
  expect_missing(signal_wiring, "sync_selection()");
  expect_missing(signal_wiring, "editor_->scene()");

  expect_contains(outliner, "document_->selection_revision()");
  expect_contains(outliner, "outliner_selection_sync_required(");
  expect_contains(outliner, "row_is_revealed(");
  expect_contains(outliner, "document_->primary_selection())");
}

TEST(UiLayoutGuardrailTests,
     InspectorUsesAResizableTwoColumnQtPropertyTree) {
  const std::string inspector =
      read_source_file("src/ui/inspector_panel.cpp");

  expect_contains(inspector, "tree_->setColumnCount(2)");
  expect_contains(inspector, "tree_->setAlternatingRowColors(true)");
  expect_contains(inspector, "tree_->setUniformRowHeights(false)");
  expect_contains(inspector, "tree_->header()->setVisible(true)");
  expect_contains(inspector, "tree_->header()->setSectionsMovable(false)");
  expect_contains(inspector, "tree_->header()->setMinimumSectionSize(64)");
  expect_contains(
      inspector,
      "tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive)");
  expect_contains(
      inspector,
      "tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch)");
  expect_contains(inspector, "item->setFirstColumnSpanned(true)");
  expect_contains(inspector, "new QTreeWidgetItem(section.item)");
  expect_contains(inspector, "tree_->setItemWidget(item, 1, field)");
  expect_contains(inspector, "new InspectorPropertyDelegate(tree_)");
  expect_contains(inspector, "option.palette.mid().color()");
  expect_contains(inspector,
                  "painter->drawLine(option.rect.topRight(), "
                  "option.rect.bottomRight())");
  expect_contains(inspector, "field->setMinimumWidth(0)");
  expect_contains(inspector,
                  "policy.setHorizontalPolicy(QSizePolicy::Ignored)");
  expect_contains(inspector,
                  "tree_->setHorizontalScrollBarPolicy("
                  "Qt::ScrollBarAlwaysOff)");
  expect_contains(inspector, "kInspectorMinimumValueColumnWidth");
  expect_contains(inspector, "&QHeaderView::sectionResized");
  expect_contains(inspector, "&QHeaderView::geometriesChanged");
  expect_contains(inspector, "clamp_label_column_width()");
  expect_contains(inspector,
                  "expanded_sections_.try_emplace(stable_id, false)");
  expect_contains(inspector, "&QTreeWidget::itemExpanded");
  expect_contains(inspector, "&QTreeWidget::itemCollapsed");

  const std::size_t collapse_begin =
      inspector.find("const auto remember_section_state");
  const std::size_t collapse_end =
      inspector.find("QObject::connect(tree_->header()", collapse_begin);
  ASSERT_NE(collapse_begin, std::string::npos);
  ASSERT_NE(collapse_end, std::string::npos);
  const std::string collapse_handlers =
      inspector.substr(collapse_begin, collapse_end - collapse_begin);
  expect_contains(collapse_handlers, "expanded_sections_[");
  expect_missing(collapse_handlers, "setExpanded(");
  expect_missing(collapse_handlers, "refresh(");
  expect_missing(collapse_handlers, "rebuild(");

  expect_missing(inspector, "new QScrollArea");
  expect_missing(inspector, "new QFormLayout");
  expect_missing(inspector, "new QToolButton");
  expect_missing(inspector, "setStyleSheet(");
  expect_missing(inspector, "setPalette(");
  expect_missing(inspector, "setFont(");
}

TEST(UiLayoutGuardrailTests,
     InspectorTransformUsesCompactSemanticAxisRows) {
  const std::string inspector =
      read_source_file("src/ui/inspector_panel.cpp");
  const std::string axis_colors =
      read_source_file("src/ui/qt_semantic_axis_colors.hpp");
  const std::string navigation_gizmo =
      read_source_file("src/ui/qt_navigation_gizmo.cpp");

  expect_contains(inspector,
                  "#include \"ui/qt_semantic_axis_colors.hpp\"");
  expect_contains(inspector,
                  "const int label_column = static_cast<int>(axis * 2u)");
  expect_contains(inspector, "const int field_column = label_column + 1");
  expect_contains(inspector,
                  "layout->addWidget(axis_label, 0, label_column)");
  expect_contains(inspector,
                  "layout->addWidget(mixed_field, 0, field_column)");
  expect_contains(inspector,
                  "layout->addWidget(spin, 0, field_column)");
  expect_contains(inspector,
                  "apply_qt_semantic_axis_label_color(*axis_label");
  expect_missing(inspector,
                 "layout->addWidget(spin, 1, static_cast<int>(axis))");

  expect_contains(axis_colors, "QPalette::WindowText");
  expect_contains(axis_colors, "QColor(235, 76, 82)");
  expect_contains(axis_colors, "QColor(90, 205, 118)");
  expect_contains(axis_colors, "QColor(78, 142, 255)");
  expect_missing(axis_colors, "QPalette::Button");
  expect_missing(axis_colors, "setStyleSheet(");
  expect_contains(navigation_gizmo,
                  "qt_semantic_axis_color(edge.axis)");
  expect_contains(navigation_gizmo,
                  "qt_semantic_axis_color(marker.axis)");
  expect_missing(navigation_gizmo, "QColor axis_color(");
}

TEST(UiLayoutGuardrailTests,
     InspectorColorRowsUseTheSharedFullWidthLiveSwatchButton) {
  const std::string inspector =
      read_source_file("src/ui/inspector_panel.cpp");
  const std::string swatch_header =
      read_source_file("src/ui/qt_color_swatch_button.hpp");
  const std::string swatch =
      read_source_file("src/ui/qt_color_swatch_button.cpp");

  expect_contains(inspector,
                  "#include \"ui/qt_color_swatch_button.hpp\"");
  expect_contains(inspector,
                  "new QtColorSwatchButton(section.editor_parent)");
  expect_contains(inspector, "button->setAccessibleName(label)");
  expect_contains(inspector, "button->set_swatch_color(initial)");
  expect_contains(inspector, "guarded_button->set_swatch_color(color)");
  expect_contains(inspector, "guarded_button->set_swatch_color(initial)");
  expect_contains(inspector, "QColorDialog::currentColorChanged");
  expect_missing(inspector,
                 "new QPushButton(initial.name(QColor::HexRgb)");
  expect_missing(inspector, "guarded_button->setText(");

  expect_contains(swatch_header,
                   "class QtColorSwatchButton final : public QPushButton");
  expect_contains(swatch, "QPalette button_palette = palette()");
  expect_contains(swatch,
                  "button_palette.setColor(QPalette::Button, swatch_color_)");
  expect_contains(swatch, "setPalette(button_palette)");
  expect_contains(swatch, "setToolTip(hex)");
  expect_contains(swatch, "setAccessibleDescription(hex)");
  expect_missing(swatch, "setStyleSheet(");
  expect_missing(swatch, "paintEvent(");
  expect_missing(swatch, "QPainter");
}

TEST(UiLayoutGuardrailTests,
     SettingsAndToolWindowsUseStockControlsAndStableNames) {
  const std::string settings_header =
      read_source_file("src/ui/qt_settings_dialog.hpp");
  const std::string settings =
      read_source_file("src/ui/qt_settings_dialog.cpp");
  const std::string settings_controller =
      read_source_file("src/ui/qt_settings_controller.cpp");
  const std::string tool_header =
      read_source_file("src/ui/qt_tool_settings_window.hpp");
  const std::string tool =
      read_source_file("src/ui/qt_tool_settings_window.cpp");
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");

  expect_contains(settings_header,
                  "class QtSettingsDialog final : public QDialog");
  expect_contains(settings, "QStringLiteral(\"settingsDialog\")");
  expect_contains(settings, "QStringLiteral(\"settingsCategories\")");
  expect_contains(settings, "QStringLiteral(\"settingsPages\")");
  expect_missing(settings, "QStringLiteral(\"settingsResetDefaults\")");
  expect_contains(settings, "new QDialogButtonBox(");
  expect_contains(settings, "QDialogButtonBox::Apply");
  expect_contains(settings, "QDialogButtonBox::Ok");
  expect_contains(settings, "QDialogButtonBox::Cancel");
  expect_contains(settings, "QStringLiteral(\"settingsApply\")");
  expect_contains(settings, "QStringLiteral(\"settingsOk\")");
  expect_contains(settings, "QStringLiteral(\"settingsCancel\")");
  expect_contains(settings, "void QtSettingsDialog::accept()");
  expect_contains(settings, "void QtSettingsDialog::reject()");
  expect_contains(settings, "viewport_settings_sections()");
  expect_contains(settings, "viewport_color_settings()");
  expect_contains(settings, "viewport_float_settings()");
  expect_contains(settings, "viewport_bool_settings()");
  expect_contains(settings, "controller.set_float(");
  expect_contains(settings, "controller.set_bool(");
  expect_contains(settings, "controller.select_locale(");
  expect_contains(settings,
                  "localization->select_locale(draft_ui_settings.locale())");
  expect_contains(settings_controller,
                  "QtSettingsChangeKind::ViewportCommit");
  expect_contains(settings_controller,
                  "QtSettingsChangeKind::InterfaceCommit");
  expect_contains(settings, "new QFormLayout");
  expect_contains(settings, "new QListWidget");
  expect_contains(settings, "new QStackedWidget");
  expect_contains(settings, "new QDoubleSpinBox");
  expect_contains(settings, "new QCheckBox");
  expect_contains(settings, "new QComboBox");

  expect_contains(tool_header,
                  "class QtToolSettingsWindow final : public QWidget");
  expect_contains(tool, "QWidget(parent, Qt::Tool)");
  expect_contains(tool, "QStringLiteral(\"floating.tool_settings\")");
  expect_contains(tool, "QStringLiteral(\"tool_settings.apply\")");
  expect_contains(tool, "QStringLiteral(\"tool_settings.cancel\")");
  expect_contains(tool, "new QCheckBox");
  expect_contains(tool, "new QSlider(Qt::Horizontal, row)");
  expect_contains(tool, "slider->setTracking(true)");
  expect_contains(tool, "const QSignalBlocker spin_blocker(spin)");
  expect_contains(tool, "const QSignalBlocker slider_blocker(slider)");
  expect_contains(tool,
                  "std::max(4, decimal_places_for_step(control.step))");
  expect_contains(tool, "dispatch_number(spin->value())");
  expect_contains(tool, "new QDoubleSpinBox");
  expect_contains(tool, "new QRadioButton");
  expect_contains(tool, "new QPushButton");
  expect_contains(tool, "editor_->tools().active_settings()");
  expect_contains(tool, "editor_->tools().commit_active_tool_preview()");
  expect_contains(tool, "editor_->tools().cancel_active_tool_preview()");
  expect_contains(tool, "editor_->tools().execute_action(");
  expect_contains(tool, "localization->text(id, fallback)");
  expect_contains(tool, "localized_descriptor_label(");
  expect_missing(tool, "qtext(control.label)");
  expect_missing(tool, "qtext(option.label)");

  expect_contains(ui_source, "new QtSettingsDialog(main_window_.get())");
  expect_contains(ui_source,
                  "new QtToolSettingsWindow(main_window_.get())");
  EXPECT_EQ(count_occurrences(
                ui_source,
                "tool_settings_window_->set_localization(localization_)"),
            2u);
}

TEST(UiLayoutGuardrailTests,
     SettingsUsesCategoryPagesAndAlphaAwareColorSwatches) {
  const std::string settings =
      read_source_file("src/ui/qt_settings_dialog.cpp");
  const std::string swatch_header =
      read_source_file("src/ui/qt_color_swatch_button.hpp");
  const std::string swatch =
      read_source_file("src/ui/qt_color_swatch_button.cpp");

  expect_contains(settings, "QListWidget::currentRowChanged");
  expect_contains(settings, "page_stack->setCurrentIndex(row)");
  expect_contains(settings, "interface_category_item->setData(");
  expect_contains(settings, "category_item->setData(");
  expect_contains(settings, "Qt::UserRole");
  expect_contains(settings, "create_options_page(\"interface\")");
  expect_contains(settings, "create_viewport_section(descriptor)");

  expect_contains(settings, "#include \"ui/qt_color_swatch_button.hpp\"");
  expect_contains(settings, "new QtColorSwatchButton(");
  expect_contains(settings, "button.set_swatch_color(swatch_color)");
  expect_contains(settings, "color_opacity_text(");
  expect_contains(settings, "\"ui.settings.color.opacity\"");
  expect_contains(settings, "button.setToolTip(details)");
  expect_contains(settings, "button.setAccessibleDescription(details)");
  expect_missing(settings, "button.setText(");
  expect_contains(swatch_header,
                  "class QtColorSwatchButton final : public QPushButton");
  expect_contains(swatch_header, "void set_swatch_color(QColor color)");
  expect_contains(swatch, "QSizePolicy::Expanding");
  expect_contains(swatch,
                  "button_palette.setColor(QPalette::Button, swatch_color_)");
  expect_contains(swatch, "setPalette(button_palette)");
  expect_contains(swatch, "QColor::HexArgb");
  expect_contains(swatch, "setToolTip(hex)");
  expect_contains(swatch, "setAccessibleDescription(hex)");
  expect_missing(swatch, "setText(");
  expect_missing(swatch, "setIcon(");
  expect_contains(settings, "QColorDialog::ShowAlphaChannel");
  expect_contains(settings, "QColorDialog::DontUseNativeDialog");
  expect_contains(settings, "QColorDialog::currentColorChanged");
  expect_contains(settings, "controller.preview_color(");
  expect_contains(settings, "controller.commit_color(");
  expect_contains(settings, "controller.rollback_color(");
  expect_contains(settings, "const int result = dialog.exec()");
  expect_missing(settings, "QColorDialog::getColor(");

  expect_missing(settings, "setStyleSheet(");
  expect_missing(settings, "setPalette(");
  expect_missing(settings, "QPalette");
  expect_missing(swatch, "setStyleSheet(");
  expect_missing(swatch, "paintEvent(");
}

TEST(UiLayoutGuardrailTests,
     SettingsPersistenceAndRetainedSynchronizationAreEventDriven) {
  const std::string dialog_header =
      read_source_file("src/ui/qt_settings_dialog.hpp");
  const std::string menu_header =
      read_source_file("src/ui/qt_main_menu.hpp");
  const std::string menu = read_source_file("src/ui/qt_main_menu.cpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");
  const std::string ui_settings =
      read_source_file("src/ui/ui_settings.cpp");
  const std::string viewport_settings =
      read_source_file("src/viewport/viewport_settings.cpp");

  expect_contains(dialog_header,
                  "std::function<void(QtSettingsChangeKind)>");
  expect_contains(menu_header, "viewport_settings_changed");
  expect_missing(menu_header, "std::function<void()> settings_changed");
  expect_contains(menu, "context.viewport_settings_changed()");
  expect_contains(menu, "&QMenu::aboutToShow");

  expect_contains(ui_system,
                  "handle_settings_dialog_change(QtSettingsChangeKind kind)");
  expect_contains(ui_system, "QtSettingsChangeKind::ViewportPreview");
  expect_contains(ui_system, "QtSettingsChangeKind::ViewportCommit");
  expect_contains(ui_system, "QtSettingsChangeKind::InterfaceCommit");
  expect_contains(ui_system, "synchronize_settings_surfaces(true)");
  expect_contains(ui_system, "settings_dialog_->isVisible()");
  expect_contains(ui_system, "settings_dialog_->refresh()");
  expect_contains(ui_system, "main_menu_->refresh()");
  expect_contains(ui_system, "Unable to save UI settings to {}");
  expect_contains(ui_system, "Unable to save viewport settings to {}");

  expect_contains(ui_settings, "file.flush()");
  expect_contains(ui_settings, "return file.good()");
  expect_contains(viewport_settings, "file.flush()");
  expect_contains(viewport_settings, "return file.good()");

  const std::size_t frame_refresh =
      ui_system.find("void UISystem::refresh(");
  const std::size_t frame_refresh_end =
      ui_system.find("void UISystem::process_viewport_input(", frame_refresh);
  ASSERT_NE(frame_refresh, std::string::npos);
  ASSERT_NE(frame_refresh_end, std::string::npos);
  const std::string frame_body =
      ui_system.substr(frame_refresh, frame_refresh_end - frame_refresh);
  expect_missing(frame_body, "settings_dialog_->refresh()");
  expect_missing(frame_body, "main_menu_->refresh()");
}

TEST(UiLayoutGuardrailTests,
     AuxiliaryWindowsLocalizeAndRetainStateWithStockCloseChrome) {
  const std::string settings =
      read_source_file("src/ui/qt_settings_dialog.cpp");
  const std::string tool =
      read_source_file("src/ui/qt_tool_settings_window.cpp");
  const std::string auxiliary_state =
      read_source_file("src/ui/qt_auxiliary_window_state.cpp");
  const std::string ui_settings =
      read_source_file("src/ui/ui_settings.cpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(tool, "#include \"core/localization_service.hpp\"");
  expect_contains(tool, "\"ui.panel.tool_settings\"");
  expect_contains(tool, "\"ui.action.apply\"");
  expect_contains(tool, "\"ui.action.cancel\"");
  expect_contains(ui_system,
                  "tool_settings_window_->set_localization(localization_)");

  expect_contains(auxiliary_state, "QSettings::IniFormat");
  expect_contains(auxiliary_state,
                  "QStringLiteral(\"SettingsDialog\")");
  expect_contains(auxiliary_state,
                  "QStringLiteral(\"ToolSettingsWindow\")");
  expect_contains(auxiliary_state, "QStringLiteral(\"geometry\")");
  expect_contains(auxiliary_state, "QStringLiteral(\"visible\")");
  expect_contains(ui_settings, "\"qt_auxiliary_windows.ini\"");
  expect_contains(ui_system, "settings_dialog_->restoreGeometry(");
  expect_contains(ui_system, "tool_settings_window_->restoreGeometry(");
  expect_contains(ui_system, "settings_dialog_->saveGeometry()");
  expect_contains(ui_system, "tool_settings_window_->saveGeometry()");
  expect_contains(ui_system,
                  "auxiliary_window_state_.settings_dialog_visible");
  expect_contains(ui_system,
                  "auxiliary_window_state_.tool_settings_visible");

  // QDialog's normal platform title bar supplies the requested close button.
  expect_contains(settings, ": QDialog(parent)");
  expect_missing(settings, "setWindowFlags(");
  expect_missing(settings, "setWindowFlag(");
  expect_missing(settings, "Qt::FramelessWindowHint");
  expect_missing(settings, "Qt::CustomizeWindowHint");
}

TEST(UiLayoutGuardrailTests,
     StableObjectNamesRemainSeparateFromLocalizedVisibleTitles) {
  const std::string panel_ids = read_source_file("src/ui/ui_panel.hpp");
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");
  const std::string menu = read_source_file("src/ui/qt_main_menu.cpp");

  for (std::string_view stable_id :
       {"return \"viewport\"", "return \"outliner\"",
        "return \"inspector\"", "return \"console\""}) {
    expect_contains(panel_ids, stable_id);
  }
  expect_contains(ui_source, "dock->setObjectName(");
  expect_contains(ui_source, "ui_panel_stable_id(id)");
  expect_contains(ui_source, "dock->setWindowTitle(");
  expect_contains(ui_source, "ui_panel_localization_key(id)");
  expect_contains(menu, "QStringLiteral(\"command_%1\")");
  expect_contains(menu, "QStringLiteral(\"panel_%1\")");
  expect_contains(menu, "QStringLiteral(\"layout_%1\")");
  expect_contains(menu, "viewport_layout_stable_id(option.id)");
}

TEST(UiLayoutGuardrailTests, QtViewportSupportsOneThroughFourPaneLayouts) {
  const std::string layout_types =
      read_source_file("src/api/quader_viewport_layout_types.hpp");
  const std::string layout =
      read_source_file("src/viewport/viewport_layout.cpp");
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string menu = read_source_file("src/ui/qt_main_menu.cpp");

  expect_contains(layout_types, "enum class ViewportLayoutId");
  for (std::string_view layout_id :
       {"ViewportLayoutId::Single", "ViewportLayoutId::TwoHorizontal",
        "ViewportLayoutId::TwoVertical",
        "ViewportLayoutId::ThreePrimaryRight",
        "ViewportLayoutId::ThreePrimaryLeft",
        "ViewportLayoutId::ThreePrimaryBottom",
        "ViewportLayoutId::ThreePrimaryTop",
        "ViewportLayoutId::FourQuad"}) {
    expect_contains(layout, layout_id);
  }
  expect_contains(layout, "kViewportLayoutOptions");
  expect_contains(panel_header,
                  "std::array<Pane, kViewportPaneCount> panes_");
  expect_contains(panel, "ViewportPaneId::Perspective");
  expect_contains(panel, "ViewportPaneId::Top");
  expect_contains(panel, "ViewportPaneId::Front");
  expect_contains(panel, "ViewportPaneId::Side");
  EXPECT_GE(count_occurrences(panel, "workspace_->active_pane_placements()"),
            3u);
  expect_contains(panel, "qt_apply_viewport_pane_activation(");
  expect_contains(panel, "panes_[index].frame->setVisible(");
  expect_contains(panel, "visible && !startup_presentation_blocked_");
  expect_contains(panel, "active[index] = true");
  expect_contains(menu, "viewport_layout_options()");
  expect_contains(menu, "editor.viewports().set_layout(layout)");
}

TEST(UiLayoutGuardrailTests,
     QtViewportRestoresPerPaneInteractiveNavigationGizmos) {
  const std::string header =
      read_source_file("src/ui/qt_navigation_gizmo.hpp");
  const std::string gizmo =
      read_source_file("src/ui/qt_navigation_gizmo.cpp");
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string cmake = read_source_file("CMakeLists.txt");

  expect_contains(header,
                  "class QtNavigationGizmo final : public QFrame");
  expect_contains(header,
                  "class QtNavigationGizmoInteraction final");
  expect_contains(gizmo,
                  "quader_editor_core::build_view_cube_primitives(");
  expect_contains(gizmo, "quader_editor_core::pick_view_cube(");
  expect_contains(gizmo, "camera.update_drag_delta(delta)");
  expect_contains(gizmo, "camera.view_axis(");
  expect_contains(gizmo, "ViewportAxisLock::Unlocked");
  expect_contains(gizmo, "grabMouse()");
  expect_contains(gizmo, "releaseMouse()");
  expect_contains(
      gizmo,
      "qt_navigation_gizmo_camera_refresh_required(camera_snapshot_");
  expect_contains(
      gizmo,
      "qt_navigation_gizmo_visibility_refresh_required(gizmo_visible_");
  expect_contains(gizmo,
                  "camera_snapshot_ = "
                  "qt_navigation_gizmo_camera_snapshot(camera_)");

  expect_contains(panel_header,
                  "QtNavigationGizmo *navigation_gizmo = nullptr");
  expect_contains(panel, "new QtNavigationGizmo(pane_state.id");
  expect_contains(panel, "editor.preferences().navigation_gizmo_visible()");
  expect_contains(panel, "workspace_->focus_pane(pane_id)");
  expect_contains(panel, "navigation_gizmo_locked_pane");
  expect_contains(panel, "navigation_gizmo_consumed");
  expect_contains(cmake, "src/ui/qt_navigation_gizmo.cpp");
  expect_contains(cmake, "tests/qt_navigation_gizmo_tests.cpp");

  expect_missing(gizmo, "ImGui");
  expect_missing(gizmo, "SDL");
  expect_missing(gizmo, "setStyleSheet(");
  expect_missing(gizmo, "setPalette(");
}

TEST(UiLayoutGuardrailTests,
     QtViewportRestoresFlyHudAndFocusedPanePresentation) {
  const std::string hud_header =
      read_source_file("src/ui/qt_viewport_fly_navigation_hud.hpp");
  const std::string hud =
      read_source_file("src/ui/qt_viewport_fly_navigation_hud.cpp");
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string cmake = read_source_file("CMakeLists.txt");

  expect_contains(hud_header,
                  "QtViewportFlyNavigationHudPresentation");
  expect_contains(hud,
                  "viewport.fly_navigation.speed_with_reset");
  expect_contains(hud, "colors.shadow().color()");
  expect_contains(hud, "colors.highlightedText().color()");
  expect_contains(hud, "setAttribute(Qt::WA_NativeWindow)");
  expect_contains(panel_header, "QFocusFrame *focused_pane_frame_");
  expect_contains(panel, "new QFocusFrame(this)");
  expect_contains(panel, "placements.size() <= 1u");
  expect_contains(panel, "focused_pane_frame_->setWidget(target)");
  expect_contains(panel, "result.fly_hud_active");
  expect_contains(panel, "camera.fly_speed_is_default()");
  expect_contains(cmake,
                  "src/ui/qt_viewport_fly_navigation_hud.cpp");
  expect_contains(cmake,
                  "tests/qt_viewport_fly_navigation_hud_tests.cpp");

  expect_missing(hud, "setStyleSheet(");
  expect_missing(hud, "setPalette(");
  expect_missing(hud, "QColor");
  expect_missing(hud, "ImGui");
  expect_missing(hud, "SDL");
}

TEST(UiLayoutGuardrailTests,
     NativeViewportActivationPrecedesGeometryAndPresentation) {
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::size_t geometry =
      panel.find("void QtViewportPanel::update_pane_geometry()");
  const std::size_t overlay_geometry =
      panel.find("void QtViewportPanel::update_pane_overlay_geometry(",
                 geometry);
  ASSERT_NE(geometry, std::string::npos);
  ASSERT_NE(overlay_geometry, std::string::npos);
  const std::string body =
      panel.substr(geometry, overlay_geometry - geometry);

  const std::size_t activation =
      body.find("qt_apply_viewport_pane_activation(");
  const std::size_t visibility =
      body.find("frame->setVisible(", activation);
  const std::size_t geometry_sync =
      body.find("frame->setGeometry(", activation);
  ASSERT_NE(activation, std::string::npos);
  ASSERT_NE(visibility, std::string::npos);
  ASSERT_NE(geometry_sync, std::string::npos);
  EXPECT_LT(visibility, geometry_sync);
  expect_contains(body, "active[index] = true");
  expect_contains(body, "visible && !startup_presentation_blocked_");
  expect_missing(body, "pane_state.frame->isVisible()");
  expect_missing(body, "pane_state.frame->show()");
  expect_missing(body, "pane_state.frame->hide()");

  const std::string overlay_body = panel.substr(overlay_geometry);
  expect_contains(overlay_body,
                  "pane_state.surface_container->setGeometry(");
  expect_missing(overlay_body, "pane_state.surface->resize(");
}

TEST(UiLayoutGuardrailTests,
     EveryQtViewportPaneOwnsADirectNativeFilamentSurface) {
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string surface_header =
      read_source_file("src/ui/qt_viewport_surface.hpp");
  const std::string surface =
      read_source_file("src/ui/qt_viewport_surface.cpp");

  expect_contains(surface_header,
                  "class QtViewportSurface final : public QWindow");
  expect_contains(surface_header, "RenderAdditionalSwapChain swap_chain_");
  expect_contains(panel, "new QtViewportSurface(pane_state.id)");
  expect_contains(panel, "QWidget::createWindowContainer(");
  expect_contains(surface, "(void)winId()");
  expect_contains(surface, "native_window_handle()");
  expect_contains(surface, "create_additional_swap_chain(");
  expect_contains(surface, "begin_frame(swap_chain_)");
  expect_contains(surface, "render_viewport(renderer_, swap_chain_)");
  expect_contains(surface, "end_frame(swap_chain_)");
  expect_contains(surface, "QEvent::PlatformSurface");
  expect_contains(surface, "QPlatformSurfaceEvent::SurfaceCreated");
  expect_contains(surface,
                  "QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed");
  expect_contains(surface, "QEvent::DevicePixelRatioChange");
  expect_contains(surface, "swap_chain_.reset()");
  expect_missing(surface, "ImGui");
  expect_missing(surface, "SDL");
}

TEST(UiLayoutGuardrailTests,
     NativeViewportRendererSizeSynchronizesBeforeCameraAndSubmission) {
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string surface_header =
      read_source_file("src/ui/qt_viewport_surface.hpp");
  const std::string surface =
      read_source_file("src/ui/qt_viewport_surface.cpp");

  const std::size_t prepare =
      panel.find("void QtViewportPanel::prepare_viewports(");
  const std::size_t size_sync =
      panel.find("surface->synchronize_render_size()", prepare);
  const std::size_t workspace_size =
      panel.find("surface->render_size_pixels()", size_sync);
  const std::size_t camera_update =
      panel.find("renderer.update_camera(", workspace_size);
  ASSERT_NE(prepare, std::string::npos);
  ASSERT_NE(size_sync, std::string::npos);
  ASSERT_NE(workspace_size, std::string::npos);
  ASSERT_NE(camera_update, std::string::npos);
  EXPECT_LT(size_sync, workspace_size);
  EXPECT_LT(workspace_size, camera_update);

  const std::size_t render_frame =
      surface.find("bool QtViewportSurface::render_frame()");
  const std::size_t submission_sync =
      surface.find("synchronize_render_size()", render_frame);
  const std::size_t readiness =
      surface.find("if (!ready_to_render())", submission_sync);
  const std::size_t begin_frame =
      surface.find("render_system_->begin_frame(", readiness);
  ASSERT_NE(render_frame, std::string::npos);
  ASSERT_NE(submission_sync, std::string::npos);
  ASSERT_NE(readiness, std::string::npos);
  ASSERT_NE(begin_frame, std::string::npos);
  EXPECT_LT(submission_sync, readiness);
  EXPECT_LT(readiness, begin_frame);

  expect_contains(surface_header, "void synchronize_render_size()");
  expect_contains(surface, "renderer_.width() == width");
  expect_contains(surface, "renderer_.height() == height");
}

TEST(UiLayoutGuardrailTests,
     QtViewportFrameRefreshKeepsNativeLayoutAndControlsCached) {
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string surface =
      read_source_file("src/ui/qt_viewport_surface.cpp");

  expect_contains(panel_header,
                  "std::vector<ViewportPanePlacement> displayed_placements_");
  expect_contains(panel, "same_pane_placements(displayed_placements_, "
                         "placements)");
  expect_contains(panel, "if (geometry_changed)");
  expect_contains(panel, "sync_pane_control_selection(pane_state)");
  EXPECT_EQ(count_occurrences(panel, "pane_state.view_combo->clear()"), 1u);
  EXPECT_EQ(count_occurrences(panel, "pane_state.shading_combo->clear()"),
            1u);

  // Detailed cadence probes remain available for regression diagnosis, but
  // production frames must not pay their clock/logging cost.
  expect_contains(panel, "std::getenv(\"QUADER_FRAME_DIAGNOSTICS\")");
  expect_contains(surface, "std::getenv(\"QUADER_FRAME_DIAGNOSTICS\")");
  expect_missing(panel, "LOG_WARN(\n        \"Qt viewport cadence");
  expect_missing(surface, "LOG_WARN(\n        \"Qt viewport surface stages");
}

TEST(UiLayoutGuardrailTests,
     QtViewportInputMutationsPrecedeSceneSyncAndOverlayPreparation) {
  const std::string app = read_source_file("src/app/app.cpp");
  const std::string ui_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");

  expect_contains(ui_header, "void process_viewport_input()");
  expect_contains(ui, "viewport_panel_->process_input(");
  expect_contains(panel_header, "void process_input(QuaderEditorApi &editor");

  const std::size_t render_frame = app.find("bool App::render_frame(");
  const std::size_t refresh = app.find("ui_.refresh(", render_frame);
  const std::size_t input =
      app.find("ui_.process_viewport_input()", refresh);
  const std::size_t scene_sync = app.find(
      "QuaderRenderApiRuntimeAccess::sync_scene", input);
  const std::size_t prepare =
      app.find("ui_.prepare_viewports(", scene_sync);
  const std::size_t prepare_with_cache =
      app.find("ui_.prepare_viewports(render_bridge_, "
               "render_overlay_snapshot_cache_)",
               scene_sync);
  ASSERT_NE(render_frame, std::string::npos);
  ASSERT_NE(refresh, std::string::npos);
  ASSERT_NE(input, std::string::npos);
  ASSERT_NE(scene_sync, std::string::npos);
  ASSERT_NE(prepare, std::string::npos);
  ASSERT_NE(prepare_with_cache, std::string::npos);
  EXPECT_EQ(prepare, prepare_with_cache);
  EXPECT_LT(refresh, input);
  EXPECT_LT(input, scene_sync);
  EXPECT_LT(scene_sync, prepare);

  const std::size_t prepare_definition =
      panel.find("void QtViewportPanel::prepare_viewports(");
  const std::size_t next_definition = panel.find(
      "void QtViewportPanel::refresh_cached_selection_overlay_frame(",
      prepare_definition);
  ASSERT_NE(prepare_definition, std::string::npos);
  ASSERT_NE(next_definition, std::string::npos);
  const std::string prepare_body =
      panel.substr(prepare_definition, next_definition - prepare_definition);
  expect_missing(prepare_body, "process_viewport_input(");
}

TEST(UiLayoutGuardrailTests,
     QtViewportContextMenuPreservesReleasePickingAndCommandRouting) {
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string presenter =
      read_source_file("src/ui/qt_viewport_context_menu.cpp");

  expect_contains(panel_header, "QtViewportContextMenuPresenter "
                                "context_menu_presenter_");
  expect_contains(panel, "!input.modifiers.primary");
  expect_contains(panel, "!input.modifiers.shift");
  expect_contains(panel, "!input.modifiers.alt");
  expect_contains(panel, "is_viewport_context_menu_click_gesture(");
  expect_contains(panel, "input_router_.active_tool_allows_selection_fallback()");
  expect_contains(panel, "interaction_controller_.pick_context_menu_target(");
  expect_contains(panel, "should_open_viewport_context_menu(");
  expect_contains(panel, "context_menu_presenter_.show(");

  const std::size_t process =
      panel.find("void QtViewportPanel::process_viewport_input(");
  const std::size_t after_tool =
      panel.find("interaction_controller_.handle_after_active_tool(", process);
  const std::size_t pick =
      panel.find("interaction_controller_.pick_context_menu_target(",
                 after_tool);
  const std::size_t release_capture =
      panel.find("candidate.surface->set_pointer_capture(", pick);
  const std::size_t show =
      panel.find("context_menu_presenter_.show(", release_capture);
  ASSERT_NE(process, std::string::npos);
  ASSERT_NE(after_tool, std::string::npos);
  ASSERT_NE(pick, std::string::npos);
  ASSERT_NE(release_capture, std::string::npos);
  ASSERT_NE(show, std::string::npos);
  EXPECT_LT(after_tool, pick);
  EXPECT_LT(pick, release_capture);
  EXPECT_LT(release_capture, show);

  expect_contains(presenter,
                  "modeling.context_actions(query)");
  expect_contains(presenter,
                  "editor.commands().command_id_for_tool_activation(");
  expect_contains(presenter,
                  "editor.commands().command_id_for_modeling_action(");
  expect_contains(presenter, "editor.commands().execute(command_id)");
  expect_contains(presenter, "input_router.set_selection_mode(");
  expect_missing(presenter, "ImGui");
}

TEST(UiLayoutGuardrailTests,
     QtMenusDisplayCanonicalShortcutsWithoutOwningQActionBindings) {
  const std::string menu_header =
      read_source_file("src/ui/qt_main_menu.hpp");
  const std::string menu = read_source_file("src/ui/qt_main_menu.cpp");
  const std::string commands =
      read_source_file("src/api/quader_commands_api.cpp");

  expect_contains(menu_header, "without owning shortcuts");
  expect_contains(menu_header,
                  "Keyboard dispatch remains owned by QuaderKeybindingService");
  expect_contains(menu, "editor.commands().execute(command_id)");
  expect_contains(menu, "editor.commands().available()");
  expect_contains(menu, "command->shortcut");
  expect_contains(menu, "QLatin1Char('\\t')");
  expect_contains(menu, "entry.action->setText(action_text(");
  for (std::string_view axis_key :
       {"ui.menu.align.axis_x", "ui.menu.align.axis_y",
        "ui.menu.align.axis_z"}) {
    expect_contains(menu, axis_key);
  }
  expect_contains(
      menu, "align_menu->addMenu(qtext(context.localization, key, fallback))");

  for (std::string_view forbidden :
       {"setShortcut(", "setShortcuts(", "setShortcutContext(",
        "QShortcut", "QKeySequence"}) {
    expect_missing(menu, forbidden);
    expect_missing(menu_header, forbidden);
  }

  expect_contains(commands, "QuaderKeybindingService keybindings_");
  expect_contains(commands, "keybindings_.dispatch(chord, context)");
  expect_contains(commands, "keybindings_.resolve_sequence({chord}, context)");
  expect_contains(commands, "dispatch_viewport_input_binding(");
}

TEST(UiLayoutGuardrailTests,
     QtViewportInputRoutesCanonicalChordsThroughQuaderKeybindings) {
  const std::string adapter_header =
      read_source_file("src/ui/qt_input_adapter.hpp");
  const std::string adapter =
      read_source_file("src/ui/qt_input_adapter.cpp");
  const std::string viewport_sources =
      read_source_file("src/ui/qt_viewport_panel.cpp") +
      read_source_file("src/ui/qt_viewport_surface.cpp");

  expect_contains(adapter_header, "std::optional<QuaderInputChord>");
  expect_contains(adapter, "quader_pressed_input_chord_from_qt_event(");
  expect_contains(adapter, "quader_mouse_input_chord_from_qt_event(");
  expect_contains(adapter, "quader_wheel_input_chord_from_qt_event(");
  expect_contains(adapter_header, "QtWheelInputTranslation");
  expect_contains(adapter, "quader_wheel_input_from_qt_event(");
  expect_contains(adapter, "event.modifiers()");
  expect_missing(adapter, "QAction");
  expect_missing(adapter, "setShortcut(");

  // The active viewport must consume the canonical adapter output and hand it
  // to the commands/keybinding owner; merely compiling the adapter is not
  // sufficient to preserve editor and viewport shortcut behavior.
  expect_contains(viewport_sources,
                  "quader_pressed_input_chord_from_qt_event(");
  expect_contains(viewport_sources, "quader_mouse_input_chord_from_qt_event(");
  expect_contains(viewport_sources, "quader_wheel_input_from_qt_event(");
  expect_contains(viewport_sources, "dispatch_viewport_input_binding(");
}

TEST(UiLayoutGuardrailTests,
     QtCapturedPointerDrainsRawMotionAndRejectsWarpFallbackMotion) {
  const std::string surface =
      read_source_file("src/ui/qt_viewport_surface.cpp");

  const std::size_t frame =
      surface.find("QtViewportSurface::take_input_frame(");
  const std::size_t captured_choice =
      surface.find("frame.input.mouse_delta = pointer_captured()", frame);
  const std::size_t raw_drain =
      surface.find("relative_pointer_input_.take_delta()", captured_choice);
  const std::size_t absolute_frame_delta =
      surface.find(": input_->mouse_delta", raw_drain);
  ASSERT_NE(frame, std::string::npos);
  ASSERT_NE(captured_choice, std::string::npos);
  ASSERT_NE(raw_drain, std::string::npos);
  ASSERT_NE(absolute_frame_delta, std::string::npos);
  EXPECT_LT(captured_choice, raw_drain);
  EXPECT_LT(raw_drain, absolute_frame_delta);

  const std::size_t record_move =
      surface.find("void QtViewportSurface::record_mouse_move(");
  const std::size_t suppress_warp =
      surface.find("if (input_->suppress_warp_event)", record_move);
  const std::size_t suppressed_return =
      surface.find("return;", suppress_warp);
  const std::size_t fallback_accumulation = surface.find(
      "relative_pointer_input_.add_fallback_delta(", suppressed_return);
  const std::size_t record_wheel =
      surface.find("void QtViewportSurface::record_wheel(", record_move);
  ASSERT_NE(record_move, std::string::npos);
  ASSERT_NE(suppress_warp, std::string::npos);
  ASSERT_NE(suppressed_return, std::string::npos);
  ASSERT_NE(fallback_accumulation, std::string::npos);
  ASSERT_NE(record_wheel, std::string::npos);
  EXPECT_LT(suppress_warp, suppressed_return);
  EXPECT_LT(suppressed_return, fallback_accumulation);
  EXPECT_LT(fallback_accumulation, record_wheel);
  const std::string record_move_source =
      surface.substr(record_move, record_wheel - record_move);
  expect_missing(record_move_source, "raw_input_active()");
}

TEST(UiLayoutGuardrailTests,
     QtCapturedPointerVerifiesCaptureAndMonitorsExternalCaptureLoss) {
  const std::string surface =
      read_source_file("src/ui/qt_viewport_surface.cpp");
  const std::string relative =
      read_source_file("src/ui/qt_relative_pointer_input.cpp");
  const std::string platform =
      read_source_file("src/platform/raw_mouse_input.cpp");

  expect_contains(
      surface,
      "platform_pointer_capture_transition_achieved(qt_result,");
  expect_contains(surface, "Qt::QueuedConnection");
  expect_contains(
      surface,
      "input_->right_down && !capture_was_stolen");

  const std::size_t start =
      relative.find("bool QtRelativePointerInput::start_raw_input(");
  const std::size_t stop =
      relative.find("bool QtRelativePointerInput::stop_raw_input(", start);
  const std::size_t filter_install =
      relative.find("installNativeEventFilter(this)", start);
  const std::size_t filter_remove =
      relative.find("removeNativeEventFilter(this)", start);
  ASSERT_NE(start, std::string::npos);
  ASSERT_NE(stop, std::string::npos);
  ASSERT_NE(filter_install, std::string::npos);
  ASSERT_NE(filter_remove, std::string::npos);
  EXPECT_LT(filter_install, stop);
  EXPECT_GT(filter_remove, stop);

  const std::size_t classifier =
      platform.find("PlatformRawMouseInput::classify_native_message(");
  const std::size_t capture_changed =
      platform.find("message->message == WM_CAPTURECHANGED", classifier);
  const std::size_t active_gate =
      platform.find("if (!active_ || message->message != WM_INPUT)",
                    classifier);
  ASSERT_NE(classifier, std::string::npos);
  ASSERT_NE(capture_changed, std::string::npos);
  ASSERT_NE(active_gate, std::string::npos);
  EXPECT_LT(capture_changed, active_gate);

  const std::size_t external_loss = surface.find(
      "void QtViewportSurface::record_native_pointer_capture_lost()");
  const std::size_t next_method =
      surface.find("bool QtViewportSurface::apply_native_mouse_grab(",
                   external_loss);
  ASSERT_NE(external_loss, std::string::npos);
  ASSERT_NE(next_method, std::string::npos);
  const std::string external_loss_source =
      surface.substr(external_loss, next_method - external_loss);
  expect_contains(external_loss_source,
                  "input_->released.push_back(*input_->right_drag)");
  expect_contains(external_loss_source, "input_->right_down = false");
  expect_missing(external_loss_source, "input_->right_released =");
}

TEST(UiLayoutGuardrailTests,
     QtApplicationCommandsYieldToActiveModalAndPopupWidgets) {
  const std::string filter_header =
      read_source_file("src/ui/qt_command_event_filter.hpp");
  const std::string filter =
      read_source_file("src/ui/qt_command_event_filter.cpp");

  expect_contains(filter_header, "modal_or_popup_owns_input()");
  expect_contains(filter, "QApplication::activeModalWidget()");
  expect_contains(filter, "QApplication::activePopupWidget()");
  expect_contains(filter, "if (modal_or_popup_owns_input())");
  expect_contains(filter, "reset_pending_chord()");

  const std::size_t ownership_check =
      filter.find("if (modal_or_popup_owns_input())");
  const std::size_t command_dispatch =
      filter.find("dispatch_viewport_input_binding(");
  ASSERT_NE(ownership_check, std::string::npos);
  ASSERT_NE(command_dispatch, std::string::npos);
  EXPECT_LT(ownership_check, command_dispatch);
}

TEST(UiLayoutGuardrailTests,
     QtApplicationCommandsPreserveCurrentViewportToolContext) {
  const std::string filter_header =
      read_source_file("src/ui/qt_command_event_filter.hpp");
  const std::string filter =
      read_source_file("src/ui/qt_command_event_filter.cpp");
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(filter_header, "QtViewportToolStateProvider");
  expect_contains(filter, "qt_command_dispatch_tool_state(");
  expect_contains(filter, "tool_state_provider_");
  expect_contains(panel_header, "const ViewportToolState &tool_state() const");
  expect_contains(ui_system, "viewport_panel_->tool_state()");
}

TEST(UiLayoutGuardrailTests,
     QtDebugClipboardFailureUsesLocalizedPlatformError) {
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(ui_system, "ui.runtime.platform_unavailable");
  expect_missing(ui_system, "Qt clipboard is unavailable.");
}

TEST(UiLayoutGuardrailTests,
     QtViewportFocusAndOutsideCancellationStayWiredToHostPolicies) {
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel = read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string surface =
      read_source_file("src/ui/qt_viewport_surface.cpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(panel_header, "focus_active_pane()");
  expect_contains(ui_system, "viewport_panel_->focus_active_pane()");
  expect_contains(surface, "ViewportPaneFocusTrigger::Hover");
  expect_contains(surface, "ViewportPaneFocusTrigger::PointerPress");
  expect_contains(surface, "ViewportPaneFocusTrigger::Wheel");
  expect_contains(panel, "viewport_pane_focus_decision({");
  expect_contains(panel, "qApp->installEventFilter(this)");
  expect_contains(panel, "current == pane_state.frame");
  expect_contains(panel,
                  "should_cancel_modal_tool_for_external_pointer({");
  expect_contains(panel, "ViewportToolId::LinearArray");
  expect_contains(panel, "current_tool_status.insert_edge_loop_dragging");
  expect_contains(panel,
                  "should_enable_viewport_overlay_pass({");
  expect_contains(panel, ".tool_overlay_visible = tool_overlay_visible");
}
