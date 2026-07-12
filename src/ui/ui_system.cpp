////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_system.hpp"

#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "platform/window_placement.hpp"
#include "quader_app_version.hpp"
#include "scene/scene_document.hpp"
#include "ui/console_panel.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/outliner_panel.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_settings_dialog.hpp"
#include "ui/qt_tool_settings_window.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/ui_context.hpp"

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QDockWidget>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace quader {
namespace {

constexpr int kQtWindowStateVersion = 1;

QuaderApiVoidResult runtime_status(bool changed, std::string message = {}) {
  return {
      .ok = true,
      .changed = changed,
      .message = std::move(message),
      .mutation_kind =
          changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None,
  };
}

QuaderApiVoidResult runtime_failure(std::string message) {
  return {
      .ok = false,
      .changed = false,
      .message = std::move(message),
      .mutation_kind = QuaderMutationKind::None,
  };
}

std::optional<UIPanelId> panel_id_from_name(std::string_view name) {
  for (UIPanelId id : {UIPanelId::Viewport, UIPanelId::Outliner,
                       UIPanelId::Inspector, UIPanelId::Console}) {
    if (name == ui_panel_stable_id(id) || name == ui_panel_key(id)) {
      return id;
    }
  }
  return std::nullopt;
}

std::string status_bar_message(
    const ModelingLiveStatus &live_status,
    const LocalizationService &localization,
    const std::optional<QuaderApiStatus> &command_status) {
  if (live_status.kind ==
      ModelingLiveStatusKind::MergeByDistanceMergedVertices) {
    std::string message = localization.text(
        "ui.status.merge_by_distance.merged_vertices",
        "Merged {count} vertices");
    constexpr std::string_view kCountToken = "{count}";
    if (const std::size_t token = message.find(kCountToken);
        token != std::string::npos) {
      message.replace(token, kCountToken.size(),
                      std::to_string(live_status.count));
    }
    return message;
  }
  if (!command_status.has_value()) {
    return {};
  }
  if (!command_status->ok && command_status->message.empty()) {
    return localization.text("ui.status.command_failed", "Command failed.");
  }
  return command_status->message;
}

std::string read_binary_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return {};
  }
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool write_binary_file(const std::filesystem::path &path,
                       const QByteArray &bytes) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }
  output.write(bytes.constData(), bytes.size());
  return output.good();
}

} // namespace

UISystem::UISystem() = default;

UISystem::~UISystem() { shutdown(); }

bool UISystem::initialize(RenderSystem &render_system,
                          const std::filesystem::path &executable_dir) {
  if (initialized_) {
    return true;
  }

  render_system_ = &render_system;
  settings_paths_ = UISettings::resolve_paths(executable_dir);
  std::error_code error;
  std::filesystem::create_directories(settings_paths_.directory, error);
  settings_ = UISettings::load(settings_paths_.settings_file);
  localization_.load(executable_dir / "locales", settings_.locale());
  if (settings_.locale() != localization_.current_locale()) {
    settings_.set_locale(localization_.current_locale());
  }
  localized_locale_ = localization_.current_locale();
  viewport_settings_ =
      ViewportSettings::load(settings_paths_.viewport_settings_file);

  main_window_ = std::make_unique<QMainWindow>();
  main_window_->setObjectName(QStringLiteral("quaderMainWindow"));
  main_window_->setWindowTitle(QStringLiteral("Quader"));
  main_window_->setWindowIcon(QApplication::windowIcon());
  main_window_->setMinimumSize(kPlatformWindowMinimumWidth,
                               kPlatformWindowMinimumHeight);
  main_window_->setDockOptions(QMainWindow::AllowNestedDocks |
                               QMainWindow::AllowTabbedDocks |
                               QMainWindow::AnimatedDocks);

  viewport_panel_ = new QtViewportPanel(main_window_.get());
  viewport_panel_->set_viewport_settings(viewport_settings_);
  viewport_panel_->set_localization(localization_);
  main_window_->setCentralWidget(viewport_panel_);
  create_docks();

  settings_dialog_ = new QtSettingsDialog(main_window_.get());
  tool_settings_window_ = new QtToolSettingsWindow(main_window_.get());
  tool_settings_window_->set_localization(localization_);
  tool_settings_window_->hide();

  status_message_ = new QLabel(main_window_.get());
  status_message_->setObjectName(QStringLiteral("statusMessage"));
  status_version_ = new QLabel(
      QStringLiteral("v%1").arg(QString::fromUtf8(kQuaderAppVersion)),
      main_window_.get());
  status_version_->setObjectName(QStringLiteral("statusVersion"));
  main_window_->statusBar()->setObjectName(QStringLiteral("statusBar"));
  main_window_->statusBar()->addWidget(status_message_, 1);
  main_window_->statusBar()->addPermanentWidget(status_version_);

  restore_window_state();
  panels_.apply_settings(settings_);
  if (!viewport_panel_->initialize(render_system)) {
    shutdown();
    return false;
  }
  initialized_ = true;
  return true;
}

void UISystem::install_runtime_hooks(
    QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
    ModelingSessionService &modeling, EditorSelectionService &selection,
    PivotToolService &pivot, LinearArrayToolService &linear_array,
    AssetId default_material) {
  editor_ = &editor;
  document_ = &document;
  assets_ = &assets;
  modeling_ = &modeling;
  selection_ = &selection;
  pivot_ = &pivot;
  linear_array_ = &linear_array;
  default_material_ = default_material;
  outliner_refresh_valid_ = false;
  inspector_refresh_valid_ = false;

  ViewportWorkspaceService &workspace =
      QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
  viewport_panel_->attach_workspace(workspace);
  tool_settings_window_->bind(editor);
  settings_dialog_->set_context(
      settings_, viewport_settings_, localization_,
      [this](QtSettingsChangeKind kind) {
        handle_settings_dialog_change(kind);
      });

  main_menu_ = std::make_unique<QtMainMenu>();
  main_menu_->initialize(
      *main_window_->menuBar(),
      QtMainMenuContext{
          .editor = editor,
          .modeling = modeling,
          .viewport_settings = viewport_settings_,
          .localization = localization_,
          .request_quit = [this]() { main_window_->close(); },
          .show_settings = [this]() { show_settings_dialog(); },
          .panel_visible = [this](UIPanelId id) { return panel_visible(id); },
          .set_panel_visible =
              [this](UIPanelId id, bool visible) {
                set_panel_visible(id, visible);
              },
          .viewport_settings_changed =
              [this]() {
                (void)save_viewport_settings();
                synchronize_settings_surfaces(true);
              },
      });

  editor.viewports().set_runtime_hooks(QuaderViewportRuntimeHooks{
      .frame_all =
          [this](ViewportPaneId pane) {
            return viewport_panel_ != nullptr
                       ? viewport_panel_->frame_all(*document_, *assets_, pane)
                       : runtime_failure(localization_.text(
                             "ui.runtime.viewport_panel_unavailable",
                             "Viewport panel is not available."));
          },
      .frame_selection =
          [this](ViewportPaneId pane) {
            return viewport_panel_ != nullptr
                       ? viewport_panel_->frame_selection(*modeling_, pane)
                       : runtime_failure(localization_.text(
                             "ui.runtime.viewport_panel_unavailable",
                             "Viewport panel is not available."));
          },
  });
#ifndef NDEBUG
  editor.dev_tools().set_runtime_hooks(QuaderDevToolsRuntimeHooks{
      .set_clipboard_text =
          [](std::string_view text, std::string &error_message) {
            QClipboard *clipboard = QApplication::clipboard();
            if (clipboard == nullptr) {
              error_message = "Qt clipboard is unavailable.";
              return false;
            }
            clipboard->setText(QString::fromUtf8(
                text.data(), static_cast<qsizetype>(text.size())));
            error_message.clear();
            return true;
          },
  });
#endif
  editor.ui().set_runtime_hooks(QuaderUIRuntimeHooks{
      .is_panel_visible =
          [this](std::string_view panel) {
            const std::optional<UIPanelId> id = panel_id_from_name(panel);
            return id.has_value() && panel_visible(*id);
          },
      .set_panel_visible =
          [this](std::string_view panel, bool visible) {
            const std::optional<UIPanelId> id = panel_id_from_name(panel);
            if (!id.has_value()) {
              return runtime_failure(localization_.text(
                  "ui.runtime.unknown_panel", "Unknown UI panel."));
            }
            const bool before = panel_visible(*id);
            set_panel_visible(*id, visible);
            return runtime_status(before != panel_visible(*id));
          },
      .focus_panel =
          [this](std::string_view panel) {
            const std::optional<UIPanelId> id = panel_id_from_name(panel);
            if (!id.has_value()) {
              return runtime_failure(localization_.text(
                  "ui.runtime.unknown_panel", "Unknown UI panel."));
            }
            if (*id == UIPanelId::Viewport) {
              return runtime_status(viewport_panel_ != nullptr &&
                                    viewport_panel_->focus_active_pane());
            }
            return runtime_status(panels_.focus(*id));
          },
      .notify =
          [this](QuaderNotification notification) {
            main_window_->statusBar()->showMessage(
                QString::fromStdString(notification.message), 5000);
            return runtime_status(false);
          },
  });
  editor.preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{
      .locale = [this]() { return settings_.locale(); },
      .set_locale =
          [this](std::string locale) {
            if (!localization_.select_locale(locale)) {
              return runtime_failure(localization_.text(
                  "ui.runtime.unknown_locale", "Unknown locale."));
            }
            const bool changed =
                settings_.locale() != localization_.current_locale();
            settings_.set_locale(localization_.current_locale());
            if (changed) {
              (void)save_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .save =
          [this]() {
            const bool ui_saved = save_settings();
            const bool viewport_saved = save_viewport_settings();
            save_window_state();
            if (!ui_saved || !viewport_saved) {
              return runtime_failure(localization_.text(
                  "ui.runtime.preferences_save_failed",
                  "Preferences could not be saved."));
            }
            return runtime_status(
                false, localization_.text("ui.runtime.preferences_saved",
                                          "Preferences saved."));
          },
      .grid_snap_enabled = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::GridSnap);
      },
      .set_grid_snap_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::GridSnap, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .navigation_gizmo_visible =
          [this]() { return settings_.navigation_gizmo_visible(); },
      .set_navigation_gizmo_visible =
          [this](bool visible) {
            const bool changed =
                settings_.navigation_gizmo_visible() != visible;
            settings_.set_navigation_gizmo_visible(visible);
            if (changed) {
              (void)save_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
  });

  command_event_filter_ = std::make_unique<QtCommandEventFilter>(editor);
  if (QCoreApplication *application = QApplication::instance()) {
    application->installEventFilter(command_event_filter_.get());
  }
}

void UISystem::show() {
  if (main_window_ != nullptr && !main_window_->isVisible()) {
    main_window_->show();
  }
  if (auxiliary_window_state_.settings_dialog_visible &&
      settings_dialog_ != nullptr && !settings_dialog_->isVisible()) {
    show_settings_dialog();
  }
}

void UISystem::shutdown() {
  if (main_window_ == nullptr && render_system_ == nullptr) {
    return;
  }
  (void)save_settings();
  (void)save_viewport_settings();
  save_window_state();
  if (command_event_filter_ != nullptr) {
    if (QCoreApplication *application = QApplication::instance()) {
      application->removeEventFilter(command_event_filter_.get());
    }
    command_event_filter_.reset();
  }
  if (tool_settings_window_ != nullptr) {
    tool_settings_window_->unbind();
  }
  if (viewport_panel_ != nullptr) {
    viewport_panel_->shutdown();
  }
  panels_.set_visibility_changed_callback({});
  main_menu_.reset();
  main_window_.reset();
  viewport_panel_ = nullptr;
  outliner_panel_ = nullptr;
  inspector_panel_ = nullptr;
  console_panel_ = nullptr;
  settings_dialog_ = nullptr;
  tool_settings_window_ = nullptr;
  status_message_ = nullptr;
  status_version_ = nullptr;
  render_system_ = nullptr;
  editor_ = nullptr;
  document_ = nullptr;
  assets_ = nullptr;
  modeling_ = nullptr;
  selection_ = nullptr;
  pivot_ = nullptr;
  linear_array_ = nullptr;
  initialized_ = false;
}

void UISystem::refresh(const FrameTiming &frame_timing,
                       RenderFramePerformanceStats render_performance) {
  frame_timing_ = frame_timing;
  render_performance_ = render_performance;
  if (viewport_panel_ != nullptr) {
    viewport_panel_->set_render_performance_stats(render_performance_);
  }
  if (!initialized_ || editor_ == nullptr || document_ == nullptr ||
      assets_ == nullptr || modeling_ == nullptr || selection_ == nullptr) {
    return;
  }

  UIInspectorPreviewContext preview{.document = *document_, .assets = *assets_};
  UIContext context{
      .intent = {.editor = *editor_},
      .scene =
          {
              .document = *document_,
              .assets = *assets_,
              .modeling = *modeling_,
              .selection = *selection_,
              .default_material = default_material_,
          },
      .inspector_preview = &preview,
      .preferences =
          {
              .ui = settings_,
              .viewport = viewport_settings_,
              .ui_settings_changed = &ui_settings_changed_,
          },
      .localization = localization_,
  };
  const bool transform_drag_active = modeling_->transform_drag_active();
  const std::string &current_locale = localization_.current_locale();
  if (!transform_drag_active && panel_visible(UIPanelId::Outliner) &&
      (!outliner_refresh_valid_ ||
       outliner_scene_revision_ != document_->revision() ||
       outliner_locale_ != current_locale)) {
    outliner_panel_->refresh(context);
    outliner_scene_revision_ = document_->revision();
    outliner_locale_ = current_locale;
    outliner_refresh_valid_ = true;
  }
  if (!transform_drag_active && panel_visible(UIPanelId::Inspector)) {
    const ModelingSelectionSummary summary = modeling_->selection_summary();
    if (!inspector_refresh_valid_ ||
        inspector_scene_revision_ != document_->revision() ||
        inspector_modeling_content_revision_ != summary.content_revision ||
        inspector_modeling_selection_revision_ !=
            summary.selection_revision ||
        inspector_locale_ != current_locale) {
      inspector_panel_->refresh(context);
      inspector_scene_revision_ = document_->revision();
      inspector_modeling_content_revision_ = summary.content_revision;
      inspector_modeling_selection_revision_ = summary.selection_revision;
      inspector_locale_ = current_locale;
      inspector_refresh_valid_ = true;
    }
  }
  if (panel_visible(UIPanelId::Console)) {
    console_panel_->refresh(context);
  }
  tool_settings_window_->refresh();
  synchronize_auxiliary_window_visibility();
  refresh_status_bar();

  if (localized_locale_ != localization_.current_locale()) {
    localized_locale_ = localization_.current_locale();
    refresh_localized_chrome();
    main_menu_->rebuild();
    viewport_panel_->set_localization(localization_);
    tool_settings_window_->set_localization(localization_);
  }
  if (ui_settings_changed_) {
    ui_settings_changed_ = false;
    (void)save_settings();
  }
}

void UISystem::process_viewport_input() {
  if (!initialized_ || viewport_panel_ == nullptr || editor_ == nullptr ||
      document_ == nullptr || assets_ == nullptr || modeling_ == nullptr) {
    return;
  }
  viewport_panel_->process_input(*editor_, *document_, *assets_, *modeling_,
                                 default_material_, pivot_, linear_array_);
}

void UISystem::prepare_viewports(
    SceneRenderBridge &render_bridge,
    RenderOverlaySnapshotCache &snapshot_cache) {
  if (!initialized_ || editor_ == nullptr || document_ == nullptr ||
      assets_ == nullptr || modeling_ == nullptr) {
    return;
  }
  viewport_panel_->prepare_viewports(*editor_, render_bridge, snapshot_cache,
                                     *document_, *assets_, *modeling_, pivot_,
                                     linear_array_);
}

std::size_t UISystem::render_viewports() {
  return viewport_panel_ != nullptr ? viewport_panel_->render_viewports() : 0u;
}

bool UISystem::needs_continuous_rendering() const {
  return viewport_panel_ != nullptr &&
         viewport_panel_->needs_continuous_rendering();
}

void *UISystem::native_window_handle() const {
  if (main_window_ == nullptr) {
    return nullptr;
  }
  const WId id = main_window_->winId();
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));
}

bool UISystem::can_render_viewports() const {
  return main_window_ != nullptr && main_window_->isVisible() &&
         !main_window_->isMinimized();
}

bool UISystem::save_settings() {
  if (settings_paths_.settings_file.empty()) {
    return false;
  }
  panels_.write_settings(settings_);
  if (settings_.save(settings_paths_.settings_file)) {
    return true;
  }
  LOG_WARN("Unable to save UI settings to {}",
           settings_paths_.settings_file.string());
  return false;
}

bool UISystem::save_viewport_settings() {
  if (settings_paths_.viewport_settings_file.empty()) {
    return false;
  }
  if (viewport_settings_.save(settings_paths_.viewport_settings_file)) {
    return true;
  }
  LOG_WARN("Unable to save viewport settings to {}",
           settings_paths_.viewport_settings_file.string());
  return false;
}

void UISystem::save_window_state() {
  if (main_window_ != nullptr &&
      !settings_paths_.qt_window_state_file.empty()) {
    (void)write_binary_file(settings_paths_.qt_window_state_file,
                            main_window_->saveState(kQtWindowStateVersion));
  }
  save_auxiliary_window_state();
}

void UISystem::create_docks() {
  panels_.set_visibility_changed_callback([this]() {
    if (initialized_) {
      (void)save_settings();
      synchronize_settings_surfaces(false);
    }
  });
  outliner_panel_ = new OutlinerPanel(main_window_.get());
  inspector_panel_ = new InspectorPanel(main_window_.get());
  console_panel_ = new ConsolePanel(main_window_.get());
  QDockWidget &outliner = create_dock(
      UIPanelId::Outliner, *outliner_panel_, Qt::LeftDockWidgetArea);
  QDockWidget &inspector = create_dock(
      UIPanelId::Inspector, *inspector_panel_, Qt::RightDockWidgetArea);
  QDockWidget &console = create_dock(
      UIPanelId::Console, *console_panel_, Qt::BottomDockWidgetArea);
  main_window_->resizeDocks({&outliner, &inspector}, {340, 340},
                            Qt::Horizontal);
  main_window_->resizeDocks({&console}, {220}, Qt::Vertical);
}

QDockWidget &UISystem::create_dock(UIPanelId id, QWidget &content,
                                   int default_area) {
  auto *dock = new QDockWidget(QString::fromUtf8(ui_panel_key(id)),
                               main_window_.get());
  dock->setObjectName(
      QStringLiteral("dock_%1").arg(QString::fromUtf8(ui_panel_stable_id(id))));
  dock->setAllowedAreas(Qt::AllDockWidgetAreas);
  dock->setFeatures(QDockWidget::DockWidgetClosable |
                    QDockWidget::DockWidgetMovable |
                    QDockWidget::DockWidgetFloatable);
  dock->setMinimumWidth(220);
  dock->setWidget(&content);
  main_window_->addDockWidget(static_cast<Qt::DockWidgetArea>(default_area),
                              dock);
  panels_.add(id, *dock);
  QObject::connect(&content, &QWidget::windowTitleChanged, dock,
                   [dock](const QString &title) { dock->setWindowTitle(title); });
  return *dock;
}

void UISystem::restore_window_state() {
  if (main_window_ != nullptr &&
      !settings_paths_.qt_window_state_file.empty()) {
    const std::string bytes =
        read_binary_file(settings_paths_.qt_window_state_file);
    if (!bytes.empty()) {
      const QByteArray state(bytes.data(),
                             static_cast<qsizetype>(bytes.size()));
      (void)main_window_->restoreState(state, kQtWindowStateVersion);
    }
  }

  auxiliary_window_state_ = QtAuxiliaryWindowState::load(
      settings_paths_.qt_auxiliary_window_state_file);
  if (settings_dialog_ != nullptr &&
      !auxiliary_window_state_.settings_dialog_geometry.isEmpty()) {
    (void)settings_dialog_->restoreGeometry(
        auxiliary_window_state_.settings_dialog_geometry);
  }
  if (tool_settings_window_ != nullptr &&
      !auxiliary_window_state_.tool_settings_geometry.isEmpty()) {
    (void)tool_settings_window_->restoreGeometry(
        auxiliary_window_state_.tool_settings_geometry);
  }
}

void UISystem::save_auxiliary_window_state() {
  if (settings_paths_.qt_auxiliary_window_state_file.empty()) {
    return;
  }
  if (settings_dialog_ != nullptr) {
    auxiliary_window_state_.settings_dialog_geometry =
        settings_dialog_->saveGeometry();
  }
  if (tool_settings_window_ != nullptr) {
    auxiliary_window_state_.tool_settings_geometry =
        tool_settings_window_->saveGeometry();
  }
  (void)auxiliary_window_state_.save(
      settings_paths_.qt_auxiliary_window_state_file);
}

void UISystem::synchronize_auxiliary_window_visibility() {
  if (main_window_ == nullptr || !main_window_->isVisible()) {
    return;
  }

  bool changed = false;
  if (settings_dialog_ != nullptr) {
    const bool visible = settings_dialog_->isVisible();
    if (visible != auxiliary_window_state_.settings_dialog_visible) {
      auxiliary_window_state_.settings_dialog_visible = visible;
      changed = true;
    }
  }
  if (tool_settings_window_ != nullptr) {
    const bool visible = tool_settings_window_->isVisible();
    if (visible != auxiliary_window_state_.tool_settings_visible) {
      auxiliary_window_state_.tool_settings_visible = visible;
      changed = true;
    }
  }
  if (changed) {
    save_auxiliary_window_state();
  }
}

void UISystem::refresh_status_bar() {
  if (status_message_ == nullptr || editor_ == nullptr || modeling_ == nullptr) {
    return;
  }
  status_message_->setText(QString::fromStdString(status_bar_message(
      modeling_->live_status(), localization_,
      editor_->commands().last_status())));
}

void UISystem::refresh_localized_chrome() {
  const auto update_title = [this](UIPanelId id) {
    if (QDockWidget *dock = panels_.dock(id)) {
      dock->setWindowTitle(QString::fromStdString(localization_.text(
          ui_panel_localization_key(id), ui_panel_key(id))));
    }
  };
  update_title(UIPanelId::Outliner);
  update_title(UIPanelId::Inspector);
  update_title(UIPanelId::Console);
}

void UISystem::handle_settings_dialog_change(QtSettingsChangeKind kind) {
  switch (kind) {
  case QtSettingsChangeKind::ViewportPreview:
    return;
  case QtSettingsChangeKind::ViewportCommit:
    (void)save_viewport_settings();
    break;
  case QtSettingsChangeKind::InterfaceCommit:
    (void)save_settings();
    break;
  }
  if (main_menu_ != nullptr) {
    main_menu_->refresh();
  }
}

void UISystem::synchronize_settings_surfaces(bool refresh_dialog) {
  if (main_menu_ != nullptr) {
    main_menu_->refresh();
  }
  if (refresh_dialog && settings_dialog_ != nullptr &&
      settings_dialog_->isVisible()) {
    settings_dialog_->refresh();
  }
}

void UISystem::show_settings_dialog() {
  if (settings_dialog_ == nullptr) {
    return;
  }
  settings_dialog_->refresh();
  settings_dialog_->show();
  settings_dialog_->raise();
  settings_dialog_->activateWindow();
  if (!auxiliary_window_state_.settings_dialog_visible) {
    auxiliary_window_state_.settings_dialog_visible = true;
    save_auxiliary_window_state();
  }
}

bool UISystem::panel_visible(UIPanelId id) const {
  return id == UIPanelId::Viewport ? true : panels_.is_visible(id);
}

void UISystem::set_panel_visible(UIPanelId id, bool visible) {
  (void)panels_.set_visible(id, visible);
}

} // namespace quader
