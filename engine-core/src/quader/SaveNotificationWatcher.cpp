#include "SaveNotificationWatcher.h"

#include <atomic>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <thread>
#endif

namespace crimson {

#ifdef _WIN32
namespace {

wchar_t normalize_record_character(wchar_t value) noexcept {
  if (value == L'/') {
    return L'\\';
  }
  return value >= L'A' && value <= L'Z' ? static_cast<wchar_t>(value + (L'a' - L'A')) : value;
}

bool record_name_equals(std::wstring_view name, std::wstring_view expected) noexcept {
  if (name.size() != expected.size()) {
    return false;
  }
  for (std::size_t index = 0; index < name.size(); ++index) {
    if (normalize_record_character(name[index]) != expected[index]) {
      return false;
    }
  }
  return true;
}

bool record_name_starts_with(std::wstring_view name, std::wstring_view prefix) noexcept {
  return name.size() >= prefix.size() && record_name_equals(name.substr(0, prefix.size()), prefix);
}

bool affects_completion_records(std::wstring_view name) noexcept {
  // Watch the project root so creating or atomically replacing bridge directories
  // is visible even when no completion record existed when the watcher started.
  if (record_name_equals(name, L".quader") || record_name_equals(name, L".quader\\bridge") ||
      record_name_equals(name, L".quader\\bridge\\scenes") ||
      record_name_equals(name, L".quader\\bridge\\materials")) {
    return true;
  }
  if (!record_name_starts_with(name, L".quader\\bridge\\scenes\\") &&
      !record_name_starts_with(name, L".quader\\bridge\\materials\\")) {
    return false;
  }
  return name.size() >= 5 && record_name_equals(name.substr(name.size() - 5), L".json");
}

} // namespace
#endif

struct SaveNotificationWatcher::Impl {
  std::atomic<bool> dirty{true};
  std::atomic<bool> watching{false};
  std::atomic<std::uint32_t> error_code{0};

#ifdef _WIN32
  HANDLE directory = INVALID_HANDLE_VALUE;
  HANDLE stop_event = nullptr;
  HANDLE completion_event = nullptr;
  std::filesystem::path root_path;
  std::thread worker;

  explicit Impl(const std::filesystem::path &project_root) : root_path(project_root) {
    directory = CreateFileW(project_root.c_str(), FILE_LIST_DIRECTORY,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
      fail(GetLastError());
      return;
    }
    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event) {
      fail(GetLastError());
      return;
    }
    completion_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!completion_event) {
      fail(GetLastError());
      return;
    }
    watching.store(true, std::memory_order_release);
    try {
      worker = std::thread([this] { run(); });
    } catch (...) {
      // Thread creation may also fail while allocating its callable storage;
      // preserve this Impl so its destructor still owns and closes every handle.
      fail(ERROR_NOT_ENOUGH_MEMORY);
    }
  }

  ~Impl() {
    if (worker.joinable()) {
      SetEvent(stop_event);
      // Also cancel here so a worker between its stop check and notification wait
      // cannot keep the editor waiting for a filesystem change during shutdown.
      CancelIoEx(directory, nullptr);
      worker.join();
    }
    if (completion_event) {
      CloseHandle(completion_event);
    }
    if (stop_event) {
      CloseHandle(stop_event);
    }
    if (directory != INVALID_HANDLE_VALUE) {
      CloseHandle(directory);
    }
  }

  void fail(DWORD error) noexcept {
    error_code.store(error, std::memory_order_relaxed);
    watching.store(false, std::memory_order_release);
    dirty.store(true, std::memory_order_release);
  }

  void consume_notifications(const std::byte *buffer, DWORD size) noexcept {
    constexpr std::size_t header_size = offsetof(FILE_NOTIFY_INFORMATION, FileName);
    std::size_t offset = 0;
    while (offset < size) {
      if (size - offset < header_size || offset % alignof(FILE_NOTIFY_INFORMATION) != 0) {
        dirty.store(true, std::memory_order_release);
        return;
      }
      const auto *entry = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(buffer + offset);
      const auto name_size = static_cast<std::size_t>(entry->FileNameLength);
      if (name_size % sizeof(wchar_t) != 0 || name_size > size - offset - header_size) {
        dirty.store(true, std::memory_order_release);
        return;
      }
      if (affects_completion_records({entry->FileName, name_size / sizeof(wchar_t)})) {
        dirty.store(true, std::memory_order_release);
        return;
      }
      if (entry->NextEntryOffset == 0) {
        return;
      }
      if (entry->NextEntryOffset < header_size + name_size || entry->NextEntryOffset > size - offset) {
        dirty.store(true, std::memory_order_release);
        return;
      }
      offset += entry->NextEntryOffset;
    }
  }

  void run() noexcept {
    // Exactly 64 KiB is accepted by both local filesystems and SMB. Notifications
    // are hints: zero-length/overflow results request a complete record recheck.
    alignas(DWORD) std::array<std::byte, 64 * 1024> buffer{};
    const HANDLE events[] = {stop_event, completion_event};
    bool first_read = true;
    while (WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0) {
      ResetEvent(completion_event);
      OVERLAPPED operation{};
      operation.hEvent = completion_event;
      const BOOL queued = ReadDirectoryChangesW(
          directory, buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
          FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
              FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
          nullptr, &operation, nullptr);
      if (!queued) {
        const DWORD error = GetLastError();
        if (error == ERROR_NOTIFY_ENUM_DIR) {
          dirty.store(true, std::memory_order_release);
          WaitForSingleObject(stop_event, 10);
          continue;
        }
        fail(error);
        return;
      }

      if (first_read) {
        // Close the startup gap if the owner consumed the initial dirty flag
        // before this thread had registered its first notification request.
        dirty.store(true, std::memory_order_release);
        first_read = false;
      }
      DWORD signaled = WAIT_TIMEOUT;
      DWORD root_error = ERROR_SUCCESS;
      while (signaled == WAIT_TIMEOUT) {
        signaled = WaitForMultipleObjects(2, events, FALSE, 1000);
        if (signaled != WAIT_TIMEOUT) {
          break;
        }
        // ReadDirectoryChangesW does not report a change to the watched root
        // itself. Check only its attributes on the worker, never on editor frames.
        const DWORD attributes = GetFileAttributesW(root_path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
          root_error = GetLastError();
          break;
        }
        FILE_STANDARD_INFO directory_info{};
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
          root_error = ERROR_DIRECTORY;
          break;
        }
        if (GetFileInformationByHandleEx(directory, FileStandardInfo, &directory_info,
                                        sizeof(directory_info)) && directory_info.DeletePending) {
          root_error = ERROR_PATH_NOT_FOUND;
          break;
        }
      }
      if (signaled != WAIT_OBJECT_0 + 1) {
        const DWORD wait_error = signaled == WAIT_FAILED ? GetLastError() : root_error;
        CancelIoEx(directory, &operation);
        // Windows requires both the OVERLAPPED and its buffer to survive until
        // canceled I/O completes. This drains cancellation, not the next change.
        DWORD ignored = 0;
        GetOverlappedResult(directory, &operation, &ignored, TRUE);
        if (wait_error != ERROR_SUCCESS) {
          fail(wait_error);
        }
        return;
      }

      DWORD bytes = 0;
      if (!GetOverlappedResult(directory, &operation, &bytes, FALSE)) {
        const DWORD error = GetLastError();
        if (error == ERROR_OPERATION_ABORTED && WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0) {
          return;
        }
        if (error == ERROR_NOTIFY_ENUM_DIR) {
          dirty.store(true, std::memory_order_release);
          continue;
        }
        fail(error);
        return;
      }
      if (bytes == 0 || bytes > buffer.size()) {
        dirty.store(true, std::memory_order_release);
      } else {
        consume_notifications(buffer.data(), bytes);
      }
    }
  }
#else
  explicit Impl(const std::filesystem::path &) {
    // ERROR_NOT_SUPPORTED, kept numeric so the fallback has no Win32 dependency.
    error_code.store(50, std::memory_order_relaxed);
  }
#endif
};

SaveNotificationWatcher::SaveNotificationWatcher(const std::filesystem::path &project_root)
    : impl_(std::make_unique<Impl>(project_root)) {}

SaveNotificationWatcher::~SaveNotificationWatcher() = default;

bool SaveNotificationWatcher::consume_changed() noexcept {
  return impl_->dirty.exchange(false, std::memory_order_acq_rel);
}

bool SaveNotificationWatcher::available() const noexcept {
  return impl_->watching.load(std::memory_order_acquire);
}

std::uint32_t SaveNotificationWatcher::last_error_code() const noexcept {
  return impl_->error_code.load(std::memory_order_relaxed);
}

} // namespace crimson
