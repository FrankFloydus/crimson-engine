////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderApiTypes.h"

#include <string>
#include <utility>

namespace quader {

/**
 * Represents a Quader API Flag value used by the public Quader API surface.
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
 * Represents a Quader API Status value used by the public Quader API surface.
 */
struct QuaderApiStatus {
    QuaderApiFlag ok = false;
    QuaderApiFlag changed = false;
    std::string message;
    QuaderMutationKind mutationKind = QuaderMutationKind::None;
    QuaderRevisionStamp revisions;

    [[nodiscard]] bool succeeded() const { return ok; }
    [[nodiscard]] bool didChange() const { return changed; }
    [[nodiscard]] const std::string& errorMessage() const { return message; }
    [[nodiscard]] explicit operator bool() const { return ok; }
};

template <typename T>
/**
 * Stores the Quader API Result Value data contract used by the public Quader API surface.
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
 * Stores the Quader API Result data contract used by the public Quader API surface.
 */
struct QuaderApiResult : QuaderApiStatus {
    QuaderApiResultValue<T> value {};
};

using QuaderApiVoidResult = QuaderApiStatus;

} // namespace quader
