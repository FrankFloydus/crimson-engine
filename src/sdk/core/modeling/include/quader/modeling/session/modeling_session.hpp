////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/commands/command.hpp>
#include <quader/modeling/curves/curve_document.hpp>
#include <quader/modeling/mesh/polygon_document.hpp>
#include <quader/modeling/tools/tool_payload.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace quader::modeling {

struct ModelingSessionImpl;
struct SessionPreviewCheckpointImpl;
struct SessionMutationCheckpointImpl;
class ModelingSessionTransactionAccess;

/// Identifies the authored document owned by a session object.
enum class SessionObjectKind {
  Mesh,
  Curve,
};

/**
 * Summarizes one SDK session object without exposing native scene identity.
 */
struct SessionObjectSummary {
  ObjectId id{};
  std::string name;
  bool selected = false;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
  SessionObjectKind kind = SessionObjectKind::Mesh;
};

/**
 * Describes the latest externally visible change to one session object.
 */
struct SessionObjectChange {
  ObjectId id{};
  std::string name;
  DirtyFlags dirty;
  bool deleted = false;
  SessionObjectKind kind = SessionObjectKind::Mesh;
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

/** Carries the pure survivor candidate produced for a cross-object Bridge. */
struct SessionBridgePreviewCandidate {
  PolygonDocument survivor_document;
  OperationResult receipt;
};

/**
 * Stores either authored document kind accepted by an SDK preview transaction.
 */
using SessionPreviewDocument = std::variant<PolygonDocument, CurveDocument>;

/** Selects how one object participates in an atomic preview transaction. */
enum class SessionPreviewAction {
  ReplaceExisting,
  AddProvisional,
  RemoveExisting,
};

/**
 * Describes one structural participant before a preview becomes visible.
 *
 * ReplaceExisting and AddProvisional require document. RemoveExisting requires
 * document to be empty. AddProvisional also requires object to be invalid; the
 * session returns a stable reserved ObjectId from begin.
 */
struct SessionPreviewParticipant {
  SessionPreviewAction action = SessionPreviewAction::ReplaceExisting;
  ObjectId object{};
  std::optional<SessionPreviewDocument> document;
  OperationResult receipt;
  std::string name;
  std::uint32_t material_slot = 0;
  bool selected = false;
};

/** Begins one atomic structural preview and its optional selection outcome. */
struct SessionPreviewTransactionRequest {
  std::vector<SessionPreviewParticipant> participants;
  bool replace_object_selection = false;
  std::optional<std::size_t> active_participant_index;
};

/** Maps one request participant to its stable preview object identity. */
struct SessionPreviewParticipantIdentity {
  std::size_t participant_index = 0;
  SessionPreviewAction action = SessionPreviewAction::ReplaceExisting;
  ObjectId object_id{};
};

/** Reports the active transaction and every request-to-preview identity. */
struct SessionPreviewBeginResult {
  PreviewId preview_id{};
  std::vector<SessionPreviewParticipantIdentity> participants;
};

/** Maps one preview participant through structural commit materialization. */
struct SessionPreviewObjectRemap {
  SessionPreviewAction action = SessionPreviewAction::ReplaceExisting;
  ObjectId preview_object_id{};
  ObjectId committed_object_id{};
  std::uint32_t material_slot = 0;
};

/** Reports one atomic structural commit and its deterministic object remap. */
struct SessionPreviewCommitResult {
  OperationResult operation;
  std::vector<SessionPreviewObjectRemap> objects;
  std::vector<ObjectId> selected_objects;
  ObjectId active_object{};
};

/**
 * Replaces one preview candidate as part of an atomic multi-object update.
 * The document kind must match the transaction participant identified by
 * object, including a stable provisional AddProvisional object ID.
 */
struct SessionPreviewDocumentUpdate {
  ObjectId object{};
  SessionPreviewDocument document;
  OperationResult receipt;
};

/**
 * Carries one exact document into a no-history workspace hydration.
 */
struct SessionWorkspaceDocument {
  PolygonDocument document;
  std::string name;
  bool selected = false;
  /** When present, supplies a curve instead of the empty polygon document. */
  std::optional<CurveDocument> curve_document;
};

/**
 * Restores one live document as part of a host-owned history transition.
 */
struct SessionDocumentHistoryRestore {
  ObjectId object{};
  PolygonDocument document;
  DirtyFlags dirty;
  /** When present, restores a curve instead of the empty polygon document. */
  std::optional<CurveDocument> curve_document;
};

/** Summarizes one object participating in an SDK preview transaction. */
struct PreviewTransactionObjectSummary {
  ObjectId object_id{};
  SessionPreviewAction action = SessionPreviewAction::ReplaceExisting;
  SessionObjectKind kind = SessionObjectKind::Mesh;
  std::uint64_t base_content_revision = 0;
  std::uint64_t content_revision = 0;
  DirtyFlags dirty;
  bool changed = false;
  std::uint32_t material_slot = 0;
  bool selected = false;
};

/**
 * Shares one immutable ordered summary publication across cheap summary reads.
 * Iterators and references remain valid while this value is retained.
 */
class PreviewTransactionObjectSummaryList {
public:
  // Mirrors the standard container spelling exposed by the iterator API.
  using const_iterator = // NOLINT(readability-identifier-naming)
      std::vector<PreviewTransactionObjectSummary>::const_iterator;

  PreviewTransactionObjectSummaryList() = default;
  explicit PreviewTransactionObjectSummaryList(
      std::vector<PreviewTransactionObjectSummary> values)
      : values_(std::make_shared<
                const std::vector<PreviewTransactionObjectSummary>>(
            std::move(values))) {}

  [[nodiscard]] bool empty() const noexcept { return values().empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return values().size(); }
  [[nodiscard]] const PreviewTransactionObjectSummary &
  operator[](std::size_t index) const noexcept {
    return values()[index];
  }
  [[nodiscard]] const PreviewTransactionObjectSummary &front() const {
    return values().front();
  }
  [[nodiscard]] const_iterator begin() const noexcept {
    return values().begin();
  }
  [[nodiscard]] const_iterator end() const noexcept { return values().end(); }

private:
  [[nodiscard]] const std::vector<PreviewTransactionObjectSummary> &
  values() const noexcept {
    static const std::vector<PreviewTransactionObjectSummary> kEmptyValues;
    return values_ != nullptr ? *values_ : kEmptyValues;
  }

  std::shared_ptr<const std::vector<PreviewTransactionObjectSummary>> values_;
};

/**
 * Summarizes the active headless SDK preview transaction.
 *
 * object_id, kind, content_revision, and dirty retain the 0.1 single-object
 * view and mirror the first ordered object. Multi-object consumers use objects.
 */
struct PreviewTransactionSummary {
  PreviewId id{};
  bool active = false;
  ObjectId object_id{};
  std::string operation_id;
  Vec3 delta{};
  std::uint64_t content_revision = 0;
  DirtyFlags dirty;
  SessionObjectKind kind = SessionObjectKind::Mesh;
  PreviewTransactionObjectSummaryList objects;
};

/**
 * Owns one renderer-neutral preview publication. Mesh objects carry a compiled
 * tool payload, Curve objects carry their authored document, and removed
 * objects carry monostate so renderers can suppress them without native state.
 */
struct SessionPreviewObjectPayload {
  ObjectId object_id{};
  SessionPreviewAction action = SessionPreviewAction::ReplaceExisting;
  SessionObjectKind kind = SessionObjectKind::Mesh;
  std::uint64_t content_revision = 0;
  DirtyFlags dirty;
  bool changed = false;
  std::uint32_t material_slot = 0;
  bool selected = false;
  std::variant<ToolPreviewPayload, CurveDocument, std::monostate> payload;
};

/** Carries every object publication in one ordered atomic preview. */
struct SessionPreviewPayload {
  PreviewId id{};
  std::string operation_id;
  std::uint64_t content_revision = 0;
  DirtyFlags dirty;
  std::vector<SessionPreviewObjectPayload> objects;
};

/** Reports deterministic preview-owner work without exposing implementation. */
struct SessionPreviewWorkStats {
  std::uint64_t live_object_lookup_count = 0;
  std::uint64_t preview_object_lookup_count = 0;
  std::uint64_t mesh_payload_build_count = 0;
  std::uint64_t checkpoint_count = 0;
  std::uint64_t checkpoint_participant_reference_count = 0;
  /** Immutable authored documents are shared; checkpoint capture keeps this 0. */
  std::uint64_t checkpoint_authored_byte_copy_count = 0;
};

/** Reports bounded participant-scoped mutation checkpoint work. */
struct SessionMutationCheckpointWorkStats {
  std::uint64_t capture_count = 0;
  std::uint64_t restore_count = 0;
  std::uint64_t commit_count = 0;
  std::uint64_t participant_lookup_count = 0;
  std::uint64_t participant_snapshot_count = 0;
  std::uint64_t participant_restore_count = 0;
  std::uint64_t transition_record_snapshot_count = 0;
  std::uint64_t history_suffix_record_count = 0;
  /** Mutation checkpoints never clone the complete workspace. */
  std::uint64_t workspace_copy_count = 0;
};

/**
 * Reports the objects created by one atomic SDK clipboard paste.
 */
struct ClipboardPasteResult {
  OperationResult operation;
  std::vector<ObjectId> objects;
};

/**
 * Pins one active preview for rollback without copying workspace or history.
 *
 * A token belongs to its originating session and active preview. Moving the
 * session transfers that ownership; copying or replacing session state does
 * not. Restoring a token never rewinds observed component or preview identities.
 */
class SessionPreviewCheckpoint {
public:
  [[nodiscard]] bool valid() const noexcept { return impl_ != nullptr; }

private:
  friend class ModelingSession;
  std::shared_ptr<const SessionPreviewCheckpointImpl> impl_;
};

/**
 * Owns one synchronous participant-scoped rollback checkpoint.
 *
 * The token is move-only because capture temporarily detaches the session redo
 * branch. It must be consumed by restore_mutation_checkpoint() on rollback or
 * commit_mutation_checkpoint() after successful publication. Dropping an
 * unconsumed token leaves the session blocked until its checked
 * abort_active_mutation_checkpoint() recovery is called.
 */
class SessionMutationCheckpoint {
public:
  SessionMutationCheckpoint() = default;
  ~SessionMutationCheckpoint();
  SessionMutationCheckpoint(SessionMutationCheckpoint &&) noexcept;
  SessionMutationCheckpoint &
  operator=(SessionMutationCheckpoint &&) noexcept;
  SessionMutationCheckpoint(const SessionMutationCheckpoint &) = delete;
  SessionMutationCheckpoint &
  operator=(const SessionMutationCheckpoint &) = delete;

  [[nodiscard]] bool valid() const noexcept { return impl_ != nullptr; }

private:
  friend class ModelingSession;
  std::shared_ptr<const SessionMutationCheckpointImpl> impl_;
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
  [[nodiscard]] Result<ObjectId> add_curve_document(CurveDocument document,
                                                   std::string name = "Curve");
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
  /**
   * Builds the exact Bridge survivor document without mutating session state.
   * The caller can stage it as ReplaceExisting beside a RemoveExisting source.
   */
  [[nodiscard]] Result<SessionBridgePreviewCandidate>
  prepare_bridge_documents(ObjectId survivor_id, ObjectId source_id,
                           Transform3 survivor_from_source,
                           SelectionKind selection_kind, int steps = 1) const;
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
  /** Begins a curve edit in the same exclusive preview registry as meshes. */
  [[nodiscard]] Result<PreviewId>
  begin_curve_document_preview(ObjectId object_id, std::string operation_id,
                               CurveDocument preview_document,
                               OperationResult receipt);
  [[nodiscard]] Result<OperationResult>
  update_curve_document_preview(PreviewId preview_id,
                                CurveDocument preview_document,
                                OperationResult receipt);
  /**
   * Begins one ordered atomic preview over one or more mesh and Curve objects.
   * Every candidate is validated before the transaction becomes visible.
   */
  [[nodiscard]] Result<PreviewId> begin_preview_transaction(
      std::string operation_id,
      std::vector<SessionPreviewDocumentUpdate> updates);
  /**
   * Begins one atomic Replace/Add/Remove transaction. Returned provisional
   * object IDs stay stable for the complete preview and, for Add, after commit.
   */
  [[nodiscard]] Result<SessionPreviewBeginResult> begin_preview_transaction(
      std::string operation_id, SessionPreviewTransactionRequest request);
  /**
   * Atomically replaces every candidate in an active multi-object preview.
   * The update set must contain each Replace/Add document participant exactly
   * once; Remove participants are omitted. Caller order does not change the
   * deterministic order established by begin.
   */
  [[nodiscard]] Result<OperationResult> update_preview_transaction(
      PreviewId preview_id,
      std::vector<SessionPreviewDocumentUpdate> updates);
  /** Captures only the active candidate, receipt, and preview payload state. */
  [[nodiscard]] Result<SessionPreviewCheckpoint>
  capture_preview_checkpoint(PreviewId preview_id) const;
  /** Checks preview-checkpoint ownership and base identity without mutation. */
  [[nodiscard]] Result<OperationResult> preflight_preview_checkpoint(
      const SessionPreviewCheckpoint &checkpoint) const;
  /**
   * Restores the same active preview without changing durable documents,
   * selection, clipboard, undo/redo, or allocator high-water marks.
   */
  [[nodiscard]] Result<OperationResult>
  restore_preview_checkpoint(const SessionPreviewCheckpoint &checkpoint);
  /**
   * Begins one synchronous rollback scope over existing participant objects.
   * Capture detaches redo in O(1); structural inventory changes are rejected.
   */
  [[nodiscard]] Result<SessionMutationCheckpoint>
  capture_mutation_checkpoint(std::span<const ObjectId> participants);
  /**
   * Checks restore/commit ownership, participant, inventory, and history
   * invariants without consuming the token or changing session state.
   */
  [[nodiscard]] Result<OperationResult> preflight_mutation_checkpoint(
      const SessionMutationCheckpoint &checkpoint) const;
  /**
   * Silently restores enrolled objects, selection, revisions, and history.
   * Component identity allocator high-water remains monotonic.
   */
  [[nodiscard]] Result<OperationResult>
  restore_mutation_checkpoint(SessionMutationCheckpoint &&checkpoint);
  /** Finalizes the successful branch and discards the detached redo history. */
  [[nodiscard]] Result<OperationResult>
  commit_mutation_checkpoint(SessionMutationCheckpoint &&checkpoint);
  /**
   * Emergency fail-open cleanup after another owner rejects aggregate
   * preflight. Keeps the current branch, discards detached redo, and validates
   * token ownership only so even a stale scope can release its active guard.
   */
  [[nodiscard]] Result<OperationResult>
  discard_mutation_checkpoint(SessionMutationCheckpoint &&checkpoint);
  /** Recovers an orphaned scope after its move-only token was dropped. */
  [[nodiscard]] Result<OperationResult> abort_active_mutation_checkpoint();
  [[nodiscard]] bool mutation_checkpoint_active() const;
  [[nodiscard]] Result<OperationResult> commit_preview();
  [[nodiscard]] Result<OperationResult> commit_preview(PreviewId preview_id);
  /** Commits a preview and reports deterministic structural object remaps. */
  [[nodiscard]] Result<SessionPreviewCommitResult>
  commit_preview_transaction(PreviewId preview_id);
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
  [[nodiscard]] Result<std::shared_ptr<const CurveDocument>>
  curve_document_snapshot(ObjectId object_id) const;
  [[nodiscard]] Result<CurveDocument>
  curve_document_copy(ObjectId object_id) const;
  [[nodiscard]] Result<std::vector<PolygonDocument>>
  document_copies(std::span<const ObjectId> object_ids) const;
  [[nodiscard]] Result<OperationResult>
  commit_document(ObjectId object_id, PolygonDocument document,
                  OperationResult receipt);
  [[nodiscard]] Result<OperationResult>
  commit_curve_document(ObjectId object_id, CurveDocument document,
                        OperationResult receipt);
  [[nodiscard]] Result<ToolPreviewPayload> preview_payload() const;
  [[nodiscard]] Result<ToolPreviewPayload>
  preview_payload(PreviewId preview_id) const;
  [[nodiscard]] Result<SessionPreviewPayload>
  preview_transaction_payload() const;
  [[nodiscard]] Result<SessionPreviewPayload>
  preview_transaction_payload(PreviewId preview_id) const;
  [[nodiscard]] Result<PolygonDocument>
  preview_document_copy(PreviewId preview_id) const;
  [[nodiscard]] Result<PolygonDocument>
  preview_document_copy(PreviewId preview_id, ObjectId object_id) const;
  [[nodiscard]] Result<CurveDocument>
  preview_curve_document_copy(PreviewId preview_id) const;
  [[nodiscard]] Result<CurveDocument>
  preview_curve_document_copy(PreviewId preview_id, ObjectId object_id) const;
  /** Reads an existing participant's immutable transaction base document. */
  [[nodiscard]] Result<PolygonDocument>
  preview_base_document_copy(PreviewId preview_id, ObjectId object_id) const;
  [[nodiscard]] Result<CurveDocument>
  preview_base_curve_document_copy(PreviewId preview_id,
                                   ObjectId object_id) const;
  [[nodiscard]] PreviewTransactionSummary preview_summary() const;
  [[nodiscard]] SessionPreviewWorkStats preview_work_stats() const;
  [[nodiscard]] SessionMutationCheckpointWorkStats
  mutation_checkpoint_work_stats() const;
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
