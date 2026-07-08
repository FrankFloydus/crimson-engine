////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace quader::modeling {

/**
 * Enumerates structured SDK error categories.
 */
enum class ErrorCode {
  Ok,
  InvalidArgument,
  InvalidId,
  StaleId,
  UnsupportedOperation,
  ValidationFailed,
  IoError,
  InternalError,
};

/**
 * Enumerates SDK diagnostic severities.
 */
enum class DiagnosticSeverity {
  Info,
  Warning,
  Error,
};

/**
 * Stores one structured SDK diagnostic.
 */
struct Diagnostic {
  std::string code;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  std::string path;
  std::string message;
};

/**
 * Stores a structured SDK error and related diagnostics.
 */
struct Error {
  ErrorCode code = ErrorCode::Ok;
  std::string message;
  std::vector<Diagnostic> diagnostics;
};

/**
 * Carries either a successful SDK value or a structured error.
 */
template <typename T> class Result {
public:
  Result() = default;

  [[nodiscard]] static Result success(T value) {
    Result result;
    result.ok_ = true;
    result.value_ = std::move(value);
    return result;
  }

  [[nodiscard]] static Result failure(Error error) {
    Result result;
    result.ok_ = false;
    result.error_ = std::move(error);
    return result;
  }

  [[nodiscard]] bool ok() const noexcept { return ok_; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok_; }
  [[nodiscard]] const T &value() const & { return value_; }
  [[nodiscard]] T &value() & { return value_; }
  [[nodiscard]] T &&value() && { return std::move(value_); }
  [[nodiscard]] const T &or_throw() const & {
    if (!ok_) {
      throw std::runtime_error(error_.message);
    }
    return value_;
  }
  [[nodiscard]] T &or_throw() & {
    if (!ok_) {
      throw std::runtime_error(error_.message);
    }
    return value_;
  }
  [[nodiscard]] T &&or_throw() && {
    if (!ok_) {
      throw std::runtime_error(error_.message);
    }
    return std::move(value_);
  }
  [[nodiscard]] T value_or(T fallback) const {
    return ok_ ? value_ : std::move(fallback);
  }
  [[nodiscard]] const Error &error() const noexcept { return error_; }
  [[nodiscard]] const std::string &error_message() const noexcept {
    return error_.message;
  }

private:
  bool ok_ = false;
  T value_{};
  Error error_{};
};

[[nodiscard]] inline Error make_error(ErrorCode code, std::string message) {
  return Error{.code = code, .message = std::move(message)};
}

} // namespace quader::modeling
