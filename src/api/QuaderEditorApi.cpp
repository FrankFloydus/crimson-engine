////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderEditorApi.h"

#include "document/ProjectSession.h"

namespace quader {

/**
 * Owns Impl behavior within the public Quader API surface.
 */
class QuaderEditorApi::Impl final {
public:
    explicit Impl(ProjectSession& session)
        : api(session)
        , project(session)
        , viewports(viewportWorkspace)
        , tools(session)
        , commands(api, viewports, tools)
        , render(session, api.query()) {}

    QuaderApi api;
    QuaderProjectApi project;
    ViewportWorkspaceService viewportWorkspace;
    QuaderViewportApi viewports;
    QuaderToolsApi tools;
    QuaderCommandsApi commands;
    QuaderPreferencesApi preferences;
    QuaderUIApi ui;
    QuaderEventsApi events;
    QuaderRenderApi render;
};

QuaderEditorApi::QuaderEditorApi(ProjectSession& session)
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

QuaderViewportHandle QuaderEditorApi::mainViewport() {
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

} // namespace quader
