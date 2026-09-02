////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "public_api_detail.hpp"

namespace quader::modeling {
using namespace detail;
namespace {

[[nodiscard]] const char *preview_operation_id(ModelingPreviewKind kind) {
  switch (kind) {
  case ModelingPreviewKind::BridgeEdges:
    return "modeling.bridge_edges";
  case ModelingPreviewKind::BridgeFaces:
    return "modeling.bridge_faces";
  case ModelingPreviewKind::EdgeBevel:
    return "modeling.bevel_edges";
  case ModelingPreviewKind::MergeByDistance:
    return "modeling.merge_vertices_by_distance";
  case ModelingPreviewKind::SliceQuad:
    return "modeling.slice_quads";
  case ModelingPreviewKind::Thicken:
    return "modeling.thicken_faces";
  case ModelingPreviewKind::Transform:
    return "modeling.transform_selection";
  case ModelingPreviewKind::None:
    return "modeling.preview";
  }
  return "modeling.preview";
}

void mark_as_tool_preview(SemanticOverlayPayload &overlay) {
  for (SemanticOverlayRecord &record : overlay.records) {
    record.kind = SemanticOverlayKind::ToolPreview;
  }
}

[[nodiscard]] Result<OperationResult>
apply_preview_operation(const ModelingPreviewState &state,
                        PolygonDocument &document) {
  Result<OperationResult> selected = document.apply_selection(
      polygon_selection_from_union(state.selection), SelectionEdit::Replace);
  if (!selected.ok()) {
    return selected;
  }

  switch (state.kind) {
  case ModelingPreviewKind::BridgeEdges:
    return document.bridge_selected_edges(state.bridge.steps);
  case ModelingPreviewKind::BridgeFaces:
    return document.bridge_selected_faces(state.bridge.steps);
  case ModelingPreviewKind::EdgeBevel:
    return document.bevel_selected_edges({
        .width = state.edge_bevel.width,
        .profile = state.edge_bevel.profile,
        .segments = state.edge_bevel.segments,
    });
  case ModelingPreviewKind::MergeByDistance:
    return document.merge_selected_vertices_by_distance(
        state.merge_by_distance.tolerance);
  case ModelingPreviewKind::SliceQuad:
    return document.slice_selected_quads(state.slice_quad.x_slices,
                                         state.slice_quad.y_slices);
  case ModelingPreviewKind::Thicken:
    return document.thicken_selected_faces(state.thicken.thickness,
                                           state.thicken.from_center);
  case ModelingPreviewKind::Transform:
    return document.transform_selection(state.transform.transform);
  case ModelingPreviewKind::None:
    return Result<OperationResult>::failure(
        unsupported_error("Preview has no operation."));
  }
  return Result<OperationResult>::failure(
      unsupported_error("Preview has no operation."));
}

[[nodiscard]] Result<PolygonDocument>
build_preview_document(const ModelingPreviewState &state,
                       OperationResult *receipt = nullptr) {
  Result<PolygonDocument> document =
      state.context->session.document_copy(state.selection.object);
  if (!document.ok()) {
    return Result<PolygonDocument>::failure(document.error());
  }

  PolygonDocument working = std::move(document).value();
  Result<OperationResult> operation = apply_preview_operation(state, working);
  if (!operation.ok()) {
    return Result<PolygonDocument>::failure(operation.error());
  }
  if (receipt != nullptr) {
    *receipt = operation.value();
  }
  return Result<PolygonDocument>::success(std::move(working));
}

[[nodiscard]] OperationReceipt preview_update_receipt(
    const std::shared_ptr<ModelingPreviewState> &state) {
  if (!state || !state->context) {
    return {};
  }
  if (!state->context->session.preview_active(state->id)) {
    return handle_failure(*state->context,
                          make_error(ErrorCode::InvalidArgument,
                                     "Preview handle is not active."));
  }
  OperationResult operation;
  Result<PolygonDocument> document = build_preview_document(*state, &operation);
  if (!document.ok()) {
    return handle_failure(*state->context, document.error());
  }
  Result<OperationResult> updated = state->context->session.update_document_preview(
      state->id, std::move(document).value(), std::move(operation));
  if (!updated.ok()) {
    return handle_failure(*state->context, updated.error());
  }
  const PreviewTransactionSummary summary =
      state->context->session.preview_summary();
  state->payload_revision = summary.content_revision;
  return receipt_from_result(updated.value(),
                             current_revisions(*state->context));
}

} // namespace

ModelingPreviewState::~ModelingPreviewState() {
  if (context && context->session.preview_active(id)) {
    static_cast<void>(context->session.cancel_preview(id));
  }
}

PreviewHandle::PreviewHandle(std::shared_ptr<ModelingPreviewState> state)
    : state_(std::move(state)) {
  if (!state_ || !state_->context) {
    return;
  }
  state_->id = {};
  OperationResult operation;
  Result<PolygonDocument> document = build_preview_document(*state_, &operation);
  if (!document.ok()) {
    record_error(*state_->context, document.error());
    return;
  }
  Result<PreviewId> begun = state_->context->session.begin_document_preview(
      state_->selection.object, preview_operation_id(state_->kind),
      std::move(document).value(), std::move(operation));
  if (!begun.ok()) {
    record_error(*state_->context, begun.error());
    return;
  }
  remember_selection_interaction(*state_->context, state_->selection);
  state_->id = begun.value();
  state_->payload_revision =
      state_->context->session.preview_summary().content_revision;
}

PreviewId PreviewHandle::id() const { return state_ ? state_->id : PreviewId{}; }
bool PreviewHandle::active() const {
  return state_ && state_->context &&
         state_->context->session.preview_active(state_->id);
}

PreviewTransactionSummary PreviewHandle::summary() const {
  if (!active()) {
    return {};
  }
  return state_->context->session.preview_summary();
}

OperationReceipt PreviewHandle::update(BridgeOptions options) {
  if (state_) {
    state_->bridge = options;
  }
  return preview_update_receipt(state_);
}

OperationReceipt PreviewHandle::update(EdgeBevelOptions options) {
  if (state_) {
    state_->edge_bevel = options;
  }
  return preview_update_receipt(state_);
}

OperationReceipt PreviewHandle::update(ThickenOptions options) {
  if (state_) {
    state_->thicken = options;
  }
  return preview_update_receipt(state_);
}

OperationReceipt PreviewHandle::update(SliceQuadOptions options) {
  if (state_) {
    state_->slice_quad = options;
  }
  return preview_update_receipt(state_);
}

OperationReceipt PreviewHandle::update(MergeByDistanceOptions options) {
  if (state_) {
    state_->merge_by_distance = options;
  }
  return preview_update_receipt(state_);
}

OperationReceipt PreviewHandle::update(TransformOptions options) {
  if (state_) {
    state_->transform = options;
  }
  return preview_update_receipt(state_);
}

ToolPreviewPayload PreviewHandle::payload(PayloadRequest request) const {
  if (!state_ || !state_->context) {
    return {};
  }
  if (!require_owned_snapshot_lifetime(*state_->context, request.lifetime,
                                       "Preview payload")) {
    return {};
  }
  if (!active()) {
    static_cast<void>(handle_failure(
        *state_->context,
        make_error(ErrorCode::InvalidArgument, "Preview handle is not active.")));
    return {};
  }
  if (request.only_if_changed &&
      state_->payload_revision <= request.previous_revision) {
    return {};
  }
  Result<ToolPreviewPayload> payload =
      state_->context->session.preview_payload(state_->id);
  if (!payload.ok()) {
    static_cast<void>(handle_failure(*state_->context, payload.error()));
    return {};
  }
  ToolPreviewPayload result = std::move(payload).value();
  mark_as_tool_preview(result.overlay);
  result.valid =
      !result.mesh.vertices.empty() || !result.overlay.records.empty();
  return result;
}

MeshPayload PreviewHandle::mesh_payload(PayloadRequest request) const {
  if (!state_ || !state_->context) {
    return {};
  }
  if (!active()) {
    static_cast<void>(handle_failure(
        *state_->context,
        make_error(ErrorCode::InvalidArgument, "Preview handle is not active.")));
    return {};
  }
  if (!require_owned_snapshot_lifetime(*state_->context, request.lifetime,
                                       "Preview mesh payload")) {
    return {};
  }
  if (request.only_if_changed &&
      state_->payload_revision <= request.previous_revision) {
    return {};
  }
  Result<ToolPreviewPayload> payload =
      state_->context->session.preview_payload(state_->id);
  if (!payload.ok()) {
    static_cast<void>(handle_failure(*state_->context, payload.error()));
    return {};
  }
  return std::move(payload).value().mesh;
}

SemanticOverlayPayload
PreviewHandle::overlay_payload(PayloadRequest request) const {
  if (!state_ || !state_->context) {
    return {};
  }
  if (!active()) {
    static_cast<void>(handle_failure(
        *state_->context,
        make_error(ErrorCode::InvalidArgument, "Preview handle is not active.")));
    return {};
  }
  if (!require_owned_snapshot_lifetime(*state_->context, request.lifetime,
                                       "Preview overlay payload")) {
    return {};
  }
  if (request.only_if_changed &&
      state_->payload_revision <= request.previous_revision) {
    return {};
  }
  Result<ToolPreviewPayload> payload =
      state_->context->session.preview_payload(state_->id);
  if (!payload.ok()) {
    static_cast<void>(handle_failure(*state_->context, payload.error()));
    return {};
  }
  SemanticOverlayPayload result = std::move(payload).value().overlay;
  mark_as_tool_preview(result);
  return result;
}

OperationReceipt PreviewHandle::commit(std::string) {
  if (!state_ || !state_->context) {
    return {};
  }
  if (!active()) {
    return handle_failure(*state_->context,
                          make_error(ErrorCode::InvalidArgument,
                                     "Preview handle is not active."));
  }
  Result<OperationResult> committed =
      state_->context->session.commit_preview(state_->id);
  if (!committed.ok()) {
    return handle_failure(*state_->context, committed.error());
  }
  return receipt_from_result(committed.value(),
                             current_revisions(*state_->context));
}

OperationReceipt PreviewHandle::cancel() {
  if (!state_ || !state_->context) {
    return {};
  }
  Result<OperationResult> canceled =
      state_->context->session.cancel_preview(state_->id);
  if (!canceled.ok()) {
    return handle_failure(*state_->context, canceled.error());
  }
  return receipt_from_result(canceled.value(),
                             current_revisions(*state_->context));
}


PreviewApi::PreviewApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}
bool PreviewApi::active() const { return context_->session.preview_active(); }
PreviewTransactionSummary PreviewApi::summary() const {
  return context_->session.preview_summary();
}
OperationReceipt PreviewApi::commit(std::string) {
  Result<OperationResult> result = context_->session.commit_preview();
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return receipt_from_result(result.value(), current_revisions(*context_));
}
OperationReceipt PreviewApi::cancel() {
  Result<OperationResult> result = context_->session.cancel_preview();
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return receipt_from_result(result.value(), current_revisions(*context_));
}


} // namespace quader::modeling
