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
using quader::test::guardrail::source_slice;

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
      "src/ui/layer_panel.hpp",
      "src/ui/layer_panel.cpp",
      "src/ui/console_panel.hpp",
      "src/ui/console_panel.cpp",
      "src/ui/qt_application_theme.hpp",
      "src/ui/qt_application_theme.cpp",
      "src/ui/qt_color_swatch_button.hpp",
      "src/ui/qt_color_swatch_button.cpp",
      "src/ui/qt_settings_dialog.hpp",
      "src/ui/qt_settings_dialog.cpp",
      "src/ui/qt_settings_controller.hpp",
      "src/ui/qt_settings_controller.cpp",
      "src/ui/qt_tool_settings_panel.hpp",
      "src/ui/qt_tool_settings_panel.cpp",
      "src/ui/qt_face_texture_panel.hpp",
      "src/ui/qt_face_texture_panel.cpp",
      "src/ui/qt_face_texture_canvas.hpp",
      "src/ui/qt_face_texture_canvas.cpp",
      "src/ui/qt_face_texture_controls.hpp",
      "src/ui/qt_face_texture_controls.cpp",
      "src/ui/qt_settings_control_widgets.hpp",
      "src/ui/qt_panel_section_header.hpp",
      "src/ui/qt_viewport_context_menu.hpp",
      "src/ui/qt_viewport_context_menu.cpp",
      "src/ui/qt_semantic_axis_colors.hpp",
      "src/ui/qt_viewport_panel.hpp",
      "src/ui/qt_viewport_panel.cpp",
      "src/ui/qt_viewport_control_strip.hpp",
      "src/ui/qt_viewport_control_strip.cpp",
      "src/ui/qt_viewport_surface.hpp",
      "src/ui/qt_viewport_surface.cpp",
      "src/ui/qt_main_window.hpp",
      "src/ui/qt_main_menu.hpp",
      "src/ui/qt_main_menu.cpp",
      "src/ui/qt_selection_toolbar.hpp",
      "src/ui/qt_selection_toolbar.cpp",
      "src/ui/qt_snapping_toolbar.hpp",
      "src/ui/qt_snapping_toolbar.cpp",
      "src/ui/qt_tool_toolbar.hpp",
      "src/ui/qt_tool_toolbar.cpp",
      "src/ui/qt_viewport_display_toolbar.hpp",
      "src/ui/qt_viewport_display_toolbar.cpp",
      "src/ui/qt_texture_toolbar.hpp",
      "src/ui/qt_texture_toolbar.cpp",
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
     QtShellSelectsFusionAndAppliesObsidianThemeOnce) {
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string theme_header =
      read_source_file("src/ui/qt_application_theme.hpp");
  const std::string theme_source =
      read_source_file("src/ui/qt_application_theme.cpp");
  const std::string shell_sources = joined_sources(qt_shell_source_paths());

  expect_contains(main_source, "#include <QApplication>");
  expect_contains(main_source,
                  "#include \"ui/qt_application_theme.hpp\"");
  expect_contains(main_source, "QApplication qt_application(argc, argv)");
  expect_contains(main_source,
                   "QStyleFactory::create(QStringLiteral(\"Fusion\"))");
  expect_contains(main_source, "QApplication::setStyle(fusion_style)");
  expect_contains(main_source,
                  "quader::apply_qt_obsidian_application_theme(");
  expect_contains(main_source, "fusion_style->standardPalette())");
  expect_contains(theme_header, "qt_obsidian_application_palette(");
  expect_contains(theme_header,
                  "qt_application_palette_for_contrast_preference(");
  expect_contains(theme_header,
                  "apply_qt_application_theme_for_contrast_preference(");
  expect_contains(theme_header, "apply_qt_obsidian_application_theme(");
  expect_contains(theme_source,
                  "style_hints.setColorScheme(Qt::ColorScheme::Dark)");
  expect_contains(theme_source, "style_hints.unsetColorScheme()");
  expect_contains(theme_source,
                  "QAccessibilityHints::contrastPreferenceChanged");
  expect_contains(theme_source, "QApplication::setPalette(");
  expect_missing(main_source, "QApplication::setPalette(");
  EXPECT_EQ(count_occurrences(shell_sources, "QStyleFactory::create("), 1u);
  EXPECT_EQ(count_occurrences(shell_sources, "QApplication::setStyle("), 1u);
  EXPECT_EQ(count_occurrences(shell_sources, "QApplication::setPalette("),
            1u);
  EXPECT_EQ(count_occurrences(main_source, "standardPalette()"), 1u);
  EXPECT_EQ(count_occurrences(theme_source, "standardPalette()"), 1u);
  EXPECT_EQ(count_occurrences(shell_sources, "standardPalette()"), 2u);
  for (const char *global_stylesheet_call : {
           "QApplication::setStyleSheet(",
           "qApp->setStyleSheet(",
           "qt_application.setStyleSheet(",
       }) {
    expect_missing(shell_sources, global_stylesheet_call);
  }

  const std::size_t style_position =
      main_source.find("QApplication::setStyle(fusion_style)");
  const std::size_t theme_position =
      main_source.find("quader::apply_qt_obsidian_application_theme(");
  ASSERT_NE(style_position, std::string::npos);
  ASSERT_NE(theme_position, std::string::npos);
  EXPECT_LT(style_position, theme_position);

  expect_sources_missing(qt_shell_source_paths(),
                         {"ImGui::", "#include <imgui", "#include <SDL",
                          "SDL_"});
}

TEST(UiLayoutGuardrailTests,
     QtApplicationInstallsProcessWideDotDecimalLocaleBeforeWidgets) {
  const std::string main_source = read_source_file("src/main.cpp");
  constexpr std::string_view kLocalePolicy =
      "QLocale::setDefault(QLocale::c())";
  constexpr std::string_view kApplicationConstruction =
      "QApplication qt_application(argc, argv)";

  expect_contains(main_source, "#include <QLocale>");
  expect_contains(main_source, kLocalePolicy);
  EXPECT_EQ(count_occurrences(main_source, kLocalePolicy), 1u);
  ASSERT_NE(main_source.find(kApplicationConstruction), std::string::npos);
  EXPECT_LT(main_source.find(kLocalePolicy),
            main_source.find(kApplicationConstruction));
}

TEST(UiLayoutGuardrailTests,
     MainWindowOwnsCentralViewportAndStandardDockPanels) {
  const std::string ui_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");
  const std::string main_window =
      read_source_file("src/ui/qt_main_window.hpp");
  const std::string panel_manager_header =
      read_source_file("src/ui/ui_panel_manager.hpp");
  const std::string panel_manager_source =
      read_source_file("src/ui/ui_panel_manager.cpp");

  expect_contains(ui_header, "std::unique_ptr<QMainWindow> main_window_");
  expect_contains(ui_source, "std::make_unique<QtMainWindow>()");
  expect_contains(main_window, "void closeEvent(QCloseEvent *event) override");
  expect_contains(main_window, "!close_request_handler_()");
  expect_contains(ui_source, "return allow_main_window_close()");
  expect_contains(ui_source, "confirm_dirty_scene(");
  expect_contains(ui_source,
                  "const QuaderApiVoidResult closed = editor_->project().close()");
  expect_contains(ui_source, "!editor_->project().has_project()");
  expect_missing(ui_source, "quader_command::kProjectSave");
  expect_contains(ui_source, "QStringLiteral(\"quaderMainWindow\")");
  expect_contains(ui_source, "setCentralWidget(central_stack_)");
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
                  "std::array<QDockWidget *, kUiPanelCount> docks_");
  expect_contains(panel_manager_header,
                  "std::array<bool, kUiPanelCount> explicit_visibility_");
  expect_contains(panel_manager_header,
                  "std::array<bool, kUiPanelCount> presented_");
  expect_contains(panel_manager_source, "UIPanelManager::add(");
  expect_contains(panel_manager_source, "UIPanelManager::is_presented(");
  expect_contains(panel_manager_source, "&QDockWidget::visibilityChanged");
  expect_contains(panel_manager_source, "UIPanelManager::set_visible(");
  expect_contains(panel_manager_source, "UIPanelManager::dock(");
  expect_contains(panel_manager_source, "&QAction::triggered");
  expect_contains(panel_manager_source, "event->type() == QEvent::Close");
  expect_contains(panel_manager_source, "explicit_visibility_[slot]");
  expect_missing(panel_manager_source, "panel->isVisible()");
  expect_missing(ui_source, "&QDockWidget::visibilityChanged");
}

TEST(UiLayoutGuardrailTests,
     DockPanelTitleBarsUseScopedPaletteAwareAccessibleChrome) {
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");
  const std::string dock_source =
      read_source_file("src/ui/qt_dock_title_presentation.cpp");
  const std::string create_dock = source_slice(
      ui_source, "QDockWidget &UISystem::create_dock(",
      "void UISystem::restore_window_state()");
  const std::string frame_refresh = source_slice(
      ui_source, "void UISystem::refresh(",
      "void UISystem::process_viewport_input()");

  expect_contains(dock_source,
                  "void install_palette_aware_dock_title_presentation("
                  "QDockWidget &dock)");
  expect_missing(dock_source, "titlebar-close-icon");
  expect_missing(dock_source, "titlebar-normal-icon");
  expect_missing(dock_source, "url(");
  expect_missing(dock_source, ":/quader/icons/dock_");
  expect_contains(dock_source,
                  "qt_dock_title_surface_uses_light_glyph(");
  expect_contains(dock_source, "QtDockTitleGlyphIconEngine");
  expect_contains(dock_source, "QPainterPath path");
  expect_contains(dock_source, "painter->drawLine(");
  expect_contains(dock_source, "painter->drawPath(path)");
  expect_contains(dock_source, "apply_programmatic_dock_title_glyphs(");
  expect_contains(dock_source, "qt_dockwidget_closebutton");
  expect_contains(dock_source, "qt_dockwidget_floatbutton");
  expect_contains(dock_source, "close_button->setIcon(");
  expect_contains(dock_source, "float_button->setIcon(");
  expect_contains(dock_source, "quader-dock-palette");
  expect_contains(dock_source, "QApplication::palette()");
  // Native title rendering and lifecycle icon replacement are covered by
  // QtApplicationStyleTests; title QSS would intercept that shared painter.
  expect_missing(dock_source, "QDockWidget::title {");
  expect_contains(dock_source,
                  "palette.color(QPalette::Active, QPalette::Button)");
  expect_contains(dock_source, ".arg(metrics.px(18))");
  expect_contains(dock_source, ".arg(metrics.px(22))");
  expect_contains(dock_source, "QDockWidget::close-button:hover");
  expect_contains(dock_source, "QDockWidget::float-button:hover");
  expect_contains(dock_source,
                  "palette.color(QPalette::Active, QPalette::Accent)");
  expect_contains(dock_source, "QDockWidget::close-button:pressed");
  expect_contains(dock_source, "QDockWidget::float-button:pressed");
  expect_contains(create_dock,
                  "install_palette_aware_dock_title_presentation(*dock)");
  expect_contains(dock_source, "QGuiApplication::paletteChanged");
  expect_contains(dock_source, "needs_contrast_tile");
  expect_contains(dock_source,
                  "if (dock.styleSheet() != style_sheet)");
  expect_missing(frame_refresh,
                 "install_palette_aware_dock_title_presentation");
  expect_missing(frame_refresh, "setStyleSheet(");
}

TEST(UiLayoutGuardrailTests,
     LoadingPresentationKeepsQtCompositionAndNativeGeometryOwnership) {
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
                                 "UIPanelId::Layers",
                                 "UIPanelId::FaceTexture",
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
  // Loading remains a UI concern. Native App hooks delegate to the same owner
  // for project and scene loading rather than creating competing covers.
  expect_contains(app_source, "ui_.begin_loading_presentation()");
  expect_contains(app_source, "ui_.end_loading_presentation(succeeded)");
  const std::string loading_body = source_slice(
      ui_source, "void UISystem::begin_loading_presentation()",
      "bool UISystem::startup_layout_ready() const");
  expect_missing(loading_body, "setUpdatesEnabled(false)");
  expect_missing(loading_body, "processEvents(");
  expect_missing(loading_body, "QEventLoop");
  expect_contains(ui_source,
                  "viewport_panel_->set_startup_presentation_blocked(true)");
  expect_contains(ui_source, "platform_window_is_maximized(");
  expect_contains(ui_source, "platform_window_client_extent(");
  expect_contains(ui_source, "qt_startup_client_extents_match(");
  expect_contains(ui_source, "startup_layout_ready()");
  expect_contains(ui_source, "startup_layout_geometry_signature(");
  expect_contains(ui_source, "main_window_->devicePixelRatioF()");
  // Elapsed time must never bypass native geometry or actual pane readiness.
  expect_missing(loading_body, "kStartupPresentationTimeout");
  expect_missing(loading_body, "forced_fallback");

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
      viewport_source.find("QtViewportPanel::render_viewports(bool");
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
  const std::size_t geometry_stabilized = viewport_source.find(
      "startup_action == QtStartupPresentationAction::GeometryStabilized",
      startup_gate);
  const std::size_t first_surface_presentation = viewport_source.find(
      "if (!pane_state.surface_presented)", geometry_stabilized);
  const std::size_t startup_geometry_reassert = viewport_source.find(
      "pane_state.surface_container->setGeometry(pane_content_rect(pane_state))",
      first_surface_presentation);
  const std::size_t mark_surface_presented = viewport_source.find(
      "pane_state.surface_presented = true", startup_geometry_reassert);
  ASSERT_NE(geometry_stabilized, std::string::npos);
  ASSERT_NE(first_surface_presentation, std::string::npos);
  ASSERT_NE(startup_geometry_reassert, std::string::npos);
  ASSERT_NE(mark_surface_presented, std::string::npos);
  EXPECT_LT(geometry_stabilized, first_surface_presentation);
  EXPECT_LT(first_surface_presentation, reveal_surface);
  EXPECT_LT(reveal_surface, startup_geometry_reassert);
  EXPECT_LT(startup_geometry_reassert, mark_surface_presented);
  EXPECT_LT(mark_surface_presented, exposure_turn);
  const std::string_view startup_reveal_span(
      viewport_source.data() + geometry_stabilized,
      exposure_turn - geometry_stabilized);
  EXPECT_EQ(count_occurrences(
                startup_reveal_span,
                "pane_state.surface_container->setGeometry("
                "pane_content_rect(pane_state))"),
            1u);
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
     DockPanelsUseStockWidgetsAndStableNames) {
  const std::string outliner_header =
      read_source_file("src/ui/outliner_panel.hpp");
  const std::string outliner = read_source_file("src/ui/outliner_panel.cpp");
  const std::string inspector_header =
      read_source_file("src/ui/inspector_panel.hpp");
  const std::string inspector =
      read_source_file("src/ui/inspector_panel.cpp");
  const std::string layer_header =
      read_source_file("src/ui/layer_panel.hpp");
  const std::string layer = read_source_file("src/ui/layer_panel.cpp");
  const std::string multiresolution_icon =
      read_source_file("src/ui/qt_multiresolution_icon.cpp");
  const std::string console_header =
      read_source_file("src/ui/console_panel.hpp");
  const std::string console = read_source_file("src/ui/console_panel.cpp");

  expect_contains(outliner_header,
                  "class OutlinerPanel final : public QWidget");
  expect_contains(outliner, "public QTreeView");
  expect_contains(outliner, "public QStandardItemModel");
  expect_contains(outliner, "ui_panel_stable_id(UIPanelId::Outliner)");
  expect_contains(outliner, "QStringLiteral(\"outliner.tree\")");
  expect_contains(outliner, "QStringLiteral(\"outliner.model\")");
  expect_contains(outliner, "QStringLiteral(\"outliner.context_menu\")");
  expect_contains(outliner, "tree_->setAlternatingRowColors(true)");
  expect_contains(outliner, "QAbstractItemView::ExtendedSelection");
  expect_contains(outliner, "QAbstractItemView::SelectRows");
  expect_contains(outliner, "selectionModel()->selectedRows(");
  expect_contains(outliner, "QItemSelectionModel::NoUpdate");
  expect_contains(outliner, "cache_.document != document_");
  expect_contains(outliner, "collapsed_entities_.clear()");
  expect_contains(outliner,
                  "document_->hierarchy_presentation_revision()");
  expect_missing(outliner, "document_->content_revision()");
  const std::size_t outliner_revision_gate =
      outliner.find("document_->hierarchy_presentation_revision()");
  const std::size_t outliner_projection =
      outliner.find("std::vector<EntityRecord> next_records");
  ASSERT_NE(outliner_revision_gate, std::string::npos);
  ASSERT_NE(outliner_projection, std::string::npos);
  EXPECT_LT(outliner_revision_gate, outliner_projection);
  expect_contains(outliner, "panel_.setFocusProxy(tree_)");
  expect_missing(outliner, "QAbstractItemView::NoSelection");
  expect_missing(outliner, "entity_range_selection(");
  expect_missing(outliner, "outliner_anchor_");
  expect_missing(outliner, "row_press_handler");
  // Inline row controls retain scene-API mutation ownership without restoring
  // the former checkbox columns or replacing Qt's native selection route.
  expect_contains(outliner, "editor_->scene().set_visible(");
  expect_contains(outliner, "editor_->scene().set_selectable(");
  expect_missing(outliner, "tree_->setCurrentIndex(");
  expect_contains(outliner,
                  "header->setSectionResizeMode(column_index(OutlinerColumn::Name),");
  expect_contains(outliner, "QHeaderView::Stretch");
  expect_missing(outliner, "QHeaderView::ResizeToContents");
  expect_missing(outliner, "setCheckable(");
  expect_contains(outliner, "QAbstractItemView::InternalMove");
  expect_contains(outliner, "editor_->scene().rename(");
  expect_contains(outliner, "editor_->scene().move_entities(");
  expect_contains(outliner, "editor_->scene().set_visibility(");
  expect_contains(outliner, "editor_->scene().set_selectability(");
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
  expect_contains(inspector, "panel_.setFocusProxy(tree_)");
  expect_contains(inspector, "event->reason() == Qt::TabFocusReason");
  expect_missing(inspector, "event->reason() == Qt::MouseFocusReason");
  const std::string environment_popup =
      read_source_file("src/ui/qt_environment_settings_popup.cpp");
  expect_contains(environment_popup, "editor_->scene().set_scene_settings(");
  expect_contains(inspector, "editor_->scene().set_light_component(");
  expect_contains(inspector, "editor_->scene().begin_light_color_edit(");
  expect_contains(inspector, "editor_->scene().update_light_color_edit(");
  expect_contains(inspector, "editor_->scene().finish_light_color_edit(");
  expect_contains(environment_popup, "editor_->scene().begin_environment_color_edit(");
  expect_contains(environment_popup, "editor_->scene().update_environment_color_edit(");
  expect_contains(environment_popup, "editor_->scene().finish_environment_color_edit(");
  expect_missing(environment_popup, "document_->set_scene_settings(");
  expect_missing(inspector, "document_->set_light(");

  expect_contains(layer_header,
                  "class LayerPanel final : public QWidget");
  expect_contains(layer, "public QTreeView");
  expect_contains(layer, "new QStandardItemModel");
  expect_contains(layer, "ui_panel_stable_id(UIPanelId::Layers)");
  expect_contains(layer, "QStringLiteral(\"layers.tree\")");
  expect_contains(layer, "QStringLiteral(\"layers.model\")");
  expect_contains(layer, "QStringLiteral(\"layers.context_menu\")");
  expect_contains(layer, "QAbstractItemView::ExtendedSelection");
  expect_contains(layer, "panel_.setFocusProxy(tree_)");
  expect_missing(layer, "visibility_toggle_handler");
  expect_missing(layer, "lock_toggle_handler");
  expect_contains(layer, "new QtColorSwatchButton");
  expect_contains(layer, "Color = 0");
  expect_contains(layer, "Name = 1");
  expect_contains(layer, "Visible = 2");
  expect_contains(layer, "Locked = 3");
  expect_contains(layer, "QtScalableGlyph::MaterialVisibility");
  expect_contains(layer, "QtScalableGlyph::MaterialVisibilityOff");
  expect_contains(layer, "QtScalableGlyph::MaterialLock");
  expect_contains(layer, "QtScalableGlyph::MaterialLockOpen");
  expect_contains(layer, "layer_add.png");
  expect_contains(layer, "clear_layer_selection()");
  expect_contains(layer, "editor_->layers().all()");
  expect_contains(layer, "editor_->layers().set_locked(");
  expect_contains(layer, "editor_->layers().remove_selection(id)");
  expect_contains(layer, "editor_->layers().revision()");
  expect_contains(layer, "QItemSelectionModel::NoUpdate");
  expect_missing(layer, "commands().available()");
  expect_missing(layer, "document_->set_");
  expect_contains(layer, "qt_multiresolution_icon(");
  expect_contains(multiresolution_icon,
                  "QPainter::CompositionMode_SourceIn");
  expect_contains(multiresolution_icon, "edge_preserving_scale(");
  expect_contains(multiresolution_icon, "QtScalableGlyphIconEngine");
  expect_missing(layer, "QPainterPath");

  expect_contains(console_header,
                  "class ConsolePanel final : public QWidget");
  expect_contains(console, "new QPlainTextEdit");
  expect_contains(console, "new QMenu");
  expect_contains(console, "ui_panel_stable_id(UIPanelId::Console)");
  expect_contains(console, "QStringLiteral(\"console.search\")");
  expect_contains(console, "QStringLiteral(\"console.entries\")");
  expect_contains(console, "panel_.setFocusProxy(command_edit_)");
  expect_contains(console, "entries_view_->setReadOnly(true)");
  expect_contains(console, "QPlainTextEdit::NoWrap");
  expect_missing(console, "new QStandardItemModel");
  const std::size_t revision_gate =
      console.find("const std::uint64_t revision = Logger::memory_revision()");
  const std::size_t incremental_read =
      console.find("Logger::entries_since(source_cursor_)", revision_gate);
  ASSERT_NE(revision_gate, std::string::npos);
  ASSERT_NE(incremental_read, std::string::npos);
  EXPECT_LT(revision_gate, incremental_read);
  expect_contains(console,
                   "source_revision_valid_ && source_revision_ == revision");
  expect_contains(console, "remove_visible_entry(entry->id, state)");
  expect_contains(console, "append_visible_entry(entry)");
  expect_contains(console, "ConsoleModel::accepts_normalized(");
  expect_contains(console, "search_filter_timer_->setSingleShot(true)");
  expect_contains(console,
                  "search_filter_timer_->setInterval("
                  "kConsoleSearchDebounceMs)");
  expect_contains(console, "pending_search_text_ = text.toUtf8().toStdString()");
  const std::size_t source_sync =
      console.find("void synchronize_source_entries()");
  const std::size_t localization =
      console.find("void update_localization()", source_sync);
  ASSERT_NE(source_sync, std::string::npos);
  ASSERT_NE(localization, std::string::npos);
  const std::string source_sync_body =
      console.substr(source_sync, localization - source_sync);
  expect_missing(source_sync_body, "rebuild_entries()");
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
  const std::size_t sync_selection_method =
      outliner.find("  void sync_selection()");
  const std::size_t selection_revision_gate =
      outliner.find("outliner_selection_sync_required(",
                    sync_selection_method);
  const std::size_t selected_entity_projection =
      outliner.find("document_->selected_entities()", selection_revision_gate);
  ASSERT_NE(sync_selection_method, std::string::npos);
  ASSERT_NE(selection_revision_gate, std::string::npos);
  ASSERT_NE(selected_entity_projection, std::string::npos);
  EXPECT_LT(selection_revision_gate, selected_entity_projection);
  expect_contains(outliner, "row_is_revealed(");
  expect_contains(outliner, "document_->primary_selection()");
  expect_contains(outliner, "localized_locale_ !=");
}

TEST(UiLayoutGuardrailTests,
     InspectorUsesAResizableTwoColumnQtPropertyTree) {
  const std::string inspector =
      read_source_file("src/ui/inspector_panel.cpp");

  expect_contains(inspector, "tree_->setColumnCount(2)");
  expect_contains(inspector, "tree_->setUniformRowHeights(false)");
  expect_contains(inspector, "tree_->header()->setSectionsMovable(false)");
  expect_contains(
      inspector,
      "tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive)");
  expect_contains(
      inspector,
      "tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch)");
  expect_contains(inspector, "item->setFirstColumnSpanned(true)");
  expect_contains(inspector, "new QTreeWidgetItem(section.item)");
  expect_contains(inspector,
                  "tree_->setHorizontalScrollBarPolicy("
                  "Qt::ScrollBarAlwaysOff)");
  expect_contains(inspector, "kInspectorMinimumValueColumnWidth");
  expect_contains(inspector, "&QHeaderView::sectionResized");
  expect_contains(inspector, "&QHeaderView::geometriesChanged");
  expect_contains(inspector, "clamp_label_column_width()");
  expect_contains(
      inspector,
      "expanded_sections_.try_emplace(stable_id, default_expanded)");
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
  expect_missing(inspector, "setStyleSheet(");
  expect_missing(inspector, "setPalette(");
  expect_missing(inspector, "->setFont(");
}

TEST(UiLayoutGuardrailTests,
     InspectorMaterialRowsAndComponentModeMatrixStayPurposeBuilt) {
  const std::string inspector =
      read_source_file("src/ui/inspector_panel.cpp");
  const std::string rebuild = source_slice(
      inspector, "void rebuild(const ModelingSelectionSummary &summary)",
      "QTreeWidgetItem *add_summary_label(");
  ASSERT_FALSE(rebuild.empty());

  // Object properties project the scene-owned Edit participants even when no
  // components are selected; material rows still consume the retained snapshot.
  const std::string refresh = source_slice(
      inspector, "void refresh(UIContext &context)",
      "void refresh_transform_preview(UIContext &context)");
  expect_contains(refresh, "context.intent.editor.mode().state()");
  expect_contains(refresh, "mode.participants");
  const std::size_t target_gate = refresh.find("if (targets_changed)");
  const std::size_t mode_snapshot =
      refresh.find("context.intent.editor.mode().state()");
  const std::size_t selection_snapshot =
      refresh.find("next_document->selected_entities()");
  ASSERT_NE(target_gate, std::string::npos);
  ASSERT_NE(mode_snapshot, std::string::npos);
  ASSERT_NE(selection_snapshot, std::string::npos);
  EXPECT_LT(target_gate, mode_snapshot);
  EXPECT_LT(target_gate, selection_snapshot);
  expect_contains(refresh, "inspected_mode_revision_ != mode_revision");
  expect_contains(refresh, "inspected_selection_revision_ != selection_revision");
  expect_contains(refresh, "selection_changed = targets_changed &&");
  expect_missing(refresh, "next_selection = selection_");
  EXPECT_EQ(count_occurrences(refresh, "next_document->selected_entities()"), 1u);
  EXPECT_EQ(count_occurrences(refresh, "context.intent.editor.mode().state()"), 1u);
  expect_contains(rebuild, "if (selection_.empty())");
  EXPECT_EQ(count_occurrences(rebuild, "build_general_section()"), 1u);
  EXPECT_EQ(count_occurrences(rebuild, "build_visibility_section()"), 1u);
  EXPECT_EQ(count_occurrences(rebuild, "build_transform_section()"), 1u);
  EXPECT_EQ(count_occurrences(rebuild, "build_material_section()"), 1u);
  expect_contains(rebuild, "editor_->mode().current() == QuaderEditorMode::Object ||");
  expect_contains(rebuild, "summary.mode == ModelingSelectionMode::Face");
  expect_missing(rebuild, "build_component_selection_summary(");
  expect_contains(inspector, "begin_inspector_transform_preview(");

  const std::string material_rows = source_slice(
      inspector, "void build_material_section()",
      "QCheckBox *add_checkbox(");
  ASSERT_FALSE(material_rows.empty());
  expect_contains(inspector, "constexpr int kInspectorMaterialPreviewSize = 56");
  expect_contains(material_rows,
                  "localized(\"ui.inspector.material\", \"Material\"), true");
  expect_contains(material_rows, "item->setFirstColumnSpanned(true)");
  expect_contains(material_rows, "new InspectorElidedLabel(");
  EXPECT_EQ(count_occurrences(material_rows, "new QPushButton(row)"), 2u);
  EXPECT_EQ(count_occurrences(material_rows, "->setFixedSize("), 3u);
  expect_contains(material_rows,
                  "InspectorMaterialActionIcon::Edit");
  expect_contains(material_rows,
                  "InspectorMaterialActionIcon::Replace");
  expect_missing(material_rows, "new QToolButton");
  expect_missing(material_rows, "QStyle::SP_Arrow");
  expect_missing(material_rows, "drawLine(QPointF(0");

  const std::string replacement_icon = source_slice(
      inspector, "QPixmap material_action_pixmap(",
      "QIcon material_action_icon(");
  ASSERT_FALSE(replacement_icon.empty());
  expect_contains(inspector, "kInspectorMaterialActionRasterExtents");
  expect_contains(inspector, "16, 24, 32, 48, 64, 96, 128, 192, 256, 384");
  expect_contains(replacement_icon, "painter.scale(design_scale, design_scale)");
  expect_contains(replacement_icon, "drawRoundedRect(");
  expect_missing(replacement_icon, "drawPolygon(");
  expect_missing(replacement_icon, "drawPolyline(");

  const std::string dpi_event = source_slice(
      inspector, "bool InspectorPanel::event(QEvent *event)",
      "void InspectorPanel::refresh(UIContext &context)");
  ASSERT_FALSE(dpi_event.empty());
  expect_contains(dpi_event, "QEvent::DevicePixelRatioChange");
  expect_contains(dpi_event, "QEvent::ScreenChangeInternal");
  expect_contains(dpi_event, "refresh_material_preview_rasters()");
  expect_contains(inspector, "preview_device_pixel_ratio");
}

TEST(UiLayoutGuardrailTests,
     MaterialFindReplaceUsesTwoSquareColumnsWithoutTransferArrows) {
  const std::string dialog =
      read_source_file("src/ui/qt_material_find_replace_dialog.cpp");
  const std::string dialog_header =
      read_source_file("src/ui/qt_material_find_replace_dialog.hpp");

  expect_contains(dialog, "constexpr int kPreviewSize = 128");
  EXPECT_EQ(count_occurrences(dialog,
                              "build_column(QtMaterialFindReplaceColumn::"),
            2u);
  expect_contains(dialog, "result.preview->setFixedSize(kPreviewSize,");
  expect_contains(dialog, "result.name->setFixedWidth(kPreviewSize)");
  expect_contains(dialog, "result.browse = new QPushButton(this)");
  expect_contains(dialog, "replace_button_ = new QPushButton(this)");
  expect_contains(dialog, "cancel_button_ = new QPushButton(this)");
  expect_contains(dialog, "setWindowModality(Qt::WindowModal)");
  expect_missing(dialog, "QToolButton");
  expect_missing(dialog, "SP_Arrow");
  expect_missing(dialog, "drawPolygon(");
  expect_missing(dialog, "drawPolyline(");
  expect_missing(dialog_header, "arrow");
}

TEST(UiLayoutGuardrailTests,
     InspectorUsesSharedSemanticAxisColors) {
  const std::string inspector =
      read_source_file("src/ui/inspector_panel.cpp");
  const std::string axis_colors =
      read_source_file("src/ui/qt_semantic_axis_colors.hpp");

  expect_contains(inspector,
                  "#include \"ui/qt_semantic_axis_colors.hpp\"");
  expect_contains(inspector,
                  "apply_qt_semantic_axis_label_color(*axis_label");

  expect_contains(axis_colors, "QPalette::WindowText");
  expect_contains(axis_colors, "QColor(235, 76, 82)");
  expect_contains(axis_colors, "QColor(90, 205, 118)");
  expect_contains(axis_colors, "QColor(78, 142, 255)");
  expect_missing(axis_colors, "QPalette::Button");
  expect_missing(axis_colors, "setStyleSheet(");
}

TEST(UiLayoutGuardrailTests,
     InspectorLiveTransformsGateOwnerQueriesAndRetainUnrelatedPresentation) {
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string inspector = read_source_file("src/ui/inspector_panel.cpp");
  const std::string live_branch = source_slice(
      ui, "if ((edit_drag_active || numeric_interaction_active) &&",
      "if (!edit_drag_active && !numeric_interaction_active &&");
  expect_contains(live_branch, "panel_presented(UIPanelId::Inspector)");
  expect_contains(live_branch, "refresh_transform_preview(context)");
  expect_missing(live_branch, "refresh_material_presentation_cache");
  expect_missing(live_branch, "refresh_inspector_material_presentation");
  expect_missing(live_branch, "mark_presented");
  const std::string preview = source_slice(
      inspector, "void refresh_transform_preview(UIContext &context)",
      "void apply_ui_scale(");
  const auto owner_gate = preview.find(
      "presented_transform_revision_ == document.transform_revision()");
  const auto synchronization = preview.find("synchronize_transform_rows()");
  ASSERT_NE(owner_gate, std::string::npos);
  ASSERT_NE(synchronization, std::string::npos);
  EXPECT_LT(owner_gate, synchronization);
  expect_missing(preview, "selected_entities()");
  expect_missing(preview, "selection_summary()");
  const std::string fields = source_slice(
      inspector, "bool synchronize_transform_rows()",
      "bool refresh_transform_properties()");
  expect_contains(fields, "QSignalBlocker");
  expect_contains(fields, "(*row.values)[axis] == values[index][axis]");
  expect_missing(fields, "rebuild(");
  expect_missing(fields, "clear_content(");
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
     SettingsDialogAndViewportSettingsUseStockControlsAndStableNames) {
  const std::string settings_header =
      read_source_file("src/ui/qt_settings_dialog.hpp");
  const std::string settings =
      read_source_file("src/ui/qt_settings_dialog.cpp");
  const std::string settings_controller =
      read_source_file("src/ui/qt_settings_controller.cpp");
  const std::string tool_header =
      read_source_file("src/ui/qt_tool_settings_panel.hpp");
  const std::string tool =
      read_source_file("src/ui/qt_tool_settings_panel.cpp");
  const std::string control_widgets =
      read_source_file("src/ui/qt_settings_control_widgets.hpp");
  const std::string number_slider_header =
      read_source_file("src/ui/qt_tool_settings_number_slider.hpp");
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");
  const std::string generic_action = source_slice(
      tool, "case QuaderToolSettingControlKind::Action:",
      "row_layout->addWidget(button, 0, 1);");

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
  expect_contains(settings, "new QtDoubleSpinBox");
  expect_contains(settings, "new QCheckBox");
  expect_contains(settings, "new QComboBox");

  expect_contains(tool_header,
                  "class QtToolSettingsPanel final : public QWidget");
  expect_missing(tool, "UIPanelId::ToolSettings");
  expect_contains(tool, "#include \"ui/qt_settings_control_widgets.hpp\"");
  expect_contains(tool, "new QtSettingsSection(");
  expect_contains(control_widgets,
                  "class QtSettingsSection final : public QWidget");
  expect_contains(control_widgets, "new QtPanelSectionHeader(this)");
  expect_missing(tool,
                  "QStringLiteral(\"tool_settings.section.description\")");
  expect_missing(tool,
                  "QStringLiteral(\"tool_settings.reset_defaults\")");
  expect_contains(tool, "QStringLiteral(\"tool_settings.apply\")");
  expect_contains(tool, "QStringLiteral(\"tool_settings.cancel\")");
  expect_contains(tool, "new QCheckBox");
  expect_contains(number_slider_header,
                  "class QtToolSettingsNumberSlider final : public QSlider");
  expect_missing(tool, "new QtToolSettingsNumberSlider");
  expect_contains(tool, "new QtSpinBox(row)");
  expect_contains(tool, "new QtDoubleSpinBox(row)");
  expect_contains(tool, "bind_number_interaction(*spin");
  expect_contains(tool,
                  "std::max(4, decimal_places_for_step(control.step))");
  expect_contains(tool, "dispatch_timer->setTimerType(Qt::PreciseTimer)");
  expect_contains(tool, "binding.pending_number_value = value");
  expect_contains(tool, "const bool pointer_drag =");
  expect_contains(tool, "!binding.number_dispatch_timer->isActive()");
  expect_contains(tool, "binding.number_dispatch_timer->start()");
  expect_contains(tool, "flush_number_dispatch(control_index)");
  expect_contains(tool, "queue_number_dispatch(control_index, value)");
  expect_contains(tool, "&QSpinBox::editingFinished");
  expect_contains(tool, "&QDoubleSpinBox::editingFinished");
  expect_contains(tool, "flush_pending_number_dispatches()");
  expect_contains(tool, "discard_pending_number_dispatches()");
  expect_contains(tool,
                  "cancel_button_->setFocusPolicy(Qt::TabFocus)");
  expect_contains(tool,
                  "QObject::connect(cancel_button_, &QPushButton::pressed, "
                  "&panel_, [this]() {\n"
                  "      discard_pending_number_dispatches();\n"
                  "    })");
  expect_contains(tool, "struct NumberDispatchLifecycle");
  expect_contains(tool, "number_dispatch_lifecycle_is_current(");
  expect_contains(tool, "settings_revision(target_pane_)");
  expect_contains(tool, "selected_tool_revision(target_pane_)");
  expect_contains(tool, "settings_pane() == target_pane_");
  const std::string apply = source_slice(
      tool, "QObject::connect(apply_button_, &QPushButton::clicked",
      "QObject::connect(cancel_button_, &QPushButton::clicked");
  const std::size_t preview_prepare = apply.find("!prepare_lifecycle_intent(");
  const std::size_t preview_commit = apply.find("commit_active_tool_preview()");
  ASSERT_NE(preview_prepare, std::string::npos);
  ASSERT_NE(preview_commit, std::string::npos);
  EXPECT_LT(preview_prepare, preview_commit);
  expect_contains(apply, "QuaderNativeToolSettingsIntent::Apply");
  const std::string prepare = source_slice(
      tool, "  [[nodiscard]] bool prepare_lifecycle_intent(",
      "  void apply_ui_scale(");
  expect_contains(prepare, "flush_pending_number_dispatches()");
  const std::string clear_controls = source_slice(
      tool, "void clear_controls()", "void present_inactive_target()");
  const auto discard = clear_controls.find("discard_pending_number_dispatches()");
  const auto remove_widgets = clear_controls.find("clear_layout(*controls_layout_)");
  ASSERT_NE(discard, std::string::npos);
  ASSERT_NE(remove_widgets, std::string::npos);
  EXPECT_LT(discard, remove_widgets);
  expect_contains(tool_header, "bool numeric_interaction_active() const");
  expect_contains(tool, "new QtDoubleSpinBox");
  expect_contains(tool, "new QRadioButton");
  expect_contains(tool, "new QPushButton");
  expect_contains(tool, "new QGridLayout(row)");
  expect_contains(tool, "row_layout->addWidget(field_label, 0, 0, Qt::AlignLeft");
  expect_contains(tool, "row_layout->addWidget(toggle, 0, 1");
  expect_missing(tool, "maximum_label");
  expect_contains(tool,
                  "option_localization_key = \"tool_settings.\"");
  expect_contains(tool, "editor_->tools().settings_for_pane(pane)");
  expect_missing(tool, "editor_->tools().reset_active_settings()");
  expect_contains(tool, "editor_->tools().commit_active_tool_preview()");
  expect_contains(tool, "editor_->tools().cancel_active_tool_preview()");
  expect_contains(tool, "editor_->tools().execute_action(");
  const std::size_t action_flush =
      generic_action.find("flush_pending_number_dispatches();");
  const std::size_t action_dispatch =
      generic_action.find("execute_pane_action(");
  ASSERT_NE(action_flush, std::string::npos);
  ASSERT_NE(action_dispatch, std::string::npos);
  EXPECT_LT(action_flush, action_dispatch);
  expect_contains(tool, "localization->text(id, fallback)");
  expect_contains(tool, "localized_descriptor_label(");
  expect_missing(tool, "qtext(control.label)");
  expect_missing(tool, "qtext(option.label)");
  expect_contains(tool, "QuaderToolSettingControlKind::Vector3Number");
  expect_contains(tool, "control.group_id");
  expect_contains(tool, "group.expanded_by_default");
  expect_missing(tool, "path_array.");

  expect_contains(ui_source, "new QtSettingsDialog(main_window_.get())");
  expect_contains(ui_source,
                  "new QtToolSettingsPanel(main_window_.get())");
  expect_contains(ui_source,
                  "viewport_panel_->set_settings_panel(tool_settings_panel_)");
  expect_contains(ui_source,
                  "viewport_panel_->refresh_settings_overlay(*editor_)");
  expect_missing(ui_source, "UIPanelId::ToolSettings");
  expect_contains(ui_source,
                  "tool_settings_panel_->numeric_interaction_active()");
  expect_contains(ui_source,
                  "if (!edit_drag_active && "
                  "!numeric_interaction_active) {");
  expect_contains(ui_source,
                  "if (!edit_drag_active && !numeric_interaction_active &&");
  EXPECT_EQ(count_occurrences(
                ui_source,
                "tool_settings_panel_->set_localization(localization_)"),
            2u);
  expect_missing(tool, "setWindowFlag(Qt::Tool");
  expect_missing(tool, "setWindowFlags(Qt::Tool");
  expect_missing(tool, "floating.tool_settings");
}

TEST(UiLayoutGuardrailTests,
     TextureSettingsRetainMixedControlsBehindOwnerRevisionGate) {
  const std::string header =
      read_source_file("src/ui/qt_face_texture_controls.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_face_texture_controls.cpp");
  const std::string widgets =
      read_source_file("src/ui/qt_settings_control_widgets.hpp");
  expect_contains(header, "void refresh()");
  expect_contains(panel,
                  "QStringLiteral(\"face_texture.section.texture\")");
  expect_contains(panel,
                  "QStringLiteral(\"face_texture.section.edit_texture\")");
  expect_contains(widgets,
                  "class QtMixedNumberSpinBox final : public QtDoubleSpinBox");
  expect_contains(widgets, "QStringLiteral(\"--\")");
  expect_missing(widgets, "setSpecialValueText(");
  expect_contains(panel, "editor_->modeling().set_face_texture_setting(");
  expect_contains(panel, ".selection_token = binding.selection_token");
  expect_contains(panel, ".merge_key = binding.merge_key");
  expect_contains(panel, "binding.dispatch_timer->setInterval(kNumberDispatchIntervalMs)");
  expect_contains(panel, "editor_->modeling().apply_face_texture_action(");
  expect_contains(panel, "editor_->modeling().set_face_texture_treat_as_one(");
  expect_contains(panel, "quader_face_texture_action_label(description.action)");
  EXPECT_EQ(count_occurrences(panel, "build_texture_actions();"), 1u);

  const std::string refresh = std::string(source_slice(
      panel, "  void refresh_texture_settings(bool force = false)",
      "  void build_texture_actions()"));
  const std::size_t revision = refresh.find("face_texture_settings_revision()");
  const std::size_t gate = refresh.find("*texture_revision_ == revision");
  const std::size_t summary = refresh.find("face_texture_settings()");
  const std::size_t visibility = refresh.find("texture_section_->setVisible");
  const std::size_t action_visibility =
      refresh.find("edit_texture_section_->setVisible");
  const std::size_t action_tooltips =
      refresh.find("refresh_texture_action_tooltips(rotation_step)");
  ASSERT_NE(revision, std::string::npos);
  ASSERT_NE(gate, std::string::npos);
  ASSERT_NE(summary, std::string::npos);
  ASSERT_NE(visibility, std::string::npos);
  ASSERT_NE(action_visibility, std::string::npos);
  ASSERT_NE(action_tooltips, std::string::npos);
  EXPECT_LT(revision, gate);
  EXPECT_LT(gate, summary);
  EXPECT_LT(summary, visibility);
  EXPECT_LT(summary, action_visibility);
  EXPECT_LT(summary, action_tooltips);
  EXPECT_EQ(count_occurrences(refresh, "face_texture_settings()"), 1u);
  expect_contains(refresh,
                  "texture_keybindings_revision_ == keybindings_revision");
  expect_contains(refresh, "texture_rotation_step_ == rotation_step");
  expect_contains(refresh, "button->isEnabled() != settings.editable");
  expect_contains(refresh,
                  "texture_treat_as_one_->isChecked() != settings.treat_as_one");
  expect_contains(refresh, "if (!binding.editing)");
  expect_missing(refresh, "commands().available()");
  expect_missing(refresh, "export_selection()");
  expect_missing(refresh, "show_panel(");
  expect_missing(refresh, "raise()");

  const std::string dispatch = std::string(source_slice(
      panel, "  bool flush_texture_inputs_for_action()",
      "  void discard_texture_dispatch(TextureControlWidget &binding)"));
  const std::size_t flush = dispatch.find("flush_texture_inputs_for_action()");
  const std::size_t action = dispatch.find("apply_face_texture_action(");
  ASSERT_NE(flush, std::string::npos);
  ASSERT_NE(action, std::string::npos);
  EXPECT_LT(flush, action);
  expect_contains(dispatch, "binding.invalid_value");
  expect_contains(dispatch, "binding.gesture_revision != current");
  expect_contains(dispatch, ".selection_token = selection_token");
  expect_contains(dispatch, "discard_texture_dispatches();");
  expect_missing(dispatch, ".merge_key");
  expect_missing(dispatch, "commands().execute(");
  expect_contains(panel, "button->setFocusPolicy(Qt::TabFocus)");
  expect_contains(panel, "if (!localized_text_dirty_)");
  expect_missing(panel, "commands().available()");
}

TEST(UiLayoutGuardrailTests,
     UvPeelUsesDescriptorSettingsAndSharedPreviewLifecycleBehindOwnerGate) {
  const std::string header =
      read_source_file("src/ui/qt_tool_settings_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_tool_settings_panel.cpp");
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string api = read_source_file("src/api/quader_tools_api.cpp");
  expect_missing(header, "refresh_uv_peel_settings");
  expect_missing(panel, "tool_settings.section.uv_peel");
  expect_missing(panel, "build_uv_peel_controls");
  expect_missing(panel, "modeling().peel_uv(");
  expect_missing(panel, "modeling().set_uv_peel_options(");
  expect_missing(ui, "uv_peel_presenter_");
  expect_contains(api, "QuaderToolSettingNumberPresentation::SpinBoxOnly");
  expect_contains(panel, "QuaderToolSettingNumberPresentation::SpinBoxOnly");
  expect_contains(panel, "control.number_positive_only");
  expect_contains(panel, "has_invalid_number_draft()");
  expect_contains(panel, "apply_button_->setFocusPolicy(Qt::TabFocus)");

  const std::string apply = source_slice(
      panel, "QObject::connect(apply_button_, &QPushButton::clicked",
      "QObject::connect(cancel_button_, &QPushButton::clicked");
  const auto preparation = apply.find("!prepare_lifecycle_intent(");
  const auto commit = apply.find("commit_active_tool_preview()");
  ASSERT_NE(preparation, std::string::npos);
  ASSERT_NE(commit, std::string::npos);
  EXPECT_LT(preparation, commit);
  expect_contains(apply, "QuaderNativeToolSettingsIntent::Apply");

  const std::string prepare = source_slice(
      panel, "  [[nodiscard]] bool prepare_lifecycle_intent(",
      "  void apply_ui_scale(");
  const auto validation = prepare.find("has_invalid_number_draft()");
  const auto interpret = prepare.find("spin->interpretText()");
  const auto flush = prepare.find("flush_pending_number_dispatches()");
  const auto rebind_check =
      prepare.find("return !number_dispatch_failed_ && still_current()");
  ASSERT_NE(validation, std::string::npos);
  ASSERT_NE(interpret, std::string::npos);
  ASSERT_NE(flush, std::string::npos);
  ASSERT_NE(rebind_check, std::string::npos);
  EXPECT_LT(validation, interpret);
  EXPECT_LT(interpret, flush);
  EXPECT_LT(flush, rebind_check);
  expect_contains(prepare, "editor_ == &editor");
  expect_contains(prepare, "generation == binding_generation_");
  expect_contains(prepare, "controls_generation == control_generation_");
  expect_contains(prepare, "presentation_is_current()");
  expect_contains(prepare, "selected_tool_revision(target_pane_) == tool_revision");

  const std::string lifecycle = source_slice(
      panel, "  [[nodiscard]] bool number_dispatch_lifecycle_is_current(",
      "  [[nodiscard]] bool vector3_dispatch_lifecycle_is_current(");
  expect_contains(lifecycle, "binding_generation_ != lifecycle.binding_generation");
  expect_contains(lifecycle, "settings_revision(target_pane_)");
  expect_contains(lifecycle, "selected_tool_revision(target_pane_)");

  const std::string frame = source_slice(
      ui, "    const ToolSettingsPresentationStamp stamp{",
      "  if (panel_presented(UIPanelId::FaceTexture))");
  const auto gate = frame.find("tool_settings_presenter_.needs_refresh(stamp)");
  const auto refresh = frame.find("tool_settings_panel_->refresh()");
  ASSERT_NE(gate, std::string::npos);
  ASSERT_NE(refresh, std::string::npos);
  EXPECT_LT(gate, refresh);
  expect_contains(frame, ".active_settings_revision = frame_tool_settings_revision");
  expect_contains(frame, ".operation_serial = settings_operation_serial");
  expect_contains(frame, "tool_settings_presenter_.mark_presented(stamp)");
  expect_missing(frame, "active_settings()");
  expect_missing(panel, "commands().available()");
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
  expect_contains(settings, "OptionsPageWidgets create_category(");
  expect_contains(settings, "item->setData(Qt::UserRole");
  expect_contains(settings, "Qt::UserRole");
  expect_contains(settings, "create_options_page(stable_id)");
  expect_contains(settings, "create_viewport_section(*page, descriptor)");
  expect_contains(settings, "new qt_settings_detail::QtSettingsSection(");
  expect_contains(settings, "QFormLayout::WrapLongRows");

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
  // Delegates may select inherited palette roles without overriding widgets.
  expect_contains(settings, "option->palette.color(");
  expect_missing(swatch, "setStyleSheet(");
  expect_missing(swatch, "paintEvent(");
}

TEST(UiLayoutGuardrailTests,
     SettingsKeybindingShowcaseUsesPublicCatalogOffTheFrameLoop) {
  const std::string dialog_header =
      read_source_file("src/ui/qt_settings_dialog.hpp");
  const std::string dialog =
      read_source_file("src/ui/qt_settings_dialog.cpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(dialog_header, "class QuaderCommandsApi;");
  expect_contains(dialog_header, "QuaderCommandsApi &commands");
  expect_contains(dialog,
                  "#include \"api/quader_commands_api.hpp\"");
  expect_contains(dialog, "QStringLiteral(\"keybindings\")");
  expect_contains(dialog, "QStringLiteral(\"settingsKeybindingsTable\")");
  expect_contains(dialog, "QStringLiteral(\"settingsKeybindingsSearch\")");
  expect_contains(
      dialog, "QStringLiteral(\"settingsKeybindingsShortcutFilter\")");
  expect_contains(
      dialog, "QStringLiteral(\"settingsKeybindingsShortcutClear\")");
  expect_contains(dialog, "new QtKeybindingTableView(page)");
  expect_contains(dialog, "setAlternatingRowColors(true)");
  expect_contains(dialog,
                  "setSelectionBehavior(QAbstractItemView::SelectRows)");
  expect_missing(dialog, "keybindings_table->setEditTriggers(");
  expect_contains(
      dialog,
      "keybindings_table->setSelectionMode(QAbstractItemView::SingleSelection)");
  expect_contains(dialog, "setDefaultSectionSize(20)");
  expect_contains(dialog,
                  "header->setSectionResizeMode(QHeaderView::Interactive)");
  expect_contains(dialog, "header->setStretchLastSection(true)");
  expect_contains(dialog, "keybindings_table->setSortingEnabled(true)");
  expect_contains(dialog, "Qt::AscendingOrder");
  expect_contains(dialog, "apply_keybinding_filters()");
  expect_contains(dialog, "quader_pressed_input_chord_from_qt_event");
  expect_contains(dialog, "commands->format_input_mapping(chord)");
  expect_contains(dialog, "commands->keybinding_catalog()");
  expect_contains(dialog, "commands->analyze_shortcut_assignment(");
  expect_contains(dialog, "commands->set_action_shortcut(");
  expect_contains(dialog, "commands->remove_action_shortcut(");
  expect_contains(dialog, "commands->reset_action_shortcut(");
  expect_contains(dialog, "commands->reset_all_shortcuts()");
  expect_missing(dialog,
                 "QStringLiteral(\"settingsKeybindingsSaveHint\")");
  expect_missing(dialog,
                 "QStringLiteral(\"settingsKeybindingsRebindHint\")");
  expect_missing(dialog,
                 "QStringLiteral(\"settingsKeybindingsSelectedAction\")");
  expect_missing(dialog,
                 "QStringLiteral(\"settingsKeybindingsDefaultValue\")");
  expect_missing(dialog,
                 "QStringLiteral(\"settingsKeybindingsCurrentValue\")");
  expect_missing(dialog,
                 "QStringLiteral(\"settingsKeybindingsWhenValue\")");
  expect_contains(dialog,
                  "QStringLiteral(\"settingsKeybindingCaptureDialog\")");
  expect_contains(dialog,
                  "QStringLiteral(\"settingsKeybindingConflictDialog\")");
  expect_contains(dialog,
                  "#include \"ui/qt_retained_message_box.hpp\"");
  expect_contains(dialog,
                  "keybinding_message_box = new QtRetainedMessageBox(&owner)");
  expect_contains(dialog, "schedule_keybinding_message_box_prewarm()");
  expect_contains(dialog, "keybinding_message_box->prewarm()");
  expect_contains(dialog,
                  "QtSettingsChangeKind::KeybindingsCommit");
  expect_contains(dialog, "entry.localization_key");
  expect_contains(dialog, "entry.scope_localization_key");
  expect_contains(dialog, "entry.shortcut");
  expect_contains(dialog, "std::stable_sort(");
  expect_contains(dialog, "column_text(left, column)");
  expect_contains(dialog, "natural_numeric_compare(");
  expect_contains(dialog, ".digitValue()");
  expect_missing(dialog, "QCollator");
  expect_missing(dialog, "SourceColumn");
  expect_missing(dialog,
                 "QStringLiteral(\"settingsKeybindingsCurrentMappings\")");
  expect_missing(dialog, "QStringLiteral(\"settingsKeybindingsAdd\")");
  expect_missing(dialog, "QStringLiteral(\"settingsKeybindingsReplace\")");
  expect_missing(dialog, "QStringLiteral(\"settingsKeybindingsRemove\")");
  expect_missing(dialog, "settingsKeybindingConflictKeepBoth");
  expect_missing(dialog, "QuaderKeybindingConflictPolicy::KeepBoth");

  const std::string keybinding_message_paths = source_slice(
      dialog, "void show_keybinding_error(",
      "create_viewport_section(");
  EXPECT_EQ(count_occurrences(keybinding_message_paths,
                              "keybinding_message_box->present("),
            3u);
  expect_missing(keybinding_message_paths, "QMessageBox box(");
  expect_missing(keybinding_message_paths, "QMessageBox confirmation(");
  expect_contains(keybinding_message_paths,
                  ".escape_button = QtRetainedMessageBoxButton::Primary");
  expect_contains(keybinding_message_paths,
                  ".close_button = QtRetainedMessageBoxButton::Primary");
  EXPECT_EQ(count_occurrences(
                keybinding_message_paths,
                ".escape_button = QtRetainedMessageBoxButton::Tertiary"),
            2u);
  EXPECT_EQ(count_occurrences(
                keybinding_message_paths,
                ".close_button = QtRetainedMessageBoxButton::Tertiary"),
            2u);

  for (std::string_view forbidden : {
           "editor/input/quader_keybinding_service.hpp",
           "editor/metadata/host_contract.hpp",
           "viewport/host_viewport_contract_adapter.hpp",
           "QShortcut",
           "QKeySequence",
           "commands().available()",
           "commands().keybindings()",
           "set_shortcut(",
           "remove_shortcut(",
           "reset_shortcut(",
       }) {
    expect_missing(dialog, forbidden);
    expect_missing(dialog_header, forbidden);
  }

  const std::size_t frame_refresh =
      ui_system.find("void UISystem::refresh(");
  const std::size_t frame_refresh_end = ui_system.find(
      "void UISystem::process_viewport_input(", frame_refresh);
  ASSERT_NE(frame_refresh, std::string::npos);
  ASSERT_NE(frame_refresh_end, std::string::npos);
  const std::string frame_body =
      ui_system.substr(frame_refresh, frame_refresh_end - frame_refresh);
  expect_missing(frame_body, "keybinding_catalog()");
  expect_missing(frame_body, "commands().available()");
  expect_missing(frame_body, "commands().keybindings()");
  expect_missing(frame_body, "settings_dialog_->refresh()");
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
                   "QtSettingsController::ChangeCallback settings_changed");
  expect_contains(menu_header, "viewport_settings_changed");
  expect_missing(menu_header, "std::function<void()> settings_changed");
  expect_missing(menu, "context.viewport_settings_changed()");
  expect_contains(menu, "context.editor.get().commands().execute(command_id)");
  expect_contains(menu, "context.editor.get().viewports().control(");
  const std::string grid_command = source_slice(
      read_source_file("src/api/quader_commands_api.cpp"),
      "register_command(quader_command::kViewportToggleGrid,",
      "register_command(quader_command::kViewportToggleWireframeOnShaded,");
  expect_contains(grid_command, "viewports_.set_control(");
  expect_contains(grid_command, "QuaderViewportControl::ShowGrid");
  expect_missing(grid_command, "preferences_.");
  expect_contains(menu, "&QMenu::aboutToShow");

  expect_contains(ui_system,
                   "handle_settings_dialog_change(const QtSettingsChange &change)");
  expect_contains(ui_system, "QtSettingsChangeKind::ViewportPreview");
  expect_contains(ui_system, "QtSettingsChangeKind::ViewportCommit");
  expect_contains(ui_system, "QtSettingsChangeKind::InterfacePreview");
  expect_contains(ui_system, "QtSettingsChangeKind::InterfaceCommit");
  expect_contains(ui_system, "QtSettingsChangeKind::KeybindingsCommit");
  expect_contains(ui_system,
                  "selection_toolbar_->synchronize(*editor_, active_mode, true)");
  expect_contains(ui_system,
                  "synchronize_tool_toolbar(selection, true)");
  expect_contains(ui_system,
                  "snapping_toolbar_->synchronize(*editor_, true)");
  expect_contains(ui_system,
                  "viewport_display_toolbar_->synchronize(*editor_, true)");
  expect_contains(ui_system,
                  "viewport_panel_->refresh_keybinding_presentation(*editor_)");
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
  expect_missing(frame_body, "synchronize_auxiliary_window_visibility()");
}

TEST(UiLayoutGuardrailTests,
     AuxiliaryWindowsRetainStateWhileViewportSettingsStayTransient) {
  const std::string settings =
      read_source_file("src/ui/qt_settings_dialog.cpp");
  const std::string tool =
      read_source_file("src/ui/qt_tool_settings_panel.cpp");
  const std::string auxiliary_state =
      read_source_file("src/ui/qt_auxiliary_window_state.cpp");
  const std::string ui_settings =
      read_source_file("src/ui/ui_settings.cpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(tool, "#include \"core/localization_service.hpp\"");
  expect_contains(tool, "\"ui.viewport.settings\"");
  expect_contains(tool, "\"ui.action.apply\"");
  expect_contains(tool, "\"ui.action.cancel\"");
  expect_contains(ui_system,
                  "tool_settings_panel_->set_localization(localization_)");

  expect_contains(auxiliary_state, "QSettings::IniFormat");
  expect_contains(auxiliary_state,
                  "QStringLiteral(\"SettingsDialog\")");
  expect_contains(auxiliary_state, "QStringLiteral(\"geometry\")");
  expect_contains(auxiliary_state, "QStringLiteral(\"visible\")");
  expect_contains(ui_settings, "\"qt_auxiliary_windows.ini\"");
  expect_contains(ui_system, "settings_dialog_->restoreGeometry(");
  expect_contains(ui_system, "settings_dialog_->saveGeometry()");
  expect_contains(ui_system, "&QDialog::finished");
  expect_contains(ui_system, "auxiliary_window_save_timer_->setSingleShot(true)");
  expect_contains(ui_system, "schedule_auxiliary_window_state_save()");
  expect_contains(ui_system,
                  "auxiliary_window_state_.settings_dialog_visible");
  expect_contains(ui_system,
                  "main_window_->saveState(kQtWindowStateVersion)");
  expect_contains(ui_system,
                  "main_window_->restoreState(state, kQtWindowStateVersion)");
  expect_missing(auxiliary_state, "ToolSettingsWindow");
  expect_missing(ui_system, "tool_settings_geometry");
  expect_missing(ui_system, "auxiliary_window_state_.tool_settings");

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
        "return \"inspector\"", "return \"console\"", "return \"layers\"",
        "return \"face_texture\""}) {
    expect_contains(panel_ids, stable_id);
  }
  expect_contains(ui_source, "dock->setObjectName(");
  expect_contains(ui_source, "ui_panel_stable_id(id)");
  expect_contains(ui_source, "dock->setWindowTitle(");
  expect_contains(ui_source, "ui_panel_localization_key(id)");
  expect_contains(menu, "QStringLiteral(\"command_%1\")");
  expect_contains(menu, "QStringLiteral(\"panel_%1\")");
}

TEST(UiLayoutGuardrailTests,
     ToolbarGroupDividersStayDistinctFromSubtleChromeEdges) {
  const std::string button_presentation =
      read_source_file("src/ui/qt_toolbar_button_presentation.cpp");
  const std::string main_window =
      read_source_file("src/ui/qt_main_window.hpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(button_presentation,
                  "element == PE_IndicatorToolBarSeparator");
  const std::string divider = source_slice(
      button_presentation, "if (element == PE_IndicatorToolBarSeparator",
      "if (element != PE_PanelButtonCommand");
  expect_contains(divider, "QPalette::Midlight");
  expect_contains(divider, "qt_application_style_active(option)");
  expect_contains(button_presentation, "PM_ToolBarSeparatorExtent");
  expect_contains(button_presentation,
                  "option->palette.brush(QPalette::Dark)");
  expect_contains(button_presentation,
                  "option->palette.brush(QPalette::Window)");
  expect_contains(button_presentation, "bounds.top()");
  expect_contains(button_presentation, "element != CE_ToolBar");
  expect_contains(button_presentation,
                  "toolbar->orientation() == Qt::Horizontal");
  expect_contains(button_presentation, "host.setStyle(presentation)");
  expect_contains(button_presentation,
                  "static_cast<QWidget &>(toolbar)");
  expect_contains(main_window, "QMainWindow::paintEvent(event)");
  expect_contains(main_window, "content->geometry().top() - extent");
  expect_contains(main_window, "palette().brush(QPalette::Midlight)");
  expect_contains(ui_system, "class QtMainMenuBar final : public QMenuBar");
  expect_contains(ui_system, "QMenuBar::paintEvent(event)");
  expect_contains(ui_system, "class QtMainStatusBar final : public QStatusBar");
  expect_contains(ui_system, "palette().brush(QPalette::Midlight)");
}

TEST(UiLayoutGuardrailTests,
     SelectionToolbarUsesNativeMovableChromeAndPublicSelectionIntent) {
  const std::string toolbar_header =
      read_source_file("src/ui/qt_selection_toolbar.hpp");
  const std::string toolbar =
      read_source_file("src/ui/qt_selection_toolbar.cpp");
  const std::string button_presentation =
      read_source_file("src/ui/qt_toolbar_button_presentation.cpp");
  const std::string menu_header =
      read_source_file("src/ui/qt_main_menu.hpp");
  const std::string menu = read_source_file("src/ui/qt_main_menu.cpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(toolbar_header,
                  "class QtSelectionToolbar final : public QToolBar");
  expect_contains(toolbar, "setAllowedAreas(Qt::AllToolBarAreas)");
  expect_contains(toolbar, "setMovable(true)");
  expect_contains(toolbar, "setFloatable(true)");
  expect_contains(toolbar, "setIconSize(QSize(32, 32))");
  expect_contains(toolbar, "kSelectionModeButtonHeight = 30");
  expect_contains(toolbar,
                  "button->setFixedHeight(kSelectionModeButtonHeight)");
  expect_contains(toolbar, "action_group->setExclusive(true)");
  for (std::string_view mode :
       {"QuaderSelectionMode::Vertex", "QuaderSelectionMode::Edge",
        "QuaderSelectionMode::Face", "QuaderSelectionMode::Object",
        "QuaderSelectionMode::Group"}) {
    expect_contains(toolbar, mode);
  }
  for (std::string_view icon :
       {"selection_vertex.png", "selection_edge.png", "selection_face.png",
        "selection_group.png"}) {
    expect_contains(toolbar, icon);
  }
  expect_contains(ui_system,
                  "addToolBar(Qt::TopToolBarArea, selection_toolbar_)");
  expect_contains(ui_system, "editor_->selection().set_mode(mode)");
  expect_contains(ui_system,
                  "selection_toolbar_->synchronize(*editor_, active_mode)");
  expect_contains(toolbar_header,
                  "void synchronize(const QuaderEditorApi &editor");
  expect_contains(
      ui_system,
      "selection_toolbar_->synchronize(*editor_, active_mode, true)");
  expect_contains(toolbar, "if (!force && synchronization_valid_");
  expect_contains(toolbar,
                  "keybinding_target_id_for_selection_mode(entry.mode)");
  expect_contains(toolbar, "editor.commands().shortcut_for(target_id)");
  expect_missing(toolbar, "editor.commands().available()");
  const std::size_t unchanged_state_gate =
      toolbar.find("if (!force && synchronization_valid_");
  const std::size_t shortcut_lookup =
      toolbar.find("editor.commands().shortcut_for(target_id)");
  ASSERT_NE(unchanged_state_gate, std::string::npos);
  ASSERT_NE(shortcut_lookup, std::string::npos);
  EXPECT_LT(unchanged_state_gate, shortcut_lookup);
  expect_contains(toolbar, "QStringLiteral(\" [%1]\")");
  expect_contains(toolbar,
                  "configure_native_toolbar_button_face(*this, *entry.action,");
  expect_contains(button_presentation,
                  "configure_native_toolbar_button_face(*button, action, presentation)");
  expect_contains(button_presentation, "button.setAutoRaise(false)");
  expect_contains(button_presentation,
                  "button.setAttribute(Qt::WA_Hover, true)");
  expect_contains(button_presentation,
                  "face.palette.color(color_group, QPalette::Highlight)");
  expect_contains(button_presentation,
                  "qt_toolbar_button_color_group(face.state)");
  expect_contains(button_presentation, "const bool keyboard_focused");
  expect_contains(button_presentation,
                  "QPen outline(accent, keyboard_focused ? 2.0 : 1.0)");
  expect_contains(button_presentation,
                  "state.testFlag(QStyle::State_Active) ? QPalette::Active");
  expect_contains(button_presentation, "return QPalette::Disabled");
  expect_missing(button_presentation, "kToolbarAccentRgb");
  expect_contains(button_presentation, "face.state.setFlag(State_On, false)");
  expect_contains(button_presentation, "kButtonMinimumSize(40, 40)");
  expect_contains(button_presentation, "metrics.size(kButtonMinimumSize)");
  expect_contains(button_presentation,
                  "QStyle::CT_ToolButton, &option, contents, &button");
  expect_contains(button_presentation, "button.setMinimumSize(minimum)");
  expect_contains(button_presentation, "button.setFocusPolicy(Qt::StrongFocus)");
  expect_contains(button_presentation, "setAccessibleDescription");
  expect_missing(button_presentation, "setDown(");
  expect_contains(menu_header, "selection_toolbar_visible");
  expect_contains(menu_header, "set_selection_toolbar_visible");
  expect_contains(menu, "QStringLiteral(\"toolbar_selection\")");
  expect_missing(toolbar, "setStyleSheet(");
  expect_missing(toolbar, "setPalette(");
}

TEST(UiLayoutGuardrailTests,
     ToolToolbarUsesNativeMovableChromeAndCanonicalToolCommands) {
  const std::string toolbar_header =
      read_source_file("src/ui/qt_tool_toolbar.hpp");
  const std::string toolbar = read_source_file("src/ui/qt_tool_toolbar.cpp");
  const std::string button_presentation =
      read_source_file("src/ui/qt_toolbar_button_presentation.cpp");
  const std::string menu_header =
      read_source_file("src/ui/qt_main_menu.hpp");
  const std::string menu = read_source_file("src/ui/qt_main_menu.cpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(toolbar_header, "class QtToolToolbar final : public QToolBar");
  expect_contains(toolbar, "setAllowedAreas(Qt::AllToolBarAreas)");
  expect_contains(toolbar, "setMovable(true)");
  expect_contains(toolbar, "setFloatable(true)");
  expect_contains(toolbar, "setIconSize(QSize(32, 32))");
  expect_contains(toolbar, "Qt::ToolButtonIconOnly");
  expect_contains(toolbar, "action_group->setExclusive(true)");
  expect_contains(toolbar, "if (spec.toolbar_visible)");
  expect_contains(toolbar, "{ViewportToolId::CreatePrimitive,");
  for (std::string_view icon :
       {"tool_box_select.png", "tool_paint_select.png", "tool_move.png", "tool_rotate.png",
        "tool_scale.png", "tool_pivot.png", "tool_create_box.png",
        "tool_extrude.png",
        "tool_insert_edge_loop.png", "tool_knife.png", "tool_cut.png",
        "tool_poly.png", "tool_mirror.png", "tool_linear_array.png",
        "tool_radial_array.png"}) {
    expect_contains(toolbar, icon);
  }

  expect_contains(ui_system,
                  "addToolBar(Qt::LeftToolBarArea, tool_toolbar_)");
  expect_contains(ui_system, "widget->geometry().isValid()");
  expect_contains(ui_system, "mix_toolbar(selection_toolbar_)");
  expect_contains(ui_system, "mix_toolbar(tool_toolbar_)");
  expect_missing(ui_system, "body_left <= expected_left");
  expect_missing(ui_system, "body_right >= expected_right");
  expect_contains(ui_system,
                  "command_id_for_tool_activation(tool_id)");
  expect_contains(ui_system, "commands().execute(command_id)");
  expect_contains(ui_system,
                  "synchronize_tool_toolbar(selection, owner_state_changed)");
  expect_contains(ui_system, "owner_state_changed);");
  expect_contains(ui_system,
                  "toolbar_owner_presenter_.needs_refresh(owner_stamp)");
  expect_contains(toolbar, "viewports.selected_tool(pane)");
  expect_contains(toolbar, "synchronized_control_revision_ == control_revision");
  expect_contains(toolbar, "synchronized_pane_ == pane");
  expect_contains(toolbar, "editor.tools().active_tool_revision()");
  expect_contains(toolbar,
                  "command_id_for_tool_activation(entry.tool_id)");
  expect_contains(toolbar,
                  "configure_native_toolbar_button_face(*this, *entry.action,");
  expect_contains(button_presentation,
                  "configure_native_toolbar_button_face(*button, action, presentation)");
  expect_contains(button_presentation, "button.setAutoRaise(false)");
  expect_contains(button_presentation, "kHoverHighlightMix");
  expect_contains(button_presentation,
                  "QPen outline(accent, keyboard_focused ? 2.0 : 1.0)");
  expect_missing(button_presentation, "marker_rect");
  expect_missing(button_presentation, "setDown(");
  expect_contains(toolbar, "if (!force && synchronization_valid_");
  expect_contains(toolbar, "QToolButton::DelayedPopup");
  expect_contains(toolbar, "editor.tools().preferred_primitive_shape()");
  expect_contains(toolbar, "synchronized_keybindings_revision_ == keybindings_revision");
  expect_contains(toolbar, "synchronized_primitive_shape_ == primitive_shape");
  expect_missing(toolbar, "setShortcut(");
  expect_contains(toolbar, "editor.commands().descriptor(command_id)");
  expect_contains(toolbar, "command.has_value() ? command->shortcut");
  expect_missing(toolbar, "editor.commands().available()");
  const std::size_t unchanged_state_gate =
      toolbar.find("if (!force && synchronization_valid_");
  const std::size_t command_lookup =
      toolbar.find("editor.commands().descriptor(command_id)");
  const std::size_t shortcut_lookup =
      toolbar.find("command.has_value() ? command->shortcut");
  ASSERT_NE(unchanged_state_gate, std::string::npos);
  ASSERT_NE(command_lookup, std::string::npos);
  ASSERT_NE(shortcut_lookup, std::string::npos);
  EXPECT_LT(unchanged_state_gate, command_lookup);
  EXPECT_LT(unchanged_state_gate, shortcut_lookup);
  expect_contains(toolbar, "QStringLiteral(\" [%1]\")");
  expect_contains(menu_header, "tool_toolbar_visible");
  expect_contains(menu_header, "set_tool_toolbar_visible");
  expect_contains(menu, "QStringLiteral(\"toolbar_tools\")");
  expect_missing(toolbar, "setStyleSheet(");
  expect_missing(toolbar, "setPalette(");
  // Only the icon is drawn locally; native button painting remains unchanged.
  expect_missing(toolbar, "paintEvent(");
  expect_contains(toolbar, "QIcon curve_tool_icon(const QPalette &palette)");
  expect_contains(toolbar, "{ViewportToolId::UvPeel, \":/quader/icons/tool_uv_peel.png\"");
  expect_contains(toolbar, "refresh_curve_tool_icon()");
  expect_contains(toolbar, "QEvent::PaletteChange");
  expect_contains(toolbar, "QPainter painter(&pixmap)");
  expect_contains(toolbar, "palette.color(QPalette::ButtonText)");
}

TEST(UiLayoutGuardrailTests,
     SnappingToolbarUsesNativeMovableChromeAndRetainedOwnerState) {
  const std::string toolbar_header =
      read_source_file("src/ui/qt_snapping_toolbar.hpp");
  const std::string toolbar =
      read_source_file("src/ui/qt_snapping_toolbar.cpp");
  const std::string menu_header =
      read_source_file("src/ui/qt_main_menu.hpp");
  const std::string menu = read_source_file("src/ui/qt_main_menu.cpp");
  const std::string ui_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(toolbar_header,
                  "class QtSnappingToolbar final : public QToolBar");
  expect_contains(toolbar, "setAllowedAreas(Qt::AllToolBarAreas)");
  expect_contains(toolbar, "setMovable(true)");
  expect_contains(toolbar, "setFloatable(true)");
  expect_contains(toolbar, "setIconSize(QSize(32, 32))");
  expect_contains(toolbar, "Qt::ToolButtonIconOnly");
  expect_contains(toolbar, "static_assert(ToggleCount == 4)");
  expect_contains(toolbar_header, "std::array<ToggleAction, 4>");

  for (std::string_view action_name :
       {"snappingEnabled", "gridSnapping", "vertexSnapping",
        "rotationSnapping"}) {
    expect_contains(toolbar, action_name);
  }
  for (std::string_view icon :
       {"snapping_master.png", "snapping_grid.png", "snapping_vertex.png",
        "snapping_rotation.png"}) {
    expect_contains(toolbar, icon);
  }
  EXPECT_EQ(count_occurrences(toolbar,
                              "new QtDownwardPopupComboBox(this)"),
            2u);
  expect_contains(toolbar,
                  "class QtDownwardPopupComboBox final : public QComboBox");
  expect_contains(toolbar, "void showPopup() override");
  expect_contains(toolbar,
                  "grid_size_combo_->setFixedHeight("
                  "kTopToolbarButtonHeight)");
  expect_contains(toolbar,
                  "angle_combo_->setFixedHeight(kTopToolbarButtonHeight)");
  expect_contains(toolbar, "snappingGridSizeCombo");
  expect_contains(toolbar, "snappingAngleCombo");
  expect_contains(toolbar, "viewport_grid_preset_catalog()");
  expect_contains(toolbar, "viewport_rotation_snap_degrees_catalog()");

  expect_contains(toolbar,
                  "install_native_toolbar_button_presentation(*this)");
  expect_missing(toolbar, "setStyleSheet(");
  expect_missing(toolbar, "setPalette(");
  expect_missing(toolbar, "QPainter");
  expect_missing(toolbar, "QProxyStyle");
  expect_missing(toolbar, "setStyle(");

  expect_contains(toolbar,
                  "if (!force && presented_state_.has_value() &&");
  expect_contains(toolbar, "*presented_state_ == state");
  expect_missing(toolbar, "editor.commands().available()");
  const std::size_t unchanged_state_gate =
      toolbar.find("if (!force && presented_state_.has_value() &&");
  const std::size_t shortcut_lookup =
      toolbar.find("editor.commands().shortcut_for(target_id)");
  ASSERT_NE(unchanged_state_gate, std::string::npos);
  ASSERT_NE(shortcut_lookup, std::string::npos);
  EXPECT_LT(unchanged_state_gate, shortcut_lookup);
  const std::size_t transform_gate = toolbar.find(
      "if (!force && transform_presentation_stamp_ == stamp)");
  const std::size_t transform_command_lookup = toolbar.find(
      "editor.commands().descriptor(\n"
      "          quader_command::kViewportToggleTransformSpace)");
  ASSERT_NE(transform_gate, std::string::npos);
  ASSERT_NE(transform_command_lookup, std::string::npos);
  EXPECT_LT(transform_gate, transform_command_lookup);
  expect_contains(toolbar,
                  "grid_size_combo_->currentIndex() != selected_index");
  expect_contains(toolbar, "grid_size_combo_->toolTip() != tooltip");
  expect_contains(toolbar,
                  "grid_size_combo_->accessibleDescription() != tooltip");
  expect_contains(toolbar,
                  "toggle_actions_[index].action->isChecked() ==");

  expect_contains(ui_header, "QtSnappingToolbar *snapping_toolbar_");
  expect_contains(ui_system,
                  "addToolBar(Qt::TopToolBarArea, snapping_toolbar_)");
  expect_contains(ui_system, "snapping_toolbar_->synchronize(*editor_");
  expect_contains(ui_header,
                  "bool snapping_toolbar_workspace_visible_ = true");
  expect_contains(ui_system,
                  "snapping_toolbar_workspace_visible_ =");
  expect_contains(ui_system,
                  "snapping_toolbar_->setVisible("
                  "snapping_toolbar_workspace_visible_)");
  expect_contains(ui_system,
                  "static_cast<const QToolBar *>(snapping_toolbar_)");
  expect_contains(ui_system, "mix_toolbar(snapping_toolbar_)");
  expect_contains(ui_system,
                  "main_window_->saveState(kQtWindowStateVersion)");
  expect_contains(ui_system,
                  "main_window_->restoreState(state, kQtWindowStateVersion)");

  expect_contains(menu_header, "snapping_toolbar_visible");
  expect_contains(menu_header, "set_snapping_toolbar_visible");
  expect_contains(menu, "QStringLiteral(\"toolbar_snapping\")");
}

TEST(UiLayoutGuardrailTests,
     ViewportDisplaySourcesStayInPaneStripsWithRetainedOwnerState) {
  const std::string toolbar_header =
      read_source_file("src/ui/qt_viewport_display_toolbar.hpp");
  const std::string toolbar =
      read_source_file("src/ui/qt_viewport_display_toolbar.cpp");
  const std::string menu_header =
      read_source_file("src/ui/qt_main_menu.hpp");
  const std::string menu = read_source_file("src/ui/qt_main_menu.cpp");
  const std::string ui_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");
  const std::string commands =
      read_source_file("src/api/quader_commands_api.cpp");

  expect_contains(
      toolbar_header,
      "class QtViewportDisplayToolbar final : public QToolBar");
  expect_contains(toolbar, "setObjectName(QStringLiteral(\"viewportDisplayToolbar\"))");
  expect_contains(toolbar, "setAllowedAreas(Qt::AllToolBarAreas)");
  expect_contains(toolbar, "setMovable(true)");
  expect_contains(toolbar, "setFloatable(true)");
  expect_contains(toolbar, "setIconSize(QSize(32, 32))");
  expect_contains(toolbar, "Qt::ToolButtonIconOnly");
  for (std::string_view action_name :
       {"viewportGrid", "viewportGizmos", "viewportMeshGrid",
        "viewportOverlays"}) {
    expect_contains(toolbar, action_name);
  }
  for (std::string_view icon :
       {"viewport_grid.png", "viewport_gizmos.png",
        "viewport_mesh_grid.png", "viewport_overlays.png"}) {
    expect_contains(toolbar, icon);
  }
  for (std::string_view command :
       {"kViewportToggleGrid", "kViewportToggleGizmos",
        "kViewportToggleMeshGrid", "kViewportToggleOverlays"}) {
    expect_contains(toolbar, command);
    expect_contains(commands, command);
  }
  expect_contains(toolbar,
                  "if (!force && presented_state_.has_value() &&");
  expect_contains(toolbar, "editor.commands().descriptor(");
  expect_contains(toolbar, "editor.commands().shortcut_for(entry.command_id)");
  expect_missing(toolbar, "editor.commands().available()");
  const std::size_t unchanged_state_gate =
      toolbar.find("if (!force && presented_state_.has_value() &&");
  const std::size_t command_lookup =
      toolbar.find("editor.commands().descriptor(");
  const std::size_t shortcut_lookup = toolbar.find(
      "editor.commands().shortcut_for(entry.command_id)");
  ASSERT_NE(unchanged_state_gate, std::string::npos);
  ASSERT_NE(command_lookup, std::string::npos);
  ASSERT_NE(shortcut_lookup, std::string::npos);
  EXPECT_LT(unchanged_state_gate, command_lookup);
  EXPECT_LT(unchanged_state_gate, shortcut_lookup);

  expect_contains(ui_header,
                  "QtViewportDisplayToolbar *viewport_display_toolbar_");
  expect_missing(
      ui_system,
      "addToolBar(Qt::TopToolBarArea, viewport_display_toolbar_)");
  expect_contains(ui_system, "viewport_display_toolbar_->hide()");
  expect_contains(ui_system, "viewport_display_toolbar_->viewport_actions()");
  expect_contains(ui_system,
                  "editor_->commands().execute(command_id)");
  expect_missing(ui_system, "mix_toolbar(viewport_display_toolbar_)");
  expect_missing(menu_header, "viewport_display_toolbar_visible");
  expect_missing(menu_header, "set_viewport_display_toolbar_visible");
  expect_missing(menu,
                  "QStringLiteral(\"toolbar_viewport_display\")");
}

TEST(UiLayoutGuardrailTests,
     TextureToolbarSeparatesLocksAndGatesRetainedCommandPresentation) {
  const std::string toolbar = read_source_file("src/ui/qt_texture_toolbar.cpp");
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string menu = read_source_file("src/ui/qt_main_menu.cpp");
  expect_contains(toolbar, "textureToolbarDivider");
  expect_contains(toolbar, "kViewportToggleTextureLock,");
  expect_contains(toolbar, "kViewportToggleTextureLockScale,");
  expect_contains(toolbar, ":/quader/icons/texture_lock.png");
  expect_contains(toolbar, ":/quader/icons/texture_lock_scale.png");
  expect_missing(toolbar, "editor.commands().available()");
  expect_missing(toolbar, "setShortcut(");
  expect_missing(toolbar, "QKeySequence");
  const std::size_t gate =
      toolbar.find("if (!force && presented_state_.has_value()");
  const std::size_t binding_revision =
      toolbar.find("preferences.keybindings_revision()");
  const std::size_t descriptor = toolbar.find("editor.commands().descriptor(");
  const std::size_t shortcut = toolbar.find("editor.commands().shortcut_for(");
  ASSERT_NE(gate, std::string::npos);
  ASSERT_NE(binding_revision, std::string::npos);
  ASSERT_NE(descriptor, std::string::npos);
  ASSERT_NE(shortcut, std::string::npos);
  EXPECT_LT(gate, descriptor);
  EXPECT_LT(gate, shortcut);
  EXPECT_LT(binding_revision, gate);
  expect_contains(toolbar,
                  "presented_keybindings_revision_ == keybindings_revision");

  const std::size_t snapping_add = ui.find(
      "addToolBar(Qt::TopToolBarArea, snapping_toolbar_)");
  const std::size_t texture_add = ui.find(
      "addToolBar(Qt::TopToolBarArea, texture_toolbar_)");
  ASSERT_NE(snapping_add, std::string::npos);
  ASSERT_NE(texture_add, std::string::npos);
  EXPECT_LT(snapping_add, texture_add);
  expect_contains(ui, "texture_toolbar_->hide()");
  expect_contains(ui, "texture_toolbar_workspace_visible_");
  expect_contains(ui, "mix_toolbar(texture_toolbar_)");
  expect_contains(menu, "QStringLiteral(\"toolbar_texture\")");

  const std::string face_sync = source_slice(
      ui, "  if (panel_presented(UIPanelId::FaceTexture))",
      "    QDR_PROFILE_SCOPE(\"UI synchronize status bar\")");
  expect_contains(face_sync, "face_texture_panel_->refresh()");
  expect_missing(face_sync, "numeric_interaction_active");
  expect_missing(face_sync, "active_settings()");
  expect_missing(face_sync, "dock->raise()");
  const std::string controls =
      read_source_file("src/ui/qt_face_texture_controls.cpp");
  const auto revision_gate = controls.find("*texture_revision_ == revision");
  const auto settings_query = controls.find("face_texture_settings()");
  ASSERT_NE(revision_gate, std::string::npos);
  ASSERT_NE(settings_query, std::string::npos);
  EXPECT_LT(revision_gate, settings_query);
  expect_contains(controls,
                  "texture_keybindings_revision_ == keybindings_revision");
  expect_contains(controls, "texture_rotation_step_ == rotation_step");
}

TEST(UiLayoutGuardrailTests,
     ToolbarSynchronizationGatesSelectionSummaryBeforeFrameWork) {
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");
  const std::size_t refresh_begin =
      ui_system.find("void UISystem::refresh(const FrameTiming &frame_timing,");
  const std::size_t refresh_end =
      ui_system.find("void UISystem::process_viewport_input()", refresh_begin);
  ASSERT_NE(refresh_begin, std::string::npos);
  ASSERT_NE(refresh_end, std::string::npos);
  const std::string refresh =
      ui_system.substr(refresh_begin, refresh_end - refresh_begin);

  const std::size_t revision_gate =
      refresh.find("if (owner_state_changed || "
                   "active_tool_revision_changed ||\n"
                   "          transform_space_revision_changed)");
  const std::size_t selection_summary =
      refresh.find("editor_->selection().summary()");
  const std::size_t active_tool_revision =
      refresh.find("editor_->tools().active_tool_revision()");
  const std::size_t active_tool_revision_gate =
      refresh.find("if (owner_state_changed || "
                   "active_tool_revision_changed ||\n"
                   "          transform_space_revision_changed)");
  const std::size_t transform_space_revision =
      refresh.find("editor_->viewports().transform_space_revision()");
  ASSERT_NE(revision_gate, std::string::npos);
  ASSERT_NE(selection_summary, std::string::npos);
  ASSERT_NE(active_tool_revision, std::string::npos);
  ASSERT_NE(active_tool_revision_gate, std::string::npos);
  ASSERT_NE(transform_space_revision, std::string::npos);
  EXPECT_LT(revision_gate, selection_summary);
  EXPECT_LT(active_tool_revision, active_tool_revision_gate);
  EXPECT_LT(transform_space_revision, active_tool_revision_gate);
  expect_contains(refresh,
                  "toolbar_tool_presenter_.needs_refresh(tool_stamp)");
  expect_contains(refresh, "editor_->tools().preferred_primitive_shape()");
  expect_contains(refresh, "editor_->commands().keybindings_revision()");
  expect_contains(refresh,
                  "toolbar_transform_presenter_.needs_refresh(transform_stamp)");
  expect_missing(refresh, "editor_->tools().active_tool()");
  expect_contains(refresh, "modeling_->content_revision()");
  expect_contains(refresh, "modeling_->selection_revision()");
  expect_missing(refresh, "commands().available()");
}

TEST(UiLayoutGuardrailTests,
     CurvePresentationKeepsSamplingBehindOwnerRevisionGates) {
  const std::string overlay =
      read_source_file("src/render/overlay/curve_tool_overlay_cache.cpp");
  const std::string picking =
      read_source_file("src/editor/modeling/picking/curve_picking_cache.cpp");
  const std::string placement = read_source_file(
      "src/editor/placement/viewport_point_placement_service.cpp");
  const std::string viewport = read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string settings = read_source_file("src/api/quader_tools_api.cpp");
  const std::string ui = read_source_file("src/ui/ui_system.cpp");

  const std::size_t overlay_gate =
      overlay.find("if (valid_ && stamp == previous_stamp_)");
  const std::size_t sample = overlay.find("curve_sample_payload(");
  ASSERT_NE(overlay_gate, std::string::npos);
  ASSERT_NE(sample, std::string::npos);
  EXPECT_LT(overlay_gate, sample);

  const std::size_t pick_gate = picking.find("if (valid_ && document_");
  const std::size_t hierarchy = picking.find("hierarchy_projection()");
  const std::size_t reserve = picking.find("candidates_.reserve(");
  ASSERT_NE(pick_gate, std::string::npos);
  ASSERT_NE(hierarchy, std::string::npos);
  ASSERT_NE(reserve, std::string::npos);
  EXPECT_LT(pick_gate, hierarchy);
  EXPECT_LT(pick_gate, reserve);

  const std::size_t projection_gate =
      placement.find("if (landmark_candidates_enabled) {");
  const std::size_t geometry_gate =
      placement.find("if (geometry_candidates_enabled) {");
  const std::size_t cache_refresh =
      placement.find("surface_snapshot(document, assets);", geometry_gate);
  ASSERT_NE(geometry_gate, std::string::npos);
  ASSERT_NE(cache_refresh, std::string::npos);
  EXPECT_LT(geometry_gate, cache_refresh);
  ASSERT_NE(projection_gate, std::string::npos);
  const std::size_t landmark_gate = placement.find(
      "if (landmark_candidates_enabled) {", projection_gate + 1u);
  const std::size_t landmark_loop =
      placement.find("for (std::size_t landmark_index", landmark_gate);
  ASSERT_NE(landmark_gate, std::string::npos);
  ASSERT_NE(landmark_loop, std::string::npos);
  EXPECT_LT(landmark_gate, landmark_loop);

  expect_missing(viewport, "CurveToolOverlayCache");
  expect_missing(viewport, "curve_tool_overlay_cache_");
  expect_contains(viewport,
                  ".curve_preview = native_presentation.curve_preview");
  const std::size_t selection_fallback_gate = viewport.find(
      "(interaction_frame.active_tool_allows_selection_fallback ||");
  const std::size_t curve_drag_gate =
      viewport.find("!native_presentation.curve_dragging",
                    selection_fallback_gate);
  const std::size_t curve_candidates =
      viewport.find("curve_pick_cache_.candidates(", selection_fallback_gate);
  ASSERT_NE(selection_fallback_gate, std::string::npos);
  ASSERT_NE(curve_drag_gate, std::string::npos);
  ASSERT_NE(curve_candidates, std::string::npos);
  EXPECT_LT(selection_fallback_gate, curve_candidates);
  EXPECT_LT(curve_drag_gate, curve_candidates)
      << "Active Curve drags use their frozen snap candidates and must not "
         "rebuild the scene Curve pick cache per pointer sample.";
  expect_contains(viewport, "!editor.modeling().can_exit_instance_edit())");
  expect_contains(viewport,
                  "viewport_vertex_snap_radius(viewport_settings) > 0.0f");
  expect_contains(viewport,
                  ".scene_snap_enabled = resolved_scene_snap_enabled");
  expect_missing(viewport, "curve_sample_payload(");
  expect_missing(viewport, "commands().available()");
  expect_contains(settings, ".revision = curve.tool_settings_revision()");

  const std::size_t refresh = ui.find("void UISystem::refresh(");
  const std::size_t drag_query = ui.find(
      "frame_native_tools.curve_dragging", refresh);
  const std::size_t toolbar_gate =
      ui.find("if (!edit_drag_active && !numeric_interaction_active)", refresh);
  const std::size_t selection_summary =
      ui.find("editor_->selection().summary()", refresh);
  const std::size_t inspector_gate = ui.find(
      "if (!edit_drag_active && !numeric_interaction_active &&", refresh);
  const std::size_t inspector_refresh =
      ui.find("inspector_panel_->refresh(context)", refresh);
  ASSERT_NE(drag_query, std::string::npos);
  ASSERT_NE(toolbar_gate, std::string::npos);
  ASSERT_NE(selection_summary, std::string::npos);
  ASSERT_NE(inspector_gate, std::string::npos);
  ASSERT_NE(inspector_refresh, std::string::npos);
  EXPECT_LT(drag_query, toolbar_gate);
  EXPECT_LT(toolbar_gate, selection_summary);
  EXPECT_LT(inspector_gate, inspector_refresh);
}

TEST(UiLayoutGuardrailTests,
     HomeKeepsEditorChromeInWorkspace) {
  const std::string ui_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");
  expect_contains(ui_source, "main_window_->menuBar()->hide()");
  expect_contains(ui_source, "main_window_->statusBar()->hide()");
  expect_contains(ui_source, "selection_toolbar_->hide()");
  expect_contains(ui_source, "snapping_toolbar_->hide()");
  expect_contains(ui_source, "viewport_display_toolbar_->hide()");
  expect_contains(ui_source, "tool_toolbar_->hide()");
  expect_contains(ui_source, "main_window_->menuBar()->show()");
  expect_contains(ui_source, "main_window_->statusBar()->show()");
  expect_contains(ui_source,
                  "selection_toolbar_workspace_visible_");
  expect_contains(ui_source, "snapping_toolbar_workspace_visible_");
  expect_missing(ui_source,
                  "viewport_display_toolbar_workspace_visible_");
  expect_contains(ui_source, "tool_toolbar_workspace_visible_");
  expect_contains(ui_header, "workspace_window_state_");
  expect_contains(ui_source, "capture_workspace_window_state()");
  expect_contains(ui_source, "std::string state_bytes = workspace_window_state_");
}

TEST(UiLayoutGuardrailTests,
     FileMenuRoutesDocumentWorkflowsThroughSceneCommands) {
  const std::string menu = read_source_file("src/ui/qt_main_menu.cpp");
  for (std::string_view scene_command :
       {"kSceneDocumentNew", "kSceneDocumentOpen", "kSceneDocumentSave",
        "kSceneDocumentSaveAs", "kSceneDocumentSaveAll",
        "kSceneDocumentClose", "kSceneDocumentNext",
        "kSceneDocumentPrevious"}) {
    expect_contains(menu, scene_command);
  }
  expect_missing(menu, "kProjectSave");
  expect_missing(menu, "kProjectSaveAs");
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
}

TEST(UiLayoutGuardrailTests,
     QtViewportUsesPaneCameraControlsWithoutNavigationGizmo) {
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string cmake = read_source_file("CMakeLists.txt");

  expect_missing(panel_header, "QtNavigationGizmo");
  expect_missing(panel, "navigation_gizmo");
  expect_missing(cmake, "src/ui/qt_navigation_gizmo.cpp");
  expect_missing(cmake, "tests/qt_navigation_gizmo_tests.cpp");
  expect_contains(cmake, "tests/qt_viewport_header_tests.cpp");
  expect_contains(panel_header, "QMenu *camera_menu = nullptr");
  expect_contains(panel, "set_pane_view(pane_state, view)");
  expect_contains(panel, "const ViewportView view = workspace_->view(pane_state.id)");
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
  expect_contains(hud, "colors.brightText().color()");
  expect_missing(hud, "colors.highlightedText().color()");
  expect_contains(hud, "setAttribute(Qt::WA_NativeWindow, native_widget)");
  expect_contains(hud,
                  "speed_label_->setAttribute(Qt::WA_NativeWindow, native_widgets)");
  expect_contains(panel,
                  "new QtViewportFlyNavigationHud(*pane_state.frame, "
                  "pane_state.id, !composed_)");
  const std::size_t raw_input_gate =
      hud.find("if (input_valid_ && input_fly_hud_active_");
  const std::size_t localized_presentation =
      hud.find("qt_viewport_fly_navigation_hud_presentation(",
               raw_input_gate);
  ASSERT_NE(raw_input_gate, std::string::npos);
  ASSERT_NE(localized_presentation, std::string::npos);
  EXPECT_LT(raw_input_gate, localized_presentation);
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
  expect_contains(body,
                  "frame_presented == should_present");
  expect_missing(body, "pane_state.frame->isVisible()");
  expect_missing(body, "pane_state.frame->show()");
  expect_missing(body, "pane_state.frame->hide()");

  const std::string overlay_body = panel.substr(overlay_geometry);
  expect_contains(overlay_body,
                  "pane_state.surface_container->setGeometry(");
  expect_contains(
      overlay_body,
      "pane_state.surface_container->geometry() != pane_bounds");
  expect_contains(overlay_body,
                  "pane_state.toolbar->geometry() != toolbar_geometry");
  expect_contains(overlay_body, "if (!pane_state.overlay_stack_valid)");
  expect_contains(overlay_body, "const bool becoming_visible = !host.visible");
  expect_contains(overlay_body,
                  "host.widget->geometry() != geometry");
  expect_contains(overlay_body,
                  "host.widget->cursor().shape() != cursor_shape");
  expect_contains(overlay_body, "if (becoming_visible)");
  expect_missing(overlay_body, "pane_state.surface->resize(");
}

TEST(UiLayoutGuardrailTests,
     EveryQtViewportPaneOwnsOneFilamentSurfaceForItsPresentationBackend) {
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string surface_header =
      read_source_file("src/ui/qt_viewport_surface.hpp");
  const std::string surface =
      read_source_file("src/ui/qt_viewport_surface.cpp");

  expect_contains(surface_header,
                  "class QtViewportSurface final : public QWindow");
  expect_contains(surface_header, "RenderAdditionalSwapChain swap_chain_");
  const std::string create_pane = source_slice(
      panel, "void QtViewportPanel::create_pane(",
      "void QtViewportPanel::refresh_keybinding_presentation(");
  expect_contains(create_pane,
                  "new QtViewportSurface(pane_state.id, composed_)");
  const std::string composed_host = source_slice(
      create_pane,
      "if (composed_) {\n    pane_state.surface->QObject::setParent",
      "} else {");
  expect_contains(composed_host, "new QtVulkanCompositionWidget(pane_state.frame)");
  expect_contains(composed_host, "host->set_prepare_callback(");
  expect_contains(composed_host, "prepare_composed_frame()");
  expect_contains(composed_host, "host->set_render_callback(");
  expect_contains(composed_host, "surface->render_composed_frame(target)");
  expect_contains(composed_host, "host->set_release_callback(");
  expect_contains(composed_host, "surface->release_composed_surface()");
  expect_missing(composed_host, "createWindowContainer(");
  expect_missing(composed_host, "WA_NativeWindow");
  const std::string native_host = source_slice(
      create_pane, "if (!composed_) {",
      "pane_state.surface->set_visible_host(");
  expect_contains(native_host, "QWidget::createWindowContainer(");
  expect_contains(native_host,
                  "pane_state.surface_backing_container->setAttribute(Qt::WA_NativeWindow)");
  expect_contains(create_pane,
                  "pane_state.surface->set_visible_host(*pane_state.surface_container)");
  const std::string initialize = source_slice(
      surface, "bool QtViewportSurface::initialize(",
      "void QtViewportSurface::shutdown()");
  const std::string composed_initialize = source_slice(
      initialize, "if (composed_) {\n    create_composed_surface();", "\n  }");
  expect_contains(composed_initialize, "return static_cast<bool>(swap_chain_)");
  expect_missing(composed_initialize, "winId()");
  expect_missing(composed_initialize, "native_window_handle()");
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
  expect_contains(surface,
                  "render_system_->render_composited_viewport(renderer_, swap_chain_, target)");
  const std::string native_swap_chain = source_slice(
      surface, "void QtViewportSurface::recreate_swap_chain(",
      "void QtViewportSurface::synchronize_render_size()");
  const std::size_t native_guard = native_swap_chain.find("if (composed_");
  const std::size_t native_handle = native_swap_chain.find("native_window_handle()");
  ASSERT_NE(native_guard, std::string::npos);
  ASSERT_NE(native_handle, std::string::npos);
  EXPECT_LT(native_guard, native_handle);
  expect_missing(surface, "ImGui");
  expect_missing(surface, "SDL");
}

TEST(UiLayoutGuardrailTests,
     NavigationDepthSamplingStaysGestureGatedAndDpiMotionIsRebased) {
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string surface =
      read_source_file("src/ui/qt_viewport_surface.cpp");

  const std::size_t resolver =
      panel.find("ViewportKeybindingResolver::resolve(");
  const std::size_t navigation_press = panel.find(
      "const ViewportNavigationPress navigation_press =", resolver);
  const std::size_t orbit_gate = panel.find(
      "if (navigation_press.action == ViewportInputActionKind::Orbit",
      navigation_press);
  const std::size_t surface_anchor = panel.find(
      "surface_navigation_anchor(navigation_press_local)", orbit_gate);
  const std::size_t selection_anchor =
      panel.find("visible_selection_anchor(navigation_press_local)",
                 surface_anchor);
  const std::size_t router =
      panel.find("input_router_.handle_frame(input, camera)", selection_anchor);
  ASSERT_NE(resolver, std::string::npos);
  ASSERT_NE(navigation_press, std::string::npos);
  ASSERT_NE(orbit_gate, std::string::npos);
  ASSERT_NE(selection_anchor, std::string::npos);
  ASSERT_NE(surface_anchor, std::string::npos);
  ASSERT_NE(router, std::string::npos);
  EXPECT_LT(resolver, orbit_gate);
  EXPECT_LT(navigation_press, orbit_gate);
  EXPECT_LT(orbit_gate, surface_anchor);
  EXPECT_LT(surface_anchor, selection_anchor);
  EXPECT_LT(selection_anchor, router);
  expect_contains(panel,
                  "navigation_press.action == ViewportInputActionKind::Pan");
  expect_contains(panel, "input.mouse_wheel != 0.0f");
  expect_contains(panel, "point_placement->surface_snapshot(document, assets)");
  expect_contains(panel, "SceneSurfaceEligibility::RenderVisible");
  expect_contains(panel, "kSelectionAnchorLogicalRadius");
  // Camera depth must not depend on unrelated scene bounds. The behavioral
  // panel tests cover surface choice, while this gate protects lazy sampling.
  expect_missing(panel, "scene_depth_navigation_anchor");
  const std::size_t wheel_gate = panel.find(
      "if (!input.fly_navigation_active", selection_anchor);
  const std::size_t wheel_sample = panel.find(
      "wheel_navigation_anchor(event.action, event.local)", wheel_gate);
  ASSERT_NE(wheel_gate, std::string::npos);
  ASSERT_NE(wheel_sample, std::string::npos);
  EXPECT_LT(wheel_gate, wheel_sample);
  EXPECT_LT(wheel_sample, router);

  const std::size_t dpi_event =
      surface.find("event->type() == QEvent::DevicePixelRatioChange");
  const std::size_t delta_reset =
      surface.find("input_->mouse_delta = {};", dpi_event);
  const std::size_t pressed_edge_reset =
      surface.find("input_->pointer_pressed_edges.clear();", delta_reset);
  const std::size_t released_edge_reset =
      surface.find("input_->pointer_released_edges.clear();",
                   pressed_edge_reset);
  const std::size_t render_size_sync =
      surface.find("synchronize_render_size();", released_edge_reset);
  ASSERT_NE(dpi_event, std::string::npos);
  ASSERT_NE(delta_reset, std::string::npos);
  ASSERT_NE(pressed_edge_reset, std::string::npos);
  ASSERT_NE(released_edge_reset, std::string::npos);
  ASSERT_NE(render_size_sync, std::string::npos);
  EXPECT_LT(dpi_event, delta_reset);
  EXPECT_LT(delta_reset, pressed_edge_reset);
  EXPECT_LT(pressed_edge_reset, released_edge_reset);
  EXPECT_LT(released_edge_reset, render_size_sync);
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
  expect_contains(surface, "renderer_.width() != width");
  expect_contains(surface, "renderer_.height() != height");
}

TEST(UiLayoutGuardrailTests,
     QtViewportFrameRefreshKeepsNativeLayoutAndControlsCached) {
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string surface =
      read_source_file("src/ui/qt_viewport_surface.cpp");
  const std::string strip =
      read_source_file("src/ui/qt_viewport_control_strip.cpp");

  expect_contains(panel_header,
                  "std::vector<ViewportPanePlacement> displayed_placements_");
  expect_contains(panel, "same_pane_placements(displayed_placements_, "
                         "placements)");
  expect_contains(panel, "if (geometry_changed)");
  expect_contains(panel, "sync_pane_control_selection(pane_state)");
  expect_contains(panel_header, "QPushButton *display_button = nullptr");
  expect_contains(panel_header, "QMenu *camera_menu = nullptr");
  expect_contains(panel_header, "QMenu *debug_view_menu = nullptr");
  expect_contains(panel_header,
                  "void refresh_keybinding_presentation(QuaderEditorApi &editor)");
  expect_contains(panel, "new QtViewportCompactMenuButton(pane_state.toolbar)");
  expect_contains(panel,
                  "pane_state.camera_menu = new QMenu(pane_state.camera_button)");
  expect_contains(panel, "pane_state.toolbar = new QtViewportControlStrip(");
  expect_contains(panel, "const QRect pane_bounds = pane_content_rect(pane_state)");
  const std::string pane_actions = source_slice(
      panel, "void QtViewportPanel::set_control_actions(",
      "void QtViewportPanel::set_settings_panel(");
  expect_contains(pane_actions, "new QAction(pane_state.toolbar)");
  expect_missing(pane_actions, "source->isChecked()");
  expect_contains(pane_actions, "workspace_->selected_tool(pane_state.id)");
  const auto controls_gate = pane_actions.find(
      "pane_state.controls_valid && pane_state.controls_revision == revision");
  const auto controls_loop = pane_actions.find(
      "for (const auto &entry : pane_state.control_actions)", controls_gate);
  ASSERT_NE(controls_gate, std::string::npos);
  ASSERT_NE(controls_loop, std::string::npos);
  EXPECT_LT(controls_gate, controls_loop);
  const auto selection_presentation = pane_actions.find(
      "const std::size_t preferred_selection =", controls_gate);
  ASSERT_NE(selection_presentation, std::string::npos);
  EXPECT_LT(controls_gate, selection_presentation);
  expect_missing(pane_actions, "commands().available()");
  expect_contains(strip, "QToolButton::DelayedPopup");
  expect_missing(strip, "setStyleSheet(");
  expect_contains(panel, "pane_state.display_menu->addMenu(");
  expect_missing(panel, "new QComboBox(pane_state.toolbar)");
  expect_missing(panel, "viewportView_%1");
  for (std::string_view command : {
           "kViewportShadingWireframe", "kViewportShadingShaded",
           "kViewportShadingRendered"}) {
    expect_contains(panel, command);
  }
  expect_contains(panel,
                  "interaction_editor_->commands().execute(*command_id)");
  expect_contains(panel,
                  "interaction_editor_->commands().shortcut_for(*command_id)");
  const std::size_t editor_gate =
      panel.find("if (interaction_editor_ != &editor)");
  const std::size_t keybinding_refresh =
      panel.find("refresh_keybinding_presentation(editor)", editor_gate);
  ASSERT_NE(editor_gate, std::string::npos);
  ASSERT_NE(keybinding_refresh, std::string::npos);
  EXPECT_LT(editor_gate, keybinding_refresh);

  const std::size_t active_tool_sync =
      panel.find("bool QtViewportPanel::sync_active_tool_from_editor(");
  const std::size_t active_tool_revision =
      panel.find("editor.tools().active_tool_revision()", active_tool_sync);
  const std::size_t active_tool_gate =
      panel.find("if (active_tool_revision_valid_", active_tool_revision);
  const std::size_t active_tool_string =
      panel.find("editor.tools().active_tool()", active_tool_gate);
  ASSERT_NE(active_tool_sync, std::string::npos);
  ASSERT_NE(active_tool_revision, std::string::npos);
  ASSERT_NE(active_tool_gate, std::string::npos);
  ASSERT_NE(active_tool_string, std::string::npos);
  EXPECT_LT(active_tool_revision, active_tool_gate);
  EXPECT_LT(active_tool_gate, active_tool_string);
  const std::size_t active_tool_sync_end =
      panel.find("void QtViewportPanel::set_pane_view(",
                 active_tool_string);
  ASSERT_NE(active_tool_sync_end, std::string::npos);
  const std::string active_tool_sync_body =
      panel.substr(active_tool_sync, active_tool_sync_end - active_tool_sync);
  expect_contains(panel_header, "active_tool_revision_valid_");
  expect_contains(active_tool_sync_body, "modeling.selection_mode()");
  expect_missing(active_tool_sync_body,
                 "to_modeling_selection_mode("
                 "editor.selection().summary().mode)");
  expect_contains(panel, "modeling.operation_preview_active()");
  expect_missing(panel, "modeling.tool_settings_target()");

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
  expect_contains(ui, "frame_timing_.delta_seconds");
  expect_contains(panel, "std::clamp(frame_delta_seconds, 0.0, 0.1)");
  expect_missing(panel_header, "QElapsedTimer");
  expect_missing(panel, "input_clock_");
  expect_missing(panel, "elapsed_ms");

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
      "QtViewportPanel::render_viewports(",
      prepare_definition);
  ASSERT_NE(prepare_definition, std::string::npos);
  ASSERT_NE(next_definition, std::string::npos);
  const std::string prepare_body =
      panel.substr(prepare_definition, next_definition - prepare_definition);
  expect_missing(prepare_body, "process_viewport_input(");
  EXPECT_EQ(count_occurrences(prepare_body,
                              "workspace_->active_pane_placements()"),
            1u);
  expect_contains(
      prepare_body,
      "const std::vector<ViewportPanePlacement> active_placements");
  expect_contains(panel_header, "bool control_selection_valid = false");
  expect_contains(panel,
                  "pane_state.control_selection_valid &&");
}

TEST(UiLayoutGuardrailTests,
     MaterialTransferPointerStaysWithItsInitiatingViewportPane) {
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string focus_lock = source_slice(
      panel, "bool QtViewportPanel::pane_interaction_locked() const",
      "void QtViewportPanel::set_modal_tool_owns_viewport_snapshot(");
  expect_contains(focus_lock,
                  "interaction_controller_.material_transfer_pointer_active()");
  const std::string input_lock = source_slice(
      panel, "const bool interaction_locked =",
      "std::vector<ViewportPaneInputCandidate> pane_candidates;");
  const auto owner_gate = input_lock.find(
      "interaction_controller_.material_transfer_pointer_active()");
  const auto input_sample = input_lock.find("take_input_frame(");
  ASSERT_NE(owner_gate, std::string::npos);
  ASSERT_NE(input_sample, std::string::npos);
  EXPECT_LT(owner_gate, input_sample);
  expect_contains(input_lock,
                  "interaction_locked && pane_state.id == interaction_pane_id_");
  expect_missing(input_lock, "commands().available()");
  expect_missing(input_lock, "face_texture_settings()");
}

TEST(UiLayoutGuardrailTests,
     ViewportWorkspaceReattachCancelsPendingMaterialIntentBeforeIdentityGate) {
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::size_t attach = panel.find(
      "void QtViewportPanel::attach_workspace(ViewportWorkspaceService "
      "&workspace)");
  const std::size_t next = panel.find(
      "void QtViewportPanel::set_viewport_settings(", attach);
  ASSERT_NE(attach, std::string::npos);
  ASSERT_NE(next, std::string::npos);
  const std::string body = panel.substr(attach, next - attach);
  const std::size_t cancel = body.find(
      "interaction_controller_.cancel_pending_material_interaction()");
  const std::size_t identity_gate =
      body.find("if (workspace_ != &workspace)");
  ASSERT_NE(cancel, std::string::npos);
  ASSERT_NE(identity_gate, std::string::npos);
  EXPECT_LT(cancel, identity_gate)
      << "same-pointer runtime hook rebinds must cancel delayed intent";
}

TEST(UiLayoutGuardrailTests,
     NativeToolPreviewResetAlwaysCarriesTheBoundCoordinatorInQt) {
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  expect_contains(panel_header,
                  "NativeViewportToolCoordinator *interaction_native_tools_");
  expect_contains(panel_header, "Non-owning runtime pair");
  for (const std::string_view concrete : {
           "CurveToolService", "PivotToolService", "LinearArrayToolService",
           "RadialArrayToolService"}) {
    expect_missing(panel_header, concrete);
    expect_missing(panel, concrete);
  }

  const std::string shutdown = source_slice(
      panel, "void QtViewportPanel::shutdown()",
      "void QtViewportPanel::reset_interactions(");
  expect_contains(
      shutdown,
      "interaction_controller_.reset(interaction_editor_,\n"
      "                                  *interaction_native_tools_)");
  expect_contains(shutdown, "interaction_native_tools_ = nullptr");
  const std::size_t owner_reset = shutdown.find(
      "interaction_controller_.reset(interaction_editor_,\n"
      "                                  *interaction_native_tools_)");
  const std::size_t render_gate =
      shutdown.find("if (render_system_ == nullptr)");
  ASSERT_NE(owner_reset, std::string::npos);
  ASSERT_NE(render_gate, std::string::npos);
  EXPECT_LT(owner_reset, render_gate)
      << "Renderer initialization failure must not strand an authored Curve "
         "control preview.";

  const std::string explicit_reset = source_slice(
      panel, "void QtViewportPanel::reset_interactions(",
      "void QtViewportPanel::abandon_interactions()");
  expect_contains(explicit_reset,
                  "interaction_controller_.reset(editor, *native_tools)");
  expect_contains(explicit_reset, "interaction_native_tools_ = nullptr");

  const std::string attach = source_slice(
      panel, "void QtViewportPanel::attach_workspace(",
      "void QtViewportPanel::set_viewport_settings(");
  expect_contains(
      attach,
      "interaction_controller_.reset(interaction_editor_,\n"
      "                                    *interaction_native_tools_)");

  const std::string input = source_slice(
      panel, "void QtViewportPanel::process_viewport_input(",
      "bool QtViewportPanel::sync_active_tool_from_editor(");
  expect_contains(input, "interaction_native_tools_ = &native_tools");
}

TEST(UiLayoutGuardrailTests,
     RuntimeHookRebindAbandonsInteractionsWithoutTouchingExpiredOwners) {
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string surface_header =
      read_source_file("src/ui/qt_viewport_surface.hpp");
  const std::string surface =
      read_source_file("src/ui/qt_viewport_surface.cpp");
  const std::string resolver_header = read_source_file(
      "src/editor/input/viewport_keybinding_resolver.hpp");
  const std::string runtime_token_header = read_source_file(
      "src/viewport/viewport_runtime_binding_token.hpp");
  const std::string resolver = read_source_file(
      "src/editor/input/viewport_keybinding_resolver.cpp");
  const std::string interaction_controller_header = read_source_file(
      "src/editor/viewport/viewport_interaction_controller.hpp");
  const std::string interaction_controller = read_source_file(
      "src/editor/viewport/viewport_interaction_controller.cpp");
  const std::string context_menu_source =
      read_source_file("src/ui/qt_viewport_context_menu.cpp");
  const std::string delete_popup_source =
      read_source_file("src/ui/qt_viewport_delete_popup.cpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(panel_header, "void abandon_interactions();");
  expect_contains(panel_header, "runtime_binding_generation_");
  const std::string abandon = source_slice(
      panel, "void QtViewportPanel::abandon_interactions()",
      "void QtViewportPanel::attach_workspace(");
  expect_contains(abandon, "++runtime_binding_generation_;");
  expect_contains(abandon, "pane_state.camera_menu, pane_state.display_menu");
  expect_contains(abandon, "pane_state.selection_action->menu()");
  expect_contains(abandon, "menu->close()");
  expect_contains(abandon, "pane_state.surface->abandon_input();");
  expect_contains(abandon, "handle.widget->releaseMouse();");
  expect_contains(abandon, "split_handle_dragging_ = false;");
  expect_contains(abandon, "input_router_.reset();");
  expect_contains(abandon, "interaction_controller_.reset();");
  expect_contains(abandon, "set_modal_tool_owns_viewport_snapshot(false);");
  expect_contains(abandon,
                  "interaction_controller_.clear_modal_tool_state();");
  expect_contains(abandon, "interaction_editor_ = nullptr");
  expect_contains(abandon, "interaction_native_tools_ = nullptr");
  expect_contains(abandon, "workspace_ = nullptr");
  expect_contains(abandon, "active_box_preview_ = {};");
  expect_contains(abandon, "modal_tool_dragging_ = false");
  expect_contains(abandon, "linear_array_wheel_remainder_ = 0.0");
  expect_contains(abandon, "radial_array_wheel_remainder_ = 0.0");
  expect_missing(abandon, "*interaction_native_tools_");
  expect_missing(abandon, "interaction_controller_.reset(interaction_editor_");

  const std::string control_dispatch = source_slice(
      panel, "void QtViewportPanel::dispatch_control_action(",
      "void QtViewportPanel::refresh_selection_tool_actions(");
  const auto dispatch_callback =
      control_dispatch.find("control_action_requested_(pane_state.id, action)");
  const auto dispatch_fence = control_dispatch.find(
      "if (generation != runtime_binding_generation_) return;", dispatch_callback);
  const auto dispatch_refresh =
      control_dispatch.find("sync_pane_actions(pane_state)", dispatch_callback);
  ASSERT_NE(dispatch_callback, std::string::npos);
  ASSERT_NE(dispatch_fence, std::string::npos);
  ASSERT_NE(dispatch_refresh, std::string::npos);
  EXPECT_LT(dispatch_callback, dispatch_fence);
  EXPECT_LT(dispatch_fence, dispatch_refresh);

  expect_contains(surface_header, "void abandon_input();");
  const std::string abandon_input = source_slice(
      surface, "void QtViewportSurface::abandon_input()",
      "void QtViewportSurface::set_pointer_capture(");
  expect_contains(abandon_input, "set_pointer_capture(false);");
  expect_contains(abandon_input, "input_->active_keys");
  expect_contains(abandon_input, "input_->click_candidates");
  expect_contains(abandon_input, "InputAccumulator replacement;");
  expect_contains(abandon_input, "replacement.hovered = input_->hovered;");
  expect_contains(abandon_input, "replacement.focused = input_->focused;");
  expect_contains(surface, "abandoned_key_releases.erase(key_identity)");
  expect_contains(surface, "abandoned_pointer_releases.erase(");

  const std::string install = source_slice(
      ui_system, "void UISystem::install_runtime_hooks(",
      "void UISystem::refresh_project_workspace(");
  const std::size_t abandon_old_binding =
      install.find("viewport_panel_->abandon_interactions();");
  const std::size_t bind_editor = install.find("editor_ = &editor;");
  const std::size_t bind_native_tools =
      install.find("native_tools_ = &native_tools;");
  const std::size_t attach_workspace =
      install.find("viewport_panel_->attach_workspace(workspace);");
  ASSERT_NE(abandon_old_binding, std::string::npos);
  ASSERT_NE(bind_editor, std::string::npos);
  ASSERT_NE(bind_native_tools, std::string::npos);
  ASSERT_NE(attach_workspace, std::string::npos);
  EXPECT_LT(abandon_old_binding, bind_editor);
  EXPECT_LT(abandon_old_binding, bind_native_tools);
  EXPECT_LT(bind_native_tools, attach_workspace);
  const std::string before_owner_rebind = install.substr(0, bind_editor);
  expect_missing(before_owner_rebind, "reset_interactions(");

  const std::string destructor = source_slice(
      panel, "QtViewportPanel::~QtViewportPanel()",
      "bool QtViewportPanel::initialize(");
  expect_contains(destructor, "shutdown();");

  const std::string detach = source_slice(
      ui_system, "void UISystem::detach_render_resources()",
      "void UISystem::shutdown()");
  expect_contains(detach,
                  "viewport_panel_->reset_interactions(editor_, "
                  "native_tools_)");
  const std::size_t reset_interactions = detach.find("reset_interactions(");
  const std::size_t shutdown_panel = detach.find("viewport_panel_->shutdown()");
  ASSERT_NE(reset_interactions, std::string::npos);
  ASSERT_NE(shutdown_panel, std::string::npos);
  EXPECT_LT(reset_interactions, shutdown_panel);

  const std::string input = source_slice(
      panel, "void QtViewportPanel::process_viewport_input(",
      "bool QtViewportPanel::sync_active_tool_from_editor(");
  expect_contains(input,
                  "const std::uint64_t input_binding_generation =");
  expect_contains(input,
                  ".current_generation = &runtime_binding_generation_");
  expect_contains(input,
                  ".expected_generation = input_binding_generation");
  expect_contains(resolver_header,
                  "ViewportRuntimeBindingToken runtime_binding = {}");
  expect_contains(runtime_token_header,
                  "struct ViewportRuntimeBindingToken");
  expect_contains(runtime_token_header, "bool current() const");
  expect_contains(resolver, "bool dispatch_continues = runtime_binding.current();");
  expect_contains(resolver, "if (!dispatch_continues)");
  expect_contains(resolver, "if (!runtime_binding.current())");
  expect_contains(resolver, "dispatch_continues = false;");
  const std::size_t stateful_dispatch =
      resolver.find("const bool handled = dispatch_viewport_input_edge(");
  const std::size_t per_edge_token_check =
      resolver.find("if (!runtime_binding.current())", stateful_dispatch);
  const std::size_t deferred_pointer_work =
      resolver.find("reserve_deferred_action_pointer(", stateful_dispatch);
  ASSERT_NE(stateful_dispatch, std::string::npos);
  ASSERT_NE(per_edge_token_check, std::string::npos);
  ASSERT_NE(deferred_pointer_work, std::string::npos);
  EXPECT_LT(stateful_dispatch, per_edge_token_check);
  EXPECT_LT(per_edge_token_check, deferred_pointer_work);
  const std::size_t resolve =
      input.find("ViewportKeybindingResolver::resolve(");
  const std::size_t first_generation_gate = input.find(
      "runtime_binding_generation_ != input_binding_generation", resolve);
  const std::size_t post_resolve_tool_sync =
      input.find("sync_active_tool_from_editor(", resolve);
  ASSERT_NE(resolve, std::string::npos);
  ASSERT_NE(first_generation_gate, std::string::npos);
  ASSERT_NE(post_resolve_tool_sync, std::string::npos);
  EXPECT_LT(resolve, first_generation_gate);
  EXPECT_LT(first_generation_gate, post_resolve_tool_sync);
  EXPECT_GE(count_occurrences(
                input,
                "runtime_binding_generation_ != input_binding_generation"),
            3u);
  expect_contains(input.substr(first_generation_gate),
                  "suppressed_keybinding_chords_.clear();");
  const auto passive_pane_start = input.find(
      "if (!editor.tools().pane_owns_executing_tool(*input_pane)) {");
  const auto passive_pane_end = input.find(
      "if ((interaction_frame.active_tool_allows_selection_fallback ||",
      passive_pane_start);
  ASSERT_NE(passive_pane_start, std::string::npos);
  ASSERT_NE(passive_pane_end, std::string::npos);
  const std::string passive_pane = input.substr(
      passive_pane_start, passive_pane_end - passive_pane_start);
  // Passive menus are mutually exclusive and return without reading the old
  // owners. Each presenter still receives the generation fence for dispatch.
  const auto expect_passive_menu_tail = [&](std::string_view presenter,
                                            std::string_view branch_end) {
    const auto call = passive_pane.find(presenter);
    const auto end = passive_pane.find(branch_end, call);
    const auto generation = passive_pane.find(
        ".expected_generation = input_binding_generation", call);
    const auto call_end = passive_pane.find("});", generation);
    ASSERT_NE(call, std::string::npos);
    ASSERT_NE(end, std::string::npos);
    ASSERT_NE(generation, std::string::npos);
    ASSERT_NE(call_end, std::string::npos);
    ASSERT_LT(generation, end);
    ASSERT_LT(call_end, end);
    expect_contains(passive_pane.substr(call, call_end - call),
                    ".current_generation = &runtime_binding_generation_");
    EXPECT_EQ(passive_pane.substr(call_end + 3, end - call_end - 3)
                  .find_first_not_of(" \t\r\n}"),
              std::string::npos);
  };
  expect_passive_menu_tail("context_menu_presenter_.show(",
                           "} else if (show_delete_popup)");
  expect_passive_menu_tail("delete_popup_presenter_.show(",
                           "} else if (show_quick_menu)");
  expect_passive_menu_tail("quick_menu_presenter_.show(", "return;");
  const auto passive_return = passive_pane.find("return;");
  ASSERT_NE(passive_return, std::string::npos);
  EXPECT_EQ(passive_pane.substr(passive_return + std::string_view("return;").size())
                .find_first_not_of(" \t\r\n}"),
            std::string::npos);
  const auto ordinary_menu_start = input.find("if (show_context_menu) {");
  ASSERT_NE(ordinary_menu_start, std::string::npos);
  const std::string ordinary_menus = input.substr(ordinary_menu_start);
  const std::size_t context_menu =
      ordinary_menus.find("context_menu_presenter_.show(");
  const std::size_t context_menu_gate = ordinary_menus.find(
      "runtime_binding_generation_ != input_binding_generation",
      context_menu);
  const std::size_t delete_popup =
      ordinary_menus.find("delete_popup_presenter_.show(", context_menu);
  const std::size_t delete_popup_gate = ordinary_menus.find(
      "runtime_binding_generation_ != input_binding_generation",
      delete_popup);
  const std::size_t final_tool_status =
      ordinary_menus.find("const ModelingToolStatus final_tool_status", delete_popup);
  ASSERT_NE(context_menu, std::string::npos);
  ASSERT_NE(context_menu_gate, std::string::npos);
  ASSERT_NE(delete_popup, std::string::npos);
  ASSERT_NE(delete_popup_gate, std::string::npos);
  ASSERT_NE(final_tool_status, std::string::npos);
  EXPECT_LT(context_menu, context_menu_gate);
  EXPECT_LT(context_menu_gate, delete_popup);
  EXPECT_LT(delete_popup, delete_popup_gate);
  EXPECT_LT(delete_popup_gate, final_tool_status);

  const std::size_t context_exec =
      context_menu_source.find("menu.exec(global_point)");
  const std::size_t context_exec_gate = context_menu_source.find(
      "if (!runtime_binding.current())", context_exec);
  const std::size_t context_selected_action =
      context_menu_source.find("if (selected_action == nullptr)",
                               context_exec);
  ASSERT_NE(context_exec, std::string::npos);
  ASSERT_NE(context_exec_gate, std::string::npos);
  ASSERT_NE(context_selected_action, std::string::npos);
  EXPECT_LT(context_exec, context_exec_gate);
  EXPECT_LT(context_exec_gate, context_selected_action);
  for (const std::string_view mutation : {
           "editor.tools().execute_action(\"curve\", \"curve.solidify.apply\")",
           "editor.modeling().apply_selected_face_material_to_face(",
           "editor.modeling().apply_face_material_to_selected_faces(",
           "editor.modeling().transfer_face_texture_to_face(",
       }) {
    const auto context_mutation = context_menu_source.find(mutation);
    const auto context_mutation_gate = context_menu_source.find(
        "if (!runtime_binding.current())", context_mutation);
    const auto context_notification = context_menu_source.find(
        "editor.ui().notify(", context_mutation);
    ASSERT_NE(context_mutation, std::string::npos);
    ASSERT_NE(context_mutation_gate, std::string::npos);
    ASSERT_NE(context_notification, std::string::npos);
    EXPECT_LT(context_mutation, context_mutation_gate);
    EXPECT_LT(context_mutation_gate, context_notification);
  }

  const std::size_t confirmation_exec =
      delete_popup_source.find("return confirmation.present(");
  const std::size_t confirmation_gate = delete_popup_source.find(
      "if (!runtime_binding.current())", confirmation_exec);
  const std::size_t confirmation_dispatch = delete_popup_source.find(
      "return execute_delete_popup_command", confirmation_exec);
  ASSERT_NE(confirmation_exec, std::string::npos);
  ASSERT_NE(confirmation_gate, std::string::npos);
  ASSERT_NE(confirmation_dispatch, std::string::npos);
  EXPECT_LT(confirmation_exec, confirmation_gate);
  EXPECT_LT(confirmation_gate, confirmation_dispatch);
  const std::size_t delete_menu_exec =
      delete_popup_source.find("selected_action = menu.exec(global_point)");
  const std::size_t delete_menu_gate = delete_popup_source.find(
      "if (!runtime_binding.current())", delete_menu_exec);
  const std::size_t delete_menu_dispatch = delete_popup_source.find(
      "return execute_delete_popup_command", delete_menu_exec);
  ASSERT_NE(delete_menu_exec, std::string::npos);
  ASSERT_NE(delete_menu_gate, std::string::npos);
  ASSERT_NE(delete_menu_dispatch, std::string::npos);
  EXPECT_LT(delete_menu_exec, delete_menu_gate);
  EXPECT_LT(delete_menu_gate, delete_menu_dispatch);

  expect_contains(interaction_controller_header,
                  "ViewportRuntimeBindingToken runtime_binding = {}");
  const std::string after_active_tool = source_slice(
      interaction_controller,
      "void ViewportInteractionController::handle_after_active_tool_impl(",
      "void ViewportInteractionController::handle_after_active_tool(");
  const std::size_t material_phase =
      after_active_tool.find("handle_material_interactions(");
  const std::size_t material_phase_gate = after_active_tool.find(
      "if (!runtime_binding.current())", material_phase);
  const std::size_t selection_phase =
      after_active_tool.find("selection_.handle_frame(");
  ASSERT_NE(material_phase, std::string::npos);
  ASSERT_NE(material_phase_gate, std::string::npos);
  ASSERT_NE(selection_phase, std::string::npos);
  EXPECT_LT(material_phase, material_phase_gate);
  EXPECT_LT(material_phase_gate, selection_phase);
  const std::string material_interactions = source_slice(
      interaction_controller,
      "void ViewportInteractionController::handle_material_interactions(",
      "void ViewportInteractionController::cancel_pending_material_interaction(");
  for (const std::string_view mutation : {
           "transfer_face_texture_to_face(",
           "apply_face_material_to_mesh(",
       }) {
    const auto call = material_interactions.find(mutation);
    const auto gate = material_interactions.find(
        "if (!runtime_binding.current())", call);
    const auto presentation = material_interactions.find(
        "log_material_interaction_failure(", call);
    ASSERT_NE(call, std::string::npos);
    ASSERT_NE(gate, std::string::npos);
    ASSERT_NE(presentation, std::string::npos);
    EXPECT_LT(call, gate);
    EXPECT_LT(gate, presentation);
  }
  expect_missing(material_interactions, "set_active_material(");
  expect_missing(material_interactions, "apply_active_material_to_face(");
  expect_missing(material_interactions, "apply_active_material_to_mesh(");
  const std::size_t after_active_tool_call =
      input.find("interaction_controller_.handle_after_active_tool(");
  const std::size_t after_active_tool_gate = input.find(
      "runtime_binding_generation_ != input_binding_generation",
      after_active_tool_call);
  ASSERT_NE(after_active_tool_call, std::string::npos);
  ASSERT_NE(after_active_tool_gate, std::string::npos);
  expect_contains(input.substr(after_active_tool_call,
                               after_active_tool_gate -
                                   after_active_tool_call),
                  ".expected_generation = input_binding_generation");

  const std::size_t context_target_pick =
      input.find("pick_context_menu_target(");
  const std::size_t context_target_gate = input.find(
      "runtime_binding_generation_ != input_binding_generation",
      context_target_pick);
  const std::size_t context_presenter =
      input.find("context_menu_presenter_.show(", context_target_pick);
  ASSERT_NE(context_target_pick, std::string::npos);
  ASSERT_NE(context_target_gate, std::string::npos);
  ASSERT_NE(context_presenter, std::string::npos);
  EXPECT_LT(context_target_pick, context_target_gate);
  EXPECT_LT(context_target_gate, context_presenter);
}

TEST(UiLayoutGuardrailTests,
     RuntimeHookRebindRetainsMenuAndCommandFilterCallbackOwners) {
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");
  const std::string menu_header =
      read_source_file("src/ui/qt_main_menu.hpp");
  const std::string menu_source =
      read_source_file("src/ui/qt_main_menu.cpp");
  const std::string filter_header =
      read_source_file("src/ui/qt_command_event_filter.hpp");
  const std::string filter_source =
      read_source_file("src/ui/qt_command_event_filter.cpp");
  const std::string install = source_slice(
      ui_system, "void UISystem::install_runtime_hooks(",
      "void UISystem::refresh_project_workspace(");

  const std::size_t menu_first_install =
      install.find("if (main_menu_ == nullptr)");
  const std::size_t menu_creation =
      install.find("main_menu_ = std::make_unique<QtMainMenu>()");
  const std::size_t menu_rebind =
      install.find("main_menu_->rebind_runtime(editor, modeling)");
  ASSERT_NE(menu_first_install, std::string::npos);
  ASSERT_NE(menu_creation, std::string::npos);
  ASSERT_NE(menu_rebind, std::string::npos);
  EXPECT_LT(menu_first_install, menu_creation);
  EXPECT_LT(menu_creation, menu_rebind);
  EXPECT_EQ(install.find("main_menu_ = std::make_unique<QtMainMenu>()",
                         menu_creation + 1u),
            std::string::npos);
  expect_contains(menu_header, "void rebind_runtime(QuaderEditorApi &editor,");
  expect_contains(menu_source, "impl_->context.editor = std::ref(editor)");
  expect_contains(menu_source,
                  "impl_->context.modeling = std::ref(modeling)");

  const std::size_t filter_first_install =
      install.find("if (command_event_filter_ == nullptr)");
  const std::size_t filter_creation = install.find(
      "command_event_filter_ = std::make_unique<QtCommandEventFilter>");
  const std::size_t filter_rebind =
      install.find("command_event_filter_->rebind_editor(editor)");
  ASSERT_NE(filter_first_install, std::string::npos);
  ASSERT_NE(filter_creation, std::string::npos);
  ASSERT_NE(filter_rebind, std::string::npos);
  EXPECT_LT(filter_first_install, filter_creation);
  EXPECT_LT(filter_creation, filter_rebind);
  expect_missing(install, "command_event_filter_.reset()");
  expect_missing(install,
                 "removeEventFilter(command_event_filter_.get())");
  expect_contains(filter_header,
                  "void rebind_editor(QuaderEditorApi &editor) noexcept;");
  expect_contains(filter_source, "editor_ = &editor;");

  const std::string detach = source_slice(
      ui_system, "void UISystem::detach_render_resources()",
      "void UISystem::shutdown()");
  const std::size_t remove_filter =
      detach.find("removeEventFilter(command_event_filter_.get())");
  const std::size_t destroy_filter = detach.find("command_event_filter_.reset()");
  ASSERT_NE(remove_filter, std::string::npos);
  ASSERT_NE(destroy_filter, std::string::npos);
  EXPECT_LT(remove_filter, destroy_filter);
}

TEST(UiLayoutGuardrailTests,
     RuntimeHookRebindDetachesAndSynchronouslyBindsSceneBackedPanels) {
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");
  const std::string outliner_header =
      read_source_file("src/ui/outliner_panel.hpp");
  const std::string outliner =
      read_source_file("src/ui/outliner_panel.cpp");
  const std::string inspector_header =
      read_source_file("src/ui/inspector_panel.hpp");
  const std::string inspector =
      read_source_file("src/ui/inspector_panel.cpp");
  const std::string layer_header =
      read_source_file("src/ui/layer_panel.hpp");
  const std::string layer = read_source_file("src/ui/layer_panel.cpp");
  const std::string drag_header = read_source_file(
      "src/editor/transform/scene_transform_drag_service.hpp");
  const std::string drag_source = read_source_file(
      "src/editor/transform/scene_transform_drag_service.cpp");
  const std::string transaction = read_source_file(
      "src/editor/history/scene_transform_preview_transaction.hpp");

  for (const std::string *header :
       {&outliner_header, &inspector_header, &layer_header}) {
    expect_contains(*header, "void abandon_runtime() noexcept;");
  }

  const std::string outliner_abandon = source_slice(
      outliner, "void abandon_runtime() noexcept {",
      "void refresh(UIContext &context)");
  expect_contains(outliner_abandon, "++runtime_binding_generation_");
  expect_contains(outliner_abandon, "editor_ = nullptr");
  expect_contains(outliner_abandon, "document_ = nullptr");
  expect_contains(outliner_abandon, "cache_ = {}");
  expect_contains(outliner_abandon, "tree_->abandon_selection_gesture()");
  expect_missing(outliner_abandon, "document_->");
  EXPECT_GE(count_occurrences(outliner, "[this, scheduled_generation]"), 2u);
  expect_contains(outliner,
                  "runtime_binding_generation_ != scheduled_generation");
  expect_contains(outliner,
                  "runtime_binding_generation_ != menu_generation");

  const std::string layer_abandon = source_slice(
      layer, "void abandon_runtime() noexcept {",
      "void refresh(UIContext &context)");
  expect_contains(layer_abandon, "++runtime_binding_generation_");
  expect_contains(layer_abandon, "editor_ = nullptr");
  expect_contains(layer_abandon, "document_ = nullptr");
  expect_contains(layer_abandon, "active_color_dialog_->reject()");
  expect_missing(layer_abandon, "document_->");
  expect_contains(layer,
                  "runtime_binding_generation_ != dialog_generation");
  expect_contains(layer, "runtime_binding_generation_ != menu_generation");

  const std::string inspector_abandon = source_slice(
      inspector, "void abandon_runtime() noexcept {",
      "void refresh(UIContext &context)");
  const std::size_t preview_abandon =
      inspector_abandon.find("active_transform_preview_.abandon()");
  const std::size_t forget_document =
      inspector_abandon.find("document_ = nullptr");
  ASSERT_NE(preview_abandon, std::string::npos);
  ASSERT_NE(forget_document, std::string::npos);
  EXPECT_LT(preview_abandon, forget_document);
  expect_contains(inspector_abandon, "++runtime_binding_generation_");
  expect_contains(inspector_abandon, "preview_document_ = nullptr");
  expect_contains(inspector_abandon, "active_color_dialog_->reject()");
  expect_missing(inspector_abandon, "active_transform_preview_.cancel()");
  expect_missing(inspector_abandon, "document_->");
  expect_contains(inspector,
                  "runtime_binding_generation_ != dialog_generation");

  expect_contains(drag_header, "void abandon() noexcept;");
  const std::string drag_abandon = source_slice(
      drag_source, "void SceneTransformDragService::abandon() noexcept",
      "void SceneTransformDragService::reset()");
  expect_contains(drag_abandon, "preview_->abandon()");
  expect_contains(drag_abandon, "preview_.reset()");
  expect_missing(drag_abandon, "cancel()");
  expect_contains(transaction, "void abandon() noexcept { active_ = false; }");

  const std::string detach = source_slice(
      ui_system, "void UISystem::abandon_scene_backed_panels() noexcept",
      "void UISystem::bind_scene_backed_panels()");
  expect_contains(detach, "outliner_panel_->abandon_runtime()");
  expect_contains(detach, "inspector_panel_->abandon_runtime()");
  expect_contains(detach, "layer_panel_->abandon_runtime()");

  const std::string bind = source_slice(
      ui_system, "void UISystem::bind_scene_backed_panels()",
      "void UISystem::clear_editor_runtime_hooks()");
  expect_contains(bind, "UIContext context{");
  expect_contains(bind, "outliner_panel_->refresh(context)");
  expect_contains(bind, "layer_panel_->refresh(context)");
  expect_contains(bind, "inspector_panel_->refresh(context)");

  const std::string install = source_slice(
      ui_system, "void UISystem::install_runtime_hooks(",
      "void UISystem::refresh_project_workspace(");
  const std::size_t detach_old =
      install.find("abandon_scene_backed_panels();");
  const std::size_t bind_editor = install.find("editor_ = &editor;");
  const std::size_t bind_new = install.find("bind_scene_backed_panels();");
  ASSERT_NE(detach_old, std::string::npos);
  ASSERT_NE(bind_editor, std::string::npos);
  ASSERT_NE(bind_new, std::string::npos);
  EXPECT_LT(detach_old, bind_editor);
  EXPECT_LT(bind_editor, bind_new);
}

TEST(UiLayoutGuardrailTests,
     ShutdownClearsUiRuntimeHooksBeforeDestroyingTheirCallbackOwner) {
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");
  const std::string clear_hooks = source_slice(
      ui_system, "void UISystem::clear_editor_runtime_hooks()",
      "bool UISystem::initialize(");
  expect_contains(
      clear_hooks,
      "set_runtime_hooks(QuaderViewportRuntimeHooks{})");
  expect_contains(clear_hooks,
                  "set_runtime_hooks(QuaderDevToolsRuntimeHooks{})");
  expect_contains(clear_hooks, "set_runtime_hooks(QuaderUIRuntimeHooks{})");
  expect_contains(
      clear_hooks,
      "set_runtime_hooks(QuaderPreferencesRuntimeHooks{})");

  const std::string detach = source_slice(
      ui_system, "void UISystem::detach_render_resources()",
      "void UISystem::shutdown()");
  const std::size_t clear = detach.find("clear_editor_runtime_hooks();");
  const std::size_t destroy_filter = detach.find("command_event_filter_.reset()");
  const std::size_t shutdown_panel = detach.find("viewport_panel_->shutdown()");
  const std::size_t clear_prepare =
      detach.find("set_composed_frame_prepare_callback({})");
  const std::size_t clear_release =
      detach.find("composition_anchor_->set_release_callback({})");
  ASSERT_NE(clear, std::string::npos);
  ASSERT_NE(destroy_filter, std::string::npos);
  ASSERT_NE(shutdown_panel, std::string::npos);
  ASSERT_NE(clear_prepare, std::string::npos);
  ASSERT_NE(clear_release, std::string::npos);
  EXPECT_LT(clear, destroy_filter);
  EXPECT_LT(clear, shutdown_panel);
  EXPECT_LT(clear_prepare, shutdown_panel);
  EXPECT_LT(clear_release, shutdown_panel);
  expect_contains(detach, "abandon_scene_backed_panels();");
  expect_missing(detach, "main_menu_.reset()");
  expect_missing(detach, "main_window_.reset()");
  expect_missing(detach, "editor_ = nullptr");

  const std::string shutdown = source_slice(
      ui_system, "void UISystem::shutdown()", "void UISystem::refresh(");
  const std::size_t detach_resources = shutdown.find("detach_render_resources();");
  const std::size_t destroy_menu = shutdown.find("main_menu_.reset()");
  const std::size_t destroy_window = shutdown.find("main_window_.reset()");
  const std::size_t forget_editor = shutdown.find("editor_ = nullptr");
  ASSERT_NE(detach_resources, std::string::npos);
  ASSERT_NE(destroy_menu, std::string::npos);
  ASSERT_NE(destroy_window, std::string::npos);
  ASSERT_NE(forget_editor, std::string::npos);
  EXPECT_LT(detach_resources, destroy_menu);
  EXPECT_LT(detach_resources, destroy_window);
  EXPECT_LT(detach_resources, forget_editor);
}

TEST(UiLayoutGuardrailTests,
     OutlinerAndLayerMutationsStopWhenSynchronousEventsRebindOwners) {
  const std::string outliner =
      read_source_file("src/ui/outliner_panel.cpp");
  const std::string layer = read_source_file("src/ui/layer_panel.cpp");

  expect_contains(outliner, "struct OutlinerRuntimeBindingToken final");
  expect_contains(outliner, "*generation == expected");
  const std::string group = source_slice(
      outliner, "void group_selection(", "void ungroup_selection(");
  const std::size_t group_dispatch =
      group.find("editor.commands().execute(quader_command::kGroupSelection)");
  const std::size_t group_generation = group.find("binding.current()");
  const std::size_t group_old_document =
      group.find("select_current_scene_selection(editor, document)");
  ASSERT_NE(group_dispatch, std::string::npos);
  ASSERT_NE(group_generation, std::string::npos);
  ASSERT_NE(group_old_document, std::string::npos);
  EXPECT_LT(group_dispatch, group_generation);
  EXPECT_LT(group_generation, group_old_document);

  const std::string ungroup = source_slice(
      outliner, "void ungroup_selection(", "void delete_selection(");
  const std::size_t ungroup_dispatch = ungroup.find(
      "editor.commands().execute(quader_command::kUngroupSelection)");
  const std::size_t ungroup_post_dispatch_generation =
      ungroup.find("binding.current()", ungroup_dispatch);
  const std::size_t ungroup_old_document =
      ungroup.find("select_current_scene_selection(editor, document)");
  ASSERT_NE(ungroup_dispatch, std::string::npos);
  ASSERT_NE(ungroup_post_dispatch_generation, std::string::npos);
  ASSERT_NE(ungroup_old_document, std::string::npos);
  EXPECT_LT(ungroup_dispatch, ungroup_post_dispatch_generation);
  EXPECT_LT(ungroup_post_dispatch_generation, ungroup_old_document);

  const std::string menu = source_slice(
      outliner, "void show_context_menu(EntityId entity,",
      "OutlinerPanel &panel_");
  expect_contains(menu, "const OutlinerRuntimeBindingToken binding{");
  expect_contains(menu, "if (!binding.current())");
  expect_contains(menu, "group_selection(*editor_, *document_, binding)");
  expect_contains(menu,
                  "ungroup_selection(*editor_, *document_, binding)");
  expect_contains(menu, "delete_selection(*editor_, *document_, binding)");
  const std::string rename = source_slice(
      outliner, "void handle_item_changed(", "void begin_rename(");
  const std::size_t rename_dispatch = rename.find("editor_->scene().rename(");
  const std::size_t rename_generation = rename.find(
      "runtime_binding_generation_ != mutation_generation", rename_dispatch);
  const std::size_t rename_old_record =
      rename.find("item->setText(qt_text(entity_record->name))", rename_dispatch);
  ASSERT_NE(rename_dispatch, std::string::npos);
  ASSERT_NE(rename_generation, std::string::npos);
  ASSERT_NE(rename_old_record, std::string::npos);
  EXPECT_LT(rename_dispatch, rename_generation);
  EXPECT_LT(rename_generation, rename_old_record);

  const std::string drop = source_slice(
      outliner, "bool handle_drop(", "void handle_item_changed(");
  const std::size_t drop_dispatch = drop.find("editor_->scene().move_entities(");
  const std::size_t drop_generation = drop.find(
      "runtime_binding_generation_ != scheduled_generation", drop_dispatch);
  const std::size_t drop_expansion =
      drop.find("collapsed_entities_.erase(destination)", drop_dispatch);
  const std::size_t drop_queued_callback =
      drop.find("[this, scheduled_generation]()", drop_dispatch);
  const std::size_t drop_queued_generation = drop.find(
      "runtime_binding_generation_ != scheduled_generation", drop_queued_callback);
  const std::size_t drop_refresh =
      drop.find("refresh_model_if_needed()", drop_queued_callback);
  ASSERT_NE(drop_dispatch, std::string::npos);
  ASSERT_NE(drop_generation, std::string::npos);
  ASSERT_NE(drop_expansion, std::string::npos);
  ASSERT_NE(drop_queued_callback, std::string::npos);
  ASSERT_NE(drop_queued_generation, std::string::npos);
  ASSERT_NE(drop_refresh, std::string::npos);
  EXPECT_LT(drop_dispatch, drop_generation);
  EXPECT_LT(drop_generation, drop_expansion);
  EXPECT_LT(drop_generation, drop_queued_callback);
  EXPECT_LT(drop_queued_callback, drop_queued_generation);
  EXPECT_LT(drop_queued_generation, drop_refresh);
  EXPECT_GE(count_occurrences(
                layer,
                "runtime_binding_generation_ != mutation_generation"),
            5u);
  EXPECT_GE(count_occurrences(
                layer, "runtime_binding_generation_ != menu_generation"),
            2u);
  expect_contains(layer,
                  "const std::uint64_t menu_layer_revision =");
  expect_contains(layer,
                  "editor_->layers().revision() != menu_layer_revision");
  expect_contains(
      layer,
      "document_->selection_revision() != menu_scene_selection_revision");
  expect_contains(layer, "selected_layer_ids() != selected");
  expect_contains(layer, "layer_id(tree_->currentIndex()) != id");
}

TEST(UiLayoutGuardrailTests,
     AppFrameDoesNotRetainSceneOwnerAcrossViewportCommandDispatch) {
  const std::string app = read_source_file("src/app/app.cpp");
  const std::string frame = source_slice(
      app, "bool App::render_frame(", "void App::schedule_next_frame(");
  const std::size_t input = frame.find("ui_.process_viewport_input();");
  const std::size_t scene_pointer = frame.find(
      "const OpenSceneSession *active_scene = project_session_.active_scene();");
  ASSERT_NE(input, std::string::npos);
  ASSERT_NE(scene_pointer, std::string::npos);
  EXPECT_LT(input, scene_pointer);
  expect_missing(frame.substr(0, input),
                 "const OpenSceneSession *active_scene");
  EXPECT_GE(count_occurrences(frame, "project_session_.active_scene()"), 5u);
}

TEST(UiLayoutGuardrailTests,
     QtViewportContextMenuPreservesReleasePickingAndCommandRouting) {
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string presenter_header =
      read_source_file("src/ui/qt_viewport_context_menu.hpp");
  const std::string presenter =
      read_source_file("src/ui/qt_viewport_context_menu.cpp");
  const std::string interaction = read_source_file(
      "src/editor/viewport/viewport_interaction_controller.cpp");

  expect_contains(panel_header, "QtViewportContextMenuPresenter "
                                "context_menu_presenter_");
  expect_contains(interaction, "!frame.input.modifiers.primary");
  expect_contains(interaction, "!frame.input.modifiers.shift");
  expect_contains(interaction, "!frame.input.modifiers.alt");
  expect_contains(panel,
                  "interaction_controller_.handle_context_menu_pointer(");
  expect_contains(interaction, "viewport_pointer_gesture_from_latest_edge(");
  expect_contains(interaction, "exact_pointer_release_after(");
  expect_contains(interaction, "context_menu_release_suppression_.consume(");
  expect_contains(panel, "is_viewport_context_menu_click_gesture(");
  expect_contains(
      panel,
      "interaction_controller_.active_tool_allows_selection_fallback(");
  expect_contains(panel,
                  "interaction_controller_.pick_material_face_target(");
  expect_contains(panel,
                  ".right_click_local = pending_context_menu_local_mouse");
  expect_contains(panel, "interaction_controller_.pick_context_menu_target(");
  expect_contains(panel, "should_open_viewport_context_menu(");
  expect_contains(panel, "context_menu_presenter_.show(");
  expect_contains(panel, "qt_event_target_belongs_to_tool_settings(target)");
  expect_contains(panel, "qt_object_belongs_to_tool_settings_panel(target)");
  expect_missing(panel, "floating.tool_settings");
  expect_missing(panel, "editor.modeling().available_action_set(query)");
  expect_missing(panel, "editor.modeling().available_actions(query)");
  expect_missing(panel, "modeling.context_actions(query)");

  const std::size_t process =
      panel.find("void QtViewportPanel::process_viewport_input(");
  const std::size_t after_tool =
      panel.find("interaction_controller_.handle_after_active_tool(", process);
  const std::size_t material_pick = panel.find(
      "interaction_controller_.pick_material_face_target(", after_tool);
  const std::size_t pick =
      panel.find("interaction_controller_.pick_context_menu_target(",
                 after_tool);
  const std::size_t release_capture =
      panel.find("synchronize_pointer_capture(result.fly_cursor_capture_active",
                 pick);
  const std::size_t show =
      panel.find("context_menu_presenter_.show(", release_capture);
  ASSERT_NE(process, std::string::npos);
  ASSERT_NE(after_tool, std::string::npos);
  ASSERT_NE(material_pick, std::string::npos);
  ASSERT_NE(pick, std::string::npos);
  ASSERT_NE(release_capture, std::string::npos);
  ASSERT_NE(show, std::string::npos);
  EXPECT_LT(after_tool, material_pick);
  EXPECT_LT(material_pick, pick);
  EXPECT_LT(pick, release_capture);
  EXPECT_LT(release_capture, show);
  const std::string synchronize_capture = source_slice(
      panel, "void QtViewportPanel::synchronize_pointer_capture(",
      "void QtViewportPanel::cancel_navigation()");
  expect_contains(synchronize_capture,
                  "candidate.surface->set_pointer_capture(owner.has_value() &&");
  expect_contains(synchronize_capture, "candidate.id == *owner");
  expect_contains(synchronize_capture,
                  "candidate.surface->set_pointer_capture(false)");

  expect_missing(presenter, "available_actions(");
  const std::size_t presenter_show =
      presenter.find("void QtViewportContextMenuPresenter::show(");
  ASSERT_NE(presenter_show, std::string::npos);
  expect_missing(std::string_view(presenter).substr(presenter_show),
                 "available_action_set(");
  expect_contains(
      presenter,
      "std::string popup_key = \"ui.viewport.context_menu.action.\";");
  expect_contains(presenter,
                  "localization.text(popup_key, action.label)");
  expect_contains(presenter,
                  "std::string operation_key = \"modeling.action.\";");
  expect_contains(presenter,
                  "editor.commands().command_id_for_tool_activation(");
  expect_contains(presenter,
                  "editor.commands().command_id_for_modeling_action(");
  expect_missing(presenter, "editor.commands().execute(selected->command_id)");
  expect_missing(presenter, "editor.commands().available()");
  expect_contains(presenter, "editor.commands().descriptor(command_id)");
  expect_contains(presenter, "editor.commands().shortcut_for(id)");
  expect_contains(
      presenter,
      "const QuaderRevisionStamp presented_context = editor.revisions()");
  expect_contains(presenter,
                  "same_context_revision(presented_context, editor.revisions())");
  expect_contains(presenter, "editor.tools().execute_action(\"curve\", \"curve.solidify.apply\")");
  expect_contains(presenter, "QLatin1Char('\\t')");
  expect_contains(presenter, "QStringLiteral(\"RightMouseClick\")");
  expect_contains(presenter, "QStringLiteral(\"LeftMouseClick\")");
  expect_contains(presenter, "QStringLiteral(\"RMouse\")");
  expect_contains(presenter, "QStringLiteral(\"LMouse\")");
  expect_missing(presenter, "setShortcut(");
  expect_contains(presenter,
                  "viewport_context_menu_action_disposition(mode,");
  expect_contains(presenter, "find_command_presentation(command_id)");
  expect_contains(presenter, "QuaderSelectionMode::Group");
  expect_missing(presenter, "input_router.set_selection_mode(");
  expect_contains(presenter, "menu.addSeparator()");
  expect_missing(presenter, "setStyleSheet(");
  expect_missing(presenter, "QMenu::item");
  expect_missing(presenter, "QApplication::setStyleSheet(");
  expect_missing(presenter, "\"viewport.material.sample_face\"");
  expect_missing(presenter, "editor.project_assets().set_active_material(");
  expect_missing(presenter, "kApplyMaterial");
  expect_missing(presenter, "show_material_browser");
  expect_contains(presenter, "quader_command::kCutSelection");
  expect_contains(presenter, "quader_command::kCopySelection");
  expect_contains(presenter, "quader_command::kPasteSelection");
  expect_missing(presenter_header, "ModelingSessionService");
  expect_missing(presenter, "ModelingSessionService");
  expect_missing(presenter, "modeling.context_actions(");
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
  expect_contains(menu,
                  "context.editor.get().commands().execute(command_id)");
  expect_missing(menu, "editor.commands().available()");
  expect_contains(
      menu, "context.editor.get().commands().descriptor(entry.command_id)");
  expect_contains(menu, "command->shortcut");
  expect_contains(menu, "command_id_for_tool_activation(");
  expect_contains(menu,
                  "context.editor.get().commands().execute(command_id)");
  expect_missing(menu, "context.editor.get().tools().activate(tool_id)");
  expect_contains(menu, "QLatin1Char('\\t')");
  expect_contains(menu, "present_text(*entry.action,");
  expect_contains(menu, "if (action.text() != text)");
  for (std::string_view forbidden :
       {"setShortcut(", "setShortcuts(", "setShortcutContext(",
        "QShortcut", "QKeySequence"}) {
    expect_missing(menu, forbidden);
    expect_missing(menu_header, forbidden);
  }

  expect_contains(commands, "QuaderKeybindingService keybindings_");
  expect_contains(
      commands,
      "keybindings_.dispatch(chord, context, target_available)");
  expect_contains(commands, "keybindings_.resolve_sequence(");
  expect_contains(commands, "ViewportInputAction");
  expect_contains(commands, "viewport_input_action_for_target_id(");
  expect_contains(commands, "{chord}, context, [](");
  expect_contains(commands, "dispatch_viewport_input_binding(");
  expect_contains(commands,
                  "return api_.modeling().can_execute_tool(tool_id)");
}

TEST(UiLayoutGuardrailTests,
     QtViewportInputRoutesCanonicalChordsThroughQuaderKeybindings) {
  const std::string adapter_header =
      read_source_file("src/ui/qt_input_adapter.hpp");
  const std::string adapter =
      read_source_file("src/ui/qt_input_adapter.cpp");
  const std::string viewport_sources =
      read_source_file("src/ui/qt_viewport_panel.cpp") +
      read_source_file("src/ui/qt_viewport_surface.cpp") +
      read_source_file("src/editor/input/viewport_keybinding_resolver.cpp");
  const std::string input_policy =
      read_source_file("src/viewport/viewport_host_interaction_policy.hpp");

  expect_contains(adapter_header, "std::optional<QuaderInputChord>");
  expect_contains(adapter, "quader_pressed_input_chord_from_qt_event(");
  expect_contains(adapter, "quader_mouse_input_chord_from_qt_event(");
  expect_contains(adapter, "quader_wheel_input_chord_from_qt_event(");
  expect_contains(adapter_header, "QtWheelInputTranslation");
  expect_contains(adapter_header, "std::vector<QuaderWheelInputEdge>");
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
  expect_contains(viewport_sources,
                  "dispatch_ordered_viewport_input_edges(");
  expect_contains(viewport_sources,
                  "has_pending_keybinding_chord()");
  expect_contains(input_policy,
                  "dispatch_ordered_viewport_input_edges(");
  expect_contains(input_policy, "dispatch_viewport_input_edge(");
  expect_contains(input_policy, "viewport_wheel_chords_share_event(");
  expect_contains(input_policy, "accumulate_discrete_wheel_steps(");
}

TEST(UiLayoutGuardrailTests,
     ArrayWheelRoutingConsumesEventsAndFiltersNavigationDelta) {
  const std::string input_resolver = read_source_file(
      "src/editor/input/viewport_keybinding_resolver.cpp");
  const std::string native_tools = read_source_file(
      "src/editor/tools/viewport_native_tool_interaction.cpp");
  const std::string input_policy =
      read_source_file("src/viewport/viewport_host_interaction_policy.hpp");

  // Raw Qt wheel magnitude is useful for detecting activity, but camera input
  // is rebuilt only from edges that resolve to a zoom semantic action.
  expect_contains(input_resolver, "input.mouse_wheel = 0.0f;");
  expect_contains(input_resolver,
                  "action == ViewportInputActionKind::ZoomToCursor");
  expect_contains(input_resolver, "input.mouse_wheel +=");

  // Array actions consume even fractional or clamped events. Completion is
  // physical-event scoped so a generic Wheel remap remains one event while an
  // unrelated next event clears stale high-resolution motion.
  expect_contains(input_resolver,
                  "input.keybinding_input_consumed = true;");
  expect_contains(input_resolver, "finish_array_wheel_event(");
  expect_contains(input_policy, "CompleteWheelEvent");
  expect_contains(input_policy,
                  "complete_wheel_event(wheel.subspan(index, 2u))");

  EXPECT_EQ(count_occurrences(native_tools,
                              "reconcile_bounded_wheel_remainder("),
            2u);
}

TEST(UiLayoutGuardrailTests,
     ViewportHoverCacheTracksResolvedGridSnapState) {
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string hover_header =
      read_source_file("src/ui/viewport_hover_controller.hpp");
  const std::string hover_source =
      read_source_file("src/ui/viewport_hover_controller.cpp");

  expect_contains(panel, "const bool resolved_grid_snap_enabled");
  expect_contains(panel,
                  ".grid_snap_enabled = resolved_grid_snap_enabled");
  expect_contains(hover_header, "bool grid_snap_enabled = true");
  expect_contains(hover_source,
                  "grid_snap_enabled == other.grid_snap_enabled");
  expect_contains(hover_source,
                  ".grid_snap_enabled = request.grid_snap_enabled");
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
                  "input_->released.push_back(*input_->middle_drag)");
  expect_contains(external_loss_source,
                  "input_->released.push_back(*input_->right_drag)");
  expect_contains(
      external_loss_source,
      "release_button_state(QuaderMouseButton::Left, input_->left_down,");
  expect_contains(
      external_loss_source,
      "release_button_state(QuaderMouseButton::Middle, input_->middle_down,");
  expect_contains(
      external_loss_source,
      "release_button_state(QuaderMouseButton::Right, input_->right_down,");
  expect_contains(external_loss_source,
                  "input_->abandoned_pointer_releases.insert(button_id)");
  expect_contains(external_loss_source, "relative_pointer_input_.clear();");
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
     QtApplicationCommandsYieldToFocusedAuxiliaryWindows) {
  const std::string filter_header =
      read_source_file("src/ui/qt_command_event_filter.hpp");
  const std::string filter =
      read_source_file("src/ui/qt_command_event_filter.cpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");

  expect_contains(filter_header, "QtApplicationCommandInputOwnerProvider");
  expect_contains(filter_header, "qt_window_owns_command_input(");
  expect_contains(filter, "qt_application_command_input_owner_active(");
  expect_contains(ui_system,
                  "qt_window_owns_command_input(settings_dialog_)");
  expect_contains(ui_system,
                  "qt_window_owns_command_input(material_browser_window_)");
  expect_contains(
      ui_system,
      "qt_window_owns_command_input(standard_material_editor_window_)");
  expect_missing(ui_system,
                 "qt_window_owns_command_input(tool_settings_panel_)");

  const std::size_t ownership_check =
      filter.find("if (qt_application_command_input_owner_active(");
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
     QtViewportFocusAndActivationStayWiredToHostPolicies) {
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
  expect_missing(panel, "qApp->installEventFilter(this)");
  expect_contains(
      panel,
      "void QtViewportPanel::set_modal_tool_owns_viewport_snapshot(");
  expect_contains(panel, "if (result.cancel_requested)");
  expect_missing(panel, "cancel_active_modal_tool(false)");
  expect_contains(panel, "should_dispatch_viewport_tool_activation({");
  expect_contains(
      panel,
      "interaction_controller_.active_tool_uses_editor_native_lifecycle(");
  expect_contains(panel, "current_tool_status.insert_edge_loop_dragging");
  expect_contains(panel,
                  "should_enable_viewport_overlay_pass({");
  expect_contains(panel, ".tool_overlay_visible = tool_overlay_visible");
}

TEST(UiLayoutGuardrailTests,
     StandardMaterialPreviewUsesOneRetainedSurfaceAndPresentationCycle) {
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");
  const std::string app = read_source_file("src/app/app.cpp");
  const std::string surface =
      read_source_file("src/ui/qt_material_preview_surface.cpp");

  const std::size_t create_surface =
      ui_system.find("material_preview_surface_ = new QtMaterialPreviewSurface()");
  ASSERT_NE(create_surface, std::string::npos);
  const std::size_t create_container =
      ui_system.find("QWidget::createWindowContainer(", create_surface);
  const std::size_t initialize_surface = ui_system.find(
      "material_preview_surface_->initialize(render_system)", create_surface);
  ASSERT_NE(create_container, std::string::npos);
  ASSERT_NE(initialize_surface, std::string::npos);
  expect_contains(ui_system.substr(create_container,
                                    initialize_surface - create_container),
                  "material_preview_surface_, standard_material_editor_window_");
  EXPECT_LT(create_container, initialize_surface)
      << "the native child must exist before preview renderer initialization";
  expect_contains(surface, "return true;\n}");
  expect_contains(surface, "if (!swap_chain_ && native_surface_available_");
  expect_contains(surface, "if (isExposed())");

  const std::size_t begin =
      ui_system.find("render_system_->begin_presentation_frame()");
  const std::size_t scene =
      ui_system.find("viewport_panel_->render_viewports(false)");
  const std::size_t preview =
      ui_system.find("material_preview_surface_->render_frame()");
  const std::size_t complete =
      ui_system.find("render_system_->complete_presentation_frame(submitted)");
  ASSERT_NE(begin, std::string::npos);
  ASSERT_NE(scene, std::string::npos);
  ASSERT_NE(preview, std::string::npos);
  ASSERT_NE(complete, std::string::npos);
  EXPECT_LT(begin, scene);
  EXPECT_LT(scene, preview);
  EXPECT_LT(preview, complete);
  expect_contains(app, "ui_.render_surfaces(render_scene_viewports)");
  expect_contains(ui_system, "!standard_material_editor_window_");
  expect_contains(ui_system, "->isMinimized()");
  expect_contains(ui_system, "material_preview_surface_->needs_rendering()");
  expect_contains(surface,
                  "if (!render_pending_ && !swap_chain_.needs_initial_frame())");
  expect_contains(surface, "render_pending_ = false");
  expect_contains(surface, "mark_render_pending()");

  const std::size_t preview_shutdown =
      ui_system.find("material_preview_surface_->shutdown()");
  const std::size_t main_window_shutdown = ui_system.find("main_window_.reset()");
  ASSERT_NE(preview_shutdown, std::string::npos);
  ASSERT_NE(main_window_shutdown, std::string::npos);
  EXPECT_LT(preview_shutdown, main_window_shutdown);
  expect_contains(ui_system, "material_preview_surface_->reset_preview()");
}

TEST(UiLayoutGuardrailTests,
     StandardMaterialRefreshGatesBroadSnapshotAndPreviewWorkOnRevision) {
  const std::string source = read_source_file("src/ui/ui_system.cpp");
  const std::size_t begin =
      source.find("void UISystem::refresh_standard_material_editor()");
  const std::size_t end =
      source.find("void UISystem::open_standard_material(", begin);
  ASSERT_NE(begin, std::string::npos);
  ASSERT_NE(end, std::string::npos);
  const std::string refresh = source.substr(begin, end - begin);

  const std::size_t revision = refresh.find("current_revision()");
  const std::size_t unchanged = refresh.find(
      "if (!standard_material_editor_presenter_.needs_refresh(stamp))");
  const std::size_t snapshot = refresh.find(".current()");
  const std::size_t preview = refresh.find(".preview()");
  const std::size_t setter = refresh.find("->set_document(");
  const std::size_t publish =
      refresh.find("standard_material_editor_presenter_.mark_presented(stamp)");
  ASSERT_NE(revision, std::string::npos);
  ASSERT_NE(unchanged, std::string::npos);
  ASSERT_NE(snapshot, std::string::npos);
  ASSERT_NE(preview, std::string::npos);
  ASSERT_NE(setter, std::string::npos);
  ASSERT_NE(publish, std::string::npos);
  EXPECT_LT(revision, unchanged);
  EXPECT_LT(unchanged, snapshot);
  EXPECT_LT(snapshot, preview);
  EXPECT_LT(preview, setter);
  EXPECT_LT(setter, publish);

  const std::size_t scalar_begin =
      source.find("void UISystem::handle_standard_material_scalar(");
  const std::size_t texture_begin = source.find(
      "void UISystem::handle_standard_material_texture_transform(");
  ASSERT_NE(scalar_begin, std::string::npos);
  ASSERT_NE(texture_begin, std::string::npos);
  const std::string scalar = source.substr(scalar_begin,
                                           texture_begin - scalar_begin);
  expect_contains(scalar, ".set_scalar(");
  expect_missing(scalar, ".current()");
  const std::string texture = source.substr(texture_begin,
                                            source.find(
                                                "void UISystem::refresh_localized_chrome(",
                                                texture_begin) -
                                                texture_begin);
  expect_contains(texture, ".set_texture_transform_component(");
  expect_missing(texture, ".current()");
}

TEST(UiLayoutGuardrailTests,
     MaterialUsageRefreshKeepsInspectionSeparateFromAssignmentAndDeletion) {
  const std::string source = read_source_file("src/ui/ui_system.cpp");
  const auto refresh = source_slice(
      source, "void UISystem::refresh_material_browser_usage()",
      "void UISystem::invalidate_material_presentation_cache()");
  const auto gate = refresh.find("material_browser_usage_revision_ == observed.value()");
  const auto snapshot = refresh.find("project_assets().material_usage()");
  ASSERT_NE(gate, std::string_view::npos);
  ASSERT_NE(snapshot, std::string_view::npos);
  EXPECT_LT(gate, snapshot);
  expect_contains(refresh.substr(gate, snapshot - gate), "return;");
  expect_contains(refresh, "set_dependency_revision(");
  expect_missing(refresh, "material_dependencies(");
  expect_missing(refresh, "present_status_notification(");
  expect_missing(refresh, "apply_material_to_selected_faces(");
  expect_missing(source, "set_usage_inspection_error_callback(");
}

TEST(UiLayoutGuardrailTests,
     FaceTextureDockSharesRetainedBrowserCatalogAndRevisionGatedMaterialImages) {
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string panel =
      read_source_file("src/ui/qt_face_texture_panel.cpp");
  const std::string docks = source_slice(
      ui, "void UISystem::create_docks()", "QDockWidget &UISystem::create_dock(");
  expect_contains(docks, "UIPanelId::FaceTexture, *face_texture_panel_");
  const auto split = docks.find(
      "splitDockWidget(&inspector, &face_texture, Qt::Vertical)");
  ASSERT_NE(split, std::string::npos);
  expect_missing(docks, "tabifyDockWidget(&inspector, &tool_settings)");
  expect_contains(docks, "panels_.set_presentation_changed_callback(");
  expect_contains(docks, "project_workspace_refresh_signal_->store(");

  const std::string workspace = source_slice(
      ui, "void UISystem::refresh_project_workspace(",
      "void UISystem::refresh_face_texture_material(");
  const auto event_gate = workspace.find(
      "project_workspace_refresh_signal_->exchange(");
  ASSERT_NE(event_gate, std::string::npos);
  expect_contains(workspace, "refresh_material_browser_usage();");

  const std::string browser_refresh = source_slice(
      ui, "void UISystem::refresh_material_browser_usage()",
      "void UISystem::invalidate_material_presentation_cache()");
  const auto catalog_gate = browser_refresh.find(
      "material_browser_catalog_signature_ !=");
  const auto allocation = browser_refresh.find(
      "std::vector<QtMaterialPresentationItem> project_materials");
  const auto setter = browser_refresh.find("browser->set_materials(project_materials)");
  ASSERT_NE(catalog_gate, std::string::npos);
  ASSERT_NE(allocation, std::string::npos);
  ASSERT_NE(setter, std::string::npos);
  EXPECT_LT(catalog_gate, allocation);
  EXPECT_LT(allocation, setter);
  expect_contains(workspace,
                  "material_browser_visible || face_texture_panel_visible");
  const std::string browser_publish = source_slice(
      browser_refresh, "for (auto *browser :",
      "material_browser_catalog_signature_ =");
  expect_contains(browser_publish, "material_browser_window_");
  expect_contains(browser_publish, "face_texture_panel_ != nullptr");
  expect_contains(browser_publish, "&face_texture_panel_->browser() : nullptr");
  const auto null_browser_gate = browser_publish.find("if (browser == nullptr)");
  const auto catalog_publish = browser_publish.find("browser->set_materials(");
  ASSERT_NE(null_browser_gate, std::string::npos);
  ASSERT_NE(catalog_publish, std::string::npos);
  EXPECT_LT(null_browser_gate, catalog_publish);
  expect_missing(workspace, "project_assets().active_material()");

  const std::string refresh = source_slice(
      ui, "void UISystem::refresh_face_texture_material(",
      "void UISystem::configure_material_browser(");
  const auto visible_gate = refresh.find("!panel_presented(");
  const auto revision_gate = refresh.find(
      "if (!face_texture_panel_presenter_.needs_refresh(stamp))");
  const auto presentation = refresh.find("material_presentation_for(");
  const auto publish =
      refresh.find("face_texture_panel_presenter_.mark_presented(stamp)");
  ASSERT_NE(visible_gate, std::string::npos);
  ASSERT_NE(revision_gate, std::string::npos);
  ASSERT_NE(presentation, std::string::npos);
  ASSERT_NE(publish, std::string::npos);
  EXPECT_LT(visible_gate, revision_gate);
  EXPECT_LT(revision_gate, presentation);
  EXPECT_LT(presentation, publish);
  expect_contains(refresh, "->set_material_presentation(");
  expect_contains(refresh, ".reference = reference");
  expect_contains(refresh, "material_presentation_catalog_revision_");
  expect_missing(refresh, ".material_preview(");
  expect_missing(refresh, "make_qt_material_thumbnail(");
  expect_contains(ui,
                  "configure_asset_browser()");
  expect_contains(ui,
                  "configure_material_browser(face_texture_panel_->browser())");
  expect_contains(ui, "apply_material_to_selected_faces(");

  const std::string frame = source_slice(
      ui, "void UISystem::refresh(", "void UISystem::process_viewport_input(");
  const auto frame_catalog = frame.find("refresh_material_presentation_cache();");
  const auto frame_usage = frame.find("refresh_material_browser_usage();");
  ASSERT_NE(frame_catalog, std::string::npos);
  ASSERT_NE(frame_usage, std::string::npos);
  EXPECT_LT(frame_catalog, frame_usage);
  expect_missing(frame, "project_assets().active_material()");
  expect_missing(frame, "commands().available()");
  expect_missing(panel, "project_assets()");
  expect_missing(panel, "commands()");
  expect_missing(panel, "setWindowFlag(Qt::Tool");

  const std::string cache = source_slice(
      ui, "void UISystem::refresh_material_presentation_cache(",
      "void UISystem::revalidate_material_find_replace_presentations()");
  expect_contains(cache,
                  "if (editor_ == nullptr || !editor_->project().has_project())");
  expect_contains(cache, "const bool scene_available = active_scene_id != 0u");
  expect_contains(cache, "scene_available ? assets_ : nullptr");
  expect_contains(cache, "scene_available ? document_ : nullptr");
  expect_contains(cache, "scene_available ? modeling_ : nullptr");
  expect_contains(
      cache,
      "scene_available ? document_->material_binding_revision() : 0u");
  expect_missing(cache, "document_->curve_binding_revision()");
  expect_missing(cache, "active_scene_id == 0u) {");
}

TEST(UiLayoutGuardrailTests,
     InspectorMaterialSurfaceQueryTracksSolidifiedCurveBindings) {
  const std::string header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string refresh = source_slice(
      ui, "void UISystem::refresh_inspector_material_presentation()",
      "void UISystem::show_home()");

  expect_contains(header, "std::uint64_t material_binding_revision = 0u;");
  expect_contains(
      refresh, "scene_entity_has_material_surface(*document_, entity)");
  expect_contains(refresh, "mix(stamp.material_binding_revision)");
  expect_missing(refresh, "curve_binding_revision");
  const auto revision_gate = refresh.find(
      "if (!inspector_material_query_presenter_.needs_refresh(stamp))");
  const auto material_query = refresh.find("materials().used_materials(");
  const auto surface_query =
      refresh.find("scene_entity_has_material_surface(*document_, entity)");
  ASSERT_NE(revision_gate, std::string::npos);
  ASSERT_NE(material_query, std::string::npos);
  ASSERT_NE(surface_query, std::string::npos);
  EXPECT_LT(revision_gate, material_query);
  EXPECT_LT(revision_gate, surface_query);
}

TEST(UiLayoutGuardrailTests,
     NativeCoordinatorRefreshKeepsCurvePointerWhileDraftIsStillDragging) {
  const std::string panel = read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string coordinator = read_source_file(
      "src/editor/tools/native_viewport_tool_coordinator.cpp");
  const std::string process = source_slice(
      panel, "void QtViewportPanel::process_viewport_input(",
      "bool QtViewportPanel::sync_active_tool_from_editor(");
  expect_contains(process, "if (native_tools.refresh().changed)");
  expect_missing(process, "curve_.refresh()");
  expect_missing(process, "CurveToolService");

  const std::string refresh = source_slice(
      coordinator, "CommandResult NativeViewportToolCoordinator::refresh()",
      "NativeViewportToolPresentationSnapshot");
  expect_contains(refresh, "const CommandResult curve = curve_.refresh();");
  expect_contains(refresh, "linear.changed || path.changed ||");
  expect_contains(refresh, "(curve.changed && !curve_.dragging())");
  expect_contains(refresh, "interaction_.clear_drag_state()");
}

TEST(UiLayoutGuardrailTests,
     Phase4ViewportRowsKeepPrecedenceDecisionsInNamedOwners) {
  const std::string panel = read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string tool_projection = read_source_file(
      "src/editor/viewport/viewport_tool_overlay_projection.cpp");
  const std::string host_policy = read_source_file(
      "src/viewport/viewport_host_interaction_policy.hpp");
  const std::string interaction = read_source_file(
      "src/editor/viewport/viewport_interaction_controller.cpp");
  const std::string native_tools = read_source_file(
      "src/editor/tools/viewport_native_tool_interaction.cpp");
  const std::string scene_projection = read_source_file(
      "src/editor/viewport/viewport_scene_projection.cpp");

  expect_contains(panel, "ViewportKeybindingResolver::resolve(");
  expect_contains(panel, "resolve_viewport_input_pane({");
  expect_contains(panel, "interaction_controller_.handle_after_active_tool(");
  expect_contains(
      panel, "interaction_controller_.handle_tool_pointer_lifecycle(");
  expect_missing(panel, "interaction_controller_.handle_before_active_tool(");
  expect_missing(panel, "interaction_controller_.handle_native_tools(");
  expect_contains(panel,
                  "ViewportSceneProjection::point_light_pick_candidates(");
  const std::size_t suppress_hover = panel.find("const bool suppress_hover_work");
  const std::size_t billboard_candidates =
      panel.find("ViewportSceneProjection::point_light_pick_candidates(");
  ASSERT_NE(suppress_hover, std::string::npos);
  ASSERT_NE(billboard_candidates, std::string::npos);
  EXPECT_LT(suppress_hover, billboard_candidates);
  expect_contains(panel, "!suppress_hover_work && viewport_settings.bool_value(");
  expect_contains(panel, "point_light_pick_cache_valid_");
  expect_contains(panel, "point_light_pick_cached_scene_revision_");
  const std::size_t light_selection_gate = panel.find(
      "point_light_pick_cached_selection_revision_ != selection_revision");
  ASSERT_NE(light_selection_gate, std::string::npos);
  EXPECT_LT(light_selection_gate, billboard_candidates);
  expect_contains(panel,
                  "point_light_pick_cached_local_visibility_revision_");
  expect_contains(panel,
                  "Viewport rebuild point light pick candidates");
  expect_missing(panel, "ViewportToolOverlayProjection::append_box(");
  expect_contains(tool_projection,
                  "ViewportToolOverlayProjection::append_box(");
  expect_contains(tool_projection,
                  "build_dynamic_tool_overlay_frame() const");
  expect_contains(panel, ".focus_optional_bounds(");
  expect_contains(panel, "qt_submit_viewport_render_frames(");

  expect_contains(host_policy,
                  "locked owner -> activity -> hover -> focus -> workspace focus");
  expect_contains(interaction, "selection_.handle_frame(");
  expect_contains(interaction, "transform_gizmo_.handle_frame(");
  EXPECT_LT(interaction.find("selection_.prepare_frame("),
            interaction.find("transform_gizmo_.handle_frame("));
  const std::string selection_source = read_source_file(
      "src/editor/viewport/viewport_selection_interaction.cpp");
  const std::string prepare_selection = source_slice(
      selection_source, "ViewportSelectionInteraction::prepare_frame(",
      "QuaderSelectionSummary ViewportSelectionInteraction::commit_pending_pick(");
  const auto press_gate = prepare_selection.find("if (!frame.left_pressed)");
  const auto baseline_capture =
      prepare_selection.find("capture_box_selection_baseline()");
  ASSERT_NE(press_gate, std::string_view::npos);
  ASSERT_NE(baseline_capture, std::string_view::npos);
  EXPECT_LT(press_gate, baseline_capture);
  expect_contains(prepare_selection.substr(press_gate, baseline_capture - press_gate),
                  "return std::nullopt;");
  const auto idle_gate =
      prepare_selection.find("!frame.left_pressed && !frame.input.cycle_selection_depth_pressed");
  const auto scene_lookup =
      prepare_selection.find("scene_documents().active_scene_id()");
  ASSERT_NE(idle_gate, std::string_view::npos);
  ASSERT_NE(scene_lookup, std::string_view::npos);
  EXPECT_LT(idle_gate, scene_lookup);
  expect_contains(interaction, "prepared.selection_state =");
  expect_contains(interaction, "result.selection_after_pending_pick");
  expect_contains(panel, "before_tool.selection_after_pending_pick->summary");
  expect_contains(panel, "cancel_pending_selection_click()");
  expect_missing(prepare_selection, "commands().available(");
  expect_missing(prepare_selection, "query().entities(");
  expect_contains(native_tools, "linear_array_drag_point(");
  expect_contains(native_tools, "pivot_handle_hit(");
  expect_contains(native_tools, "radial_array->has_valid_center()");
  expect_contains(native_tools,
                  "record_commit(\"Radial Array apply\", lifecycle.commit_active())");
  expect_contains(scene_projection, "point_light_pick_candidates(");

  expect_missing(panel, "float screen_distance_squared(");
  expect_missing(panel, "bool pivot_handle_hit(");
  expect_missing(panel, "bool linear_array_handle_hit(");
  expect_missing(panel, "document_world_bounds(");
  expect_missing(panel, "point_light_pick_candidates(\n    const SceneDocument");
}

TEST(UiLayoutGuardrailTests,
     InterfaceScaleApplicationIsEventDrivenAndAbsentFromTheFrameLoop) {
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string refresh = source_slice(
      ui, "void UISystem::refresh(", "void UISystem::process_viewport_input(");
  expect_missing(refresh, "interface_scale");
  expect_missing(refresh, "apply_interface_scale");
  expect_missing(refresh, "apply_ui_scale_to_retained_surfaces");

  const std::string settings_change = source_slice(
      ui, "void UISystem::handle_settings_dialog_change(",
      "void UISystem::apply_viewport_settings_to_workspace(");
  expect_contains(settings_change, "QtSettingsChangeKind::InterfacePreview");
  expect_contains(settings_change, "apply_interface_scale(");
  expect_contains(settings_change, "apply_ui_scale_to_retained_surfaces(");
}

TEST(UiLayoutGuardrailTests,
     PublicInterfaceScaleRuntimeHookHasOneConditionalPersistenceSite) {
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string set_interface_scale_hook = source_slice(
      ui, ".set_interface_scale =", ".vsync_enabled =");
  ASSERT_FALSE(set_interface_scale_hook.empty());
  expect_contains(set_interface_scale_hook, "const bool changed =");
  expect_contains(set_interface_scale_hook, "if (changed)");
  EXPECT_EQ(count_occurrences(set_interface_scale_hook, "save_settings()"),
            1u)
      << "an effective public scale commit persists exactly once, while a "
         "no-op bypasses the changed branch";
}

TEST(UiLayoutGuardrailTests, VsyncChangesAreAppliedOutsideRetainedFrameRefresh) {
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string hook =
      source_slice(ui, ".set_vsync_enabled =", ".session_settings =");
  const std::string refresh = source_slice(
      ui, "void UISystem::refresh(", "void UISystem::process_viewport_input(");
  expect_contains(hook, "if (changed)");
  EXPECT_EQ(count_occurrences(hook, "save_settings()"), 1u);
  expect_missing(refresh, "set_vsync_enabled(");
  expect_missing(refresh, "settings_dialog_->refresh()");
  expect_contains(ui, "QtSettingsChangeKind::RenderingPreview");
  expect_contains(ui, "QtSettingsChangeKind::RenderingCommit");
}

TEST(UiLayoutGuardrailTests,
     DockContentMinimumWidthUsesTheAuthoredBaselineForEveryScaleRevision) {
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string create_dock = source_slice(
      ui, "QDockWidget &UISystem::create_dock(",
      "void UISystem::restore_window_state()");
  const std::string apply_retained = source_slice(
      ui, "void UISystem::apply_ui_scale_to_retained_surfaces(",
      "void UISystem::apply_viewport_settings_to_workspace()");
  const std::string refresh = source_slice(
      ui, "void UISystem::refresh(", "void UISystem::process_viewport_input(");

  expect_contains(ui, "constexpr int kDockContentMinimumWidth = 220");
  expect_contains(
      ui, "const int minimum_width = metrics.px(kDockContentMinimumWidth)");
  expect_contains(ui, "content.setMinimumWidth(minimum_width)");
  expect_contains(create_dock, "apply_dock_content_minimum_width(content,");
  expect_contains(create_dock, "ui_scale_controller_->metrics()");
  expect_contains(apply_retained,
                  "apply_dock_content_minimum_width(*content, metrics)");
  expect_missing(create_dock, "std::max(220, content.minimumWidth())");
  expect_missing(refresh, "apply_dock_content_minimum_width");
}

TEST(UiLayoutGuardrailTests,
     StatusSelectionGatesOwnerQueriesFormattingAndWidgetSettersByRevisions) {
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string refresh = source_slice(
      ui, "void UISystem::refresh_status_selection()",
      "void UISystem::refresh_status_bar()");
  const std::size_t gate = refresh.find(
      "if (!status_selection_presenter_.needs_refresh(stamp))");
  ASSERT_NE(gate, std::string::npos);
  for (const std::string_view revision : {
           "document_->revision()", "document_->selection_revision()",
           "modeling_->content_revision()", "modeling_->selection_revision()",
           "localization_.revision()"}) {
    const std::size_t observation = refresh.find(revision);
    ASSERT_NE(observation, std::string::npos) << revision;
    EXPECT_LT(observation, gate) << revision;
  }
  for (const std::string_view work : {
           "editor_->selection().summary()", "modeling_->selection_mode()",
           "document_->selected_entities()",
           "localization_.text(", "status_selection_->setText(text)",
           "status_selection_->setAccessibleName(text)"}) {
    const std::size_t query = refresh.find(work);
    ASSERT_NE(query, std::string::npos) << work;
    EXPECT_LT(gate, query) << work;
  }
  expect_contains(refresh, "if (status_selection_->text() != text)");
  expect_contains(refresh, "status_selection_presenter_.mark_presented(stamp)");
  const std::string rebind = source_slice(
      ui, "void UISystem::install_runtime_hooks(",
      "void UISystem::refresh_project_workspace(");
  expect_contains(rebind, "status_selection_presenter_.invalidate()");
  expect_missing(refresh, "commands().available()");
}
