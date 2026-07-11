////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"

#include "api/quader_api_session_internal_access.hpp"
#include "editor/session/project_session.hpp"

namespace quader {

/**
 * Owns Impl behavior within the public Quader API surface.
 */
class QuaderEditorApi::Impl final {
public:
    explicit Impl(QuaderApiSession session)
        : api(session)
        , project(session)
        , tools(session)
#ifndef NDEBUG
        , dev_tools(session)
        , preferences()
        , commands(api, project, viewports, tools, preferences, dev_tools)
#else
        , preferences()
        , commands(api, project, viewports, tools, preferences)
#endif
        , render(session, api.query()) {
      preferences.attach_commands(commands);
      tools.set_runtime_hooks(QuaderToolsRuntimeHooks{
          .commit_active_tool_preview =
              [this] { return api.modeling().commit_preview(); },
          .cancel_active_tool_preview =
              [this] { return api.modeling().cancel_preview(); },
      });
    }

    QuaderApi api;
    QuaderProjectApi project;
    QuaderViewportApi viewports;
    QuaderToolsApi tools;
#ifndef NDEBUG
    QuaderDevToolsApi dev_tools;
#endif
    QuaderPreferencesApi preferences;
    QuaderCommandsApi commands;
    QuaderUIApi ui;
    QuaderEventsApi events;
    QuaderRenderApi render;
};

QuaderEditorApi::QuaderEditorApi(QuaderApiSession session)
    : impl_(std::make_unique<Impl>(session)) {}

QuaderEditorApi::~QuaderEditorApi() = default;

QuaderEditorApi::QuaderEditorApi(QuaderEditorApi&&) noexcept = default;
QuaderEditorApi& QuaderEditorApi::operator=(QuaderEditorApi&&) noexcept = default;

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
        editor(QuaderApiSessionInternalAccess::from_project_session(session)) {}

  ProjectSession session;
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
