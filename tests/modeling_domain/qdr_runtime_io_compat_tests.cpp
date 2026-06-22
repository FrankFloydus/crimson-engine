////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.h"

#include <editor/modeling/runtime/session.h>
#include <io/qdr/qdr.h>
#include <mesh/polygon/document.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::string replace_once(std::string text, std::string_view from, std::string_view to)
{
	const std::string from_text(from);
	const std::string to_text(to);
	const std::size_t position = text.find(from_text);
	require_test(position != std::string::npos, "fixture text should contain the replacement token");
	text.replace(position, from_text.size(), to_text);
	return text;
}

void require_no_runtime_session_fields(std::string_view text)
{
	require_test(text.find("\"dirty\"") == std::string_view::npos, "compat writer should not serialize live runtime dirty state");
	require_test(text.find("\"history\"") == std::string_view::npos, "compat writer should not serialize live runtime history state");
	require_test(text.find("\"undo\"") == std::string_view::npos, "compat writer should not serialize live runtime undo state");
	require_test(text.find("\"redo\"") == std::string_view::npos, "compat writer should not serialize live runtime redo state");
	require_test(text.find("\"undoStack\"") == std::string_view::npos, "compat writer should not serialize live runtime undo stacks");
	require_test(text.find("\"redoStack\"") == std::string_view::npos, "compat writer should not serialize live runtime redo stacks");
	require_test(text.find("\"activeTool\"") == std::string_view::npos, "compat writer should not serialize live runtime active tools");
	require_test(text.find("\"active_tool\"") == std::string_view::npos, "compat writer should not serialize live runtime active tools");
	require_test(text.find("\"preview\"") == std::string_view::npos, "compat writer should not serialize live runtime preview state");
	require_test(text.find("\"previewTransaction\"") == std::string_view::npos, "compat writer should not serialize live runtime preview transactions");
	require_test(text.find("\"preview_transaction\"") == std::string_view::npos, "compat writer should not serialize live runtime preview transactions");
	require_test(text.find("\"transient\"") == std::string_view::npos, "compat writer should not serialize live runtime transient state");
	require_test(text.find("\"transientState\"") == std::string_view::npos, "compat writer should not serialize live runtime transient state");
	require_test(text.find("\"transient_state\"") == std::string_view::npos, "compat writer should not serialize live runtime transient state");
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

std::string read_text_file(const std::filesystem::path& path)
{
	std::ifstream stream(path, std::ios::in | std::ios::binary);
	require_test(stream.is_open(), "Failed to open test output file: " + path.string());
	std::ostringstream contents;
	contents << stream.rdbuf();
	require_test(!stream.bad(), "Failed to read test output file: " + path.string());
	return contents.str();
}

std::string legacy_session_text_with_runtime_fields(const quader_editor_core::SessionState& state)
{
	std::string legacy_text = replace_once(quader_io::serialize_session_state(state), "\"document\"", "\"session\"");
	return replace_once(legacy_text, "    \"objects\": [",
		"    \"dirty\": true,\n"
		"    \"history\": { \"undo\": [ { \"dirty\": true } ], \"redo\": [ { \"dirty\": true } ] },\n"
		"    \"undoStack\": [ { \"dirty\": true } ],\n"
		"    \"redoStack\": [ { \"dirty\": true } ],\n"
		"    \"activeTool\": \"knife\",\n"
		"    \"previewTransaction\": { \"active\": true, \"changed\": true, \"kind\": \"transform\" },\n"
		"    \"preview\": { \"active\": true },\n"
		"    \"transientState\": { \"hoverObjectId\": 99 },\n"
		"    \"objects\": [");
}

quader_poly::Selection first_face_selection(const quader_poly::Document& document)
{
	quader_poly::Selection selection;
	selection.mode = quader_poly::SelectionMode::face;
	selection.faces.push_back(document.faces.front().id);
	selection.has_active = true;
	selection.active_kind = quader_poly::ElementKind::face;
	selection.active_face = document.faces.front().id;
	return selection;
}

void session_compatibility_api_roundtrips_through_stable_dto()
{
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	require_test(session.create_box_from_bounds({ 4.0F, 0.0F, 0.0F }, { 5.0F, 1.0F, 1.0F }).changed,
		"fixture should create a second object");

	quader_editor_core::SessionState state = session.state();
	state.active_object_id = state.objects.front().id;
	state.selection_mode = quader_editor_core::SelectionMode::face;
	state.next_object_id = 12;
	state.content_revision = 42;
	state.selection_revision = 17;
	state.dirty = true;
	state.objects.front().material_id = 5;
	state.objects.front().selected = true;
	quader_poly::ensure_face_uvs(state.objects.front().document);
	state.objects.front().selection = first_face_selection(state.objects.front().document);
	state.lights.push_back({
		2,
		"Fill",
		true,
		quader_editor_core::LightType::point,
		true,
		{ 2.0F, 3.0F, 4.0F },
		{ 0.0F, -1.0F, 0.0F },
		0xFFCCFFFFU,
		2.5F,
		quader_editor_core::LightShadowMode::hard,
		0.5F,
		0.02F,
		0.3F,
		0.1F,
		15.0F,
		30.0F,
		10.0F,
	});
	state.next_light_id = 3;
	state.active_light_id = 2;

	const quader_io::QdrDocument dto = quader_io::document_from_session_state(state);
	require_test(dto.selection_mode == quader_io::QdrSelectionMode::face, "compat adapter should convert editor selection mode to DTO mode");
	require_test(dto.objects.size() == state.objects.size(), "compat adapter should preserve objects");
	require_test(dto.objects.front().material_id == 5, "compat adapter should preserve material metadata");
	require_test(dto.lights.size() == 1 && dto.lights.front().type == quader_io::QdrLightType::point,
		"compat adapter should preserve light entities");

	const std::string text = quader_io::serialize_session_state(state);
	require_test(text.find("\"document\": {") != std::string::npos, "compat writer should use the stable DTO document payload");
	require_test(text.find("\"session\": {") == std::string::npos, "compat writer should not emit the legacy session payload key");
	require_no_runtime_session_fields(text);
	const quader_io::ReadResult read_result = quader_io::deserialize_session_state(text);
	require_test(read_result.ok, read_result.message.empty() ? "compat session state should deserialize" : read_result.message);
	require_test(!read_result.state.dirty, "loaded compatibility session state should be clean");
	require_test(read_result.state.next_object_id == 12, "compat reader should preserve next object id");
	require_test(read_result.state.active_object_id == state.active_object_id, "compat reader should preserve active object id");
	require_test(read_result.state.selection_mode == quader_editor_core::SelectionMode::face, "compat reader should preserve selection mode");
	require_test(read_result.state.content_revision == 42 && read_result.state.selection_revision == 17, "compat reader should preserve revisions");
	require_test(read_result.state.objects.size() == 2, "compat reader should preserve object count");
	require_test(read_result.state.objects.front().material_id == 5, "compat reader should preserve object metadata");
	require_test(read_result.state.objects.front().selected, "compat reader should preserve selected object flags");
	require_test(read_result.state.objects.front().selection.faces == state.objects.front().selection.faces, "compat reader should preserve component selection");
	require_test(read_result.state.lights.size() == 1, "compat reader should preserve light entities");
	require_test(read_result.state.next_light_id == 3, "compat reader should preserve next light id");
	require_test(read_result.state.active_light_id == 2, "compat reader should preserve active light id");
	require_test(read_result.state.lights.front().color_abgr == 0xFFCCFFFFU, "compat reader should preserve light color");
	require_test(read_result.state.lights.front().shadow_mode == quader_editor_core::LightShadowMode::hard,
		"compat reader should preserve light shadow mode");
}

void session_compatibility_reader_accepts_legacy_session_payload_but_rewrites_stable_document_payload()
{
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	quader_editor_core::SessionState state = session.state();
	state.next_object_id = 4;
	state.active_object_id = state.objects.front().id;
	state.selection_mode = quader_editor_core::SelectionMode::object;

	const std::string legacy_text = replace_once(quader_io::serialize_session_state(state), "\"document\"", "\"session\"");
	const quader_io::ReadResult result = quader_io::deserialize_session_state(legacy_text);
	require_test(result.ok, result.message.empty() ? "compat reader should accept legacy session payloads" : result.message);
	require_test(result.state.active_object_id == state.active_object_id, "compat reader should preserve active object id from legacy payloads");

	const std::string rewritten = quader_io::serialize_session_state(result.state);
	require_test(rewritten.find("\"document\": {") != std::string::npos, "compat writer should rewrite legacy payloads through the stable document key");
	require_test(rewritten.find("\"session\": {") == std::string::npos, "compat writer should not re-emit legacy session payloads");
	require_no_runtime_session_fields(rewritten);
}

void session_compatibility_reader_drops_runtime_fields_from_legacy_payloads()
{
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	require_test(session.create_box_from_bounds({ 4.0F, 0.0F, 0.0F }, { 5.0F, 1.0F, 1.0F }).changed,
		"fixture should create a second object");

	quader_editor_core::SessionState state = session.state();
	state.next_object_id = 9;
	state.active_object_id = state.objects.front().id;
	state.selection_mode = quader_editor_core::SelectionMode::face;
	state.content_revision = 33;
	state.selection_revision = 7;
	state.dirty = true;

	const std::string legacy_text = legacy_session_text_with_runtime_fields(state);
	const quader_io::ReadResult result = quader_io::deserialize_session_state(legacy_text);
	require_test(result.ok, result.message.empty() ? "compat reader should accept legacy runtime fields" : result.message);
	require_test(!result.state.dirty, "legacy runtime dirty fields should not make loaded session state dirty");
	require_test(result.state.next_object_id == 9, "compat reader should preserve durable next object id from legacy payloads");
	require_test(result.state.active_object_id == state.active_object_id, "compat reader should preserve durable active object id from legacy payloads");
	require_test(result.state.selection_mode == quader_editor_core::SelectionMode::face, "compat reader should preserve durable selection mode from legacy payloads");
	require_test(result.state.content_revision == 33 && result.state.selection_revision == 7,
		"compat reader should preserve durable revisions from legacy payloads");

	quader_editor_core::EditorSession loaded_session(result.state);
	const quader_editor_core::HistoryStatus history_status = loaded_session.history_status();
	require_test(!loaded_session.dirty(), "legacy runtime fields should load into a clean editor session");
	require_test(!history_status.can_undo && !history_status.can_redo && history_status.undo_count == 0 && history_status.redo_count == 0,
		"legacy history fields should not hydrate live undo or redo stacks");
	const quader_editor_core::PreviewTransactionStatus preview_status = loaded_session.preview_transaction_status();
	require_test(!preview_status.active && !preview_status.changed && preview_status.kind == quader_editor_core::PreviewTransactionKind::none,
		"legacy preview fields should not hydrate live preview transactions");

	const std::string rewritten = quader_io::serialize_session_state(result.state);
	require_test(rewritten.find("\"document\": {") != std::string::npos, "compat writer should rewrite legacy runtime payloads through the stable document key");
	require_test(rewritten.find("\"session\": {") == std::string::npos, "compat writer should not re-emit legacy runtime payloads");
	require_no_runtime_session_fields(rewritten);
}

void session_compatibility_api_rejects_invalid_dto_text()
{
	require_test(!quader_io::deserialize_session_state("{not json}").ok, "compat reader should reject malformed JSON");
	require_test(!quader_io::deserialize_session_state("{\"format\":\"quader.qdr\",\"version\":999}").ok, "compat reader should reject unsupported versions");
}

void session_compatibility_atomic_writer_uses_stable_dto_payload()
{
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	quader_editor_core::SessionState state = session.state();
	state.dirty = true;
	state.content_revision = 55;
	state.selection_revision = 9;

	ScopedTestDirectory directory("qdr-session-atomic-success");
	const std::filesystem::path target = directory.path() / "session.qdr";
	const quader_io::QdrWriteResult result = quader_io::write_session_state_atomic(target, state);
	require_test(result.ok, result.message.empty() ? "compat atomic writer should succeed" : result.message);

	const std::string text = read_text_file(target);
	require_test(text.find("\"document\": {") != std::string::npos, "compat atomic writer should use the stable document payload");
	require_test(text.find("\"session\": {") == std::string::npos, "compat atomic writer should not emit the legacy session payload key");
	require_no_runtime_session_fields(text);
	const quader_io::ReadResult read_result = quader_io::deserialize_session_state(text);
	require_test(read_result.ok, read_result.message.empty() ? "compat atomic writer output should deserialize" : read_result.message);
	require_test(read_result.state.content_revision == 55 && read_result.state.selection_revision == 9,
		"compat atomic writer should preserve durable revisions");
	require_test(!read_result.state.dirty, "compat atomic writer should not persist live dirty state");
}

} // namespace

TEST_CASE("qdr_io_session_tests", "[io-session]")
{
	session_compatibility_api_roundtrips_through_stable_dto();
	session_compatibility_reader_accepts_legacy_session_payload_but_rewrites_stable_document_payload();
	session_compatibility_reader_drops_runtime_fields_from_legacy_payloads();
	session_compatibility_api_rejects_invalid_dto_text();
	session_compatibility_atomic_writer_uses_stable_dto_payload();
}
