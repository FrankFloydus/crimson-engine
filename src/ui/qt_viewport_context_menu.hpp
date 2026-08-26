////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"

#include <functional>
#include <optional>

class QPoint;
class QWidget;

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class ViewportInputRouter;

/**
 * Presents the modeling operations available at a viewport position through a
 * stock Qt context menu. Modeling availability and execution remain owned by
 * the public editor API.
 */
class QtViewportContextMenuPresenter final {
public:
  void show(QWidget &parent, const QPoint &global_point,
            QuaderEditorApi &editor, ViewportInputRouter &input_router,
            LocalizationService &localization,
            const QuaderActionSet &action_set,
            QuaderSelectionMode selection_mode,
            const std::optional<ProjectAssetId> &material_under_mouse,
            const std::function<void(ProjectAssetId)> &open_material_editor)
      const;
};

} // namespace quader
