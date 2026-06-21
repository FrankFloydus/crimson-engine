#pragma once

namespace quader {

class ModelingSessionService;
class UndoRedoManager;
class ViewportInputRouter;

class ViewportContextMenuPresenter {
public:
    void drawOperationsPopup(
            UndoRedoManager& undoRedo,
            ModelingSessionService& modeling,
            ViewportInputRouter& inputRouter,
            bool modalToolOwnsViewport) const;
};

} // namespace quader
