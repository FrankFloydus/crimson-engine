////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <editor/modeling/runtime/tool_runtime_policy.hpp>
#include <editor/interactions/interaction_api.hpp>
#include <mesh/polygon/document.hpp>

#include <string>

TEST(QdrModelingRuntimeToolActivationTests, ViewportToolIdsAreStableHostContracts) {
  EXPECT_TRUE(std::string(quader_editor_core::viewport_interaction_tool_id(
                  quader_editor_core::ViewportInteractionTool::Select)) ==
              "select");
  EXPECT_TRUE(std::string(quader_editor_core::viewport_interaction_tool_id(
                  quader_editor_core::ViewportInteractionTool::Move)) ==
              "move");
  EXPECT_TRUE(std::string(quader_editor_core::viewport_interaction_tool_id(
                  quader_editor_core::ViewportInteractionTool::Extrude)) ==
              "extrude");
  EXPECT_TRUE(std::string(quader_editor_core::viewport_interaction_tool_id(
                  quader_editor_core::ViewportInteractionTool::Rotate)) ==
              "rotate");
  EXPECT_TRUE(std::string(quader_editor_core::viewport_interaction_tool_id(
                  quader_editor_core::ViewportInteractionTool::Scale)) ==
              "scale");
  EXPECT_TRUE(std::string(quader_editor_core::viewport_interaction_tool_id(
                  quader_editor_core::ViewportInteractionTool::Box)) == "box");
  EXPECT_TRUE(
      std::string(quader_editor_core::viewport_interaction_tool_id(
          quader_editor_core::ViewportInteractionTool::InsertEdgeLoop)) ==
      "insert_edge_loop");
  EXPECT_TRUE(std::string(quader_editor_core::viewport_interaction_tool_id(
                  quader_editor_core::ViewportInteractionTool::Knife)) ==
              "knife");
  EXPECT_TRUE(std::string(quader_editor_core::viewport_interaction_tool_id(
                  quader_editor_core::ViewportInteractionTool::Cut)) == "cut");

  EXPECT_TRUE(quader_editor_core::viewport_interaction_tool_from_id("select") ==
              quader_editor_core::ViewportInteractionTool::Select);
  EXPECT_TRUE(quader_editor_core::viewport_interaction_tool_from_id("move") ==
              quader_editor_core::ViewportInteractionTool::Move);
  EXPECT_TRUE(
      quader_editor_core::viewport_interaction_tool_from_id("extend") ==
      quader_editor_core::ViewportInteractionTool::Unknown);
  EXPECT_TRUE(
      quader_editor_core::viewport_interaction_tool_from_id("extrude") ==
      quader_editor_core::ViewportInteractionTool::Extrude);
  EXPECT_TRUE(quader_editor_core::viewport_interaction_tool_from_id("rotate") ==
              quader_editor_core::ViewportInteractionTool::Rotate);
  EXPECT_TRUE(quader_editor_core::viewport_interaction_tool_from_id("scale") ==
              quader_editor_core::ViewportInteractionTool::Scale);
  EXPECT_TRUE(quader_editor_core::viewport_interaction_tool_from_id("box") ==
              quader_editor_core::ViewportInteractionTool::Box);
  EXPECT_TRUE(quader_editor_core::viewport_interaction_tool_from_id(
                  "insert_edge_loop") ==
              quader_editor_core::ViewportInteractionTool::InsertEdgeLoop);
  EXPECT_TRUE(quader_editor_core::viewport_interaction_tool_from_id("knife") ==
              quader_editor_core::ViewportInteractionTool::Knife);
  EXPECT_TRUE(quader_editor_core::viewport_interaction_tool_from_id("cut") ==
              quader_editor_core::ViewportInteractionTool::Cut);
  EXPECT_TRUE(
      quader_editor_core::viewport_interaction_tool_from_id("missing") ==
      quader_editor_core::ViewportInteractionTool::Unknown);
}

TEST(QdrModelingRuntimeToolActivationTests, ToolActivationPolicyResetsConflictingRuntimeOwnersSameToolActivationPreservesOwnedToolPreviewsAndClearsStaleTransformDrag) {
	{
		quader_editor_core::ViewportToolActivationRequest same_tool;
                same_tool.current_tool =
                    quader_editor_core::ViewportInteractionTool::Move;
                same_tool.requested_tool =
                    quader_editor_core::ViewportInteractionTool::Move;
                const quader_editor_core::ViewportToolActivationPlan same_tool_plan =
			quader_editor_core::plan_viewport_tool_activation(same_tool);
		EXPECT_FALSE(same_tool_plan.active_tool_changed);
		EXPECT_FALSE(same_tool_plan.cancel_transform_preview);
		EXPECT_TRUE(same_tool_plan.reset_transform_drag);

		quader_editor_core::ViewportToolActivationRequest same_insert;
                same_insert.current_tool =
                    quader_editor_core::ViewportInteractionTool::InsertEdgeLoop;
                same_insert.requested_tool =
                    quader_editor_core::ViewportInteractionTool::InsertEdgeLoop;
                same_insert.selection_mode =
                    quader_editor_core::SelectionMode::Edge;
                const quader_editor_core::ViewportToolActivationPlan same_insert_plan =
			quader_editor_core::plan_viewport_tool_activation(same_insert);
		EXPECT_TRUE(same_insert_plan.insert_edge_loop_tool_active);
		EXPECT_FALSE(same_insert_plan.reset_insert_edge_loop_tool);

		quader_editor_core::ViewportToolActivationRequest same_knife;
                same_knife.current_tool =
                    quader_editor_core::ViewportInteractionTool::Knife;
                same_knife.requested_tool =
                    quader_editor_core::ViewportInteractionTool::Knife;
                const quader_editor_core::ViewportToolActivationPlan same_knife_plan =
			quader_editor_core::plan_viewport_tool_activation(same_knife);
		EXPECT_TRUE(same_knife_plan.knife_tool_active);
		EXPECT_FALSE(same_knife_plan.reset_knife_tool);

		quader_editor_core::ViewportToolActivationRequest same_cut;
                same_cut.current_tool =
                    quader_editor_core::ViewportInteractionTool::Cut;
                same_cut.requested_tool =
                    quader_editor_core::ViewportInteractionTool::Cut;
                same_cut.selected_cut_target_object_count = 1;
		const quader_editor_core::ViewportToolActivationPlan same_cut_plan =
			quader_editor_core::plan_viewport_tool_activation(same_cut);
		EXPECT_TRUE(same_cut_plan.cut_tool_active);
		EXPECT_FALSE(same_cut_plan.reset_cut_tool);

		quader_editor_core::ViewportToolActivationRequest same_mirror;
                same_mirror.current_tool =
                    quader_editor_core::ViewportInteractionTool::Mirror;
                same_mirror.requested_tool =
                    quader_editor_core::ViewportInteractionTool::Mirror;
                same_mirror.selected_object_count = 1;
		const quader_editor_core::ViewportToolActivationPlan same_mirror_plan =
			quader_editor_core::plan_viewport_tool_activation(same_mirror);
		EXPECT_TRUE(same_mirror_plan.mirror_tool_active);
		EXPECT_FALSE(same_mirror_plan.reset_mirror_tool);
	}

        EXPECT_TRUE(quader_editor_core::sanitize_viewport_interaction_tool(
                        quader_editor_core::ViewportInteractionTool::Unknown) ==
                    quader_editor_core::ViewportInteractionTool::Select);
        EXPECT_TRUE(quader_editor_core::viewport_tool_uses_transform_drag(
            quader_editor_core::ViewportInteractionTool::Extrude));
        EXPECT_FALSE(quader_editor_core::viewport_tool_uses_transform_gizmo(
            quader_editor_core::ViewportInteractionTool::Extrude));
}

TEST(QdrModelingRuntimeToolActivationTests, ToolActivationPolicyResetsConflictingRuntimeOwnersConflictingActivationPlansResetTransientOwners) {
	

	{
		quader_editor_core::ViewportToolActivationRequest request;
                request.current_tool =
                    quader_editor_core::ViewportInteractionTool::Move;
                request.requested_tool =
                    quader_editor_core::ViewportInteractionTool::Cut;
                request.selection_mode =
                    quader_editor_core::SelectionMode::Face;
                request.transform_drag_active = true;
		request.selected_cut_target_object_count = 1;

		const quader_editor_core::ViewportToolActivationPlan plan = quader_editor_core::plan_viewport_tool_activation(request);
                EXPECT_TRUE(plan.active_tool ==
                            quader_editor_core::ViewportInteractionTool::Cut);
                EXPECT_TRUE(plan.active_tool_changed);
		EXPECT_TRUE(plan.cancel_transform_preview);
		EXPECT_FALSE(plan.reset_transform_drag);
		EXPECT_TRUE(plan.reset_insert_edge_loop_tool);
		EXPECT_TRUE(plan.reset_knife_tool);
		EXPECT_FALSE(plan.knife_tool_active);
		EXPECT_TRUE(plan.cut_tool_active);
		EXPECT_TRUE(plan.reset_cut_tool);
		EXPECT_FALSE(plan.insert_edge_loop_tool_active);
		EXPECT_TRUE(plan.reset_box_tool);
		EXPECT_TRUE(plan.reset_box_grid_size);
		EXPECT_TRUE(plan.clear_gizmo_hover);
		EXPECT_FALSE(plan.selection_mode_changed);
	}

        EXPECT_TRUE(quader_editor_core::sanitize_viewport_interaction_tool(
                        quader_editor_core::ViewportInteractionTool::Unknown) ==
                    quader_editor_core::ViewportInteractionTool::Select);
        EXPECT_TRUE(quader_editor_core::viewport_tool_uses_transform_drag(
            quader_editor_core::ViewportInteractionTool::Extrude));
        EXPECT_FALSE(quader_editor_core::viewport_tool_uses_transform_gizmo(
            quader_editor_core::ViewportInteractionTool::Extrude));
}

TEST(QdrModelingRuntimeToolActivationTests, ToolActivationPolicyResetsConflictingRuntimeOwnersDrawnPlaneObjectToolsRejectActivationWithoutRequiredSelection) {
	

	

	{
		quader_editor_core::ViewportToolActivationRequest cut_request;
                cut_request.current_tool =
                    quader_editor_core::ViewportInteractionTool::Knife;
                cut_request.requested_tool =
                    quader_editor_core::ViewportInteractionTool::Cut;
                cut_request.transform_drag_active = true;

		const quader_editor_core::ViewportToolActivationPlan cut_plan =
			quader_editor_core::plan_viewport_tool_activation(cut_request);
		EXPECT_TRUE(cut_plan.activation_blocked);
                EXPECT_TRUE(cut_plan.active_tool ==
                            quader_editor_core::ViewportInteractionTool::Knife);
                EXPECT_FALSE(cut_plan.cancel_transform_preview);
		EXPECT_FALSE(cut_plan.reset_transform_drag);
		EXPECT_FALSE(cut_plan.reset_cut_tool);
		EXPECT_FALSE(cut_plan.clear_gizmo_hover);
		EXPECT_TRUE(std::string(cut_plan.blocked_message) == "Select one or more mesh objects to cut.");

		cut_request.selected_cut_target_object_count = 1;
		const quader_editor_core::ViewportToolActivationPlan cut_allowed =
			quader_editor_core::plan_viewport_tool_activation(cut_request);
		EXPECT_FALSE(cut_allowed.activation_blocked);
                EXPECT_TRUE(cut_allowed.active_tool ==
                            quader_editor_core::ViewportInteractionTool::Cut);

                quader_editor_core::ViewportToolActivationRequest mirror_request;
                mirror_request.current_tool =
                    quader_editor_core::ViewportInteractionTool::Select;
                mirror_request.requested_tool =
                    quader_editor_core::ViewportInteractionTool::Mirror;
                mirror_request.selection_mode =
                    quader_editor_core::SelectionMode::Face;
                mirror_request.selected_object_count = 1;
		const quader_editor_core::ViewportToolActivationPlan mirror_component_mode =
			quader_editor_core::plan_viewport_tool_activation(mirror_request);
		EXPECT_TRUE(mirror_component_mode.activation_blocked);
		EXPECT_TRUE(std::string(mirror_component_mode.blocked_message) == "Select one or more mesh objects to mirror.");

                mirror_request.selection_mode =
                    quader_editor_core::SelectionMode::Object;
                const quader_editor_core::ViewportToolActivationPlan mirror_allowed =
			quader_editor_core::plan_viewport_tool_activation(mirror_request);
		EXPECT_FALSE(mirror_allowed.activation_blocked);
                EXPECT_TRUE(
                    mirror_allowed.active_tool ==
                    quader_editor_core::ViewportInteractionTool::Mirror);
                EXPECT_TRUE(mirror_allowed.mirror_tool_active);
	}

        EXPECT_TRUE(quader_editor_core::sanitize_viewport_interaction_tool(
                        quader_editor_core::ViewportInteractionTool::Unknown) ==
                    quader_editor_core::ViewportInteractionTool::Select);
        EXPECT_TRUE(quader_editor_core::viewport_tool_uses_transform_drag(
            quader_editor_core::ViewportInteractionTool::Extrude));
        EXPECT_FALSE(quader_editor_core::viewport_tool_uses_transform_gizmo(
            quader_editor_core::ViewportInteractionTool::Extrude));
}

TEST(QdrModelingRuntimeToolActivationTests, ToolActivationPolicyResetsConflictingRuntimeOwnersInsertEdgeLoopActivationOwnsEdgeSelectionWithoutOverwritingCandidates) {
	

	

	

	{
		quader_editor_core::ViewportToolActivationRequest request;
                request.current_tool =
                    quader_editor_core::ViewportInteractionTool::Select;
                request.requested_tool =
                    quader_editor_core::ViewportInteractionTool::InsertEdgeLoop;
                request.selection_mode =
                    quader_editor_core::SelectionMode::Object;
                request.hover_has_object = true;
		request.hover_has_candidate_object = true;

		const quader_editor_core::ViewportToolActivationPlan plan = quader_editor_core::plan_viewport_tool_activation(request);
                EXPECT_TRUE(plan.selection_mode ==
                            quader_editor_core::SelectionMode::Edge);
                EXPECT_TRUE(plan.insert_edge_loop_tool_active);
		EXPECT_FALSE(plan.promote_hover_object_to_candidate);
	}

        EXPECT_TRUE(quader_editor_core::sanitize_viewport_interaction_tool(
                        quader_editor_core::ViewportInteractionTool::Unknown) ==
                    quader_editor_core::ViewportInteractionTool::Select);
        EXPECT_TRUE(quader_editor_core::viewport_tool_uses_transform_drag(
            quader_editor_core::ViewportInteractionTool::Extrude));
        EXPECT_FALSE(quader_editor_core::viewport_tool_uses_transform_gizmo(
            quader_editor_core::ViewportInteractionTool::Extrude));
}

TEST(QdrModelingRuntimeToolActivationTests, ToolActivationPolicyResetsConflictingRuntimeOwnersBoxActivationPreservesBoxStateWhileClearingCutState) {
	

	

	

	

	{
		quader_editor_core::ViewportToolActivationRequest request;
                request.current_tool =
                    quader_editor_core::ViewportInteractionTool::Cut;
                request.requested_tool =
                    quader_editor_core::ViewportInteractionTool::Box;

                const quader_editor_core::ViewportToolActivationPlan plan = quader_editor_core::plan_viewport_tool_activation(request);
		EXPECT_FALSE(plan.reset_box_tool);
		EXPECT_FALSE(plan.reset_box_grid_size);
		EXPECT_TRUE(plan.reset_knife_tool);
		EXPECT_TRUE(plan.reset_cut_tool);
	}

        EXPECT_TRUE(quader_editor_core::sanitize_viewport_interaction_tool(
                        quader_editor_core::ViewportInteractionTool::Unknown) ==
                    quader_editor_core::ViewportInteractionTool::Select);
        EXPECT_TRUE(quader_editor_core::viewport_tool_uses_transform_drag(
            quader_editor_core::ViewportInteractionTool::Extrude));
        EXPECT_FALSE(quader_editor_core::viewport_tool_uses_transform_gizmo(
            quader_editor_core::ViewportInteractionTool::Extrude));
}

TEST(QdrModelingRuntimeToolActivationTests, EditorRuntimeActivationWaitsForTransformPreviewCancellation) {
	quader_editor_core::EditorRuntimeState runtime;
        runtime.active_tool =
            quader_editor_core::ViewportInteractionTool::Scale;
        runtime.transform_drag.active = true;
        runtime.transform_drag.axis = quader_editor_core::TransformAxis::X;
        runtime.gizmo_hover_axis = quader_editor_core::TransformAxis::X;

        quader_editor_core::EditorRuntimeToolActivationRequest request;
        request.requested_tool =
            quader_editor_core::ViewportInteractionTool::InsertEdgeLoop;
        request.selection_mode = quader_editor_core::SelectionMode::Object;
        request.transform_preview_active = true;

	const quader_editor_core::EditorRuntimeToolActivationResult blocked =
		quader_editor_core::activate_editor_runtime_tool(runtime, request);
	EXPECT_FALSE(blocked.applied);
	EXPECT_TRUE(blocked.requires_transform_preview_cancel);
        EXPECT_TRUE(runtime.active_tool ==
                    quader_editor_core::ViewportInteractionTool::Scale);
        EXPECT_TRUE(runtime.transform_drag.active);
        EXPECT_TRUE(runtime.gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::X);

        request.transform_preview_cleared = true;
	const quader_editor_core::EditorRuntimeToolActivationResult applied =
		quader_editor_core::activate_editor_runtime_tool(runtime, request);
	EXPECT_TRUE(applied.applied);
	EXPECT_FALSE(applied.requires_transform_preview_cancel);
        EXPECT_TRUE(
            runtime.active_tool ==
            quader_editor_core::ViewportInteractionTool::InsertEdgeLoop);
        EXPECT_TRUE(runtime.insert_edge_loop.active);
        EXPECT_TRUE(applied.plan.selection_mode ==
                    quader_editor_core::SelectionMode::Edge);
        EXPECT_FALSE(runtime.transform_drag.active);
        EXPECT_TRUE(runtime.gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::None);
}

TEST(QdrModelingRuntimeToolActivationTests, EditorRuntimeActivationBlocksSelectedObjectToolsBeforeMutatingState) {
	quader_editor_core::EditorRuntimeState runtime;
        runtime.active_tool =
            quader_editor_core::ViewportInteractionTool::Scale;
        runtime.transform_drag.active = true;
        runtime.transform_drag.axis = quader_editor_core::TransformAxis::X;
        runtime.gizmo_hover_axis = quader_editor_core::TransformAxis::X;
        runtime.cut.points = { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };

	quader_editor_core::EditorRuntimeToolActivationRequest request;
        request.requested_tool =
            quader_editor_core::ViewportInteractionTool::Cut;
        request.selection_mode = quader_editor_core::SelectionMode::Object;
        request.transform_preview_active = true;

	const quader_editor_core::EditorRuntimeToolActivationResult blocked =
		quader_editor_core::activate_editor_runtime_tool(runtime, request);

	EXPECT_FALSE(blocked.applied);
	EXPECT_FALSE(blocked.requires_transform_preview_cancel);
	EXPECT_TRUE(blocked.plan.activation_blocked);
	EXPECT_TRUE(std::string(blocked.plan.blocked_message) == "Select one or more mesh objects to cut.");
        EXPECT_TRUE(runtime.active_tool ==
                    quader_editor_core::ViewportInteractionTool::Scale);
        EXPECT_TRUE(runtime.transform_drag.active);
        EXPECT_TRUE(runtime.gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::X);
        EXPECT_TRUE(runtime.cut.points.size() == 2U);

	request.selected_cut_target_object_count = 1;
	const quader_editor_core::EditorRuntimeToolActivationResult pending_cancel =
		quader_editor_core::activate_editor_runtime_tool(runtime, request);

	EXPECT_FALSE(pending_cancel.applied);
	EXPECT_TRUE(pending_cancel.requires_transform_preview_cancel);
	EXPECT_FALSE(pending_cancel.plan.activation_blocked);
        EXPECT_TRUE(runtime.active_tool ==
                    quader_editor_core::ViewportInteractionTool::Scale);
}

TEST(QdrModelingRuntimeToolActivationTests, EditorRuntimeStatusAndResetPoliciesReportTransientOwners) {
	quader_editor_core::EditorRuntimeState runtime;
        runtime.active_tool = quader_editor_core::ViewportInteractionTool::Move;
        runtime.gizmo_hover_axis = quader_editor_core::TransformAxis::X;
        runtime.hover.object_id = 7;
	runtime.hover.candidate_object_id = 7;
	runtime.box.preview_valid = true;
	runtime.box_grid_size = 2.0;
	runtime.insert_edge_loop.active = true;
	runtime.insert_edge_loop.dragging = true;
	runtime.insert_edge_loop.preview_lines.push_back({});
	runtime.knife.active = true;
	runtime.knife.has_active_stroke = true;
	runtime.knife.has_current = true;
	runtime.transform_drag.active = true;
	runtime.clipboard_objects.push_back({ 99, 0, true, quader_poly::make_cube({}), {} });

	const quader_editor_core::EditorRuntimeStatus status = quader_editor_core::editor_runtime_status(runtime);
        EXPECT_TRUE(status.active_tool ==
                    quader_editor_core::ViewportInteractionTool::Move);
        EXPECT_TRUE(status.gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::X);
        EXPECT_TRUE(status.hover_active);
	EXPECT_TRUE(status.hover_has_candidate_object);
	EXPECT_TRUE(status.box_active);
	EXPECT_TRUE(status.box_grid_size == 2.0);
	EXPECT_FALSE(status.box_grid_at_default);
	EXPECT_TRUE(status.insert_edge_loop_active);
	EXPECT_TRUE(status.insert_edge_loop_dragging);
	EXPECT_TRUE(status.insert_edge_loop_has_preview);
	EXPECT_TRUE(status.knife_active);
	EXPECT_TRUE(status.knife_has_active_stroke);
	EXPECT_TRUE(status.knife_has_current);
	EXPECT_TRUE(status.transform_drag_active);
	EXPECT_TRUE(status.clipboard_available);
	EXPECT_TRUE(status.clipboard_object_count == 1);

	quader_editor_core::clear_editor_runtime_preview_presentation_state(runtime);
	const quader_editor_core::EditorRuntimeStatus cleared = quader_editor_core::editor_runtime_status(runtime);
        EXPECT_TRUE(runtime.active_tool ==
                    quader_editor_core::ViewportInteractionTool::Move);
        EXPECT_TRUE(runtime.box_grid_size == 2.0);
	EXPECT_FALSE(cleared.hover_active);
	EXPECT_FALSE(cleared.box_active);
	EXPECT_FALSE(cleared.insert_edge_loop_active);
	EXPECT_FALSE(cleared.knife_active);
	EXPECT_FALSE(cleared.transform_drag_active);
        EXPECT_TRUE(cleared.gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::None);
        EXPECT_TRUE(cleared.clipboard_available);
}

TEST(QdrModelingRuntimeToolActivationTests, EditorRuntimeSelectionPresentationClearOwnsHoverGizmoAndDragOrdering) {
	quader_editor_core::EditorRuntimeState runtime;
        runtime.active_tool = quader_editor_core::ViewportInteractionTool::Move;
        runtime.hover.object_id = 7;
	runtime.hover.candidate_object_id = 7;
        runtime.gizmo_hover_axis = quader_editor_core::TransformAxis::X;
        runtime.transform_drag.active = true;
        runtime.transform_drag.axis = quader_editor_core::TransformAxis::X;
        runtime.box.preview_valid = true;
	runtime.box_grid_size = 4.0;
	runtime.insert_edge_loop.active = true;
	runtime.insert_edge_loop.preview_lines.push_back({});
	runtime.clipboard_objects.push_back({ 99, 0, true, quader_poly::make_cube({}), {} });

	quader_editor_core::clear_editor_runtime_selection_presentation_state(runtime);

	const quader_editor_core::EditorRuntimeStatus status = quader_editor_core::editor_runtime_status(runtime);
        EXPECT_TRUE(status.active_tool ==
                    quader_editor_core::ViewportInteractionTool::Move);
        EXPECT_FALSE(status.hover_active);
        EXPECT_TRUE(status.gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::None);
        EXPECT_FALSE(status.transform_drag_active);
	EXPECT_TRUE(status.box_active);
	EXPECT_TRUE(status.box_grid_size == 4.0);
	EXPECT_TRUE(status.insert_edge_loop_active);
	EXPECT_TRUE(status.insert_edge_loop_has_preview);
	EXPECT_TRUE(status.clipboard_available);

	quader_editor_core::EditorRuntime wrapper;
	quader_editor_core::SelectionHoverState hover;
	hover.object_id = 11;
	wrapper.set_hover(hover);
        wrapper.set_gizmo_hover_axis(quader_editor_core::TransformAxis::Z);
        wrapper.begin_transform_drag_state(
            quader_editor_core::ViewportInteractionTool::Rotate,
            quader_editor_core::TransformDragOperation::Transform,
            quader_editor_core::TransformAxis::Z, {});

        wrapper.clear_selection_presentation_state();

	EXPECT_FALSE(wrapper.status().hover_active);
        EXPECT_TRUE(wrapper.status().gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::None);
        EXPECT_FALSE(wrapper.status().transform_drag_active);
}

TEST(QdrModelingRuntimeToolActivationTests, EditorRuntimeDocumentResetClearsSessionPreviewAndRuntimeToolState) {
	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult preview_result = session.begin_transform_preview(false);
	EXPECT_TRUE(preview_result.ok);
	EXPECT_TRUE(session.preview_transaction_status().active);

	quader_editor_core::EditorRuntime runtime;
        runtime.state().active_tool =
            quader_editor_core::ViewportInteractionTool::Scale;
        runtime.state().gizmo_hover_axis = quader_editor_core::TransformAxis::Y;
        runtime.state().box_grid_size = 16.0;
	runtime.state().transform_drag.active = true;
	runtime.state().clipboard_objects.push_back({ 4, 0, false, quader_poly::make_cube({}), {} });

	runtime.reset_document_state(session);
	const quader_editor_core::EditorRuntimeStatus status = runtime.status();
	EXPECT_FALSE(session.preview_transaction_status().active);
        EXPECT_TRUE(status.active_tool ==
                    quader_editor_core::ViewportInteractionTool::Select);
        EXPECT_TRUE(status.gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::None);
        EXPECT_FALSE(status.transform_drag_active);
        EXPECT_TRUE(runtime.state().box_grid_size ==
                    quader_editor_core::kDefaultEditorRuntimeBoxGridSize);
        EXPECT_TRUE(status.clipboard_available);
}

TEST(QdrModelingRuntimeToolActivationTests, EditorRuntimeSetActiveToolOwnsPreviewCancellationAndSelectionMode) {
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	session.set_selected_object_ids({ 1 }, 1);
	EXPECT_TRUE(session.begin_transform_preview(false).ok);

	quader_editor_core::EditorRuntime runtime;
        runtime.state().active_tool =
            quader_editor_core::ViewportInteractionTool::Move;
        runtime.state().transform_drag.active = true;
        runtime.state().transform_drag.axis =
            quader_editor_core::TransformAxis::Y;
        runtime.state().gizmo_hover_axis = quader_editor_core::TransformAxis::Y;
        runtime.state().hover.object_id = 1;

        const quader_editor_core::EditorRuntimeToolActivationResult result =
            runtime.set_active_tool(
                session,
                quader_editor_core::ViewportInteractionTool::InsertEdgeLoop);

        EXPECT_TRUE(result.applied);
	EXPECT_TRUE(result.transform_preview_cancelled);
	EXPECT_TRUE(result.selection_mode_applied);
	EXPECT_FALSE(session.preview_transaction_status().active);
        EXPECT_TRUE(session.selection_mode() ==
                    quader_editor_core::SelectionMode::Edge);
        EXPECT_TRUE(
            runtime.active_tool() ==
            quader_editor_core::ViewportInteractionTool::InsertEdgeLoop);
        EXPECT_TRUE(runtime.state().insert_edge_loop.active);
	EXPECT_FALSE(runtime.state().transform_drag.active);
        EXPECT_TRUE(runtime.state().gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::None);
        EXPECT_TRUE(runtime.state().hover.candidate_object_id == 1);
}

TEST(QdrModelingRuntimeToolActivationTests, EditorRuntimeWrapperMethodsKeepStateOwnershipInCompiledSource) {
	quader_editor_core::EditorRuntimeState initial;
        initial.active_tool = quader_editor_core::ViewportInteractionTool::Box;
        initial.box_grid_size = 4.0;

	quader_editor_core::EditorRuntime runtime(initial);
        EXPECT_TRUE(runtime.active_tool() ==
                    quader_editor_core::ViewportInteractionTool::Box);
        EXPECT_TRUE(runtime.box_grid_size() == 4.0);

	quader_editor_core::SelectionHoverState hover;
	hover.object_id = 7;
	runtime.set_hover(hover);
	EXPECT_TRUE(runtime.status().hover_active);

	quader_editor_core::SelectionHoverUpdateResult hover_update;
	hover_update.hover.object_id = 8;
	hover_update.hover.candidate_object_id = 8;
	runtime.apply_hover_update(hover_update);
	EXPECT_TRUE(runtime.status().hover_has_candidate_object);
	EXPECT_TRUE(runtime.hover().object_id == 8);
	EXPECT_TRUE(runtime.hover().candidate_object_id == 8);

	runtime.clear_hover();
	EXPECT_FALSE(runtime.status().hover_active);

	quader_editor_core::BoxToolState box;
	box.preview_valid = true;
	runtime.set_box_state(box);
	EXPECT_TRUE(runtime.status().box_active);
	EXPECT_TRUE(runtime.box_active());

	runtime.reset_box_state();
	EXPECT_FALSE(runtime.status().box_active);
        EXPECT_TRUE(runtime.box_grid_size() ==
                    quader_editor_core::kDefaultEditorRuntimeBoxGridSize);
        runtime.set_box_grid_size(0.5);
	EXPECT_TRUE(runtime.set_box_construction_plane({ 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }).ok);
	const quader_editor_core::BoxConstructionPointResult box_point =
		runtime.box_construction_point_from_ray({ { 0.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } });
	EXPECT_TRUE(box_point.ok);
	EXPECT_TRUE(runtime.begin_box_footprint(box_point.position, box_point.position, true).ok);
	EXPECT_TRUE(runtime.update_box_footprint({ 1.0F, 0.0F, 1.0F }, true).ok);
	EXPECT_TRUE(runtime.box_preview_descriptor(true).ok);
        EXPECT_TRUE(runtime.box_state().drag_state ==
                    quader_editor_core::BoxDragState::Footprint);

        runtime.begin_transform_drag_state(
            quader_editor_core::ViewportInteractionTool::Scale,
            quader_editor_core::TransformDragOperation::Transform,
            quader_editor_core::TransformAxis::Y, {1.0F, 2.0F, 3.0F});
        EXPECT_TRUE(runtime.status().transform_drag_active);
	EXPECT_TRUE(runtime.transform_drag_active());
        EXPECT_TRUE(runtime.transform_drag_state().tool ==
                    quader_editor_core::ViewportInteractionTool::Scale);
        EXPECT_TRUE(runtime.transform_drag_state().axis ==
                    quader_editor_core::TransformAxis::Y);

        quader_editor_core::ScaleToolState scale_state;
	scale_state.preview_scale = { 2.0F, 2.0F, 2.0F };
	runtime.set_transform_drag_scale_state(scale_state);
	EXPECT_TRUE(runtime.transform_drag_state().scale.preview_scale.x == 2.0F);

	runtime.clear_transform_drag_state();
	EXPECT_FALSE(runtime.status().transform_drag_active);

	quader_editor_core::InsertEdgeLoopToolState insert_edge_loop;
	insert_edge_loop.active = true;
	runtime.set_insert_edge_loop_state(insert_edge_loop);
	runtime.set_insert_edge_loop_dragging(true);
	EXPECT_TRUE(runtime.status().insert_edge_loop_active);
	EXPECT_TRUE(runtime.status().insert_edge_loop_dragging);
	runtime.set_insert_edge_loop_active(false);
	EXPECT_FALSE(runtime.insert_edge_loop_state().active);

	quader_editor_core::KnifeToolState knife;
	knife.active = true;
	knife.has_current = true;
	runtime.set_knife_state(knife);
	EXPECT_TRUE(runtime.status().knife_active);
	EXPECT_TRUE(runtime.status().knife_has_current);

	runtime.set_knife_active(false);
	EXPECT_FALSE(runtime.status().knife_active);

	quader_editor_core::CutHoverState cut_hover;
	cut_hover.valid = true;
	cut_hover.object_id = 3;
	runtime.set_cut_hover(cut_hover);
	EXPECT_TRUE(runtime.cut_hover().valid);
	EXPECT_TRUE(runtime.cut_hover().object_id == 3);
	runtime.clear_cut_hover();
	EXPECT_FALSE(runtime.cut_hover().valid);

	quader_editor_core::CutToolState cut;
	cut.points = { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };
	cut.target_object_ids = { 1 };
	runtime.set_cut_state(cut);
	EXPECT_TRUE(runtime.cut_state().points.size() == 2U);
        EXPECT_TRUE(
            runtime.set_cut_mode(quader_editor_core::CutMode::KeepFront).ok);
        EXPECT_TRUE(runtime.cut_state().mode ==
                    quader_editor_core::CutMode::KeepFront);

        quader_editor_core::PolyToolState poly;
	poly.active = true;
	poly.closed = true;
	poly.has_current = true;
	poly.points.push_back({ 0.0F, 0.0F, 0.0F });
	poly.points.push_back({ 1.0F, 0.0F, 0.0F });
	poly.points.push_back({ 1.0F, 0.0F, 1.0F });
	runtime.set_poly_state(poly);
	EXPECT_TRUE(runtime.status().poly_active);
	EXPECT_TRUE(runtime.status().poly_closed);
	EXPECT_TRUE(runtime.status().poly_can_commit);
	EXPECT_TRUE(runtime.poly_state().points.size() == 3U);
	runtime.set_poly_active(false);
	EXPECT_FALSE(runtime.poly_state().active);

        runtime.set_gizmo_hover_axis(quader_editor_core::TransformAxis::Z);
        EXPECT_TRUE(runtime.status().gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::Z);
        EXPECT_TRUE(runtime.gizmo_hover_axis() ==
                    quader_editor_core::TransformAxis::Z);

        quader_editor_core::TransformGizmoPickHit miss;
	runtime.set_gizmo_hover_from_pick(miss);
        EXPECT_TRUE(runtime.status().gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::None);

        quader_editor_core::TransformGizmoPickHit hit;
	hit.hit = true;
        hit.axis = quader_editor_core::TransformAxis::X;
        runtime.set_gizmo_hover_from_pick(hit);
        EXPECT_TRUE(runtime.status().gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::X);

        runtime.set_clipboard({ { 42, 0, true, quader_poly::make_cube({}), {} } });
	EXPECT_TRUE(runtime.clipboard_available());
	EXPECT_TRUE(runtime.clipboard_objects().size() == 1U);
	EXPECT_TRUE(runtime.clipboard_object_count() == 1U);
	EXPECT_TRUE(runtime.status().clipboard_object_count == 1U);

	runtime.clear_clipboard();
	EXPECT_FALSE(runtime.clipboard_available());

	quader_editor_core::EditorRuntimeState replacement;
        replacement.active_tool =
            quader_editor_core::ViewportInteractionTool::Select;
        replacement.box_grid_size = 2.0;
	runtime.set_state(replacement);
        EXPECT_TRUE(runtime.active_tool() ==
                    quader_editor_core::ViewportInteractionTool::Select);
        EXPECT_TRUE(runtime.box_grid_size() == 2.0);
}
