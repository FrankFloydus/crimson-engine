////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_session.hpp"
#include "scene/document/project_session.hpp"

namespace quader {

/**
 * Bridges API-owned opaque session handles to the internal project session owner.
 */
class QuaderApiSessionInternalAccess final {
public:
    [[nodiscard]] static QuaderApiSession fromProjectSession(ProjectSession& session) noexcept {
        return QuaderApiSession(&session);
    }

    [[nodiscard]] static ProjectSession& projectSession(QuaderApiSession session) noexcept {
        return *static_cast<ProjectSession*>(session.session_);
    }
};

} // namespace quader
