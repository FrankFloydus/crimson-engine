////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QWidget>

#include <memory>

class QEvent;

namespace quader {

class QtUiScaleMetrics;
struct UIContext;

/**
 * Presents scene display layers and routes layer intent through the public
 * editor API. Layer membership, visibility, and locking remain scene-owned.
 */
class LayerPanel final : public QWidget {
public:
  explicit LayerPanel(QWidget *parent = nullptr);
  ~LayerPanel() override;

  void abandon_runtime() noexcept;
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void refresh(UIContext &context);

protected:
  bool event(QEvent *event) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
