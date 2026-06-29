////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_editor_api_types.hpp"
#include "viewport/viewport_workspace_service.hpp"

namespace quader {

inline ViewportShadingMode quaderApiToViewportShadingMode(QuaderShadingMode mode) {
    switch (mode) {
    case QuaderShadingMode::Wireframe:
        return ViewportShadingMode::Wireframe;
    case QuaderShadingMode::Material:
        return ViewportShadingMode::Rendered;
    case QuaderShadingMode::Solid:
    case QuaderShadingMode::XRay:
        return ViewportShadingMode::Shaded;
    }
    return ViewportShadingMode::Shaded;
}

inline QuaderShadingMode quaderApiFromViewportShadingMode(ViewportShadingMode mode) {
    switch (mode) {
    case ViewportShadingMode::Wireframe:
        return QuaderShadingMode::Wireframe;
    case ViewportShadingMode::Rendered:
        return QuaderShadingMode::Material;
    case ViewportShadingMode::Shaded:
        return QuaderShadingMode::Solid;
    }
    return QuaderShadingMode::Solid;
}

} // namespace quader
