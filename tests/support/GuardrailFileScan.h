////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace quader::test::guardrail {

[[nodiscard]] std::filesystem::path sourceRoot();
[[nodiscard]] std::filesystem::path sourcePath(const std::filesystem::path& relativePath);
[[nodiscard]] std::filesystem::path sourcePath(std::initializer_list<std::string_view> parts);
[[nodiscard]] std::string readFile(const std::filesystem::path& path);
[[nodiscard]] std::string readSourceFile(const std::filesystem::path& relativePath);
[[nodiscard]] std::string readSourceFile(std::initializer_list<std::string_view> parts);
[[nodiscard]] std::string normalizedRelative(const std::filesystem::path& path);

[[nodiscard]] bool startsWith(std::string_view value, std::string_view prefix);
[[nodiscard]] bool contains(std::string_view source, std::string_view needle);
[[nodiscard]] bool containsAny(const std::string& source, const std::vector<std::string_view>& needles);

[[nodiscard]] std::vector<std::filesystem::path> filesWithExtension(
        const std::filesystem::path& root,
        std::string_view extension);
[[nodiscard]] std::vector<std::filesystem::path> filesNamedUnder(
        const std::filesystem::path& root,
        std::string_view filename);
[[nodiscard]] std::vector<std::filesystem::path> sourceFilesUnder(const std::filesystem::path& root);
[[nodiscard]] std::vector<std::string> splitLines(const std::string& source);
[[nodiscard]] std::vector<std::string> linesContaining(
        const std::string& source,
        const std::vector<std::string_view>& needles);
[[nodiscard]] std::string sourceSlice(
        std::string_view source,
        std::string_view beginMarker,
        std::string_view endMarker);

} // namespace quader::test::guardrail
