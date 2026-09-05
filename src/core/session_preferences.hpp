////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

/// Persistent user policy for retained history and recovery copies.
struct SessionPreferences final {
  static constexpr int kMinimumUndoSteps = 0;
  static constexpr int kMaximumUndoSteps = 256;
  static constexpr int kDefaultUndoSteps = 32;
  static constexpr int kMinimumAutoSaveIntervalMinutes = 1;
  static constexpr int kMaximumAutoSaveIntervalMinutes = 60;
  static constexpr int kDefaultAutoSaveIntervalMinutes = 2;
  static constexpr int kMinimumAutoSaveMaxFiles = 1;
  static constexpr int kMaximumAutoSaveMaxFiles = 20;
  static constexpr int kDefaultAutoSaveMaxFiles = 3;

  /// Zero disables retention; already committed edits remain in the document.
  int undo_steps = kDefaultUndoSteps;
  bool autosave_enabled = true;
  int autosave_interval_minutes = kDefaultAutoSaveIntervalMinutes;
  int autosave_max_files = kDefaultAutoSaveMaxFiles;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return undo_steps >= kMinimumUndoSteps &&
           undo_steps <= kMaximumUndoSteps &&
           autosave_interval_minutes >= kMinimumAutoSaveIntervalMinutes &&
           autosave_interval_minutes <= kMaximumAutoSaveIntervalMinutes &&
           autosave_max_files >= kMinimumAutoSaveMaxFiles &&
           autosave_max_files <= kMaximumAutoSaveMaxFiles;
  }

  constexpr bool operator==(const SessionPreferences &) const = default;
};

} // namespace quader
