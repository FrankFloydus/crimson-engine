////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/session/project_session.hpp"

#include "diagnostics/profile.hpp"
#include "editor/commands/asset_command_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/selection/editor_selection_service.hpp"

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
  asset_commands_ = std::make_unique<AssetCommandService>(
      document_.assets(), *history_, &document_.scene());
  modeling_ = std::make_unique<ModelingSessionService>(document_.scene(),
                                                       document_.assets());
  selection_ = std::make_unique<EditorSelectionService>(
      document_.scene(), *document_commands_, *modeling_);
  pivot_ = std::make_unique<PivotToolService>(
      document_.scene(), document_.assets(), *history_, *modeling_);
}

ProjectSessionReadView ProjectSession::read_view() const {
  return {
      .document = document_,
      .scene = document_.scene(),
      .assets = document_.assets(),
      .modeling = *modeling_,
      .selection = *selection_,
  };
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
  const AssetCommandResult result =
      asset_commands().replace_mesh(mesh, std::move(replacement));
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = result.message,
  };
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
