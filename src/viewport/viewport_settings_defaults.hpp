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
inline constexpr float kDefaultViewportGridMinorLineSize = 0.325f;
inline constexpr float kDefaultViewportGridMajorLineSize = 0.250f;
inline constexpr float kDefaultViewportGridAxisLineSize = 1.0f;

} // namespace quader
