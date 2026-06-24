////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/core/UIClassList.h"

#include <algorithm>

namespace quader {

UIClassList& UIClassList::add(std::string_view className) {
    if (className.empty() || contains(className)) {
        return *this;
    }
    classes_.emplace_back(className);
    return *this;
}

UIClassList& UIClassList::addIf(bool condition, std::string_view className) {
    if (condition) {
        add(className);
    }
    return *this;
}

bool UIClassList::empty() const {
    return classes_.empty();
}

bool UIClassList::contains(std::string_view className) const {
    return std::find(classes_.begin(), classes_.end(), className) != classes_.end();
}

std::string UIClassList::toString() const {
    std::string joined;
    for (const std::string& className : classes_) {
        if (!joined.empty()) {
            joined.push_back(' ');
        }
        joined += className;
    }
    return joined;
}

} // namespace quader
