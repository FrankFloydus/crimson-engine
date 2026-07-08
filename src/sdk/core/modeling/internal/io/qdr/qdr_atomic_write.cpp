////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_atomic_write.hpp"

#include "diagnostics/profile.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace quader_io {
namespace {

std::string system_error_message(const std::string& action, const std::error_code& error) {
    if (!error) {
        return action;
    }
    return action + ": " + error.message();
}

std::string windows_error_message(const std::string& action, unsigned long error_code) {
    std::error_code error(static_cast<int>(error_code), std::system_category());
    return system_error_message(action, error);
}

std::string unique_temp_suffix(std::uint64_t attempt) {
    static std::atomic_uint64_t counter{0};
    const std::uint64_t tick =
            static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::uint64_t value = counter.fetch_add(1, std::memory_order_relaxed) ^ tick;
    std::ostringstream stream;
    stream << std::hex << value << "-" << attempt;
    return stream.str();
}

std::filesystem::path target_directory_for_atomic_write(const std::filesystem::path& target) {
    const std::filesystem::path directory = target.parent_path();
    return directory.empty() ? std::filesystem::path(".") : directory;
}

std::filesystem::path make_temp_path_for_atomic_write(
        const std::filesystem::path& target,
        const std::filesystem::path& directory,
        std::uint64_t attempt) {
    const std::string file_name = target.filename().string();
    return directory / ("." + file_name + "." + unique_temp_suffix(attempt) + ".tmp");
}

void delete_temp_file_best_effort(const std::filesystem::path& temp_path) {
    std::error_code ignored;
    std::filesystem::remove(temp_path, ignored);
}

QdrAtomicWriteResult write_text_file(const std::filesystem::path& temp_path, std::string_view text) {
    QDR_PROFILE_FUNCTION();
    std::ofstream stream(temp_path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return {false, "Could not create temporary QDR file: " + temp_path.string()};
    }

    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream.good()) {
        return {false, "Could not write temporary QDR file: " + temp_path.string()};
    }

    stream.flush();
    if (!stream.good()) {
        return {false, "Could not flush temporary QDR file: " + temp_path.string()};
    }

    stream.close();
    if (!stream.good()) {
        return {false, "Could not close temporary QDR file: " + temp_path.string()};
    }

    return {true, {}};
}

QdrAtomicWriteResult replace_with_temp_file(
        const std::filesystem::path& temp_path,
        const std::filesystem::path& target) {
    QDR_PROFILE_FUNCTION();
#ifdef _WIN32
    if (::MoveFileExW(
                temp_path.c_str(),
                target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        return {false, windows_error_message("Could not replace QDR file", ::GetLastError())};
    }
    return {true, {}};
#else
    std::error_code error;
    std::filesystem::rename(temp_path, target, error);
    if (error) {
        return {false, system_error_message("Could not replace QDR file", error)};
    }
    return {true, {}};
#endif
}

} // namespace

QdrAtomicWriteResult write_qdr_text_atomic(
        const std::filesystem::path& target,
        std::string_view text,
        QdrAtomicWriteValidator validate_temp) {
    QDR_PROFILE_FUNCTION();
    if (target.empty() || target.filename().empty()) {
        return {false, "QDR file path is empty."};
    }

    const std::filesystem::path directory = target_directory_for_atomic_write(target);
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return {false, system_error_message("QDR output directory does not exist", error)};
    }

    constexpr std::uint64_t kMaxAttempts = 32;
    for (std::uint64_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
      const std::filesystem::path temp_path =
          make_temp_path_for_atomic_write(target, directory, attempt);
      if (std::filesystem::exists(temp_path, error)) {
        continue;
      }
      if (error) {
        return {false, system_error_message(
                           "Could not inspect temporary QDR file path", error)};
      }

      QdrAtomicWriteResult write_result = [&]() {
        QDR_PROFILE_SCOPE("QDR write temp file");
        return write_text_file(temp_path, text);
      }();
      if (!write_result.ok) {
        delete_temp_file_best_effort(temp_path);
        return write_result;
      }

      if (validate_temp) {
        QdrAtomicWriteResult validation_result = [&]() {
          QDR_PROFILE_SCOPE("QDR validate temp file");
          return validate_temp(temp_path);
        }();
        if (!validation_result.ok) {
          delete_temp_file_best_effort(temp_path);
          return validation_result;
        }
      }

      QdrAtomicWriteResult replace_result = [&]() {
        QDR_PROFILE_SCOPE("QDR replace target file");
        return replace_with_temp_file(temp_path, target);
      }();
      if (!replace_result.ok) {
        delete_temp_file_best_effort(temp_path);
        return replace_result;
      }

      return {true, {}};
    }

    return {false, "Could not allocate a unique temporary QDR file path."};
}

} // namespace quader_io
