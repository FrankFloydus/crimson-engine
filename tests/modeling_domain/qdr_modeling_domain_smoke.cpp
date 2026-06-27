////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/session.hpp>
#include <io/qdr/qdr_runtime_io_compat.hpp>
#include <mesh/polygon/document.hpp>

#include <string>

TEST(QdrModelingDomainSmoke, AppOwnedModelingDomainLinkThroughDocumentEditorCoreAndIO) {
	quader_poly::CubePrimitiveSpec spec;
	spec.width = 128.0F;
	spec.height = 32.0F;
	spec.depth = 128.0F;

	quader_poly::Document document = quader_poly::make_cube(spec);
	EXPECT_TRUE(document.vertices.size() == 8U);
	EXPECT_TRUE(document.faces.size() == 6U);

	const quader_poly::CompiledMesh mesh = quader_poly::compile_document(document);
	EXPECT_FALSE(mesh.vertices.empty());
	EXPECT_FALSE(mesh.indices.empty());

	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult create =
		session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F });
	EXPECT_TRUE(create.ok);
	EXPECT_TRUE(create.changed);
	EXPECT_TRUE(session.objects().size() == 1U);

	const quader_io::QdrDocument qdr_document = quader_io::document_from_session_state(session.state());
	const std::string serialized = quader_io::serialize_document(qdr_document);
	EXPECT_TRUE(serialized.find("\"format\": \"quader.qdr\"") != std::string::npos);

	const quader_io::QdrReadResult read = quader_io::deserialize_document(serialized);
	EXPECT_TRUE(read.ok);
	EXPECT_TRUE(read.document.objects.size() == 1U);
}
