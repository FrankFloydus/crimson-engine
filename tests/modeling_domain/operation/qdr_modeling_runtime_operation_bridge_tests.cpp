////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/modeling/operations/operation_schema.hpp>

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace {

struct BridgeDocumentFixture {
	quader_poly::Document document;
	quader_poly::Edge bridge_edge;
	quader_poly::ElementId face = quader_poly::kInvalidElementId;
	std::vector<quader::QVec3> vertex_positions;
};

struct CrossObjectBridgeFixture {
	quader_editor_core::EditorSession session;
	std::vector<quader::QVec3> source_common_local_positions;
};

BridgeDocumentFixture make_bridge_quad_document(const std::array<quader::QVec3, 4>& positions)
{
	BridgeDocumentFixture fixture;
	fixture.vertex_positions.assign(positions.begin(), positions.end());
	const quader_poly::ElementId first = quader_poly::add_vertex(fixture.document, positions[0]);
	const quader_poly::ElementId second = quader_poly::add_vertex(fixture.document, positions[1]);
	const quader_poly::ElementId third = quader_poly::add_vertex(fixture.document, positions[2]);
	const quader_poly::ElementId fourth = quader_poly::add_vertex(fixture.document, positions[3]);
	fixture.face = quader_poly::add_face(fixture.document, std::array<quader_poly::ElementId, 4>{ first, second, third, fourth });
	EXPECT_TRUE(fixture.face != quader_poly::kInvalidElementId);
	fixture.bridge_edge = quader_poly::make_edge(first, second);
	return fixture;
}

quader_editor_core::PolygonObject selected_edge_object(int id, BridgeDocumentFixture fixture)
{
	quader_editor_core::PolygonObject object = qdr_test::make_object(id, std::move(fixture.document), true);
	object.selection = qdr_test::edge_selection({ fixture.bridge_edge }, fixture.bridge_edge);
	return object;
}

quader_editor_core::PolygonObject selected_face_object(int id, BridgeDocumentFixture fixture)
{
	quader_editor_core::PolygonObject object = qdr_test::make_object(id, std::move(fixture.document), true);
	object.selection = qdr_test::face_selection({ fixture.face }, fixture.face);
	return object;
}

CrossObjectBridgeFixture make_cross_object_edge_bridge_fixture()
{
	BridgeDocumentFixture survivor = make_bridge_quad_document({
		quader::QVec3 { 0.0F, 0.0F, 0.0F },
		quader::QVec3 { 0.0F, 0.0F, 1.0F },
		quader::QVec3 { 0.0F, 1.0F, 1.0F },
		quader::QVec3 { 0.0F, 1.0F, 0.0F },
	});
	BridgeDocumentFixture source = make_bridge_quad_document({
		quader::QVec3 { 3.0F, 0.0F, 0.0F },
		quader::QVec3 { 3.0F, 0.0F, 1.0F },
		quader::QVec3 { 3.0F, 1.0F, 1.0F },
		quader::QVec3 { 3.0F, 1.0F, 0.0F },
	});
	const std::vector<quader::QVec3> source_positions = source.vertex_positions;

	quader_editor_core::SessionState state;
	state.objects.push_back(selected_edge_object(1, std::move(survivor)));
	state.objects.push_back(selected_edge_object(2, std::move(source)));
	state.selection_mode = quader_editor_core::SelectionMode::Edge;
	state.active_object_id = 1;
	return { quader_editor_core::EditorSession(std::move(state)), source_positions };
}

CrossObjectBridgeFixture make_cross_object_face_bridge_fixture()
{
	BridgeDocumentFixture survivor = make_bridge_quad_document({
		quader::QVec3 { -1.0F, -1.0F, -1.0F },
		quader::QVec3 { 1.0F, -1.0F, -1.0F },
		quader::QVec3 { 1.0F, 1.0F, -1.0F },
		quader::QVec3 { -1.0F, 1.0F, -1.0F },
	});
	BridgeDocumentFixture source = make_bridge_quad_document({
		quader::QVec3 { -1.0F, -1.0F, 2.5F },
		quader::QVec3 { 1.0F, -1.0F, 2.5F },
		quader::QVec3 { 1.0F, 1.0F, 2.5F },
		quader::QVec3 { -1.0F, 1.0F, 2.5F },
	});
	const std::vector<quader::QVec3> source_positions = source.vertex_positions;

	quader_editor_core::SessionState state;
	state.objects.push_back(selected_face_object(1, std::move(survivor)));
	state.objects.push_back(selected_face_object(2, std::move(source)));
	state.selection_mode = quader_editor_core::SelectionMode::Face;
	state.active_object_id = 1;
	return { quader_editor_core::EditorSession(std::move(state)), source_positions };
}

bool document_contains_vertex_position(const quader_poly::Document& document, quader::QVec3 position)
{
	return std::any_of(document.vertices.begin(), document.vertices.end(), [position](const quader_poly::Vertex& vertex) {
		return qdr_test::vec_nearly_equal(vertex.position, position);
	});
}

void expect_document_contains_positions(const quader_poly::Document& document, const std::vector<quader::QVec3>& positions)
{
	for (quader::QVec3 position : positions) {
		EXPECT_TRUE(document_contains_vertex_position(document, position));
	}
}

} // namespace

TEST(QdrModelingRuntimeOperationBridgeTests, BridgeOperationSchemaOwnsDefaultsRangesAndNormalizer) {
	const quader_editor_core::BridgeOperationSchema bridge = quader_editor_core::bridge_operation_schema();

        EXPECT_TRUE(bridge.id == quader_editor_core::EditorOperationId::Bridge);
        EXPECT_TRUE(std::string(bridge.operation_id) == "bridge");
	EXPECT_TRUE(std::string(bridge.label) == "Bridge");
	EXPECT_TRUE(std::string(bridge.steps.id) == "steps");
	EXPECT_TRUE(bridge.steps.default_value == 1);
	EXPECT_TRUE(bridge.steps.minimum == 1);
	EXPECT_TRUE(bridge.steps.maximum == 64);
	EXPECT_TRUE(bridge.steps.step == 1);
	EXPECT_TRUE(bridge.steps.hard_limits);
	EXPECT_TRUE(quader_editor_core::normalize_bridge_operation_settings({ -100 }).steps == 1);
	EXPECT_TRUE(quader_editor_core::normalize_bridge_operation_settings({ 1000 }).steps == 64);
}

TEST(QdrModelingRuntimeOperationBridgeTests, BridgeOperationPreviewsFromBaseTopologyWithoutCommittingHistoryEarlyFaceBridgePreviewRebuildsFromBaseAndCancelsWithoutHistory) {
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_parallel_quad_document());
		quader_editor_core::EditorSession session({ object });
                session.set_component_selection(
                    1,
                    qdr_test::all_face_selection(
                        session.objects().front().document),
                    quader_editor_core::SelectionMode::Face);
                const std::size_t face_count_before = session.objects().front().document.faces.size();

		EXPECT_TRUE(session.begin_bridge_preview(1).changed);
                EXPECT_TRUE(session.preview_transaction_status().kind ==
                            quader_editor_core::PreviewTransactionKind::Bridge);
                EXPECT_TRUE(session.objects().front().document.faces.size() > face_count_before);
		EXPECT_TRUE(session.update_bridge_preview(3).changed);
		EXPECT_TRUE(session.objects().front().document.faces.size() > face_count_before);
                const quader_editor_core::PreviewTransactionFinishResult
                    cancel = session.cancel_preview_transaction(
                        quader_editor_core::PreviewTransactionKind::Bridge);
                EXPECT_TRUE(cancel.restored);
		EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before);
		EXPECT_FALSE(session.history_status().can_undo);
	}

	

	
}

TEST(QdrModelingRuntimeOperationBridgeTests, BridgeOperationPreviewsFromBaseTopologyWithoutCommittingHistoryEarlySchemaSettingsAreNormalizedBeforeBridgePreview) {
	

	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_parallel_quad_document());
		quader_editor_core::EditorSession session({ object });
                session.set_component_selection(
                    1,
                    qdr_test::all_face_selection(
                        session.objects().front().document),
                    quader_editor_core::SelectionMode::Face);

                quader_editor_core::BridgeOperationSettings settings;
		settings.steps = 1000;
		const quader_editor_core::CommandResult begin = session.begin_bridge_preview(settings);
		settings.steps = -100;
		const quader_editor_core::CommandResult update = session.update_bridge_preview(settings);

		EXPECT_TRUE(begin.ok);
		EXPECT_TRUE(begin.changed);
		EXPECT_TRUE(update.ok);
		EXPECT_TRUE(update.changed);
		EXPECT_TRUE(session.preview_transaction_status().active);
                EXPECT_TRUE(session.preview_transaction_status().kind ==
                            quader_editor_core::PreviewTransactionKind::Bridge);
        }

	
}

TEST(QdrModelingRuntimeOperationBridgeTests, BridgeOperationPreviewsFromBaseTopologyWithoutCommittingHistoryEarlyWrongExpectedKindDoesNotFinishTheActiveBridgePreview) {
	

	

	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_parallel_quad_document());
		quader_editor_core::EditorSession session({ object });
                session.set_component_selection(
                    1,
                    qdr_test::all_face_selection(
                        session.objects().front().document),
                    quader_editor_core::SelectionMode::Face);
                EXPECT_TRUE(session.begin_bridge_preview(1).changed);

                const quader_editor_core::PreviewTransactionFinishResult
                    cancel = session.cancel_preview_transaction(
                        quader_editor_core::PreviewTransactionKind::Thicken);

                EXPECT_FALSE(cancel.restored);
		EXPECT_TRUE(session.preview_transaction_status().active);
                EXPECT_TRUE(session.preview_transaction_status().kind ==
                            quader_editor_core::PreviewTransactionKind::Bridge);
        }
}

TEST(QdrModelingRuntimeOperationBridgeTests, CrossObjectEdgeBridgeConsumesDocumentsAlreadyConvertedToCommonLocalSpace) {
	CrossObjectBridgeFixture fixture = make_cross_object_edge_bridge_fixture();

	// Runtime PolygonObject has no scene transform. Scene/editor callers must
	// convert source documents into survivor-local/common-local space before
	// invoking the runtime bridge path.
	const quader_editor_core::CommandResult result = fixture.session.bridge_selected_edges(1);

	ASSERT_TRUE(result.ok) << result.message;
	ASSERT_TRUE(result.changed) << result.message;
	ASSERT_TRUE(fixture.session.objects().size() == 1U);
	const quader_editor_core::PolygonObject& survivor = qdr_test::require_object(fixture.session, 1);
	expect_document_contains_positions(survivor.document, fixture.source_common_local_positions);
	EXPECT_TRUE(fixture.session.active_object_id() == 1);
	EXPECT_TRUE(survivor.selection.mode == quader_poly::SelectionMode::Edge);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(survivor.document));
}

TEST(QdrModelingRuntimeOperationBridgeTests, CrossObjectFaceBridgeConsumesDocumentsAlreadyConvertedToCommonLocalSpace) {
	CrossObjectBridgeFixture fixture = make_cross_object_face_bridge_fixture();

	const quader_editor_core::CommandResult result = fixture.session.bridge_selected_faces(1);

	ASSERT_TRUE(result.ok) << result.message;
	ASSERT_TRUE(result.changed) << result.message;
	ASSERT_TRUE(fixture.session.objects().size() == 1U);
	const quader_editor_core::PolygonObject& survivor = qdr_test::require_object(fixture.session, 1);
	expect_document_contains_positions(survivor.document, fixture.source_common_local_positions);
	EXPECT_TRUE(fixture.session.active_object_id() == 1);
	EXPECT_TRUE(survivor.selection.mode == quader_poly::SelectionMode::Face);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(survivor.document));
}

TEST(QdrModelingRuntimeOperationBridgeTests, CrossObjectBridgePreviewCommitsOneHistoryStepAndUndoRestoresCommonLocalInputs) {
	CrossObjectBridgeFixture fixture = make_cross_object_face_bridge_fixture();
	const std::size_t original_object_count = fixture.session.objects().size();

	const quader_editor_core::CommandResult begin = fixture.session.begin_bridge_preview(1);

	ASSERT_TRUE(begin.ok) << begin.message;
	ASSERT_TRUE(begin.changed) << begin.message;
	EXPECT_FALSE(fixture.session.history_status().can_undo);
	ASSERT_TRUE(fixture.session.objects().size() == 1U);
	expect_document_contains_positions(qdr_test::require_object(fixture.session, 1).document, fixture.source_common_local_positions);

	const quader_editor_core::PreviewTransactionFinishResult commit =
		fixture.session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::Bridge);

	EXPECT_TRUE(commit.before.active);
	EXPECT_TRUE(commit.before.changed);
	EXPECT_FALSE(commit.restored);
	EXPECT_FALSE(fixture.session.preview_transaction_status().active);
	EXPECT_TRUE(fixture.session.history_status().can_undo);
	EXPECT_TRUE(fixture.session.history_status().undo_count == 1U);

	const quader_editor_core::HistoryRestoreResult undo = fixture.session.undo();
	ASSERT_TRUE(undo.applied);
	EXPECT_TRUE(fixture.session.objects().size() == original_object_count);
	expect_document_contains_positions(qdr_test::require_object(fixture.session, 2).document, fixture.source_common_local_positions);
	EXPECT_TRUE(undo.status.can_redo);

	const quader_editor_core::HistoryRestoreResult redo = fixture.session.redo();
	ASSERT_TRUE(redo.applied);
	ASSERT_TRUE(fixture.session.objects().size() == 1U);
	expect_document_contains_positions(qdr_test::require_object(fixture.session, 1).document, fixture.source_common_local_positions);
	EXPECT_TRUE(redo.status.can_undo);
}
