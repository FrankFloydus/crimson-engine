////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_materials_api.hpp"

#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_status_internal.hpp"
#include "editor/material/material_reference_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "io/project/project_material_reference_resolver.hpp"

#include <utility>

namespace quader {
namespace {

[[nodiscard]] MaterialReference
to_internal_reference(const QuaderMaterialReference &reference) {
  return reference.project_owned()
             ? MaterialReference::project(reference.project_asset)
             : MaterialReference(reference.embedded_asset);
}

[[nodiscard]] QuaderMaterialReference
to_public_reference(const MaterialReference &reference) {
  return reference.project_owned()
             ? QuaderMaterialReference::project(reference.project_asset())
             : QuaderMaterialReference::embedded(reference.embedded_asset());
}

[[nodiscard]] MaterialReferenceUsageScope
to_internal_scope(QuaderMaterialUsageScope scope) {
  return scope == QuaderMaterialUsageScope::ActiveScene
             ? MaterialReferenceUsageScope::ActiveScene
             : MaterialReferenceUsageScope::Selection;
}

[[nodiscard]] bool destination_exists(QuaderApiSession api_session,
                                      SceneSession &scene,
                                      const MaterialReference &reference) {
  if (reference.embedded()) {
    return scene.assets().material(reference.embedded_asset()) != nullptr;
  }
  if (!reference.project_owned()) {
    return false;
  }
  const ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(api_session);
  return project != nullptr &&
         project->manifest() != nullptr &&
         !project->manifest()->is_material_deleted(reference.project_asset()) &&
         project->material_catalog().find(reference.project_asset()) !=
             nullptr;
}

[[nodiscard]] MaterialReferenceService::ReferenceResolver
reference_resolver(QuaderApiSession session) {
  const ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session);
  return [project](MaterialReference reference) {
    if (!reference.project_owned() || project == nullptr ||
        project->manifest() == nullptr) {
      return reference;
    }
    return MaterialReference::project(
        ProjectMaterialReferenceResolver::canonical_material_id(
            *project->manifest(), reference.project_asset()));
  };
}

} // namespace

QuaderMaterialsApi::QuaderMaterialsApi(QuaderApiSession session,
                                       QuaderApiMutationRunner &mutations)
    : session_(session), mutations_(&mutations) {}

QuaderApiResult<std::vector<QuaderMaterialReference>>
QuaderMaterialsApi::used_materials(QuaderMaterialUsageScope scope) const {
  SceneSession *scene =
      QuaderApiSessionInternalAccess::active_scene_session(session_);
  if (scene == nullptr) {
    return quader_api_result_from_status<
        std::vector<QuaderMaterialReference>>(quader_api_failure_status(
        QuaderApiSessionInternalAccess::read_scene_session(session_),
        "No scene is active."));
  }

  MaterialReferenceService service(scene->scene(), scene->assets(),
                                   scene->modeling(), reference_resolver(session_));
  const std::vector<MaterialReference> internal =
      service.used_materials(to_internal_scope(scope));
  std::vector<QuaderMaterialReference> references;
  references.reserve(internal.size());
  for (const MaterialReference &reference : internal) {
    references.push_back(to_public_reference(reference));
  }
  const QuaderApiVoidResult status = quader_api_void_status(
      *scene, true, false, {}, QuaderMutationKind::None);
  return quader_api_result_from_status(status, std::move(references));
}

QuaderApiVoidResult QuaderMaterialsApi::replace_material_references(
    QuaderMaterialReference source, QuaderMaterialReference replacement,
    QuaderMaterialUsageScope scope) {
  SceneSession *scene =
      QuaderApiSessionInternalAccess::active_scene_session(session_);
  if (scene == nullptr || mutations_ == nullptr) {
    return quader_api_failure_status(
        QuaderApiSessionInternalAccess::read_scene_session(session_),
        "No scene is active.");
  }

  const MaterialReference internal_source = to_internal_reference(source);
  const MaterialReference internal_replacement =
      to_internal_reference(replacement);
  MaterialReferenceService service(scene->scene(), scene->assets(),
                                   scene->modeling(), reference_resolver(session_));
  MaterialReferenceReplacementPreparation prepared =
      service.prepare_replacement(
          internal_source, internal_replacement, to_internal_scope(scope),
          [this, scene](const MaterialReference &candidate) {
            return destination_exists(session_, *scene, candidate);
          });
  if (!prepared.ok || !prepared.changed || prepared.command == nullptr) {
    return mutations_->run_modeling_result(
        {
            .ok = prepared.ok,
            .changed = false,
            .message = std::move(prepared.message),
        },
        "Replace Material");
  }
  return mutations_->run_modeling_undoable_command(
      std::move(prepared.command), "Replace Material");
}

} // namespace quader
