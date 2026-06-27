////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/core/ui_element.hpp"

#include <string>
#include <string_view>

namespace quader {

[[nodiscard]] std::string escapeRmlText(std::string_view value);
[[nodiscard]] std::string writeRmlMarkup(const UIElement& element);

} // namespace quader
