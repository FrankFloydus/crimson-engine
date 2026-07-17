////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_system.hpp"

#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "core/scene_environment_catalog.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "platform/platform_file_dialog.hpp"
#include "platform/window_placement.hpp"
#include "quader_app_version.hpp"
#include "scene/scene_document.hpp"
#include "ui/console_panel.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/outliner_panel.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_main_window.hpp"
#include "ui/qt_material_browser_window.hpp"
#include "ui/qt_material_preview_surface.hpp"
#include "ui/qt_project_home_page.hpp"
#include "ui/qt_scene_workspace.hpp"
#include "ui/qt_selection_toolbar.hpp"
#include "ui/qt_settings_dialog.hpp"
#include "ui/qt_snapping_toolbar.hpp"
#include "ui/qt_standard_material_editor_window.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "ui/qt_tool_settings_window.hpp"
#include "ui/qt_viewport_display_toolbar.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/ui_context.hpp"

#include <QApplication>
#include <QAbstractButton>
#include <QByteArray>
#include <QClipboard>
#include <QColor>
#include <QDockWidget>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QMessageBox>
#include <QMenuBar>
#include <QRect>
#include <QPushButton>
#include <QStatusBar>
#include <QStackedWidget>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr int kQtWindowStateVersion = 1;

constexpr std::array<QuaderStandardMaterialTextureSlot, 4>
    kMaterialTextureSlots{
        QuaderStandardMaterialTextureSlot::Albedo,
        QuaderStandardMaterialTextureSlot::Roughness,
        QuaderStandardMaterialTextureSlot::Metallic,
        QuaderStandardMaterialTextureSlot::Normal,
};

QuaderStandardMaterialTextureSlot domain_texture_slot(
    QtStandardMaterialTextureSlot slot) {
  return kMaterialTextureSlots[static_cast<std::size_t>(slot)];
}

MaterialPreviewMeshKind preview_mesh_kind(
    QtStandardMaterialPreviewMesh mesh) {
  return static_cast<MaterialPreviewMeshKind>(mesh);
}

MaterialPreviewShadingMode preview_shading_mode(
    QtStandardMaterialPreviewShading shading) {
  return static_cast<MaterialPreviewShadingMode>(shading);
}

std::optional<ProjectRelativePath> project_relative_dialog_path(
    const std::filesystem::path &selected,
    const std::filesystem::path &project_root) {
  if (selected.empty() || project_root.empty()) {
    return std::nullopt;
  }
  const std::filesystem::path relative =
      selected.lexically_normal().lexically_relative(
          project_root.lexically_normal());
  ProjectIoResult<ProjectRelativePath> parsed =
      ProjectRelativePath::parse(relative);
  return parsed.ok() ? std::optional(parsed.take_value()) : std::nullopt;
}

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

  auto main_window = std::make_unique<QtMainWindow>();
  main_window->set_close_request_handler(
      [this]() { return allow_main_window_close(); });
  main_window_ = std::move(main_window);
  main_window_->setObjectName(QStringLiteral("quaderMainWindow"));
  main_window_->setWindowTitle(QStringLiteral("Quader"));
  main_window_->setWindowIcon(QApplication::windowIcon());
  main_window_->setMinimumSize(kPlatformWindowMinimumWidth,
                               kPlatformWindowMinimumHeight);
  main_window_->setDockOptions(QMainWindow::AllowNestedDocks |
                               QMainWindow::AllowTabbedDocks |
                               QMainWindow::AnimatedDocks);

  selection_toolbar_ = new QtSelectionToolbar(main_window_.get());
  selection_toolbar_->set_localization(localization_);
  selection_toolbar_->setEnabled(false);
  main_window_->addToolBar(Qt::TopToolBarArea, selection_toolbar_);

  snapping_toolbar_ = new QtSnappingToolbar(main_window_.get());
  snapping_toolbar_->set_localization(localization_);
  snapping_toolbar_->setEnabled(false);
  main_window_->addToolBar(Qt::TopToolBarArea, snapping_toolbar_);

  viewport_display_toolbar_ =
      new QtViewportDisplayToolbar(main_window_.get());
  viewport_display_toolbar_->set_localization(localization_);
  viewport_display_toolbar_->setEnabled(false);
  main_window_->addToolBar(Qt::TopToolBarArea, viewport_display_toolbar_);

  tool_toolbar_ = new QtToolToolbar(main_window_.get());
  tool_toolbar_->set_localization(localization_);
  tool_toolbar_->setEnabled(false);
  main_window_->addToolBar(Qt::LeftToolBarArea, tool_toolbar_);

  central_stack_ = new QStackedWidget(main_window_.get());
  central_stack_->setObjectName(QStringLiteral("mainCentralStack"));
  project_home_page_ = new QtProjectHomePage(central_stack_);
  scene_workspace_ = new QtSceneWorkspace(central_stack_);
  viewport_panel_ = new QtViewportPanel(scene_workspace_);
  viewport_panel_->set_viewport_settings(viewport_settings_);
  viewport_panel_->set_localization(localization_);
  scene_workspace_->set_viewport_widget(viewport_panel_);
  central_stack_->addWidget(project_home_page_);
  central_stack_->addWidget(scene_workspace_);
  central_stack_->setCurrentWidget(project_home_page_);
  main_window_->setCentralWidget(central_stack_);

  project_home_page_->set_new_project_requested_callback([this]() {
    if (editor_ != nullptr) {
      const QuaderApiVoidResult result = editor_->project().create();
      present_lifecycle_result(result);
      refresh_project_workspace();
    }
  });
  project_home_page_->set_open_project_requested_callback([this]() {
    if (editor_ != nullptr) {
      const QuaderApiVoidResult result = editor_->project().open({});
      present_lifecycle_result(result);
      refresh_project_workspace();
    }
  });
  project_home_page_->set_open_recent_project_requested_callback(
      [this](QString manifest_path) {
        if (editor_ != nullptr) {
          const QuaderApiVoidResult result = editor_->project().open(
              std::filesystem::path(manifest_path.toStdWString()));
          present_lifecycle_result(result);
          refresh_project_workspace();
        }
      });
  project_home_page_->set_remove_recent_project_requested_callback(
      [this](QString manifest_path) {
        if (editor_ != nullptr) {
          const QuaderApiVoidResult result =
              editor_->project().remove_recent_project(
                  std::filesystem::path(manifest_path.toStdWString()));
          present_lifecycle_result(result);
          refresh_project_workspace();
        }
      });
  scene_workspace_->set_new_scene_requested_callback([this]() {
    if (editor_ != nullptr) {
      const QuaderApiResult<SceneSessionId> result =
          editor_->scene_documents().new_scene();
      present_lifecycle_result(result);
      refresh_project_workspace();
    }
  });
  scene_workspace_->set_open_scene_requested_callback([this]() {
    if (editor_ != nullptr) {
      const QuaderApiResult<SceneSessionId> result =
          editor_->scene_documents().open();
      present_lifecycle_result(result);
      refresh_project_workspace();
    }
  });
  scene_workspace_->set_activate_scene_requested_callback(
      [this](QtSceneSessionId id) {
        if (editor_ != nullptr) {
          const QuaderApiVoidResult result =
              editor_->scene_documents().activate(
                  SceneSessionId::from_packed(id));
          present_lifecycle_result(result);
          refresh_project_workspace();
        }
      });
  scene_workspace_->set_close_scene_requested_callback(
      [this](QtSceneSessionId id) {
        if (editor_ != nullptr) {
          const QuaderApiVoidResult result = editor_->scene_documents().close(
              SceneSessionId::from_packed(id));
          present_lifecycle_result(result);
          refresh_project_workspace();
        }
      });
  scene_workspace_->set_exit_instance_edit_requested_callback([this]() {
    if (editor_ == nullptr) {
      return;
    }
    const QuaderApiVoidResult result =
        editor_->commands().execute(quader_command::kEditExitInstanceEdit);
    present_lifecycle_result(result);
    refresh_project_workspace();
  });
  create_docks();

  settings_dialog_ = new QtSettingsDialog(main_window_.get());
  material_browser_window_ =
      new QtMaterialBrowserWindow(main_window_.get());
  material_browser_window_->set_presentation_state_changed_callback(
      [this]() { save_auxiliary_window_state(); });
  material_browser_window_->set_activate_material_callback(
      [this](QString stable_id) {
        if (editor_ == nullptr) {
          return;
        }
        const std::optional<ProjectAssetId> id =
            ProjectAssetId::parse(stable_id.toStdString());
        if (!id.has_value()) {
          return;
        }
        const QuaderApiVoidResult result =
            editor_->project_assets().set_active_material(*id);
        present_lifecycle_result(result);
        refresh_project_workspace();
      });
  material_browser_window_->set_open_material_callback(
      [this](QString stable_id) {
        open_standard_material(stable_id.toStdString());
      });
  material_browser_window_->hide();
  standard_material_editor_window_ =
      new QtStandardMaterialEditorWindow(main_window_.get());
  material_preview_surface_ = new QtMaterialPreviewSurface();
  QWidget *preview_container = QWidget::createWindowContainer(
      material_preview_surface_, standard_material_editor_window_);
  preview_container->setObjectName(
      QStringLiteral("standardMaterialEditor.previewSurfaceContainer"));
  preview_container->setFocusPolicy(Qt::StrongFocus);
  standard_material_editor_window_->install_preview_widget(preview_container);
  if (!material_preview_surface_->initialize(render_system)) {
    LOG_ERROR("Unable to initialize the Standard Material preview surface");
    standard_material_editor_window_->install_preview_widget(nullptr);
    material_preview_surface_ = nullptr;
  }
  standard_material_editor_window_->set_file_action_callback(
      [this](QtStandardMaterialEditorFileAction action) {
        handle_standard_material_file_action(action);
      });
  standard_material_editor_window_->set_name_edited_callback(
      [this](QString name) {
        if (editor_ == nullptr) {
          return;
        }
        const QuaderApiVoidResult result =
            editor_->standard_material_documents().set_name(
                name.toStdString());
        present_lifecycle_result(result);
        refresh_standard_material_editor();
      });
  standard_material_editor_window_->set_albedo_edited_callback(
      [this](QColor color) {
        if (editor_ == nullptr) {
          return;
        }
        const QuaderApiVoidResult result =
            editor_->standard_material_documents().set_base_color(
                {static_cast<float>(color.redF()),
                 static_cast<float>(color.greenF()),
                 static_cast<float>(color.blueF())});
        present_lifecycle_result(result);
        refresh_standard_material_editor();
      });
  standard_material_editor_window_->set_scalar_edited_callback(
      [this](QtStandardMaterialScalar scalar, double value) {
        handle_standard_material_scalar(scalar, value);
      });
  standard_material_editor_window_->set_normal_convention_edited_callback(
      [this](bool direct_x) {
        if (editor_ == nullptr) {
          return;
        }
        const QuaderApiVoidResult result =
            editor_->standard_material_documents().set_normal_convention(
                direct_x ? QuaderNormalMapConvention::DirectX
                         : QuaderNormalMapConvention::OpenGL);
        present_lifecycle_result(result);
        refresh_standard_material_editor();
      });
  standard_material_editor_window_->set_texture_action_callback(
      [this](QtStandardMaterialTextureSlot slot,
             QtStandardMaterialTextureAction action) {
        handle_standard_material_texture(slot, action);
      });
  standard_material_editor_window_->set_texture_transform_edited_callback(
      [this](QtStandardMaterialTextureTransformComponent component,
             double value) {
        handle_standard_material_texture_transform(component, value);
      });
  standard_material_editor_window_
      ->set_presentation_state_changed_callback(
          [this]() {
            if (material_preview_surface_ != nullptr) {
              (void)material_preview_surface_->set_preview_mesh(
                  preview_mesh_kind(
                      standard_material_editor_window_->preview_mesh()));
              (void)material_preview_surface_->set_preview_shading(
                  preview_shading_mode(
                      standard_material_editor_window_->preview_shading()));
              (void)material_preview_surface_->set_preview_environment(
                  standard_material_editor_window_->preview_environment()
                      .toStdString());
              (void)material_preview_surface_
                  ->set_show_environment_background(
                      standard_material_editor_window_
                          ->preview_environment_background_visible());
            }
            save_auxiliary_window_state();
          });
  standard_material_editor_window_->set_preview_reset_callback([this]() {
    if (material_preview_surface_ != nullptr) {
      (void)material_preview_surface_->reset_preview();
    }
  });
  standard_material_editor_window_->set_close_request_handler(
      [this]() { return resolve_standard_material_document(true); });
  standard_material_editor_window_->hide();
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

  refresh_localized_chrome();
  restore_window_state();
  panels_.apply_settings(settings_);
  initialized_ = true;
  show_home();
  return true;
}

void UISystem::install_runtime_hooks(
    QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
    ModelingSessionService &modeling, EditorSelectionService &selection,
    PivotToolService &pivot, LinearArrayToolService &linear_array,
    RadialArrayToolService &radial_array,
    ViewportPointPlacementService &point_placement, AssetId default_material) {
  editor_ = &editor;
  document_ = &document;
  assets_ = &assets;
  modeling_ = &modeling;
  selection_ = &selection;
  pivot_ = &pivot;
  linear_array_ = &linear_array;
  radial_array_ = &radial_array;
  point_placement_ = &point_placement;
  default_material_ = default_material;
  outliner_refresh_valid_ = false;
  inspector_refresh_valid_ = false;
  toolbar_refresh_valid_ = false;

  selection_toolbar_->set_mode_requested_callback(
      [this](QuaderSelectionMode mode) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(editor_->selection().set_mode(mode));
        const QuaderSelectionSummary selection =
            editor_->selection().summary();
        selection_toolbar_->synchronize(*editor_, selection.mode, true);
        tool_toolbar_->synchronize(*editor_, selection, true);
      });
  tool_toolbar_->set_tool_requested_callback(
      [this](std::string_view tool_id) {
        if (editor_ == nullptr) {
          return;
        }
        const std::string command_id =
            editor_->commands().command_id_for_tool_activation(tool_id);
        present_lifecycle_result(editor_->commands().execute(command_id));
        tool_toolbar_->synchronize(*editor_, editor_->selection().summary(),
                                   true);
      });
  snapping_toolbar_->set_grid_preset_requested_callback(
      [this](int preset) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(editor_->viewports().set_grid_preset(preset));
        snapping_toolbar_->synchronize(*editor_, true);
      });
  snapping_toolbar_->set_rotation_snap_degrees_requested_callback(
      [this](double degrees) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(
            editor_->preferences().set_rotation_snap_degrees(degrees));
      });
  viewport_display_toolbar_->set_command_requested_callback(
      [this](QuaderCommandId command_id) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(editor_->commands().execute(command_id));
        viewport_display_toolbar_->synchronize(*editor_, true);
      });
  snapping_toolbar_->set_snapping_enabled_requested_callback(
      [this](bool enabled) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(
            editor_->preferences().set_snapping_enabled(enabled));
      });
  snapping_toolbar_->set_grid_snap_enabled_requested_callback(
      [this](bool enabled) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(
            editor_->preferences().set_grid_snap_enabled(enabled));
      });
  snapping_toolbar_->set_vertex_snap_enabled_requested_callback(
      [this](bool enabled) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(
            editor_->preferences().set_vertex_snap_enabled(enabled));
      });
  snapping_toolbar_->set_rotation_snap_enabled_requested_callback(
      [this](bool enabled) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(
            editor_->preferences().set_rotation_snap_enabled(enabled));
      });

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
          .selection_toolbar_visible =
              [this]() {
                return selection_toolbar_ != nullptr &&
                       selection_toolbar_->isVisible();
              },
          .set_selection_toolbar_visible =
              [this](bool visible) {
                if (selection_toolbar_ != nullptr) {
                  selection_toolbar_->setVisible(visible);
                }
              },
          .snapping_toolbar_visible =
              [this]() {
                return snapping_toolbar_ != nullptr &&
                       snapping_toolbar_->isVisible();
              },
          .set_snapping_toolbar_visible =
              [this](bool visible) {
                if (snapping_toolbar_ != nullptr) {
                  snapping_toolbar_->setVisible(visible);
                }
              },
          .viewport_display_toolbar_visible =
              [this]() {
                return viewport_display_toolbar_ != nullptr &&
                       viewport_display_toolbar_->isVisible();
              },
          .set_viewport_display_toolbar_visible =
              [this](bool visible) {
                if (viewport_display_toolbar_ != nullptr) {
                  viewport_display_toolbar_->setVisible(visible);
                }
              },
          .tool_toolbar_visible =
              [this]() {
                return tool_toolbar_ != nullptr && tool_toolbar_->isVisible();
              },
          .set_tool_toolbar_visible =
              [this](bool visible) {
                if (tool_toolbar_ != nullptr) {
                  tool_toolbar_->setVisible(visible);
                }
              },
          .viewport_settings_changed =
              [this]() {
                (void)save_viewport_settings();
                synchronize_settings_surfaces(true);
              },
          .show_material_browser = [this]() { show_material_browser(); },
          .material_browser_available =
              [this]() {
                return !home_visible_ && editor_ != nullptr &&
                       editor_->project().has_project();
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
          [this](std::string_view text, std::string &error_message) {
            QClipboard *clipboard = QApplication::clipboard();
            if (clipboard == nullptr) {
              error_message = localization_.text(
                  "ui.runtime.platform_unavailable",
                  "UI platform is not available.");
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
      .snapping_enabled = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::SnappingEnabled);
      },
      .set_snapping_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::SnappingEnabled, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .grid_visible = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::ShowGrid);
      },
      .set_grid_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowGrid, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .gizmos_visible = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::ShowGizmos);
      },
      .set_gizmos_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowGizmos, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .mesh_grid_visible = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::ShowMeshGrid);
      },
      .set_mesh_grid_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowMeshGrid, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .overlays_visible = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::ShowOverlays);
      },
      .set_overlays_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowOverlays, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
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
      .vertex_snap_enabled = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::VertexSnap);
      },
      .set_vertex_snap_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::VertexSnap, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .rotation_snap_enabled = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::RotationSnap);
      },
      .set_rotation_snap_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::RotationSnap, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .rotation_snap_degrees = [this]() {
        return static_cast<double>(viewport_settings_.float_value(
            ViewportFloatSettingId::RotationSnapDegrees));
      },
      .set_rotation_snap_degrees =
          [this](double degrees) {
            const bool changed = viewport_settings_.set_float_value(
                ViewportFloatSettingId::RotationSnapDegrees,
                static_cast<float>(degrees));
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

  if (command_event_filter_ != nullptr) {
    if (QCoreApplication *application = QApplication::instance()) {
      application->removeEventFilter(command_event_filter_.get());
    }
    command_event_filter_.reset();
  }
  command_event_filter_ = std::make_unique<QtCommandEventFilter>(
      editor, [this]() {
        return viewport_panel_ != nullptr ? viewport_panel_->tool_state()
                                          : ViewportToolState{};
      });
  command_event_filter_->set_command_input_owner_provider([this]() {
    return standard_material_editor_window_ != nullptr &&
           standard_material_editor_window_->isVisible() &&
           standard_material_editor_window_->isActiveWindow();
  });
  if (QCoreApplication *application = QApplication::instance()) {
    application->installEventFilter(command_event_filter_.get());
  }
  refresh_project_workspace();
}

void UISystem::refresh_project_workspace() {
  if (editor_ == nullptr || project_home_page_ == nullptr ||
      scene_workspace_ == nullptr) {
    return;
  }

  if (home_visible_) {
    std::vector<QtRecentProjectItem> recent;
    for (const QuaderRecentProject &project :
         editor_->project().recent_projects()) {
      recent.push_back({
          .name = QString::fromStdString(project.name),
          .manifest_path =
              QString::fromStdWString(project.manifest_path.wstring()),
          .available = project.available,
      });
    }
    project_home_page_->set_recent_projects(std::move(recent));
  }

  std::vector<QtSceneTabItem> tabs;
  std::optional<QtSceneSessionId> active;
  for (const QuaderSceneDocumentInfo &scene :
       editor_->scene_documents().open_scenes()) {
    tabs.push_back({
        .session_id = scene.session_id.packed(),
        .name = QString::fromStdString(scene.name),
        .project_relative_path =
            scene.path.has_value()
                ? QString::fromStdString(scene.path->portable_string())
                : QString{},
        .dirty = scene.dirty,
    });
    if (scene.active) {
      active = scene.session_id.packed();
    }
  }
  if (scene_workspace_->scene_tabs() != tabs ||
      scene_workspace_->active_scene() != active) {
    (void)scene_workspace_->set_scene_tabs(std::move(tabs), active);
  }

  const QuaderInstanceEditState instance_edit =
      editor_->modeling().instance_edit_state();
  if (instance_edit.active) {
    scene_workspace_->set_instance_edit_breadcrumb(QtInstanceEditBreadcrumb{
        .scene_name = QString::fromStdString(instance_edit.scene_name),
        .instance_name = QString::fromStdString(instance_edit.instance_name),
    });
  } else {
    scene_workspace_->set_instance_edit_breadcrumb(std::nullopt);
  }

  if (editor_->project().has_project() &&
      material_browser_window_ != nullptr) {
    const QuaderApiResult<std::vector<QuaderProjectMaterialInfo>> materials =
        editor_->project_assets().materials();
    const QuaderApiResult<QuaderProjectMaterialInfo> active_material =
        editor_->project_assets().active_material();
    if (materials.ok && active_material.ok) {
      std::uint64_t signature = 1469598103934665603ull;
      const auto mix = [&signature](std::uint64_t value) {
        signature ^= value;
        signature *= 1099511628211ull;
      };
      const QuaderProjectMetadata project = editor_->project().metadata();
      const std::string project_identity =
          project.uuid + '\n' +
          editor_->project().manifest_path().generic_string();
      for (const char character : project_identity) {
        mix(static_cast<unsigned char>(character));
      }
      for (const QuaderProjectMaterialInfo &material : materials.value()) {
        for (const char character : material.id.str()) {
          mix(static_cast<unsigned char>(character));
        }
        mix(static_cast<std::uint64_t>(material.kind));
        mix(material.content_revision);
        mix(static_cast<std::uint64_t>(material.qmat_size_bytes));
      }
      const std::string active_id(active_material.value().id.str());
      if (signature != material_browser_catalog_signature_) {
        std::vector<QtMaterialBrowserItem> items;
        items.reserve(materials.value().size());
        for (const QuaderProjectMaterialInfo &material : materials.value()) {
          QByteArray preview_bytes;
          std::uint64_t preview_revision = material.content_revision;
          const QuaderApiResult<QuaderProjectMaterialPreview> preview =
              editor_->project_assets().material_preview(material.id);
          if (preview.ok && !preview.value().encoded_bytes.empty()) {
            preview_revision = preview.value().content_revision;
            preview_bytes = QByteArray(
                reinterpret_cast<const char *>(
                    preview.value().encoded_bytes.data()),
                static_cast<qsizetype>(
                    preview.value().encoded_bytes.size()));
          }
          items.push_back(QtMaterialBrowserItem{
              .stable_id = QString::fromStdString(
                  std::string(material.id.str())),
              .name = QString::fromStdString(material.name),
              .project_relative_path =
                  QString::fromStdString(material.project_relative_path),
              .type_display_name = QString::fromStdString(
                  localization_.text(
                      material.kind == QuaderProjectMaterialKind::Standard
                          ? "ui.material_browser.standard_material_type"
                          : "ui.material_browser.graph_material_type",
                      material.kind == QuaderProjectMaterialKind::Standard
                          ? "Standard Material"
                          : "Graph Material")),
              .qmat_size_bytes =
                  static_cast<qint64>(material.qmat_size_bytes),
              .encoded_albedo = std::move(preview_bytes),
              .fallback_color = QColor::fromRgbF(
                  material.base_color[0], material.base_color[1],
                  material.base_color[2]),
              .content_revision = preview_revision,
          });
        }
        material_browser_window_->set_materials(
            std::move(items), QString::fromStdString(active_id));
        material_browser_catalog_signature_ = signature;
        material_browser_active_id_ = active_id;
      } else if (active_id != material_browser_active_id_) {
        material_browser_window_->set_active_material(
            QString::fromStdString(active_id));
        material_browser_active_id_ = active_id;
      }
    }
  }

  refresh_standard_material_editor();

  if (editor_->project().has_project()) {
    show_workspace();
  } else {
    show_home();
  }
}

void UISystem::show_home() {
  const bool presentation_changed = !home_visible_;
  if (presentation_changed) {
    capture_workspace_window_state();
  }
  home_visible_ = true;
  if (presentation_changed && material_browser_window_ != nullptr) {
    material_browser_window_->hide();
    material_browser_window_->clear_materials();
    material_browser_catalog_signature_ = 0;
    material_browser_active_id_.clear();
  }
  if (standard_material_editor_window_ != nullptr) {
    standard_material_editor_window_->hide();
    standard_material_editor_window_->clear_document();
  }
  if (central_stack_ != nullptr && project_home_page_ != nullptr) {
    central_stack_->setCurrentWidget(project_home_page_);
  }
  if (main_window_ != nullptr) {
    main_window_->menuBar()->hide();
    main_window_->statusBar()->hide();
  }
  if (selection_toolbar_ != nullptr) {
    selection_toolbar_->setEnabled(false);
    selection_toolbar_->hide();
  }
  if (snapping_toolbar_ != nullptr) {
    snapping_toolbar_->setEnabled(false);
    snapping_toolbar_->hide();
  }
  if (viewport_display_toolbar_ != nullptr) {
    viewport_display_toolbar_->setEnabled(false);
    viewport_display_toolbar_->hide();
  }
  if (tool_toolbar_ != nullptr) {
    tool_toolbar_->setEnabled(false);
    tool_toolbar_->hide();
  }
  for (UIPanelId id :
       {UIPanelId::Outliner, UIPanelId::Inspector, UIPanelId::Console}) {
    if (QDockWidget *dock = panels_.dock(id)) {
      dock->hide();
    }
  }
  if (presentation_changed && main_menu_ != nullptr) {
    main_menu_->refresh();
  }
}

void UISystem::show_workspace() {
  const bool presentation_changed = home_visible_;
  if (!viewport_initialized_ && viewport_panel_ != nullptr &&
      render_system_ != nullptr) {
    if (!viewport_panel_->initialize(*render_system_)) {
      LOG_ERROR("Unable to initialize the native viewport surface");
      return;
    }
    viewport_initialized_ = true;
    if (main_window_ != nullptr && main_window_->isVisible()) {
      prepare_startup_presentation();
    }
  }
  home_visible_ = false;
  if (central_stack_ != nullptr && scene_workspace_ != nullptr) {
    central_stack_->setCurrentWidget(scene_workspace_);
  }
  if (main_window_ != nullptr) {
    main_window_->menuBar()->show();
    main_window_->statusBar()->show();
  }
  if (selection_toolbar_ != nullptr) {
    selection_toolbar_->setEnabled(true);
    selection_toolbar_->setVisible(selection_toolbar_workspace_visible_);
  }
  if (snapping_toolbar_ != nullptr) {
    snapping_toolbar_->setEnabled(true);
    snapping_toolbar_->setVisible(snapping_toolbar_workspace_visible_);
  }
  if (viewport_display_toolbar_ != nullptr) {
    viewport_display_toolbar_->setEnabled(true);
    viewport_display_toolbar_->setVisible(
        viewport_display_toolbar_workspace_visible_);
  }
  if (tool_toolbar_ != nullptr) {
    tool_toolbar_->setEnabled(true);
    tool_toolbar_->setVisible(tool_toolbar_workspace_visible_);
  }
  if (presentation_changed && editor_ != nullptr) {
    const QuaderSelectionSummary selection = editor_->selection().summary();
    if (selection_toolbar_ != nullptr) {
      selection_toolbar_->synchronize(*editor_, selection.mode, true);
    }
    if (snapping_toolbar_ != nullptr) {
      snapping_toolbar_->synchronize(*editor_, true);
    }
    if (viewport_display_toolbar_ != nullptr) {
      viewport_display_toolbar_->synchronize(*editor_, true);
    }
    if (tool_toolbar_ != nullptr) {
      tool_toolbar_->synchronize(*editor_, selection, true);
    }
  }
  panels_.apply_settings(settings_);
  if (presentation_changed && main_menu_ != nullptr) {
    main_menu_->refresh();
  }
}

QuaderDirtyProjectDecision
UISystem::confirm_dirty_project_replacement(std::string_view action_id) {
  const bool opening = action_id == "project.open";
  const bool closing = action_id == "application.close";
  QMessageBox confirmation(
      QMessageBox::Warning,
      QString::fromStdString(localization_.text(
          "ui.project.unsaved.title", "Unsaved Changes")),
      QString::fromStdString(localization_.text(
          opening   ? "ui.project.unsaved.open"
          : closing ? "ui.project.unsaved.close"
                    : "ui.project.unsaved.new",
          opening   ? "Save changes before opening another project?"
          : closing ? "Save changes before closing Quader?"
                    : "Save changes before creating a new project?")),
      QMessageBox::NoButton, main_window_.get());
  confirmation.setObjectName(QStringLiteral("project_unsaved_confirmation"));
  confirmation.setWindowModality(Qt::WindowModal);
  QPushButton *save = confirmation.addButton(
      QString::fromStdString(
          localization_.text("ui.project.unsaved.save", "Save")),
      QMessageBox::AcceptRole);
  QPushButton *discard = confirmation.addButton(
      QString::fromStdString(
          localization_.text("ui.project.unsaved.discard", "Discard")),
      QMessageBox::DestructiveRole);
  QPushButton *cancel = confirmation.addButton(
      QString::fromStdString(
          localization_.text("ui.project.unsaved.cancel", "Cancel")),
      QMessageBox::RejectRole);
  confirmation.setEscapeButton(cancel);
  confirmation.exec();
  if (confirmation.clickedButton() == save) {
    return QuaderDirtyProjectDecision::Save;
  }
  if (confirmation.clickedButton() == discard) {
    return QuaderDirtyProjectDecision::Discard;
  }
  return QuaderDirtyProjectDecision::Cancel;
}

QuaderDirtySceneDecision
UISystem::confirm_dirty_scene(const QuaderSceneDocumentInfo &scene,
                              std::string_view action_id) {
  (void)action_id;
  QMessageBox confirmation(
      QMessageBox::Warning,
      QString::fromStdString(localization_.text(
          "ui.scene.unsaved.title", "Unsaved Scene")),
      QString::fromStdString(scene.name + " has unsaved changes. Save it?"),
      QMessageBox::NoButton, main_window_.get());
  confirmation.setObjectName(QStringLiteral("scene_unsaved_confirmation"));
  confirmation.setWindowModality(Qt::WindowModal);
  QPushButton *save = confirmation.addButton(
      QString::fromStdString(
          localization_.text("ui.scene.unsaved.save", "Save")),
      QMessageBox::AcceptRole);
  QPushButton *discard = confirmation.addButton(
      QString::fromStdString(
          localization_.text("ui.scene.unsaved.discard", "Discard")),
      QMessageBox::DestructiveRole);
  QPushButton *cancel = confirmation.addButton(
      QString::fromStdString(
          localization_.text("ui.scene.unsaved.cancel", "Cancel")),
      QMessageBox::RejectRole);
  confirmation.setEscapeButton(cancel);
  confirmation.exec();
  if (confirmation.clickedButton() == save) {
    return QuaderDirtySceneDecision::Save;
  }
  if (confirmation.clickedButton() == discard) {
    return QuaderDirtySceneDecision::Discard;
  }
  return QuaderDirtySceneDecision::Cancel;
}

bool UISystem::prepare_standard_material_project_transition(
    std::string_view action_id) {
  (void)action_id;
  return resolve_standard_material_document(true);
}

bool UISystem::allow_main_window_close() {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return true;
  }
  if (!resolve_standard_material_document(true)) {
    return false;
  }
  const QuaderApiVoidResult closed = editor_->project().close();
  return closed.ok() && !editor_->project().has_project();
}

void UISystem::show() {
  if (main_window_ != nullptr && !main_window_->isVisible()) {
    if (!home_visible_) {
      prepare_startup_presentation();
    }
    main_window_->show();
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
  if (material_preview_surface_ != nullptr) {
    material_preview_surface_->shutdown();
  }
  panels_.set_visibility_changed_callback({});
  main_menu_.reset();
  main_window_.reset();
  startup_presentation_ = {};
  startup_presentation_started_ = {};
  startup_expected_maximized_ = false;
  viewport_panel_ = nullptr;
  central_stack_ = nullptr;
  project_home_page_ = nullptr;
  scene_workspace_ = nullptr;
  selection_toolbar_ = nullptr;
  snapping_toolbar_ = nullptr;
  viewport_display_toolbar_ = nullptr;
  tool_toolbar_ = nullptr;
  workspace_window_state_.clear();
  outliner_panel_ = nullptr;
  inspector_panel_ = nullptr;
  console_panel_ = nullptr;
  settings_dialog_ = nullptr;
  material_browser_window_ = nullptr;
  material_preview_surface_ = nullptr;
  standard_material_editor_window_ = nullptr;
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
  radial_array_ = nullptr;
  point_placement_ = nullptr;
  initialized_ = false;
  viewport_initialized_ = false;
}

void UISystem::refresh(const FrameTiming &frame_timing,
                       RenderFramePerformanceStats render_performance) {
  frame_timing_ = frame_timing;
  render_performance_ = render_performance;
  refresh_project_workspace();
  if (editor_ != nullptr) {
    const std::string active_tool = editor_->tools().active_tool();
    const bool owner_state_changed =
        !toolbar_refresh_valid_ || document_ == nullptr || modeling_ == nullptr ||
        toolbar_scene_revision_ != document_->revision() ||
        toolbar_scene_selection_revision_ != document_->selection_revision() ||
        toolbar_modeling_content_revision_ != modeling_->content_revision() ||
        toolbar_modeling_selection_revision_ !=
            modeling_->selection_revision();
    const bool active_tool_changed =
        !toolbar_refresh_valid_ || toolbar_active_tool_ != active_tool;
    if (owner_state_changed || active_tool_changed) {
      const QuaderSelectionSummary selection = editor_->selection().summary();
      if (selection_toolbar_ != nullptr && owner_state_changed) {
        selection_toolbar_->synchronize(*editor_, selection.mode);
      }
      if (tool_toolbar_ != nullptr) {
        tool_toolbar_->synchronize(*editor_, selection);
      }
      if (document_ != nullptr && modeling_ != nullptr) {
        toolbar_scene_revision_ = document_->revision();
        toolbar_scene_selection_revision_ = document_->selection_revision();
        toolbar_modeling_content_revision_ = modeling_->content_revision();
        toolbar_modeling_selection_revision_ =
            modeling_->selection_revision();
        toolbar_active_tool_ = active_tool;
        toolbar_refresh_valid_ = true;
      }
    }
    if (snapping_toolbar_ != nullptr) {
      snapping_toolbar_->synchronize(*editor_);
    }
    if (viewport_display_toolbar_ != nullptr) {
      viewport_display_toolbar_->synchronize(*editor_);
    }
  }
  if (viewport_panel_ != nullptr) {
    viewport_panel_->set_render_performance_stats(render_performance_);
  }
  advance_startup_presentation();
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
  if (home_visible_ || !initialized_ || viewport_panel_ == nullptr || editor_ == nullptr ||
      document_ == nullptr || assets_ == nullptr || modeling_ == nullptr) {
    return;
  }
  viewport_panel_->process_input(*editor_, *document_, *assets_, *modeling_,
                                 default_material_, pivot_, linear_array_,
                                 radial_array_, point_placement_,
                                 frame_timing_.delta_seconds);
}

void UISystem::prepare_viewports(
    SceneRenderBridge &render_bridge,
    RenderOverlaySnapshotCache &snapshot_cache) {
  if (home_visible_ || !initialized_ || editor_ == nullptr || document_ == nullptr ||
      assets_ == nullptr || modeling_ == nullptr) {
    return;
  }
  viewport_panel_->prepare_viewports(*editor_, render_bridge, snapshot_cache,
                                     *document_, *assets_, *modeling_, pivot_,
                                     linear_array_, radial_array_);
}

std::size_t UISystem::render_viewports() {
  return render_surfaces(true);
}

std::size_t UISystem::render_surfaces(bool render_scene_viewports) {
  if (render_system_ == nullptr) {
    return 0u;
  }
  render_system_->begin_presentation_frame();
  std::size_t submitted = 0u;
  if (render_scene_viewports && !home_visible_ && viewport_panel_ != nullptr) {
    submitted += viewport_panel_->render_viewports(false);
  }
  if (material_preview_surface_ != nullptr &&
      standard_material_editor_window_ != nullptr &&
      standard_material_editor_window_->isVisible() &&
      material_preview_surface_->render_frame()) {
    ++submitted;
  }
  render_system_->complete_presentation_frame(submitted);
  return submitted;
}

bool UISystem::needs_continuous_rendering() const {
  const bool scene_continuous =
      !home_visible_ && viewport_panel_ != nullptr &&
      viewport_panel_->needs_continuous_rendering();
  const bool preview_visible = material_preview_surface_ != nullptr &&
                               standard_material_editor_window_ != nullptr &&
                               standard_material_editor_window_->isVisible() &&
                               !standard_material_editor_window_
                                    ->isMinimized() &&
                               material_preview_surface_->isExposed();
  return scene_continuous || preview_visible;
}

void *UISystem::native_window_handle() const {
  if (main_window_ == nullptr) {
    return nullptr;
  }
  const WId id = main_window_->winId();
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));
}

bool UISystem::can_render_viewports() const {
  return !home_visible_ && main_window_ != nullptr && main_window_->isVisible() &&
         !main_window_->isMinimized() && main_window_->updatesEnabled() &&
         viewport_panel_ != nullptr &&
         !viewport_panel_->startup_presentation_blocked();
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
    std::string state_bytes = workspace_window_state_;
    if (!home_visible_ || state_bytes.empty()) {
      const QByteArray state = main_window_->saveState(kQtWindowStateVersion);
      state_bytes.assign(state.constData(),
                         static_cast<std::size_t>(state.size()));
    }
    (void)write_binary_file(
        settings_paths_.qt_window_state_file,
        QByteArray(state_bytes.data(),
                   static_cast<qsizetype>(state_bytes.size())));
  }
  save_auxiliary_window_state();
}

void UISystem::capture_workspace_window_state() {
  if (main_window_ == nullptr) {
    return;
  }
  const QByteArray state = main_window_->saveState(kQtWindowStateVersion);
  workspace_window_state_.assign(
      state.constData(), static_cast<std::size_t>(state.size()));
  selection_toolbar_workspace_visible_ =
      selection_toolbar_ != nullptr && !selection_toolbar_->isHidden();
  snapping_toolbar_workspace_visible_ =
      snapping_toolbar_ != nullptr && !snapping_toolbar_->isHidden();
  viewport_display_toolbar_workspace_visible_ =
      viewport_display_toolbar_ != nullptr &&
      !viewport_display_toolbar_->isHidden();
  tool_toolbar_workspace_visible_ =
      tool_toolbar_ != nullptr && !tool_toolbar_->isHidden();
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
  if (material_browser_window_ != nullptr) {
    if (!auxiliary_window_state_.material_browser_geometry.isEmpty()) {
      (void)material_browser_window_->restoreGeometry(
          auxiliary_window_state_.material_browser_geometry);
    }
    material_browser_window_->restore_presentation_state(
        auxiliary_window_state_.material_browser_grid_mode
            ? QtMaterialBrowserViewMode::Grid
            : QtMaterialBrowserViewMode::List,
        auxiliary_window_state_.material_browser_tile_size);
  }
  if (standard_material_editor_window_ != nullptr) {
    if (!auxiliary_window_state_.standard_material_editor_geometry.isEmpty()) {
      (void)standard_material_editor_window_->restoreGeometry(
          auxiliary_window_state_.standard_material_editor_geometry);
    }
    const int preview_mesh = std::clamp(
        auxiliary_window_state_.standard_material_preview_mesh, 0, 4);
    const int preview_shading = std::clamp(
        auxiliary_window_state_.standard_material_preview_shading, 0, 2);
    standard_material_editor_window_->restore_presentation_state(
        static_cast<QtStandardMaterialPreviewMesh>(preview_mesh),
        static_cast<QtStandardMaterialPreviewShading>(preview_shading),
        auxiliary_window_state_.standard_material_preview_environment,
        auxiliary_window_state_.standard_material_preview_background);
    if (material_preview_surface_ != nullptr) {
      (void)material_preview_surface_->set_preview_mesh(
          preview_mesh_kind(
              standard_material_editor_window_->preview_mesh()));
      (void)material_preview_surface_->set_preview_shading(
          preview_shading_mode(
              standard_material_editor_window_->preview_shading()));
      (void)material_preview_surface_->set_preview_environment(
          standard_material_editor_window_->preview_environment()
              .toStdString());
      (void)material_preview_surface_->set_show_environment_background(
          standard_material_editor_window_
              ->preview_environment_background_visible());
    }
  }
}

void UISystem::prepare_startup_presentation() {
  if (main_window_ == nullptr || viewport_panel_ == nullptr) {
    return;
  }
  startup_presentation_ = {};
  startup_presentation_started_ = std::chrono::steady_clock::now();
  startup_expected_maximized_ =
      main_window_->windowState().testFlag(Qt::WindowMaximized);
  viewport_panel_->set_startup_presentation_blocked(true);
  main_window_->setUpdatesEnabled(false);
}

void UISystem::advance_startup_presentation() {
  if (main_window_ == nullptr || main_window_->updatesEnabled()) {
    return;
  }
  if (QLayout *layout = main_window_->layout()) {
    (void)layout->activate();
  }
  const QWindow *host_window = main_window_->windowHandle();
  void *const native_handle = native_window_handle();
  const PlatformWindowClientExtent native_client =
      platform_window_client_extent(native_handle);
  const qreal pixel_scale = main_window_->devicePixelRatioF();
  const int retained_client_width_pixels =
      static_cast<int>(std::lround(main_window_->width() * pixel_scale));
  const int retained_client_height_pixels =
      static_cast<int>(std::lround(main_window_->height() * pixel_scale));
  const bool native_state_ready =
      !startup_expected_maximized_ ||
      platform_window_is_maximized(native_handle);
  const bool native_client_ready = qt_startup_client_extents_match(
      retained_client_width_pixels, retained_client_height_pixels,
      native_client.width, native_client.height);
  const bool layout_ready = startup_layout_ready();
  const bool hard_invariants_ready =
      host_window != nullptr && host_window->isExposed() &&
      !main_window_->isMinimized() && native_state_ready &&
      native_client_ready && layout_ready;
  const QtStartupPresentationAction action = qt_startup_presentation_action(
      startup_presentation_, hard_invariants_ready,
      hard_invariants_ready
          ? startup_layout_geometry_signature(native_client.width,
                                              native_client.height)
          : 0u);
  if (action == QtStartupPresentationAction::PresentationReady) {
    finish_startup_presentation(false);
    return;
  }

  constexpr auto kStartupPresentationTimeout = std::chrono::seconds(2);
  if (startup_presentation_started_ !=
          std::chrono::steady_clock::time_point{} &&
      std::chrono::steady_clock::now() - startup_presentation_started_ >=
          kStartupPresentationTimeout &&
      hard_invariants_ready) {
    finish_startup_presentation(true);
  }
}

void UISystem::finish_startup_presentation(bool forced_fallback) {
  if (main_window_ == nullptr || main_window_->updatesEnabled()) {
    return;
  }
  if (forced_fallback) {
    LOG_WARN("Qt startup presentation did not stabilize within two seconds; "
             "revealing the shell with its latest layout.");
  }
  if (viewport_panel_ != nullptr) {
    viewport_panel_->set_startup_presentation_blocked(false);
  }
  main_window_->setUpdatesEnabled(true);
  main_window_->repaint();
  if (auxiliary_window_state_.settings_dialog_visible &&
      settings_dialog_ != nullptr && !settings_dialog_->isVisible()) {
    show_settings_dialog();
  }
}

bool UISystem::startup_layout_ready() const {
  if (main_window_ == nullptr || main_window_->centralWidget() == nullptr ||
      main_window_->menuBar() == nullptr ||
      main_window_->statusBar() == nullptr) {
    return false;
  }
  const auto geometry_ready = [](const QWidget *widget) {
    return widget != nullptr && widget->geometry().isValid();
  };
  if (!main_window_->contentsRect().isValid() ||
      !geometry_ready(main_window_->centralWidget()) ||
      !geometry_ready(main_window_->menuBar()) ||
      !geometry_ready(main_window_->statusBar())) {
    return false;
  }

  for (UIPanelId id : {UIPanelId::Outliner, UIPanelId::Inspector,
                       UIPanelId::Console}) {
    const QDockWidget *dock = panels_.dock(id);
    if (dock != nullptr && dock->isVisible() && !geometry_ready(dock)) {
      return false;
    }
  }
  for (const QToolBar *toolbar :
       {static_cast<const QToolBar *>(selection_toolbar_),
        static_cast<const QToolBar *>(snapping_toolbar_),
        static_cast<const QToolBar *>(viewport_display_toolbar_),
        static_cast<const QToolBar *>(tool_toolbar_)}) {
    if (toolbar != nullptr && toolbar->isVisible() &&
        !geometry_ready(toolbar)) {
      return false;
    }
  }
  return true;
}

std::uint64_t UISystem::startup_layout_geometry_signature(
    int native_client_width_pixels,
    int native_client_height_pixels) const {
  if (main_window_ == nullptr) {
    return 0u;
  }
  std::uint64_t signature = 1469598103934665603ull;
  const auto mix = [&signature](int value) {
    signature ^= static_cast<std::uint32_t>(value);
    signature *= 1099511628211ull;
  };
  const qreal pixel_scale = main_window_->devicePixelRatioF();
  const auto mix_rect = [&mix, pixel_scale](const QRect &rect) {
    mix(static_cast<int>(std::lround(rect.x() * pixel_scale)));
    mix(static_cast<int>(std::lround(rect.y() * pixel_scale)));
    mix(static_cast<int>(std::lround(rect.width() * pixel_scale)));
    mix(static_cast<int>(std::lround(rect.height() * pixel_scale)));
  };

  mix(static_cast<int>(std::lround(pixel_scale * 1000.0)));
  mix(native_client_width_pixels);
  mix(native_client_height_pixels);
  mix_rect(main_window_->rect());
  mix_rect(main_window_->contentsRect());
  if (const QWidget *central = main_window_->centralWidget()) {
    mix_rect(central->geometry());
  }
  mix_rect(main_window_->menuBar()->geometry());
  mix_rect(main_window_->statusBar()->geometry());
  for (UIPanelId id : {UIPanelId::Outliner, UIPanelId::Inspector,
                       UIPanelId::Console}) {
    mix(panels_.is_visible(id) ? 1 : 0);
    if (const QDockWidget *dock = panels_.dock(id)) {
      mix(dock->isFloating() ? 1 : 0);
      mix_rect(dock->geometry());
    }
  }
  const auto mix_toolbar = [&mix, &mix_rect](const QToolBar *toolbar) {
    mix(toolbar != nullptr && toolbar->isVisible() ? 1 : 0);
    if (toolbar != nullptr) {
      mix(toolbar->isFloating() ? 1 : 0);
      mix_rect(toolbar->geometry());
    }
  };
  mix_toolbar(selection_toolbar_);
  mix_toolbar(snapping_toolbar_);
  mix_toolbar(viewport_display_toolbar_);
  mix_toolbar(tool_toolbar_);
  return signature;
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
  if (material_browser_window_ != nullptr) {
    auxiliary_window_state_.material_browser_geometry =
        material_browser_window_->saveGeometry();
    auxiliary_window_state_.material_browser_grid_mode =
        material_browser_window_->view_mode() ==
        QtMaterialBrowserViewMode::Grid;
    auxiliary_window_state_.material_browser_tile_size =
        material_browser_window_->tile_size();
  }
  if (standard_material_editor_window_ != nullptr) {
    auxiliary_window_state_.standard_material_editor_geometry =
        standard_material_editor_window_->saveGeometry();
    auxiliary_window_state_.standard_material_preview_mesh =
        static_cast<int>(standard_material_editor_window_->preview_mesh());
    auxiliary_window_state_.standard_material_preview_shading =
        static_cast<int>(
            standard_material_editor_window_->preview_shading());
    auxiliary_window_state_.standard_material_preview_environment =
        standard_material_editor_window_->preview_environment();
    auxiliary_window_state_.standard_material_preview_background =
        standard_material_editor_window_
            ->preview_environment_background_visible();
  }
  (void)auxiliary_window_state_.save(
      settings_paths_.qt_auxiliary_window_state_file);
}

void UISystem::synchronize_auxiliary_window_visibility() {
  if (main_window_ == nullptr || !main_window_->isVisible() ||
      !main_window_->updatesEnabled()) {
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

void UISystem::present_lifecycle_result(const QuaderApiStatus &result) {
  if (status_message_ != nullptr && !result.message.empty()) {
    status_message_->setText(QString::fromStdString(result.message));
  }

  const bool restored_scene_warning =
      result.ok() && result.message.find("restored scenes were skipped") !=
                         std::string::npos;
  if (result.ok() && !restored_scene_warning) {
    return;
  }

  QMessageBox::warning(
      main_window_.get(),
      QString::fromStdString(localization_.text(
          restored_scene_warning ? "ui.project.open_warning.title"
                                 : "ui.lifecycle.error.title",
          restored_scene_warning ? "Project Opened with Warnings"
                                 : "Quader Operation Failed")),
      QString::fromStdString(
          result.message.empty()
              ? localization_.text("ui.status.command_failed",
                                   "Command failed.")
              : result.message));
}

void UISystem::present_standard_material_error(std::string_view message) {
  if (message.empty()) {
    return;
  }
  QMessageBox::warning(
      standard_material_editor_window_ != nullptr
          ? static_cast<QWidget *>(standard_material_editor_window_)
          : static_cast<QWidget *>(main_window_.get()),
      QString::fromStdString(localization_.text(
          "ui.standard_material_editor.error_title",
          "Standard Material Operation Failed")),
      QString::fromUtf8(message.data(), static_cast<qsizetype>(message.size())));
}

void UISystem::refresh_standard_material_editor() {
  if (standard_material_editor_window_ == nullptr) {
    return;
  }
  if (editor_ == nullptr || !editor_->project().has_project()) {
    standard_material_preview_image_cache_.clear();
    standard_material_editor_window_->clear_document();
    standard_material_editor_window_->hide();
    return;
  }

  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> current =
      editor_->standard_material_documents().current();
  if (!current.ok) {
    standard_material_preview_image_cache_.clear();
    if (standard_material_editor_window_->has_document()) {
      standard_material_editor_window_->clear_document();
      standard_material_editor_window_->hide();
    }
    return;
  }

  const QuaderStandardMaterialDocumentInfo &source = current.value();
  QtStandardMaterialEditorDocument document{
      .stable_id = QString::fromStdString(std::string(source.id.str())),
      .name = QString::fromStdString(source.name),
      .project_relative_path =
          source.path.has_value()
              ? QString::fromStdString(source.path->portable_string())
              : QString{},
      .albedo = QColor::fromRgbF(source.material.base_color[0],
                                 source.material.base_color[1],
                                 source.material.base_color[2]),
      .roughness = source.material.roughness,
      .metallic = source.material.metallic,
      .normal_strength = source.material.normal_strength,
      .direct_x_normal = source.material.normal_convention ==
                         QuaderNormalMapConvention::DirectX,
      .texture_transform =
          QtStandardMaterialTextureTransform{
              .center_u = source.material.texture_transform.center[0],
              .center_v = source.material.texture_transform.center[1],
              .scale_u = source.material.texture_transform.scale[0],
              .scale_v = source.material.texture_transform.scale[1],
              .offset_u = source.material.texture_transform.offset[0],
              .offset_v = source.material.texture_transform.offset[1],
              .rotation_degrees =
                  source.material.texture_transform.rotation_degrees,
          },
      .dirty = source.dirty,
  };

  for (std::size_t index = 0u; index < kMaterialTextureSlots.size(); ++index) {
    QtStandardMaterialTexturePresentation &target = document.textures[index];
    if (source.pending_texture_sources[index].has_value()) {
      const std::filesystem::path &pending =
          *source.pending_texture_sources[index];
      target.project_relative_path =
          QString::fromStdWString(pending.filename().wstring());
      target.diagnostic = QString::fromStdWString(pending.wstring());
      continue;
    }
    const std::optional<QuaderStandardMaterialTextureReference> *saved =
        source.material.texture(kMaterialTextureSlots[index]);
    if (saved != nullptr && saved->has_value()) {
      target.project_relative_path =
          QString::fromStdString((*saved)->project_relative_path);
    }
  }
  const QuaderApiResult<QuaderStandardMaterialPreviewInfo> preview =
      editor_->standard_material_documents().preview();
  std::optional<StandardMaterialPreviewSnapshot> render_snapshot;
  RenderStandardMaterialEvaluationSnapshot *render_standard = nullptr;
  if (material_preview_surface_ != nullptr) {
    render_snapshot.emplace(StandardMaterialPreviewSnapshot{
        .project_material_id = source.id,
        .kind = RenderMaterialEvaluationKind::Standard,
        .identity_revision =
            render_project_asset_identity_revision(source.id) ^
            (static_cast<std::uint64_t>(source.session_id.generation()) <<
             32u),
        .content_revision = source.draft_revision,
        .payload = RenderStandardMaterialEvaluationSnapshot{
            .base_color = {source.material.base_color[0],
                           source.material.base_color[1],
                           source.material.base_color[2]},
            .roughness = source.material.roughness,
            .metallic = source.material.metallic,
            .normal_strength = source.material.normal_strength,
            .directx_normal = source.material.normal_convention ==
                              QuaderNormalMapConvention::DirectX,
            .texture_transform =
                RenderStandardMaterialTextureTransformSnapshot{
                    .center =
                        {source.material.texture_transform.center[0],
                         source.material.texture_transform.center[1]},
                    .scale =
                        {source.material.texture_transform.scale[0],
                         source.material.texture_transform.scale[1]},
                    .offset =
                        {source.material.texture_transform.offset[0],
                         source.material.texture_transform.offset[1]},
                    .rotation_degrees =
                        source.material.texture_transform.rotation_degrees,
                },
        },
    });
    render_standard = render_snapshot->standard_material();
  }

  if (!preview.ok()) {
    standard_material_preview_image_cache_.clear();
  } else {
    const std::string material_identity(source.id.str());
    for (std::size_t index = 0u; index < preview.value().textures.size();
         ++index) {
      if (!preview.value().texture_diagnostics[index].empty()) {
        if (!document.status_message.isEmpty()) {
          document.status_message += QStringLiteral("; ");
        }
        document.status_message += QString::fromStdString(
            preview.value().texture_diagnostics[index]);
      }
      if (!preview.value().textures[index].has_value()) {
        standard_material_preview_image_cache_.clear_slot(index);
        continue;
      }
      const QuaderStandardMaterialPreviewTexture &texture =
          *preview.value().textures[index];
      QtStandardMaterialTexturePresentation &presentation =
          document.textures[index];
      presentation.content_revision = texture.content_revision;
      if (texture.width <=
              static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
          texture.height <=
              static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        presentation.pixel_size =
            QSize(static_cast<int>(texture.width),
                  static_cast<int>(texture.height));
      }
      const std::uintmax_t maximum_qint64 =
          static_cast<std::uintmax_t>(std::numeric_limits<qint64>::max());
      presentation.encoded_byte_size = static_cast<qint64>(
          std::min(texture.encoded_byte_size, maximum_qint64));
      presentation.format =
          QString::fromStdString(texture.detected_format).toUpper();
      if (!presentation.format.isEmpty()) {
        presentation.format += QStringLiteral(" / ");
      }
      presentation.format += QStringLiteral("RGBA8");
      presentation.color_space =
          texture.color_space == QuaderProjectTextureColorSpace::Srgb
              ? QStringLiteral("sRGB")
              : QStringLiteral("Linear");
      presentation.preview_image = standard_material_preview_image_cache_.image(
          index, material_identity, source.session_id.generation(),
          texture.content_revision, texture.rgba8, texture.width,
          texture.height);

      if (render_standard != nullptr) {
        render_standard->textures[index] = MaterialPreviewTextureSnapshot{
            .project_texture_id = texture.id.value_or(ProjectAssetId{}),
            .identity_revision =
                texture.id.has_value()
                    ? render_project_asset_identity_revision(*texture.id)
                    : render_snapshot->identity_revision ^
                          (0x9e3779b97f4a7c15ull + index),
            .content_revision = texture.content_revision,
            .color_space = texture.color_space ==
                                   QuaderProjectTextureColorSpace::Srgb
                               ? TextureColorSpace::Srgb
                               : TextureColorSpace::Linear,
            .decoded_rgba8 = texture.rgba8,
            .width = texture.width,
            .height = texture.height,
        };
      }
    }
  }
  if (material_preview_surface_ != nullptr && render_snapshot.has_value()) {
    material_preview_surface_->set_material(std::move(*render_snapshot));
  }
  standard_material_editor_window_->set_document(std::move(document));
}

void UISystem::open_standard_material(std::string_view stable_id) {
  if (editor_ == nullptr || standard_material_editor_window_ == nullptr ||
      !editor_->project().has_project()) {
    return;
  }
  const std::optional<ProjectAssetId> id = ProjectAssetId::parse(stable_id);
  if (!id.has_value()) {
    present_standard_material_error("Material identity is invalid.");
    return;
  }

  const QuaderApiResult<std::vector<QuaderProjectMaterialInfo>> materials =
      editor_->project_assets().materials();
  if (!materials.ok) {
    present_standard_material_error(materials.message);
    return;
  }
  const auto found = std::find_if(
      materials.value().begin(), materials.value().end(),
      [&id](const QuaderProjectMaterialInfo &material) {
        return material.id == *id;
      });
  if (found == materials.value().end()) {
    present_standard_material_error("Project material is unavailable.");
    return;
  }
  const std::map<QuaderProjectMaterialKind, std::function<void()>>
      editor_registry{
      {QuaderProjectMaterialKind::Standard,
       [this, material_id = *id]() {
         open_standard_material_document(material_id);
       }},
  };
  const auto editor = editor_registry.find(found->kind);
  if (editor == editor_registry.end()) {
    present_standard_material_error(
        localization_.text("ui.standard_material_editor.graph_unavailable",
                           "Graph Material editing is not available yet."));
    return;
  }
  editor->second();
}

void UISystem::open_standard_material_document(ProjectAssetId id) {
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> current =
      editor_->standard_material_documents().current();
  if (current.ok && current.value().id == id) {
    refresh_standard_material_editor();
    standard_material_editor_window_->show();
    standard_material_editor_window_->raise();
    standard_material_editor_window_->activateWindow();
    return;
  }
  if (!resolve_standard_material_document(false)) {
    return;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> opened =
      editor_->standard_material_documents().open(id);
  present_lifecycle_result(opened);
  if (opened.ok && material_preview_surface_ != nullptr) {
    (void)material_preview_surface_->reset_preview();
  }
  refresh_standard_material_editor();
  if (!opened.ok) {
    return;
  }
  standard_material_editor_window_->show();
  standard_material_editor_window_->raise();
  standard_material_editor_window_->activateWindow();
}

void UISystem::open_standard_material_from_dialog() {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return;
  }
  PlatformFileDialogOptions options;
  options.owner_window_handle = native_window_handle();
  options.initial_directory = editor_->project().root() / "Materials";
  const std::optional<std::filesystem::path> selected =
      platform_open_material_file_dialog(options);
  if (!selected.has_value()) {
    return;
  }
  const std::optional<ProjectRelativePath> relative =
      project_relative_dialog_path(*selected, editor_->project().root());
  if (!relative.has_value()) {
    present_standard_material_error(localization_.text(
        "ui.standard_material_editor.path_outside_project",
        "Choose a material inside the current project."));
    return;
  }
  const QuaderApiResult<std::vector<QuaderProjectMaterialInfo>> materials =
      editor_->project_assets().materials();
  if (!materials.ok) {
    present_standard_material_error(materials.message);
    return;
  }
  const std::string portable_path = relative->portable_string();
  const auto found = std::find_if(
      materials.value().begin(), materials.value().end(),
      [&portable_path](const QuaderProjectMaterialInfo &material) {
        return material.project_relative_path == portable_path;
      });
  if (found == materials.value().end()) {
    present_standard_material_error(localization_.text(
        "ui.standard_material_editor.not_in_catalog",
        "The selected QMAT is not present in the project material catalog."));
    return;
  }
  open_standard_material(found->id.str());
}

void UISystem::new_standard_material() {
  if (editor_ == nullptr || standard_material_editor_window_ == nullptr ||
      !editor_->project().has_project() ||
      !resolve_standard_material_document(false)) {
    return;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> created =
      editor_->standard_material_documents().new_material();
  present_lifecycle_result(created);
  if (created.ok && material_preview_surface_ != nullptr) {
    (void)material_preview_surface_->reset_preview();
  }
  refresh_standard_material_editor();
  if (created.ok) {
    standard_material_editor_window_->show();
    standard_material_editor_window_->raise();
    standard_material_editor_window_->activateWindow();
  }
}

bool UISystem::save_standard_material(bool save_as) {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return false;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> current =
      editor_->standard_material_documents().current();
  if (!current.ok) {
    present_standard_material_error(current.message);
    return false;
  }

  QuaderApiVoidResult result;
  if (!save_as && current.value().path.has_value()) {
    result = editor_->standard_material_documents().save();
  } else {
    PlatformFileDialogOptions options;
    options.owner_window_handle = native_window_handle();
    options.initial_directory = editor_->project().root() / "Materials";
    if (current.value().path.has_value()) {
      const std::filesystem::path filename =
          current.value().path->native_path().filename();
      options.initial_file_name =
          filename.stem().string() + "_copy" + filename.extension().string();
      options.initial_directory =
          editor_->project().root() /
          current.value().path->native_path().parent_path();
    } else {
      options.initial_file_name = "untitled_material.qmat";
    }
    const std::optional<std::filesystem::path> selected =
        platform_save_material_file_dialog(options);
    if (!selected.has_value()) {
      return false;
    }
    const std::optional<ProjectRelativePath> relative =
        project_relative_dialog_path(*selected, editor_->project().root());
    if (!relative.has_value()) {
      present_standard_material_error(localization_.text(
          "ui.standard_material_editor.path_outside_project",
          "Choose a material inside the current project."));
      return false;
    }
    result = editor_->standard_material_documents().save_as(*relative);
  }
  present_lifecycle_result(result);
  refresh_project_workspace();
  return result.ok;
}

bool UISystem::resolve_standard_material_document(bool close_after) {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return true;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> current =
      editor_->standard_material_documents().current();
  if (!current.ok) {
    return true;
  }

  bool discard = false;
  if (current.value().dirty) {
    std::string message = localization_.text(
        "ui.standard_material_editor.unsaved_message",
        "{name} has unsaved changes. Save it?");
    constexpr std::string_view kNameToken = "{name}";
    if (const std::size_t token = message.find(kNameToken);
        token != std::string::npos) {
      message.replace(token, kNameToken.size(), current.value().name);
    }
    QMessageBox confirmation(
        QMessageBox::Warning,
        QString::fromStdString(localization_.text(
            "ui.standard_material_editor.unsaved_title",
            "Unsaved Standard Material")),
        QString::fromStdString(message), QMessageBox::NoButton,
        standard_material_editor_window_ != nullptr
            ? static_cast<QWidget *>(standard_material_editor_window_)
            : static_cast<QWidget *>(main_window_.get()));
    confirmation.setObjectName(
        QStringLiteral("standard_material_unsaved_confirmation"));
    confirmation.setWindowModality(Qt::WindowModal);
    QPushButton *save = confirmation.addButton(
        QString::fromStdString(localization_.text(
            "ui.standard_material_editor.unsaved_save", "Save")),
        QMessageBox::AcceptRole);
    QPushButton *discard_button = confirmation.addButton(
        QString::fromStdString(localization_.text(
            "ui.standard_material_editor.unsaved_discard", "Discard")),
        QMessageBox::DestructiveRole);
    QPushButton *cancel = confirmation.addButton(
        QString::fromStdString(localization_.text(
            "ui.standard_material_editor.unsaved_cancel", "Cancel")),
        QMessageBox::RejectRole);
    confirmation.setEscapeButton(cancel);
    confirmation.exec();
    if (confirmation.clickedButton() == save) {
      if (!save_standard_material(false)) {
        return false;
      }
    } else if (confirmation.clickedButton() == discard_button) {
      discard = true;
    } else {
      return false;
    }
  }

  if (discard || close_after) {
    const QuaderApiVoidResult closed =
        editor_->standard_material_documents().close(discard);
    present_lifecycle_result(closed);
    refresh_standard_material_editor();
    return closed.ok;
  }
  return true;
}

void UISystem::handle_standard_material_file_action(
    QtStandardMaterialEditorFileAction action) {
  switch (action) {
  case QtStandardMaterialEditorFileAction::New:
    new_standard_material();
    return;
  case QtStandardMaterialEditorFileAction::Open:
    open_standard_material_from_dialog();
    return;
  case QtStandardMaterialEditorFileAction::Save:
    (void)save_standard_material(false);
    return;
  case QtStandardMaterialEditorFileAction::SaveAs:
    (void)save_standard_material(true);
    return;
  }
}

void UISystem::handle_standard_material_scalar(
    QtStandardMaterialScalar scalar, double value) {
  if (editor_ == nullptr) {
    return;
  }
  QuaderApiVoidResult result;
  switch (scalar) {
  case QtStandardMaterialScalar::Roughness:
    result = editor_->standard_material_documents().set_roughness(
        static_cast<float>(value));
    break;
  case QtStandardMaterialScalar::Metallic:
    result = editor_->standard_material_documents().set_metallic(
        static_cast<float>(value));
    break;
  case QtStandardMaterialScalar::NormalStrength:
    result = editor_->standard_material_documents().set_normal_strength(
        static_cast<float>(value));
    break;
  }
  present_lifecycle_result(result);
  refresh_standard_material_editor();
}

void UISystem::handle_standard_material_texture(
    QtStandardMaterialTextureSlot slot,
    QtStandardMaterialTextureAction action) {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return;
  }
  QuaderApiVoidResult result;
  if (action == QtStandardMaterialTextureAction::Clear) {
    result = editor_->standard_material_documents().clear_texture(
        domain_texture_slot(slot));
  } else {
    PlatformFileDialogOptions options;
    options.owner_window_handle = native_window_handle();
    options.initial_directory = editor_->project().root() / "Textures";
    const std::optional<std::filesystem::path> selected =
        platform_open_texture_file_dialog(options);
    if (!selected.has_value()) {
      return;
    }
    result = editor_->standard_material_documents().import_texture(
        domain_texture_slot(slot), *selected);
  }
  present_lifecycle_result(result);
  refresh_standard_material_editor();
}

void UISystem::handle_standard_material_texture_transform(
    QtStandardMaterialTextureTransformComponent component, double value) {
  if (editor_ == nullptr || !std::isfinite(value)) {
    return;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> current =
      editor_->standard_material_documents().current();
  if (!current.ok) {
    present_lifecycle_result(current);
    return;
  }
  QuaderStandardMaterialTextureTransform transform =
      current.value().material.texture_transform;
  switch (component) {
  case QtStandardMaterialTextureTransformComponent::CenterU:
    transform.center[0] = static_cast<float>(value);
    break;
  case QtStandardMaterialTextureTransformComponent::CenterV:
    transform.center[1] = static_cast<float>(value);
    break;
  case QtStandardMaterialTextureTransformComponent::ScaleU:
    transform.scale[0] = static_cast<float>(value);
    break;
  case QtStandardMaterialTextureTransformComponent::ScaleV:
    transform.scale[1] = static_cast<float>(value);
    break;
  case QtStandardMaterialTextureTransformComponent::OffsetU:
    transform.offset[0] = static_cast<float>(value);
    break;
  case QtStandardMaterialTextureTransformComponent::OffsetV:
    transform.offset[1] = static_cast<float>(value);
    break;
  case QtStandardMaterialTextureTransformComponent::RotationDegrees:
    transform.rotation_degrees = static_cast<float>(value);
    break;
  }
  const QuaderApiVoidResult result =
      editor_->standard_material_documents().set_texture_transform(transform);
  present_lifecycle_result(result);
  refresh_standard_material_editor();
}

void UISystem::refresh_localized_chrome() {
  toolbar_refresh_valid_ = false;
  const auto text = [this](std::string_view key, std::string_view fallback) {
    return QString::fromStdString(localization_.text(key, fallback));
  };
  const auto update_title = [this](UIPanelId id) {
    if (QDockWidget *dock = panels_.dock(id)) {
      dock->setWindowTitle(QString::fromStdString(localization_.text(
          ui_panel_localization_key(id), ui_panel_key(id))));
    }
  };
  update_title(UIPanelId::Outliner);
  update_title(UIPanelId::Inspector);
  update_title(UIPanelId::Console);
  if (selection_toolbar_ != nullptr) {
    selection_toolbar_->set_localization(localization_);
  }
  if (snapping_toolbar_ != nullptr) {
    snapping_toolbar_->set_localization(localization_);
  }
  if (viewport_display_toolbar_ != nullptr) {
    viewport_display_toolbar_->set_localization(localization_);
  }
  if (tool_toolbar_ != nullptr) {
    tool_toolbar_->set_localization(localization_);
  }
  if (project_home_page_ != nullptr) {
    project_home_page_->set_labels(QtProjectHomeLabels{
        .new_project =
            text("ui.home.new_project", "New Quader Project"),
        .open_project = text("ui.home.open_project", "Open Project"),
        .recent_projects =
            text("ui.home.recent_projects", "Recent Projects"),
        .recent_name_column = text("ui.home.recent_name", "Name"),
        .recent_location_column =
            text("ui.home.recent_location", "Location"),
        .no_recent_projects =
            text("ui.home.no_recent_projects", "No recent projects"),
        .unavailable_project =
            text("ui.home.unavailable_project", "Project is unavailable"),
        .remove_recent_project = text("ui.home.remove_recent_project",
                                      "Remove from Recent"),
    });
  }
  if (scene_workspace_ != nullptr) {
    scene_workspace_->set_labels(QtSceneWorkspaceLabels{
        .untitled_scene =
            text("ui.workspace.untitled_scene", "Untitled Scene"),
        .unsaved_scene =
            text("ui.workspace.unsaved_scene", "Unsaved scene"),
        .new_scene = text("ui.workspace.new_scene", "New Scene"),
        .open_scene = text("ui.workspace.open_scene", "Open Scene"),
        .empty_title = text("ui.workspace.empty_title", "No scenes open"),
        .empty_description = text(
            "ui.workspace.empty_description",
            "Create a new scene or open one from this project."),
        .return_to_scene = text("ui.workspace.return_to_scene",
                                "Return to scene %1"),
        .editing_instance = text("ui.workspace.editing_instance",
                                 "Editing instance %1"),
    });
  }
  if (material_browser_window_ != nullptr) {
    material_browser_window_->set_labels(QtMaterialBrowserLabels{
        .window_title =
            text("ui.material_browser.title", "Material Browser"),
        .search_placeholder = text("ui.material_browser.search",
                                   "Search materials"),
        .grid = text("ui.material_browser.grid", "Grid"),
        .list = text("ui.material_browser.list", "List"),
        .tile_size =
            text("ui.material_browser.tile_size", "Preview size"),
        .name_column = text("ui.material_browser.name", "Name"),
        .type_column = text("ui.material_browser.type", "Type"),
        .size_column = text("ui.material_browser.size", "Size"),
        .material_type = text(
            "ui.material_browser.standard_material_type",
            "Standard Material"),
        .empty_catalog = text("ui.material_browser.empty",
                              "No materials in this project"),
        .no_results = text("ui.material_browser.no_results",
                           "No materials match this search"),
    });
    material_browser_catalog_signature_ = 0u;
  }
  if (standard_material_editor_window_ != nullptr) {
    standard_material_editor_window_->set_labels(
        QtStandardMaterialEditorLabels{
            .window_title = text("ui.standard_material_editor.title",
                                 "Standard Material Editor"),
            .file_menu = text("ui.standard_material_editor.file", "File"),
            .new_material =
                text("ui.standard_material_editor.file.new", "New"),
            .open_material = text("ui.standard_material_editor.file.open",
                                  "Open..."),
            .save_material =
                text("ui.standard_material_editor.file.save", "Save"),
            .save_material_as = text(
                "ui.standard_material_editor.file.save_as", "Save As..."),
            .close_window =
                text("ui.standard_material_editor.file.close", "Close"),
            .view_menu = text("ui.standard_material_editor.view", "View"),
            .preview_mesh_menu = text(
                "ui.standard_material_editor.view.preview_mesh",
                "Preview Mesh"),
            .reset_preview = text(
                "ui.standard_material_editor.view.reset_preview",
                "Reset Preview"),
            .environment_menu = text(
                "ui.standard_material_editor.environment", "Environment"),
            .show_environment_background = text(
                "ui.standard_material_editor.environment.show_background",
                "Show Background"),
            .sphere = text("ui.standard_material_editor.mesh.sphere",
                           "Sphere"),
            .box = text("ui.standard_material_editor.mesh.box", "Box"),
            .rounded_box = text(
                "ui.standard_material_editor.mesh.rounded_box",
                "Rounded Box"),
            .cylinder = text("ui.standard_material_editor.mesh.cylinder",
                             "Cylinder"),
            .plane =
                text("ui.standard_material_editor.mesh.plane", "Plane"),
            .wireframe = text(
                "ui.standard_material_editor.shading.wireframe",
                "Wireframe"),
            .shaded = text("ui.standard_material_editor.shading.shaded",
                           "Shaded"),
            .rendered = text(
                "ui.standard_material_editor.shading.rendered",
                "Rendered"),
            .preview_unavailable = text(
                "ui.standard_material_editor.preview_unavailable",
                "Material preview is unavailable"),
            .preview_navigation = text(
                "ui.standard_material_editor.preview_navigation",
                "MMB Tumble - Shift+MMB Pan - Ctrl+MMB Environment - Wheel Zoom"),
            .properties_group = text(
                "ui.standard_material_editor.properties", "Material"),
            .name = text("ui.standard_material_editor.name", "Name"),
            .type = text("ui.standard_material_editor.type", "Type"),
            .standard_material = text(
                "ui.standard_material_editor.standard_material",
                "Standard Material"),
            .albedo = text("ui.standard_material_editor.albedo", "Albedo"),
            .albedo_texture = text(
                "ui.standard_material_editor.albedo_texture",
                "Albedo Texture"),
            .roughness = text("ui.standard_material_editor.roughness",
                              "Roughness"),
            .roughness_texture = text(
                "ui.standard_material_editor.roughness_texture",
                "Roughness Texture"),
            .metallic =
                text("ui.standard_material_editor.metallic", "Metallic"),
            .metallic_texture = text(
                "ui.standard_material_editor.metallic_texture",
                "Metallic Texture"),
            .normal_strength = text(
                "ui.standard_material_editor.normal_strength",
                "Normal Strength"),
            .normal_texture = text(
                "ui.standard_material_editor.normal_texture",
                "Normal Texture"),
            .direct_x_normal = text(
                "ui.standard_material_editor.directx_normal",
                "DirectX"),
            .choose_albedo_title = text(
                "ui.standard_material_editor.choose_albedo_title",
                "Choose Albedo Color"),
            .browse_texture = text(
                "ui.standard_material_editor.texture.browse", "Browse..."),
            .clear_texture = text(
                "ui.standard_material_editor.texture.clear", "Clear"),
            .no_texture = text(
                "ui.standard_material_editor.texture.none", "No texture"),
            .untitled_material = text(
                "ui.standard_material_editor.untitled",
                "Untitled Material"),
            .unsaved =
                text("ui.standard_material_editor.unsaved", "Unsaved"),
            .inspect_texture = text(
                "ui.standard_material_editor.texture.inspect",
                "Inspect texture"),
            .texture_transform = text(
                "ui.standard_material_editor.texture_transform",
                "Texture Transform"),
            .texture_center = text(
                "ui.standard_material_editor.texture_transform.center",
                "Center"),
            .texture_scale = text(
                "ui.standard_material_editor.texture_transform.scale",
                "Scale"),
            .texture_offset = text(
                "ui.standard_material_editor.texture_transform.offset",
                "Offset"),
            .texture_rotation = text(
                "ui.standard_material_editor.texture_transform.rotation",
                "Rotation"),
            .texture_inspector = text(
                "ui.standard_material_editor.texture_inspector",
                "Texture Inspector"),
        });
    std::vector<QtStandardMaterialPreviewEnvironment> environments;
    environments.reserve(scene_environment_catalog().size());
    for (const SceneEnvironmentDescriptor &environment :
         scene_environment_catalog()) {
      environments.push_back(QtStandardMaterialPreviewEnvironment{
          .stable_id = QString::fromUtf8(environment.id.data(),
                                         static_cast<qsizetype>(
                                             environment.id.size())),
          .display_name =
              text(environment.label_key, environment.fallback_label),
      });
    }
    standard_material_editor_window_->set_preview_environments(
        std::move(environments));
  }
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
  toolbar_refresh_valid_ = false;
  if (snapping_toolbar_ != nullptr && editor_ != nullptr) {
    snapping_toolbar_->synchronize(*editor_, true);
  }
  if (viewport_display_toolbar_ != nullptr && editor_ != nullptr) {
    viewport_display_toolbar_->synchronize(*editor_, true);
  }
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

void UISystem::show_material_browser() {
  if (material_browser_window_ == nullptr || home_visible_ ||
      editor_ == nullptr || !editor_->project().has_project()) {
    return;
  }
  material_browser_window_->show();
  material_browser_window_->raise();
  material_browser_window_->activateWindow();
}

bool UISystem::panel_visible(UIPanelId id) const {
  return id == UIPanelId::Viewport ? true : panels_.is_visible(id);
}

void UISystem::set_panel_visible(UIPanelId id, bool visible) {
  (void)panels_.set_visible(id, visible);
}

} // namespace quader
