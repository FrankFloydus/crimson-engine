#pragma once

#include <quader_io/qdr_document.h>

#include <quader_editor_core/session.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace quader_io {

struct ReadResult {
    bool ok = false;
    quader_editor_core::SessionState state;
    std::string message;
};

[[nodiscard]] QdrDocument document_from_session_state(const quader_editor_core::SessionState& state);
[[nodiscard]] quader_editor_core::SessionState session_state_from_document(const QdrDocument& document);
[[nodiscard]] std::string serialize_session_state(const quader_editor_core::SessionState& state);
[[nodiscard]] ReadResult deserialize_session_state(std::string_view text);
[[nodiscard]] QdrWriteResult write_session_state_atomic(
	const std::filesystem::path& path,
	const quader_editor_core::SessionState& state);

} // namespace quader_io
