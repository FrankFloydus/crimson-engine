////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "guardrail_file_scan.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader::test::guardrail {
namespace {

std::filesystem::path resolved_path(const std::filesystem::path &path) {
  return path.is_absolute() ? path : source_root() / path;
}

} // namespace

std::filesystem::path source_root() {
  return std::filesystem::path(QUADER_SOURCE_DIR);
}

std::filesystem::path source_path(const std::filesystem::path &relative_path) {
  return source_root() / relative_path;
}

std::filesystem::path
source_path(std::initializer_list<std::string_view> parts) {
  std::filesystem::path path = source_root();
  for (std::string_view part : parts) {
    path /= std::string(part);
  }
  return path;
}

std::string read_file(const std::filesystem::path &path) {
  const std::filesystem::path resolved = resolved_path(path);
  std::ifstream file(resolved);
  EXPECT_TRUE(file.is_open()) << resolved.string();
  if (!file.is_open()) {
    return {};
  }
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

std::string read_source_file(const std::filesystem::path &relative_path) {
  return read_file(source_path(relative_path));
}

std::string read_source_file(std::initializer_list<std::string_view> parts) {
  return read_file(source_path(parts));
}

std::string normalized_relative(const std::filesystem::path &path) {
  const std::filesystem::path resolved = resolved_path(path);
  return std::filesystem::relative(resolved, source_root()).generic_string();
}

bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

bool contains(std::string_view source, std::string_view needle) {
    return source.find(needle) != std::string_view::npos;
}

bool contains_any(const std::string &source,
                  const std::vector<std::string_view> &needles) {
  return std::any_of(needles.begin(), needles.end(),
                     [&](std::string_view needle) {
                       return source.find(needle) != std::string::npos;
                     });
}

std::vector<std::filesystem::path>
files_with_extension(const std::filesystem::path &root,
                     std::string_view extension) {
  std::vector<std::filesystem::path> files;
  const std::filesystem::path resolved_root = resolved_path(root);
  if (!std::filesystem::exists(resolved_root)) {
    return files;
  }

  if (std::filesystem::is_regular_file(resolved_root)) {
    if (resolved_root.extension() == extension) {
      files.push_back(resolved_root);
    }
    return files;
  }

  for (const std::filesystem::directory_entry &entry :
       std::filesystem::recursive_directory_iterator(resolved_root)) {
    if (entry.is_regular_file() && entry.path().extension() == extension) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::vector<std::filesystem::path>
files_named_under(const std::filesystem::path &root,
                  std::string_view filename) {
  std::vector<std::filesystem::path> files;
  const std::filesystem::path resolved_root = resolved_path(root);
  if (!std::filesystem::exists(resolved_root)) {
    return files;
  }

  if (std::filesystem::is_regular_file(resolved_root)) {
    if (resolved_root.filename() == filename) {
      files.push_back(resolved_root);
    }
    return files;
  }

  for (const std::filesystem::directory_entry &entry :
       std::filesystem::recursive_directory_iterator(resolved_root)) {
    if (entry.is_regular_file() && entry.path().filename() == filename) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::vector<std::filesystem::path>
source_files_under(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> files;
  for (const std::string_view extension :
       {std::string_view{".hpp"}, std::string_view{".cpp"}}) {
    std::vector<std::filesystem::path> with_extension =
        files_with_extension(root, extension);
    files.insert(files.end(), with_extension.begin(), with_extension.end());
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::vector<std::string> split_lines(const std::string &source) {
  std::vector<std::string> result;
  std::string line;
  for (const char ch : source) {
    if (ch == '\n') {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      result.push_back(line);
      line.clear();
    } else {
      line.push_back(ch);
    }
  }
  if (!line.empty()) {
    result.push_back(line);
  }
  return result;
}

std::vector<std::string>
lines_containing(const std::string &source,
                 const std::vector<std::string_view> &needles) {
  std::vector<std::string> matches;
  for (const std::string &line : split_lines(source)) {
    if (contains_any(line, needles)) {
      matches.push_back(line);
    }
  }
  return matches;
}

std::string source_slice(std::string_view source, std::string_view begin_marker,
                         std::string_view end_marker) {
  const std::size_t begin = source.find(begin_marker);
  if (begin == std::string_view::npos) {
    return {};
  }
  const std::size_t end = source.find(end_marker, begin + begin_marker.size());
  if (end == std::string_view::npos) {
    return std::string(source.substr(begin));
  }
  return std::string(source.substr(begin, end - begin + end_marker.size()));
}

} // namespace quader::test::guardrail
