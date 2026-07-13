////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "scene/document/project_document.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace quader {

class AssetCommandService;
class EditorSelectionService;
class LinearArrayToolService;
class ModelingSessionService;
class NativeViewportToolCoordinator;
class PivotToolService;

/**
 * Stores the persisted-content revision stamp used by the project session dirty-state boundary.
 */
struct ProjectContentRevisionStamp {
  std::uint64_t project_revision = 0;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::uint64_t modeling_content_revision = 0;

  [[nodiscard]] bool
  operator==(const ProjectContentRevisionStamp &) const = default;
};

/**
 * Stores the result of project-owned asset mutations that depend on scene ownership.
 */
struct ProjectAssetMutationResult {
  bool ok = true;
  bool changed = false;
  std::string message;
};

/**
 * Stores const project-session owner references for read-only consumers.
 */
struct ProjectSessionReadView final {
  const ProjectDocument &document;
  const SceneDocument &scene;
  const AssetRegistry &assets;
  const ModelingSessionService &modeling;
  const EditorSelectionService &selection;
};

/**
 * Owns project session behavior within the editor session ownership layer.
 */
class ProjectSession {
public:
  ProjectSession();
  explicit ProjectSession(ProjectDocument document);
  ~ProjectSession();

  ProjectSession(const ProjectSession &) = delete;
  ProjectSession &operator=(const ProjectSession &) = delete;
  ProjectSession(ProjectSession &&) = delete;
  ProjectSession &operator=(ProjectSession &&) = delete;

  ProjectDocument &document() { return document_; }
  const ProjectDocument &document() const { return document_; }

  SceneDocument &scene() { return document_.scene(); }
  const SceneDocument &scene() const { return document_.scene(); }

  AssetRegistry &assets() { return document_.assets(); }
  const AssetRegistry &assets() const { return document_.assets(); }

  EditorHistoryService &history() { return *history_; }
  const EditorHistoryService &history() const { return *history_; }

  DocumentCommandService &document_commands() { return *document_commands_; }
  AssetCommandService &asset_commands() { return *asset_commands_; }
  ModelingSessionService &modeling() { return *modeling_; }
  const ModelingSessionService &modeling() const { return *modeling_; }
  EditorSelectionService &selection() { return *selection_; }
  const EditorSelectionService &selection() const { return *selection_; }
  PivotToolService &pivot() { return *pivot_; }
  const PivotToolService &pivot() const { return *pivot_; }
  LinearArrayToolService &linear_array() { return *linear_array_; }
  const LinearArrayToolService &linear_array() const { return *linear_array_; }
  NativeViewportToolCoordinator &native_tools() { return *native_tools_; }
  const NativeViewportToolCoordinator &native_tools() const {
    return *native_tools_;
  }

  [[nodiscard]] ProjectSessionReadView read_view() const;
  [[nodiscard]] ProjectContentRevisionStamp content_revision_stamp() const;
  [[nodiscard]] bool dirty() const;
  [[nodiscard]] ProjectAssetMutationResult
  replace_standalone_mesh(AssetId mesh, MeshAsset replacement);
  void replace_document(ProjectDocument document);
  void set_document_replacement_finalizer(
      std::function<void(ProjectSession &)> finalizer);
  void mark_clean();

private:
  /**
   * Tracks revision writes owned by one live operation projection.
   */
  struct PreviewContentCheckpoint {
    ProjectContentRevisionStamp expected_stamp;
    bool began_clean = false;
    bool contaminated = false;
  };

  void rebuild_services();
  void begin_modeling_preview_checkpoint();
  void observe_before_modeling_preview_projection();
  void record_after_modeling_preview_projection();
  void finish_modeling_preview_checkpoint(bool restored_by_cancel);

  ProjectDocument document_;
  ProjectContentRevisionStamp clean_stamp_;
  std::optional<PreviewContentCheckpoint> preview_content_checkpoint_;
  std::unique_ptr<EditorHistoryService> history_;
  std::unique_ptr<DocumentCommandService> document_commands_;
  std::unique_ptr<AssetCommandService> asset_commands_;
  std::unique_ptr<ModelingSessionService> modeling_;
  std::unique_ptr<EditorSelectionService> selection_;
  std::unique_ptr<PivotToolService> pivot_;
  std::unique_ptr<LinearArrayToolService> linear_array_;
  std::unique_ptr<NativeViewportToolCoordinator> native_tools_;
  std::function<void(ProjectSession &)> document_replacement_finalizer_;
};

/**
 * Coordinates project session creation for editor-owned session composition.
 */
class ProjectSessionService final {
public:
  std::unique_ptr<ProjectSession> create_session() const;
  std::unique_ptr<ProjectSession> create_session(ProjectDocumentRecord record) const;
  std::unique_ptr<ProjectSession> open_session(ProjectDocument document) const;
};

} // namespace quader
