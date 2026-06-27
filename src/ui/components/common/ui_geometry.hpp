////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

/// Stores a backend-neutral point in framebuffer pixels.
struct UIPixelPoint {
    float x = 0.0f;
    float y = 0.0f;
};

/// Stores a backend-neutral rectangle in framebuffer pixels.
struct UIPixelRect {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
};

} // namespace quader
