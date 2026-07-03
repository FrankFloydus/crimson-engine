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
    199.0f / 255.0f, 199.0f / 255.0f, 199.0f / 255.0f, 54.0f / 255.0f};
inline constexpr ViewportSettingColor kDefaultViewportGridMajorColor = {
    172.0f / 255.0f, 172.0f / 255.0f, 172.0f / 255.0f, 1.0f};
inline constexpr float kDefaultViewportGridMinorLineSize = 0.325f;
inline constexpr float kDefaultViewportGridMajorLineSize = 0.250f;
inline constexpr float kDefaultViewportGridAxisLineSize = 1.0f;

} // namespace quader
