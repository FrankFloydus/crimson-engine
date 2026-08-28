////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "project_material_fixture.hpp"

#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/project_session.hpp"

#include <utility>

namespace quader {
namespace {

SceneDocument *active_scene(ProjectSession *project) {
  if (project == nullptr) {
    return nullptr;
  }
  OpenSceneSession *open_scene = project->active_scene();
  return open_scene != nullptr && open_scene->session != nullptr
             ? &open_scene->session->scene()
             : nullptr;
}

} // namespace

ProjectMaterialFixture::ProjectMaterialFixture(ProjectSession &project)
    : project_(&project) {}

bool ProjectMaterialFixture::set_material(EntityId entity,
                                          MaterialComponent material) const {
  SceneDocument *scene = active_scene(project_);
  return scene != nullptr && scene->set_material(entity, std::move(material));
}

bool ProjectMaterialFixture::clear_material(EntityId entity) const {
  SceneDocument *scene = active_scene(project_);
  return scene != nullptr && scene->clear_material(entity);
}

bool ProjectMaterialFixture::set_visible(EntityId entity, bool visible) const {
  SceneDocument *scene = active_scene(project_);
  return scene != nullptr && scene->set_visible(entity, visible);
}

bool ProjectMaterialFixture::set_locked(EntityId entity, bool locked) const {
  SceneDocument *scene = active_scene(project_);
  return scene != nullptr && scene->set_locked(entity, locked);
}

bool ProjectMaterialFixture::set_selectable(EntityId entity,
                                            bool selectable) const {
  SceneDocument *scene = active_scene(project_);
  return scene != nullptr && scene->set_selectable(entity, selectable);
}

bool ProjectMaterialFixture::bind_shared_mesh(EntityId entity, AssetId mesh) const {
  SceneDocument *scene = active_scene(project_);
  return scene != nullptr && scene->set_mesh(entity, {.mesh = mesh});
}

bool ProjectMaterialFixture::set_projection_failure(
    ProjectionFailurePoint point) const {
  OpenSceneSession *open_scene =
      project_ != nullptr ? project_->active_scene() : nullptr;
  if (open_scene == nullptr || open_scene->session == nullptr) {
    return false;
  }
  ModelingSdkProjectionFailurePoint native = ModelingSdkProjectionFailurePoint::None;
  switch (point) {
  case ProjectionFailurePoint::None:
    break;
  case ProjectionFailurePoint::AfterSdkMutation:
    native = ModelingSdkProjectionFailurePoint::AfterSdkMutation;
    break;
  case ProjectionFailurePoint::CommittedSceneNotify:
    native = ModelingSdkProjectionFailurePoint::CommittedSceneNotify;
    break;
  case ProjectionFailurePoint::BeforeCommit:
    native = ModelingSdkProjectionFailurePoint::BeforeCommit;
    break;
  }
  open_scene->session->modeling().set_modeling_sdk_projection_failure_for_tests(native);
  return true;
}

std::optional<QMat4> ProjectMaterialFixture::instance_member_world_transform(
    EntityId member, EntityId occurrence) const {
  OpenSceneSession *open_scene =
      project_ != nullptr ? project_->active_scene() : nullptr;
  return open_scene != nullptr && open_scene->session != nullptr
             ? open_scene->session->instances().effective_member_world_transform(
                   member, occurrence)
             : std::nullopt;
}

ModelingSelectionExport ProjectMaterialFixture::selection() const {
  if (project_ == nullptr) {
    return {};
  }
  OpenSceneSession *open_scene = project_->active_scene();
  return open_scene != nullptr && open_scene->session != nullptr
             ? open_scene->session->modeling().export_selection()
             : ModelingSelectionExport{};
}

} // namespace quader
