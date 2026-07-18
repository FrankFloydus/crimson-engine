////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <memory>

namespace quader {

class ModelingSessionService;

/**
 * Stores an opaque modeling selection baseline for one deferred viewport
 * box-selection commit.
 */
class ModelingSelectionBoxBaseline final {
public:
    ModelingSelectionBoxBaseline();
    ~ModelingSelectionBoxBaseline();
    ModelingSelectionBoxBaseline(const ModelingSelectionBoxBaseline&) noexcept = default;
    ModelingSelectionBoxBaseline& operator=(const ModelingSelectionBoxBaseline&) noexcept = default;
    ModelingSelectionBoxBaseline(ModelingSelectionBoxBaseline&&) noexcept = default;
    ModelingSelectionBoxBaseline& operator=(ModelingSelectionBoxBaseline&&) noexcept = default;

    [[nodiscard]] bool valid() const;

private:
    friend class ModelingSessionService;

    struct Impl;
    explicit ModelingSelectionBoxBaseline(std::shared_ptr<const Impl> impl);

    std::shared_ptr<const Impl> impl_;
};

} // namespace quader
