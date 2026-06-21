#pragma once

namespace quader {

class LocalizationService;
class ModelingSessionService;
class UndoRedoManager;
class ViewportInputRouter;

class ViewportContextMenuPresenter {
public:
    void drawOperationsPopup(
            UndoRedoManager& undoRedo,
            ModelingSessionService& modeling,
            ViewportInputRouter& inputRouter,
            LocalizationService& localization,
            bool modalToolOwnsViewport) const;
};

} // namespace quader
