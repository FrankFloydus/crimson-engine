////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_settings.hpp"

namespace quader {

inline constexpr ViewportSettingColor kDefaultViewportGridMinorColor = {
    150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f};
inline constexpr ViewportSettingColor kDefaultViewportGridMajorColor = {
    210.0f / 255.0f, 210.0f / 255.0f, 210.0f / 255.0f, 1.0f};
inline constexpr ViewportSettingColor kDefaultViewportMeshGridMinorColor = {
    0.02f, 0.02f, 0.02f, 1.0f};
inline constexpr ViewportSettingColor kDefaultViewportMeshGridMajorColor = {
    1.0f, 0.84f, 0.22f, 1.0f};
inline constexpr ViewportSettingColor kDefaultViewportGridXAxisColor = {
    1.0f, 0.239f, 0.0f, 0.72f};
inline constexpr ViewportSettingColor kDefaultViewportGridYAxisColor = {
    0.29f, 0.58f, 0.0f, 0.72f};
inline constexpr ViewportSettingColor kDefaultViewportGridZAxisColor = {
    0.059f, 0.612f, 1.0f, 0.72f};
inline constexpr ViewportSettingColor
    kDefaultViewportInstanceEditBackgroundColor = {
        0.34f, 0.36f, 0.37f, 1.0f};
inline constexpr ViewportSettingColor kDefaultViewportInstanceEditContextColor = {
    0.38f, 0.40f, 0.42f, 0.45f};
inline constexpr ViewportSettingColor kDefaultViewportCollisionFillColor = {
    0.0f, 0.6f, 0.7f, 0.42f};
inline constexpr float kDefaultViewportGridMinorLineSize = 0.325f;
inline constexpr float kDefaultViewportGridMajorLineSize = 0.250f;
inline constexpr float kDefaultViewportGridAxisLineSize = 1.0f;
inline constexpr float kDefaultViewportShadedKeyDirectionX = -0.09f;
inline constexpr float kDefaultViewportShadedKeyDirectionY = 0.47f;
inline constexpr float kDefaultViewportShadedKeyDirectionZ = 0.88f;
inline constexpr float kDefaultViewportShadedKeyIntensity = 0.60f;
inline constexpr float kDefaultViewportShadedDiffuseWrap = 0.0f;
inline constexpr float kDefaultViewportShadedGroundFill = 0.08f;
inline constexpr float kDefaultViewportShadedSkyFill = 0.21f;

} // namespace quader
