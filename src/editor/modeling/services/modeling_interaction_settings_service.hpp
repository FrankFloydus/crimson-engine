////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"

#include <cstdint>

namespace quader {

/**
 * Coordinates Modeling Interaction Settings Service responsibilities for the modeling application service layer.
 */
class ModelingInteractionSettingsService {
public:
    ModelingInteractionSettingsService();

    [[nodiscard]] ModelingInteractionSettings settings() const;
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    [[nodiscard]] ModelingInteractionSettings normalize(const ModelingInteractionSettings& settings) const;
    [[nodiscard]] bool set_normalized(ModelingInteractionSettings settings);
    [[nodiscard]] bool
    reset_for_target(ModelingToolSettingsTargetKind target_kind);

    void set_cut_mode(ModelingCutMode mode);
    void cycle_cut_mode_without_runtime();

  private:
    ModelingInteractionSettings settings_;
    std::uint64_t revision_ = 0;
};

} // namespace quader
