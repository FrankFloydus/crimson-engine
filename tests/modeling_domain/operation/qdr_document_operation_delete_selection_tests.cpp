////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationDeleteSelectionTests, DeleteSelectionPrunesAffectedFacesAndUnusedVertices) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const ElementId loose_vertex = add_vertex(fixture.document, { 4.0F, 0.0F, 0.0F });
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left });

	const OperationResult result = delete_selection(fixture.document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(fixture.document.faces.empty());
	EXPECT_TRUE(find_vertex(fixture.document, loose_vertex) == nullptr);
	EXPECT_TRUE(fixture.document.vertices.empty());
}
