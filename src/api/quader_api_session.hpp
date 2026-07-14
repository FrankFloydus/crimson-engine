////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <memory>
#include <utility>

namespace quader {

class QuaderApiSessionInternalAccess;

/**
 * Stores an opaque editor-session handle used by the public Quader API surface.
 */
class QuaderApiSession final {
public:
    QuaderApiSession() = default;
    [[nodiscard]] bool valid() const noexcept {
      return binding_ != nullptr && binding_->owner != nullptr;
    }

private:
    friend class QuaderApiSessionInternalAccess;

    /// Identifies which native owner an opaque API session resolves through.
    enum class OwnerKind : std::uint8_t {
      Scene,
      Project,
    };

    /// Stores the opaque owner pointer and its resolution strategy.
    struct Binding final {
      void *owner = nullptr;
      OwnerKind kind = OwnerKind::Scene;
      std::uint32_t standalone_generation = 1u;
    };

    explicit QuaderApiSession(std::shared_ptr<Binding> binding) noexcept
        : binding_(std::move(binding)) {}

    std::shared_ptr<Binding> binding_;
};

} // namespace quader
