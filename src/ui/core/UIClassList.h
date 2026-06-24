////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Maintains an ordered, duplicate-free list of UI classes for retained UI nodes.
 */
class UIClassList {
public:
    UIClassList& add(std::string_view className);
    UIClassList& addIf(bool condition, std::string_view className);

    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool contains(std::string_view className) const;
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] const std::vector<std::string>& values() const { return classes_; }

private:
    std::vector<std::string> classes_;
};

} // namespace quader
