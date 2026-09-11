////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace quader_io {

/**
 * Stores the native QDR atomic write result.
 */
struct QdrAtomicWriteResult {
    bool ok = false;
    std::string message;
};

/// Controls whether an atomic publication may replace an existing target.
enum class QdrAtomicWriteDisposition : std::uint8_t {
  ReplaceExisting,
  CreateNew,
};

/// Durably records an allocated rollback path before an existing file is replaced.
using QdrAtomicBeforeReplace =
    std::function<QdrAtomicWriteResult(const std::filesystem::path &backup_path)>;

/**
 * Stages one native QDR file beside its final destination, reversibly publishes
 * it before wider durable state may reference it, and preserves any prior
 * destination entry until the wider transaction commits.
 */
class QdrAtomicFileTransaction final {
public:
  QdrAtomicFileTransaction() = default;
  ~QdrAtomicFileTransaction();

  QdrAtomicFileTransaction(const QdrAtomicFileTransaction &) = delete;
  QdrAtomicFileTransaction &
  operator=(const QdrAtomicFileTransaction &) = delete;
  QdrAtomicFileTransaction(QdrAtomicFileTransaction &&) = delete;
  QdrAtomicFileTransaction &
  operator=(QdrAtomicFileTransaction &&) = delete;

  [[nodiscard]] QdrAtomicWriteResult
  prepare(const std::filesystem::path &target,
          QdrAtomicWriteDisposition disposition =
              QdrAtomicWriteDisposition::ReplaceExisting);
  /// The optional callback runs only for an existing target, before replacement.
  [[nodiscard]] QdrAtomicWriteResult
  publish(const QdrAtomicBeforeReplace &before_replace = {});
  [[nodiscard]] QdrAtomicWriteResult verify_published() const;
  [[nodiscard]] QdrAtomicWriteResult rollback();
  [[nodiscard]] QdrAtomicWriteResult commit();
  /**
   * Disarms automatic rollback while deliberately retaining the published
   * target and recovery artifacts for a higher-level durable recovery path.
   * Returns false without changing state unless publication is active.
   */
  [[nodiscard]] bool preserve_published_for_recovery() noexcept;

  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] bool published() const noexcept { return published_; }
  [[nodiscard]] const std::filesystem::path &target_path() const noexcept {
    return target_;
  }
  [[nodiscard]] const std::filesystem::path &staging_path() const noexcept {
    return staging_;
  }
  [[nodiscard]] const std::filesystem::path &backup_path() const noexcept {
    return backup_;
  }
  [[nodiscard]] const std::filesystem::path &recovery_path() const noexcept {
    return recovery_;
  }

private:
  [[nodiscard]] QdrAtomicWriteResult verify_published_bytes() const;

  std::filesystem::path target_;
  std::filesystem::path staging_;
  std::filesystem::path backup_;
  std::filesystem::path recovery_;
  std::array<std::uint64_t, 6> staged_file_seal_{};
  std::string backup_cleanup_failure_message_;
  void *published_guard_ = nullptr;
  bool target_existed_ = false;
  bool published_ = false;
  bool cleanup_pending_ = false;
  bool active_ = false;
};

using QdrAtomicWriteValidator =
    std::function<QdrAtomicWriteResult(const std::filesystem::path &temp_path)>;

/// Moves a validated project file or folder without replacing a competing entry.
[[nodiscard]] QdrAtomicWriteResult move_project_entry_no_replace(
    const std::filesystem::path &source,
    const std::filesystem::path &destination);

[[nodiscard]] QdrAtomicWriteResult write_qdr_text_atomic(
        const std::filesystem::path& target,
        std::string_view bytes,
        QdrAtomicWriteValidator validate_temp = {},
        QdrAtomicWriteDisposition disposition =
            QdrAtomicWriteDisposition::ReplaceExisting);

} // namespace quader_io
