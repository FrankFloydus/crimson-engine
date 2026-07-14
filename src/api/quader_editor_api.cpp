////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_project_api_internal_access.hpp"
#include "api/quader_project_assets_api_internal_access.hpp"
#include "api/quader_scene_documents_api_internal_access.hpp"
#include "api/quader_standard_material_documents_api_internal_access.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

namespace quader {
namespace {

QuaderApiSession project_api_session(ProjectSession &project_session) {
  return QuaderApiSessionInternalAccess::from_project_session(project_session);
}

} // namespace

/**
 * Owns Impl behavior within the public Quader API surface.
 */
class QuaderEditorApi::Impl final {
public:
    explicit Impl(QuaderApiSession session)
        : api(session)
        , project(session)
        , project_assets(session)
        , standard_material_documents(session)
        , scene_documents(session)
        , tools(session)
#ifndef NDEBUG
        , dev_tools(session)
        , preferences()
        , commands(api, project, scene_documents, viewports, tools,
                   preferences, dev_tools)
#else
        , preferences()
        , commands(api, project, scene_documents, viewports, tools,
                   preferences)
#endif
        , render(session, api) {
      attach_document_lifecycle();
      rebind(session);
      install_tool_hooks();
    }

    explicit Impl(ProjectSession &project_session)
        : project_session_owner(&project_session)
        , api(project_api_session(project_session))
        , project(project_session)
        , project_assets(project_api_session(project_session))
        , standard_material_documents(project_api_session(project_session))
        , scene_documents(project_session)
        , tools(project_api_session(project_session))
#ifndef NDEBUG
        , dev_tools(project_api_session(project_session))
        , preferences()
        , commands(api, project, scene_documents, viewports, tools,
                   preferences, dev_tools)
#else
        , preferences()
        , commands(api, project, scene_documents, viewports, tools,
                   preferences)
#endif
        , render(project_api_session(project_session), api) {
      attach_document_lifecycle();
      rebind(project_api_session(project_session));
      install_tool_hooks();
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
      project.rebind_session(session);
      project_assets.rebind_session(session);
      standard_material_documents.rebind_session(session);
      tools.rebind_session(session);
#ifndef NDEBUG
      dev_tools.rebind_session(session);
#endif
      render.rebind_session(session, binding_identity,
                            activation_generation);
      QuaderViewportApiRuntimeAccess::bind_scene_workspace(
          viewports, scene.viewport_workspace());
      if (host_scene_binding_changed) {
        host_scene_binding_changed(session);
      }
    }

    ProjectSession *project_session_owner = nullptr;
    QuaderApi api;
    QuaderProjectApi project;
    QuaderProjectAssetsApi project_assets;
    QuaderStandardMaterialDocumentsApi standard_material_documents;
    QuaderSceneDocumentsApi scene_documents;
    QuaderViewportApi viewports;
    QuaderToolsApi tools;
#ifndef NDEBUG
    QuaderDevToolsApi dev_tools;
#endif
    QuaderPreferencesApi preferences;
    QuaderCommandsApi commands;
    QuaderUIApi ui;
    QuaderEventsApi events;
    QuaderEventSubscription dirty_sync_subscription;
    QuaderEventSubscription project_assets_sync_subscription;
    QuaderEventSubscription project_assets_close_subscription;
    QuaderRenderApi render;
    std::function<void(QuaderApiSession)> host_scene_binding_changed;
};

QuaderEditorApi::QuaderEditorApi(QuaderApiSession session)
    : impl_(std::make_unique<Impl>(session)) {}

QuaderEditorApi::QuaderEditorApi(ProjectSession &project_session)
    : impl_(std::make_unique<Impl>(project_session)) {}

QuaderEditorApi::~QuaderEditorApi() = default;

QuaderEditorApi::QuaderEditorApi(QuaderEditorApi&&) noexcept = default;
QuaderEditorApi& QuaderEditorApi::operator=(QuaderEditorApi&&) noexcept = default;

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
