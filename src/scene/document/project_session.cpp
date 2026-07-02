////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/document/project_session.hpp"

#include "diagnostics/profile.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

#include <utility>

namespace quader {

ProjectSession::ProjectSession() {
  (void)document_.ensure_scene_root();
  rebuild_services();
  mark_clean();
}

ProjectSession::ProjectSession(ProjectDocument document)
    : document_(std::move(document)) {
  (void)document_.ensure_scene_root();
  rebuild_services();
  mark_clean();
}

ProjectSession::~ProjectSession() = default;

void ProjectSession::rebuild_services() {
  QDR_PROFILE_FUNCTION();
  history_ = std::make_unique<EditorHistoryService>();
  document_commands_ = std::make_unique<DocumentCommandService>(
      document_.scene(), *history_, &document_.assets());
  modeling_ = std::make_unique<ModelingSessionService>(document_.scene(),
                                                       document_.assets());
  selection_ = std::make_unique<EditorSelectionService>(
      document_.scene(), *document_commands_, *modeling_);
}

ProjectContentRevisionStamp ProjectSession::content_revision_stamp() const {
  const ModelingSelectionSummary modeling = modeling_->selection_summary();
  return {
      .project_revision = document_.revision(),
      .scene_revision = document_.scene().revision(),
      .asset_revision = document_.assets().revision(),
      .modeling_content_revision = modeling.content_revision,
  };
}

bool ProjectSession::dirty() const {
  return !(content_revision_stamp() == clean_stamp_);
}

ProjectAssetMutationResult
ProjectSession::replace_standalone_mesh(AssetId mesh, MeshAsset replacement) {
  if (mesh == kInvalidAssetId) {
    return {.ok = false, .changed = false, .message = "Mesh asset is invalid"};
  }
  for (const EntityId entity : scene().entities()) {
    const MeshComponent *mesh_component = scene().mesh(entity);
    if (mesh_component == nullptr || mesh_component->mesh != mesh) {
      continue;
    }
    if (scene().modeling_object(entity) != nullptr) {
      return {.ok = false,
              .changed = false,
              .message = "Mesh asset is owned by a modeling object"};
    }
  }
  if (!assets().replace_mesh(mesh, std::move(replacement))) {
    return {
        .ok = false, .changed = false, .message = "Mesh asset does not exist"};
  }
  return {.ok = true, .changed = true, .message = "Mesh replaced"};
}

void ProjectSession::replace_document(ProjectDocument document) {
  QDR_PROFILE_FUNCTION();
  const std::uint64_t previous_scene_revision = document_.scene().revision();
  document_ = std::move(document);
  {
    QDR_PROFILE_SCOPE("ProjectSession replace ensure scene root");
    (void)document_.ensure_scene_root();
  }
  {
    QDR_PROFILE_SCOPE("ProjectSession replace advance scene revision");
    document_.scene().advance_revision_for_replacement(previous_scene_revision);
  }
  {
    QDR_PROFILE_SCOPE("ProjectSession replace clear history");
    history_->clear();
  }
  {
    QDR_PROFILE_SCOPE("ProjectSession replace reset modeling from scene");
    modeling_->reset_from_scene();
  }
  {
    QDR_PROFILE_SCOPE("ProjectSession replace mark clean");
    mark_clean();
  }
}

void ProjectSession::mark_clean() { clean_stamp_ = content_revision_stamp(); }

std::unique_ptr<ProjectSession> ProjectSessionService::create_session() const {
  return std::make_unique<ProjectSession>();
}

std::unique_ptr<ProjectSession>
ProjectSessionService::create_session(ProjectDocumentRecord record) const {
  return std::make_unique<ProjectSession>(ProjectDocument(std::move(record)));
}

std::unique_ptr<ProjectSession>
ProjectSessionService::open_session(ProjectDocument document) const {
  return std::make_unique<ProjectSession>(std::move(document));
}

} // namespace quader
