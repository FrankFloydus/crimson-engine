#pragma once

#include "modeling/ModelingSessionService.h"

#include <string>
#include <string_view>

namespace quader {

class ModelingToolSettingsService {
public:
    [[nodiscard]] const std::string& activePreviewActionId() const;
    [[nodiscard]] const std::string& activePreviewOperationId() const;
    [[nodiscard]] const std::string& activePreviewLabel() const;

    void setPreviewMetadata(std::string actionId, std::string operationId, std::string label);
    void clearPreviewMetadata();

    [[nodiscard]] ModelingToolSettingsTarget operationPreviewTarget(
            ModelingToolSettingsTargetKind kind,
            std::string_view fallbackLabel,
            std::string_view fallbackOperationId) const;

private:
    std::string activePreviewActionId_;
    std::string activePreviewOperationId_;
    std::string activePreviewLabel_;
};

} // namespace quader
