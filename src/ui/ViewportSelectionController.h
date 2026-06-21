#pragma once

#include "assets/AssetRegistry.h"
#include "document/DocumentCommandService.h"
#include "modeling/ModelingSessionService.h"
#include "scene/SceneDocument.h"
#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportSettings.h"

namespace quader {

class ViewportSelectionController {
public:
    ModelingSelectionSummary pickSelection(
            SceneDocument& document,
            DocumentCommandService& documentCommands,
            const AssetRegistry& assets,
            ModelingSessionService& modeling,
            ViewportInputRouter& inputRouter,
            const ViewportSettings& settings,
            const ViewportCameraController& camera,
            Vec2 localMouse,
            ViewportModifiers modifiers,
            int clickCount,
            bool preserveSelectionOnMiss = false,
            bool preserveSelectionOnSelectedHit = false) const;
};

} // namespace quader
