////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <imgui.h>

namespace quader {

inline constexpr ImWchar kPhosphorIconRangeStart = 0xe000;
inline constexpr ImWchar kPhosphorIconRangeEnd = 0xf8ff;

inline constexpr const char* kPhosphorCaretDown = "\xee\x84\xb6";  // U+E136
inline constexpr const char* kPhosphorCaretRight = "\xee\x84\xba"; // U+E13A

} // namespace quader
