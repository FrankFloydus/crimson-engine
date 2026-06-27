////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace quader_io {

/**
 * Stores the shared QDR atomic text write result.
 */
struct QdrAtomicWriteResult {
    bool ok = false;
    std::string message;
};

using QdrAtomicWriteValidator = std::function<QdrAtomicWriteResult(const std::filesystem::path& temp_path)>;

[[nodiscard]] QdrAtomicWriteResult write_qdr_text_atomic(
        const std::filesystem::path& target,
        std::string_view text,
        QdrAtomicWriteValidator validate_temp = {});

} // namespace quader_io
