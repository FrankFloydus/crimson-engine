////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <io/qdr/qdr_document.h>

#include <modeling/runtime/session.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace quader_io {

/**
 * Stores the Read Result data contract used by the QDR document persistence layer.
 */
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
