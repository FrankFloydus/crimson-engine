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
    bool deactivated_after_edit = false;
    std::string merge_key;

    [[nodiscard]] bool should_apply() const {
      return changed || deactivated_after_edit;
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

    void reset_if_entity_scope_changed(EntityId primary,
                                       std::span<const EntityId> selection) {
      if (!active_) {
        return;
      }
      if (active_->primary != primary ||
          !same_selection(active_->selection, selection)) {
        active_.reset();
      }
    }

    [[nodiscard]] std::string
    ensure_merge_key(std::string stable_id, EntityId primary,
                     std::span<const EntityId> selection) {
      if (!active_ || active_->stable_id != stable_id ||
          active_->primary != primary ||
          !same_selection(active_->selection, selection)) {
        std::string stable_key = std::move(stable_id);
        std::string merge_key =
            make_merge_key(stable_key, primary, selection, next_gesture_++);
        active_ = State{
            .stable_id = std::move(stable_key),
            .primary = primary,
            .selection =
                std::vector<EntityId>(selection.begin(), selection.end()),
            .values = {},
            .merge_key = std::move(merge_key),
        };
      }
      if (active_->merge_key.empty()) {
        active_->merge_key = make_merge_key(active_->stable_id, primary,
                                            selection, next_gesture_++);
      }
      return active_->merge_key;
    }

    [[nodiscard]] bool matches(std::string_view stable_id, EntityId primary,
                               std::span<const EntityId> selection,
                               std::size_t value_count) const {
      return active_ && active_->stable_id == stable_id &&
             active_->primary == primary &&
             active_->values.size() == value_count &&
             same_selection(active_->selection, selection);
    }

    void apply_active_values(std::string_view stable_id, EntityId primary,
                             std::span<const EntityId> selection,
                             std::span<float> values) const {
      if (!matches(stable_id, primary, selection, values.size())) {
        return;
      }
      std::copy(active_->values.begin(), active_->values.end(), values.begin());
    }

    void stage(std::string stable_id, EntityId primary,
               std::span<const EntityId> selection,
               std::span<const float> values) {
      const std::string merge_key =
          ensure_merge_key(std::move(stable_id), primary, selection);
      active_->values = std::vector<float>(values.begin(), values.end());
      active_->merge_key = merge_key;
    }

    [[nodiscard]] std::vector<float>
    final_values(std::string_view stable_id, EntityId primary,
                 std::span<const EntityId> selection,
                 std::span<const float> fallback_values) const {
      if (!matches(stable_id, primary, selection, fallback_values.size())) {
        return {fallback_values.begin(), fallback_values.end()};
      }
      return active_->values;
    }

private:
    /**
     * Stores one active continuous edit gesture and its final values.
     */
    struct State {
      std::string stable_id;
      EntityId primary = kInvalidEntityId;
      std::vector<EntityId> selection;
      std::vector<float> values;
      std::string merge_key;
    };

    static std::string make_merge_key(std::string_view stable_id,
                                      EntityId primary,
                                      std::span<const EntityId> selection,
                                      std::uint64_t gesture) {
      std::string key = "ui.continuous:";
      key += stable_id;
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

    static bool same_selection(const std::vector<EntityId> &stored,
                               std::span<const EntityId> current) {
      return stored.size() == current.size() &&
             std::equal(stored.begin(), stored.end(), current.begin(),
                        current.end());
    }

    std::optional<State> active_;
    std::uint64_t next_gesture_ = 1;
};

} // namespace quader
