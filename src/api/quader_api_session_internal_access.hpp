////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_session.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"

#include <memory>

namespace quader {

/**
 * Bridges API-owned opaque session handles to the internal project session owner.
 */
class QuaderApiSessionInternalAccess final {
public:
  [[nodiscard]] static QuaderApiSession
  from_scene_session(SceneSession &session) noexcept {
    auto binding = std::make_shared<QuaderApiSession::Binding>();
    binding->owner = &session;
    binding->kind = QuaderApiSession::OwnerKind::Scene;
    return QuaderApiSession(std::move(binding));
  }

  [[nodiscard]] static QuaderApiSession
  from_project_session(ProjectSession &session) noexcept {
    auto binding = std::make_shared<QuaderApiSession::Binding>();
    binding->owner = &session;
    binding->kind = QuaderApiSession::OwnerKind::Project;
    return QuaderApiSession(std::move(binding));
  }

  /** Rebinds every facade/handle sharing target without replacing them. */
  static void rebind(QuaderApiSession target,
                     QuaderApiSession source) noexcept {
    if (!target.valid() || !source.valid() ||
        target.binding_ == source.binding_) {
      return;
    }
    if (target.binding_->kind == QuaderApiSession::OwnerKind::Scene &&
        source.binding_->kind == QuaderApiSession::OwnerKind::Scene &&
        target.binding_->owner == source.binding_->owner) {
      return;
    }
    const bool replaces_standalone_scene =
        target.binding_->kind == QuaderApiSession::OwnerKind::Scene;
    target.binding_->owner = source.binding_->owner;
    target.binding_->kind = source.binding_->kind;
    if (replaces_standalone_scene &&
        target.binding_->kind == QuaderApiSession::OwnerKind::Scene) {
      ++target.binding_->standalone_generation;
      if (target.binding_->standalone_generation == 0u) {
        target.binding_->standalone_generation = 1u;
      }
    } else {
      target.binding_->standalone_generation =
          source.binding_->standalone_generation;
    }
  }

  [[nodiscard]] static SceneSession *
  active_scene_session(QuaderApiSession session) noexcept {
    if (!session.valid()) {
      return nullptr;
    }
    if (session.binding_->kind == QuaderApiSession::OwnerKind::Scene) {
      return static_cast<SceneSession *>(session.binding_->owner);
    }
    ProjectSession &project =
        *static_cast<ProjectSession *>(session.binding_->owner);
    OpenSceneSession *active = project.active_scene();
    return active != nullptr ? active->session.get() : nullptr;
  }

  [[nodiscard]] static ProjectSession *
  project_session(QuaderApiSession session) noexcept {
    if (!session.valid() ||
        session.binding_->kind != QuaderApiSession::OwnerKind::Project) {
      return nullptr;
    }
    return static_cast<ProjectSession *>(session.binding_->owner);
  }

  [[nodiscard]] static SceneSession &
  scene_session(QuaderApiSession session) noexcept {
    if (SceneSession *active = active_scene_session(session)) {
      return *active;
    }
    return static_cast<ProjectSession *>(session.binding_->owner)
        ->fallback_scene();
  }

  [[nodiscard]] static const SceneSession &
  read_scene_session(QuaderApiSession session) noexcept {
    return scene_session(session);
  }

  [[nodiscard]] static SceneSessionId
  scene_session_id(QuaderApiSession session) noexcept {
    if (!session.valid()) {
      return {};
    }
    if (session.binding_->kind == QuaderApiSession::OwnerKind::Scene) {
      return SceneSessionId::from_parts(
          0u, session.binding_->standalone_generation);
    }
    return static_cast<ProjectSession *>(session.binding_->owner)
        ->active_scene_id();
  }

  [[nodiscard]] static bool
  belongs_to_active_scene(QuaderApiSession session,
                          SceneSessionId id) noexcept {
    return id.valid() && id == scene_session_id(session) &&
           active_scene_session(session) != nullptr;
  }
};

} // namespace quader
