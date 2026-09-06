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

  void abandon_runtime() noexcept;
  void refresh(UIContext &context);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
