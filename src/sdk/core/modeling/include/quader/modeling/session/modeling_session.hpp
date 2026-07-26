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
class ModelingSessionTransactionAccess;

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
 * Describes the latest externally visible change to one session object.
 */
struct SessionObjectChange {
  ObjectId id{};
  std::string name;
  DirtyFlags dirty;
  bool deleted = false;
};

/**
 * Carries object changes after one session-global monotonic revision.
 */
struct SessionChangeSnapshot {
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
  std::vector<SessionObjectChange> objects;
};

/**
 * Replaces one session object's exact portable component selection as part of
 * an atomic multi-object selection publication.
 */
struct SessionComponentSelectionUpdate {
  ObjectId object{};
  PolygonSelectionSnapshot selection;
};

/**
 * Replaces one session object's portable document as part of an atomic
 * multi-object publication.
 */
struct SessionDocumentUpdate {
  ObjectId object{};
  PolygonDocument document;
  OperationResult receipt;
};

/**
 * Carries one exact document into a no-history workspace hydration.
 */
struct SessionWorkspaceDocument {
  PolygonDocument document;
  std::string name;
  bool selected = false;
};

/**
 * Restores one live document as part of a host-owned history transition.
 */
struct SessionDocumentHistoryRestore {
  ObjectId object{};
  PolygonDocument document;
  DirtyFlags dirty;
};

/**
 * Summarizes the active headless SDK preview transaction.
 */
struct PreviewTransactionSummary {
  PreviewId id{};
  bool active = false;
  ObjectId object_id{};
  std::string operation_id;
  Vec3 delta{};
  std::uint64_t content_revision = 0;
  DirtyFlags dirty;
};

/**
 * Reports the objects created by one atomic SDK clipboard paste.
 */
struct ClipboardPasteResult {
  OperationResult operation;
  std::vector<ObjectId> objects;
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
  [[nodiscard]] Result<OperationResult> commit_component_selection_batch(
      std::span<const SessionComponentSelectionUpdate> updates,
      std::span<const ObjectId> selected_objects, ObjectId active_object = {});
  [[nodiscard]] Result<OperationResult>
  commit_documents_batch(std::vector<SessionDocumentUpdate> updates,
                         bool preserve_object_selection = false);
  [[nodiscard]] Result<OperationResult> select_object(ObjectId object_id);
  [[nodiscard]] Result<OperationResult> rename_object(ObjectId object_id,
                                                      std::string name);
  [[nodiscard]] Result<OperationResult> remove_object(ObjectId object_id);
  [[nodiscard]] Result<ObjectId> duplicate_object(ObjectId object_id,
                                                   Vec3 offset = {},
                                                   std::string name = {});
  /**
   * Duplicates an ordered object set as one atomic session mutation and one
   * undo step. Returned IDs preserve the source order.
   */
  [[nodiscard]] Result<std::vector<ObjectId>>
  duplicate_objects(std::span<const ObjectId> object_ids, Vec3 offset = {},
                    std::string name = {});
  [[nodiscard]] Result<OperationResult> copy_selection_to_clipboard();
  [[nodiscard]] Result<ClipboardPasteResult>
  paste_clipboard(Vec3 offset = {});
  [[nodiscard]] bool clipboard_available() const;
  [[nodiscard]] std::size_t clipboard_object_count() const;
  /**
   * Replaces the modeled workspace while retaining the session clipboard.
   */
  void reset_workspace_preserving_clipboard();
  /**
   * Hydrates a complete modeled workspace in one publication and without
   * producing one growing undo snapshot per imported document.
   */
  [[nodiscard]] Result<std::vector<ObjectId>>
  replace_workspace_preserving_clipboard(
      std::vector<SessionWorkspaceDocument> documents);
  /**
   * Restores exact documents for a host-owned undo/redo transition without
   * copying or diffing the complete workspace.
   */
  [[nodiscard]] Result<OperationResult>
  restore_documents_as_history_transition(
      std::vector<SessionDocumentHistoryRestore> documents);
  [[nodiscard]] Result<OperationResult>
  combine_objects(std::span<const ObjectId> object_ids,
                  std::string name = {});
  [[nodiscard]] Result<OperationResult>
  bridge_objects(ObjectId survivor_id, ObjectId source_id,
                 Transform3 survivor_from_source, SelectionKind selection_kind,
                 int steps = 1);
  [[nodiscard]] Result<OperationResult> select_face(ObjectId object_id,
                                                    FaceId face_id);
  [[nodiscard]] Result<OperationResult> translate_selection(Vec3 delta);
  [[nodiscard]] Result<OperationResult>
  begin_translate_preview(ObjectId object_id, Vec3 delta);
  [[nodiscard]] Result<OperationResult> update_translate_preview(Vec3 delta);
  [[nodiscard]] Result<PreviewId>
  begin_document_preview(ObjectId object_id, std::string operation_id,
                         PolygonDocument preview_document,
                         OperationResult receipt);
  [[nodiscard]] Result<OperationResult>
  update_document_preview(PreviewId preview_id,
                          PolygonDocument preview_document,
                          OperationResult receipt);
  [[nodiscard]] Result<OperationResult> commit_preview();
  [[nodiscard]] Result<OperationResult> commit_preview(PreviewId preview_id);
  [[nodiscard]] Result<OperationResult> cancel_preview();
  [[nodiscard]] Result<OperationResult> cancel_preview(PreviewId preview_id);
  [[nodiscard]] Result<OperationResult> undo();
  [[nodiscard]] Result<OperationResult> redo();
  [[nodiscard]] bool can_undo() const;
  [[nodiscard]] bool can_redo() const;
  [[nodiscard]] std::size_t undo_depth() const;
  void squash_undo_since(std::size_t mark);
  /** Drops SDK-local history entries without changing the current state. */
  void discard_undo_since(std::size_t mark);
  [[nodiscard]] bool rollback_undo_since(std::size_t mark);
  /**
   * Clones the current durable workspace for a host-owned checkpoint.
   *
   * Objects, documents, selection, clipboard, revisions, and identity
   * high-water marks are retained. SDK undo/redo storage, active preview
   * payloads, and transient batch transaction state are deliberately omitted.
   */
  [[nodiscard]] ModelingSession current_workspace_checkpoint() const;
  /**
   * Restores a semantic session snapshot while retaining every allocator and
   * topology-identity high-water mark already observed by this session.
   */
  void restore_snapshot_preserving_identity_high_water(
      const ModelingSession &snapshot);
  /**
   * Restores a host-history snapshot as a newly published state transition,
   * retaining allocator, topology identity, and revision high-water marks.
   */
  void restore_snapshot_as_history_transition(const ModelingSession &snapshot);
  /** Clears SDK-local undo/redo storage after host-owned workspace hydration. */
  void clear_history();
  [[nodiscard]] bool preview_active() const;
  [[nodiscard]] bool preview_active(PreviewId preview_id) const;
  [[nodiscard]] std::vector<SessionObjectSummary> objects() const;
  [[nodiscard]] std::uint64_t content_revision() const;
  [[nodiscard]] std::uint64_t selection_revision() const;
  [[nodiscard]] SessionChangeSnapshot
  changes_since(std::uint64_t previous_content_revision) const;
  [[deprecated("Use document_snapshot() or document_copy(); borrowed pointers "
               "are invalidated by session mutation.")]]
  [[nodiscard]] const PolygonDocument *document(ObjectId object_id) const;
  [[nodiscard]] Result<std::shared_ptr<const PolygonDocument>>
  document_snapshot(ObjectId object_id) const;
  [[nodiscard]] Result<PolygonDocument> document_copy(ObjectId object_id) const;
  [[nodiscard]] Result<std::vector<PolygonDocument>>
  document_copies(std::span<const ObjectId> object_ids) const;
  [[nodiscard]] Result<OperationResult>
  commit_document(ObjectId object_id, PolygonDocument document,
                  OperationResult receipt);
  [[nodiscard]] Result<ToolPreviewPayload> preview_payload() const;
  [[nodiscard]] Result<ToolPreviewPayload>
  preview_payload(PreviewId preview_id) const;
  [[nodiscard]] Result<PolygonDocument>
  preview_document_copy(PreviewId preview_id) const;
  [[nodiscard]] PreviewTransactionSummary preview_summary() const;
  [[nodiscard]] Result<SemanticOverlayPayload>
  semantic_overlay(ObjectId object_id) const;
private:
  friend class ModelingSessionTransactionAccess;

  [[nodiscard]] bool begin_batch_transaction();
  void authorize_batch_creation();
  void target_batch_object(ObjectId object_id);
  void clear_batch_target();
  [[nodiscard]] bool finish_batch_transaction();

  std::unique_ptr<ModelingSessionImpl> impl_;
};

} // namespace quader::modeling
