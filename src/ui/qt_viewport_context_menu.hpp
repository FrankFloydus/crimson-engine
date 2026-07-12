////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

class QPoint;
class QWidget;

namespace quader {

class LocalizationService;
class ModelingSessionService;
class QuaderEditorApi;
class ViewportInputRouter;

/**
 * Presents the modeling operations available at a viewport position through a
 * stock Qt context menu. Modeling availability and execution remain owned by
 * the modeling session and public editor API respectively.
 */
class QtViewportContextMenuPresenter final {
public:
  void show(QWidget &parent, const QPoint &global_point,
            QuaderEditorApi &editor, ModelingSessionService &modeling,
            ViewportInputRouter &input_router,
            LocalizationService &localization,
            bool modal_tool_owns_viewport) const;
};

} // namespace quader
