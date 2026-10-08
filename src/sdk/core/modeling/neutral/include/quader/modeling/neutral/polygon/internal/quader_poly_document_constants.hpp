////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only neutral modeling core contract shared by SDK and native owners.

namespace quader_poly::document_internal {

inline constexpr float kEpsilon = 0.000001F;
/// Keeps a component coplanar with its visible surface selectable while
/// rejecting geometry that lies meaningfully behind that surface.
inline constexpr float kComponentPickOcclusionTolerance = 0.001F;
inline constexpr float kFaceAreaScoreEpsilon = kEpsilon * kEpsilon;
inline constexpr float kGeneratedUvMapUnitsPerWorldUnit = 32.0F;
inline constexpr float kGeneratedUvTileSizeMapUnits = 64.0F;
inline constexpr float kGeneratedUvTilesPerWorldUnit =
    kGeneratedUvMapUnitsPerWorldUnit / kGeneratedUvTileSizeMapUnits;
inline constexpr float kPi = 3.14159265358979323846F;
inline constexpr float kTau = kPi * 2.0F;
inline constexpr float kMinPrimitiveDimension = 0.001F;
inline constexpr float kMaxPrimitiveDimension = 10000.0F;
inline constexpr float kHullDistanceEpsilon = 0.001F;
inline constexpr float kHullNormalEpsilon = 0.000001F;
inline constexpr float kEdgeBevelMinProfile = 0.0F;
inline constexpr float kEdgeBevelMaxProfile = 1.0F;

} // namespace quader_poly::document_internal
