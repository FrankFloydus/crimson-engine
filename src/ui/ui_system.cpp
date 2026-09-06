////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_system.hpp"

#include "api/quader_editor_api.hpp"
#include "assets/asset_registry.hpp"
#include "core/logger.hpp"
#include "core/scene_environment_catalog.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "platform/platform_file_dialog.hpp"
#include "platform/platform_shell.hpp"
#include "platform/window_placement.hpp"
#include "quader_app_version.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_material_surface.hpp"
#include "ui/standard_material_preview_render_snapshot_adapter.hpp"
#include "ui/console_panel.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/layer_panel.hpp"
#include "ui/outliner_panel.hpp"
#include "ui/qt_active_material_panel.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_command_palette.hpp"
#include "ui/qt_dock_title_presentation.hpp"
#include "ui/qt_main_window.hpp"
#include "ui/qt_operation_input_hints.hpp"
#include "ui/qt_material_browser_window.hpp"
#include "ui/qt_material_find_replace_dialog.hpp"
#include "ui/qt_material_preview_surface.hpp"
#include "ui/qt_project_home_page.hpp"
#include "ui/qt_retained_message_box.hpp"
#include "ui/qt_scene_workspace.hpp"
#include "ui/qt_selection_toolbar.hpp"
#include "ui/qt_settings_dialog.hpp"
#include "ui/qt_snapping_toolbar.hpp"
#include "ui/qt_texture_toolbar.hpp"
#include "ui/qt_standard_material_editor_window.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_unsaved_confirmation_dialog.hpp"
#include "ui/qt_viewport_display_toolbar.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/ui_context.hpp"

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QColor>
#include <QDialog>
#include <QDockWidget>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLocale>
#include <QMainWindow>
#include <QMenuBar>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QStatusBar>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
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
constexpr int kDockContentMinimumWidth = 220;

void apply_dock_content_minimum_width(
    QWidget &content, const QtUiScaleMetrics &metrics) {
  const int minimum_width = metrics.px(kDockContentMinimumWidth);
  if (content.minimumWidth() != minimum_width) {
    content.setMinimumWidth(minimum_width);
  }
}

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

QuaderStandardMaterialScalar domain_material_scalar(
    QtStandardMaterialScalar scalar) {
  switch (scalar) {
  case QtStandardMaterialScalar::Roughness:
    return QuaderStandardMaterialScalar::Roughness;
  case QtStandardMaterialScalar::Metallic:
    return QuaderStandardMaterialScalar::Metallic;
  case QtStandardMaterialScalar::NormalStrength:
    return QuaderStandardMaterialScalar::NormalStrength;
  }
  return QuaderStandardMaterialScalar::Roughness;
}

QuaderStandardMaterialTextureTransformComponent
domain_texture_transform_component(
    QtStandardMaterialTextureTransformComponent component) {
  using Source = QtStandardMaterialTextureTransformComponent;
  using Target = QuaderStandardMaterialTextureTransformComponent;
  switch (component) {
  case Source::CenterU:
    return Target::CenterU;
  case Source::CenterV:
    return Target::CenterV;
  case Source::ScaleU:
    return Target::ScaleU;
  case Source::ScaleV:
    return Target::ScaleV;
  case Source::OffsetU:
    return Target::OffsetU;
  case Source::OffsetV:
    return Target::OffsetV;
  case Source::RotationDegrees:
    return Target::RotationDegrees;
  }
  return Target::CenterU;
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
  if (name == ui_panel_stable_id(UIPanelId::Viewport) ||
      name == ui_panel_key(UIPanelId::Viewport)) {
    return UIPanelId::Viewport;
  }
  for (UIPanelId id : kUiDockPanelIds) {
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

void UISystem::abandon_scene_backed_panels() noexcept {
  if (outliner_panel_ != nullptr) {
    outliner_panel_->abandon_runtime();
  }
  if (inspector_panel_ != nullptr) {
    inspector_panel_->abandon_runtime();
  }
  if (layer_panel_ != nullptr) {
    layer_panel_->abandon_runtime();
  }
}

void UISystem::bind_scene_backed_panels() {
  if (editor_ == nullptr || document_ == nullptr || assets_ == nullptr ||
      modeling_ == nullptr || selection_ == nullptr) {
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
  if (outliner_panel_ != nullptr) {
    outliner_panel_->refresh(context);
  }
  if (layer_panel_ != nullptr) {
    layer_panel_->refresh(context);
  }
  if (inspector_panel_ != nullptr) {
    refresh_inspector_material_presentation();
    inspector_panel_->refresh(context);
  }
}

void UISystem::clear_editor_runtime_hooks() {
  if (editor_ == nullptr) {
    return;
  }
  editor_->viewports().set_runtime_hooks(QuaderViewportRuntimeHooks{});
#ifndef NDEBUG
  editor_->dev_tools().set_runtime_hooks(QuaderDevToolsRuntimeHooks{});
#endif
  editor_->ui().set_runtime_hooks(QuaderUIRuntimeHooks{});
  editor_->preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{});
}

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
  render_system_->set_vsync_enabled(settings_.vsync_enabled());
  auto *application =
      qobject_cast<QApplication *>(QCoreApplication::instance());
  if (application == nullptr) {
    LOG_ERROR("Qt application is unavailable while initializing UI scale");
    return false;
  }
  ui_scale_controller_ = std::make_unique<QtUiScaleApplicationController>(
      *application, settings_.interface_scale());
  localization_.load(executable_dir / "locales", settings_.locale());
  if (settings_.locale() != localization_.current_locale()) {
    settings_.set_locale(localization_.current_locale());
  }
  localized_locale_ = localization_.current_locale();
  localized_revision_ = localization_.revision();
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

  unsaved_confirmation_ =
      new QtUnsavedConfirmationDialog(main_window_.get());
  lifecycle_message_box_ = new QtRetainedMessageBox(main_window_.get());

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

  texture_toolbar_ = new QtTextureToolbar(main_window_.get());
  texture_toolbar_->set_localization(localization_);
  texture_toolbar_->setEnabled(false);
  main_window_->addToolBar(Qt::TopToolBarArea, texture_toolbar_);

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
  viewport_panel_->set_open_material_editor_callback(
      [this](ProjectAssetId id) { open_standard_material(id.str()); });
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
  project_home_page_->set_recent_project_action_requested_callback(
      [this](QtRecentProjectAction action, QString manifest_path) {
        const std::filesystem::path path(manifest_path.toStdWString());
        switch (action) {
        case QtRecentProjectAction::Open:
          if (editor_ != nullptr) {
            const QuaderApiVoidResult result = editor_->project().open(path);
            present_lifecycle_result(result);
            refresh_project_workspace();
          }
          break;
        case QtRecentProjectAction::OpenFileLocation:
          (void)platform_reveal_file(path);
          break;
        case QtRecentProjectAction::CopyPath:
          if (QClipboard *clipboard = QApplication::clipboard()) {
            clipboard->setText(manifest_path);
          }
          break;
        case QtRecentProjectAction::RemoveFromRecent:
          if (editor_ != nullptr) {
            const QuaderApiVoidResult result =
                editor_->project().remove_recent_project(path);
            present_lifecycle_result(result);
            refresh_project_workspace();
          }
          break;
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
  const QtUiScaleMetrics &initial_ui_metrics =
      ui_scale_controller_->metrics();

  settings_dialog_ = new QtSettingsDialog(main_window_.get());
  settings_dialog_->resize(initial_ui_metrics.size(QSize(720, 560)));
  QObject::connect(settings_dialog_, &QDialog::finished, settings_dialog_,
                   [this](int) {
                     if (auxiliary_window_state_.settings_dialog_visible) {
                       auxiliary_window_state_.settings_dialog_visible = false;
                       save_auxiliary_window_state();
                     }
                   });
  auxiliary_window_save_timer_ = new QTimer(main_window_.get());
  auxiliary_window_save_timer_->setSingleShot(true);
  auxiliary_window_save_timer_->setInterval(200);
  QObject::connect(auxiliary_window_save_timer_, &QTimer::timeout,
                   main_window_.get(),
                   [this]() { save_auxiliary_window_state(); });
  material_browser_window_ =
      new QtMaterialBrowserWindow(main_window_.get());
  material_browser_window_->resize(
      initial_ui_metrics.size(QSize(760, 520)));
  material_browser_window_->set_purpose(
      QtMaterialBrowserPurpose::ActiveMaterial);
  material_browser_window_->set_presentation_state_changed_callback(
      [this]() { schedule_auxiliary_window_state_save(); });
  material_browser_window_->set_activate_material_callback(
      [this](QuaderMaterialReference reference) {
        if (editor_ == nullptr || !reference.project_owned()) {
          return;
        }
        const QuaderApiVoidResult result =
            editor_->project_assets().set_active_material(
                reference.project_asset);
        present_lifecycle_result(result);
        refresh_project_workspace();
      });
  material_browser_window_->set_open_material_callback(
      [this](QuaderMaterialReference reference) {
        if (reference.project_owned()) {
          open_standard_material(reference.project_asset.str());
        }
      });
  material_browser_window_->set_import_textures_callback(
      [this]() { import_material_textures(); });
  material_browser_import_message_box_ =
      new QtRetainedMessageBox(material_browser_window_);
  material_browser_window_->hide();
  material_find_replace_dialog_ =
      new QtMaterialFindReplaceDialog(main_window_.get());
  material_find_replace_dialog_->resize(
      initial_ui_metrics.size(QSize(380, 300)));
  material_find_replace_dialog_->set_browse_material_callback(
      [this](QtMaterialFindReplaceColumn column) {
        show_material_picker(column ==
                             QtMaterialFindReplaceColumn::Replace);
      });
  material_find_replace_dialog_->set_replace_materials_callback(
      [this](QuaderMaterialReference source,
             QuaderMaterialReference replacement) {
        validate_material_find_replace_context();
        if (editor_ == nullptr || material_find_replace_dialog_ == nullptr ||
            !material_find_replace_dialog_->isVisible()) {
          return false;
        }
        refresh_material_presentation_cache();
        const std::optional<QtMaterialPresentationItem> current_source =
            material_presentation_for(source, true);
        const std::optional<QtMaterialPresentationItem> current_replacement =
            material_presentation_for(replacement, true);
        material_find_replace_dialog_->set_find_material(current_source);
        material_find_replace_dialog_->set_replacement_material(
            current_replacement);
        if (!current_source.has_value() ||
            !current_replacement.has_value() ||
            !current_replacement->replacement_valid() ||
            source == replacement) {
          return false;
        }
        const QuaderApiVoidResult result =
            editor_->materials().replace_material_references(
                source, replacement, material_find_replace_scope_);
        present_lifecycle_result(result);
        if (!result.succeeded()) {
          return false;
        }
        invalidate_material_presentation_cache();
        inspector_presenter_.invalidate();
        project_workspace_refresh_signal_->store(
            true, std::memory_order_relaxed);
        return true;
      });
  QObject::connect(material_find_replace_dialog_, &QDialog::finished,
                   material_find_replace_dialog_, [this](int) {
                     if (material_picker_window_ != nullptr) {
                       material_picker_window_->hide();
                       material_picker_window_->clear_materials();
                     }
                     if (material_find_replace_dialog_ != nullptr) {
                       material_find_replace_dialog_->clear_materials();
                     }
                     material_find_replace_scene_id_ = 0;
                     material_find_replace_scene_selection_revision_ = 0;
                     material_find_replace_modeling_selection_revision_ = 0;
                     material_find_replace_modeling_object_map_revision_ = 0;
                     material_find_replace_presentation_revision_ = 0;
                     material_picker_presentation_revision_ = 0;
                   });
  material_find_replace_dialog_->hide();
  material_picker_window_ =
      new QtMaterialBrowserWindow(material_find_replace_dialog_);
  material_picker_window_->resize(
      initial_ui_metrics.size(QSize(760, 520)));
  material_picker_window_->set_purpose(QtMaterialBrowserPurpose::Picker);
  material_picker_window_->setWindowModality(Qt::WindowModal);
  material_picker_window_->set_accept_material_callback(
      [this](QuaderMaterialReference reference) {
        validate_material_find_replace_context();
        if (material_find_replace_dialog_ == nullptr ||
            !material_find_replace_dialog_->isVisible()) {
          return;
        }
        refresh_material_presentation_cache();
        const std::optional<QtMaterialPresentationItem> presentation =
            material_presentation_for(reference, true);
        if (!presentation.has_value() ||
            (material_picker_replacement_column_ &&
             !presentation->replacement_valid())) {
          return;
        }
        material_find_replace_dialog_->set_material(
            material_picker_replacement_column_
                ? QtMaterialFindReplaceColumn::Replace
                : QtMaterialFindReplaceColumn::Find,
            presentation);
      });
  material_picker_window_->hide();
  standard_material_editor_window_ =
      new QtStandardMaterialEditorWindow(main_window_.get());
  standard_material_editor_window_->resize(
      initial_ui_metrics.size(QSize(1040, 680)));
  standard_material_unsaved_confirmation_ =
      new QtUnsavedConfirmationDialog(standard_material_editor_window_);
  standard_material_error_message_box_ =
      new QtRetainedMessageBox(standard_material_editor_window_);
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
            schedule_auxiliary_window_state_save();
          });
  standard_material_editor_window_->set_preview_reset_callback([this]() {
    if (material_preview_surface_ != nullptr) {
      (void)material_preview_surface_->reset_preview();
    }
  });
  standard_material_editor_window_->set_close_request_handler(
      [this]() { return resolve_standard_material_document(true); });
  standard_material_editor_window_->hide();
  status_message_ = new QLabel(main_window_.get());
  status_message_->setObjectName(QStringLiteral("statusMessage"));
  status_message_->setWordWrap(false);
  status_message_->hide();
  operation_input_hints_ = new QtOperationInputHints(main_window_->statusBar());
  status_version_ = new QLabel(
      QStringLiteral("v%1").arg(QString::fromUtf8(kQuaderAppVersion)),
      main_window_.get());
  status_version_->setObjectName(QStringLiteral("statusVersion"));
  main_window_->statusBar()->setObjectName(QStringLiteral("statusBar"));
  main_window_->statusBar()->addPermanentWidget(operation_input_hints_, 1);
  main_window_->statusBar()->addPermanentWidget(status_message_, 1);
  main_window_->statusBar()->addPermanentWidget(status_version_);

  apply_ui_scale_to_retained_surfaces(ui_scale_controller_->metrics());
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
    NativeViewportToolCoordinator &native_tools,
    ViewportPointPlacementService &point_placement, AssetId default_material) {
  ++runtime_binding_generation_;
  abandon_scene_backed_panels();
  if (viewport_panel_ != nullptr) {
    // Project rebinding can release the previous SceneSession before this
    // callback. Drop only Qt-owned state; the stored runtime pair may dangle.
    viewport_panel_->abandon_interactions();
  }
  editor_ = &editor;
  if (operation_input_hints_ != nullptr) {
    operation_input_hints_->invalidate();
  }
  document_ = &document;
  assets_ = &assets;
  modeling_ = &modeling;
  selection_ = &selection;
  native_tools_ = &native_tools;
  point_placement_ = &point_placement;
  default_material_ = default_material;
  if (command_palette_ == nullptr && main_window_ != nullptr) {
    command_palette_ =
        new QtCommandPalette(editor.commands(), localization_,
                             main_window_.get());
    command_palette_->resize(
        ui_scale_controller_->metrics().size(QSize(760, 460)));
    QObject::connect(command_palette_, &QDialog::finished, command_palette_,
                     [this](int) {
                       command_palette_open_pending_ = false;
                     });
    command_palette_->hide();
  } else if (command_palette_ != nullptr) {
    command_palette_->rebind_commands(editor.commands());
  }
  invalidate_material_presentation_cache();
  outliner_presenter_.invalidate();
  layers_presenter_.invalidate();
  inspector_presenter_.invalidate();
  inspector_material_query_presenter_.invalidate();
  toolbar_owner_presenter_.invalidate();
  toolbar_tool_presenter_.invalidate();
  toolbar_transform_presenter_.invalidate();
  tool_settings_presenter_.invalidate();
  active_material_panel_presenter_.invalidate();
  face_texture_presenter_.invalidate();
  uv_peel_presenter_.invalidate();
  standard_material_editor_presenter_.invalidate();
  status_presenter_.invalidate();
  project_workspace_instance_edit_revision_ = 0;
  project_workspace_instance_edit_revision_valid_ = false;
  bind_scene_backed_panels();
  bind_project_workspace_events(editor);

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
  tool_toolbar_->set_transform_space_toggle_requested_callback([this]() {
    if (editor_ == nullptr) {
      return;
    }
    present_lifecycle_result(editor_->commands().execute(
        quader_command::kViewportToggleTransformSpace));
    tool_toolbar_->synchronize(*editor_, editor_->selection().summary(), true);
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
  texture_toolbar_->set_command_requested_callback(
      [this](QuaderCommandId command_id) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(editor_->commands().execute(command_id));
        texture_toolbar_->synchronize(*editor_, true);
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
  apply_viewport_settings_to_workspace();
  viewport_panel_->attach_workspace(workspace);
  tool_settings_panel_->bind(editor);
  settings_dialog_->set_context(
      settings_, viewport_settings_, localization_, editor.commands(),
      [this](const QtSettingsChange &change) {
        handle_settings_dialog_change(change);
      },
      [this]() { return save_settings(); });

  if (main_menu_ == nullptr) {
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
          .show_about =
              [this]() {
                if (lifecycle_message_box_ == nullptr) {
                  return;
                }
                const QString version =
                    QString::fromUtf8(kQuaderAppVersionDisplay);
                (void)lifecycle_message_box_->present(
                    QtRetainedMessageBoxPresentation{
                        .icon = QMessageBox::Information,
                        .object_name =
                            QStringLiteral("about_quader_message_box"),
                        .title = QString::fromStdString(localization_.text(
                            "ui.about.title", "About Quader")),
                        .text =
                            QString::fromStdString(localization_.text(
                                "ui.about.version", "Quader %1"))
                                .arg(version),
                        .informative_text = QString::fromStdString(
                            localization_.text(
                                "ui.about.description",
                                "A native polygon modeling editor for "
                                "Windows.")),
                        .buttons =
                            {{
                                .button =
                                    QtRetainedMessageBoxButton::Primary,
                                .object_name = QStringLiteral(
                                    "aboutQuaderMessageBox.ok"),
                                .label = QString::fromStdString(
                                    localization_.text("ui.action.ok", "OK")),
                                .role = QMessageBox::AcceptRole,
                            }},
                        .default_button =
                            QtRetainedMessageBoxButton::Primary,
                        .escape_button =
                            QtRetainedMessageBoxButton::Primary,
                        .close_button =
                            QtRetainedMessageBoxButton::Primary,
                    });
              },
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
          .texture_toolbar_visible =
              [this]() {
                return texture_toolbar_ != nullptr && texture_toolbar_->isVisible();
              },
          .set_texture_toolbar_visible =
              [this](bool visible) {
                if (texture_toolbar_ != nullptr) {
                  texture_toolbar_->setVisible(visible);
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
          .show_material_find_replace =
              [this]() { show_material_find_replace(); },
          .material_find_replace_available =
              [this]() {
                return !home_visible_ && editor_ != nullptr &&
                       editor_->project().has_project() &&
                       editor_->scene_documents().has_active_scene();
              },
          .show_autosave_recovery = [this]() { show_autosave_recovery(); },
          .autosave_recovery_available = [this]() {
            return editor_ != nullptr && editor_->project().has_project();
          },
        });
  } else {
    // A close command can synchronously rebind the active SceneSession from a
    // QAction callback owned by this menu. Keep that callback owner alive and
    // update only the runtime references it resolves on subsequent access.
    main_menu_->rebind_runtime(editor, modeling);
  }

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
      .show_command_search =
          [this]() {
            if (command_palette_ == nullptr) {
              return runtime_failure(localization_.text(
                  "ui.runtime.command_search_unavailable",
                  "Command Search is not available."));
            }
            command_palette_open_pending_ = true;
            command_palette_->open_palette();
            return runtime_status(false);
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
      .interface_scale =
          [this]() { return static_cast<double>(settings_.interface_scale()); },
      .set_interface_scale =
          [this](double factor) {
            if (!std::isfinite(factor)) {
              return runtime_failure(localization_.text(
                  "ui.runtime.interface_scale_invalid",
                  "Interface scale must be finite."));
            }
            const float canonical = static_cast<float>(std::clamp(
                factor,
                static_cast<double>(UISettings::kMinimumInterfaceScale),
                static_cast<double>(UISettings::kMaximumInterfaceScale)));
            const bool persisted_changed =
                settings_.interface_scale() != canonical;
            const bool presented_changed =
                ui_scale_controller_ != nullptr &&
                ui_scale_controller_->metrics().factor() != canonical;
            const bool changed = persisted_changed || presented_changed;
            const float previous_persisted = settings_.interface_scale();
            const float previous_presented =
                ui_scale_controller_ != nullptr
                    ? ui_scale_controller_->metrics().factor()
                    : previous_persisted;
            settings_.set_interface_scale(canonical);
            if (changed) {
              (void)apply_interface_scale(settings_.interface_scale());
              if (!save_settings()) {
                settings_.set_interface_scale(previous_persisted);
                (void)apply_interface_scale(previous_presented);
                return runtime_failure(localization_.text(
                    "ui.runtime.preferences_save_failed",
                    "Preferences could not be saved."));
              }
              if (settings_dialog_ != nullptr &&
                  settings_dialog_->isVisible()) {
                settings_dialog_->synchronize_interface_scale();
              }
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .vsync_enabled = [this]() { return settings_.vsync_enabled(); },
      .set_vsync_enabled =
          [this](bool enabled) {
            if (render_system_ == nullptr) {
              return runtime_failure(localization_.text(
                  "ui.runtime.renderer_unavailable",
                  "Renderer runtime is not attached."));
            }
            const bool previous_persisted = settings_.vsync_enabled();
            const bool previous_presented = render_system_->vsync_enabled();
            const bool changed = previous_persisted != enabled ||
                                 previous_presented != enabled;
            if (changed) {
              settings_.set_vsync_enabled(enabled);
              render_system_->set_vsync_enabled(enabled);
              if (!save_settings()) {
                settings_.set_vsync_enabled(previous_persisted);
                render_system_->set_vsync_enabled(previous_presented);
                return runtime_failure(localization_.text(
                    "ui.runtime.preferences_save_failed",
                    "Preferences could not be saved."));
              }
              if (settings_dialog_ != nullptr &&
                  settings_dialog_->isVisible()) {
                settings_dialog_->synchronize_vsync_enabled();
              }
            }
            return runtime_status(changed);
          },
      .session_settings = [this]() { return settings_.session_settings(); },
      .set_session_settings = [this](SessionPreferences value) {
        const SessionPreferences previous = settings_.session_settings();
        if (!value.valid()) {
          return runtime_failure(localization_.text(
              "ui.runtime.session_preferences_invalid",
              "Undo or autosave settings are outside the supported range."));
        }
        if (previous == value) {
          return runtime_status(false);
        }
        (void)settings_.set_session_settings(value);
        if (!save_settings()) {
          (void)settings_.set_session_settings(previous);
          return runtime_failure(localization_.text(
              "ui.runtime.preferences_save_failed",
              "Preferences could not be saved."));
        }
        if (session_settings_changed_) {
          session_settings_changed_(value);
        }
        if (settings_dialog_ != nullptr && settings_dialog_->isVisible()) {
          settings_dialog_->synchronize_session_settings();
        }
        return runtime_status(true);
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
      .backface_selection_enabled = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::BackfaceSelection);
      },
      .set_backface_selection_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::BackfaceSelection, enabled);
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
      .texture_lock_enabled = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::TextureLock);
      },
      .set_texture_lock_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::TextureLock, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .texture_lock_scale_enabled = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::TextureLockScale);
      },
      .set_texture_lock_scale_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::TextureLockScale, enabled);
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
      .viewport_info_visible = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::ShowInfo);
      },
      .set_viewport_info_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowInfo, visible);
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

  if (command_event_filter_ == nullptr) {
    command_event_filter_ = std::make_unique<QtCommandEventFilter>(
        editor, [this]() {
          return viewport_panel_ != nullptr ? viewport_panel_->tool_state()
                                            : ViewportToolState{};
        });
    command_event_filter_->set_command_input_owner_provider([this]() {
      return command_palette_open_pending_ ||
             qt_window_owns_command_input(command_palette_) ||
             qt_window_owns_command_input(settings_dialog_) ||
             qt_window_owns_command_input(material_browser_window_) ||
             qt_window_owns_command_input(material_picker_window_) ||
             qt_window_owns_command_input(material_find_replace_dialog_) ||
             qt_window_owns_command_input(standard_material_editor_window_);
    });
    command_event_filter_->set_outliner_input_owner_provider([this]() {
      return qt_outliner_owns_command_input(outliner_panel_);
    });
    command_event_filter_->set_input_context_provider([this]() {
      QuaderViewportKeybindingContext context =
          viewport_panel_ != nullptr ? viewport_panel_->command_input_context()
                                    : QuaderViewportKeybindingContext{};
      if (modeling_ != nullptr) {
        context.operation_preview_active = modeling_->operation_preview_active();
        context.modal_tool_active =
            context.modal_tool_active || modeling_->modal_tool_owns_viewport();
      }
      return context;
    });
    if (QCoreApplication *application = QApplication::instance()) {
      application->installEventFilter(command_event_filter_.get());
    }
  } else {
    // Ctrl+W can close a scene and re-enter this function from inside the
    // filter's own eventFilter call. Rebind it in place so event delivery can
    // return through the same live QObject.
    command_event_filter_->rebind_editor(editor);
  }
  refresh_project_workspace();
}

void UISystem::refresh_project_workspace(bool force) {
  QDR_PROFILE_SCOPE("UI project workspace refresh");
  if (editor_ == nullptr || project_home_page_ == nullptr ||
      scene_workspace_ == nullptr) {
    return;
  }

  observe_material_presentation_events();
  if (force) {
    project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
  }
  const std::uint64_t instance_edit_revision = [&]() {
    QDR_PROFILE_SCOPE("UI project workspace query instance edit revision");
    return editor_->modeling().instance_edit_revision();
  }();
  if (!project_workspace_instance_edit_revision_valid_ ||
      project_workspace_instance_edit_revision_ != instance_edit_revision) {
    project_workspace_instance_edit_revision_ = instance_edit_revision;
    project_workspace_instance_edit_revision_valid_ = true;
    project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
  }
  if (!project_workspace_refresh_signal_->exchange(
          false, std::memory_order_acq_rel)) {
    return;
  }

  {
    QDR_PROFILE_SCOPE("UI project workspace refresh material editor");
    refresh_standard_material_editor();
  }

  {
    QDR_PROFILE_SCOPE("UI project workspace synchronize presentation");
    const bool has_project = editor_->project().has_project();
    if (has_project) {
      show_workspace();
    } else {
      show_home();
    }

    if (!has_project) {
      std::vector<QtRecentProjectItem> recent;
      for (const QuaderRecentProject &project :
           editor_->project().recent_projects()) {
        recent.push_back({
            .name = QString::fromStdString(project.name),
            .manifest_path =
                QString::fromStdWString(project.manifest_path.wstring()),
            .available = project.available,
            .manifest_last_modified_unix_ms =
                project.manifest_last_modified_unix_ms,
            .manifest_size_bytes = project.manifest_size_bytes,
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

    const bool active_material_panel_visible =
        panel_presented(UIPanelId::ActiveMaterial);
    const bool material_browser_visible =
        material_browser_window_ != nullptr &&
        material_browser_window_->isVisible();
    const bool material_consumer_visible =
        active_material_panel_visible ||
        panel_presented(UIPanelId::Inspector) ||
        material_browser_visible ||
        (material_picker_window_ != nullptr &&
         material_picker_window_->isVisible()) ||
        (material_find_replace_dialog_ != nullptr &&
         material_find_replace_dialog_->isVisible());
    if (has_project && material_consumer_visible) {
      refresh_material_presentation_cache();
    }

    if (has_project &&
        (active_material_panel_visible || material_browser_visible)) {
      QuaderMaterialReference active_reference;
      const QuaderApiResult<QuaderProjectMaterialInfo> active_material =
          editor_->project_assets().active_material();
      if (active_material.succeeded()) {
        active_reference = QuaderMaterialReference::project(
            active_material.value().id);
      }
      if (active_material_panel_visible) {
        refresh_active_material_panel(active_reference);
      }
      if (material_browser_visible && material_browser_catalog_signature_ !=
          material_presentation_catalog_revision_) {
        std::vector<QtMaterialPresentationItem> project_materials;
        for (const QtMaterialPresentationItem &material :
             material_presentation_items_) {
          if (material.reference.project_owned() && material.available) {
            project_materials.push_back(material);
          }
        }
        material_browser_window_->set_materials(
            std::move(project_materials), active_reference);
        material_browser_catalog_signature_ =
            material_presentation_catalog_revision_;
        material_browser_active_reference_ = active_reference;
      } else if (material_browser_visible && active_reference !=
                 material_browser_active_reference_) {
        material_browser_window_->set_active_material(active_reference);
        material_browser_active_reference_ = active_reference;
      }
    }
  }
}

void UISystem::refresh_active_material_panel(
    QuaderMaterialReference active_reference) {
  if (active_material_panel_ == nullptr ||
      !panel_presented(UIPanelId::ActiveMaterial)) {
    return;
  }
  const ActiveMaterialPanelPresentationStamp stamp{
      .catalog_revision = material_presentation_catalog_revision_,
      .reference = active_reference,
  };
  if (!active_material_panel_presenter_.needs_refresh(stamp)) {
    return;
  }
  active_material_panel_->set_presentation(
      material_presentation_for(active_reference, false),
      material_browser_window_ != nullptr);
  active_material_panel_presenter_.mark_presented(stamp);
}

void UISystem::invalidate_material_presentation_cache() {
  ++material_presentation_invalidation_generation_;
  material_presentation_cache_valid_ = false;
  inspector_material_query_presenter_.invalidate();
}

void UISystem::observe_material_presentation_events() {
  const std::uint64_t generation =
      material_presentation_event_generation_signal_->load(
          std::memory_order_acquire);
  if (generation == material_presentation_observed_event_generation_) {
    return;
  }
  material_presentation_observed_event_generation_ = generation;
  material_presentation_cache_valid_ = false;
  inspector_material_query_presenter_.invalidate();
}

void UISystem::refresh_material_presentation_cache() {
  QDR_PROFILE_SCOPE("UI material presentation cache refresh");
  observe_material_presentation_events();

  const std::uint64_t active_scene_id = [&]() {
    if (editor_ == nullptr) {
      return std::uint64_t{0};
    }
    const std::optional<SceneSessionId> scene_id =
        editor_->scene_documents().active_scene_id();
    return scene_id.has_value() ? scene_id->packed() : std::uint64_t{0};
  }();

  if (editor_ == nullptr || !editor_->project().has_project()) {
    const bool presentation_changed =
        material_presentation_cache_valid_ ||
        !material_presentation_items_.empty();
    material_presentation_items_.clear();
    material_presentation_signature_ = 0;
    material_presentation_cache_valid_ = false;
    material_presentation_catalog_attempt_.reset();
    material_presentation_context_observation_.reset();
    if (presentation_changed) {
      ++material_presentation_catalog_revision_;
      ++material_presentation_revision_;
      inspector_presenter_.invalidate();
      inspector_material_query_presenter_.invalidate();
      synchronize_material_presentation_consumers();
    }
    return;
  }

  // Project materials remain available with no open scene. Do not include the
  // host's fallback scene or its embedded assets in that project-only state.
  const bool scene_available = active_scene_id != 0u &&
                               document_ != nullptr && assets_ != nullptr &&
                               modeling_ != nullptr;
  const MaterialPresentationContextStamp context_stamp{
      .document = scene_available ? document_ : nullptr,
      .modeling = scene_available ? modeling_ : nullptr,
      .assets = scene_available ? assets_ : nullptr,
      .active_scene_id = scene_available ? active_scene_id : 0u,
      .material_binding_revision =
          scene_available ? document_->material_binding_revision() : 0u,
      .mesh_binding_revision =
          scene_available ? document_->mesh_binding_revision() : 0u,
      .mesh_material_usage_revision =
          scene_available ? assets_->mesh_material_usage_revision() : 0u,
      .hierarchy_revision =
          scene_available ? document_->hierarchy_presentation_revision() : 0u,
      .modeling_content_revision =
          scene_available ? modeling_->content_revision() : 0u,
      .modeling_object_map_revision =
          scene_available ? modeling_->modeling_object_map_revision() : 0u,
  };
  const bool context_changed =
      !material_presentation_context_observation_.has_value() ||
      *material_presentation_context_observation_ != context_stamp;
  material_presentation_context_observation_ = context_stamp;

  const QuaderApiResult<QuaderMaterialPresentationRevision> owner_revision =
      editor_->project_assets().material_presentation_revision();
  if (!owner_revision.succeeded()) {
    material_presentation_cache_valid_ = false;
    material_presentation_catalog_attempt_.reset();
    return;
  }
  const MaterialPresentationCatalogStamp catalog_stamp{
      .editor = editor_,
      .owner_revision = owner_revision.value(),
      .event_generation =
          material_presentation_observed_event_generation_,
      .invalidation_generation =
          material_presentation_invalidation_generation_,
      .localization_revision = localization_.revision(),
  };
  if (material_presentation_catalog_attempt_.has_value() &&
      *material_presentation_catalog_attempt_ == catalog_stamp) {
    if (context_changed) {
      ++material_presentation_revision_;
      inspector_presenter_.invalidate();
      inspector_material_query_presenter_.invalidate();
      synchronize_material_presentation_consumers();
    }
    return;
  }

  // Record the attempted owner/event inputs before querying. A failed project
  // catalog read therefore stays gated until a real owner or event changes.
  material_presentation_catalog_attempt_ = catalog_stamp;
  QuaderApiResult<QuaderMaterialPresentationSnapshot> materials =
      editor_->project_assets().material_presentations();
  if (!materials.succeeded()) {
    const bool presentation_changed =
        material_presentation_cache_valid_ ||
        !material_presentation_items_.empty();
    material_presentation_items_.clear();
    material_presentation_signature_ = 0;
    material_presentation_cache_valid_ = false;
    if (presentation_changed) {
      ++material_presentation_catalog_revision_;
    }
    if (presentation_changed || context_changed) {
      ++material_presentation_revision_;
      inspector_presenter_.invalidate();
      inspector_material_query_presenter_.invalidate();
      synchronize_material_presentation_consumers();
    }
    return;
  }

  const auto reuse_or_make_thumbnail =
      [this](QtMaterialPresentationItem &item) {
        const auto existing = std::ranges::find_if(
            material_presentation_items_,
            [&item](const QtMaterialPresentationItem &candidate) {
              return candidate.reference == item.reference;
            });
        if (existing != material_presentation_items_.end() &&
            existing->content_revision == item.content_revision &&
            existing->encoded_albedo == item.encoded_albedo &&
            existing->fallback_color == item.fallback_color &&
            existing->thumbnail) {
          item.thumbnail = existing->thumbnail;
        } else {
          item.thumbnail = make_qt_material_thumbnail(item);
        }
      };

  std::vector<QtMaterialPresentationItem> next;
  next.reserve(materials.value().materials.size());
  for (const QuaderMaterialPresentationItem &material :
       materials.value().materials) {
    const QuaderMaterialReference reference = material.reference;
    const std::string_view type_key = [&material]() -> std::string_view {
      switch (material.kind) {
      case QuaderMaterialPresentationKind::Embedded:
        return "ui.material_browser.embedded_material_type";
      case QuaderMaterialPresentationKind::Standard:
        return "ui.material_browser.standard_material_type";
      case QuaderMaterialPresentationKind::Graph:
        return "ui.material_browser.graph_material_type";
      }
      return "ui.material_browser.embedded_material_type";
    }();
    const std::string_view type_fallback =
        material.kind == QuaderMaterialPresentationKind::Embedded
            ? "Embedded Material"
            : material.kind == QuaderMaterialPresentationKind::Standard
                  ? "Standard Material"
                  : "Graph Material";
    QtMaterialPresentationItem item{
        .reference = reference,
        .stable_id = qt_material_reference_stable_id(reference),
        .name = QString::fromStdString(material.name),
        .project_relative_path =
            QString::fromStdString(material.project_relative_path),
        .type_display_name =
            QString::fromStdString(localization_.text(type_key, type_fallback)),
        .qmat_size_bytes = static_cast<qint64>(material.qmat_size_bytes),
        .encoded_albedo = QByteArray(
            reinterpret_cast<const char *>(material.encoded_albedo.data()),
            static_cast<qsizetype>(material.encoded_albedo.size())),
        .fallback_color = QColor::fromRgbF(
            material.fallback_color[0], material.fallback_color[1],
            material.fallback_color[2]),
        .content_revision = material.content_revision,
        .available = material.available,
        .editable = material.editable,
    };
    reuse_or_make_thumbnail(item);
    next.push_back(std::move(item));
  }

  std::uint64_t signature = 1469598103934665603ull;
  const auto mix = [&signature](std::uint64_t value) {
    signature ^= value;
    signature *= 1099511628211ull;
  };
  const auto mix_text = [&mix](std::string_view value) {
    for (const char character : value) {
      mix(static_cast<unsigned char>(character));
    }
  };
  const QuaderProjectMetadata project = editor_->project().metadata();
  mix_text(project.uuid);
  mix_text(editor_->project().manifest_path().generic_string());
  mix_text(localization_.current_locale());
  for (const QtMaterialPresentationItem &item : next) {
    const QByteArray stable_id = item.stable_id.toUtf8();
    const QByteArray name = item.name.toUtf8();
    const QByteArray path = item.project_relative_path.toUtf8();
    const QByteArray type = item.type_display_name.toUtf8();
    mix_text(std::string_view(stable_id.constData(), stable_id.size()));
    mix_text(std::string_view(name.constData(), name.size()));
    mix_text(std::string_view(path.constData(), path.size()));
    mix_text(std::string_view(type.constData(), type.size()));
    mix(static_cast<std::uint64_t>(item.qmat_size_bytes));
    mix(item.content_revision);
    mix(static_cast<std::uint64_t>(item.fallback_color.rgba()));
    mix(item.available ? 1u : 0u);
    mix(item.editable ? 1u : 0u);
  }

  if (signature == material_presentation_signature_) {
    material_presentation_cache_valid_ = true;
    if (context_changed) {
      ++material_presentation_revision_;
      inspector_presenter_.invalidate();
      inspector_material_query_presenter_.invalidate();
      synchronize_material_presentation_consumers();
    }
    return;
  }

  material_presentation_items_ = std::move(next);
  material_presentation_cache_valid_ = true;
  material_presentation_signature_ = signature;
  ++material_presentation_catalog_revision_;
  ++material_presentation_revision_;
  inspector_presenter_.invalidate();
  inspector_material_query_presenter_.invalidate();
  synchronize_material_presentation_consumers();
}

void UISystem::revalidate_material_find_replace_presentations() {
  if (material_find_replace_dialog_ == nullptr ||
      !material_find_replace_dialog_->isVisible() ||
      material_find_replace_presentation_revision_ ==
          material_presentation_revision_) {
    return;
  }
  const std::optional<QtMaterialPresentationItem> current_find =
      material_find_replace_dialog_->material(
          QtMaterialFindReplaceColumn::Find);
  const std::optional<QtMaterialPresentationItem> current_replacement =
      material_find_replace_dialog_->material(
          QtMaterialFindReplaceColumn::Replace);
  if (current_find.has_value()) {
    material_find_replace_dialog_->set_find_material(
        material_presentation_for(current_find->reference, true));
  }
  if (current_replacement.has_value()) {
    material_find_replace_dialog_->set_replacement_material(
        material_presentation_for(current_replacement->reference, true));
  }
  material_find_replace_presentation_revision_ =
      material_presentation_revision_;
}

void UISystem::refresh_material_picker_presentations() {
  if (material_picker_window_ == nullptr ||
      !material_picker_window_->isVisible() || editor_ == nullptr ||
      material_picker_presentation_revision_ ==
          material_presentation_revision_) {
    return;
  }
  const QuaderMaterialReference selected =
      material_picker_window_->selected_material();
  std::vector<QtMaterialPresentationItem> items;
  if (material_picker_replacement_column_) {
    items = replacement_material_presentations();
  } else {
    const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
        editor_->materials().used_materials(material_find_replace_scope_);
    if (used.succeeded()) {
      items = material_presentations_for(used.value(), true);
    }
  }
  material_picker_window_->set_materials(std::move(items), selected);
  material_picker_presentation_revision_ =
      material_presentation_revision_;
}

void UISystem::synchronize_material_presentation_consumers() {
  revalidate_material_find_replace_presentations();
  refresh_material_picker_presentations();
}

std::optional<QtMaterialPresentationItem>
UISystem::material_presentation_for(QuaderMaterialReference reference,
                                    bool include_missing) const {
  const auto found = std::ranges::find_if(
      material_presentation_items_,
      [&reference](const QtMaterialPresentationItem &item) {
        return item.reference == reference;
      });
  if (found != material_presentation_items_.end()) {
    return *found;
  }
  if (!include_missing || !reference.valid()) {
    return std::nullopt;
  }
  QtMaterialPresentationItem missing{
      .reference = reference,
      .stable_id = qt_material_reference_stable_id(reference),
      .name = QString::fromStdString(localization_.text(
          "ui.inspector.material.missing", "Missing material")),
      .type_display_name = QString::fromStdString(localization_.text(
          "ui.inspector.material.missing", "Missing material")),
      .fallback_color = QColor(Qt::darkGray),
      .available = false,
      .editable = false,
  };
  missing.thumbnail = make_qt_material_thumbnail(missing);
  return missing;
}

std::vector<QtMaterialPresentationItem>
UISystem::material_presentations_for(
    const std::vector<QuaderMaterialReference> &references,
    bool include_missing) const {
  std::vector<QtMaterialPresentationItem> result;
  result.reserve(references.size());
  for (const QuaderMaterialReference &reference : references) {
    if (std::ranges::any_of(
            result, [&reference](const QtMaterialPresentationItem &item) {
              return item.reference == reference;
            })) {
      continue;
    }
    std::optional<QtMaterialPresentationItem> presentation =
        material_presentation_for(reference, include_missing);
    if (presentation.has_value()) {
      result.push_back(std::move(*presentation));
    }
  }
  return result;
}

std::vector<QtMaterialPresentationItem>
UISystem::replacement_material_presentations() const {
  std::vector<QtMaterialPresentationItem> result;
  for (const QtMaterialPresentationItem &material :
       material_presentation_items_) {
    if (material.replacement_valid()) {
      result.push_back(material);
    }
  }
  return result;
}

void UISystem::refresh_inspector_material_presentation() {
  if (inspector_panel_ == nullptr || editor_ == nullptr ||
      document_ == nullptr || assets_ == nullptr || modeling_ == nullptr) {
    return;
  }
  refresh_material_presentation_cache();
  const InspectorMaterialQueryStamp stamp{
      .presentation_revision = material_presentation_revision_,
      .scene_selection_revision = document_->selection_revision(),
      .hierarchy_revision = document_->hierarchy_presentation_revision(),
      .material_binding_revision = document_->material_binding_revision(),
      .mesh_binding_revision = document_->mesh_binding_revision(),
      .mesh_material_usage_revision =
          assets_->mesh_material_usage_revision(),
      .modeling_content_revision = modeling_->content_revision(),
      .modeling_selection_revision = modeling_->selection_revision(),
      .modeling_object_map_revision =
          modeling_->modeling_object_map_revision(),
  };
  if (!inspector_material_query_presenter_.needs_refresh(stamp)) {
    return;
  }

  const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
      editor_->materials().used_materials(
          QuaderMaterialUsageScope::Selection);
  const QuaderSelectionSummary selection = editor_->selection().summary();
  const bool object_material_surface_selection =
      selection.mode == QuaderSelectionMode::Object &&
      std::ranges::any_of(
          document_->selected_entities(), [this](EntityId entity) {
            return scene_entity_has_material_surface(*document_, entity);
          });
  const bool section_visible =
      selection.mode == QuaderSelectionMode::Face ||
      selection.mode == QuaderSelectionMode::Group ||
      object_material_surface_selection;
  std::uint64_t revision = 1469598103934665603ull;
  const auto mix = [&revision](std::uint64_t value) {
    revision ^= value;
    revision *= 1099511628211ull;
  };
  mix(material_presentation_revision_);
  mix(stamp.scene_selection_revision);
  mix(stamp.hierarchy_revision);
  mix(stamp.material_binding_revision);
  mix(stamp.mesh_binding_revision);
  mix(stamp.mesh_material_usage_revision);
  mix(stamp.modeling_content_revision);
  mix(stamp.modeling_selection_revision);
  mix(stamp.modeling_object_map_revision);
  inspector_panel_->set_material_presentation(
      InspectorMaterialPresentationSnapshot{
          .revision = revision,
          .section_visible = used.succeeded() && section_visible,
          .materials = used.succeeded()
                           ? material_presentations_for(used.value(), true)
                           : std::vector<QtMaterialPresentationItem>{},
          .edit_material =
              [this](QuaderMaterialReference reference) {
                if (reference.project_owned()) {
                  open_standard_material(reference.project_asset.str());
                }
              },
          .replace_material =
              [this](QuaderMaterialReference reference) {
                show_material_find_replace(reference);
              },
      });
  inspector_material_query_presenter_.mark_presented(stamp);
}

void UISystem::show_home() {
  if (home_visible_) {
    return;
  }
  const bool presentation_changed = !home_visible_;
  if (presentation_changed) {
    capture_workspace_window_state();
  }
  home_visible_ = true;
  if (presentation_changed && material_browser_window_ != nullptr) {
    material_browser_window_->hide();
    material_browser_window_->clear_materials();
    material_browser_catalog_signature_ = 0;
    material_browser_active_reference_ = {};
  }
  if (presentation_changed) {
    close_material_find_replace();
    if (active_material_panel_ != nullptr) {
      active_material_panel_->set_presentation(std::nullopt, false);
    }
    active_material_panel_presenter_.invalidate();
    material_presentation_items_.clear();
    material_presentation_signature_ = 0;
    material_presentation_catalog_attempt_.reset();
    material_presentation_context_observation_.reset();
    invalidate_material_presentation_cache();
    ++material_presentation_catalog_revision_;
    ++material_presentation_revision_;
  }
  if (standard_material_editor_window_ != nullptr) {
    standard_material_editor_window_->hide();
    standard_material_editor_window_->clear_document();
    standard_material_editor_presenter_.invalidate();
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
  if (texture_toolbar_ != nullptr) {
    texture_toolbar_->setEnabled(false);
    texture_toolbar_->hide();
  }
  if (tool_toolbar_ != nullptr) {
    tool_toolbar_->setEnabled(false);
    tool_toolbar_->hide();
  }
  for (UIPanelId id : kUiDockPanelIds) {
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
      project_workspace_refresh_signal_->store(true,
                                               std::memory_order_relaxed);
      return;
    }
    viewport_initialized_ = true;
    if (main_window_ != nullptr && main_window_->isVisible()) {
      prepare_startup_presentation();
    }
  }
  if (!presentation_changed) {
    return;
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
  if (texture_toolbar_ != nullptr) {
    texture_toolbar_->setEnabled(true);
    texture_toolbar_->setVisible(texture_toolbar_workspace_visible_);
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
    if (texture_toolbar_ != nullptr) {
      texture_toolbar_->synchronize(*editor_, true);
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
  if (unsaved_confirmation_ == nullptr) {
    return QuaderDirtyProjectDecision::Cancel;
  }
  const QtUnsavedConfirmationDecision decision =
      unsaved_confirmation_->present(QtUnsavedConfirmationPresentation{
          .object_name =
              QStringLiteral("project_unsaved_confirmation"),
          .title = QString::fromStdString(localization_.text(
              "ui.project.unsaved.title", "Unsaved Changes")),
          .message = QString::fromStdString(localization_.text(
              opening   ? "ui.project.unsaved.open"
              : closing ? "ui.project.unsaved.close"
                        : "ui.project.unsaved.new",
              opening   ? "Save changes before opening another project?"
              : closing ? "Save changes before closing Quader?"
                        : "Save changes before creating a new project?")),
          .save_label = QString::fromStdString(
              localization_.text("ui.project.unsaved.save", "Save")),
          .discard_label = QString::fromStdString(localization_.text(
              "ui.project.unsaved.discard", "Discard")),
          .cancel_label = QString::fromStdString(
              localization_.text("ui.project.unsaved.cancel", "Cancel")),
      });
  if (decision == QtUnsavedConfirmationDecision::Save) {
    return QuaderDirtyProjectDecision::Save;
  }
  if (decision == QtUnsavedConfirmationDecision::Discard) {
    return QuaderDirtyProjectDecision::Discard;
  }
  return QuaderDirtyProjectDecision::Cancel;
}

QuaderDirtySceneDecision
UISystem::confirm_dirty_scene(const QuaderSceneDocumentInfo &scene,
                              std::string_view action_id) {
  (void)action_id;
  if (unsaved_confirmation_ == nullptr) {
    return QuaderDirtySceneDecision::Cancel;
  }
  const QtUnsavedConfirmationDecision decision =
      unsaved_confirmation_->present(QtUnsavedConfirmationPresentation{
          .object_name = QStringLiteral("scene_unsaved_confirmation"),
          .title = QString::fromStdString(localization_.text(
              "ui.scene.unsaved.title", "Unsaved Scene")),
          .message = QString::fromStdString(
              scene.name + " has unsaved changes. Save it?"),
          .save_label = QString::fromStdString(
              localization_.text("ui.scene.unsaved.save", "Save")),
          .discard_label = QString::fromStdString(localization_.text(
              "ui.scene.unsaved.discard", "Discard")),
          .cancel_label = QString::fromStdString(
              localization_.text("ui.scene.unsaved.cancel", "Cancel")),
      });
  if (decision == QtUnsavedConfirmationDecision::Save) {
    return QuaderDirtySceneDecision::Save;
  }
  if (decision == QtUnsavedConfirmationDecision::Discard) {
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
    schedule_retained_message_box_prewarm();
  }
}

void UISystem::schedule_retained_message_box_prewarm() {
  if (main_window_ == nullptr || retained_message_box_prewarm_scheduled_) {
    return;
  }
  retained_message_box_prewarm_scheduled_ = true;
  const std::array<QPointer<QtRetainedMessageBox>, 5u> message_boxes{
      QPointer<QtRetainedMessageBox>(unsaved_confirmation_),
      QPointer<QtRetainedMessageBox>(standard_material_unsaved_confirmation_),
      QPointer<QtRetainedMessageBox>(lifecycle_message_box_),
      QPointer<QtRetainedMessageBox>(standard_material_error_message_box_),
      QPointer<QtRetainedMessageBox>(material_browser_import_message_box_),
  };
  QTimer::singleShot(0, main_window_.get(), [this, message_boxes]() {
    retained_message_box_prewarm_scheduled_ = false;
    if (main_window_ == nullptr || !main_window_->isVisible() ||
        !main_window_->updatesEnabled()) {
      return;
    }
    for (const QPointer<QtRetainedMessageBox> &message_box : message_boxes) {
      if (message_box != nullptr) {
        message_box->prewarm();
      }
    }
    if (viewport_panel_ != nullptr) {
      viewport_panel_->prewarm_retained_dialogs();
    }
  });
}

void UISystem::shutdown() {
  if (main_window_ == nullptr && render_system_ == nullptr) {
    return;
  }
  clear_editor_runtime_hooks();
  abandon_scene_backed_panels();
  (void)save_settings();
  (void)save_viewport_settings();
  save_window_state();
  clear_project_workspace_events();
  if (command_event_filter_ != nullptr) {
    if (QCoreApplication *application = QApplication::instance()) {
      application->removeEventFilter(command_event_filter_.get());
    }
    command_event_filter_.reset();
  }
  if (tool_settings_panel_ != nullptr) {
    tool_settings_panel_->unbind();
  }
  if (viewport_panel_ != nullptr) {
    viewport_panel_->reset_interactions(editor_, native_tools_);
    viewport_panel_->shutdown();
  }
  if (material_preview_surface_ != nullptr) {
    material_preview_surface_->shutdown();
  }
  panels_.set_visibility_changed_callback({});
  panels_.set_presentation_changed_callback({});
  if (command_palette_ != nullptr) {
    command_palette_->hide();
  }
  main_menu_.reset();
  main_window_.reset();
  command_palette_ = nullptr;
  command_palette_open_pending_ = false;
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
  texture_toolbar_ = nullptr;
  tool_toolbar_ = nullptr;
  workspace_window_state_.clear();
  outliner_panel_ = nullptr;
  inspector_panel_ = nullptr;
  layer_panel_ = nullptr;
  console_panel_ = nullptr;
  settings_dialog_ = nullptr;
  material_browser_window_ = nullptr;
  active_material_panel_ = nullptr;
  material_picker_window_ = nullptr;
  material_find_replace_dialog_ = nullptr;
  material_preview_surface_ = nullptr;
  standard_material_editor_window_ = nullptr;
  tool_settings_panel_ = nullptr;
  unsaved_confirmation_ = nullptr;
  standard_material_unsaved_confirmation_ = nullptr;
  lifecycle_message_box_ = nullptr;
  standard_material_error_message_box_ = nullptr;
  material_browser_import_message_box_ = nullptr;
  status_message_ = nullptr;
  operation_input_hints_ = nullptr;
  status_version_ = nullptr;
  auxiliary_window_save_timer_ = nullptr;
  ui_scale_controller_.reset();
  render_system_ = nullptr;
  editor_ = nullptr;
  document_ = nullptr;
  assets_ = nullptr;
  modeling_ = nullptr;
  selection_ = nullptr;
  native_tools_ = nullptr;
  point_placement_ = nullptr;
  initialized_ = false;
  retained_message_box_prewarm_scheduled_ = false;
  viewport_initialized_ = false;
  home_visible_ = false;
  project_workspace_instance_edit_revision_ = 0;
  project_workspace_instance_edit_revision_valid_ = false;
  project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
  material_presentation_items_.clear();
  material_presentation_signature_ = 0;
  material_presentation_revision_ = 0;
  material_presentation_catalog_revision_ = 0;
  active_material_panel_presenter_.invalidate();
  material_presentation_catalog_attempt_.reset();
  material_presentation_context_observation_.reset();
  material_presentation_cache_valid_ = false;
  material_presentation_observed_event_generation_ = 0;
  material_presentation_invalidation_generation_ = 1;
  material_presentation_event_generation_signal_->store(
      1u, std::memory_order_relaxed);
  inspector_material_query_presenter_.invalidate();
  material_browser_catalog_signature_ = 0;
  material_browser_active_reference_ = {};
  material_find_replace_project_identity_.clear();
  material_find_replace_scene_id_ = 0;
  material_find_replace_scene_selection_revision_ = 0;
  material_find_replace_modeling_selection_revision_ = 0;
  material_find_replace_modeling_object_map_revision_ = 0;
  material_find_replace_presentation_revision_ = 0;
  material_picker_presentation_revision_ = 0;
  tool_settings_presenter_.invalidate();
  face_texture_presenter_.invalidate();
  uv_peel_presenter_.invalidate();
  standard_material_editor_presenter_.invalidate();
  status_presenter_.invalidate();
}

void UISystem::refresh(const FrameTiming &frame_timing,
                       RenderFramePerformanceStats render_performance) {
  QDR_PROFILE_SCOPE("UI system refresh");
  frame_timing_ = frame_timing;
  render_performance_ = render_performance;
  // Keep retained property/command presentation stable during a pointer drag.
  // Curve previews publish geometry each sample just like transform previews;
  // their final revisions are presented once the gesture ends.
  const bool edit_drag_active =
      (modeling_ != nullptr && modeling_->transform_drag_active()) ||
      (native_tools_ != nullptr &&
       native_tools_->presentation_snapshot().curve_dragging);
  const bool numeric_interaction_active =
      tool_settings_panel_ != nullptr &&
      tool_settings_panel_->numeric_interaction_active();
  const std::uint64_t frame_tool_settings_revision =
      editor_ != nullptr ? editor_->tools().active_settings_revision() : 0u;
  const std::uint64_t frame_active_tool_revision =
      editor_ != nullptr ? editor_->tools().active_tool_revision() : 0u;
  const std::uint64_t frame_transform_space_revision =
      editor_ != nullptr ? editor_->viewports().transform_space_revision()
                         : 0u;
  {
    QDR_PROFILE_SCOPE("UI refresh project workspace");
    refresh_project_workspace(false);
  }
  validate_material_find_replace_context();
  const bool retained_material_surface_visible =
      (material_browser_window_ != nullptr &&
       material_browser_window_->isVisible()) ||
      (material_picker_window_ != nullptr &&
       material_picker_window_->isVisible()) ||
      (material_find_replace_dialog_ != nullptr &&
       material_find_replace_dialog_->isVisible());
  if (retained_material_surface_visible) {
    refresh_material_presentation_cache();
  }
  if (editor_ != nullptr) {
    if (!edit_drag_active && !numeric_interaction_active) {
      QDR_PROFILE_SCOPE("UI refresh retained toolbars");
      const bool owner_available =
          document_ != nullptr && modeling_ != nullptr;
      const ToolbarOwnerPresentationStamp owner_stamp = [&]() {
        QDR_PROFILE_SCOPE("UI toolbar query owner revisions");
        return ToolbarOwnerPresentationStamp{
            .owner_available = owner_available,
            .scene_revision = owner_available ? document_->revision() : 0u,
            .scene_selection_revision =
                owner_available ? document_->selection_revision() : 0u,
            .modeling_content_revision =
                owner_available ? modeling_->content_revision() : 0u,
            .modeling_selection_revision =
                owner_available ? modeling_->selection_revision() : 0u,
        };
      }();
      const ToolbarToolPresentationStamp tool_stamp{
          .active_tool_revision = frame_active_tool_revision,
      };
      const ToolbarTransformPresentationStamp transform_stamp{
          .transform_space_revision = frame_transform_space_revision,
      };
      const bool owner_state_changed =
          toolbar_owner_presenter_.needs_refresh(owner_stamp);
      const bool active_tool_revision_changed =
          toolbar_tool_presenter_.needs_refresh(tool_stamp);
      const bool transform_space_revision_changed =
          toolbar_transform_presenter_.needs_refresh(transform_stamp);
      if (owner_state_changed || active_tool_revision_changed ||
          transform_space_revision_changed) {
        const QuaderSelectionSummary selection = [&]() {
          QDR_PROFILE_SCOPE("UI toolbar query selection summary");
          return editor_->selection().summary();
        }();
        if (selection_toolbar_ != nullptr && owner_state_changed) {
          QDR_PROFILE_SCOPE("UI synchronize selection toolbar");
          selection_toolbar_->synchronize(*editor_, selection.mode);
        }
        if (tool_toolbar_ != nullptr) {
          QDR_PROFILE_SCOPE("UI synchronize tool toolbar");
          tool_toolbar_->synchronize(*editor_, selection,
                                     owner_state_changed);
        }
        toolbar_owner_presenter_.mark_presented(owner_stamp);
        toolbar_tool_presenter_.mark_presented(tool_stamp);
        toolbar_transform_presenter_.mark_presented(transform_stamp);
      }
    }
    if (snapping_toolbar_ != nullptr) {
      QDR_PROFILE_SCOPE("UI synchronize snapping toolbar");
      snapping_toolbar_->synchronize(*editor_);
    }
    if (viewport_display_toolbar_ != nullptr) {
      QDR_PROFILE_SCOPE("UI synchronize viewport display toolbar");
      viewport_display_toolbar_->synchronize(*editor_);
    }
    if (texture_toolbar_ != nullptr) {
      QDR_PROFILE_SCOPE("UI synchronize texture toolbar");
      texture_toolbar_->synchronize(*editor_);
    }
  }
  if (viewport_panel_ != nullptr) {
    viewport_panel_->set_render_performance_stats(render_performance_);
  }
  {
    QDR_PROFILE_SCOPE("UI advance startup presentation");
    advance_startup_presentation();
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
  {
    QDR_PROFILE_SCOPE("UI refresh dock panels");
    const std::uint64_t localization_revision = localization_.revision();
    if (!edit_drag_active && panel_presented(UIPanelId::Outliner)) {
      const OutlinerPresentationStamp stamp{
          .hierarchy_revision =
              document_->hierarchy_presentation_revision(),
          .selection_revision = document_->selection_revision(),
          .instance_edit_revision =
              editor_->modeling().instance_edit_revision(),
          .localization_revision = localization_revision,
      };
      if (outliner_presenter_.needs_refresh(stamp)) {
        QDR_PROFILE_SCOPE("UI refresh outliner panel");
        outliner_panel_->refresh(context);
        outliner_presenter_.mark_presented(stamp);
      }
    }
    if (!edit_drag_active && panel_presented(UIPanelId::Layers)) {
      const LayersPresentationStamp stamp{
          .layer_revision = document_->layer_revision(),
          .selection_revision = document_->selection_revision(),
          .localization_revision = localization_revision,
      };
      if (layers_presenter_.needs_refresh(stamp)) {
        QDR_PROFILE_SCOPE("UI refresh layers panel");
        layer_panel_->refresh(context);
        layers_presenter_.mark_presented(stamp);
      }
    }
    if (!edit_drag_active && !numeric_interaction_active &&
        panel_presented(UIPanelId::Inspector)) {
      refresh_material_presentation_cache();
      const InspectorPresentationStamp stamp{
          .scene_revision = document_->revision(),
          .modeling_content_revision = modeling_->content_revision(),
          .modeling_selection_revision = modeling_->selection_revision(),
          .material_presentation_revision = material_presentation_revision_,
          .localization_revision = localization_revision,
      };
      if (inspector_presenter_.needs_refresh(stamp)) {
        QDR_PROFILE_SCOPE("UI refresh inspector panel");
        refresh_inspector_material_presentation();
        inspector_panel_->refresh(context);
        inspector_presenter_.mark_presented(stamp);
      }
    }
    if (panel_presented(UIPanelId::Console)) {
      QDR_PROFILE_SCOPE("UI refresh console panel");
      console_panel_->refresh(context);
    }
  }
  if (panel_visible(UIPanelId::ToolSettings) &&
      !numeric_interaction_active) {
    const ToolSettingsPresentationStamp stamp{
        .active_settings_revision = frame_tool_settings_revision,
    };
    if (tool_settings_presenter_.needs_refresh(stamp)) {
      QDR_PROFILE_SCOPE("UI refresh tool settings presentation");
      const bool was_active = tool_settings_panel_->has_active_target();
      tool_settings_panel_->refresh();
      if (!was_active && tool_settings_panel_->has_active_target() &&
          panel_visible(UIPanelId::ToolSettings)) {
        if (QDockWidget *dock = panels_.dock(UIPanelId::ToolSettings)) {
          dock->raise();
        }
      }
      tool_settings_presenter_.mark_presented(stamp);
    }
  }
  if (panel_visible(UIPanelId::ToolSettings)) {
    const QuaderFaceTextureRevision revision =
        editor_->modeling().face_texture_settings_revision();
    const std::uint64_t keybindings_revision =
        editor_->preferences().keybindings_revision();
    const double rotation_step = editor_->preferences().rotation_snap_degrees();
    const FaceTexturePresentationStamp texture_stamp{
        .settings = revision,
        .keybindings_revision = keybindings_revision,
        .rotation_step = rotation_step,
    };
    if (face_texture_presenter_.needs_refresh(texture_stamp)) {
      QDR_PROFILE_SCOPE("UI refresh face texture settings presentation");
      // Selection/scene changes must invalidate stale text even while a numeric
      // control owns focus. The panel preserves edits for unchanged targets.
      tool_settings_panel_->refresh_texture_settings();
      face_texture_presenter_.mark_presented(texture_stamp);
    }
    const QuaderUvPeelRevision peel_revision =
        editor_->modeling().uv_peel_settings_revision();
    const UvPeelPresentationStamp peel_stamp{
        .settings = peel_revision,
        .keybindings_revision = keybindings_revision,
    };
    if (uv_peel_presenter_.needs_refresh(peel_stamp)) {
      QDR_PROFILE_SCOPE("UI refresh UV Peel settings presentation");
      tool_settings_panel_->refresh_uv_peel_settings();
      uv_peel_presenter_.mark_presented(peel_stamp);
    }
  }
  {
    QDR_PROFILE_SCOPE("UI synchronize status bar");
    refresh_status_bar();
  }

  if (localized_locale_ != localization_.current_locale() ||
      localized_revision_ != localization_.revision()) {
    QDR_PROFILE_SCOPE("UI synchronize localized presentation");
    localized_locale_ = localization_.current_locale();
    localized_revision_ = localization_.revision();
    project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
    status_presenter_.invalidate();
    refresh_localized_chrome();
    main_menu_->rebuild();
    viewport_panel_->set_localization(localization_);
    tool_settings_panel_->set_localization(localization_);
  }
  if (ui_settings_changed_) {
    ui_settings_changed_ = false;
    (void)save_settings();
  }
}

void UISystem::process_viewport_input() {
  if (home_visible_ || !initialized_ || viewport_panel_ == nullptr || editor_ == nullptr ||
      document_ == nullptr || assets_ == nullptr || modeling_ == nullptr ||
      native_tools_ == nullptr) {
    return;
  }
  viewport_panel_->process_input(*editor_, *document_, *assets_, *modeling_,
                                 default_material_, *native_tools_,
                                 point_placement_, frame_timing_.delta_seconds);
}

void UISystem::prepare_viewports(
    SceneRenderBridge &render_bridge,
    RenderOverlaySnapshotCache &snapshot_cache) {
  if (home_visible_ || !initialized_ || editor_ == nullptr || document_ == nullptr ||
      assets_ == nullptr || modeling_ == nullptr || native_tools_ == nullptr) {
    return;
  }
  viewport_panel_->prepare_viewports(*editor_, render_bridge, snapshot_cache,
                                     *document_, *assets_, *modeling_,
                                     *native_tools_);
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
  const bool preview_pending =
      material_preview_surface_ != nullptr &&
      standard_material_editor_window_ != nullptr &&
      standard_material_editor_window_->isVisible() &&
      !standard_material_editor_window_->isMinimized() &&
      material_preview_surface_->needs_rendering();
  return scene_continuous || preview_pending;
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
  texture_toolbar_workspace_visible_ =
      texture_toolbar_ != nullptr && !texture_toolbar_->isHidden();
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
  panels_.set_presentation_changed_callback([this](UIPanelId, bool presented) {
    if (presented) {
      project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
    }
  });
  outliner_panel_ = new OutlinerPanel(main_window_.get());
  inspector_panel_ = new InspectorPanel(main_window_.get());
  layer_panel_ = new LayerPanel(main_window_.get());
  console_panel_ = new ConsolePanel(main_window_.get());
  tool_settings_panel_ = new QtToolSettingsPanel(main_window_.get());
  tool_settings_panel_->set_localization(localization_);
  active_material_panel_ = new QtActiveMaterialPanel(main_window_.get());
  active_material_panel_->set_browse_material_callback(
      [this]() { show_material_browser(); });
  QDockWidget &outliner = create_dock(
      UIPanelId::Outliner, *outliner_panel_, Qt::LeftDockWidgetArea);
  QDockWidget &inspector = create_dock(
      UIPanelId::Inspector, *inspector_panel_, Qt::RightDockWidgetArea);
  QDockWidget &layers = create_dock(
      UIPanelId::Layers, *layer_panel_, Qt::RightDockWidgetArea);
  QDockWidget &console = create_dock(
      UIPanelId::Console, *console_panel_, Qt::BottomDockWidgetArea);
  QDockWidget &tool_settings = create_dock(
      UIPanelId::ToolSettings, *tool_settings_panel_,
      Qt::RightDockWidgetArea);
  QDockWidget &active_material = create_dock(
      UIPanelId::ActiveMaterial, *active_material_panel_,
      Qt::RightDockWidgetArea);
  main_window_->splitDockWidget(&inspector, &layers, Qt::Vertical);
  main_window_->splitDockWidget(&inspector, &active_material, Qt::Vertical);
  main_window_->tabifyDockWidget(&inspector, &tool_settings);
  inspector.raise();
  const QtUiScaleMetrics &metrics = ui_scale_controller_->metrics();
  main_window_->resizeDocks({&outliner, &inspector},
                            {metrics.px(340), metrics.px(340)},
                            Qt::Horizontal);
  main_window_->resizeDocks({&inspector, &active_material, &layers},
                            {metrics.px(320), metrics.px(320),
                             metrics.px(180)},
                            Qt::Vertical);
  main_window_->resizeDocks({&console}, {metrics.px(220)}, Qt::Vertical);
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
  install_palette_aware_dock_title_presentation(*dock);
  apply_dock_content_minimum_width(content,
                                   ui_scale_controller_->metrics());
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
  schedule_retained_message_box_prewarm();
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

  for (UIPanelId id : kUiDockPanelIds) {
    const QDockWidget *dock = panels_.dock(id);
    if (dock != nullptr && dock->isVisible() && !geometry_ready(dock)) {
      return false;
    }
  }
  for (const QToolBar *toolbar :
       {static_cast<const QToolBar *>(selection_toolbar_),
        static_cast<const QToolBar *>(snapping_toolbar_),
        static_cast<const QToolBar *>(viewport_display_toolbar_),
        static_cast<const QToolBar *>(texture_toolbar_),
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
  for (UIPanelId id : kUiDockPanelIds) {
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
  mix_toolbar(texture_toolbar_);
  mix_toolbar(tool_toolbar_);
  return signature;
}

void UISystem::save_auxiliary_window_state() {
  if (settings_paths_.qt_auxiliary_window_state_file.empty()) {
    return;
  }
  if (auxiliary_window_save_timer_ != nullptr &&
      auxiliary_window_save_timer_->isActive()) {
    auxiliary_window_save_timer_->stop();
  }
  if (settings_dialog_ != nullptr) {
    auxiliary_window_state_.settings_dialog_geometry =
        settings_dialog_->saveGeometry();
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

void UISystem::bind_project_workspace_events(QuaderEditorApi &editor) {
  if (project_workspace_events_editor_ == &editor) {
    return;
  }
  clear_project_workspace_events();
  project_workspace_refresh_signal_ =
      std::make_shared<std::atomic_bool>(true);
  material_presentation_event_generation_signal_ =
      std::make_shared<std::atomic_uint64_t>(1u);
  material_presentation_observed_event_generation_ = 0;
  project_workspace_events_editor_ = &editor;
  constexpr std::array<std::string_view, 12> kWorkspaceEvents{
      quader_event::kProjectOpened,
      quader_event::kProjectClosed,
      quader_event::kProjectAssetsChanged,
      quader_event::kActiveProjectMaterialChanged,
      quader_event::kSceneOpened,
      quader_event::kActiveSceneChanged,
      quader_event::kSceneDirtyChanged,
      quader_event::kSceneSaved,
      quader_event::kStandardMaterialOpened,
      quader_event::kStandardMaterialDirtyChanged,
      quader_event::kStandardMaterialSaved,
      quader_event::kStandardMaterialClosed,
  };
  constexpr std::array<std::string_view, 7> kMaterialPresentationEvents{
      quader_event::kProjectOpened,
      quader_event::kProjectClosed,
      quader_event::kProjectAssetsChanged,
      quader_event::kSceneOpened,
      quader_event::kActiveSceneChanged,
      quader_event::kStandardMaterialSaved,
      quader_event::kSceneClosed,
  };
  project_workspace_event_subscriptions_.reserve(kWorkspaceEvents.size() +
                                                  1u);
  const std::shared_ptr<std::atomic_bool> refresh_signal =
      project_workspace_refresh_signal_;
  const std::shared_ptr<std::atomic_uint64_t> material_signal =
      material_presentation_event_generation_signal_;
  for (const std::string_view event_name : kWorkspaceEvents) {
    const bool affects_material_presentation =
        std::ranges::find(kMaterialPresentationEvents, event_name) !=
        kMaterialPresentationEvents.end();
    QuaderEventSubscription subscription =
        editor.events().subscribe(
            event_name,
            [refresh_signal, material_signal,
             affects_material_presentation](const QuaderEvent &) {
              refresh_signal->store(true, std::memory_order_relaxed);
              if (affects_material_presentation) {
                material_signal->fetch_add(1u,
                                           std::memory_order_release);
              }
            });
    if (subscription.valid()) {
      project_workspace_event_subscriptions_.push_back(subscription);
    }
  }
  QuaderEventSubscription scene_closed =
      editor.events().subscribe(
          quader_event::kSceneClosed,
          [refresh_signal, material_signal](const QuaderEvent &) {
            refresh_signal->store(true, std::memory_order_relaxed);
            material_signal->fetch_add(1u, std::memory_order_release);
          });
  if (scene_closed.valid()) {
    project_workspace_event_subscriptions_.push_back(scene_closed);
  }
  project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
}

void UISystem::clear_project_workspace_events() {
  if (project_workspace_events_editor_ != nullptr) {
    for (const QuaderEventSubscription subscription :
         project_workspace_event_subscriptions_) {
      (void)project_workspace_events_editor_->events().unsubscribe(
          subscription);
    }
  }
  project_workspace_event_subscriptions_.clear();
  project_workspace_events_editor_ = nullptr;
  project_workspace_refresh_signal_ =
      std::make_shared<std::atomic_bool>(false);
  material_presentation_event_generation_signal_ =
      std::make_shared<std::atomic_uint64_t>(0u);
  material_presentation_observed_event_generation_ = 0;
}

void UISystem::schedule_auxiliary_window_state_save() {
  if (auxiliary_window_save_timer_ != nullptr) {
    auxiliary_window_save_timer_->start();
  }
}

void UISystem::refresh_status_bar() {
  QDR_PROFILE_SCOPE("UI status bar refresh");
  if (status_message_ == nullptr || editor_ == nullptr || modeling_ == nullptr) {
    return;
  }
  if (operation_input_hints_ != nullptr) {
    operation_input_hints_->refresh(editor_->tools(), editor_->commands(), localization_);
  }
  const auto sync_message_visibility = [this]() {
    const bool visible = !status_message_->text().isEmpty() &&
        (operation_input_hints_ == nullptr || operation_input_hints_->hints().empty());
    if (status_message_->isHidden() == visible) {
      status_message_->setVisible(visible);
    }
  };
  sync_message_visibility();
  const ModelingLiveStatus live_status = [&]() {
    QDR_PROFILE_SCOPE("UI status bar query live status");
    return modeling_->live_status();
  }();
  const std::uint64_t command_revision = [&]() {
    QDR_PROFILE_SCOPE("UI status bar query command status revision");
    return editor_->commands().last_status_revision();
  }();
  const std::uint8_t live_kind =
      static_cast<std::uint8_t>(live_status.kind);
  const StatusPresentationStamp stamp{
      .command_revision = command_revision,
      .live_count = live_status.count,
      .live_kind = live_kind,
      .localization_revision = localization_.revision(),
  };
  if (!status_presenter_.needs_refresh(stamp)) {
    return;
  }

  {
    QDR_PROFILE_SCOPE("UI status bar present message");
    const QString message = QString::fromStdString(status_bar_message(
        live_status, localization_, editor_->commands().last_status()));
    if (status_message_->text() != message) {
      status_message_->setText(message);
      sync_message_visibility();
    }
  }
  status_presenter_.mark_presented(stamp);
}

void UISystem::present_lifecycle_result(const QuaderApiStatus &result) {
  QDR_PROFILE_SCOPE("UI present lifecycle result");
  if (status_message_ != nullptr && !result.message.empty()) {
    status_message_->setText(QString::fromStdString(result.message));
    status_message_->show();
    status_presenter_.invalidate();
  }

  const bool restored_scene_warning =
      result.ok() && result.message.find("restored scenes were skipped") !=
                         std::string::npos;
  if (result.ok() && !restored_scene_warning) {
    return;
  }

  if (lifecycle_message_box_ == nullptr) {
    return;
  }
  (void)lifecycle_message_box_->present(QtRetainedMessageBoxPresentation{
      .icon = QMessageBox::Warning,
      .object_name =
          restored_scene_warning
              ? QStringLiteral("project_open_warning_message_box")
              : QStringLiteral("lifecycle_error_message_box"),
      .title = QString::fromStdString(localization_.text(
          restored_scene_warning ? "ui.project.open_warning.title"
                                 : "ui.lifecycle.error.title",
          restored_scene_warning ? "Project Opened with Warnings"
                                 : "Quader Operation Failed")),
      .text = QString::fromStdString(
          result.message.empty()
              ? localization_.text("ui.status.command_failed",
                                   "Command failed.")
              : result.message),
      .buttons =
          {{
              .button = QtRetainedMessageBoxButton::Primary,
              .object_name = QStringLiteral("lifecycleMessageBox.ok"),
              .label = QString::fromStdString(
                  localization_.text("ui.action.ok", "OK")),
              .role = QMessageBox::AcceptRole,
          }},
      .default_button = QtRetainedMessageBoxButton::Primary,
      .escape_button = QtRetainedMessageBoxButton::Primary,
      .close_button = QtRetainedMessageBoxButton::Primary,
  });
}

void UISystem::present_standard_material_error(std::string_view message) {
  if (message.empty()) {
    return;
  }
  if (standard_material_error_message_box_ == nullptr) {
    return;
  }
  (void)standard_material_error_message_box_->present(
      QtRetainedMessageBoxPresentation{
          .icon = QMessageBox::Warning,
          .object_name =
              QStringLiteral("standard_material_error_message_box"),
          .title = QString::fromStdString(localization_.text(
              "ui.standard_material_editor.error_title",
              "Standard Material Operation Failed")),
          .text = QString::fromUtf8(
              message.data(), static_cast<qsizetype>(message.size())),
          .buttons =
              {{
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name =
                      QStringLiteral("standardMaterialErrorMessageBox.ok"),
                  .label = QString::fromStdString(
                      localization_.text("ui.action.ok", "OK")),
                  .role = QMessageBox::AcceptRole,
              }},
          .default_button = QtRetainedMessageBoxButton::Primary,
          .escape_button = QtRetainedMessageBoxButton::Primary,
          .close_button = QtRetainedMessageBoxButton::Primary,
      });
}

void UISystem::refresh_standard_material_editor() {
  if (standard_material_editor_window_ == nullptr) {
    return;
  }
  const StandardMaterialEditorPresentationStamp empty_stamp{};
  if (editor_ == nullptr || !editor_->project().has_project()) {
    if (!standard_material_editor_presenter_.needs_refresh(empty_stamp)) {
      return;
    }
    standard_material_preview_image_cache_.clear();
    standard_material_editor_window_->clear_document();
    standard_material_editor_window_->hide();
    standard_material_editor_presenter_.mark_presented(empty_stamp);
    return;
  }

  const QuaderApiResult<QuaderStandardMaterialDocumentRevision> revision =
      editor_->standard_material_documents().current_revision();
  if (!revision.ok) {
    if (!standard_material_editor_presenter_.needs_refresh(empty_stamp)) {
      return;
    }
    standard_material_preview_image_cache_.clear();
    if (standard_material_editor_window_->has_document()) {
      standard_material_editor_window_->clear_document();
      standard_material_editor_window_->hide();
    }
    standard_material_editor_presenter_.mark_presented(empty_stamp);
    return;
  }

  const QuaderStandardMaterialDocumentRevision &source_revision =
      revision.value();
  const StandardMaterialEditorPresentationStamp stamp{
      .session_id = source_revision.session_id,
      .asset_id = source_revision.id,
      .draft_revision = source_revision.draft_revision,
      .clean_revision = source_revision.clean_revision,
      .dirty = source_revision.dirty,
      .has_document = true,
  };
  if (!standard_material_editor_presenter_.needs_refresh(stamp)) {
    return;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> current =
      editor_->standard_material_documents().current();
  if (!current.ok) {
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
  if (material_preview_surface_ != nullptr) {
    render_snapshot = standard_material_preview_render_snapshot(
        source, preview.ok() ? &preview.value() : nullptr);
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

    }
  }
  if (material_preview_surface_ != nullptr && render_snapshot.has_value()) {
    material_preview_surface_->set_material(std::move(*render_snapshot));
  }
  standard_material_editor_window_->set_document(std::move(document));
  standard_material_editor_presenter_.mark_presented(stamp);
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
    if (standard_material_unsaved_confirmation_ == nullptr) {
      return false;
    }
    const QtUnsavedConfirmationDecision decision =
        standard_material_unsaved_confirmation_->present(
            QtUnsavedConfirmationPresentation{
                .object_name = QStringLiteral(
                    "standard_material_unsaved_confirmation"),
                .title = QString::fromStdString(localization_.text(
                    "ui.standard_material_editor.unsaved_title",
                    "Unsaved Standard Material")),
                .message = QString::fromStdString(message),
                .save_label = QString::fromStdString(localization_.text(
                    "ui.standard_material_editor.unsaved_save", "Save")),
                .discard_label = QString::fromStdString(localization_.text(
                    "ui.standard_material_editor.unsaved_discard",
                    "Discard")),
                .cancel_label = QString::fromStdString(localization_.text(
                    "ui.standard_material_editor.unsaved_cancel", "Cancel")),
            });
    if (decision == QtUnsavedConfirmationDecision::Save) {
      if (!save_standard_material(false)) {
        return false;
      }
    } else if (decision == QtUnsavedConfirmationDecision::Discard) {
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
  const QuaderApiVoidResult result =
      editor_->standard_material_documents().set_scalar(
          domain_material_scalar(scalar), static_cast<float>(value));
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
  const QuaderApiVoidResult result =
      editor_->standard_material_documents().set_texture_transform_component(
          domain_texture_transform_component(component),
          static_cast<float>(value));
  present_lifecycle_result(result);
  refresh_standard_material_editor();
}

void UISystem::refresh_localized_chrome() {
  QDR_PROFILE_SCOPE("UI refresh localized chrome");
  toolbar_owner_presenter_.invalidate();
  toolbar_tool_presenter_.invalidate();
  toolbar_transform_presenter_.invalidate();
  const auto text = [this](std::string_view key, std::string_view fallback) {
    return QString::fromStdString(localization_.text(key, fallback));
  };
  const auto update_title = [this](UIPanelId id) {
    if (QDockWidget *dock = panels_.dock(id)) {
      dock->setWindowTitle(QString::fromStdString(localization_.text(
          ui_panel_localization_key(id), ui_panel_key(id))));
    }
  };
  if (command_palette_ != nullptr) {
    command_palette_->refresh_localization();
  }
  update_title(UIPanelId::Outliner);
  update_title(UIPanelId::Inspector);
  update_title(UIPanelId::Layers);
  update_title(UIPanelId::ToolSettings);
  update_title(UIPanelId::ActiveMaterial);
  update_title(UIPanelId::Console);
  if (active_material_panel_ != nullptr) {
    active_material_panel_->set_labels(QtActiveMaterialPanelLabels{
        .preview_title = text("ui.active_material.preview",
                              "Active material preview"),
        .empty = text("ui.active_material.empty", "No active material"),
        .browse = text("ui.active_material.browse", "Browse"),
    });
  }
  if (selection_toolbar_ != nullptr) {
    selection_toolbar_->set_localization(localization_);
  }
  if (snapping_toolbar_ != nullptr) {
    snapping_toolbar_->set_localization(localization_);
  }
  if (viewport_display_toolbar_ != nullptr) {
    viewport_display_toolbar_->set_localization(localization_);
  }
  if (texture_toolbar_ != nullptr) {
    texture_toolbar_->set_localization(localization_);
  }
  if (tool_toolbar_ != nullptr) {
    tool_toolbar_->set_localization(localization_);
  }
  if (project_home_page_ != nullptr) {
    project_home_page_->set_labels(QtProjectHomeLabels{
        .new_project = text("ui.home.new_project", "New"),
        .open_project = text("ui.home.open_project", "Open"),
        .recent_projects =
            text("ui.home.recent_projects", "Recent"),
        .recent_name_column = text("ui.home.recent_name", "Name"),
        .recent_last_modified_column = text(
            "ui.home.recent_last_modified", "Last modified"),
        .recent_size_column = text("ui.home.recent_size", "Size"),
        .recent_actions_column =
            text("ui.home.recent_actions", "Actions"),
        .no_recent_projects =
            text("ui.home.no_recent_projects", "No recent projects"),
        .unavailable_project =
            text("ui.home.unavailable_project", "Project is unavailable"),
        .open_recent_project =
            text("ui.home.recent_action.open", "Open"),
        .open_recent_project_location = text(
            "ui.home.recent_action.open_file_location",
            "Open file location"),
        .copy_recent_project_path =
            text("ui.home.recent_action.copy_path", "Copy path"),
        .remove_recent_project = text(
            "ui.home.recent_action.remove", "Remove from recent"),
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
  if (material_browser_window_ != nullptr ||
      material_picker_window_ != nullptr) {
    const QtMaterialBrowserLabels labels{
        .window_title =
            text("ui.material_browser.title", "Material Browser"),
        .search_placeholder = text("ui.material_browser.search",
                                   "Search materials"),
        .grid = text("ui.material_browser.grid", "Grid"),
        .list = text("ui.material_browser.list", "List"),
        .tile_size =
            text("ui.material_browser.tile_size", "Preview size"),
        .tile_size_value = text("ui.material_browser.tile_size_value",
                                "Preview size: %1 px"),
        .file = text("ui.material_browser.file", "File"),
        .import_textures = text("ui.material_browser.file.import",
                                "Import…"),
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
        .accept = text("ui.material_browser.accept", "Accept"),
        .picker_window_title = text(
            "ui.material_browser.picker_title",
            "Replace Material Browser"),
    };
    if (material_browser_window_ != nullptr) {
      material_browser_window_->set_labels(labels);
    }
    if (material_picker_window_ != nullptr) {
      material_picker_window_->set_labels(labels);
    }
    material_browser_catalog_signature_ = 0u;
    invalidate_material_presentation_cache();
  }
  if (material_find_replace_dialog_ != nullptr) {
    material_find_replace_dialog_->set_labels(
        QtMaterialFindReplaceLabels{
            .window_title = text("ui.material_find_replace.title",
                                 "Find/Replace Materials"),
            .find = text("ui.material_find_replace.find", "Find"),
            .replace = text("ui.material_find_replace.replace", "Replace"),
            .browse = text("ui.material_find_replace.browse", "Browse…"),
            .empty = text("ui.material_find_replace.empty",
                          "No material selected"),
            .replace_action = text(
                "ui.material_find_replace.action.replace", "Replace"),
            .cancel_action = text(
                "ui.material_find_replace.action.cancel", "Cancel"),
        });
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

void UISystem::set_session_settings_changed_callback(
    std::function<void(const SessionPreferences &)> callback) {
  session_settings_changed_ = std::move(callback);
}

void UISystem::handle_settings_dialog_change(const QtSettingsChange &change) {
  switch (change.kind) {
  case QtSettingsChangeKind::ViewportPreview:
    return;
  case QtSettingsChangeKind::ViewportCommit:
    apply_viewport_settings_to_workspace();
    (void)save_viewport_settings();
    break;
  case QtSettingsChangeKind::InterfacePreview:
    if (change.interface_scale.has_value()) {
      (void)apply_interface_scale(*change.interface_scale);
    }
    return;
  case QtSettingsChangeKind::InterfaceCommit:
    if (change.interface_scale.has_value()) {
      (void)apply_interface_scale(*change.interface_scale);
    }
    break;
  case QtSettingsChangeKind::RenderingPreview:
  case QtSettingsChangeKind::RenderingCommit:
    if (render_system_ != nullptr && change.vsync_enabled.has_value()) {
      render_system_->set_vsync_enabled(*change.vsync_enabled);
    }
    return;
  case QtSettingsChangeKind::KeybindingsCommit:
    if (editor_ != nullptr) {
      const QuaderSelectionSummary selection = editor_->selection().summary();
      if (selection_toolbar_ != nullptr) {
        selection_toolbar_->synchronize(*editor_, selection.mode, true);
      }
      if (tool_toolbar_ != nullptr) {
        tool_toolbar_->synchronize(*editor_, selection, true);
      }
      if (snapping_toolbar_ != nullptr) {
        snapping_toolbar_->synchronize(*editor_, true);
      }
      if (viewport_display_toolbar_ != nullptr) {
        viewport_display_toolbar_->synchronize(*editor_, true);
      }
      if (texture_toolbar_ != nullptr) {
        texture_toolbar_->synchronize(*editor_, true);
      }
      if (viewport_panel_ != nullptr) {
        viewport_panel_->refresh_keybinding_presentation(*editor_);
      }
    }
    break;
  case QtSettingsChangeKind::SessionCommit:
    if (change.session_settings.has_value() && session_settings_changed_) {
      session_settings_changed_(*change.session_settings);
    }
    return;
  }
  if (main_menu_ != nullptr) {
    main_menu_->refresh();
  }
}

bool UISystem::apply_interface_scale(float factor) {
  if (ui_scale_controller_ == nullptr ||
      !ui_scale_controller_->apply_factor(factor)) {
    return false;
  }
  apply_ui_scale_to_retained_surfaces(ui_scale_controller_->metrics());
  return true;
}

void UISystem::apply_ui_scale_to_retained_surfaces(
    const QtUiScaleMetrics &metrics) {
  if (operation_input_hints_ != nullptr) {
    operation_input_hints_->apply_ui_scale(metrics);
  }
  if (selection_toolbar_ != nullptr) {
    selection_toolbar_->apply_ui_scale(metrics);
  }
  if (snapping_toolbar_ != nullptr) {
    snapping_toolbar_->apply_ui_scale(metrics);
  }
  if (viewport_display_toolbar_ != nullptr) {
    viewport_display_toolbar_->apply_ui_scale(metrics);
  }
  if (texture_toolbar_ != nullptr) {
    texture_toolbar_->apply_ui_scale(metrics);
  }
  if (tool_toolbar_ != nullptr) {
    tool_toolbar_->apply_ui_scale(metrics);
  }
  if (project_home_page_ != nullptr) {
    project_home_page_->apply_ui_scale(metrics);
  }
  if (scene_workspace_ != nullptr) {
    scene_workspace_->apply_ui_scale(metrics);
  }
  if (viewport_panel_ != nullptr) {
    viewport_panel_->apply_ui_scale(metrics);
  }
  if (inspector_panel_ != nullptr) {
    inspector_panel_->apply_ui_scale(metrics);
  }
  if (layer_panel_ != nullptr) {
    layer_panel_->apply_ui_scale(metrics);
  }
  if (tool_settings_panel_ != nullptr) {
    tool_settings_panel_->apply_ui_scale(metrics);
  }
  if (active_material_panel_ != nullptr) {
    active_material_panel_->apply_ui_scale(metrics);
  }
  if (settings_dialog_ != nullptr) {
    settings_dialog_->apply_ui_scale(metrics);
  }
  if (material_browser_window_ != nullptr) {
    material_browser_window_->apply_ui_scale(metrics);
  }
  if (material_picker_window_ != nullptr) {
    material_picker_window_->apply_ui_scale(metrics);
  }
  if (material_find_replace_dialog_ != nullptr) {
    material_find_replace_dialog_->apply_ui_scale(metrics);
  }
  if (standard_material_editor_window_ != nullptr) {
    standard_material_editor_window_->apply_ui_scale(metrics);
  }
  for (QWidget *widget : QApplication::allWidgets()) {
    if (auto *message_box = dynamic_cast<QtRetainedMessageBox *>(widget)) {
      message_box->apply_ui_scale(metrics);
    }
  }
  for (UIPanelId id : kUiDockPanelIds) {
    if (QDockWidget *dock = panels_.dock(id)) {
      if (QWidget *content = dock->widget()) {
        apply_dock_content_minimum_width(*content, metrics);
      }
    }
  }
  if (main_window_ != nullptr) {
    const QList<QDockWidget *> docks =
        main_window_->findChildren<QDockWidget *>();
    for (QDockWidget *dock : docks) {
      if (dock != nullptr) {
        apply_ui_scale_to_palette_aware_dock_title_presentation(*dock,
                                                                 metrics);
      }
    }
    if (main_window_->layout() != nullptr) {
      main_window_->layout()->invalidate();
    }
    main_window_->updateGeometry();
    main_window_->update();
  }
}

void UISystem::apply_viewport_settings_to_workspace() {
  if (editor_ == nullptr) {
    return;
  }
  ViewportWorkspaceService &workspace =
      QuaderViewportApiRuntimeAccess::workspace(editor_->viewports());
  (void)workspace.set_default_fly_speed(viewport_settings_.float_value(
      ViewportFloatSettingId::DefaultFlyNavigationSpeed));
}

void UISystem::synchronize_settings_surfaces(bool refresh_dialog) {
  QDR_PROFILE_SCOPE("UI synchronize settings surfaces");
  toolbar_owner_presenter_.invalidate();
  toolbar_tool_presenter_.invalidate();
  toolbar_transform_presenter_.invalidate();
  if (snapping_toolbar_ != nullptr && editor_ != nullptr) {
    snapping_toolbar_->synchronize(*editor_, true);
  }
  if (viewport_display_toolbar_ != nullptr && editor_ != nullptr) {
    viewport_display_toolbar_->synchronize(*editor_, true);
  }
  if (texture_toolbar_ != nullptr && editor_ != nullptr) {
    texture_toolbar_->synchronize(*editor_, true);
  }
  if (main_menu_ != nullptr) {
    main_menu_->refresh();
  }
  if (refresh_dialog && settings_dialog_ != nullptr &&
      settings_dialog_->isVisible()) {
    settings_dialog_->refresh();
  }
}

void UISystem::show_autosave_recovery() {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return;
  }
  const auto directory = editor_->scene_documents().autosave_directory();
  if (!directory.ok()) {
    present_lifecycle_result(directory);
    return;
  }
  // Native dialogs run a nested message loop. Resolve through the current API
  // after it closes, and reject a selection if that loop replaced the project.
  const auto project_root = editor_->project().root();
  const auto binding_generation = runtime_binding_generation_;
  PlatformFileDialogOptions options;
  options.owner_window_handle = native_window_handle();
  options.initial_directory = directory.value();
  const auto selected = platform_open_scene_recovery_file_dialog(options);
  if (!selected || editor_ == nullptr ||
      binding_generation != runtime_binding_generation_ ||
      !editor_->project().has_project() ||
      editor_->project().root() != project_root) {
    return;
  }
  present_lifecycle_result(
      editor_->scene_documents().recover_autosave_native_path(*selected));
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
  refresh_project_workspace();
  material_browser_window_->raise();
  material_browser_window_->activateWindow();
}

void UISystem::show_material_find_replace() {
  if (material_find_replace_dialog_ == nullptr || home_visible_ ||
      editor_ == nullptr || document_ == nullptr || modeling_ == nullptr ||
      !editor_->project().has_project() ||
      !editor_->scene_documents().has_active_scene()) {
    return;
  }
  material_find_replace_scope_ = QuaderMaterialUsageScope::ActiveScene;
  material_find_replace_dialog_->clear_materials();
  refresh_material_presentation_cache();
  const QuaderProjectMetadata project = editor_->project().metadata();
  material_find_replace_project_identity_ =
      project.uuid + '\n' +
      editor_->project().manifest_path().generic_string();
  const std::optional<SceneSessionId> scene_id =
      editor_->scene_documents().active_scene_id();
  material_find_replace_scene_id_ =
      scene_id.has_value() ? scene_id->packed() : 0u;
  material_find_replace_scene_selection_revision_ =
      document_->selection_revision();
  material_find_replace_modeling_selection_revision_ =
      modeling_->selection_revision();
  material_find_replace_modeling_object_map_revision_ =
      modeling_->modeling_object_map_revision();
  material_find_replace_presentation_revision_ =
      material_presentation_revision_;
  material_find_replace_dialog_->open();
  material_find_replace_dialog_->raise();
  material_find_replace_dialog_->activateWindow();
}

void UISystem::show_material_find_replace(
    QuaderMaterialReference source) {
  if (!source.valid()) {
    return;
  }
  show_material_find_replace();
  if (material_find_replace_dialog_ == nullptr ||
      !material_find_replace_dialog_->isVisible()) {
    return;
  }
  material_find_replace_scope_ = QuaderMaterialUsageScope::Selection;
  material_find_replace_scene_selection_revision_ =
      document_ != nullptr ? document_->selection_revision() : 0u;
  material_find_replace_modeling_selection_revision_ =
      modeling_ != nullptr ? modeling_->selection_revision() : 0u;
  material_find_replace_modeling_object_map_revision_ =
      modeling_ != nullptr ? modeling_->modeling_object_map_revision() : 0u;
  material_find_replace_dialog_->set_find_material(
      material_presentation_for(source, true));
}

void UISystem::show_material_picker(bool replacement_column) {
  validate_material_find_replace_context();
  if (material_picker_window_ == nullptr ||
      material_find_replace_dialog_ == nullptr || editor_ == nullptr ||
      !material_find_replace_dialog_->isVisible()) {
    return;
  }
  refresh_material_presentation_cache();
  material_picker_replacement_column_ = replacement_column;
  std::vector<QtMaterialPresentationItem> items;
  if (replacement_column) {
    items = replacement_material_presentations();
  } else {
    const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
        editor_->materials().used_materials(material_find_replace_scope_);
    if (used.succeeded()) {
      items = material_presentations_for(used.value(), true);
    }
  }
  const QtMaterialFindReplaceColumn column =
      replacement_column ? QtMaterialFindReplaceColumn::Replace
                         : QtMaterialFindReplaceColumn::Find;
  QuaderMaterialReference selected;
  const std::optional<QtMaterialPresentationItem> &current =
      material_find_replace_dialog_->material(column);
  if (current.has_value()) {
    selected = current->reference;
  }
  material_picker_window_->set_materials(std::move(items), selected);
  material_picker_presentation_revision_ =
      material_presentation_revision_;
  material_picker_window_->show();
  material_picker_window_->raise();
  material_picker_window_->activateWindow();
}

void UISystem::close_material_find_replace() {
  if (material_picker_window_ != nullptr) {
    material_picker_window_->hide();
    material_picker_window_->clear_materials();
  }
  if (material_find_replace_dialog_ != nullptr) {
    if (material_find_replace_dialog_->isVisible()) {
      material_find_replace_dialog_->reject();
    } else {
      material_find_replace_dialog_->clear_materials();
    }
  }
  material_find_replace_project_identity_.clear();
  material_find_replace_scene_id_ = 0;
  material_find_replace_scene_selection_revision_ = 0;
  material_find_replace_modeling_selection_revision_ = 0;
  material_find_replace_modeling_object_map_revision_ = 0;
  material_find_replace_presentation_revision_ = 0;
  material_picker_presentation_revision_ = 0;
}

void UISystem::validate_material_find_replace_context() {
  if ((material_find_replace_dialog_ == nullptr ||
       !material_find_replace_dialog_->isVisible()) &&
      (material_picker_window_ == nullptr ||
       !material_picker_window_->isVisible())) {
    return;
  }
  if (editor_ == nullptr || document_ == nullptr || modeling_ == nullptr ||
      !editor_->project().has_project()) {
    close_material_find_replace();
    return;
  }
  const QuaderProjectMetadata project = editor_->project().metadata();
  const std::string project_identity =
      project.uuid + '\n' +
      editor_->project().manifest_path().generic_string();
  const std::optional<SceneSessionId> scene_id =
      editor_->scene_documents().active_scene_id();
  if (!scene_id.has_value() ||
      project_identity != material_find_replace_project_identity_ ||
      scene_id->packed() != material_find_replace_scene_id_ ||
      (material_find_replace_scope_ == QuaderMaterialUsageScope::Selection &&
       (document_->selection_revision() !=
            material_find_replace_scene_selection_revision_ ||
        modeling_->selection_revision() !=
            material_find_replace_modeling_selection_revision_ ||
        modeling_->modeling_object_map_revision() !=
            material_find_replace_modeling_object_map_revision_))) {
    close_material_find_replace();
  }
}

void UISystem::import_material_textures() {
  if (editor_ == nullptr || material_browser_window_ == nullptr ||
      !editor_->project().has_project()) {
    return;
  }

  PlatformFileDialogOptions options;
  const WId browser_window_id = material_browser_window_->winId();
  options.owner_window_handle = reinterpret_cast<void *>(
      static_cast<std::uintptr_t>(browser_window_id));
  options.initial_directory = editor_->project().root() / "Textures";
  const std::optional<std::vector<std::filesystem::path>> selected =
      platform_open_texture_files_dialog(options);
  if (!selected.has_value() || selected->empty()) {
    return;
  }

  const QuaderApiResult<QuaderProjectMaterialImportReport> result =
      editor_->project_assets().import_textures_as_materials(*selected);
  const std::size_t imported_count = result.value().imported.size();
  const std::size_t failed_count = result.value().failed.size();

  const QString status = [&]() {
    if (!result.ok) {
      return QString::fromStdString(localization_.text(
          "ui.material_browser.import.status.failed",
          "Texture import failed."));
    }
    if (failed_count == 0u) {
      return QString::fromStdString(localization_.text(
          "ui.material_browser.import.status.success",
                 "Materials imported: %1."))
          .arg(static_cast<qulonglong>(imported_count));
    }
    return QString::fromStdString(localization_.text(
               "ui.material_browser.import.status.with_failures",
               "Materials imported: %1. Failed: %2."))
        .arg(static_cast<qulonglong>(imported_count))
        .arg(static_cast<qulonglong>(failed_count));
  }();
  if (main_window_ != nullptr && main_window_->statusBar() != nullptr) {
    main_window_->statusBar()->showMessage(status, 5000);
  }

  if (result.ok && failed_count == 0u) {
    return;
  }
  if (material_browser_import_message_box_ == nullptr) {
    return;
  }

  constexpr std::size_t kVisibleFailureLimit = 10u;
  QStringList failure_lines;
  const std::size_t visible_failure_count =
      std::min(failed_count, kVisibleFailureLimit);
  failure_lines.reserve(static_cast<qsizetype>(visible_failure_count + 1u));
  for (std::size_t index = 0u; index < visible_failure_count; ++index) {
    const QuaderProjectMaterialImportFailure &failure =
        result.value().failed[index];
    QString file_name =
        QString::fromStdWString(failure.source_path.filename().wstring());
    if (file_name.isEmpty()) {
      file_name = QString::fromStdWString(failure.source_path.wstring());
    }
    failure_lines.push_back(
        QStringLiteral("%1: %2")
            .arg(file_name, QString::fromStdString(failure.error_message)));
  }
  if (failed_count > visible_failure_count) {
    failure_lines.push_back(
        QString::fromStdString(localization_.text(
            "ui.material_browser.import.warning.remaining",
            "...and %1 more."))
            .arg(static_cast<qulonglong>(failed_count -
                                        visible_failure_count)));
  }
  if (!result.ok && !result.message.empty()) {
    failure_lines.push_back(QString::fromStdString(result.message));
  }

  (void)material_browser_import_message_box_->present(
      QtRetainedMessageBoxPresentation{
          .icon = QMessageBox::Warning,
          .object_name =
              QStringLiteral("material_browser_import_warning_message_box"),
          .title = QString::fromStdString(localization_.text(
              "ui.material_browser.import.warning.title",
              "Texture Import Warnings")),
          .text = status,
          .informative_text = failure_lines.join(QLatin1Char('\n')),
          .buttons =
              {{
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name = QStringLiteral(
                      "materialBrowserImportWarningMessageBox.ok"),
                  .label = QString::fromStdString(
                      localization_.text("ui.action.ok", "OK")),
                  .role = QMessageBox::AcceptRole,
              }},
          .default_button = QtRetainedMessageBoxButton::Primary,
          .escape_button = QtRetainedMessageBoxButton::Primary,
          .close_button = QtRetainedMessageBoxButton::Primary,
      });
}

bool UISystem::panel_visible(UIPanelId id) const {
  return id == UIPanelId::Viewport ? true : panels_.is_visible(id);
}

bool UISystem::panel_presented(UIPanelId id) const {
  return panels_.is_presented(id);
}

void UISystem::set_panel_visible(UIPanelId id, bool visible) {
  (void)panels_.set_visible(id, visible);
}

} // namespace quader
