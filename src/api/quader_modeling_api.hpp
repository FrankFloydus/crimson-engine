////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_api_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class QuaderApiMutationRunner;

/**
 * Exposes Quader Modeling API operations through the public Quader API surface.
 */
class QuaderModelingApi final {
public:
  QuaderModelingApi(QuaderApiSession session,
                    QuaderApiMutationRunner &mutations);

  QuaderApiResult<QuaderModelingObjectRef>
  create_box(const QuaderCreateBoxArgs &args);
  QuaderApiResult<QuaderModelingObjectRef> create_box(QuaderBounds bounds,
                                                      std::string name = "Box");
  QuaderApiResult<QuaderModelingObjectRef>
  commit_box_preview(std::string name = "Box",
                     AssetId default_material = kInvalidAssetId);

  QuaderApiResult<QuaderModelingObjectRef>
  create_mesh(QuaderPolygonDocument document, QuaderCreateMeshArgs args = {});

  QuaderApiVoidResult set_selection_mode(QuaderSelectionMode mode);
  [[nodiscard]] bool can_execute_tool(std::string_view tool_id) const;
  QuaderApiVoidResult execute_tool(std::string_view tool_id);
  QuaderApiVoidResult remove_doubles();

  QuaderApiVoidResult begin_preview(std::string_view action_id);
  QuaderApiVoidResult update_preview(const QuaderPreviewSettings &settings);
  QuaderApiVoidResult commit_preview(std::string label = {});
  QuaderApiVoidResult cancel_preview();
  [[nodiscard]] bool can_cancel_active_tool_or_preview() const;

  QuaderApiVoidResult begin_transform_preview(bool duplicate_selected = false);
  QuaderApiVoidResult update_transform_translate(QVec3 delta);
  QuaderApiVoidResult update_transform_matrix(const QuaderTransform3 &transform,
                                              QVec3 pivot);
  QuaderApiVoidResult
  update_transform_extrude(QVec3 offset, float closed_edge_ledge_size = 0.0f);
  QuaderApiVoidResult update_transform_inset(const QuaderTransform3 &transform,
                                             QVec3 pivot);
  QuaderApiVoidResult commit_transform_preview(std::string label = {});
  QuaderApiVoidResult cancel_transform_preview();

  QuaderApiResult<QuaderTransformDragPreview>
  begin_transform_drag(const QuaderTransformDragBeginRequest &request);
  QuaderApiResult<QuaderTransformDragPreview>
  update_transform_drag(const QuaderTransformDragUpdateRequest &request);
  QuaderApiResult<QuaderTransformDragPreview> commit_transform_drag();
  QuaderApiVoidResult cancel_transform_drag();

  QuaderApiVoidResult move_selection(QVec3 delta);
  QuaderApiVoidResult rotate_selection(QVec3 euler_radians, QVec3 pivot = {});
  QuaderApiVoidResult scale_selection(QVec3 scale, QVec3 pivot = {});
  QuaderApiVoidResult transform_selection(const QuaderTransform3 &transform,
                                          QVec3 pivot = {});
  [[nodiscard]] bool can_duplicate_selected_entity_hierarchy() const;
  QuaderApiVoidResult duplicate_selected_entity_hierarchy();
  QuaderApiVoidResult duplicate_selected_hierarchy_for_group_drag();

  QuaderApiVoidResult apply_active_material(std::uint32_t material_slot = 0);
  QuaderApiVoidResult
  assign_material_slot_to_selection(std::uint32_t material_slot);
  QuaderApiVoidResult
  assign_material_slot_to_face(QuaderModelingObjectRef object,
                               QuaderElementId face,
                               std::uint32_t material_slot);

  [[nodiscard]] QuaderActionSet
  available_action_set(const QuaderActionQuery &query = {}) const;
  [[nodiscard]] std::vector<QuaderActionItem>
  available_actions(const QuaderActionQuery &query = {}) const;
  [[nodiscard]] std::vector<QuaderInstantOperationDescriptor>
  instant_operations() const;
  [[nodiscard]] QuaderSelectionSummary selection_summary() const;
  [[nodiscard]] std::vector<QuaderModelingObjectRecord> objects() const;

private:
  [[nodiscard]] QuaderModelingObjectRef
  ref_from_record(const QuaderModelingObjectRecord &record) const;

  QuaderApiSession session_;
  QuaderApiMutationRunner &mutations_;
};

} // namespace quader
