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

template <typename T>
[[nodiscard]] std::vector<T> complement_ids(const std::vector<T> &all,
                                            const std::vector<T> &selected) {
  std::vector<T> result;
  for (const T &id : all) {
    if (std::ranges::find(selected, id) == selected.end()) {
      result.push_back(id);
    }
  }
  return result;
}

} // namespace

Selection::Selection(std::shared_ptr<ModelingApiContext> context,
                     SelectionUnion selection)
    : context_(std::move(context)), selection_(std::move(selection)) {}

SelectionKind Selection::kind() const { return selection_.kind; }
ObjectId Selection::object() const { return selection_.object; }

bool Selection::empty() const { return count() == 0U; }

std::size_t Selection::count() const {
  if (selection_.kind == SelectionKind::Object) {
    return selection_.objects.size();
  }
  if (selection_.kind == SelectionKind::Vertex) {
    return selection_.vertices.size();
  }
  if (selection_.kind == SelectionKind::Edge) {
    return selection_.edges.size();
  }
  return selection_.faces.size();
}

bool Selection::is_object_selection() const {
  return selection_.kind == SelectionKind::Object;
}
bool Selection::is_vertex_selection() const {
  return selection_.kind == SelectionKind::Vertex;
}
bool Selection::is_edge_selection() const {
  return selection_.kind == SelectionKind::Edge;
}
bool Selection::is_face_selection() const {
  return selection_.kind == SelectionKind::Face;
}

OperationReceipt Selection::apply(SelectionEdit edit) const {
  if (selection_.kind == SelectionKind::Object) {
    Result<OperationResult> result =
        context_->session.apply_object_selection(selection_.objects, edit);
    if (!result.ok()) {
      return handle_failure(*context_, result.error());
    }
    return receipt_from_result(result.value(), current_revisions(*context_));
  }
  Result<OperationReceipt> result =
      apply_selection_to_document(*context_, selection_, edit);
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return std::move(result).value();
}

OperationReceipt Selection::replace() const {
  return apply(SelectionEdit::Replace);
}
OperationReceipt Selection::add() const { return apply(SelectionEdit::Add); }
OperationReceipt Selection::remove() const {
  return apply(SelectionEdit::Remove);
}
OperationReceipt Selection::toggle() const {
  return apply(SelectionEdit::Toggle);
}
SelectionUnion Selection::to_union() const { return selection_; }

std::span<const ObjectId> ObjectSelection::objects() const {
  return selection_.objects;
}

OperationReceipt ObjectSelection::delete_objects() const {
  OperationReceipt receipt;
  for (ObjectId object : selection_.objects) {
    Result<OperationResult> result = context_->session.remove_object(object);
    if (!result.ok()) {
      return handle_failure(*context_, result.error());
    }
    receipt = receipt_from_result(result.value(), current_revisions(*context_));
  }
  return receipt;
}

MeshHandle ObjectSelection::duplicate(DuplicateOptions options) const {
  if (selection_.objects.empty()) {
    return handle_value_failure(
        *context_, make_error(ErrorCode::InvalidArgument,
                              "Object selection is empty."),
        MeshHandle{});
  }
  Vec3 offset = options.offset;
  if (options.use_grid_default && offset.x == 0.0F && offset.y == 0.0F &&
      offset.z == 0.0F) {
    offset = {1.0F, 0.0F, 0.0F};
  }
  Result<ObjectId> object =
      context_->session.duplicate_object(selection_.objects.front(), offset);
  if (!object.ok()) {
    return handle_value_failure(*context_, object.error(), MeshHandle{});
  }
  return MeshHandle(context_, object.value());
}

OperationReceipt ObjectSelection::combine() const {
  Result<OperationResult> result =
      context_->session.combine_objects(selection_.objects);
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return receipt_from_result(result.value(), current_revisions(*context_));
}

std::span<const VertexId> VertexSelection::vertices() const {
  return selection_.vertices;
}

OperationReceipt VertexSelection::snap_to_active() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.snap_selected_vertices_to_active();
  });
}

OperationReceipt VertexSelection::merge_to_active() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.merge_selected_vertices_to_active();
  });
}

OperationReceipt VertexSelection::merge_to_center() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.merge_selected_vertices_to_center();
  });
}

PreviewHandle VertexSelection::merge_by_distance(
    MergeByDistanceOptions options) const {
  auto state = std::make_shared<ModelingPreviewState>();
  state->context = context_;
  state->id = make_id<PreviewTag>(1);
  state->kind = ModelingPreviewKind::MergeByDistance;
  state->selection = selection_;
  state->merge_by_distance = options;
  return PreviewHandle(std::move(state));
}

OperationReceipt VertexSelection::remove_doubles(MergeByDistanceOptions) const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.remove_double_vertices();
  });
}

OperationReceipt VertexSelection::bevel(BevelVerticesOptions options) const {
  return direct_mutate_selection(*context_, selection_, [&](PolygonDocument &document) {
    return document.bevel_selected_vertices(options.distance.value_or(0.18F));
  });
}

OperationReceipt VertexSelection::connect() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.connect_selected_vertices();
  });
}

OperationReceipt VertexSelection::dissolve() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.dissolve_selected_vertices();
  });
}

OperationReceipt VertexSelection::radial_align() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.radial_align_selection();
  });
}

std::span<const EdgeKey> EdgeSelection::edges() const { return selection_.edges; }

OperationReceipt EdgeSelection::snap_to_active() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.snap_selected_edges_to_active();
  });
}

OperationReceipt EdgeSelection::connect() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.connect_selected_edges();
  });
}

OperationReceipt EdgeSelection::split() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.split_selected_edges();
  });
}

OperationReceipt EdgeSelection::harden_normals() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.harden_selected_edge_normals();
  });
}

OperationReceipt EdgeSelection::soften_normals() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.soften_selected_edge_normals();
  });
}

PreviewHandle EdgeSelection::bevel(EdgeBevelOptions options) const {
  auto state = std::make_shared<ModelingPreviewState>();
  state->context = context_;
  state->id = make_id<PreviewTag>(1);
  state->kind = ModelingPreviewKind::EdgeBevel;
  state->selection = selection_;
  state->edge_bevel = options;
  return PreviewHandle(std::move(state));
}

OperationReceipt EdgeSelection::bridge(BridgeOptions options) const {
  return direct_mutate_selection(*context_, selection_, [&](PolygonDocument &document) {
    return document.bridge_selected_edges(options.steps);
  });
}

PreviewHandle EdgeSelection::interpolated_bridge(BridgeOptions options) const {
  auto state = std::make_shared<ModelingPreviewState>();
  state->context = context_;
  state->id = make_id<PreviewTag>(1);
  state->kind = ModelingPreviewKind::BridgeEdges;
  state->selection = selection_;
  state->bridge = options;
  return PreviewHandle(std::move(state));
}

OperationReceipt EdgeSelection::bridge_pairs(BridgeOptions options) const {
  return bridge(options);
}

OperationReceipt EdgeSelection::bridge_boundaries(BridgeOptions options) const {
  return bridge(options);
}

OperationReceipt EdgeSelection::dissolve() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.dissolve_selected_edges();
  });
}

OperationReceipt EdgeSelection::merge() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.merge_selected_edges();
  });
}

OperationReceipt EdgeSelection::collapse() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.collapse_selected_edges();
  });
}

OperationReceipt EdgeSelection::fill_hole() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.fill_hole_from_selected_edges();
  });
}

OperationReceipt EdgeSelection::radial_align() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.radial_align_selection();
  });
}

OperationReceipt EdgeSelection::insert_loop(InsertEdgeLoopOptions options) const {
  SelectionUnion loop_selection = selection_;
  if (options.edge.valid()) {
    loop_selection.edges = {options.edge};
  }
  return direct_mutate_selection(*context_, loop_selection,
                                 [&](PolygonDocument &document) {
                                   return document.insert_edge_loop(options.t);
                                 });
}

std::span<const FaceId> FaceSelection::faces() const { return selection_.faces; }

OperationReceipt FaceSelection::bridge(BridgeOptions options) const {
  return direct_mutate_selection(*context_, selection_, [&](PolygonDocument &document) {
    return document.bridge_selected_faces(options.steps);
  });
}

PreviewHandle FaceSelection::interpolated_bridge(BridgeOptions options) const {
  auto state = std::make_shared<ModelingPreviewState>();
  state->context = context_;
  state->id = make_id<PreviewTag>(1);
  state->kind = ModelingPreviewKind::BridgeFaces;
  state->selection = selection_;
  state->bridge = options;
  return PreviewHandle(std::move(state));
}

OperationReceipt FaceSelection::flip_normals() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.flip_selected_face_normals();
  });
}

OperationReceipt
FaceSelection::recalculate_normals(RecalculateNormalsOptions options) const {
  return direct_mutate_selection(*context_, selection_, [&](PolygonDocument &document) {
    return document.recalculate_selected_face_normals(options.outside);
  });
}

OperationReceipt FaceSelection::shade_smooth() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.shade_selected_faces_smooth();
  });
}

OperationReceipt FaceSelection::shade_flat() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.shade_selected_faces_flat();
  });
}

OperationReceipt FaceSelection::combine() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.combine_selected_faces();
  });
}

OperationReceipt FaceSelection::collapse() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.collapse_selected_faces();
  });
}

OperationReceipt FaceSelection::radial_align() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.radial_align_selection();
  });
}

MeshHandle FaceSelection::extract() const {
  Result<PolygonDocument> document = context_->session.document_copy(selection_.object);
  if (!document.ok()) {
    return handle_value_failure(*context_, document.error(), MeshHandle{});
  }
  PolygonDocument working = std::move(document).value();
  Result<OperationResult> selected =
      working.apply_selection(polygon_selection_from_union(selection_));
  if (!selected.ok()) {
    return handle_value_failure(*context_, selected.error(), MeshHandle{});
  }
  Result<ExtractedPolygonDocument> extracted = working.extract_selected_faces();
  if (!extracted.ok()) {
    return handle_value_failure(*context_, extracted.error(), MeshHandle{});
  }
  Result<OperationReceipt> committed =
      commit_document_receipt(*context_, selection_.object, std::move(working),
                              extracted.value().receipt);
  if (!committed.ok()) {
    return handle_value_failure(*context_, committed.error(), MeshHandle{});
  }
  Result<ObjectId> object = context_->session.add_document(
      std::move(extracted.value().document), "Extracted Faces");
  if (!object.ok()) {
    return handle_value_failure(*context_, object.error(), MeshHandle{});
  }
  return MeshHandle(context_, object.value());
}

OperationReceipt FaceSelection::detach() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.detach_selected_faces();
  });
}

PreviewHandle FaceSelection::slice_quads(SliceQuadOptions options) const {
  auto state = std::make_shared<ModelingPreviewState>();
  state->context = context_;
  state->id = make_id<PreviewTag>(1);
  state->kind = ModelingPreviewKind::SliceQuad;
  state->selection = selection_;
  state->slice_quad = options;
  return PreviewHandle(std::move(state));
}

PreviewHandle FaceSelection::thicken(ThickenOptions options) const {
  auto state = std::make_shared<ModelingPreviewState>();
  state->context = context_;
  state->id = make_id<PreviewTag>(1);
  state->kind = ModelingPreviewKind::Thicken;
  state->selection = selection_;
  state->thicken = options;
  return PreviewHandle(std::move(state));
}

OperationReceipt FaceSelection::extrude(ExtrudeOptions options) const {
  return direct_mutate_selection(*context_, selection_, [&](PolygonDocument &document) {
    return document.extrude_selected_elements(options.offset,
                                             options.closed_edge_ledge_size);
  });
}

OperationReceipt FaceSelection::inset(InsetOptions options) const {
  return direct_mutate_selection(*context_, selection_, [&](PolygonDocument &document) {
    return document.inset_selected_elements(options.transform, options.pivot);
  });
}

OperationReceipt FaceSelection::delete_faces() const {
  return direct_mutate_selection(*context_, selection_, [](PolygonDocument &document) {
    return document.delete_selection();
  });
}


SelectionApi::SelectionApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

SelectionEdit SelectionApi::default_edit() const {
  return context_->default_edit;
}

OperationReceipt SelectionApi::set_default_edit(SelectionEdit edit) {
  context_->default_edit = edit;
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

OperationReceipt SelectionApi::clear() {
  Result<OperationResult> objects_cleared =
      context_->session.apply_object_selection(std::span<const ObjectId>{},
                                               SelectionEdit::Replace);
  if (!objects_cleared.ok()) {
    return handle_failure(*context_, objects_cleared.error());
  }
  OperationReceipt receipt =
      receipt_from_result(objects_cleared.value(), current_revisions(*context_));
  for (const SessionObjectSummary &summary : context_->session.objects()) {
    Result<PolygonDocument> document = context_->session.document_copy(summary.id);
    if (!document.ok()) {
      return handle_failure(*context_, document.error());
    }
    PolygonDocument working = std::move(document).value();
    Result<OperationResult> cleared = working.clear_selection();
    if (!cleared.ok()) {
      return handle_failure(*context_, cleared.error());
    }
    Result<OperationReceipt> committed =
        commit_document_receipt(*context_, summary.id, std::move(working),
                                std::move(cleared).value());
    if (!committed.ok()) {
      return handle_failure(*context_, committed.error());
    }
    receipt = std::move(committed).value();
  }
  return receipt;
}

OperationReceipt SelectionApi::select_all() {
  std::vector<MeshHandle> meshes = MeshCollection(context_).all();
  if (meshes.empty()) {
    OperationReceipt receipt;
    receipt.revisions = current_revisions(*context_);
    return receipt;
  }
  return meshes.front().select().replace();
}

OperationReceipt SelectionApi::invert() {
  const SelectionSummary current = summary();
  if (current.vertex_count == 0U && current.edge_count == 0U &&
      current.face_count == 0U) {
    std::vector<ObjectId> inverted;
    for (const SessionObjectSummary &object : context_->session.objects()) {
      if (!object.selected) {
        inverted.push_back(object.id);
      }
    }
    Result<OperationResult> result = context_->session.apply_object_selection(
        inverted, SelectionEdit::Replace);
    if (!result.ok()) {
      return handle_failure(*context_, result.error());
    }
    return receipt_from_result(result.value(), current_revisions(*context_));
  }

  OperationReceipt receipt;
  for (const SessionObjectSummary &object : context_->session.objects()) {
    if (!object.selected) {
      continue;
    }
    Result<PolygonDocument> document =
        context_->session.document_copy(object.id);
    if (!document.ok()) {
      return handle_failure(*context_, document.error());
    }
    PolygonDocument working = std::move(document).value();
    const PolygonSelectionSnapshot snapshot = working.selection();
    PolygonSelectionSnapshot inverted;
    inverted.kind = snapshot.kind;
    if (snapshot.kind == SelectionKind::Vertex) {
      inverted.vertices =
          complement_ids(working.vertex_ids(), snapshot.vertices);
    } else if (snapshot.kind == SelectionKind::Edge) {
      inverted.edges = complement_ids(working.edge_ids(), snapshot.edges);
    } else if (snapshot.kind == SelectionKind::Face) {
      inverted.faces = complement_ids(working.face_ids(), snapshot.faces);
    }
    Result<OperationResult> applied =
        working.apply_selection(inverted, SelectionEdit::Replace);
    if (!applied.ok()) {
      return handle_failure(*context_, applied.error());
    }
    Result<OperationReceipt> committed =
        commit_document_receipt(*context_, object.id, std::move(working),
                                std::move(applied).value());
    if (!committed.ok()) {
      return handle_failure(*context_, committed.error());
    }
    receipt = std::move(committed).value();
  }
  return receipt;
}

ObjectSelection SelectionApi::objects(std::span<const ObjectId> objects,
                                      SelectionEdit edit) {
  ObjectSelection selection = MeshCollection(context_).only(objects);
  static_cast<void>(selection.apply(edit));
  return selection;
}

VertexSelection SelectionApi::vertices(ObjectId object,
                                       std::span<const VertexId> vertices,
                                       SelectionEdit edit) {
  SelectionUnion value;
  value.kind = SelectionKind::Vertex;
  value.object = object;
  value.vertices.assign(vertices.begin(), vertices.end());
  VertexSelection selection(context_, std::move(value));
  static_cast<void>(selection.apply(edit));
  return selection;
}

EdgeSelection SelectionApi::edges(ObjectId object, std::span<const EdgeKey> edges,
                                  SelectionEdit edit) {
  SelectionUnion value;
  value.kind = SelectionKind::Edge;
  value.object = object;
  value.edges.assign(edges.begin(), edges.end());
  EdgeSelection selection(context_, std::move(value));
  static_cast<void>(selection.apply(edit));
  return selection;
}

FaceSelection SelectionApi::faces(ObjectId object, std::span<const FaceId> faces,
                                  SelectionEdit edit) {
  SelectionUnion value;
  value.kind = SelectionKind::Face;
  value.object = object;
  value.faces.assign(faces.begin(), faces.end());
  FaceSelection selection(context_, std::move(value));
  static_cast<void>(selection.apply(edit));
  return selection;
}

PickResult SelectionApi::pick(PickRequest request) {
  if (request.resolved.has_value()) {
    return {.hit = true, .selection = std::move(*request.resolved)};
  }
  static_cast<void>(
      unsupported(*context_, "Portable picking requires host hit-test input."));
  return {};
}

OperationReceipt SelectionApi::pick_select(PickSelectRequest request) {
  PickResult result = pick(std::move(request.pick));
  if (!result.hit) {
    return unsupported(*context_,
                       "Portable pick-select requires host hit-test input.");
  }
  Selection selection(context_, std::move(result.selection));
  return selection.apply(request.edit);
}

BoxSelectionBaseline SelectionApi::capture_box_baseline() {
  return {.summary = summary()};
}

OperationReceipt SelectionApi::preview_box(BoxSelectionRequest request) {
  if (request.resolved.has_value()) {
    OperationReceipt receipt;
    receipt.revisions = current_revisions(*context_);
    receipt.dirty.overlays = true;
    return receipt;
  }
  return unsupported(*context_,
                     "Portable box selection requires host projected targets.");
}

OperationReceipt SelectionApi::commit_box(BoxSelectionRequest request) {
  if (request.resolved.has_value()) {
    Selection selection(context_, std::move(*request.resolved));
    return selection.apply(request.edit);
  }
  return unsupported(*context_,
                     "Portable box selection requires host projected targets.");
}

HoverResult SelectionApi::preview_hover(HoverRequest request) {
  if (request.resolved.has_value()) {
    return {.hit = true, .target = {.selection = std::move(*request.resolved)}};
  }
  static_cast<void>(
      unsupported(*context_, "Portable hover requires host hit-test input."));
  return {};
}

OperationReceipt SelectionApi::apply_hover(HoverTarget target) {
  if (target.selection.kind == SelectionKind::Object) {
    ObjectSelection selection(context_, std::move(target.selection));
    return selection.replace();
  }
  if (target.selection.kind == SelectionKind::Vertex) {
    VertexSelection selection(context_, std::move(target.selection));
    return selection.replace();
  }
  if (target.selection.kind == SelectionKind::Edge) {
    EdgeSelection selection(context_, std::move(target.selection));
    return selection.replace();
  }
  FaceSelection selection(context_, std::move(target.selection));
  return selection.replace();
}

OperationReceipt SelectionApi::clear_hover() {
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

SelectionSummary SelectionApi::summary() const {
  SelectionSummary result;
  for (const SessionObjectSummary &object : context_->session.objects()) {
    if (object.selected) {
      result.objects.push_back(object.id);
    }
    result.selection_revision =
        std::max(result.selection_revision, object.selection_revision);
    Result<PolygonDocument> document = context_->session.document_copy(object.id);
    if (!document.ok()) {
      continue;
    }
    const PolygonSelectionSnapshot snapshot = document.value().selection();
    result.vertex_count += snapshot.vertices.size();
    result.edge_count += snapshot.edges.size();
    result.face_count += snapshot.faces.size();
  }
  return result;
}


} // namespace quader::modeling
