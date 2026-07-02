////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

using namespace quader_editor_core;

TEST(QdrModelingRuntimeOperationNormalShadingTests, ShadeSmoothAndFlatOperateOnSelectedMeshObjects) {
	EditorSession session = qdr_test::selected_cube_session();

	const CommandResult smooth = session.shade_selected_faces_smooth();

	EXPECT_TRUE(smooth.ok);
	EXPECT_TRUE(smooth.changed);
	for (const quader_poly::Face& face : session.objects().front().document.faces) {
          EXPECT_TRUE(face.normal_shading ==
                      quader_poly::SurfaceShading::Smooth);
        }

	const CommandResult flat = session.shade_selected_faces_flat();

	EXPECT_TRUE(flat.ok);
	EXPECT_TRUE(flat.changed);
	for (const quader_poly::Face& face : session.objects().front().document.faces) {
          EXPECT_TRUE(face.normal_shading == quader_poly::SurfaceShading::Flat);
        }
}

TEST(QdrModelingRuntimeOperationNormalShadingTests, HardAndSoftNormalOperationsOperateOnSelectedEdges) {
	EditorSession session = qdr_test::selected_cube_session();
	quader_editor_core::PolygonObject& object = session.objects().front();
	const quader_poly::Face first_face = object.document.faces.front();
	const quader_poly::Edge edge = quader_poly::make_edge(first_face.vertices[0], first_face.vertices[1]);
        session.set_component_selection(object.id,
                                        qdr_test::edge_selection({edge}, edge),
                                        SelectionMode::Edge);

        const CommandResult soft_default = session.soften_selected_edge_normals();

	EXPECT_TRUE(soft_default.ok);
	EXPECT_TRUE(soft_default.changed);
	std::size_t smooth_face_count = 0;
	for (const quader_poly::Face& face : session.objects().front().document.faces) {
          if (face.normal_shading == quader_poly::SurfaceShading::Smooth) {
            ++smooth_face_count;
          }
        }
	EXPECT_TRUE(smooth_face_count > 0U);
	EXPECT_TRUE(session.objects().front().document.hard_normal_edges.empty());

	const CommandResult hard = session.harden_selected_edge_normals();

	EXPECT_TRUE(hard.ok);
	EXPECT_TRUE(hard.changed);
	EXPECT_TRUE(session.objects().front().document.hard_normal_edges.size() == 1U);

	const CommandResult soft = session.soften_selected_edge_normals();

	EXPECT_TRUE(soft.ok);
	EXPECT_TRUE(soft.changed);
	EXPECT_TRUE(session.objects().front().document.hard_normal_edges.empty());
}
