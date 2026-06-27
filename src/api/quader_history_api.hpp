////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_api_snapshot.hpp"

namespace quader {

class QuaderApiMutationRunner;

/**
 * Exposes Quader History API operations through the public Quader API surface.
 */
class QuaderHistoryApi final {
public:
    QuaderHistoryApi(QuaderApiSession session, QuaderApiMutationRunner& mutations);

    QuaderApiVoidResult undo();
    QuaderApiVoidResult redo();
    QuaderApiVoidResult repeatLastAction();

    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] QuaderHistorySnapshot snapshot() const;

private:
    QuaderApiSession session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
