#pragma once

#include "modeling/ModelingActionCatalog.h"
#include "modeling/ModelingSessionService.h"

namespace quader {

class ModelingInteractionSettingsService {
public:
    ModelingInteractionSettingsService();

    [[nodiscard]] ModelingInteractionSettings settings() const;
    [[nodiscard]] ModelingInteractionSettings normalize(const ModelingInteractionSettings& settings) const;
    [[nodiscard]] bool setNormalized(ModelingInteractionSettings settings);

    void resetForPreviewAction(ModelingActionPreviewKind previewKind);
    void setCutMode(ModelingCutMode mode);
    void cycleCutModeWithoutSdk();

private:
    ModelingInteractionSettings settings_;
};

} // namespace quader
