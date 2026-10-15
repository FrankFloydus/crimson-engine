#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

namespace crimson {

// Watches completed-save records without reading scene or material source files.
// The editor owns one watcher per project and consumes this inexpensive flag on
// its main thread. Debouncing, record comparison and fallback polling belong to
// that owner, not the notification thread.
class SaveNotificationWatcher {
public:
  explicit SaveNotificationWatcher(const std::filesystem::path &project_root);
  ~SaveNotificationWatcher();

  SaveNotificationWatcher(const SaveNotificationWatcher &) = delete;
  SaveNotificationWatcher &operator=(const SaveNotificationWatcher &) = delete;
  SaveNotificationWatcher(SaveNotificationWatcher &&) = delete;
  SaveNotificationWatcher &operator=(SaveNotificationWatcher &&) = delete;

  // Initially true. Overflow or loss of the watch also requests a record recheck.
  bool consume_changed() noexcept;

  // False means the owner must poll records periodically. A failed watcher does
  // not retry or retain handles to individual source files.
  bool available() const noexcept;

  // Native error code for optional diagnostics; zero while the watch is healthy.
  std::uint32_t last_error_code() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace crimson
