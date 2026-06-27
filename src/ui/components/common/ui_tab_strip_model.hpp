////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Stores ordered tab item identity and keeps the active item valid.
 */
class UITabStripModel {
public:
    [[nodiscard]] static UITabStripModel fromItems(
            const std::vector<std::string_view>& itemIds,
            std::string_view preferredActiveItemId = {});

    [[nodiscard]] std::vector<std::string_view> itemIds() const;
    [[nodiscard]] std::optional<std::string_view> activeItemId() const;
    [[nodiscard]] bool contains(std::string_view itemId) const;
    [[nodiscard]] bool empty() const;

    [[nodiscard]] bool setItems(
            const std::vector<std::string_view>& itemIds,
            std::string_view preferredActiveItemId = {});
    [[nodiscard]] bool activateItem(std::string_view itemId);

private:
    std::vector<std::string> itemIds_;
    std::string activeItemId_;
};

} // namespace quader
