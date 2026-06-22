#pragma once

namespace quader {

class LocalizationService;
class ModelingSessionService;
class QuaderEditorApi;
class ViewportInputRouter;

class ViewportContextMenuPresenter {
public:
    void drawOperationsPopup(
            QuaderEditorApi& editor,
            ModelingSessionService& modeling,
            ViewportInputRouter& inputRouter,
            LocalizationService& localization,
            bool modalToolOwnsViewport) const;
};

} // namespace quader
