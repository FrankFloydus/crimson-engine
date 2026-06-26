////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

class QuaderApiSessionInternalAccess;

/**
 * Stores an opaque editor-session handle used by the public Quader API surface.
 */
class QuaderApiSession final {
public:
    [[nodiscard]] bool valid() const noexcept { return session_ != nullptr; }

private:
    friend class QuaderApiSessionInternalAccess;

    explicit QuaderApiSession(void* session) noexcept
        : session_(session) {}

    void* session_ = nullptr;
};

} // namespace quader
