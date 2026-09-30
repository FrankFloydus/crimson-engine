////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QWidget>

#include <spdlog/common.h>

#include <functional>
#include <memory>
#include <string_view>

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
  void set_command_handler(std::function<void(std::string_view)> handler);
  void append_command_reply(spdlog::level::level_enum level,
                            std::string_view text);

protected:
  void changeEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
