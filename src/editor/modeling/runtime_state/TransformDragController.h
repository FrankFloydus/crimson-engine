////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

/**
 * Coordinates Transform Drag Controller state transitions for the editor command and action routing layer.
 */
class TransformDragController {
public:
    void setPreviewDriven(bool previewDriven, bool duplicateSelected);
    void clear();

    [[nodiscard]] bool previewDriven() const;
    [[nodiscard]] bool duplicateSelected() const;

private:
    bool previewDriven_ = false;
    bool duplicateSelected_ = false;
};

} // namespace quader
