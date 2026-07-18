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
#include <variant>
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
  Result()
      : storage_(ErrorArm{Error{
            .code = ErrorCode::InternalError,
            .message = "Result was not initialized.",
        }}) {}

  [[nodiscard]] static Result success(T value) {
    return Result(ValueArm{std::move(value)});
  }

  [[nodiscard]] static Result failure(Error error) {
    if (error.code == ErrorCode::Ok) {
      error.code = ErrorCode::InternalError;
    }
    return Result(ErrorArm{std::move(error)});
  }

  [[nodiscard]] bool ok() const noexcept {
    return std::holds_alternative<ValueArm>(storage_);
  }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] const T &value() const & {
    require_value();
    return std::get<ValueArm>(storage_).value;
  }
  [[nodiscard]] T &value() & {
    require_value();
    return std::get<ValueArm>(storage_).value;
  }
  [[nodiscard]] T &&value() && {
    require_value();
    return std::move(std::get<ValueArm>(storage_).value);
  }
  [[nodiscard]] const T &or_throw() const & {
    if (!ok()) {
      throw std::runtime_error(std::get<ErrorArm>(storage_).error.message);
    }
    return std::get<ValueArm>(storage_).value;
  }
  [[nodiscard]] T &or_throw() & {
    if (!ok()) {
      throw std::runtime_error(std::get<ErrorArm>(storage_).error.message);
    }
    return std::get<ValueArm>(storage_).value;
  }
  [[nodiscard]] T &&or_throw() && {
    if (!ok()) {
      throw std::runtime_error(std::get<ErrorArm>(storage_).error.message);
    }
    return std::move(std::get<ValueArm>(storage_).value);
  }
  [[nodiscard]] T value_or(T fallback) const {
    return ok() ? std::get<ValueArm>(storage_).value : std::move(fallback);
  }
  [[nodiscard]] const Error &error() const & {
    require_error();
    return std::get<ErrorArm>(storage_).error;
  }
  [[nodiscard]] Error &&error() && {
    require_error();
    return std::move(std::get<ErrorArm>(storage_).error);
  }
  [[nodiscard]] const std::string &error_message() const {
    return error().message;
  }

private:
  /**
   * Distinguishes a successful payload from the failure alternative.
   */
  struct ValueArm {
    T value;
  };
  /**
   * Distinguishes a structured failure from the success alternative.
   */
  struct ErrorArm {
    Error error;
  };

  explicit Result(ValueArm value) : storage_(std::move(value)) {}
  explicit Result(ErrorArm error) : storage_(std::move(error)) {}

  void require_value() const {
    if (!ok()) {
      throw std::logic_error("Attempted to access the value arm of a failed Result.");
    }
  }

  void require_error() const {
    if (ok()) {
      throw std::logic_error("Attempted to access the error arm of a successful Result.");
    }
  }

  std::variant<ValueArm, ErrorArm> storage_;
};

[[nodiscard]] inline Error make_error(ErrorCode code, std::string message) {
  return Error{.code = code, .message = std::move(message)};
}

} // namespace quader::modeling
