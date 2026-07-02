////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api.hpp"

#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_object_handle.hpp"
#include "editor/commands/editor_command_service.hpp"

namespace quader {

/**
 * Owns Impl behavior within the public Quader API surface.
 */
class QuaderApi::Impl final {
public:
  explicit Impl(QuaderApiSession api_session)
      : session(api_session),
        project_session(
            QuaderApiSessionInternalAccess::project_session(api_session)),
        history(project_session.history()),
        editor_commands(history, project_session.document_commands()),
        mutations(project_session, history, editor_commands, modeling_commands),
        scene(api_session, mutations), modeling(api_session, mutations),
        selection(api_session, mutations), assets(api_session, mutations),
        history_api(api_session, mutations), query(api_session, mutations) {}

  QuaderApiSession session;
  ProjectSession &project_session;
  EditorHistoryService &history;
  EditorCommandService editor_commands;
  ModelingCommandExecutionService modeling_commands;
  QuaderApiMutationRunner mutations;
  QuaderSceneApi scene;
  QuaderModelingApi modeling;
  QuaderSelectionApi selection;
  QuaderAssetApi assets;
  QuaderHistoryApi history_api;
  QuaderQueryApi query;
};

QuaderApi::QuaderApi(QuaderApiSession session)
    : impl_(std::make_unique<Impl>(session)) {}

QuaderApi::~QuaderApi() = default;

QuaderApi::QuaderApi(QuaderApi&&) noexcept = default;
QuaderApi& QuaderApi::operator=(QuaderApi&&) noexcept = default;

QuaderSceneApi& QuaderApi::scene() {
    return impl_->scene;
}

const QuaderSceneApi& QuaderApi::scene() const {
    return impl_->scene;
}

QuaderModelingApi& QuaderApi::modeling() {
    return impl_->modeling;
}

const QuaderModelingApi& QuaderApi::modeling() const {
    return impl_->modeling;
}

QuaderSelectionApi& QuaderApi::selection() {
    return impl_->selection;
}

const QuaderSelectionApi& QuaderApi::selection() const {
    return impl_->selection;
}

QuaderAssetApi& QuaderApi::assets() {
    return impl_->assets;
}

const QuaderAssetApi& QuaderApi::assets() const {
    return impl_->assets;
}

QuaderHistoryApi &QuaderApi::history() { return impl_->history_api; }

const QuaderHistoryApi& QuaderApi::history() const {
  return impl_->history_api;
}

QuaderQueryApi& QuaderApi::query() {
    return impl_->query;
}

const QuaderQueryApi& QuaderApi::query() const {
    return impl_->query;
}

QuaderApiSnapshot QuaderApi::snapshot() const {
    return impl_->query.snapshot();
}

QuaderRevisionStamp QuaderApi::revisions() const {
    return impl_->query.revisions();
}

QuaderScript QuaderApi::script(std::string label) {
    return QuaderScript(*this, std::move(label));
}

QuaderObjectHandle QuaderApi::object(QuaderModelingObjectRef ref) {
    return QuaderObjectHandle(*this, ref);
}

QuaderApiSession QuaderApi::session() const {
    return impl_->session;
}

QuaderApiMutationRunner& QuaderApi::mutations() {
    return impl_->mutations;
}

} // namespace quader
