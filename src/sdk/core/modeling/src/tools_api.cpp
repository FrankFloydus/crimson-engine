////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "public_api_detail.hpp"

#include <array>

namespace quader::modeling {
using namespace detail;



CommandApi::CommandApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

std::vector<CommandDescriptor> CommandApi::catalog() const {
  return OperationsApi(context_).catalog();
}

CommandDescriptor CommandApi::describe(std::string_view command_id) const {
  return OperationsApi(context_).describe(command_id);
}

OperationReceipt CommandApi::execute(std::string_view command_id,
                                     OperationSettings settings) {
  return OperationsApi(context_).execute(command_id, settings);
}

ToolsApi::ToolsApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

std::vector<ToolDescriptor> ToolsApi::catalog() const {
  return {{"select", "Select"}, {"move", "Move"}, {"extend", "Extend"},
          {"extrude", "Extrude"}, {"rotate", "Rotate"}, {"scale", "Scale"},
          {"box", "Box"}, {"insert_edge_loop", "Insert Edge Loop"},
          {"knife", "Knife"}, {"cut", "Cut"}, {"poly", "Poly"},
          {"mirror", "Mirror"}, {"pivot", "Pivot"}};
}

std::string ToolsApi::active_tool() const { return context_->active_tool; }

ToolStatus ToolsApi::status() const {
  return {
      .active_tool = context_->active_tool,
      .preview_active = context_->session.preview_active(),
  };
}

SelectTool ToolsApi::activate_select_tool() {
  context_->active_tool = "select";
  return SelectTool(context_);
}

TransformTool ToolsApi::activate_move_tool() {
  context_->active_tool = "move";
  return TransformTool(context_, "move");
}

TransformTool ToolsApi::activate_extend_tool() {
  context_->active_tool = "extend";
  return TransformTool(context_, "extend");
}

TransformTool ToolsApi::activate_extrude_tool() {
  context_->active_tool = "extrude";
  return TransformTool(context_, "extrude");
}

TransformTool ToolsApi::activate_rotate_tool() {
  context_->active_tool = "rotate";
  return TransformTool(context_, "rotate");
}

TransformTool ToolsApi::activate_scale_tool() {
  context_->active_tool = "scale";
  return TransformTool(context_, "scale");
}

BoxTool ToolsApi::activate_box_tool(BoxToolOptions options) {
  context_->active_tool = "box";
  return BoxTool(context_, std::move(options));
}

InsertEdgeLoopTool ToolsApi::activate_insert_edge_loop_tool(
    InsertEdgeLoopToolOptions options) {
  context_->active_tool = "insert_edge_loop";
  return InsertEdgeLoopTool(context_, options);
}

KnifeTool ToolsApi::activate_knife_tool(KnifeToolOptions) {
  context_->active_tool = "knife";
  return KnifeTool(context_);
}

CutTool ToolsApi::activate_cut_tool(CutToolOptions options) {
  context_->active_tool = "cut";
  return CutTool(context_, options);
}

PolyTool ToolsApi::activate_poly_tool(PolyToolOptions) {
  context_->active_tool = "poly";
  return PolyTool(context_);
}

MirrorTool ToolsApi::activate_mirror_tool(MirrorToolOptions options) {
  context_->active_tool = "mirror";
  return MirrorTool(context_, options);
}

PivotTool ToolsApi::activate_pivot_tool(PivotToolOptions) {
  context_->active_tool = "pivot";
  return PivotTool(context_);
}

OperationReceipt ToolsApi::handle_frame(ViewportId,
                                        const ViewportHostInputSnapshot &,
                                        const ViewportCameraSnapshot &) {
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

OperationReceipt ToolsApi::commit_active_tool_preview() {
  return PreviewApi(context_).commit();
}

OperationReceipt ToolsApi::cancel_active_tool_preview() {
  return PreviewApi(context_).cancel();
}

OperationReceipt ToolsApi::cancel_modal_tool() {
  context_->active_tool = "select";
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

BoxTool::BoxTool(std::shared_ptr<ModelingApiContext> context,
                 BoxToolOptions options)
    : context_(std::move(context)), options_(std::move(options)) {}

BoxTool &BoxTool::drag_footprint(PointerEvent, PointerEvent) { return *this; }
BoxTool &BoxTool::drag_height(PointerEvent) { return *this; }

MeshHandle BoxTool::commit(std::string) {
  return ModelingApi(context_).create_box({
      .name = options_.name,
      .min = options_.min,
      .max = options_.max,
      .material = options_.material,
  });
}

OperationReceipt BoxTool::cancel() {
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

SelectTool::SelectTool(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

OperationReceipt SelectTool::cancel() {
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

TransformTool::TransformTool(std::shared_ptr<ModelingApiContext> context,
                             std::string operation_id)
    : context_(std::move(context)), operation_id_(std::move(operation_id)) {}

namespace {

[[nodiscard]] bool transform_tool_uses_translate_preview(
    std::string_view operation_id) {
  return operation_id == "move" || operation_id == "extend";
}

[[nodiscard]] Vec3 normalized_transform_tool_value(
    std::string_view operation_id, Vec3 value) {
  if (operation_id == "scale" && value.x == 0.0F && value.y == 0.0F &&
      value.z == 0.0F) {
    return {1.0F, 1.0F, 1.0F};
  }
  return value;
}

[[nodiscard]] OperationReceipt transform_tool_staged_receipt(
    const ModelingApiContext &context) {
  OperationReceipt receipt;
  receipt.revisions = current_revisions(context);
  receipt.dirty.overlays = true;
  return receipt;
}

} // namespace

OperationReceipt TransformTool::begin(Vec3 delta) {
  value_ = normalized_transform_tool_value(operation_id_, delta);
  has_value_ = true;
  MeshHandle mesh = MeshCollection(context_).first_selected();
  if (!mesh.valid()) {
    return unsupported(*context_, "No selected mesh.");
  }
  if (!transform_tool_uses_translate_preview(operation_id_)) {
    return transform_tool_staged_receipt(*context_);
  }
  Result<OperationResult> result =
      context_->session.begin_translate_preview(mesh.id(), value_);
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return receipt_from_result(result.value(), current_revisions(*context_));
}

OperationReceipt TransformTool::update(Vec3 delta) {
  value_ = normalized_transform_tool_value(operation_id_, delta);
  has_value_ = true;
  if (!transform_tool_uses_translate_preview(operation_id_)) {
    return transform_tool_staged_receipt(*context_);
  }
  Result<OperationResult> result =
      context_->session.update_translate_preview(value_);
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return receipt_from_result(result.value(), current_revisions(*context_));
}

OperationReceipt TransformTool::commit(std::string) {
  if (transform_tool_uses_translate_preview(operation_id_)) {
    return PreviewApi(context_).commit();
  }
  const Vec3 value = has_value_
                         ? value_
                         : normalized_transform_tool_value(operation_id_, {});
  if (operation_id_ == "extrude") {
    return OperationsApi(context_).extrude({.offset = value});
  }
  if (operation_id_ == "rotate") {
    return OperationsApi(context_).rotate_selection({.radians = value});
  }
  if (operation_id_ == "scale") {
    return OperationsApi(context_).scale_selection({.scale = value});
  }
  return OperationsApi(context_).translate_selection(value);
}

OperationReceipt TransformTool::cancel() {
  if (!transform_tool_uses_translate_preview(operation_id_)) {
    has_value_ = false;
    OperationReceipt receipt;
    receipt.revisions = current_revisions(*context_);
    return receipt;
  }
  return PreviewApi(context_).cancel();
}

InsertEdgeLoopTool::InsertEdgeLoopTool(
    std::shared_ptr<ModelingApiContext> context,
    InsertEdgeLoopToolOptions options)
    : context_(std::move(context)), options_(options) {}

InsertEdgeLoopTool &InsertEdgeLoopTool::set_edge(EdgeKey edge) {
  options_.loop.edge = edge;
  return *this;
}

InsertEdgeLoopTool &InsertEdgeLoopTool::set_factor(float factor) {
  options_.loop.t = factor;
  return *this;
}

OperationReceipt InsertEdgeLoopTool::commit(std::string) {
  return OperationsApi(context_).insert_edge_loop(options_.loop);
}

OperationReceipt InsertEdgeLoopTool::cancel() {
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

KnifeTool::KnifeTool(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

KnifeTool &KnifeTool::add_point(Ray ray) {
  points_.push_back(ray);
  return *this;
}

KnifeTool &KnifeTool::add_segment(Ray from, Ray to) {
  points_.push_back(from);
  points_.push_back(to);
  return *this;
}

KnifeTool &KnifeTool::add_point(KnifeTarget target) {
  targets_.push_back(target);
  return *this;
}

KnifeTool &KnifeTool::add_segment(KnifeTarget from, KnifeTarget to) {
  const std::uint32_t first =
      static_cast<std::uint32_t>(targets_.size());
  targets_.push_back(from);
  const std::uint32_t second =
      static_cast<std::uint32_t>(targets_.size());
  targets_.push_back(to);
  segments_.push_back({.first_point = first, .second_point = second});
  return *this;
}

OperationReceipt KnifeTool::commit(std::string) {
  if (targets_.empty()) {
    return unsupported(*context_,
                       "Knife tool requires resolved topology targets.");
  }
  std::vector<KnifeStrokeSegment> segments = segments_;
  if (segments.empty() && targets_.size() >= 2U) {
    segments.reserve(targets_.size() - 1U);
    for (std::uint32_t index = 1; index < targets_.size(); ++index) {
      segments.push_back({.first_point = index - 1U, .second_point = index});
    }
  }
  return OperationsApi(context_).knife_stroke({
      .points = targets_,
      .segments = std::move(segments),
  });
}

OperationReceipt KnifeTool::cancel() {
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

CutTool::CutTool(std::shared_ptr<ModelingApiContext> context,
                 CutToolOptions options)
    : context_(std::move(context)) {
  options_.keep = options.keep;
}

CutTool &CutTool::set_keep_mode(CutKeepMode mode) {
  options_.keep = mode;
  return *this;
}

CutTool &CutTool::set_plane(Vec3 a, Vec3 b, Vec3 c) {
  options_.a = a;
  options_.b = b;
  options_.c = c;
  return *this;
}

OperationReceipt CutTool::commit(std::string) {
  return OperationsApi(context_).plane_cut(options_);
}

OperationReceipt CutTool::cancel() {
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

PolyTool::PolyTool(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

PolyTool &PolyTool::add_point(Vec3 point) {
  points_.push_back(point);
  return *this;
}

PolyTool &PolyTool::set_material(MaterialId material) {
  material_ = material;
  return *this;
}

OperationReceipt PolyTool::commit(std::string) {
  Result<PolygonDocument> document =
      PolygonDocument::make_face(points_, material_);
  if (!document.ok()) {
    return handle_failure(*context_, document.error());
  }

  const std::size_t undo_mark = context_->session.undo_depth();
  Result<ObjectId> object =
      context_->session.add_document(std::move(document).value(), "Poly");
  if (!object.ok()) {
    return handle_failure(*context_, object.error());
  }
  const std::array<ObjectId, 1> selected{object.value()};
  Result<OperationResult> selection =
      context_->session.apply_object_selection(selected, SelectionEdit::Replace);
  if (!selection.ok()) {
    static_cast<void>(context_->session.rollback_undo_since(undo_mark));
    return handle_failure(*context_, selection.error());
  }
  context_->session.squash_undo_since(undo_mark);

  OperationReceipt receipt =
      receipt_from_result(selection.value(), current_revisions(*context_));
  receipt.changed = true;
  receipt.dirty.topology = true;
  receipt.dirty.geometry = true;
  receipt.dirty.selection = true;
  return receipt;
}

OperationReceipt PolyTool::cancel() {
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

MirrorTool::MirrorTool(std::shared_ptr<ModelingApiContext> context,
                       MirrorToolOptions options)
    : context_(std::move(context)), options_(options) {}

MirrorTool &MirrorTool::set_axis(Axis axis) {
  options_.axis = axis;
  return *this;
}

OperationReceipt MirrorTool::commit(std::string) {
  return OperationsApi(context_).mirror_selection(options_.axis);
}

OperationReceipt MirrorTool::cancel() {
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

PivotTool::PivotTool(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

OperationReceipt PivotTool::commit(std::string) {
  return unsupported(*context_,
                     "Pivot tool is native-editor-owned until portable pivot semantics exist.");
}

OperationReceipt PivotTool::cancel() {
  OperationReceipt receipt;
  receipt.revisions = current_revisions(*context_);
  return receipt;
}

ProfilingApi::ProfilingApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

CheckedToolsApi::CheckedToolsApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

Result<BoxTool> CheckedToolsApi::activate_box_tool(BoxToolOptions options) {
  return Result<BoxTool>::success(ToolsApi(context_).activate_box_tool(options));
}

Result<KnifeTool>
CheckedToolsApi::activate_knife_tool(KnifeToolOptions options) {
  return Result<KnifeTool>::success(
      ToolsApi(context_).activate_knife_tool(options));
}

Result<CutTool> CheckedToolsApi::activate_cut_tool(CutToolOptions options) {
  return Result<CutTool>::success(ToolsApi(context_).activate_cut_tool(options));
}

} // namespace quader::modeling
