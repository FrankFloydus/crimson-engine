////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"

#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_project_api_internal_access.hpp"
#include "api/quader_project_assets_api_internal_access.hpp"
#include "api/quader_scene_documents_api_internal_access.hpp"
#include "api/quader_standard_material_documents_api_internal_access.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/material/project_material_texture_snapshot.hpp"
#include "editor/modes/editor_mode_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <stdexcept>

namespace quader {
namespace {

QuaderApiSession project_api_session(ProjectSession &project_session) {
  return QuaderApiSessionInternalAccess::from_project_session(project_session);
}

[[nodiscard]] QuaderApiSession require_bound_editor_session(
    QuaderApiSession session) {
  if (!session.valid()) {
    throw std::invalid_argument(
        "QuaderEditorApi requires a bound editor session.");
  }
  return session;
}

} // namespace

/**
 * Owns Impl behavior within the public Quader API surface.
 */
class QuaderEditorApi::Impl final {
public:
    /// Weakly held by scene services, so they remain safe after facade teardown.
    struct TextureRuntimeBinding final {
      QuaderPreferencesApi *preferences = nullptr;
      QuaderUIApi *ui = nullptr;
      const SceneSession *active_scene = nullptr;
    };

    explicit Impl(QuaderApiSession session)
        : api(session)
        , mode(session, api.mutations())
        , materials(session, api.mutations())
        , project(session)
        , project_assets(session)
        , standard_material_documents(session)
        , scene_documents(session)
        , tools(session)
        , selection_diagnostics(session)
#ifndef NDEBUG
        , dev_tools(session)
        , preferences()
        , ui()
        , commands(api, project, scene_documents, viewports, tools, mode,
                   preferences, ui, selection_diagnostics, dev_tools)
#else
        , preferences()
        , ui()
        , commands(api, project, scene_documents, viewports, tools, mode,
                   preferences, ui, selection_diagnostics)
#endif
        , render(session, api) {
      attach_document_lifecycle();
      rebind(session);
      install_tool_hooks();
    }

    explicit Impl(ProjectSession &project_session)
        : project_session_owner(&project_session)
        , api(project_api_session(project_session))
        , mode(project_api_session(project_session), api.mutations())
        , materials(project_api_session(project_session), api.mutations())
        , project(project_session)
        , project_assets(project_api_session(project_session))
        , standard_material_documents(project_api_session(project_session))
        , scene_documents(project_session)
        , tools(project_api_session(project_session))
        , selection_diagnostics(project_api_session(project_session))
#ifndef NDEBUG
        , dev_tools(project_api_session(project_session))
        , preferences()
        , ui()
        , commands(api, project, scene_documents, viewports, tools, mode,
                   preferences, ui, selection_diagnostics, dev_tools)
#else
        , preferences()
        , ui()
        , commands(api, project, scene_documents, viewports, tools, mode,
                   preferences, ui, selection_diagnostics)
#endif
        , render(project_api_session(project_session), api) {
      attach_document_lifecycle();
      rebind(project_api_session(project_session));
      install_tool_hooks();
    }

    ~Impl() { texture_runtime_binding.reset(); }

    void install_texture_hooks(SceneSession &scene, QuaderApiSession session) {
      scene.modeling().invalidate_face_texture_edit_state();
      scene.modeling().invalidate_uv_peel_selection();
      texture_runtime_binding->preferences = &preferences;
      texture_runtime_binding->ui = &ui;
      texture_runtime_binding->active_scene = &scene;
      const std::weak_ptr<TextureRuntimeBinding> lifetime =
          texture_runtime_binding;
      scene.editor_mode().set_transition_settlement([lifetime, owner = &scene] {
        const auto binding = lifetime.lock();
        if (binding == nullptr || binding->ui == nullptr ||
            binding->active_scene != owner) {
          return command_unchanged_result();
        }
        const QuaderApiVoidResult result =
            binding->ui->settle_pending_property_edits();
        return CommandResult{.ok = result.ok(), .changed = result.changed(),
                             .message = result.error_message()};
      });
      const auto texture_locks = [lifetime] {
        if (const auto binding = lifetime.lock(); binding != nullptr) {
          return TextureLockSettings{
              .texture_lock = binding->preferences->texture_lock_enabled(),
              .texture_lock_scale =
                  binding->preferences->texture_lock_scale_enabled(),
          };
        }
        return TextureLockSettings{};
      };
      scene.document_commands().set_texture_lock_settings_provider(texture_locks);
      scene.modeling().set_texture_lock_settings_provider(texture_locks);
      scene.modeling().set_face_texture_rotation_step_provider([lifetime] {
        const auto binding = lifetime.lock();
        return binding != nullptr ? binding->preferences->rotation_snap_degrees()
                                   : 15.0;
      });

      ProjectSession *project_owner =
          QuaderApiSessionInternalAccess::project_session(session);
      scene.modeling().set_face_texture_extent_provider(
          [lifetime, project_owner](MaterialReference material)
              -> std::optional<QVec2> {
            if (lifetime.expired() || project_owner == nullptr ||
                !material.project_owned()) {
              return std::nullopt;
            }
            const auto texture = project_owner->material_texture_image(
                material.project_asset(), StandardMaterialTextureSlot::Albedo);
            if (texture == nullptr) {
              return std::nullopt;
            }
            return QVec2{static_cast<float>(texture->width),
                         static_cast<float>(texture->height)};
          },
          [lifetime, project_owner] {
            return !lifetime.expired() && project_owner != nullptr
                       ? project_owner->material_texture_revision()
                       : std::uint64_t{0};
          });
      scene.modeling().set_face_texture_material_transform_provider(
          [lifetime, project_owner](MaterialReference material) {
            if (lifetime.expired() || project_owner == nullptr || !material.project_owned()) {
              return StandardMaterialTextureTransform{};
            }
            const auto *entry = project_owner->material_catalog().find(
                project_owner->canonical_material_id(material.project_asset()));
            const auto *standard = entry != nullptr ? entry->material.standard_material() : nullptr;
            return standard != nullptr ? standard->texture_transform : StandardMaterialTextureTransform{};
          });
    }

    void install_tool_hooks() {
      preferences.attach_commands(commands);
      tools.set_runtime_hooks(QuaderToolsRuntimeHooks{
          .commit_active_tool_preview =
              [this] { return api.modeling().commit_preview(); },
          .cancel_active_tool_preview =
              [this] { return api.modeling().cancel_preview(); },
          .grid_world_size = [this] {
            return static_cast<double>(viewport_grid_world_size_from_contract(
                viewports.grid_preset()));
          },
          .notify = [this](QuaderNotification notification) {
            (void)ui.notify(std::move(notification));
          },
      });
    }

    void attach_document_lifecycle() {
      api.set_dirty_changed_callback([this](bool) {
        QuaderSceneDocumentsApiInternalAccess::publish_dirty_changed(
            scene_documents);
      });
      dirty_sync_subscription = events.subscribe(
          quader_event::kSceneDirtyChanged,
          [this](const QuaderEvent &) { api.synchronize_dirty_state(); });
      QuaderSceneDocumentsApiInternalAccess::attach_events(scene_documents,
                                                           events);
      QuaderProjectApiInternalAccess::attach_events(project, events);
      QuaderProjectAssetsApiInternalAccess::attach_events(project_assets,
                                                          events);
      QuaderStandardMaterialDocumentsApiInternalAccess::attach_events(
          standard_material_documents, events);
      QuaderProjectApiInternalAccess::attach_scene_documents(project,
                                                              scene_documents);
      project_assets_sync_subscription = events.subscribe(
          quader_event::kProjectOpened, [this](const QuaderEvent &) {
            QuaderProjectAssetsApiInternalAccess::publish_assets_changed(
                project_assets);
          });
      project_assets_close_subscription = events.subscribe(
          quader_event::kProjectClosed, [this](const QuaderEvent &) {
            QuaderProjectAssetsApiInternalAccess::publish_assets_changed(
                project_assets);
          });
      const auto callback = [this](QuaderApiSession session) {
        rebind(session);
      };
      QuaderSceneDocumentsApiInternalAccess::set_binding_changed_callback(
          scene_documents, callback);
      QuaderProjectApiInternalAccess::set_binding_changed_callback(project,
                                                                   callback);
      QuaderProjectAssetsApiInternalAccess::set_binding_changed_callback(
          project_assets, callback);
    }

    void rebind(QuaderApiSession session) {
      SceneSession &scene =
          QuaderApiSessionInternalAccess::scene_session(session);
      std::uint64_t binding_identity = 1u;
      std::uint64_t activation_generation = 1u;
      if (project_session_owner != nullptr) {
        binding_identity = 0u;
        activation_generation =
            project_session_owner->activation_generation();
        for (const std::unique_ptr<OpenSceneSession> &candidate :
             project_session_owner->open_scenes()) {
          if (candidate != nullptr && candidate->session.get() == &scene) {
            binding_identity = candidate->session_id.packed();
            break;
          }
        }
      }
      api.rebind(session, binding_identity);
      mode.rebind_session(session);
      install_texture_hooks(scene, session);
      materials.rebind_session(session);
      project.rebind_session(session);
      project_assets.rebind_session(session);
      standard_material_documents.rebind_session(session);
      tools.rebind_session(session);
      selection_diagnostics.rebind_session(session);
#ifndef NDEBUG
      dev_tools.rebind_session(session);
#endif
      render.rebind_session(session, binding_identity,
                            activation_generation);
      QuaderViewportApiRuntimeAccess::bind_scene_workspace(
          viewports, scene.viewport_workspace());
      QuaderViewportApiRuntimeAccess::bind_scene_local_visibility(
          viewports, scene.viewport_local_visibility());
      if (host_scene_binding_changed) {
        host_scene_binding_changed(session);
      }
    }

    ProjectSession *project_session_owner = nullptr;
    QuaderApi api;
    QuaderEditorModeApi mode;
    QuaderMaterialsApi materials;
    QuaderProjectApi project;
    QuaderProjectAssetsApi project_assets;
    QuaderStandardMaterialDocumentsApi standard_material_documents;
    QuaderSceneDocumentsApi scene_documents;
    QuaderViewportApi viewports;
    QuaderToolsApi tools;
    QuaderSelectionDiagnosticsApi selection_diagnostics;
#ifndef NDEBUG
    QuaderDevToolsApi dev_tools;
#endif
    QuaderPreferencesApi preferences;
    QuaderUIApi ui;
    QuaderCommandsApi commands;
    QuaderEventsApi events;
    QuaderEventSubscription dirty_sync_subscription;
    QuaderEventSubscription project_assets_sync_subscription;
    QuaderEventSubscription project_assets_close_subscription;
    QuaderRenderApi render;
    std::function<void(QuaderApiSession)> host_scene_binding_changed;
    std::shared_ptr<TextureRuntimeBinding> texture_runtime_binding =
        std::make_shared<TextureRuntimeBinding>();
};

QuaderEditorApi::QuaderEditorApi(QuaderApiSession session)
    : impl_(std::make_unique<Impl>(require_bound_editor_session(session))) {}

QuaderEditorApi::QuaderEditorApi(ProjectSession &project_session)
    : impl_(std::make_unique<Impl>(project_session)) {}

QuaderEditorApi::~QuaderEditorApi() = default;

QuaderEditorApi::QuaderEditorApi(QuaderEditorApi&&) noexcept = default;
QuaderEditorApi& QuaderEditorApi::operator=(QuaderEditorApi&&) noexcept = default;

QuaderReleaseInfo QuaderEditorApi::release_info() const {
  const ProductRelease version = current_product_release();
  return {.version = version,
          .label = product_release_label(version),
          .internal_full = version == kInternalFullRelease};
}

bool QuaderEditorApi::feature_available(ReleaseFeature feature) const noexcept {
  return release_has_feature(feature);
}

void QuaderEditorApi::rebind(QuaderApiSession session) {
  impl_->rebind(session);
}

void QuaderEditorApi::set_scene_binding_changed_callback(
    std::function<void(QuaderApiSession)> callback) {
  impl_->host_scene_binding_changed = std::move(callback);
}

QuaderApi& QuaderEditorApi::api() {
    return impl_->api;
}

const QuaderApi& QuaderEditorApi::api() const {
    return impl_->api;
}

QuaderProjectApi& QuaderEditorApi::project() {
    return impl_->project;
}

const QuaderProjectApi& QuaderEditorApi::project() const {
    return impl_->project;
}

QuaderProjectAssetsApi &QuaderEditorApi::project_assets() {
  return impl_->project_assets;
}

const QuaderProjectAssetsApi &QuaderEditorApi::project_assets() const {
  return impl_->project_assets;
}

QuaderMaterialsApi &QuaderEditorApi::materials() {
  return impl_->materials;
}

const QuaderMaterialsApi &QuaderEditorApi::materials() const {
  return impl_->materials;
}

QuaderStandardMaterialDocumentsApi &
QuaderEditorApi::standard_material_documents() {
  return impl_->standard_material_documents;
}

const QuaderStandardMaterialDocumentsApi &
QuaderEditorApi::standard_material_documents() const {
  return impl_->standard_material_documents;
}

QuaderSceneDocumentsApi &QuaderEditorApi::scene_documents() {
  return impl_->scene_documents;
}

const QuaderSceneDocumentsApi &QuaderEditorApi::scene_documents() const {
  return impl_->scene_documents;
}

QuaderSceneApi& QuaderEditorApi::scene() {
    return impl_->api.scene();
}

const QuaderSceneApi& QuaderEditorApi::scene() const {
  return impl_->api.scene();
}

QuaderLayersApi &QuaderEditorApi::layers() { return impl_->api.layers(); }

const QuaderLayersApi &QuaderEditorApi::layers() const {
  return impl_->api.layers();
}

QuaderModelingApi& QuaderEditorApi::modeling() {
    return impl_->api.modeling();
}

const QuaderModelingApi& QuaderEditorApi::modeling() const {
    return impl_->api.modeling();
}

QuaderSelectionApi& QuaderEditorApi::selection() {
    return impl_->api.selection();
}

const QuaderSelectionApi& QuaderEditorApi::selection() const {
    return impl_->api.selection();
}

QuaderEditorModeApi &QuaderEditorApi::mode() { return impl_->mode; }

const QuaderEditorModeApi &QuaderEditorApi::mode() const {
  return impl_->mode;
}

QuaderSelectionDiagnosticsApi &QuaderEditorApi::selection_diagnostics() {
  return impl_->selection_diagnostics;
}

const QuaderSelectionDiagnosticsApi &QuaderEditorApi::selection_diagnostics() const {
  return impl_->selection_diagnostics;
}

QuaderAssetApi& QuaderEditorApi::assets() {
    return impl_->api.assets();
}

const QuaderAssetApi& QuaderEditorApi::assets() const {
    return impl_->api.assets();
}

QuaderHistoryApi& QuaderEditorApi::history() {
    return impl_->api.history();
}

const QuaderHistoryApi& QuaderEditorApi::history() const {
    return impl_->api.history();
}

QuaderQueryApi& QuaderEditorApi::query() {
    return impl_->api.query();
}

const QuaderQueryApi& QuaderEditorApi::query() const {
    return impl_->api.query();
}

QuaderViewportApi& QuaderEditorApi::viewports() {
    return impl_->viewports;
}

const QuaderViewportApi& QuaderEditorApi::viewports() const {
    return impl_->viewports;
}

QuaderToolsApi& QuaderEditorApi::tools() {
    return impl_->tools;
}

const QuaderToolsApi& QuaderEditorApi::tools() const {
    return impl_->tools;
}

QuaderCommandsApi& QuaderEditorApi::commands() {
    return impl_->commands;
}

const QuaderCommandsApi& QuaderEditorApi::commands() const {
    return impl_->commands;
}

#ifndef NDEBUG
QuaderDevToolsApi& QuaderEditorApi::dev_tools() {
    return impl_->dev_tools;
}

const QuaderDevToolsApi& QuaderEditorApi::dev_tools() const {
    return impl_->dev_tools;
}
#endif

QuaderPreferencesApi& QuaderEditorApi::preferences() {
    return impl_->preferences;
}

const QuaderPreferencesApi& QuaderEditorApi::preferences() const {
    return impl_->preferences;
}

QuaderUIApi& QuaderEditorApi::ui() {
    return impl_->ui;
}

const QuaderUIApi& QuaderEditorApi::ui() const {
    return impl_->ui;
}

QuaderEventsApi& QuaderEditorApi::events() {
    return impl_->events;
}

const QuaderEventsApi& QuaderEditorApi::events() const {
    return impl_->events;
}

QuaderRenderApi& QuaderEditorApi::render() {
    return impl_->render;
}

const QuaderRenderApi& QuaderEditorApi::render() const {
    return impl_->render;
}

QuaderViewportHandle QuaderEditorApi::main_viewport() {
  return QuaderViewportHandle(impl_->viewports, ViewportPaneId::Perspective);
}

QuaderViewportHandle QuaderEditorApi::viewport(ViewportPaneId pane) {
    return QuaderViewportHandle(impl_->viewports, pane);
}

QuaderObjectHandle QuaderEditorApi::object(QuaderModelingObjectRef ref) {
    return impl_->api.object(ref);
}

QuaderScript QuaderEditorApi::script(std::string label) {
    return impl_->api.script(std::move(label));
}

QuaderApiSnapshot QuaderEditorApi::snapshot() const {
    return impl_->api.snapshot();
}

QuaderRevisionStamp QuaderEditorApi::revisions() const {
    return impl_->api.revisions();
}

/**
 * Owns Impl behavior for the public standalone editor bootstrap.
 */
class QuaderStandaloneEditor::Impl final {
public:
  Impl()
      : session(),
        editor(QuaderApiSessionInternalAccess::from_scene_session(session)) {}

  SceneSession session;
  QuaderEditorApi editor;
};

QuaderStandaloneEditor::QuaderStandaloneEditor()
    : impl_(std::make_unique<Impl>()) {}

QuaderStandaloneEditor::~QuaderStandaloneEditor() = default;

QuaderStandaloneEditor::QuaderStandaloneEditor(QuaderStandaloneEditor&&) noexcept = default;
QuaderStandaloneEditor& QuaderStandaloneEditor::operator=(QuaderStandaloneEditor&&) noexcept = default;

QuaderEditorApi& QuaderStandaloneEditor::editor() {
    return impl_->editor;
}

const QuaderEditorApi& QuaderStandaloneEditor::editor() const {
    return impl_->editor;
}

QuaderStandaloneEditor create_standalone_editor() {
  return QuaderStandaloneEditor();
}

} // namespace quader
