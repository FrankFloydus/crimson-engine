////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "public_api_detail.hpp"

#include "polygon_document_native.hpp"

#include <mesh/polygon/document_operations.hpp>

namespace quader::modeling {
using namespace detail;
namespace {

[[nodiscard]] quader::QVec3 native_vec3(Vec3 value) {
  return {value.x, value.y, value.z};
}

[[nodiscard]] quader_poly::PlaneCutMode native_cut_mode(CutKeepMode mode) {
  if (mode == CutKeepMode::DiscardLeft) {
    return quader_poly::PlaneCutMode::KeepBack;
  }
  if (mode == CutKeepMode::DiscardRight) {
    return quader_poly::PlaneCutMode::KeepFront;
  }
  return quader_poly::PlaneCutMode::KeepBoth;
}

[[nodiscard]] quader_poly::PlaneCutRequest
native_plane_cut_request(PlaneCutOptions options) {
  return {
      .first_point = native_vec3(options.a),
      .second_point = native_vec3(options.b),
      .third_point = native_vec3(options.c),
      .mode = native_cut_mode(options.keep),
  };
}

[[nodiscard]] OperationResult plane_cut_receipt(std::string message) {
  OperationResult result;
  result.success = true;
  result.changed = true;
  result.message = std::move(message);
  result.dirty.topology = true;
  result.dirty.geometry = true;
  result.dirty.selection = true;
  return result;
}

[[nodiscard]] SelectionUnion current_component_selection(
    ModelingApiContext &context, MeshHandle mesh) {
  SelectionUnion selection;
  selection.object = mesh.id();
  Result<PolygonDocument> document = context.session.document_copy(mesh.id());
  if (!document.ok()) {
    return all_face_selection(mesh);
  }
  const PolygonSelectionSnapshot snapshot = document.value().selection();
  selection.kind = snapshot.kind;
  selection.vertices = snapshot.vertices;
  selection.edges = snapshot.edges;
  selection.faces = snapshot.faces;
  if (selection.vertices.empty() && selection.edges.empty() &&
      selection.faces.empty()) {
    return all_face_selection(mesh);
  }
  return selection;
}

[[nodiscard]] Transform3 mirror_transform(Axis axis) {
  Transform3 transform;
  if (axis == Axis::X) {
    transform.x_axis.x = -1.0F;
  } else if (axis == Axis::Y) {
    transform.y_axis.y = -1.0F;
  } else {
    transform.z_axis.z = -1.0F;
  }
  return transform;
}

[[nodiscard]] bool nonzero(Vec3 value) {
  return value.x != 0.0F || value.y != 0.0F || value.z != 0.0F;
}

[[nodiscard]] Result<OperationResult>
translate_whole_document(PolygonDocument &document, Vec3 offset) {
  if (!nonzero(offset)) {
    OperationResult result;
    result.success = true;
    result.changed = false;
    result.message = "Paste offset was zero.";
    return Result<OperationResult>::success(std::move(result));
  }
  Result<OperationResult> selected = document.select_all_faces();
  if (!selected.ok()) {
    return selected;
  }
  Result<OperationResult> translated = document.translate_selection(offset);
  if (!translated.ok()) {
    return translated;
  }
  static_cast<void>(document.clear_selection());
  return translated;
}

[[nodiscard]] OperationReceipt clean_receipt(const ModelingApiContext &context,
                                             bool changed) {
  OperationReceipt receipt;
  receipt.success = true;
  receipt.changed = changed;
  receipt.revisions = current_revisions(context);
  return receipt;
}

[[nodiscard]] bool repeatable_command(std::string_view id) {
  return id != "copy_selection" && id != "paste_selection" &&
         id != "repeat_last_action" && id != "delete_selection";
}

} // namespace



OperationsApi::OperationsApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

std::vector<CommandDescriptor> OperationsApi::catalog() const {
  return {
      {"delete_selection", "Delete Selection", "Selection"},
      {"copy_selection", "Copy Selection", "Selection"},
      {"paste_selection", "Paste Selection", "Selection"},
      {"repeat_last_action", "Repeat Last Action", "Selection"},
      {"duplicate_meshes", "Duplicate Meshes", "Object"},
      {"combine_meshes", "Combine Meshes", "Object"},
      {"translate_selection", "Translate Selection", "Transform"},
      {"modeling.translate_selection", "Translate Selection", "Transform"},
      {"transform_selection", "Transform Selection", "Transform"},
      {"rotate_selection", "Rotate Selection", "Transform"},
      {"scale_selection", "Scale Selection", "Transform"},
      {"extrude_elements", "Extrude Elements", "Element"},
      {"inset_elements", "Inset Elements", "Element"},
      {"mirror_selection_x", "Mirror Selection X", "Transform"},
      {"mirror_selection_y", "Mirror Selection Y", "Transform"},
      {"mirror_selection_z", "Mirror Selection Z", "Transform"},
      {"flip_horizontal", "Flip Horizontal", "Transform"},
      {"flip_vertical", "Flip Vertical", "Transform"},
      {"invert_mesh_normals", "Invert Mesh Normals", "Mesh"},
      {"shade_smooth_mesh", "Shade Smooth Mesh", "Mesh"},
      {"shade_flat_mesh", "Shade Flat Mesh", "Mesh"},
      {"create_outer_hull", "Create Outer Hull", "Mesh", true, "", true},
      {"snap_vertices_to_active", "Snap Vertices To Active", "Vertex"},
      {"merge_vertices_to_active", "Merge Vertices To Active", "Vertex"},
      {"merge_vertices_to_center", "Merge Vertices To Center", "Vertex"},
      {"merge_vertices_by_distance", "Merge Vertices By Distance", "Vertex",
       true, "", true},
      {"remove_doubles", "Remove Doubles", "Vertex"},
      {"bevel_vertices", "Bevel Vertices", "Vertex"},
      {"connect_vertices", "Connect Vertices", "Vertex"},
      {"dissolve_vertices", "Dissolve Vertices", "Vertex"},
      {"radial_align_vertices", "Radial Align Vertices", "Vertex"},
      {"snap_edges_to_active", "Snap Edges To Active", "Edge"},
      {"connect_edges", "Connect Edges", "Edge"},
      {"split_edges", "Split Edges", "Edge"},
      {"harden_edge_normals", "Harden Edge Normals", "Edge"},
      {"soften_edge_normals", "Soften Edge Normals", "Edge"},
      {"bevel_edges", "Bevel Edges", "Edge", true, "", true},
      {"bridge_edges", "Bridge Edges", "Edge"},
      {"interpolated_bridge_edges", "Interpolated Bridge Edges", "Edge", true,
       "", true},
      {"bridge_edge_pairs", "Bridge Edge Pairs", "Edge"},
      {"bridge_edge_boundaries", "Bridge Edge Boundaries", "Edge"},
      {"dissolve_edges", "Dissolve Edges", "Edge"},
      {"merge_edges", "Merge Edges", "Edge"},
      {"collapse_edges", "Collapse Edges", "Edge"},
      {"fill_hole", "Fill Hole", "Edge"},
      {"radial_align_edges", "Radial Align Edges", "Edge"},
      {"insert_edge_loop", "Insert Edge Loop", "Edge"},
      {"bridge_faces", "Bridge Faces", "Face"},
      {"interpolated_bridge_faces", "Interpolated Bridge Faces", "Face", true,
       "", true},
      {"flip_face_normals", "Flip Face Normals", "Face"},
      {"recalculate_face_normals", "Recalculate Face Normals", "Face"},
      {"shade_faces_smooth", "Shade Faces Smooth", "Face"},
      {"shade_faces_flat", "Shade Faces Flat", "Face"},
      {"combine_faces", "Combine Faces", "Face"},
      {"collapse_faces", "Collapse Faces", "Face"},
      {"radial_align_faces", "Radial Align Faces", "Face"},
      {"slice_quads", "Slice Quads", "Face", true, "", true},
      {"thicken_faces", "Thicken Faces", "Face", true, "", true},
      {"extrude_faces", "Extrude Faces", "Face"},
      {"inset_faces", "Inset Faces", "Face"},
      {"extract_faces", "Extract Faces", "Face"},
      {"detach_faces", "Detach Faces", "Face"},
      {"assign_face_material_slot", "Assign Face Material Slot", "Material"},
      {"plane_cut", "Plane Cut", "Cutting"},
      {"knife_segment", "Knife Segment", "Cutting"},
      {"knife_stroke", "Knife Stroke", "Cutting"},
  };
}

CommandDescriptor OperationsApi::describe(std::string_view id) const {
  for (const CommandDescriptor &descriptor : catalog()) {
    if (descriptor.id == id) {
      return descriptor;
    }
  }
  return {};
}

OperationReceipt OperationsApi::execute(std::string_view id,
                                        OperationSettings settings) {
  auto finish = [&](OperationReceipt receipt) {
    if (repeatable_command(id)) {
      record_repeatable_operation(*context_, std::string(id), settings,
                                  receipt);
    }
    return receipt;
  };

  if (id == "modeling.translate_selection" || id == "translate_selection") {
    return finish(translate_selection(settings.delta));
  }
  if (id == "delete_selection") {
    return finish(delete_selection());
  }
  if (id == "copy_selection") {
    return finish(copy_selection());
  }
  if (id == "paste_selection") {
    return finish(paste_selection(settings.paste));
  }
  if (id == "repeat_last_action") {
    return finish(repeat_last_action());
  }
  if (id == "duplicate_meshes") {
    MeshHandle duplicated = duplicate_meshes(settings.duplicate);
    if (!duplicated.valid()) {
      if (context_->last_error.has_value()) {
        return failed_receipt(*context_->last_error,
                              current_revisions(*context_));
      }
      return unsupported(*context_, "No selected mesh.");
    }
    OperationReceipt receipt;
    receipt.changed = true;
    receipt.revisions = current_revisions(*context_);
    receipt.dirty.topology = true;
    receipt.dirty.geometry = true;
    return finish(receipt);
  }
  if (id == "combine_meshes") {
    return finish(combine_meshes());
  }
  if (id == "transform_selection") {
    return finish(transform_selection(settings.transform));
  }
  if (id == "rotate_selection") {
    return finish(rotate_selection(settings.rotate));
  }
  if (id == "scale_selection") {
    return finish(scale_selection(settings.scale));
  }
  if (id == "extrude_elements" || id == "extrude_faces") {
    return finish(extrude(settings.extrude));
  }
  if (id == "inset_elements" || id == "inset_faces") {
    return finish(inset(settings.inset));
  }
  if (id == "mirror_selection_x") {
    return finish(mirror_selection(Axis::X));
  }
  if (id == "mirror_selection_y") {
    return finish(mirror_selection(Axis::Y));
  }
  if (id == "mirror_selection_z") {
    return finish(mirror_selection(Axis::Z));
  }
  if (id == "mirror_selection") {
    return finish(mirror_selection(settings.axis));
  }
  if (id == "flip_horizontal") {
    return finish(flip_horizontal());
  }
  if (id == "flip_vertical") {
    return finish(flip_vertical());
  }
  if (id == "invert_mesh_normals") {
    return finish(invert_mesh_normals());
  }
  if (id == "shade_smooth_mesh") {
    return finish(shade_smooth_mesh());
  }
  if (id == "shade_flat_mesh") {
    return finish(shade_flat_mesh());
  }
  if (id == "create_outer_hull") {
    return finish(create_outer_hull(settings.thicken).commit("Create Outer Hull"));
  }
  if (id == "snap_vertices_to_active") {
    return finish(snap_vertices_to_active());
  }
  if (id == "merge_vertices_to_active") {
    return finish(merge_vertices_to_active());
  }
  if (id == "merge_vertices_to_center") {
    return finish(merge_vertices_to_center());
  }
  if (id == "merge_vertices_by_distance") {
    return finish(
        merge_by_distance(settings.merge_by_distance).commit("Merge By Distance"));
  }
  if (id == "remove_doubles") {
    return finish(remove_doubles(settings.merge_by_distance));
  }
  if (id == "bevel_vertices") {
    return finish(bevel_vertices(settings.bevel_vertices));
  }
  if (id == "connect_vertices") {
    return finish(connect_vertices());
  }
  if (id == "dissolve_vertices") {
    return finish(dissolve_vertices());
  }
  if (id == "radial_align_vertices") {
    return finish(radial_align_vertices());
  }
  if (id == "snap_edges_to_active") {
    return finish(snap_edges_to_active());
  }
  if (id == "connect_edges") {
    return finish(connect_edges());
  }
  if (id == "split_edges") {
    return finish(split_edges());
  }
  if (id == "harden_edge_normals") {
    return finish(harden_edge_normals());
  }
  if (id == "soften_edge_normals") {
    return finish(soften_edge_normals());
  }
  if (id == "bevel_edges") {
    return finish(bevel_edges(settings.edge_bevel).commit("Bevel Edges"));
  }
  if (id == "interpolated_bridge_edges") {
    return finish(interpolated_bridge_edges(settings.bridge)
                      .commit("Interpolated Bridge Edges"));
  }
  if (id == "bridge_edges" || id == "bridge_edge_pairs" ||
      id == "bridge_edge_boundaries") {
    return finish(bridge_edges(settings.bridge));
  }
  if (id == "dissolve_edges") {
    return finish(dissolve_edges());
  }
  if (id == "merge_edges") {
    return finish(merge_edges());
  }
  if (id == "collapse_edges") {
    return finish(collapse_edges());
  }
  if (id == "fill_hole") {
    return finish(fill_hole());
  }
  if (id == "radial_align_edges") {
    return finish(radial_align_edges());
  }
  if (id == "insert_edge_loop") {
    return finish(insert_edge_loop(settings.insert_edge_loop));
  }
  if (id == "bridge_faces") {
    return finish(bridge_faces(settings.bridge));
  }
  if (id == "interpolated_bridge_faces") {
    return finish(interpolated_bridge_faces(settings.bridge)
                      .commit("Interpolated Bridge Faces"));
  }
  if (id == "flip_face_normals") {
    return finish(invert_faces());
  }
  if (id == "recalculate_face_normals") {
    MeshHandle mesh = MeshCollection(context_).first_selected();
    return finish(mesh.valid() ? mesh.faces().selected().recalculate_normals()
                               : unsupported(*context_, "No selected mesh."));
  }
  if (id == "shade_faces_smooth") {
    MeshHandle mesh = MeshCollection(context_).first_selected();
    return finish(mesh.valid() ? mesh.faces().selected().shade_smooth()
                               : unsupported(*context_, "No selected mesh."));
  }
  if (id == "shade_faces_flat") {
    MeshHandle mesh = MeshCollection(context_).first_selected();
    return finish(mesh.valid() ? mesh.faces().selected().shade_flat()
                               : unsupported(*context_, "No selected mesh."));
  }
  if (id == "combine_faces") {
    return finish(combine_faces());
  }
  if (id == "collapse_faces") {
    return finish(collapse_faces());
  }
  if (id == "radial_align_faces") {
    return finish(radial_align_faces());
  }
  if (id == "slice_quads") {
    return finish(slice_quad(settings.slice_quad).commit("Slice Quads"));
  }
  if (id == "thicken_faces") {
    return finish(thicken_faces(settings.thicken).commit("Thicken Faces"));
  }
  if (id == "extract_faces") {
    return finish(extract_faces());
  }
  if (id == "detach_faces") {
    return finish(detach_faces());
  }
  if (id == "assign_face_material_slot") {
    return finish(assign_material_slot_to_selection(settings.material_slot));
  }
  if (id == "plane_cut") {
    return finish(plane_cut(settings.plane_cut));
  }
  if (id == "knife_segment") {
    return finish(knife_segment(settings.knife_segment));
  }
  if (id == "knife_stroke") {
    return finish(knife_stroke(settings.knife_stroke));
  }
  return unsupported(*context_, "Unknown modeling operation command.");
}

OperationReceipt OperationsApi::delete_selection() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().delete_faces()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::copy_selection() {
  context_->clipboard.clear();
  for (const SessionObjectSummary &summary : context_->session.objects()) {
    if (!summary.selected) {
      continue;
    }
    Result<PolygonDocument> document =
        context_->session.document_copy(summary.id);
    if (!document.ok()) {
      return handle_failure(*context_, document.error());
    }
    PolygonDocument copied = std::move(document).value();
    static_cast<void>(copied.clear_selection());
    context_->clipboard.push_back({
        .name = summary.name,
        .document = std::move(copied),
    });
  }
  if (context_->clipboard.empty()) {
    return handle_failure(
        *context_,
        make_error(ErrorCode::InvalidArgument,
                   "Select one or more meshes before copying."));
  }
  return clean_receipt(*context_, false);
}

OperationReceipt OperationsApi::paste_selection(PasteOptions options) {
  if (context_->clipboard.empty()) {
    return handle_failure(
        *context_,
        make_error(ErrorCode::InvalidArgument,
                   "Copy one or more meshes before pasting."));
  }

  const std::size_t undo_mark = context_->session.undo_depth();
  Result<OperationResult> cleared = context_->session.apply_object_selection(
      std::span<const ObjectId>{}, SelectionEdit::Replace);
  if (!cleared.ok()) {
    return handle_failure(*context_, cleared.error());
  }

  std::vector<ObjectId> pasted;
  pasted.reserve(context_->clipboard.size());
  for (const ModelingClipboardObject &object : context_->clipboard) {
    PolygonDocument document = object.document;
    Result<OperationResult> translated =
        translate_whole_document(document, options.offset);
    if (!translated.ok()) {
      static_cast<void>(context_->session.rollback_undo_since(undo_mark));
      return handle_failure(*context_, translated.error());
    }
    Result<ObjectId> added =
        context_->session.add_document(std::move(document), object.name);
    if (!added.ok()) {
      static_cast<void>(context_->session.rollback_undo_since(undo_mark));
      return handle_failure(*context_, added.error());
    }
    pasted.push_back(added.value());
  }

  Result<OperationResult> selected =
      context_->session.apply_object_selection(pasted, SelectionEdit::Replace);
  if (!selected.ok()) {
    static_cast<void>(context_->session.rollback_undo_since(undo_mark));
    return handle_failure(*context_, selected.error());
  }
  context_->session.squash_undo_since(undo_mark);

  OperationReceipt receipt =
      receipt_from_result(selected.value(), current_revisions(*context_));
  receipt.changed = true;
  receipt.dirty.topology = true;
  receipt.dirty.geometry = true;
  receipt.dirty.selection = true;
  return receipt;
}

OperationReceipt OperationsApi::repeat_last_action() {
  if (!context_->repeat_operation.has_value()) {
    return handle_failure(
        *context_,
        make_error(ErrorCode::InvalidArgument,
                   "No repeatable action is available."));
  }

  const ModelingRepeatOperation operation = *context_->repeat_operation;
  context_->repeating_operation = true;
  try {
    OperationReceipt receipt = execute(operation.id, operation.settings);
    context_->repeating_operation = false;
    return receipt;
  } catch (...) {
    context_->repeating_operation = false;
    throw;
  }
}

MeshHandle OperationsApi::duplicate_meshes(DuplicateOptions options) {
  return MeshCollection(context_).duplicate_selected(options);
}

OperationReceipt OperationsApi::combine_meshes() {
  return MeshCollection(context_).combine_selected();
}

OperationReceipt OperationsApi::translate_selection(Vec3 delta) {
  Result<OperationResult> result = context_->session.translate_selection(delta);
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return receipt_from_result(result.value(), current_revisions(*context_));
}

OperationReceipt OperationsApi::transform_selection(TransformOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.transform().apply(options)
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::rotate_selection(RotateOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.transform().rotate(options)
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::scale_selection(ScaleOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.transform().scale(options)
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::extrude(ExtrudeOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().extrude(options)
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::inset(InsetOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().inset(options)
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::mirror_selection(Axis axis) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  if (!mesh.valid()) {
    return unsupported(*context_, "No selected mesh.");
  }
  const SelectionUnion selection = current_component_selection(*context_, mesh);
  const Transform3 transform = mirror_transform(axis);
  return direct_mutate_selection(*context_, selection,
                                 [&](PolygonDocument &document) {
                                   return document.transform_selection(transform);
                                 });
}

OperationReceipt OperationsApi::flip_horizontal() {
  return mirror_selection(Axis::X);
}

OperationReceipt OperationsApi::flip_vertical() {
  return mirror_selection(Axis::Y);
}

OperationReceipt OperationsApi::invert_mesh_normals() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().all().flip_normals()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::shade_smooth_mesh() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().all().shade_smooth()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::shade_flat_mesh() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().all().shade_flat()
                      : unsupported(*context_, "No selected mesh.");
}

PreviewHandle OperationsApi::create_outer_hull(ThickenOptions options) {
  return thicken_faces(options);
}

OperationReceipt OperationsApi::snap_vertices_to_active() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.vertices().selected().snap_to_active()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::merge_vertices_to_active() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.vertices().selected().merge_to_active()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::merge_vertices_to_center() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.vertices().selected().merge_to_center()
                      : unsupported(*context_, "No selected mesh.");
}

PreviewHandle
OperationsApi::merge_by_distance(MergeByDistanceOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.vertices().selected().merge_by_distance(options)
                      : handle_value_failure(
                            *context_, unsupported_error("No selected mesh."),
                            PreviewHandle{});
}

OperationReceipt
OperationsApi::remove_doubles(MergeByDistanceOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.vertices().selected().remove_doubles(options)
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt
OperationsApi::bevel_vertices(BevelVerticesOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.vertices().selected().bevel(options)
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::connect_vertices() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.vertices().selected().connect()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::dissolve_vertices() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.vertices().selected().dissolve()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::radial_align_vertices() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.vertices().selected().radial_align()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::snap_edges_to_active() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().snap_to_active()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::connect_edges() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().connect()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::split_edges() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().split()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::harden_edge_normals() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().harden_normals()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::soften_edge_normals() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().soften_normals()
                      : unsupported(*context_, "No selected mesh.");
}

PreviewHandle OperationsApi::bevel_edges(EdgeBevelOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().bevel(options)
                      : handle_value_failure(
                            *context_, unsupported_error("No selected mesh."),
                            PreviewHandle{});
}

OperationReceipt OperationsApi::bridge_edges(BridgeOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().bridge(options)
                      : unsupported(*context_, "No selected mesh.");
}

PreviewHandle
OperationsApi::interpolated_bridge_edges(BridgeOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().interpolated_bridge(options)
                      : handle_value_failure(
                            *context_, unsupported_error("No selected mesh."),
                            PreviewHandle{});
}

OperationReceipt OperationsApi::dissolve_edges() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().dissolve()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::merge_edges() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().merge()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::collapse_edges() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().collapse()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::fill_hole() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().fill_hole()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::radial_align_edges() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().radial_align()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::bridge_faces(BridgeOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().bridge(options)
                      : unsupported(*context_, "No selected mesh.");
}

PreviewHandle
OperationsApi::interpolated_bridge_faces(BridgeOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().interpolated_bridge(options)
                      : handle_value_failure(
                            *context_, unsupported_error("No selected mesh."),
                            PreviewHandle{});
}

OperationReceipt OperationsApi::invert_faces() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().flip_normals()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::combine_faces() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().combine()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::collapse_faces() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().collapse()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::radial_align_faces() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().radial_align()
                      : unsupported(*context_, "No selected mesh.");
}

PreviewHandle OperationsApi::slice_quad(SliceQuadOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().slice_quads(options)
                      : handle_value_failure(
                            *context_, unsupported_error("No selected mesh."),
                            PreviewHandle{});
}

PreviewHandle OperationsApi::thicken_faces(ThickenOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().thicken(options)
                      : handle_value_failure(
                            *context_, unsupported_error("No selected mesh."),
                            PreviewHandle{});
}

OperationReceipt OperationsApi::extract_faces() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  if (!mesh.valid()) {
    return unsupported(*context_, "No selected mesh.");
  }
  MeshHandle extracted = mesh.faces().selected().extract();
  if (!extracted.valid()) {
    return handle_failure(*context_,
                          make_error(ErrorCode::InternalError,
                                     "Extract did not create a mesh."));
  }
  OperationReceipt receipt;
  receipt.changed = true;
  receipt.revisions = current_revisions(*context_);
  receipt.dirty.topology = true;
  receipt.dirty.geometry = true;
  return receipt;
}

OperationReceipt OperationsApi::detach_faces() {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.faces().selected().detach()
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt
OperationsApi::assign_material_slot_to_selection(std::uint32_t slot) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.materials().assign_slot(slot)
                      : unsupported(*context_, "No selected mesh.");
}

OperationReceipt OperationsApi::plane_cut(PlaneCutOptions options) {
  std::vector<ObjectId> targets;
  for (const SessionObjectSummary &object : context_->session.objects()) {
    if (object.selected) {
      targets.push_back(object.id);
    }
  }
  if (targets.empty()) {
    return handle_failure(
        *context_,
        make_error(ErrorCode::InvalidArgument, "Cut needs a selected mesh."));
  }

  const std::size_t undo_mark = context_->session.undo_depth();
  OperationReceipt receipt;
  for (ObjectId object : targets) {
    Result<PolygonDocument> document = context_->session.document_copy(object);
    if (!document.ok()) {
      return handle_failure(*context_, document.error());
    }

    const quader_poly::PlaneCutResult cut = quader_poly::plane_cut(
        PolygonDocumentNativeAccess::document(document.value()),
        native_plane_cut_request(options));
    if (!cut.changed) {
      return handle_failure(
          *context_,
          make_error(ErrorCode::InvalidArgument,
                     cut.message.empty()
                         ? std::string("Plane Cut could not split the mesh.")
                         : cut.message));
    }

    const bool keep_back = options.keep == CutKeepMode::DiscardLeft;
    PolygonDocument front = PolygonDocumentNativeAccess::from_native(
        keep_back ? cut.back_document : cut.front_document,
        keep_back ? cut.back_selection : cut.front_selection);
    Result<OperationResult> committed = context_->session.commit_document(
        object, std::move(front), plane_cut_receipt("Plane Cut"));
    if (!committed.ok()) {
      return handle_failure(*context_, committed.error());
    }
    receipt = receipt_from_result(committed.value(), current_revisions(*context_));

    if (options.keep == CutKeepMode::KeepBoth) {
      Result<ObjectId> back = context_->session.add_document(
          PolygonDocumentNativeAccess::from_native(cut.back_document,
                                                  cut.back_selection),
          "Cut Back");
      if (!back.ok()) {
        return handle_failure(*context_, back.error());
      }
      receipt.changed = true;
      receipt.dirty.topology = true;
      receipt.dirty.geometry = true;
      receipt.dirty.selection = true;
      receipt.revisions = current_revisions(*context_);
    }
  }
  context_->session.squash_undo_since(undo_mark);
  return receipt;
}

OperationReceipt OperationsApi::knife_segment(KnifeSegmentOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  if (!mesh.valid()) {
    return unsupported(*context_, "No selected mesh.");
  }
  return direct_mutate_selection(
      *context_, current_component_selection(*context_, mesh),
      [&](PolygonDocument &document) {
        return document.knife_segment(options.from, options.to);
      });
}

OperationReceipt OperationsApi::knife_stroke(KnifeStrokeOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  if (!mesh.valid()) {
    return unsupported(*context_, "No selected mesh.");
  }
  return direct_mutate_selection(
      *context_, current_component_selection(*context_, mesh),
      [&](PolygonDocument &document) {
        return document.knife_stroke(options.points, options.segments);
      });
}

OperationReceipt OperationsApi::insert_edge_loop(InsertEdgeLoopOptions options) {
  MeshHandle mesh = MeshCollection(context_).first_selected();
  return mesh.valid() ? mesh.edges().selected().insert_loop(options)
                      : unsupported(*context_, "No selected mesh.");
}

} // namespace quader::modeling
