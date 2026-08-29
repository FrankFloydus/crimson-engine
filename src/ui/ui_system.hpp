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
#include "ui/ui_panel_manager.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
class CurveToolService;
class EditorSelectionService;
class InspectorPanel;
class LayerPanel;
class LinearArrayToolService;
class ModelingSessionService;
class OutlinerPanel;
class PivotToolService;
class RadialArrayToolService;
class ViewportPointPlacementService;
class QtSettingsDialog;
class QtCommandEventFilter;
class QtMaterialFindReplaceDialog;
class QtMaterialBrowserWindow;
class QtActiveMaterialPanel;
class QtMaterialPreviewSurface;
class QtRetainedMessageBox;
class QtStandardMaterialEditorWindow;
class QtToolSettingsPanel;
class QtUnsavedConfirmationDialog;
class QtViewportPanel;
class QtProjectHomePage;
class QtSceneWorkspace;
class QtSelectionToolbar;
class QtSnappingToolbar;
class QtTextureToolbar;
class QtToolToolbar;
class QtViewportDisplayToolbar;
enum class QtSettingsChangeKind;
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
                             PivotToolService &pivot,
                             LinearArrayToolService &linear_array,
                             RadialArrayToolService &radial_array,
                             ViewportPointPlacementService &point_placement,
                             AssetId default_material,
                             CurveToolService *curve = nullptr);
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
  void bind_project_workspace_events(QuaderEditorApi &editor);
  void clear_project_workspace_events();
  void refresh_status_bar();
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
  void handle_settings_dialog_change(QtSettingsChangeKind kind);
  void apply_viewport_settings_to_workspace();
  void synchronize_settings_surfaces(bool refresh_dialog);
  void show_settings_dialog();
  void show_material_browser();
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
  void import_material_textures();
  [[nodiscard]] bool allow_main_window_close();
  [[nodiscard]] bool panel_visible(UIPanelId id) const;
  [[nodiscard]] bool panel_presented(UIPanelId id) const;
  void set_panel_visible(UIPanelId id, bool visible);

  std::unique_ptr<QMainWindow> main_window_;
  std::unique_ptr<QtMainMenu> main_menu_;
  std::unique_ptr<QtCommandEventFilter> command_event_filter_;
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
  QtMaterialBrowserWindow *material_browser_window_ = nullptr;
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
  QLabel *status_message_ = nullptr;
  QLabel *status_version_ = nullptr;
  QTimer *auxiliary_window_save_timer_ = nullptr;
  UIPanelManager panels_;
  LocalizationService localization_;
  UISettings settings_;
  UISettingsPaths settings_paths_;
  QtAuxiliaryWindowState auxiliary_window_state_;
  ViewportSettings viewport_settings_;
  RenderSystem *render_system_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  SceneDocument *document_ = nullptr;
  AssetRegistry *assets_ = nullptr;
  ModelingSessionService *modeling_ = nullptr;
  EditorSelectionService *selection_ = nullptr;
  PivotToolService *pivot_ = nullptr;
  LinearArrayToolService *linear_array_ = nullptr;
  RadialArrayToolService *radial_array_ = nullptr;
  CurveToolService *curve_ = nullptr;
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
  std::uint64_t outliner_scene_revision_ = 0;
  std::uint64_t outliner_scene_selection_revision_ = 0;
  std::uint64_t outliner_instance_edit_revision_ = 0;
  std::uint64_t layers_scene_revision_ = 0;
  std::uint64_t layers_scene_selection_revision_ = 0;
  std::uint64_t inspector_scene_revision_ = 0;
  std::uint64_t inspector_modeling_content_revision_ = 0;
  std::uint64_t inspector_modeling_selection_revision_ = 0;
  std::uint64_t inspector_material_presentation_revision_ = 0;
  std::uint64_t toolbar_scene_revision_ = 0;
  std::uint64_t toolbar_scene_selection_revision_ = 0;
  std::uint64_t toolbar_modeling_content_revision_ = 0;
  std::uint64_t toolbar_modeling_selection_revision_ = 0;
  std::uint64_t toolbar_active_tool_revision_ = 0;
  std::uint64_t toolbar_transform_space_revision_ = 0;
  std::uint64_t tool_settings_revision_ = 0;
  std::optional<QuaderFaceTextureRevision> face_texture_settings_revision_;
  std::uint64_t face_texture_keybindings_revision_ = 0;
  double face_texture_rotation_step_ = 0.0;
  std::optional<QuaderUvPeelRevision> uv_peel_settings_revision_;
  std::uint64_t uv_peel_keybindings_revision_ = 0;
  bool tool_settings_refresh_valid_ = false;
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
  std::uint64_t active_material_panel_catalog_revision_ = 0;
  QuaderMaterialReference active_material_panel_reference_;
  bool active_material_panel_refresh_valid_ = false;

  struct MaterialPresentationCatalogStamp final {
    const QuaderEditorApi *editor = nullptr;
    const SceneDocument *document = nullptr;
    const AssetRegistry *assets = nullptr;
    std::uint64_t active_scene_id = 0;
    std::uint64_t material_catalog_revision = 0;
    std::uint64_t event_generation = 0;
    std::uint64_t invalidation_generation = 0;
    std::string locale;

    bool operator==(const MaterialPresentationCatalogStamp &) const =
        default;
  };

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
  std::uint64_t material_presentation_signature_ = 0;
  std::uint64_t material_presentation_revision_ = 0;
  std::uint64_t material_presentation_catalog_revision_ = 0;
  std::optional<MaterialPresentationCatalogStamp>
      material_presentation_catalog_attempt_;
  std::optional<MaterialPresentationContextStamp>
      material_presentation_context_observation_;
  bool material_presentation_cache_valid_ = false;
  bool inspector_material_query_valid_ = false;
  std::uint64_t inspector_material_query_presentation_revision_ = 0;
  std::uint64_t inspector_material_query_scene_selection_revision_ = 0;
  std::uint64_t inspector_material_query_hierarchy_revision_ = 0;
  std::uint64_t inspector_material_query_material_binding_revision_ = 0;
  std::uint64_t inspector_material_query_mesh_binding_revision_ = 0;
  std::uint64_t inspector_material_query_mesh_material_usage_revision_ = 0;
  std::uint64_t inspector_material_query_modeling_content_revision_ = 0;
  std::uint64_t inspector_material_query_modeling_selection_revision_ = 0;
  std::uint64_t inspector_material_query_modeling_object_map_revision_ = 0;
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
  MaterialEditSessionId standard_material_editor_session_id_;
  ProjectAssetId standard_material_editor_asset_id_;
  std::uint64_t standard_material_editor_draft_revision_ = 0u;
  std::uint64_t standard_material_editor_clean_revision_ = 0u;
  bool standard_material_editor_dirty_ = false;
  bool standard_material_editor_refresh_valid_ = false;
  bool standard_material_editor_has_document_ = false;
  std::string outliner_locale_;
  std::string layers_locale_;
  std::string inspector_locale_;
  bool outliner_refresh_valid_ = false;
  bool layers_refresh_valid_ = false;
  bool inspector_refresh_valid_ = false;
  bool toolbar_refresh_valid_ = false;
  bool toolbar_owner_available_ = false;
  std::uint64_t status_command_revision_ = 0;
  std::uint32_t status_live_count_ = 0;
  std::uint8_t status_live_kind_ = 0;
  std::string status_locale_;
  bool status_refresh_valid_ = false;
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
