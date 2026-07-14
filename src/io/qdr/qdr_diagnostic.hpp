////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace quader_io {

/**
 * Classifies QDR diagnostics for callers that need structured read feedback.
 */
enum class QdrDiagnosticSeverity {
  Info,
  Warning,
  Error,
};

/**
 * Stores a structured QDR diagnostic. The path field identifies a semantic
 * field or chunk; an empty path identifies the document root.
 */
struct QdrDiagnostic {
  std::string code;
  QdrDiagnosticSeverity severity = QdrDiagnosticSeverity::Error;
  std::string path;
  std::string message;
  std::optional<std::size_t> line;
  std::optional<std::size_t> column;
  std::optional<std::filesystem::path> source_path;
};

using QdrError = QdrDiagnostic;

[[nodiscard]] inline QdrDiagnostic
qdr_make_diagnostic(std::string_view code, QdrDiagnosticSeverity severity,
                    std::string_view path, std::string message,
                    std::optional<std::size_t> line = std::nullopt,
                    std::optional<std::size_t> column = std::nullopt,
                    std::optional<std::filesystem::path> source_path =
                        std::nullopt) {
  return QdrDiagnostic{
      .code = std::string(code),
      .severity = severity,
      .path = std::string(path),
      .message = std::move(message),
      .line = line,
      .column = column,
      .source_path = std::move(source_path),
  };
}

[[nodiscard]] inline QdrError
qdr_make_error(std::string_view code, std::string_view path,
               std::string message,
               std::optional<std::size_t> line = std::nullopt,
               std::optional<std::size_t> column = std::nullopt,
               std::optional<std::filesystem::path> source_path =
                   std::nullopt) {
  return qdr_make_diagnostic(code, QdrDiagnosticSeverity::Error, path,
                             std::move(message), line, column,
                             std::move(source_path));
}

/**
 * Carries a QDR diagnostic through existing exception-based parser code.
 */
class QdrDiagnosticException final : public std::runtime_error {
public:
  explicit QdrDiagnosticException(QdrDiagnostic diagnostic)
      : std::runtime_error(diagnostic.message), diagnostic_(std::move(diagnostic)) {}

  [[nodiscard]] const QdrDiagnostic &diagnostic() const noexcept {
    return diagnostic_;
  }

private:
  QdrDiagnostic diagnostic_;
};

} // namespace quader_io
