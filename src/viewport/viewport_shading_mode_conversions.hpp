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

inline ViewportShadingMode
quader_api_to_viewport_shading_mode(QuaderShadingMode mode) {
  switch (mode) {
  case QuaderShadingMode::Wireframe:
    return ViewportShadingMode::Wireframe;
  case QuaderShadingMode::Material:
    return ViewportShadingMode::Rendered;
  case QuaderShadingMode::Solid:
  case QuaderShadingMode::XRay:
    return ViewportShadingMode::Shaded;
  case QuaderShadingMode::DiffuseOnly:
    return ViewportShadingMode::DiffuseOnly;
  case QuaderShadingMode::SpecularOnly:
    return ViewportShadingMode::SpecularOnly;
  case QuaderShadingMode::Albedo:
    return ViewportShadingMode::Albedo;
  case QuaderShadingMode::Roughness:
    return ViewportShadingMode::Roughness;
  case QuaderShadingMode::Metalness:
    return ViewportShadingMode::Metalness;
  case QuaderShadingMode::Normal:
    return ViewportShadingMode::Normal;
  }
  return ViewportShadingMode::Shaded;
}

inline QuaderShadingMode
quader_api_from_viewport_shading_mode(ViewportShadingMode mode) {
  switch (mode) {
  case ViewportShadingMode::Wireframe:
    return QuaderShadingMode::Wireframe;
  case ViewportShadingMode::Rendered:
    return QuaderShadingMode::Material;
  case ViewportShadingMode::Shaded:
    return QuaderShadingMode::Solid;
  case ViewportShadingMode::DiffuseOnly:
    return QuaderShadingMode::DiffuseOnly;
  case ViewportShadingMode::SpecularOnly:
    return QuaderShadingMode::SpecularOnly;
  case ViewportShadingMode::Albedo:
    return QuaderShadingMode::Albedo;
  case ViewportShadingMode::Roughness:
    return QuaderShadingMode::Roughness;
  case ViewportShadingMode::Metalness:
    return QuaderShadingMode::Metalness;
  case ViewportShadingMode::Normal:
    return QuaderShadingMode::Normal;
  }
  return QuaderShadingMode::Solid;
}

} // namespace quader
