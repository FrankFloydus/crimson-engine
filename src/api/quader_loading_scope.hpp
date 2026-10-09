////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <functional>
#include <utility>

namespace quader {

/// Balances native loading presentation around synchronous API work.
class QuaderLoadingScope final {
public:
  QuaderLoadingScope(std::function<void()> begin,
                     std::function<void(bool)> end)
      : end_(std::move(end)) {
    // Copy both hooks before invoking either: shell binding may replace them.
    if (begin) {
      begin();
    }
  }

  ~QuaderLoadingScope() {
    if (end_) {
      end_(succeeded_);
    }
  }

  QuaderLoadingScope(const QuaderLoadingScope &) = delete;
  QuaderLoadingScope &operator=(const QuaderLoadingScope &) = delete;

  void set_succeeded(bool succeeded) { succeeded_ = succeeded; }

private:
  std::function<void(bool)> end_;
  bool succeeded_ = false;
};

} // namespace quader
