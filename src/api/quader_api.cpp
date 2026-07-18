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

#include <stdexcept>

namespace quader {
namespace {

[[nodiscard]] QuaderApiSession require_bound_session(
    QuaderApiSession session) {
  if (!session.valid()) {
    throw std::invalid_argument("QuaderApi requires a bound editor session.");
  }
  return session;
}

} // namespace

/**
 * Owns Impl behavior within the public Quader API surface.
 */
class QuaderApi::Impl final {
public:
  explicit Impl(QuaderApiSession api_session)
      : session(api_session), mutations(api_session),
        scene(api_session, mutations), layers(api_session, mutations),
        modeling(api_session, mutations),
        selection(api_session, mutations), assets(api_session, mutations),
        history_api(api_session, mutations), query(api_session, mutations) {}

  QuaderApiSession session;
  std::function<void(bool)> dirty_changed_callback;
  QuaderApiMutationRunner mutations;
  QuaderSceneApi scene;
  QuaderLayersApi layers;
  QuaderModelingApi modeling;
  QuaderSelectionApi selection;
  QuaderAssetApi assets;
  QuaderHistoryApi history_api;
  QuaderQueryApi query;
};

QuaderApi::QuaderApi(QuaderApiSession session)
    : impl_(std::make_unique<Impl>(require_bound_session(session))) {}

QuaderApi::~QuaderApi() = default;

QuaderApi::QuaderApi(QuaderApi&&) noexcept = default;
QuaderApi& QuaderApi::operator=(QuaderApi&&) noexcept = default;

void QuaderApi::rebind(QuaderApiSession session,
                       std::uint64_t) {
  QuaderApiSessionInternalAccess::rebind(impl_->session, session);
  impl_->mutations.synchronize_dirty_state();
}

void QuaderApi::set_dirty_changed_callback(
    std::function<void(bool)> callback) {
  impl_->dirty_changed_callback = std::move(callback);
  impl_->mutations.set_dirty_changed_callback(impl_->dirty_changed_callback);
}

void QuaderApi::synchronize_dirty_state() {
  impl_->mutations.synchronize_dirty_state();
}

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

QuaderLayersApi &QuaderApi::layers() { return impl_->layers; }

const QuaderLayersApi &QuaderApi::layers() const { return impl_->layers; }

std::uint64_t QuaderApi::scene_binding_identity() const {
  return QuaderApiSessionInternalAccess::scene_session_id(impl_->session)
      .packed();
}

QuaderApiMutationRunner& QuaderApi::mutations() {
    return impl_->mutations;
}

} // namespace quader
