#pragma once

#include "modeling/ModelingSessionService.h"
#include "viewport/ViewportInputRouter.h"

namespace quader {

class QuaderEditorApi;

struct ViewportShortcutControllerResult {
    bool consumed = false;
    bool commandExecuted = false;
    bool invalidateHover = false;
    bool markOverlayDirty = false;
};

class ViewportShortcutController {
public:
    [[nodiscard]] ViewportShortcutControllerResult handleOperationPreviewShortcut(
            const ViewportInputResult& inputResult,
            const ModelingToolSettingsTarget& toolSettingsTarget,
            ViewportInputRouter& inputRouter,
            QuaderEditorApi& editor,
            ModelingSessionService& modeling) const;

    [[nodiscard]] ViewportShortcutControllerResult handleModelingShortcut(
            const ViewportInputResult& inputResult,
            QuaderEditorApi& editor,
            ModelingSessionService& modeling) const;
};

} // namespace quader
