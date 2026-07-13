////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/session/project_session.hpp"

#include "diagnostics/profile.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/commands/asset_command_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"

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
  modeling_ = std::make_unique<ModelingSessionService>(
      document_.scene(), document_.assets(),
      ModelingOperationPreviewCheckpointHooks{
          .begin = [this]() { begin_modeling_preview_checkpoint(); },
          .observe_before_projection = [this]() {
            observe_before_modeling_preview_projection();
          },
          .record_after_projection = [this]() {
            record_after_modeling_preview_projection();
          },
          .finish = [this](bool restored_by_cancel) {
            finish_modeling_preview_checkpoint(restored_by_cancel);
          },
      });
  selection_ = std::make_unique<EditorSelectionService>(
      document_.scene(), *document_commands_, *modeling_);
  pivot_ = std::make_unique<PivotToolService>(
      document_.scene(), document_.assets(), *history_, *modeling_);
  linear_array_ = std::make_unique<LinearArrayToolService>(
      document_.scene(), document_.assets(), *history_, *modeling_,
      *selection_);
  native_tools_ = std::make_unique<NativeViewportToolCoordinator>(
      *pivot_, *linear_array_, *selection_, *modeling_);
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

void ProjectSession::begin_modeling_preview_checkpoint() {
  const ProjectContentRevisionStamp current = content_revision_stamp();
  preview_content_checkpoint_ = PreviewContentCheckpoint{
      .expected_stamp = current,
      .began_clean = current == clean_stamp_,
  };
}

void ProjectSession::observe_before_modeling_preview_projection() {
  if (!preview_content_checkpoint_.has_value()) {
    return;
  }
  if (!(content_revision_stamp() ==
        preview_content_checkpoint_->expected_stamp)) {
    preview_content_checkpoint_->contaminated = true;
  }
}

void ProjectSession::record_after_modeling_preview_projection() {
  if (preview_content_checkpoint_.has_value()) {
    preview_content_checkpoint_->expected_stamp = content_revision_stamp();
  }
}

void ProjectSession::finish_modeling_preview_checkpoint(
    bool restored_by_cancel) {
  if (!preview_content_checkpoint_.has_value()) {
    return;
  }

  const ProjectContentRevisionStamp current = content_revision_stamp();
  if (!(current == preview_content_checkpoint_->expected_stamp)) {
    preview_content_checkpoint_->contaminated = true;
  }
  if (restored_by_cancel && preview_content_checkpoint_->began_clean &&
      !preview_content_checkpoint_->contaminated) {
    clean_stamp_ = current;
  }
  preview_content_checkpoint_.reset();
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
  const std::uint64_t previous_asset_revision = document_.assets().revision();
  modeling_->clear_transient_state_for_document_replacement();
  native_tools_->reset_transient_state();
  preview_content_checkpoint_.reset();
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
    QDR_PROFILE_SCOPE("ProjectSession replace advance asset revision");
    document_.assets().advance_revision_for_replacement(
        previous_asset_revision);
  }
  {
    QDR_PROFILE_SCOPE("ProjectSession replace clear history");
    history_->clear();
  }
  {
    QDR_PROFILE_SCOPE("ProjectSession replace reset modeling from scene");
    modeling_->reset_from_scene();
  }
  if (document_replacement_finalizer_) {
    QDR_PROFILE_SCOPE("ProjectSession replace finalize native document");
    document_replacement_finalizer_(*this);
  }
  {
    QDR_PROFILE_SCOPE("ProjectSession replace mark clean");
    mark_clean();
  }
}

void ProjectSession::set_document_replacement_finalizer(
    std::function<void(ProjectSession &)> finalizer) {
  document_replacement_finalizer_ = std::move(finalizer);
}

void ProjectSession::mark_clean() {
  clean_stamp_ = content_revision_stamp();
  preview_content_checkpoint_.reset();
}

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
