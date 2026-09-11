////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_events_api.hpp"
#include "api/quader_materials_api.hpp"
#include "api/quader_modeling_api.hpp"
#include "api/quader_project_assets_api.hpp"
#include "api/quader_standard_material_documents_api.hpp"
#include "core/frame_timing.hpp"
#include "core/localization_service.hpp"
#include "core/types.hpp"
#include "render/render_performance_stats.hpp"
#include "ui/qt_auxiliary_window_state.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_material_presentation.hpp"
#include "ui/qt_standard_material_preview_image_cache.hpp"
#include "ui/qt_startup_presentation.hpp"
#include "ui/retained_ui_presenter.hpp"
#include "ui/ui_panel_manager.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QDockWidget;
class QLabel;
class QMainWindow;
class QStackedWidget;
class QTimer;
class QWidget;

namespace quader {

class AssetRegistry;
class ConsolePanel;
class EditorSelectionService;
class InspectorPanel;
class LayerPanel;
class ModelingSessionService;
class NativeViewportToolCoordinator;
class OutlinerPanel;
class ViewportPointPlacementService;
class QtSettingsDialog;
class QtCommandEventFilter;
class QtCommandPalette;
class QtMaterialFindReplaceDialog;
class QtMaterialBrowserWindow;
class QtTextureInfoWindow;
class QtActiveMaterialPanel;
class QtMaterialPreviewSurface;
class QtRetainedMessageBox;
class QtStandardMaterialEditorWindow;
class QtToolSettingsPanel;
class QtOperationInputHints;
class QtStatusMessage;
class QtUnsavedConfirmationDialog;
class QtViewportPanel;
class QtProjectHomePage;
class QtSceneWorkspace;
class QtSelectionToolbar;
class QtSnappingToolbar;
class QtTextureToolbar;
class QtToolToolbar;
class QtViewportDisplayToolbar;
struct QtSettingsChange;
struct QtMaterialBrowserActionRequest;
class QtUiScaleApplicationController;
class QtUiScaleMetrics;
enum class QtStandardMaterialEditorFileAction : std::uint8_t;
enum class QtStandardMaterialScalar : std::uint8_t;
enum class QtStandardMaterialTextureAction : std::uint8_t;
enum class QtStandardMaterialTextureSlot : std::uint8_t;
enum class QtStandardMaterialTextureTransformComponent : std::uint8_t;
class QuaderEditorApi;
struct QuaderApiStatus;
class RenderOverlaySnapshotCache;
class RenderSystem;
class SceneDocument;
class SceneRenderBridge;
enum class QuaderDirtyProjectDecision;
enum class QuaderDirtySceneDecision;
struct QuaderSceneDocumentInfo;

/**
 * Owns Quader's retained Qt Widgets shell and all top-level presentation
 * surfaces. Domain state remains owned by editor, scene, viewport and render
 * services.
 */
class UISystem final {
public:
  UISystem();
  ~UISystem();

  UISystem(const UISystem &) = delete;
  UISystem &operator=(const UISystem &) = delete;

  bool initialize(RenderSystem &render_system,
                  const std::filesystem::path &executable_dir);
  void install_runtime_hooks(QuaderEditorApi &editor,
                             SceneDocument &document, AssetRegistry &assets,
                             ModelingSessionService &modeling,
                             EditorSelectionService &selection,
                             NativeViewportToolCoordinator &native_tools,
                             ViewportPointPlacementService &point_placement,
                             AssetId default_material);
  void set_default_material(AssetId default_material) {
    default_material_ = default_material;
  }
  [[nodiscard]] QuaderDirtyProjectDecision
  confirm_dirty_project_replacement(std::string_view action_id);
  [[nodiscard]] QuaderDirtySceneDecision
  confirm_dirty_scene(const QuaderSceneDocumentInfo &scene,
                      std::string_view action_id);
  [[nodiscard]] bool
  prepare_standard_material_project_transition(std::string_view action_id);
  void refresh_project_workspace(bool force = true);
  void show_home();
  void show_workspace();
  void show();
  void shutdown();

  void refresh(const FrameTiming &frame_timing,
               RenderFramePerformanceStats render_performance);
  void process_viewport_input();
  void prepare_viewports(SceneRenderBridge &render_bridge,
                         RenderOverlaySnapshotCache &snapshot_cache);
  [[nodiscard]] std::size_t render_viewports();
  [[nodiscard]] std::size_t render_surfaces(bool render_scene_viewports);
  [[nodiscard]] bool needs_continuous_rendering() const;

  [[nodiscard]] QMainWindow *main_window() const { return main_window_.get(); }
  [[nodiscard]] void *native_window_handle() const;
  [[nodiscard]] bool can_render_viewports() const;

  [[nodiscard]] bool save_settings();
  [[nodiscard]] bool save_viewport_settings();
  [[nodiscard]] const SessionPreferences &session_settings() const {
    return settings_.session_settings();
  }
  void set_session_settings_changed_callback(
      std::function<void(const SessionPreferences &)> callback);
  void save_window_state();

private:
  void create_docks();
  QDockWidget &create_dock(UIPanelId id, QWidget &content,
                           int default_area);
  void restore_window_state();
  void capture_workspace_window_state();
  void prepare_startup_presentation();
  void advance_startup_presentation();
  void finish_startup_presentation(bool forced_fallback);
  void schedule_retained_message_box_prewarm();
  [[nodiscard]] bool startup_layout_ready() const;
  [[nodiscard]] std::uint64_t startup_layout_geometry_signature(
      int native_client_width_pixels,
      int native_client_height_pixels) const;
  void save_auxiliary_window_state();
  void schedule_auxiliary_window_state_save();
  void abandon_scene_backed_panels() noexcept;
  void bind_scene_backed_panels();
  void clear_editor_runtime_hooks();
  void bind_project_workspace_events(QuaderEditorApi &editor);
  void clear_project_workspace_events();
  void refresh_status_bar();
  void refresh_status_selection();
  void present_status_notification(const QuaderApiStatus &result,
                                   int timeout_ms = 0);
  void present_lifecycle_result(const QuaderApiStatus &result);
  void present_standard_material_error(std::string_view message);
  void refresh_localized_chrome();
  void refresh_standard_material_editor();
  void open_standard_material(std::string_view stable_id);
  void open_standard_material_document(ProjectAssetId id);
  void open_standard_material_from_dialog();
  void new_standard_material();
  [[nodiscard]] bool save_standard_material(bool save_as);
  [[nodiscard]] bool resolve_standard_material_document(bool close_after);
  void handle_standard_material_file_action(
      QtStandardMaterialEditorFileAction action);
  void handle_standard_material_scalar(QtStandardMaterialScalar scalar,
                                       double value);
  void handle_standard_material_texture(
      QtStandardMaterialTextureSlot slot,
      QtStandardMaterialTextureAction action);
  void handle_standard_material_texture_transform(
      QtStandardMaterialTextureTransformComponent component, double value);
  void handle_settings_dialog_change(const QtSettingsChange &change);
  bool apply_interface_scale(float factor);
  void apply_ui_scale_to_retained_surfaces(const QtUiScaleMetrics &metrics);
  void apply_viewport_settings_to_workspace();
  void synchronize_settings_surfaces(bool refresh_dialog);
  void show_settings_dialog();
  void show_autosave_recovery();
  void show_material_browser();
  void refresh_material_browser_usage();
  void handle_material_browser_action(QtMaterialBrowserActionRequest request);
  void show_material_texture_info(ProjectAssetId material_id,
                                  bool activate = true);
  void refresh_material_texture_info();
  void delete_materials(std::vector<ProjectAssetId> material_ids);
  void show_material_find_replace();
  void show_material_find_replace(QuaderMaterialReference source);
  void show_material_picker(bool replacement_column);
  void close_material_find_replace();
  void invalidate_material_presentation_cache();
  void observe_material_presentation_events();
  void refresh_material_presentation_cache();
  void refresh_active_material_panel(QuaderMaterialReference active_reference);
  void synchronize_material_presentation_consumers();
  void refresh_material_picker_presentations();
  void revalidate_material_find_replace_presentations();
  void refresh_inspector_material_presentation();
  [[nodiscard]] std::vector<QtMaterialPresentationItem>
  material_presentations_for(
      const std::vector<QuaderMaterialReference> &references,
      bool include_missing) const;
  [[nodiscard]] std::optional<QtMaterialPresentationItem>
  material_presentation_for(QuaderMaterialReference reference,
                            bool include_missing) const;
  [[nodiscard]] std::vector<QtMaterialPresentationItem>
  replacement_material_presentations() const;
  void validate_material_find_replace_context();
  void import_material_textures(QString destination_folder);
  void delete_material_folder(QString folder);
  void finish_material_folder_operation(const QuaderApiStatus &result);
  [[nodiscard]] bool allow_main_window_close();
  [[nodiscard]] bool panel_visible(UIPanelId id) const;
  [[nodiscard]] bool panel_presented(UIPanelId id) const;
  void set_panel_visible(UIPanelId id, bool visible);

  std::unique_ptr<QtUiScaleApplicationController> ui_scale_controller_;
  std::unique_ptr<QMainWindow> main_window_;
  std::unique_ptr<QtMainMenu> main_menu_;
  std::unique_ptr<QtCommandEventFilter> command_event_filter_;
  QtCommandPalette *command_palette_ = nullptr;
  bool command_palette_open_pending_ = false;
  QStackedWidget *central_stack_ = nullptr;
  QtProjectHomePage *project_home_page_ = nullptr;
  QtSceneWorkspace *scene_workspace_ = nullptr;
  QtSelectionToolbar *selection_toolbar_ = nullptr;
  QtSnappingToolbar *snapping_toolbar_ = nullptr;
  QtViewportDisplayToolbar *viewport_display_toolbar_ = nullptr;
  QtTextureToolbar *texture_toolbar_ = nullptr;
  QtToolToolbar *tool_toolbar_ = nullptr;
  QtViewportPanel *viewport_panel_ = nullptr;
  OutlinerPanel *outliner_panel_ = nullptr;
  InspectorPanel *inspector_panel_ = nullptr;
  LayerPanel *layer_panel_ = nullptr;
  ConsolePanel *console_panel_ = nullptr;
  QtSettingsDialog *settings_dialog_ = nullptr;
  std::uint64_t runtime_binding_generation_ = 0;
  QtMaterialBrowserWindow *material_browser_window_ = nullptr;
  QtTextureInfoWindow *material_texture_info_window_ = nullptr;
  std::uint64_t material_texture_info_catalog_revision_ = 0u;
  QtActiveMaterialPanel *active_material_panel_ = nullptr;
  QtMaterialBrowserWindow *material_picker_window_ = nullptr;
  QtMaterialFindReplaceDialog *material_find_replace_dialog_ = nullptr;
  QtMaterialPreviewSurface *material_preview_surface_ = nullptr;
  QtStandardMaterialEditorWindow *standard_material_editor_window_ = nullptr;
  QtToolSettingsPanel *tool_settings_panel_ = nullptr;
  QtUnsavedConfirmationDialog *unsaved_confirmation_ = nullptr;
  QtUnsavedConfirmationDialog *standard_material_unsaved_confirmation_ =
      nullptr;
  QtRetainedMessageBox *lifecycle_message_box_ = nullptr;
  QtRetainedMessageBox *standard_material_error_message_box_ = nullptr;
  QtRetainedMessageBox *material_browser_import_message_box_ = nullptr;
  QtStatusMessage *status_message_ = nullptr;
  QtOperationInputHints *operation_input_hints_ = nullptr;
  QLabel *status_version_ = nullptr;
  QLabel *status_selection_ = nullptr;
  QTimer *auxiliary_window_save_timer_ = nullptr;
  UIPanelManager panels_;
  LocalizationService localization_;
  UISettings settings_;
  std::function<void(const SessionPreferences &)>
      session_settings_changed_;
  UISettingsPaths settings_paths_;
  QtAuxiliaryWindowState auxiliary_window_state_;
  ViewportSettings viewport_settings_;
  RenderSystem *render_system_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  SceneDocument *document_ = nullptr;
  AssetRegistry *assets_ = nullptr;
  ModelingSessionService *modeling_ = nullptr;
  EditorSelectionService *selection_ = nullptr;
  NativeViewportToolCoordinator *native_tools_ = nullptr;
  ViewportPointPlacementService *point_placement_ = nullptr;
  AssetId default_material_ = kInvalidAssetId;
  FrameTiming frame_timing_{};
  RenderFramePerformanceStats render_performance_{};
  QtStartupPresentationState startup_presentation_;
  std::chrono::steady_clock::time_point startup_presentation_started_{};
  bool startup_expected_maximized_ = false;
  bool retained_message_box_prewarm_scheduled_ = false;
  bool initialized_ = false;
  bool ui_settings_changed_ = false;
  std::string localized_locale_;
  std::uint64_t localized_revision_ = 0u;

  /** Captures revisions that determine whether the retained outliner must refresh. */
  struct OutlinerPresentationStamp final {
    std::uint64_t hierarchy_revision = 0u;
    std::uint64_t selection_revision = 0u;
    std::uint64_t instance_edit_revision = 0u;
    std::uint64_t localization_revision = 0u;
    friend bool operator==(const OutlinerPresentationStamp &,
                           const OutlinerPresentationStamp &) = default;
  };
  /** Captures revisions that determine whether the retained layers panel must refresh. */
  struct LayersPresentationStamp final {
    std::uint64_t layer_revision = 0u;
    std::uint64_t selection_revision = 0u;
    std::uint64_t localization_revision = 0u;
    friend bool operator==(const LayersPresentationStamp &,
                           const LayersPresentationStamp &) = default;
  };
  /** Captures revisions that determine whether the retained inspector must refresh. */
  struct InspectorPresentationStamp final {
    std::uint64_t scene_revision = 0u;
    std::uint64_t modeling_content_revision = 0u;
    std::uint64_t modeling_selection_revision = 0u;
    std::uint64_t material_presentation_revision = 0u;
    std::uint64_t localization_revision = 0u;
    friend bool operator==(const InspectorPresentationStamp &,
                           const InspectorPresentationStamp &) = default;
  };
  /** Captures owner revisions shared by retained toolbar controls. */
  struct ToolbarOwnerPresentationStamp final {
    bool owner_available = false;
    std::uint64_t scene_revision = 0u;
    std::uint64_t scene_selection_revision = 0u;
    std::uint64_t modeling_content_revision = 0u;
    std::uint64_t modeling_selection_revision = 0u;
    friend bool operator==(const ToolbarOwnerPresentationStamp &,
                           const ToolbarOwnerPresentationStamp &) = default;
  };
  /** Captures the active-tool revision presented by the toolbar. */
  struct ToolbarToolPresentationStamp final {
    std::uint64_t active_tool_revision = 0u;
    friend bool operator==(const ToolbarToolPresentationStamp &,
                           const ToolbarToolPresentationStamp &) = default;
  };
  /** Captures the transform-space revision presented by the toolbar. */
  struct ToolbarTransformPresentationStamp final {
    std::uint64_t transform_space_revision = 0u;
    friend bool operator==(const ToolbarTransformPresentationStamp &,
                           const ToolbarTransformPresentationStamp &) = default;
  };
  /** Captures the active settings revision presented by Tool Settings. */
  struct ToolSettingsPresentationStamp final {
    std::uint64_t active_settings_revision = 0u;
    friend bool operator==(const ToolSettingsPresentationStamp &,
                           const ToolSettingsPresentationStamp &) = default;
  };
  /** Captures face-texture settings and shortcut inputs shown by retained controls. */
  struct FaceTexturePresentationStamp final {
    QuaderFaceTextureRevision settings;
    std::uint64_t keybindings_revision = 0u;
    double rotation_step = 0.0;
    friend bool operator==(const FaceTexturePresentationStamp &,
                           const FaceTexturePresentationStamp &) = default;
  };
  /** Captures UV peel settings and shortcut inputs shown by retained controls. */
  struct UvPeelPresentationStamp final {
    QuaderUvPeelRevision settings;
    std::uint64_t keybindings_revision = 0u;
    friend bool operator==(const UvPeelPresentationStamp &,
                           const UvPeelPresentationStamp &) = default;
  };

  RetainedUiPresenter<OutlinerPresentationStamp> outliner_presenter_;
  RetainedUiPresenter<LayersPresentationStamp> layers_presenter_;
  RetainedUiPresenter<InspectorPresentationStamp> inspector_presenter_;
  RetainedUiPresenter<ToolbarOwnerPresentationStamp> toolbar_owner_presenter_;
  RetainedUiPresenter<ToolbarToolPresentationStamp> toolbar_tool_presenter_;
  RetainedUiPresenter<ToolbarTransformPresentationStamp>
      toolbar_transform_presenter_;
  RetainedUiPresenter<ToolSettingsPresentationStamp> tool_settings_presenter_;
  RetainedUiPresenter<FaceTexturePresentationStamp> face_texture_presenter_;
  RetainedUiPresenter<UvPeelPresentationStamp> uv_peel_presenter_;
  QuaderEditorApi *project_workspace_events_editor_ = nullptr;
  std::vector<QuaderEventSubscription>
      project_workspace_event_subscriptions_;
  std::uint64_t project_workspace_instance_edit_revision_ = 0;
  bool project_workspace_instance_edit_revision_valid_ = false;
  std::shared_ptr<std::atomic_bool> project_workspace_refresh_signal_ =
      std::make_shared<std::atomic_bool>(true);
  std::shared_ptr<std::atomic_uint64_t>
      material_presentation_event_generation_signal_ =
          std::make_shared<std::atomic_uint64_t>(1u);
  std::uint64_t material_presentation_observed_event_generation_ = 0;
  std::uint64_t material_presentation_invalidation_generation_ = 1;
  std::uint64_t material_browser_catalog_signature_ = 0;
  QuaderMaterialReference material_browser_active_reference_;
  std::optional<QuaderProjectMaterialDependencyRevision>
      material_browser_dependency_revision_;
  enum class MaterialClipboardMode : std::uint8_t { Empty, Copy, Cut };
  /** Captures the committed source identity fenced by the app-local clipboard. */
  struct MaterialClipboardSource final {
    ProjectAssetId id;
    std::string project_relative_path;
    std::uint64_t content_revision = 0u;
  };
  /** Retains repeatable copy or deferred cut intent for one project generation. */
  struct MaterialClipboard final {
    MaterialClipboardMode mode = MaterialClipboardMode::Empty;
    std::uint64_t project_generation = 0u;
    std::vector<ProjectAssetId> material_ids;
    std::vector<MaterialClipboardSource> sources;
  } material_clipboard_;
  /** Captures material identity and catalog revision shown by the active-material panel. */
  struct ActiveMaterialPanelPresentationStamp final {
    std::uint64_t catalog_revision = 0u;
    QuaderMaterialReference reference;
    friend bool operator==(const ActiveMaterialPanelPresentationStamp &,
                           const ActiveMaterialPanelPresentationStamp &) =
        default;
  };
  RetainedUiPresenter<ActiveMaterialPanelPresentationStamp>
      active_material_panel_presenter_;

  /** Keys one attempted material-catalog rebuild by owner and invalidation revisions. */
  struct MaterialPresentationCatalogStamp final {
    const QuaderEditorApi *editor = nullptr;
    QuaderMaterialPresentationRevision owner_revision;
    std::uint64_t event_generation = 0;
    std::uint64_t invalidation_generation = 0;
    std::uint64_t localization_revision = 0u;

    bool operator==(const MaterialPresentationCatalogStamp &) const =
        default;
  };

  /** Captures source-owner state that determines material presentation validity. */
  struct MaterialPresentationContextStamp final {
    const SceneDocument *document = nullptr;
    const ModelingSessionService *modeling = nullptr;
    const AssetRegistry *assets = nullptr;
    std::uint64_t active_scene_id = 0;
    std::uint64_t material_binding_revision = 0;
    std::uint64_t mesh_binding_revision = 0;
    std::uint64_t mesh_material_usage_revision = 0;
    std::uint64_t hierarchy_revision = 0;
    std::uint64_t modeling_content_revision = 0;
    std::uint64_t modeling_object_map_revision = 0;

    bool operator==(const MaterialPresentationContextStamp &) const =
        default;
  };

  std::vector<QtMaterialPresentationItem> material_presentation_items_;
  std::vector<QString> material_presentation_folders_;
  std::string material_browser_project_identity_;
  std::uint64_t material_browser_project_generation_ = 0u;
  std::shared_ptr<std::atomic_uint64_t> material_browser_project_generation_signal_ =
      std::make_shared<std::atomic_uint64_t>(0u);
  std::uint64_t material_presentation_signature_ = 0;
  std::uint64_t material_presentation_revision_ = 0;
  std::uint64_t material_presentation_catalog_revision_ = 0;
  std::optional<MaterialPresentationCatalogStamp>
      material_presentation_catalog_attempt_;
  std::optional<MaterialPresentationContextStamp>
      material_presentation_context_observation_;
  bool material_presentation_cache_valid_ = false;
  /** Captures revisions that determine whether inspector material usage must be queried. */
  struct InspectorMaterialQueryStamp final {
    std::uint64_t presentation_revision = 0u;
    std::uint64_t scene_selection_revision = 0u;
    std::uint64_t hierarchy_revision = 0u;
    std::uint64_t material_binding_revision = 0u;
    std::uint64_t mesh_binding_revision = 0u;
    std::uint64_t mesh_material_usage_revision = 0u;
    std::uint64_t modeling_content_revision = 0u;
    std::uint64_t modeling_selection_revision = 0u;
    std::uint64_t modeling_object_map_revision = 0u;
    friend bool operator==(const InspectorMaterialQueryStamp &,
                           const InspectorMaterialQueryStamp &) = default;
  };
  RetainedUiPresenter<InspectorMaterialQueryStamp>
      inspector_material_query_presenter_;
  QuaderMaterialUsageScope material_find_replace_scope_ =
      QuaderMaterialUsageScope::ActiveScene;
  std::string material_find_replace_project_identity_;
  std::uint64_t material_find_replace_scene_id_ = 0;
  std::uint64_t material_find_replace_scene_selection_revision_ = 0;
  std::uint64_t material_find_replace_modeling_selection_revision_ = 0;
  std::uint64_t material_find_replace_modeling_object_map_revision_ = 0;
  std::uint64_t material_find_replace_presentation_revision_ = 0;
  bool material_picker_replacement_column_ = false;
  std::uint64_t material_picker_presentation_revision_ = 0;
  QtStandardMaterialPreviewImageCache
      standard_material_preview_image_cache_;
  std::optional<MaterialEditSessionId> material_color_preview_session_;
  std::uint64_t material_color_preview_binding_generation_ = 0u;
  /** Captures the edit-session state presented by the standard material editor. */
  struct StandardMaterialEditorPresentationStamp final {
    MaterialEditSessionId session_id;
    ProjectAssetId asset_id;
    std::uint64_t draft_revision = 0u;
    std::uint64_t clean_revision = 0u;
    bool dirty = false;
    bool has_document = false;
    friend bool operator==(
        const StandardMaterialEditorPresentationStamp &,
        const StandardMaterialEditorPresentationStamp &) = default;
  };
  RetainedUiPresenter<StandardMaterialEditorPresentationStamp>
      standard_material_editor_presenter_;
  /** Captures command, live-object, and localization state shown in the status bar. */
  struct StatusPresentationStamp final {
    std::uint64_t command_revision = 0u;
    std::uint32_t live_count = 0u;
    std::uint8_t live_kind = 0u;
    std::uint64_t localization_revision = 0u;
    friend bool operator==(const StatusPresentationStamp &,
                           const StatusPresentationStamp &) = default;
  };
  RetainedUiPresenter<StatusPresentationStamp> status_presenter_;
  /** Gates selection counting and formatting before querying either owner. */
  struct StatusSelectionPresentationStamp final {
    std::uint64_t scene_revision = 0u;
    std::uint64_t scene_selection_revision = 0u;
    std::uint64_t modeling_content_revision = 0u;
    std::uint64_t modeling_selection_revision = 0u;
    std::uint64_t localization_revision = 0u;
    friend bool operator==(const StatusSelectionPresentationStamp &,
                           const StatusSelectionPresentationStamp &) = default;
  };
  RetainedUiPresenter<StatusSelectionPresentationStamp>
      status_selection_presenter_;
  std::uint64_t status_observed_command_revision_ = 0u;
  std::optional<QuaderApiStatus> status_notification_;
  std::uint64_t status_notification_command_revision_ = 0u;
  std::uint64_t status_notification_generation_ = 0u;
  bool home_visible_ = false;
  bool viewport_initialized_ = false;
  bool selection_toolbar_workspace_visible_ = true;
  bool snapping_toolbar_workspace_visible_ = true;
  bool viewport_display_toolbar_workspace_visible_ = true;
  bool texture_toolbar_workspace_visible_ = true;
  bool tool_toolbar_workspace_visible_ = true;
  std::string workspace_window_state_;
};

} // namespace quader
