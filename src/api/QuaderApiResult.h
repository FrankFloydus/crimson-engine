#pragma once

#include "api/QuaderApiTypes.h"

#include <string>
#include <utility>

namespace quader {

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
struct QuaderApiResult : QuaderApiStatus {
    QuaderApiResultValue<T> value {};
};

using QuaderApiVoidResult = QuaderApiStatus;

} // namespace quader
