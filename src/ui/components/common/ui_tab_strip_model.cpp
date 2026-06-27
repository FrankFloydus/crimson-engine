////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/common/ui_tab_strip_model.hpp"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

bool itemIdIsPresent(const std::vector<std::string>& itemIds, std::string_view itemId) {
    return std::find(itemIds.begin(), itemIds.end(), itemId) != itemIds.end();
}

std::vector<std::string> normalizedItemIds(const std::vector<std::string_view>& itemIds) {
    std::vector<std::string> normalized;
    normalized.reserve(itemIds.size());
    for (std::string_view itemId : itemIds) {
        if (itemId.empty() || itemIdIsPresent(normalized, itemId)) {
            continue;
        }
        normalized.emplace_back(itemId);
    }
    return normalized;
}

std::string normalizedActiveItemId(
        const std::vector<std::string>& itemIds,
        std::string_view preferredActiveItemId) {
    if (itemIdIsPresent(itemIds, preferredActiveItemId)) {
        return std::string(preferredActiveItemId);
    }
    return itemIds.empty() ? std::string{} : itemIds.front();
}

} // namespace

UITabStripModel UITabStripModel::fromItems(
        const std::vector<std::string_view>& itemIds,
        std::string_view preferredActiveItemId) {
    UITabStripModel model;
    const bool changed = model.setItems(itemIds, preferredActiveItemId);
    (void)changed;
    return model;
}

std::vector<std::string_view> UITabStripModel::itemIds() const {
    std::vector<std::string_view> result;
    result.reserve(itemIds_.size());
    for (const std::string& itemId : itemIds_) {
        result.push_back(itemId);
    }
    return result;
}

std::optional<std::string_view> UITabStripModel::activeItemId() const {
    if (activeItemId_.empty()) {
        return std::nullopt;
    }
    return std::string_view(activeItemId_);
}

bool UITabStripModel::contains(std::string_view itemId) const {
    return itemIdIsPresent(itemIds_, itemId);
}

bool UITabStripModel::empty() const {
    return itemIds_.empty();
}

bool UITabStripModel::setItems(
        const std::vector<std::string_view>& itemIds,
        std::string_view preferredActiveItemId) {
    std::vector<std::string> normalizedItems = normalizedItemIds(itemIds);
    std::string normalizedActive = normalizedActiveItemId(normalizedItems, preferredActiveItemId);
    if (itemIds_ == normalizedItems && activeItemId_ == normalizedActive) {
        return false;
    }
    itemIds_ = std::move(normalizedItems);
    activeItemId_ = std::move(normalizedActive);
    return true;
}

bool UITabStripModel::activateItem(std::string_view itemId) {
    if (!contains(itemId) || activeItemId_ == itemId) {
        return false;
    }
    activeItemId_ = std::string(itemId);
    return true;
}

} // namespace quader
