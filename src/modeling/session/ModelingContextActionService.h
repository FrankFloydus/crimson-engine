#pragma once

#include "modeling/ModelingActionCatalog.h"
#include "modeling/ModelingSessionService.h"

#include <vector>

namespace quader {

class ModelingContextActionService {
public:
    [[nodiscard]] ModelingActionQuery effectiveActionQuery(ModelingActionQuery query, bool modalToolOwnsViewport) const;
    [[nodiscard]] ModelingActionSet actionSet(
            const ModelingSelectionSummary& summary,
            ModelingActionQuery query,
            bool modalToolOwnsViewport) const;
    [[nodiscard]] std::vector<ModelingActionItem> actions(
            const ModelingSelectionSummary& summary,
            ModelingActionQuery query,
            bool modalToolOwnsViewport) const;
};

} // namespace quader
