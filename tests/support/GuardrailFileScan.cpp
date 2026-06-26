////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "GuardrailFileScan.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader::test::guardrail {
namespace {

std::filesystem::path resolvedPath(const std::filesystem::path& path) {
    return path.is_absolute() ? path : sourceRoot() / path;
}

} // namespace

std::filesystem::path sourceRoot() {
    return std::filesystem::path(QUADER_SOURCE_DIR);
}

std::filesystem::path sourcePath(const std::filesystem::path& relativePath) {
    return sourceRoot() / relativePath;
}

std::filesystem::path sourcePath(std::initializer_list<std::string_view> parts) {
    std::filesystem::path path = sourceRoot();
    for (std::string_view part : parts) {
        path /= std::string(part);
    }
    return path;
}

std::string readFile(const std::filesystem::path& path) {
    const std::filesystem::path resolved = resolvedPath(path);
    std::ifstream file(resolved);
    EXPECT_TRUE(file.is_open()) << resolved.string();
    if (!file.is_open()) {
        return {};
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string readSourceFile(const std::filesystem::path& relativePath) {
    return readFile(sourcePath(relativePath));
}

std::string readSourceFile(std::initializer_list<std::string_view> parts) {
    return readFile(sourcePath(parts));
}

std::string normalizedRelative(const std::filesystem::path& path) {
    const std::filesystem::path resolved = resolvedPath(path);
    return std::filesystem::relative(resolved, sourceRoot()).generic_string();
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool contains(std::string_view source, std::string_view needle) {
    return source.find(needle) != std::string_view::npos;
}

bool containsAny(const std::string& source, const std::vector<std::string_view>& needles) {
    return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
        return source.find(needle) != std::string::npos;
    });
}

std::vector<std::filesystem::path> filesWithExtension(
        const std::filesystem::path& root,
        std::string_view extension) {
    std::vector<std::filesystem::path> files;
    const std::filesystem::path resolvedRoot = resolvedPath(root);
    if (!std::filesystem::exists(resolvedRoot)) {
        return files;
    }

    if (std::filesystem::is_regular_file(resolvedRoot)) {
        if (resolvedRoot.extension() == extension) {
            files.push_back(resolvedRoot);
        }
        return files;
    }

    for (const std::filesystem::directory_entry& entry :
            std::filesystem::recursive_directory_iterator(resolvedRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::filesystem::path> filesNamedUnder(
        const std::filesystem::path& root,
        std::string_view filename) {
    std::vector<std::filesystem::path> files;
    const std::filesystem::path resolvedRoot = resolvedPath(root);
    if (!std::filesystem::exists(resolvedRoot)) {
        return files;
    }

    if (std::filesystem::is_regular_file(resolvedRoot)) {
        if (resolvedRoot.filename() == filename) {
            files.push_back(resolvedRoot);
        }
        return files;
    }

    for (const std::filesystem::directory_entry& entry :
            std::filesystem::recursive_directory_iterator(resolvedRoot)) {
        if (entry.is_regular_file() && entry.path().filename() == filename) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::filesystem::path> sourceFilesUnder(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    for (const std::string_view extension : {std::string_view{".h"}, std::string_view{".cpp"}}) {
        std::vector<std::filesystem::path> withExtension = filesWithExtension(root, extension);
        files.insert(files.end(), withExtension.begin(), withExtension.end());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::string> splitLines(const std::string& source) {
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

std::vector<std::string> linesContaining(
        const std::string& source,
        const std::vector<std::string_view>& needles) {
    std::vector<std::string> matches;
    for (const std::string& line : splitLines(source)) {
        if (containsAny(line, needles)) {
            matches.push_back(line);
        }
    }
    return matches;
}

std::string sourceSlice(
        std::string_view source,
        std::string_view beginMarker,
        std::string_view endMarker) {
    const std::size_t begin = source.find(beginMarker);
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = source.find(endMarker, begin + beginMarker.size());
    if (end == std::string_view::npos) {
        return std::string(source.substr(begin));
    }
    return std::string(source.substr(begin, end - begin + endMarker.size()));
}

} // namespace quader::test::guardrail
