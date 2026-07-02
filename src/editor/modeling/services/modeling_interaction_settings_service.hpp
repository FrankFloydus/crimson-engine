////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/services/modeling_action_catalog.hpp"
#include "editor/modeling/modeling_types.hpp"

namespace quader {

/**
 * Coordinates Modeling Interaction Settings Service responsibilities for the modeling application service layer.
 */
class ModelingInteractionSettingsService {
public:
    ModelingInteractionSettingsService();

    [[nodiscard]] ModelingInteractionSettings settings() const;
    [[nodiscard]] ModelingInteractionSettings normalize(const ModelingInteractionSettings& settings) const;
    [[nodiscard]] bool set_normalized(ModelingInteractionSettings settings);

    void reset_for_preview_action(ModelingActionPreviewKind preview_kind);
    void set_cut_mode(ModelingCutMode mode);
    void cycle_cut_mode_without_runtime();

  private:
    ModelingInteractionSettings settings_;
};

} // namespace quader
