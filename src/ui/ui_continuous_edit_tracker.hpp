////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Stores the lifecycle emitted by an ImGui continuous edit widget.
 */
struct UIContinuousEditResult {
    bool changed = false;
    bool activated = false;
    bool deactivated = false;
    bool deactivatedAfterEdit = false;
    std::string mergeKey;

    [[nodiscard]] bool shouldApply() const {
        return changed || deactivatedAfterEdit;
    }
};

/**
 * Tracks staged values for one active entity-backed UI edit gesture.
 */
class UIContinuousEditTracker final {
public:
    void reset() {
        active_.reset();
    }

    void resetIfEntityScopeChanged(EntityId primary, std::span<const EntityId> selection) {
        if (!active_) {
            return;
        }
        if (active_->primary != primary || !sameSelection(active_->selection, selection)) {
            active_.reset();
        }
    }

    [[nodiscard]] std::string ensureMergeKey(
            std::string stableId,
            EntityId primary,
            std::span<const EntityId> selection) {
        if (!active_ || active_->stableId != stableId || active_->primary != primary ||
                !sameSelection(active_->selection, selection)) {
            std::string stableKey = std::move(stableId);
            std::string mergeKey = makeMergeKey(stableKey, primary, selection, nextGesture_++);
            active_ = State{
                    .stableId = std::move(stableKey),
                    .primary = primary,
                    .selection = std::vector<EntityId>(selection.begin(), selection.end()),
                    .values = {},
                    .mergeKey = std::move(mergeKey),
            };
        }
        if (active_->mergeKey.empty()) {
            active_->mergeKey = makeMergeKey(active_->stableId, primary, selection, nextGesture_++);
        }
        return active_->mergeKey;
    }

    [[nodiscard]] bool matches(
            std::string_view stableId,
            EntityId primary,
            std::span<const EntityId> selection,
            std::size_t valueCount) const {
        return active_ &&
                active_->stableId == stableId &&
                active_->primary == primary &&
                active_->values.size() == valueCount &&
                sameSelection(active_->selection, selection);
    }

    void applyActiveValues(
            std::string_view stableId,
            EntityId primary,
            std::span<const EntityId> selection,
            std::span<float> values) const {
        if (!matches(stableId, primary, selection, values.size())) {
            return;
        }
        std::copy(active_->values.begin(), active_->values.end(), values.begin());
    }

    void stage(
            std::string stableId,
            EntityId primary,
            std::span<const EntityId> selection,
            std::span<const float> values) {
        const std::string mergeKey = ensureMergeKey(std::move(stableId), primary, selection);
        active_->values = std::vector<float>(values.begin(), values.end());
        active_->mergeKey = mergeKey;
    }

    [[nodiscard]] std::vector<float> finalValues(
            std::string_view stableId,
            EntityId primary,
            std::span<const EntityId> selection,
            std::span<const float> fallbackValues) const {
        if (!matches(stableId, primary, selection, fallbackValues.size())) {
            return {fallbackValues.begin(), fallbackValues.end()};
        }
        return active_->values;
    }

private:
    /**
     * Stores one active continuous edit gesture and its final values.
     */
    struct State {
        std::string stableId;
        EntityId primary = InvalidEntityId;
        std::vector<EntityId> selection;
        std::vector<float> values;
        std::string mergeKey;
    };

    static std::string makeMergeKey(
            std::string_view stableId,
            EntityId primary,
            std::span<const EntityId> selection,
            std::uint64_t gesture) {
        std::string key = "ui.continuous:";
        key += stableId;
        key += ":";
        key += std::to_string(primary);
        key += ":";
        for (const EntityId entity : selection) {
            key += std::to_string(entity);
            key += ",";
        }
        key += ":";
        key += std::to_string(gesture);
        return key;
    }

    static bool sameSelection(const std::vector<EntityId>& stored, std::span<const EntityId> current) {
        return stored.size() == current.size() &&
                std::equal(stored.begin(), stored.end(), current.begin(), current.end());
    }

    std::optional<State> active_;
    std::uint64_t nextGesture_ = 1;
};

} // namespace quader
