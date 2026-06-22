////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

class LocalizationService;
class ModelingSessionService;
class QuaderEditorApi;
class ViewportInputRouter;

/**
 * Owns Viewport Context Menu Presenter behavior within the ImGui editor presentation layer.
 */
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
