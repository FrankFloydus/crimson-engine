////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <io/qdr/qdr_document.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef QUADER_QDR_TEST_FIXTURE_DIR
#error "QUADER_QDR_TEST_FIXTURE_DIR must point to tests/modeling_domain/fixtures/io for qdr_io_document_tests"
#endif

namespace {

constexpr std::string_view kStableDocumentFixtureName =
    "stable_document_v3.qdr.json";
constexpr std::string_view kStableDocumentV2FixtureName =
    "stable_document_v2.qdr.json";
constexpr std::string_view kLegacySessionPayloadFixtureName =
    "legacy_session_payload_v1.qdr.json";
constexpr std::string_view kMalformedDuplicateFaceIdFixtureName =
    "malformed_duplicate_face_id_v1.qdr.json";

std::filesystem::path io_fixture_path(std::string_view file_name)
{
	const std::filesystem::path fixture_directory(QUADER_QDR_TEST_FIXTURE_DIR);
	std::error_code error;
	const bool is_directory = std::filesystem::is_directory(fixture_directory, error);
	require_test(!error && is_directory,
		"QUADER_QDR_TEST_FIXTURE_DIR is missing or is not a directory: " + fixture_directory.string());
	return fixture_directory / std::string(file_name);
}

std::string read_io_fixture(std::string_view file_name)
{
	const std::filesystem::path path = io_fixture_path(file_name);
	std::ifstream stream(path, std::ios::in | std::ios::binary);
	require_test(stream.is_open(), "Missing io fixture: " + path.string());

	std::ostringstream contents;
	contents << stream.rdbuf();
	require_test(!stream.bad(), "Failed to read io fixture: " + path.string());
	return contents.str();
}

std::string read_text_file(const std::filesystem::path& path)
{
	std::ifstream stream(path, std::ios::in | std::ios::binary);
	require_test(stream.is_open(), "Failed to open test output file: " + path.string());
	std::ostringstream contents;
	contents << stream.rdbuf();
	require_test(!stream.bad(), "Failed to read test output file: " + path.string());
	return contents.str();
}

void write_text_file(const std::filesystem::path& path, std::string_view text)
{
	std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
	require_test(stream.is_open(), "Failed to create test file: " + path.string());
	stream.write(text.data(), static_cast<std::streamsize>(text.size()));
	stream.close();
	require_test(stream.good(), "Failed to write test file: " + path.string());
}

std::filesystem::path unique_test_directory_path(std::string_view test_name)
{
	const auto tick = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	return std::filesystem::temp_directory_path() / ("quader-qdr-" + std::string(test_name) + "-" + std::to_string(tick));
}

/**
 * Owns Scoped Test Directory behavior within the automated test suite.
 */
class ScopedTestDirectory {
public:
	explicit ScopedTestDirectory(std::string_view test_name)
		: path_(unique_test_directory_path(test_name))
	{
		std::error_code error;
		std::filesystem::create_directories(path_, error);
		require_test(!error, "Failed to create QDR IO temp directory: " + path_.string());
	}

	~ScopedTestDirectory()
	{
		std::error_code ignored;
		std::filesystem::remove_all(path_, ignored);
	}

	const std::filesystem::path& path() const
	{
		return path_;
	}

private:
	std::filesystem::path path_;
};

void require_no_qdr_temp_files(const std::filesystem::path& directory)
{
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
		const std::string file_name = entry.path().filename().string();
		require_test(file_name.find(".tmp") == std::string::npos, "atomic writer should clean temporary QDR files");
	}
}

std::string canonical_fixture_text(std::string text)
{
	std::string normalized;
	normalized.reserve(text.size());
	for (const char character : text) {
		if (character != '\r') {
			normalized.push_back(character);
		}
	}
	while (!normalized.empty() && normalized.back() == '\n') {
		normalized.pop_back();
	}
	return normalized;
}

std::string escaped_snippet(std::string_view text, std::size_t position)
{
	const std::size_t begin = position > 24U ? position - 24U : 0U;
	const std::size_t end = std::min(text.size(), position + 24U);
	std::string snippet;
	for (std::size_t index = begin; index < end; ++index) {
		switch (text[index]) {
		case '\n':
			snippet += "\\n";
			break;
		case '\r':
			snippet += "\\R";
			break;
		case '\t':
			snippet += "\\t";
			break;
		default:
			snippet.push_back(text[index]);
			break;
		}
	}
	return snippet;
}

std::string canonical_text_mismatch_message(std::string_view expected, std::string_view actual)
{
	std::size_t mismatch = 0;
	const std::size_t common_size = std::min(expected.size(), actual.size());
	while (mismatch < common_size && expected[mismatch] == actual[mismatch]) {
		++mismatch;
	}
	return "DTO writer should emit the committed current-version golden fixture exactly; expected size " +
		std::to_string(expected.size()) +
		", actual size " +
		std::to_string(actual.size()) +
		", first mismatch at " +
		std::to_string(mismatch) +
		", expected near `" +
		escaped_snippet(expected, mismatch) +
		"`, actual near `" +
		escaped_snippet(actual, mismatch) +
		"`";
}

quader_poly::Document make_triangle_document()
{
	quader_poly::Document document;
	const quader_poly::ElementId a = quader_poly::add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const quader_poly::ElementId b = quader_poly::add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const quader_poly::ElementId c = quader_poly::add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const quader_poly::ElementId face = quader_poly::add_face(document, std::array<quader_poly::ElementId, 3> { a, b, c }, 9);
	quader_poly::Face* face_data = quader_poly::find_face(document, face);
	require_test(face_data != nullptr, "fixture face should exist");
	face_data->uvs = { { 0.0F, 0.0F }, { 1.0F, 0.0F }, { 0.0F, 1.0F } };
	return document;
}

quader_io::QdrDocument make_qdr_document_fixture()
{
	quader_io::QdrObject object;
	object.id = 7;
	object.material_id = 3;
	object.selected = true;
	object.document = make_triangle_document();
        object.selection.mode = quader_poly::SelectionMode::Face;
        object.selection.faces = { object.document.faces.front().id };
	object.selection.has_active = true;
        object.selection.active_kind = quader_poly::ElementKind::Face;
        object.selection.active_face = object.document.faces.front().id;

	quader_io::QdrDocument document;
	document.objects = { object };
        document.lights = {
            quader_io::QdrLight{
                3,
                "Key",
                true,
                quader_io::QdrLightType::Spot,
                true,
                {0.0F, 4.0F, 0.0F},
                {0.0F, -1.0F, 0.0F},
                0xFFEFF7FFU,
                8.0F,
                quader_io::QdrLightShadowMode::Soft,
                0.75F,
                0.06F,
                0.45F,
                0.25F,
                12.0F,
                45.0F,
                30.0F,
            },
        };
        document.next_object_id = 8;
	document.next_light_id = 4;
	document.active_object_id = 7;
	document.active_light_id = 3;
        document.selection_mode = quader_io::QdrSelectionMode::Face;
        document.content_revision = 123;
	document.selection_revision = 45;
	return document;
}

std::string replace_once(std::string text, std::string_view from, std::string_view to)
{
	const std::string from_text(from);
	const std::string to_text(to);
	const std::size_t position = text.find(from_text);
	require_test(position != std::string::npos, "fixture text should contain the replacement token");
	text.replace(position, from_text.size(), to_text);
	return text;
}

void require_read_failure_contains(std::string_view text, std::string_view expected_message, std::string_view failure_message)
{
	const quader_io::QdrReadResult result = quader_io::deserialize_document(text);
	require_test(!result.ok, failure_message);
	require_test(result.message.find(expected_message) != std::string::npos, failure_message);
}

std::string stable_document_payload_v2_fixture()
{
	return R"json({
  "format": "quader.qdr",
  "version": 2,
  "document": {
    "nextObjectId": 8,
    "activeObjectId": 7,
    "selectionMode": "face",
    "contentRevision": 123,
    "selectionRevision": 45,
    "objects": [
      {
        "id": 7,
        "materialId": 3,
        "selected": true,
        "document": {
          "nextVertexId": 4,
          "nextFaceId": 2,
          "vertices": [
            { "id": 1, "position": { "x": 0, "y": 0, "z": 0 } },
            { "id": 2, "position": { "x": 1, "y": 0, "z": 0 } },
            { "id": 3, "position": { "x": 0, "y": 1, "z": 0 } }
          ],
          "faces": [
            {
              "id": 1,
              "vertices": [1, 2, 3],
              "uvs": [
                { "x": 0, "y": 0 },
                { "x": 1, "y": 0 },
                { "x": 0, "y": 1 }
              ],
              "materialSlot": 9,
              "normalShading": "flat"
            }
          ],
          "hardNormalEdges": []
        },
        "selection": {
          "mode": "face",
          "vertices": [],
          "edges": [],
          "faces": [1],
          "active": {
            "hasActive": true,
            "kind": "face",
            "vertex": 0,
            "edge": { "a": 0, "b": 0 },
            "face": 1
          }
        }
      }
    ]
  }
})json";
}

void dto_roundtrip_preserves_portable_document_data()
{
	const quader_io::QdrDocument document = make_qdr_document_fixture();
	const std::string text = quader_io::serialize_document(document);

	require_test(text.find("\"format\": \"quader.qdr\"") != std::string::npos, "serialized DTO should carry the QDR format marker");
	require_test(text.find("\"version\": 3") != std::string::npos, "serialized DTO should carry the current version");
	require_test(text.find("\"document\": {") != std::string::npos, "serialized DTO should use the stable document payload key");
	require_test(text.find("\"session\": {") == std::string::npos, "serialized DTO should not write the legacy session payload key");
	require_test(text.find("\"materialSlot\": 9") != std::string::npos, "serialized DTO should preserve face material slots");
	require_test(text.find("\"normalShading\": \"flat\"") != std::string::npos, "serialized DTO should preserve face normal shading");
	require_test(text.find("\"hardNormalEdges\": []") != std::string::npos, "serialized DTO should preserve hard normal edges");
	require_test(text.find("\"contentRevision\": 123") != std::string::npos, "serialized DTO should preserve content revision");
	require_test(text.find("\"lights\": [") != std::string::npos, "serialized DTO should preserve light entities");
	require_test(text.find("\"shadowMode\": \"soft\"") != std::string::npos, "serialized DTO should preserve light shadow mode");

	const quader_io::QdrReadResult result = quader_io::deserialize_document(text);
	require_test(result.ok, result.message.empty() ? "DTO should deserialize" : result.message);
	require_test(result.document.objects.size() == 1, "DTO should preserve object count");
	require_test(result.document.next_object_id == 8, "DTO should preserve next object id");
	require_test(result.document.active_object_id == 7, "DTO should preserve active object id");
        require_test(result.document.selection_mode ==
                         quader_io::QdrSelectionMode::Face,
                     "DTO should preserve selection mode");
        require_test(result.document.objects.front().document.faces.front().material_slot == 9, "DTO should preserve face material slot");
        require_test(result.document.objects.front()
                             .document.faces.front()
                             .normal_shading ==
                         quader_poly::SurfaceShading::Flat,
                     "DTO should preserve face normal shading");
        require_test(result.document.objects.front().document.hard_normal_edges.empty(), "DTO should preserve hard normal edge state");
	require_test(result.document.objects.front().selection.faces == document.objects.front().selection.faces, "DTO should preserve selected faces");
	require_test(result.document.objects.front().document.faces.front().uvs.size() == 3, "DTO should preserve explicit UV loops");
	require_test(result.document.lights.size() == 1, "DTO should preserve light count");
	require_test(result.document.next_light_id == 4, "DTO should preserve next light id");
	require_test(result.document.active_light_id == 3, "DTO should preserve active light id");
        require_test(result.document.lights.front().type ==
                         quader_io::QdrLightType::Spot,
                     "DTO should preserve light type");
        require_test(result.document.lights.front().color_abgr == 0xFFEFF7FFU, "DTO should preserve light color");
	require_test(result.document.lights.front().inner_spot_angle == 30.0F, "DTO should preserve inner spot angle");
}

void dto_roundtrip_preserves_normal_control_data()
{
	quader_io::QdrDocument document = make_qdr_document_fixture();
        document.objects.front().document.faces.front().normal_shading =
            quader_poly::SurfaceShading::Smooth;
        document.objects.front().document.hard_normal_edges = {
		quader_poly::make_edge(
			document.objects.front().document.faces.front().vertices[0],
			document.objects.front().document.faces.front().vertices[1]),
	};

	const std::string text = quader_io::serialize_document(document);
	require_test(text.find("\"normalShading\": \"smooth\"") != std::string::npos, "serialized DTO should preserve smooth face shading");
	require_test(text.find("\"hardNormalEdges\": [") != std::string::npos, "serialized DTO should preserve hard normal edge payloads");

	const quader_io::QdrReadResult result = quader_io::deserialize_document(text);
	require_test(result.ok, result.message.empty() ? "normal-control DTO should deserialize" : result.message);
        require_test(result.document.objects.front()
                             .document.faces.front()
                             .normal_shading ==
                         quader_poly::SurfaceShading::Smooth,
                     "DTO should preserve smooth face shading");
        require_test(result.document.objects.front().document.hard_normal_edges.size() == 1U,
		"DTO should preserve hard normal edges");
}

void dto_roundtrip_preserves_material_slot_assignment()
{
	quader_io::QdrDocument document = make_qdr_document_fixture();
	quader_io::QdrObject& object = document.objects.front();
	object.document.faces.front().material_slot = 0U;
	const quader_poly::OperationResult assigned =
		quader_poly::assign_selected_face_material_slot(object.document, object.selection, 14U);
	require_test(assigned.changed, "material-slot assignment fixture should update the selected face");

	const std::string text = quader_io::serialize_document(document);
	require_test(text.find("\"materialSlot\": 14") != std::string::npos,
		"serialized DTO should preserve assigned face material slot");

	const quader_io::QdrReadResult result = quader_io::deserialize_document(text);
	require_test(result.ok, result.message.empty() ? "material-slot DTO should deserialize" : result.message);
	require_test(result.document.objects.front().document.faces.front().material_slot == 14U,
		"DTO should preserve assigned face material slot");
}

void dto_reader_accepts_golden_stable_document_payload_v2_fixture()
{
  const std::string text = read_io_fixture(kStableDocumentV2FixtureName);
  const quader_io::QdrReadResult result = quader_io::deserialize_document(text);
  require_test(result.ok, result.message.empty()
                              ? "stable v2 golden fixture should deserialize"
                              : result.message);
  require_test(result.document.next_object_id == 8,
               "stable v2 golden fixture should preserve next object id");
  require_test(result.document.active_object_id == 7,
               "stable v2 golden fixture should preserve active object id");
  require_test(result.document.selection_mode ==
                   quader_io::QdrSelectionMode::Face,
               "stable v2 golden fixture should preserve selection mode");
  require_test(result.document.content_revision == 123,
               "stable v2 golden fixture should preserve content revision");
  require_test(result.document.selection_revision == 45,
               "stable v2 golden fixture should preserve selection revision");
  require_test(result.document.objects.size() == 1,
               "stable v2 golden fixture should preserve object count");
  require_test(result.document.objects.front().id == 7,
               "stable v2 golden fixture should preserve object id");
  require_test(result.document.objects.front().material_id == 3,
               "stable v2 golden fixture should preserve object material");
  require_test(result.document.objects.front().document.vertices.size() == 3,
               "stable v2 golden fixture should preserve vertices");
  require_test(result.document.objects.front().document.faces.size() == 1,
               "stable v2 golden fixture should preserve faces");
  require_test(
      result.document.objects.front().document.faces.front().material_slot == 9,
      "stable v2 golden fixture should preserve face material slot");
  require_test(
      result.document.objects.front().document.faces.front().normal_shading ==
          quader_poly::SurfaceShading::Flat,
      "stable v2 golden fixture should preserve face normal shading");
  require_test(
      result.document.objects.front().document.hard_normal_edges.empty(),
      "stable v2 golden fixture should preserve hard normal edges");
  require_test(
      result.document.objects.front().document.faces.front().uvs.size() == 3,
      "stable v2 golden fixture should preserve face UV loops");
  require_test(result.document.objects.front().selection.has_active,
               "stable v2 golden fixture should preserve active selection");
  require_test(
      result.document.lights.empty(),
      "stable v2 golden fixture should migrate with no light entities");
  require_test(result.document.next_light_id == 1,
               "stable v2 golden fixture should default the next light id");
  require_test(result.document.active_light_id == -1,
               "stable v2 golden fixture should default the active light id");
}

void dto_writer_matches_golden_stable_document_payload_v2_fixture()
{
  const std::string expected =
      canonical_fixture_text(read_io_fixture(kStableDocumentFixtureName));
  const std::string serialized = canonical_fixture_text(
      quader_io::serialize_document(make_qdr_document_fixture()));
  require_test(serialized == expected,
               canonical_text_mismatch_message(expected, serialized));

  const quader_io::QdrReadResult result =
      quader_io::deserialize_document(expected);
  require_test(
      result.ok,
      result.message.empty()
          ? "stable v3 golden fixture should deserialize before rewrite"
          : result.message);
  const std::string rewritten =
      canonical_fixture_text(quader_io::serialize_document(result.document));
  require_test(rewritten == expected,
               "DTO reader/writer should round-trip the stable v3 fixture to "
               "deterministic canonical output");

  const quader_io::QdrReadResult rewritten_result =
      quader_io::deserialize_document(rewritten);
  require_test(rewritten_result.ok,
               rewritten_result.message.empty()
                   ? "rewritten stable v3 fixture should deserialize"
                   : rewritten_result.message);
  const std::string second_rewrite = canonical_fixture_text(
      quader_io::serialize_document(rewritten_result.document));
  require_test(second_rewrite == rewritten,
               "canonical DTO serialization should be idempotent across "
               "repeated round-trips");
}

void dto_reader_accepts_legacy_session_payload_alias_as_v1_migration()
{
  const std::string text = read_io_fixture(kLegacySessionPayloadFixtureName);

  const quader_io::QdrReadResult result = quader_io::deserialize_document(text);
  require_test(result.ok,
               result.message.empty()
                   ? "DTO reader should accept legacy session payload alias"
                   : result.message);
  require_test(result.document.objects.front().id == 7,
               "legacy session payload alias should preserve object ids");

  const std::string rewritten = quader_io::serialize_document(result.document);
  require_test(rewritten.find("\"document\": {") != std::string::npos,
               "legacy session payload should migrate back to the stable "
               "document key when written");
  require_test(
      rewritten.find("\"session\": {") == std::string::npos,
      "legacy session payload should not be re-emitted by the stable writer");
}

void dto_reader_prefers_stable_document_payload_over_legacy_session_alias()
{
	const std::string text = replace_once(
		stable_document_payload_v2_fixture(),
		"\"document\": {",
		"\"session\": { \"nextObjectId\": 0 },\n  \"document\": {");

	const quader_io::QdrReadResult result = quader_io::deserialize_document(text);
	require_test(result.ok, result.message.empty() ? "DTO reader should prefer stable document payloads over legacy aliases" : result.message);
	require_test(result.document.objects.front().id == 7, "stable document payload should win when both payload keys are present");
}

void dto_version_support_is_explicit_and_current_only()
{
  require_test(quader_io::kQdrMinSupportedFormatVersion == 1,
               "v1 is the oldest supported QDR document version; add migration "
               "fixtures before lowering this guard");
  require_test(quader_io::kQdrFormatVersion == 3,
               "v3 is the current QDR document version; add current-version "
               "golden fixtures before bumping this guard");
  require_test(
      quader_io::is_supported_document_version(quader_io::kQdrFormatVersion),
      "current DTO format version should be supported");
  require_test(quader_io::is_supported_document_version(
                   quader_io::kQdrMinSupportedFormatVersion),
               "minimum DTO format version should be supported");
  require_test(!quader_io::is_supported_document_version(0),
               "version zero should not be supported");
  require_test(!quader_io::is_supported_document_version(
                   quader_io::kQdrFormatVersion + 1),
               "future DTO versions should require an explicit migration");
}

void dto_reader_rejects_invalid_header_shapes()
{
	require_read_failure_contains("[]", "root must be an object", "DTO reader should reject non-object roots");
	require_read_failure_contains("{\"version\":1,\"document\":{}}", "missing its format header", "DTO reader should reject files without a format header");
	require_read_failure_contains("{\"format\":7,\"version\":1,\"document\":{}}", "format header must be a string", "DTO reader should reject non-string format headers");
	require_read_failure_contains("{\"format\":\"quader.qdr\",\"document\":{}}", "missing its version header", "DTO reader should reject files without a version header");
	require_read_failure_contains("{\"format\":\"quader.qdr\",\"version\":\"1\",\"document\":{}}", "version header must be an integer", "DTO reader should reject string version headers");
	require_read_failure_contains("{\"format\":\"quader.qdr\",\"version\":1.5,\"document\":{}}", "version header must be an integer", "DTO reader should reject floating point version headers");
}

void dto_reader_rejects_future_versions_before_payload_migration()
{
  const std::string future_version_header =
      "\"version\": " + std::to_string(quader_io::kQdrFormatVersion + 1);
  const std::string future_document_payload =
      replace_once(stable_document_payload_v2_fixture(), "\"version\": 2",
                   future_version_header);
  require_read_failure_contains(
      future_document_payload,
      "Unsupported Quader file version: " +
          std::to_string(quader_io::kQdrFormatVersion + 1),
      "DTO reader should reject future stable document payloads before parsing "
      "the payload");

  const std::string future_legacy_payload =
      replace_once(read_io_fixture(kLegacySessionPayloadFixtureName),
                   "\"version\": 1", future_version_header);
  require_read_failure_contains(
      future_legacy_payload,
      "Unsupported Quader file version: " +
          std::to_string(quader_io::kQdrFormatVersion + 1),
      "DTO reader should reject future legacy session aliases before applying "
      "v1 migration rules");
}

void dto_reader_rejects_malformed_payloads()
{
	require_read_failure_contains("{not json}", "parse error", "DTO reader should reject malformed JSON");
	require_read_failure_contains("{\"format\":\"quader.qdr\",\"version\":0,\"document\":{}}", "Unsupported Quader file version: 0", "DTO reader should reject prehistory versions");
	require_read_failure_contains("{\"format\":\"quader.qdr\",\"version\":999,\"document\":{}}", "Unsupported Quader file version: 999", "DTO reader should reject unsupported future versions");
	require_read_failure_contains("{\"format\":\"wrong\",\"version\":1,\"document\":{}}", "Unsupported Quader file format: wrong", "DTO reader should reject unsupported formats");
	require_read_failure_contains("{\"format\":\"quader.qdr\",\"version\":1}", "missing its document payload", "DTO reader should reject files without a document payload");
	require_read_failure_contains("{\"format\":\"quader.qdr\",\"version\":1,\"document\":{\"nextObjectId\":1,\"activeObjectId\":-1,\"selectionMode\":\"object\",\"objects\":[]}}",
		"at least one object",
		"DTO reader should reject empty documents");
	require_read_failure_contains("{\"format\":\"quader.qdr\",\"version\":1,\"document\":{\"nextObjectId\":1,\"activeObjectId\":0,\"selectionMode\":\"object\",\"objects\":[]}}",
		"invalid active object id",
		"DTO reader should reject zero active object ids");
	require_read_failure_contains("{\"format\":\"quader.qdr\",\"version\":1,\"document\":{\"nextObjectId\":0,\"activeObjectId\":-1,\"selectionMode\":\"object\",\"objects\":[]}}",
		"invalid next object id",
		"DTO reader should reject invalid next object ids");
        require_read_failure_contains(
            read_io_fixture(kMalformedDuplicateFaceIdFixtureName),
            "duplicate face id",
            "DTO reader should reject malformed fixture with duplicate face "
            "ids");
        require_read_failure_contains(replace_once(stable_document_payload_v2_fixture(), "\"uvs\": [\n                { \"x\": 0, \"y\": 0 },\n                { \"x\": 1, \"y\": 0 },\n                { \"x\": 0, \"y\": 1 }\n              ]", "\"uvs\": [{ \"x\": 0, \"y\": 0 }]"),
		"UV count",
		"DTO reader should reject mismatched face UV counts");
}

void dto_reader_rejects_invalid_topology_and_selection()
{
	const std::string missing_vertex = R"json({
  "format": "quader.qdr",
  "version": 1,
  "session": {
    "nextObjectId": 2,
    "activeObjectId": 1,
    "selectionMode": "object",
    "objects": [
      {
        "id": 1,
        "materialId": 0,
        "selected": false,
        "document": {
          "nextVertexId": 4,
          "nextFaceId": 2,
          "vertices": [
            { "id": 1, "position": { "x": 0, "y": 0, "z": 0 } },
            { "id": 2, "position": { "x": 1, "y": 0, "z": 0 } },
            { "id": 3, "position": { "x": 0, "y": 1, "z": 0 } }
          ],
          "faces": [
            { "id": 1, "vertices": [1, 2, 999], "uvs": [], "materialSlot": 0 }
          ]
        },
        "selection": {
          "mode": "vertex",
          "vertices": [],
          "edges": [],
          "faces": [],
          "active": { "hasActive": false, "kind": "vertex", "vertex": 0, "edge": { "a": 0, "b": 0 }, "face": 0 }
        }
      }
    ]
  }
})json";
	require_test(!quader_io::deserialize_document(missing_vertex).ok, "DTO reader should reject faces that reference missing vertices");

	const std::string duplicate_selection = R"json({
  "format": "quader.qdr",
  "version": 1,
  "session": {
    "nextObjectId": 2,
    "activeObjectId": 1,
    "selectionMode": "face",
    "objects": [
      {
        "id": 1,
        "materialId": 0,
        "selected": true,
        "document": {
          "nextVertexId": 4,
          "nextFaceId": 2,
          "vertices": [
            { "id": 1, "position": { "x": 0, "y": 0, "z": 0 } },
            { "id": 2, "position": { "x": 1, "y": 0, "z": 0 } },
            { "id": 3, "position": { "x": 0, "y": 1, "z": 0 } }
          ],
          "faces": [
            { "id": 1, "vertices": [1, 2, 3], "uvs": [], "materialSlot": 0 }
          ]
        },
        "selection": {
          "mode": "face",
          "vertices": [],
          "edges": [],
          "faces": [1, 1],
          "active": { "hasActive": true, "kind": "face", "vertex": 0, "edge": { "a": 0, "b": 0 }, "face": 1 }
        }
      }
    ]
  }
})json";
	require_test(!quader_io::deserialize_document(duplicate_selection).ok, "DTO reader should reject duplicate selected face ids");
}

void dto_reader_rejects_missing_active_object_and_duplicate_objects()
{
	const std::string missing_active_object = R"json({
  "format": "quader.qdr",
  "version": 1,
  "session": {
    "nextObjectId": 3,
    "activeObjectId": 2,
    "selectionMode": "object",
    "objects": [
      {
        "id": 1,
        "materialId": 0,
        "selected": false,
        "document": {
          "nextVertexId": 4,
          "nextFaceId": 2,
          "vertices": [
            { "id": 1, "position": { "x": 0, "y": 0, "z": 0 } },
            { "id": 2, "position": { "x": 1, "y": 0, "z": 0 } },
            { "id": 3, "position": { "x": 0, "y": 1, "z": 0 } }
          ],
          "faces": [
            { "id": 1, "vertices": [1, 2, 3], "uvs": [], "materialSlot": 0 }
          ]
        },
        "selection": {
          "mode": "vertex",
          "vertices": [],
          "edges": [],
          "faces": [],
          "active": { "hasActive": false, "kind": "vertex", "vertex": 0, "edge": { "a": 0, "b": 0 }, "face": 0 }
        }
      }
    ]
  }
})json";
	require_test(!quader_io::deserialize_document(missing_active_object).ok, "DTO reader should reject missing active object ids");

	std::string duplicate_objects = stable_document_payload_v2_fixture();
	const std::string object_marker = "\"id\": 7";
	const std::size_t first_object = duplicate_objects.find(object_marker);
	require_test(first_object != std::string::npos, "fixture should contain object id");
	const std::size_t insert_position = duplicate_objects.rfind("]", duplicate_objects.rfind("}"));
	require_test(insert_position != std::string::npos, "fixture should contain an object array terminator");
	const std::size_t object_start = duplicate_objects.rfind("{", duplicate_objects.find("\"document\"", first_object));
	require_test(object_start != std::string::npos, "fixture should contain an object start");
	duplicate_objects.insert(insert_position, ",\n" + duplicate_objects.substr(object_start, insert_position - object_start));
	require_test(!quader_io::deserialize_document(duplicate_objects).ok, "DTO reader should reject duplicate object ids");
}

void dto_reader_rejects_invalid_edge_selection_and_active_selection()
{
	const std::string invalid_edge_selection = R"json({
  "format": "quader.qdr",
  "version": 1,
  "session": {
    "nextObjectId": 2,
    "activeObjectId": 1,
    "selectionMode": "edge",
    "objects": [
      {
        "id": 1,
        "materialId": 0,
        "selected": true,
        "document": {
          "nextVertexId": 4,
          "nextFaceId": 2,
          "vertices": [
            { "id": 1, "position": { "x": 0, "y": 0, "z": 0 } },
            { "id": 2, "position": { "x": 1, "y": 0, "z": 0 } },
            { "id": 3, "position": { "x": 0, "y": 1, "z": 0 } }
          ],
          "faces": [
            { "id": 1, "vertices": [1, 2, 3], "uvs": [], "materialSlot": 0 }
          ]
        },
        "selection": {
          "mode": "edge",
          "vertices": [],
          "edges": [{ "a": 1, "b": 999 }],
          "faces": [],
          "active": { "hasActive": false, "kind": "vertex", "vertex": 0, "edge": { "a": 0, "b": 0 }, "face": 0 }
        }
      }
    ]
  }
})json";
	require_test(!quader_io::deserialize_document(invalid_edge_selection).ok, "DTO reader should reject selected edges with missing vertices");

	const std::string invalid_active_selection = R"json({
  "format": "quader.qdr",
  "version": 1,
  "session": {
    "nextObjectId": 2,
    "activeObjectId": 1,
    "selectionMode": "vertex",
    "objects": [
      {
        "id": 1,
        "materialId": 0,
        "selected": true,
        "document": {
          "nextVertexId": 4,
          "nextFaceId": 2,
          "vertices": [
            { "id": 1, "position": { "x": 0, "y": 0, "z": 0 } },
            { "id": 2, "position": { "x": 1, "y": 0, "z": 0 } },
            { "id": 3, "position": { "x": 0, "y": 1, "z": 0 } }
          ],
          "faces": [
            { "id": 1, "vertices": [1, 2, 3], "uvs": [], "materialSlot": 0 }
          ]
        },
        "selection": {
          "mode": "vertex",
          "vertices": [],
          "edges": [],
          "faces": [],
          "active": { "hasActive": true, "kind": "vertex", "vertex": 999, "edge": { "a": 0, "b": 0 }, "face": 0 }
        }
      }
    ]
  }
})json";
	require_test(!quader_io::deserialize_document(invalid_active_selection).ok, "DTO reader should reject active selections that reference missing topology");
}

void dto_reader_normalizes_next_ids_without_changing_schema()
{
	const std::string low_next_ids = R"json({
  "format": "quader.qdr",
  "version": 1,
  "document": {
    "nextObjectId": 1,
    "activeObjectId": 7,
    "selectionMode": "face",
    "objects": [
      {
        "id": 7,
        "materialId": 0,
        "selected": true,
        "document": {
          "nextVertexId": 1,
          "nextFaceId": 1,
          "vertices": [
            { "id": 10, "position": { "x": 0, "y": 0, "z": 0 } },
            { "id": 11, "position": { "x": 1, "y": 0, "z": 0 } },
            { "id": 12, "position": { "x": 0, "y": 1, "z": 0 } }
          ],
          "faces": [
            { "id": 20, "vertices": [10, 11, 12], "uvs": [], "materialSlot": 0 }
          ]
        },
        "selection": {
          "mode": "face",
          "vertices": [],
          "edges": [],
          "faces": [20],
          "active": { "hasActive": true, "kind": "face", "vertex": 0, "edge": { "a": 0, "b": 0 }, "face": 20 }
        }
      }
    ]
  }
})json";

	const quader_io::QdrReadResult result = quader_io::deserialize_document(low_next_ids);
	require_test(result.ok, result.message.empty() ? "DTO reader should accept document payload alias with low next ids" : result.message);
	require_test(result.document.next_object_id == 8, "DTO reader should normalize next object id above existing object ids");
	require_test(result.document.objects.front().document.next_vertex_id == 13, "DTO reader should normalize next vertex id above existing vertex ids");
	require_test(result.document.objects.front().document.next_face_id == 21, "DTO reader should normalize next face id above existing face ids");
}

void dto_atomic_writer_replaces_existing_file_with_canonical_payload()
{
	ScopedTestDirectory directory("qdr-document-atomic-success");
	const std::filesystem::path target = directory.path() / "scene.qdr";
	write_text_file(target, "old qdr bytes");

	const quader_io::QdrWriteResult result = quader_io::write_document_atomic(target, make_qdr_document_fixture());
	require_test(result.ok, result.message.empty() ? "atomic DTO writer should succeed" : result.message);

	const std::string text = read_text_file(target);
	require_test(text.find("old qdr bytes") == std::string::npos, "atomic DTO writer should replace existing bytes");
	require_test(text.find("\"document\": {") != std::string::npos, "atomic DTO writer should write the stable document payload");
	require_test(text.find("\"session\": {") == std::string::npos, "atomic DTO writer should not write the legacy session payload");
	const quader_io::QdrReadResult read_result = quader_io::deserialize_document(text);
	require_test(read_result.ok, read_result.message.empty() ? "atomic DTO output should deserialize" : read_result.message);
	require_test(read_result.document.active_object_id == 7, "atomic DTO output should preserve durable object state");
	require_no_qdr_temp_files(directory.path());
}

void dto_atomic_writer_rejects_invalid_document_without_clobbering_existing_file()
{
	ScopedTestDirectory directory("qdr-document-atomic-failure");
	const std::filesystem::path target = directory.path() / "scene.qdr";
	const std::string existing_text = stable_document_payload_v2_fixture();
	write_text_file(target, existing_text);

	quader_io::QdrDocument invalid_document;
	invalid_document.next_object_id = 1;
	invalid_document.active_object_id = -1;
	const quader_io::QdrWriteResult result = quader_io::write_document_atomic(target, invalid_document);
	require_test(!result.ok, "atomic DTO writer should reject invalid serialized DTOs");
	require_test(result.message.find("at least one object") != std::string::npos,
		"atomic DTO writer failure should explain validation failure");
	require_test(read_text_file(target) == existing_text, "atomic DTO writer should not clobber existing files on validation failure");
	require_no_qdr_temp_files(directory.path());
}

} // namespace

TEST(QdrIoDocumentTests, QdrIoDocumentTests) {
	dto_roundtrip_preserves_portable_document_data();
	dto_roundtrip_preserves_normal_control_data();
	dto_roundtrip_preserves_material_slot_assignment();
	dto_reader_accepts_golden_stable_document_payload_v2_fixture();
	dto_writer_matches_golden_stable_document_payload_v2_fixture();
	dto_reader_accepts_legacy_session_payload_alias_as_v1_migration();
	dto_reader_prefers_stable_document_payload_over_legacy_session_alias();
	dto_version_support_is_explicit_and_current_only();
	dto_reader_rejects_invalid_header_shapes();
	dto_reader_rejects_future_versions_before_payload_migration();
	dto_reader_rejects_malformed_payloads();
	dto_reader_rejects_invalid_topology_and_selection();
	dto_reader_rejects_missing_active_object_and_duplicate_objects();
	dto_reader_rejects_invalid_edge_selection_and_active_selection();
	dto_reader_normalizes_next_ids_without_changing_schema();
	dto_atomic_writer_replaces_existing_file_with_canonical_payload();
	dto_atomic_writer_rejects_invalid_document_without_clobbering_existing_file();
}
