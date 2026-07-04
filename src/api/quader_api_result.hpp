////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"

#include <string>
#include <utility>

namespace quader {

/**
 * Callable public API boolean flag.
 *
 * The public result types keep ok and changed as fields for source
 * compatibility, while QuaderApiFlag also acts like a bool and a nullary helper
 * so callers can write either result.ok or result.ok(). Prefer the helper-style
 * accessors on QuaderApiStatus in new examples.
 */
struct QuaderApiFlag {
    bool state = false;

    constexpr QuaderApiFlag() = default;
    constexpr QuaderApiFlag(bool value)
        : state(value) {}

    constexpr bool operator()() const { return state; }
    constexpr operator bool() const { return state; }
    constexpr bool operator==(bool value) const { return state == value; }
    constexpr bool operator!=(bool value) const { return state != value; }

    QuaderApiFlag& operator=(bool value) {
        state = value;
        return *this;
    }
};

/**
 * Shared status header for every public API operation result.
 *
 * ok reports whether the request succeeded, changed reports whether it mutated
 * project/editor state, message preserves the user-facing error text for UI and
 * scripting compatibility, and mutation_kind/revisions describe the committed
 * mutation when one happened. New public examples should prefer succeeded(),
 * did_change(), error_message(), and bool(result) over direct field inspection.
 */
struct QuaderApiStatus {
    QuaderApiFlag ok = false;
    QuaderApiFlag changed = false;
    std::string message;
    QuaderMutationKind mutation_kind = QuaderMutationKind::None;
    QuaderRevisionStamp revisions;

    [[nodiscard]] bool succeeded() const { return ok; }
    [[nodiscard]] bool did_change() const { return changed; }
    [[nodiscard]] const std::string &error_message() const { return message; }
    [[nodiscard]] explicit operator bool() const { return ok; }
};

template <typename T>
/**
 * Value carrier that preserves QuaderApiResult<T>::value compatibility.
 *
 * The wrapper derives from T so older field-style code keeps compiling, and it
 * also provides value() call syntax for newer examples:
 * auto object = editor.modeling().create_box(args).value();
 */
struct QuaderApiResultValue : T {
    constexpr QuaderApiResultValue() = default;
    QuaderApiResultValue(const T& value)
        : T(value) {}
    QuaderApiResultValue(T&& value) noexcept
        : T(std::move(value)) {}

    QuaderApiResultValue& operator=(const T& value) {
        static_cast<T&>(*this) = value;
        return *this;
    }

    QuaderApiResultValue& operator=(T&& value) noexcept {
        static_cast<T&>(*this) = std::move(value);
        return *this;
    }

    [[nodiscard]] T& operator()() & { return static_cast<T&>(*this); }
    [[nodiscard]] const T& operator()() const& { return static_cast<const T&>(*this); }
    [[nodiscard]] T&& operator()() && { return std::move(static_cast<T&>(*this)); }
};

template <typename T>
/**
 * Public API result for operations that return a value.
 *
 * QuaderApiResult<T> combines QuaderApiStatus with a compatible value field.
 * Check result.ok(), bool(result), or result.succeeded() before using
 * result.value(); failed results preserve message/error_message() and may leave
 * value default-initialized.
 */
struct QuaderApiResult : QuaderApiStatus {
    QuaderApiResultValue<T> value {};
};

using QuaderApiVoidResult = QuaderApiStatus;

inline void copy_quader_api_status(QuaderApiStatus &target,
                                   const QuaderApiStatus &source) {
  target.ok = source.ok;
  target.changed = source.changed;
  target.message = source.message;
  target.mutation_kind = source.mutation_kind;
  target.revisions = source.revisions;
}

inline QuaderApiStatus
quader_api_status_from_result(const QuaderApiStatus &status) {
  return status;
}

template <typename T>
QuaderApiStatus
quader_api_status_from_result(const QuaderApiResult<T> &result) {
  QuaderApiStatus status;
  copy_quader_api_status(status, result);
  return status;
}

template <typename T>
QuaderApiResult<T>
quader_api_result_from_status(const QuaderApiStatus &status) {
  QuaderApiResult<T> result;
  copy_quader_api_status(result, status);
  return result;
}

template <typename T>
QuaderApiResult<T> quader_api_result_from_status(const QuaderApiStatus &status,
                                                 T value) {
  QuaderApiResult<T> result = quader_api_result_from_status<T>(status);
  result.value = std::move(value);
  return result;
}

} // namespace quader
