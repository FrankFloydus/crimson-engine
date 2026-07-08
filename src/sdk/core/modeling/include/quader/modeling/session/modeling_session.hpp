////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/commands/command.hpp>
#include <quader/modeling/mesh/polygon_document.hpp>
#include <quader/modeling/tools/tool_payload.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace quader::modeling {

struct ModelingSessionImpl;

/**
 * Summarizes one SDK session object without exposing native scene identity.
 */
struct SessionObjectSummary {
  ObjectId id{};
  std::string name;
  bool selected = false;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
};

/**
 * Summarizes the active headless SDK preview transaction.
 */
struct PreviewTransactionSummary {
  bool active = false;
  ObjectId object_id{};
  std::string operation_id;
  Vec3 delta{};
  std::uint64_t content_revision = 0;
  DirtyFlags dirty;
};

/**
 * Owns headless SDK modeling session state and undo/redo snapshots.
 */
class ModelingSession {
public:
  ModelingSession();
  ModelingSession(const ModelingSession &other);
  ModelingSession(ModelingSession &&other) noexcept;
  ModelingSession &operator=(const ModelingSession &other);
  ModelingSession &operator=(ModelingSession &&other) noexcept;
  ~ModelingSession();

  [[nodiscard]] Result<ObjectId> add_document(PolygonDocument document,
                                              std::string name = "Mesh");
  [[nodiscard]] Result<OperationResult>
  apply_object_selection(std::span<const ObjectId> objects,
                         SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] Result<OperationResult> select_object(ObjectId object_id);
  [[nodiscard]] Result<OperationResult> rename_object(ObjectId object_id,
                                                      std::string name);
  [[nodiscard]] Result<OperationResult> remove_object(ObjectId object_id);
  [[nodiscard]] Result<ObjectId> duplicate_object(ObjectId object_id,
                                                  Vec3 offset = {},
                                                  std::string name = {});
  [[nodiscard]] Result<OperationResult>
  combine_objects(std::span<const ObjectId> object_ids,
                  std::string name = {});
  [[nodiscard]] Result<OperationResult> select_face(ObjectId object_id,
                                                    FaceId face_id);
  [[nodiscard]] Result<OperationResult> translate_selection(Vec3 delta);
  [[nodiscard]] Result<OperationResult>
  begin_translate_preview(ObjectId object_id, Vec3 delta);
  [[nodiscard]] Result<OperationResult> update_translate_preview(Vec3 delta);
  [[nodiscard]] Result<OperationResult> commit_preview();
  [[nodiscard]] Result<OperationResult> cancel_preview();
  [[nodiscard]] Result<OperationResult> undo();
  [[nodiscard]] Result<OperationResult> redo();
  [[nodiscard]] bool can_undo() const;
  [[nodiscard]] bool can_redo() const;
  [[nodiscard]] std::size_t undo_depth() const;
  void squash_undo_since(std::size_t mark);
  [[nodiscard]] bool rollback_undo_since(std::size_t mark);
  [[nodiscard]] bool preview_active() const;
  [[nodiscard]] std::vector<SessionObjectSummary> objects() const;
  [[nodiscard]] const PolygonDocument *document(ObjectId object_id) const;
  [[nodiscard]] Result<PolygonDocument> document_copy(ObjectId object_id) const;
  [[nodiscard]] Result<OperationResult>
  commit_document(ObjectId object_id, PolygonDocument document,
                  OperationResult receipt);
  [[nodiscard]] Result<ToolPreviewPayload> preview_payload() const;
  [[nodiscard]] PreviewTransactionSummary preview_summary() const;
  [[nodiscard]] Result<SemanticOverlayPayload>
  semantic_overlay(ObjectId object_id) const;
  [[nodiscard]] std::vector<CommandDescriptor> operation_registry() const;

private:
  std::unique_ptr<ModelingSessionImpl> impl_;
};

} // namespace quader::modeling
