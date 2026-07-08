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

[[nodiscard]] Result<SemanticOverlayPayload>
build_preview_overlay_payload(const PolygonDocument &document) {
  Result<AuthoredPolygonPayload> authored_result = document.authored_payload();
  if (!authored_result.ok()) {
    return Result<SemanticOverlayPayload>::failure(authored_result.error());
  }

  const AuthoredPolygonPayload &authored = authored_result.value();
  auto position_for = [&](VertexId vertex_id) -> const Vec3 * {
    for (std::size_t index = 0; index < authored.vertices.size(); ++index) {
      if (authored.vertices[index] == vertex_id) {
        return &authored.positions[index];
      }
    }
    return nullptr;
  };

  SemanticOverlayPayload overlay;
  overlay.selection_revision = document.selection_revision();
  for (const AuthoredPolygonFacePayload &face : authored.faces) {
    SemanticOverlayRecord record;
    record.kind = SemanticOverlayKind::ToolPreview;
    record.color_abgr = 0xfff0c36aU;
    for (VertexId vertex_id : face.vertices) {
      if (const Vec3 *position = position_for(vertex_id)) {
        record.points.push_back(*position);
      }
    }
    if (!record.points.empty()) {
      record.points.push_back(record.points.front());
    }
    if (record.points.size() >= 2U) {
      overlay.records.push_back(std::move(record));
    }
  }
  return Result<SemanticOverlayPayload>::success(std::move(overlay));
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
  if (!state) {
    return {};
  }
  if (!state->active) {
    return handle_failure(*state->context,
                          make_error(ErrorCode::InvalidArgument,
                                     "Preview handle is not active."));
  }
  OperationResult operation;
  Result<PolygonDocument> document = build_preview_document(*state, &operation);
  if (!document.ok()) {
    return handle_failure(*state->context, document.error());
  }
  OperationReceipt receipt =
      receipt_from_result(operation, current_revisions(*state->context));
  receipt.dirty.overlays = true;
  return receipt;
}

} // namespace

PreviewHandle::PreviewHandle(std::shared_ptr<ModelingPreviewState> state)
    : state_(std::move(state)) {}

PreviewId PreviewHandle::id() const { return state_ ? state_->id : PreviewId{}; }
bool PreviewHandle::active() const { return state_ && state_->active; }

PreviewTransactionSummary PreviewHandle::summary() const {
  if (!state_) {
    return {};
  }
  return {
      .active = state_->active,
      .object_id = state_->selection.object,
      .operation_id = preview_operation_id(state_->kind),
      .content_revision = current_revisions(*state_->context).content,
  };
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
  ToolPreviewPayload payload;
  payload.mesh = mesh_payload(request);
  payload.overlay = overlay_payload(request);
  payload.valid = !payload.mesh.vertices.empty() || !payload.overlay.records.empty();
  return payload;
}

MeshPayload PreviewHandle::mesh_payload(PayloadRequest request) const {
  if (!state_) {
    return {};
  }
  if (!state_->active) {
    static_cast<void>(handle_failure(
        *state_->context,
        make_error(ErrorCode::InvalidArgument, "Preview handle is not active.")));
    return {};
  }
  Result<PolygonDocument> document = build_preview_document(*state_);
  if (!document.ok()) {
    static_cast<void>(handle_failure(*state_->context, document.error()));
    return {};
  }
  if (request.only_if_changed &&
      document.value().content_revision() <= request.previous_revision) {
    return {};
  }
  Result<MeshPayload> payload = document.value().compile_mesh();
  if (!payload.ok()) {
    static_cast<void>(handle_failure(*state_->context, payload.error()));
    return {};
  }
  return std::move(payload).value();
}

SemanticOverlayPayload PreviewHandle::overlay_payload(PayloadRequest) const {
  if (!state_) {
    return {};
  }
  if (!state_->active) {
    static_cast<void>(handle_failure(
        *state_->context,
        make_error(ErrorCode::InvalidArgument, "Preview handle is not active.")));
    return {};
  }
  Result<PolygonDocument> document = build_preview_document(*state_);
  if (!document.ok()) {
    static_cast<void>(handle_failure(*state_->context, document.error()));
    return {};
  }
  Result<SemanticOverlayPayload> payload =
      build_preview_overlay_payload(document.value());
  if (!payload.ok()) {
    static_cast<void>(handle_failure(*state_->context, payload.error()));
    return {};
  }
  return std::move(payload).value();
}

OperationReceipt PreviewHandle::commit(std::string) {
  if (!state_) {
    return {};
  }
  if (!state_->active) {
    return handle_failure(*state_->context,
                          make_error(ErrorCode::InvalidArgument,
                                     "Preview handle is not active."));
  }
  OperationReceipt receipt;
  switch (state_->kind) {
  case ModelingPreviewKind::BridgeEdges:
    receipt = direct_mutate_selection(
        *state_->context, state_->selection, [&](PolygonDocument &document) {
          return document.bridge_selected_edges(state_->bridge.steps);
        });
    break;
  case ModelingPreviewKind::BridgeFaces:
    receipt = direct_mutate_selection(
        *state_->context, state_->selection, [&](PolygonDocument &document) {
          return document.bridge_selected_faces(state_->bridge.steps);
        });
    break;
  case ModelingPreviewKind::EdgeBevel:
    receipt = direct_mutate_selection(
        *state_->context, state_->selection, [&](PolygonDocument &document) {
          return document.bevel_selected_edges({
              .width = state_->edge_bevel.width,
              .profile = state_->edge_bevel.profile,
              .segments = state_->edge_bevel.segments,
          });
        });
    break;
  case ModelingPreviewKind::MergeByDistance:
    receipt = direct_mutate_selection(
        *state_->context, state_->selection, [&](PolygonDocument &document) {
          return document.merge_selected_vertices_by_distance(
              state_->merge_by_distance.tolerance);
        });
    break;
  case ModelingPreviewKind::SliceQuad:
    receipt = direct_mutate_selection(
        *state_->context, state_->selection, [&](PolygonDocument &document) {
          return document.slice_selected_quads(state_->slice_quad.x_slices,
                                               state_->slice_quad.y_slices);
        });
    break;
  case ModelingPreviewKind::Thicken:
    receipt = direct_mutate_selection(
        *state_->context, state_->selection, [&](PolygonDocument &document) {
          return document.thicken_selected_faces(state_->thicken.thickness,
                                                 state_->thicken.from_center);
        });
    break;
  case ModelingPreviewKind::Transform:
    receipt = direct_mutate_selection(
        *state_->context, state_->selection, [&](PolygonDocument &document) {
          return document.transform_selection(state_->transform.transform);
        });
    break;
  case ModelingPreviewKind::None:
    receipt = unsupported(*state_->context, "Preview has no operation.");
    break;
  }
  state_->active = false;
  return receipt;
}

OperationReceipt PreviewHandle::cancel() {
  if (state_) {
    state_->active = false;
    OperationReceipt receipt;
    receipt.revisions = current_revisions(*state_->context);
    receipt.dirty.overlays = true;
    return receipt;
  }
  return {};
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
