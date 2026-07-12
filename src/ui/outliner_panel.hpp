////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QWidget>
#include <cstdint>
#include <memory>

namespace quader {

struct UIContext;

/**
 * Describes how a local scene property is presented in the Outliner.
 * Inherited means an ancestor changes the property's effective value.
 */
enum class OutlinerPropertyState {
  Off,
  On,
  Inherited,
};

[[nodiscard]] OutlinerPropertyState
outliner_visibility_property_state(bool visible,
                                   bool effectively_visible) noexcept;
[[nodiscard]] OutlinerPropertyState
outliner_lock_property_state(bool locked,
                             bool effectively_locked) noexcept;
[[nodiscard]] bool outliner_selection_sync_required(
    bool model_rebuilt, bool cached_revision_valid,
    std::uint64_t cached_selection_revision,
    std::uint64_t current_selection_revision) noexcept;

/**
 * Presents the scene hierarchy and routes scene-selection intent through the
 * public editor API.
 */
class OutlinerPanel final : public QWidget {
public:
  explicit OutlinerPanel(QWidget *parent = nullptr);
  ~OutlinerPanel() override;

  void refresh(UIContext &context);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
