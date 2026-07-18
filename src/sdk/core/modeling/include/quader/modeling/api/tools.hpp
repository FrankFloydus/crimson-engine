////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/api/modeling_contracts.hpp>

namespace quader::modeling {

/**
 * Provides explicit-result tool activation methods.
 */
class CheckedToolsApi {
public:
  CheckedToolsApi() = default;
  [[nodiscard]] Result<BoxTool> activate_box_tool(BoxToolOptions options = {});
  [[nodiscard]] Result<KnifeTool>
  activate_knife_tool(KnifeToolOptions options = {});
  [[nodiscard]] Result<CutTool> activate_cut_tool(CutToolOptions options = {});

private:
  friend class CheckedModelingApi;
  explicit CheckedToolsApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Provides host-neutral tool activation and frame handling.
 */
class ToolsApi {
public:
  ToolsApi() = default;
  [[nodiscard]] std::vector<ToolDescriptor> catalog() const;
  [[nodiscard]] std::string active_tool() const;
  [[nodiscard]] ToolStatus status() const;
  [[nodiscard]] SelectTool activate_select_tool();
  [[nodiscard]] TransformTool activate_move_tool();
  [[nodiscard]] TransformTool activate_extrude_tool();
  [[nodiscard]] TransformTool activate_rotate_tool();
  [[nodiscard]] TransformTool activate_scale_tool();
  [[nodiscard]] BoxTool activate_box_tool(BoxToolOptions options = {});
  [[nodiscard]] InsertEdgeLoopTool
  activate_insert_edge_loop_tool(InsertEdgeLoopToolOptions options = {});
  [[nodiscard]] KnifeTool activate_knife_tool(KnifeToolOptions options = {});
  [[nodiscard]] CutTool activate_cut_tool(CutToolOptions options = {});
  [[nodiscard]] PolyTool activate_poly_tool(PolyToolOptions options = {});
  [[nodiscard]] MirrorTool activate_mirror_tool(MirrorToolOptions options = {});
  /**
   * @deprecated Portable pivot behavior is unsupported and this compatibility
   * entry point is scheduled for removal in 0.2.0.
   */
  [[deprecated("Portable pivot is unsupported and will be removed in 0.2.0; "
               "use the native editor tools API.")]]
  [[nodiscard]] PivotTool activate_pivot_tool(PivotToolOptions options = {});
  [[nodiscard]] OperationReceipt
  handle_frame(ViewportId viewport, const ViewportHostInputSnapshot &input,
               const ViewportCameraSnapshot &camera);
  [[nodiscard]] OperationReceipt commit_active_tool_preview();
  [[nodiscard]] OperationReceipt cancel_active_tool_preview();
  [[nodiscard]] OperationReceipt cancel_modal_tool();

private:
  friend class ModelingApi;
  friend class CheckedToolsApi;
  explicit ToolsApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Controller for host-neutral box creation.
 */
class BoxTool {
public:
  BoxTool() = default;
  [[nodiscard]] BoxTool &drag_footprint(PointerEvent begin, PointerEvent update);
  [[nodiscard]] BoxTool &drag_height(PointerEvent update);
  [[nodiscard]] MeshHandle commit(std::string label = "Create Box");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ModelingApi;
  friend class ToolsApi;
  BoxTool(std::shared_ptr<ModelingApiContext> context, BoxToolOptions options);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
  BoxToolOptions options_;
};

/**
 * Controller for host-neutral select tool state.
 */
class SelectTool {
public:
  SelectTool() = default;
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  explicit SelectTool(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Controller for host-neutral transform preview tools.
 */
class TransformTool {
public:
  TransformTool() = default;
  [[nodiscard]] OperationReceipt begin(Vec3 delta = {});
  [[nodiscard]] OperationReceipt update(Vec3 delta);
  [[nodiscard]] OperationReceipt commit(std::string label = {});
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  TransformTool(std::shared_ptr<ModelingApiContext> context,
                std::string operation_id);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
  std::string operation_id_;
  Vec3 value_{};
  bool has_value_ = false;
};

/**
 * Controller for edge-loop insertion.
 */
class InsertEdgeLoopTool {
public:
  InsertEdgeLoopTool() = default;
  [[nodiscard]] InsertEdgeLoopTool &set_edge(EdgeKey edge);
  [[nodiscard]] InsertEdgeLoopTool &set_factor(float factor);
  [[nodiscard]] OperationReceipt commit(std::string label = "Insert Edge Loop");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  InsertEdgeLoopTool(std::shared_ptr<ModelingApiContext> context,
                     InsertEdgeLoopToolOptions options);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
  InsertEdgeLoopToolOptions options_;
};

/**
 * Controller for host-neutral knife input.
 */
class KnifeTool {
public:
  KnifeTool() = default;
  [[nodiscard]] KnifeTool &add_point(Ray ray);
  [[nodiscard]] KnifeTool &add_segment(Ray from, Ray to);
  [[nodiscard]] KnifeTool &add_point(KnifeTarget target);
  [[nodiscard]] KnifeTool &add_segment(KnifeTarget from, KnifeTarget to);
  [[nodiscard]] OperationReceipt commit(std::string label = "Knife");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ModelingApi;
  friend class ToolsApi;
  explicit KnifeTool(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
  std::vector<Ray> points_;
  std::vector<KnifeTarget> targets_;
  std::vector<KnifeStrokeSegment> segments_;
};

/**
 * Controller for host-neutral plane cut input.
 */
class CutTool {
public:
  CutTool() = default;
  [[nodiscard]] CutTool &set_keep_mode(CutKeepMode mode);
  [[nodiscard]] CutTool &set_plane(Vec3 a, Vec3 b, Vec3 c);
  [[nodiscard]] OperationReceipt commit(std::string label = "Cut");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ModelingApi;
  friend class ToolsApi;
  CutTool(std::shared_ptr<ModelingApiContext> context, CutToolOptions options);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
  PlaneCutOptions options_;
};

/**
 * Controller for host-neutral polygon drawing input.
 */
class PolyTool {
public:
  PolyTool() = default;
  [[nodiscard]] PolyTool &add_point(Vec3 point);
  [[nodiscard]] PolyTool &set_material(MaterialId material);
  [[nodiscard]] OperationReceipt commit(std::string label = "Poly");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  explicit PolyTool(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
  std::vector<Vec3> points_;
  MaterialId material_{};
};

/**
 * Controller for mirror tool settings.
 */
class MirrorTool {
public:
  MirrorTool() = default;
  [[nodiscard]] MirrorTool &set_axis(Axis axis);
  [[nodiscard]] OperationReceipt commit(std::string label = "Mirror");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  MirrorTool(std::shared_ptr<ModelingApiContext> context,
             MirrorToolOptions options);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
  MirrorToolOptions options_;
};

/**
 * Deprecated compatibility controller for native-editor-owned pivot behavior.
 * Commit always returns ErrorCode::UnsupportedOperation. Scheduled for removal
 * in 0.2.0.
 */
class PivotTool {
public:
  PivotTool() = default;
  [[nodiscard]] OperationReceipt commit(std::string label = "Pivot");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  explicit PivotTool(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

} // namespace quader::modeling
