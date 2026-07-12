////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QWidget>

#include <memory>

namespace quader {

struct UIContext;

/**
 * Presents the in-memory application log with filtering controls.
 */
class ConsolePanel final : public QWidget {
public:
  explicit ConsolePanel(QWidget *parent = nullptr);
  ~ConsolePanel() override;

  void refresh(UIContext &context);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
