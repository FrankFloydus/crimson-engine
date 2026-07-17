////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace quader {

using ViewportLocalVisibilityEntityId = std::uint64_t;
inline constexpr ViewportLocalVisibilityEntityId
    kInvalidViewportLocalVisibilityEntityId = 0u;

class ViewportWorkspaceService;

/**
 * An owned, immutable view of the workspace-local visibility filter.
 *
 * Entity identifiers intentionally remain viewport-generic. The editor layer
 * maps its scene entities into this contract without making viewport or render
 * code depend on scene ownership.
 */
class ViewportLocalVisibilitySnapshot final {
public:
  ViewportLocalVisibilitySnapshot() = default;

  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] std::uint64_t revision() const { return revision_; }
  [[nodiscard]] std::span<const ViewportLocalVisibilityEntityId>
  baseline_entities() const {
    if (!baseline_entities_) {
      return {};
    }
    return *baseline_entities_;
  }
  [[nodiscard]] std::span<const ViewportLocalVisibilityEntityId>
  members() const {
    if (!members_) {
      return {};
    }
    return *members_;
  }
  [[nodiscard]] bool
  contains_member(ViewportLocalVisibilityEntityId entity) const {
    const std::span<const ViewportLocalVisibilityEntityId> current_members =
        members();
    return std::binary_search(current_members.begin(), current_members.end(),
                              entity);
  }
  [[nodiscard]] bool allows(ViewportLocalVisibilityEntityId entity) const {
    return !active_ || contains_member(entity);
  }

private:
  friend class ViewportWorkspaceService;

  using EntityIdStorage = std::vector<ViewportLocalVisibilityEntityId>;

  ViewportLocalVisibilitySnapshot(
      bool active, std::uint64_t revision,
      std::shared_ptr<const EntityIdStorage> baseline_entities,
      std::shared_ptr<const EntityIdStorage> members)
      : active_(active), revision_(revision),
        baseline_entities_(std::move(baseline_entities)),
        members_(std::move(members)) {}

  bool active_ = false;
  std::uint64_t revision_ = 0u;
  std::shared_ptr<const EntityIdStorage> baseline_entities_;
  std::shared_ptr<const EntityIdStorage> members_;
};

} // namespace quader
