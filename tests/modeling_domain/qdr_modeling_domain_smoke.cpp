////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <modeling/runtime/session.h>
#include <io/qdr/qdr.h>
#include <modeling/polygon/document.h>

#include <string>

TEST_CASE("app-owned modeling domain link through document editor-core and IO", "[modeling-runtime-smoke]")
{
	quader_poly::CubePrimitiveSpec spec;
	spec.width = 128.0F;
	spec.height = 32.0F;
	spec.depth = 128.0F;

	quader_poly::Document document = quader_poly::make_cube(spec);
	REQUIRE(document.vertices.size() == 8U);
	REQUIRE(document.faces.size() == 6U);

	const quader_poly::CompiledMesh mesh = quader_poly::compile_document(document);
	REQUIRE_FALSE(mesh.vertices.empty());
	REQUIRE_FALSE(mesh.indices.empty());

	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult create =
		session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F });
	REQUIRE(create.ok);
	REQUIRE(create.changed);
	REQUIRE(session.objects().size() == 1U);

	const quader_io::QdrDocument qdr_document = quader_io::document_from_session_state(session.state());
	const std::string serialized = quader_io::serialize_document(qdr_document);
	REQUIRE(serialized.find("\"format\": \"quader.qdr\"") != std::string::npos);

	const quader_io::QdrReadResult read = quader_io::deserialize_document(serialized);
	REQUIRE(read.ok);
	REQUIRE(read.document.objects.size() == 1U);
}
