////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "modeling/services/ModelingActionCatalog.h"
#include "modeling/ModelingTypes.h"

namespace quader {

/**
 * Coordinates Modeling Interaction Settings Service responsibilities for the modeling application service layer.
 */
class ModelingInteractionSettingsService {
public:
    ModelingInteractionSettingsService();

    [[nodiscard]] ModelingInteractionSettings settings() const;
    [[nodiscard]] ModelingInteractionSettings normalize(const ModelingInteractionSettings& settings) const;
    [[nodiscard]] bool setNormalized(ModelingInteractionSettings settings);

    void resetForPreviewAction(ModelingActionPreviewKind previewKind);
    void setCutMode(ModelingCutMode mode);
    void cycleCutModeWithoutRuntime();

private:
    ModelingInteractionSettings settings_;
};

} // namespace quader
