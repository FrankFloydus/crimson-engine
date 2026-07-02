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

[[nodiscard]] std::filesystem::path source_root();
[[nodiscard]] std::filesystem::path
source_path(const std::filesystem::path &relative_path);
[[nodiscard]] std::filesystem::path
source_path(std::initializer_list<std::string_view> parts);
[[nodiscard]] std::string read_file(const std::filesystem::path &path);
[[nodiscard]] std::string
read_source_file(const std::filesystem::path &relative_path);
[[nodiscard]] std::string
read_source_file(std::initializer_list<std::string_view> parts);
[[nodiscard]] std::string
normalized_relative(const std::filesystem::path &path);

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix);
[[nodiscard]] bool contains(std::string_view source, std::string_view needle);
[[nodiscard]] bool contains_any(const std::string &source,
                                const std::vector<std::string_view> &needles);

[[nodiscard]] std::vector<std::filesystem::path>
files_with_extension(const std::filesystem::path &root,
                     std::string_view extension);
[[nodiscard]] std::vector<std::filesystem::path>
files_named_under(const std::filesystem::path &root, std::string_view filename);
[[nodiscard]] std::vector<std::filesystem::path>
source_files_under(const std::filesystem::path &root);
[[nodiscard]] std::vector<std::string> split_lines(const std::string &source);
[[nodiscard]] std::vector<std::string>
lines_containing(const std::string &source,
                 const std::vector<std::string_view> &needles);
[[nodiscard]] std::string source_slice(std::string_view source,
                                       std::string_view begin_marker,
                                       std::string_view end_marker);

} // namespace quader::test::guardrail
