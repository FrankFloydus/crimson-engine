////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_atomic_write.hpp"

#include "diagnostics/profile.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif

namespace quader_io {
namespace {

[[nodiscard]] std::string
system_error_message(const std::string &action, const std::error_code &error) {
  if (!error) {
    return action;
  }
  return action + ": " + error.message();
}

[[nodiscard]] std::string
windows_error_message(const std::string &action, unsigned long error_code) {
  std::error_code error(static_cast<int>(error_code), std::system_category());
  return system_error_message(action, error);
}

[[nodiscard]] std::string unique_temp_suffix(std::uint64_t attempt) {
  static std::atomic_uint64_t counter{0};
  const std::uint64_t tick = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::uint64_t value =
      counter.fetch_add(1, std::memory_order_relaxed) ^ tick;
  std::ostringstream stream;
  stream << std::hex << value << "-" << attempt;
  return stream.str();
}

[[nodiscard]] std::filesystem::path
target_directory_for_atomic_write(const std::filesystem::path &target) {
  const std::filesystem::path directory = target.parent_path();
  return directory.empty() ? std::filesystem::path(".") : directory;
}

[[nodiscard]] QdrAtomicWriteResult
flush_parent_directory(const std::filesystem::path &path) {
#ifdef _WIN32
  (void)path;
  return {true, {}};
#else
  const std::filesystem::path directory =
      target_directory_for_atomic_write(path);
#ifdef O_DIRECTORY
  const int descriptor = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY);
#else
  const int descriptor = ::open(directory.c_str(), O_RDONLY);
#endif
  if (descriptor < 0) {
    return {false,
            system_error_message(
                "Could not open QDR parent directory for durable flush",
                std::error_code(errno, std::generic_category()))};
  }
  const bool flushed = ::fsync(descriptor) == 0;
  const int error = flushed ? 0 : errno;
  (void)::close(descriptor);
  return flushed
             ? QdrAtomicWriteResult{true, {}}
             : QdrAtomicWriteResult{
                   false,
                   system_error_message(
                       "Could not durably flush QDR parent directory",
                       std::error_code(error, std::generic_category()))};
#endif
}

[[nodiscard]] std::filesystem::path make_private_path(
    const std::filesystem::path &target,
    const std::filesystem::path &directory, std::uint64_t attempt,
    std::string_view suffix) {
  std::filesystem::path name(".");
  name += target.filename();
  name += ".";
  name += unique_temp_suffix(attempt);
  name += suffix;
  return directory / name;
}

/**
 * Classifies an exclusive same-directory staging-file reservation attempt.
 */
enum class ReservePrivateFileStatus {
  Reserved,
  AlreadyExists,
  Failed,
};

/**
 * Carries staging-file reservation status without throwing filesystem errors.
 */
struct ReservePrivateFileResult final {
  ReservePrivateFileStatus status = ReservePrivateFileStatus::Failed;
  std::string message;
};

[[nodiscard]] ReservePrivateFileResult
reserve_private_file(const std::filesystem::path &path) {
#ifdef _WIN32
  const HANDLE file = ::CreateFileW(
      path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
      FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    (void)::CloseHandle(file);
    return {ReservePrivateFileStatus::Reserved, {}};
  }
  const unsigned long error = ::GetLastError();
  if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) {
    return {ReservePrivateFileStatus::AlreadyExists, {}};
  }
  return {ReservePrivateFileStatus::Failed,
          windows_error_message("Could not reserve QDR staging file", error)};
#else
  const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (file >= 0) {
    (void)::close(file);
    return {ReservePrivateFileStatus::Reserved, {}};
  }
  if (errno == EEXIST) {
    return {ReservePrivateFileStatus::AlreadyExists, {}};
  }
  return {ReservePrivateFileStatus::Failed,
          system_error_message(
              "Could not reserve QDR staging file",
              std::error_code(errno, std::generic_category()))};
#endif
}

/**
 * Classifies one atomic no-replacement move without collapsing conflicts.
 */
enum class AtomicMoveStatus {
  Moved,
  DestinationExists,
  SourceMissing,
  Failed,
};

/**
 * Carries the classified outcome and diagnostics for one atomic move.
 */
struct AtomicMoveResult final {
  AtomicMoveStatus status = AtomicMoveStatus::Failed;
  std::string message;
};

[[nodiscard]] AtomicMoveResult atomic_move_no_replace(
    const std::filesystem::path &source,
    const std::filesystem::path &destination, std::string_view action) {
#ifdef _WIN32
  if (::MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_WRITE_THROUGH) != 0) {
    return {AtomicMoveStatus::Moved, {}};
  }
  const unsigned long error = ::GetLastError();
  if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) {
    return {AtomicMoveStatus::DestinationExists, {}};
  }
  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
    return {AtomicMoveStatus::SourceMissing, {}};
  }
  return {AtomicMoveStatus::Failed,
          windows_error_message(std::string(action), error)};
#elif defined(__linux__)
  const long moved = ::syscall(SYS_renameat2, AT_FDCWD, source.c_str(),
                               AT_FDCWD, destination.c_str(),
                               RENAME_NOREPLACE);
  if (moved == 0) {
    QdrAtomicWriteResult flushed = flush_parent_directory(destination);
    return {AtomicMoveStatus::Moved,
            flushed.ok ? std::string{} : std::move(flushed.message)};
  }
  const int error = errno;
  if (error == EEXIST) {
    return {AtomicMoveStatus::DestinationExists, {}};
  }
  if (error == ENOENT) {
    return {AtomicMoveStatus::SourceMissing, {}};
  }
  return {AtomicMoveStatus::Failed,
          system_error_message(std::string(action),
                               std::error_code(error, std::generic_category()))};
#else
  (void)source;
  (void)destination;
  return {AtomicMoveStatus::Failed,
          std::string(action) +
              ": atomic no-replace rename is unavailable on this platform"};
#endif
}

/**
 * Carries the private destination chosen while moving a transaction file.
 */
struct UniqueAtomicMoveResult final {
  AtomicMoveStatus status = AtomicMoveStatus::Failed;
  std::filesystem::path destination;
  std::string message;
};

[[nodiscard]] UniqueAtomicMoveResult move_to_unique_transaction_path(
    const std::filesystem::path &source,
    const std::filesystem::path &target_name_source, std::string_view suffix,
    std::string_view action) {
  const std::filesystem::path directory =
      target_directory_for_atomic_write(target_name_source);
  constexpr std::uint64_t kMaxAttempts = 32;
  for (std::uint64_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
    const std::filesystem::path candidate = make_private_path(
        target_name_source, directory, attempt, suffix);
    AtomicMoveResult moved =
        atomic_move_no_replace(source, candidate, action);
    if (moved.status == AtomicMoveStatus::DestinationExists) {
      continue;
    }
    return {
        .status = moved.status,
        .destination = moved.status == AtomicMoveStatus::Moved
                           ? candidate
                           : std::filesystem::path{},
        .message = std::move(moved.message),
    };
  }
  return {
      .status = AtomicMoveStatus::Failed,
      .message = "Could not allocate a unique QDR transaction path.",
  };
}

[[nodiscard]] QdrAtomicWriteResult find_unique_transaction_path(
    const std::filesystem::path &target, std::string_view suffix,
    std::filesystem::path &candidate) {
  const std::filesystem::path directory =
      target_directory_for_atomic_write(target);
  std::error_code error;
  constexpr std::uint64_t kMaxAttempts = 32;
  for (std::uint64_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
    std::filesystem::path proposed =
        make_private_path(target, directory, attempt, suffix);
    if (!std::filesystem::exists(proposed, error)) {
      if (error) {
        return {false, system_error_message(
                           "Could not inspect QDR recovery path", error)};
      }
      candidate = std::move(proposed);
      return {true, {}};
    }
    if (error) {
      return {false, system_error_message(
                         "Could not inspect QDR recovery path", error)};
    }
  }
  return {false, "Could not allocate a unique QDR recovery path."};
}

[[nodiscard]] QdrAtomicWriteResult atomic_replace_with_backup(
    const std::filesystem::path &replacement,
    const std::filesystem::path &target,
    const std::filesystem::path &windows_backup) {
#ifdef _WIN32
  // ReplaceFileW is the supported Windows primitive that atomically installs a
  // replacement while retaining the prior file. REPLACEFILE_WRITE_THROUGH is
  // documented as unsupported, and Windows exposes no supported parent-folder
  // fsync equivalent here. This establishes process-visible namespace ordering
  // and recoverability, but it must not be described as power-loss durability
  // across the scene/manifest boundary.
  if (::ReplaceFileW(target.c_str(), replacement.c_str(),
                     windows_backup.c_str(), 0u, nullptr, nullptr) == 0) {
    return {false,
            windows_error_message(
                "Could not atomically replace QDR destination",
                ::GetLastError())};
  }
  return {true, {}};
#elif defined(__linux__)
  (void)windows_backup;
  const long exchanged =
      ::syscall(SYS_renameat2, AT_FDCWD, replacement.c_str(), AT_FDCWD,
                target.c_str(), RENAME_EXCHANGE);
  if (exchanged != 0) {
    return {false,
            system_error_message(
                "Could not atomically exchange QDR destination",
                std::error_code(errno, std::generic_category()))};
  }
  QdrAtomicWriteResult flushed = flush_parent_directory(target);
  return {true, flushed.ok ? std::string{} : std::move(flushed.message)};
#else
  (void)replacement;
  (void)target;
  (void)windows_backup;
  return {false,
          "Atomic QDR replacement with backup is unavailable on this "
          "platform."};
#endif
}

[[nodiscard]] QdrAtomicWriteResult atomic_restore_from_backup(
    const std::filesystem::path &backup,
    const std::filesystem::path &target,
    const std::filesystem::path &windows_recovery) {
#ifdef _WIN32
  if (::ReplaceFileW(target.c_str(), backup.c_str(), windows_recovery.c_str(),
                     0u, nullptr, nullptr) == 0) {
    return {false,
            windows_error_message(
                "Could not atomically restore prior QDR destination",
                ::GetLastError())};
  }
  return {true, {}};
#elif defined(__linux__)
  (void)windows_recovery;
  const long exchanged =
      ::syscall(SYS_renameat2, AT_FDCWD, backup.c_str(), AT_FDCWD,
                target.c_str(), RENAME_EXCHANGE);
  if (exchanged != 0) {
    return {false,
            system_error_message(
                "Could not atomically restore prior QDR destination",
                std::error_code(errno, std::generic_category()))};
  }
  QdrAtomicWriteResult flushed = flush_parent_directory(target);
  return {true, flushed.ok ? std::string{} : std::move(flushed.message)};
#else
  (void)backup;
  (void)target;
  (void)windows_recovery;
  return {false,
          "Atomic QDR restoration with backup is unavailable on this "
          "platform."};
#endif
}

void update_file_hash(std::span<const std::byte> bytes, std::uint64_t &first,
                      std::uint64_t &second) noexcept {
  constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ull;
  for (const std::byte value : bytes) {
    const std::uint64_t byte = static_cast<std::uint64_t>(value);
    first = (first ^ byte) * kFnvPrime;
    second ^= byte + 0x9e3779b97f4a7c15ull + (second << 6u) +
              (second >> 2u);
  }
}

[[nodiscard]] QdrAtomicWriteResult capture_file_seal(
    const std::filesystem::path &path,
    std::array<std::uint64_t, 6> &seal) {
  constexpr std::size_t kBufferSize = 64u * 1024u;
  std::array<std::byte, kBufferSize> buffer{};
  std::uint64_t first_hash = 1'469'598'103'934'665'603ull;
  std::uint64_t second_hash = 0x6a09e667f3bcc909ull;

#ifdef _WIN32
  /**
   * Keeps one Windows file handle alive while its identity is sealed.
   */
  class ScopedHandle final {
  public:
    explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
    ~ScopedHandle() {
      if (handle_ != INVALID_HANDLE_VALUE) {
        (void)::CloseHandle(handle_);
      }
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

  private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
  };

  ScopedHandle file(::CreateFileW(
      path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
      nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    return {false, windows_error_message("Could not open QDR transaction file",
                                         ::GetLastError())};
  }
  BY_HANDLE_FILE_INFORMATION before{};
  if (::GetFileInformationByHandle(file.get(), &before) == 0) {
    return {false,
            windows_error_message("Could not inspect QDR transaction file",
                                  ::GetLastError())};
  }
  if ((before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
    return {false, "QDR transaction target is not a regular file."};
  }
  for (;;) {
    DWORD read = 0u;
    if (::ReadFile(file.get(), buffer.data(),
                   static_cast<DWORD>(buffer.size()), &read, nullptr) == 0) {
      return {false,
              windows_error_message("Could not read QDR transaction file",
                                    ::GetLastError())};
    }
    if (read == 0u) {
      break;
    }
    update_file_hash(std::span<const std::byte>(buffer.data(), read),
                     first_hash, second_hash);
  }
  BY_HANDLE_FILE_INFORMATION after{};
  if (::GetFileInformationByHandle(file.get(), &after) == 0) {
    return {false,
            windows_error_message("Could not re-inspect QDR transaction file",
                                  ::GetLastError())};
  }
  if (before.dwVolumeSerialNumber != after.dwVolumeSerialNumber ||
      before.nFileIndexHigh != after.nFileIndexHigh ||
      before.nFileIndexLow != after.nFileIndexLow ||
      before.nFileSizeHigh != after.nFileSizeHigh ||
      before.nFileSizeLow != after.nFileSizeLow ||
      before.ftLastWriteTime.dwHighDateTime !=
          after.ftLastWriteTime.dwHighDateTime ||
      before.ftLastWriteTime.dwLowDateTime !=
          after.ftLastWriteTime.dwLowDateTime) {
    return {false, "QDR transaction file changed while it was being sealed."};
  }
  seal = {
      before.dwVolumeSerialNumber,
      (static_cast<std::uint64_t>(before.nFileIndexHigh) << 32u) |
          before.nFileIndexLow,
      (static_cast<std::uint64_t>(before.nFileSizeHigh) << 32u) |
          before.nFileSizeLow,
      (static_cast<std::uint64_t>(before.ftLastWriteTime.dwHighDateTime)
       << 32u) |
          before.ftLastWriteTime.dwLowDateTime,
      first_hash,
      second_hash,
  };
  return {true, {}};
#else
  const int raw_file = ::open(path.c_str(), O_RDONLY);
  if (raw_file < 0) {
    return {false,
            system_error_message(
                "Could not open QDR transaction file",
                std::error_code(errno, std::generic_category()))};
  }
  /**
   * Keeps one POSIX file descriptor alive while its identity is sealed.
   */
  class ScopedFile final {
  public:
    explicit ScopedFile(int file) : file_(file) {}
    ~ScopedFile() { (void)::close(file_); }
    [[nodiscard]] int get() const noexcept { return file_; }

  private:
    int file_ = -1;
  } file(raw_file);
  struct stat before {};
  if (::fstat(file.get(), &before) != 0 || !S_ISREG(before.st_mode)) {
    return {false,
            system_error_message(
                "Could not inspect QDR transaction file",
                std::error_code(errno, std::generic_category()))};
  }
  for (;;) {
    const ssize_t read = ::read(file.get(), buffer.data(), buffer.size());
    if (read < 0) {
      return {false,
              system_error_message(
                  "Could not read QDR transaction file",
                  std::error_code(errno, std::generic_category()))};
    }
    if (read == 0) {
      break;
    }
    update_file_hash(
        std::span<const std::byte>(buffer.data(),
                                  static_cast<std::size_t>(read)),
        first_hash, second_hash);
  }
  struct stat after {};
  if (::fstat(file.get(), &after) != 0) {
    return {false,
            system_error_message(
                "Could not re-inspect QDR transaction file",
                std::error_code(errno, std::generic_category()))};
  }
  if (before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
      before.st_size != after.st_size || before.st_mtime != after.st_mtime) {
    return {false, "QDR transaction file changed while it was being sealed."};
  }
  seal = {
      static_cast<std::uint64_t>(before.st_dev),
      static_cast<std::uint64_t>(before.st_ino),
      static_cast<std::uint64_t>(before.st_size),
      static_cast<std::uint64_t>(before.st_mtime),
      first_hash,
      second_hash,
  };
  return {true, {}};
#endif
}

[[nodiscard]] QdrAtomicWriteResult retain_published_guard(
    const std::filesystem::path &target, void *&guard) {
#ifdef _WIN32
  const HANDLE file = ::CreateFileW(target.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return {false, windows_error_message(
                       "Could not guard published QDR destination",
                       ::GetLastError())};
  }
  guard = file;
#else
  (void)target;
  guard = nullptr;
#endif
  return {true, {}};
}

void release_published_guard(void *&guard) noexcept {
#ifdef _WIN32
  if (guard != nullptr) {
    (void)::CloseHandle(static_cast<HANDLE>(guard));
  }
#endif
  guard = nullptr;
}

[[nodiscard]] QdrAtomicWriteResult
flush_file_to_storage(const std::filesystem::path &path) {
#ifdef _WIN32
  const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return {false, windows_error_message(
                       "Could not open temporary QDR file for durable flush",
                       ::GetLastError())};
  }
  const bool flushed = ::FlushFileBuffers(file) != 0;
  const unsigned long error = flushed ? ERROR_SUCCESS : ::GetLastError();
  (void)::CloseHandle(file);
  return flushed
             ? QdrAtomicWriteResult{true, {}}
             : QdrAtomicWriteResult{
                   false, windows_error_message(
                              "Could not durably flush temporary QDR file",
                              error)};
#else
  const int file = ::open(path.c_str(), O_RDWR);
  if (file < 0) {
    return {false,
            system_error_message(
                "Could not open temporary QDR file for durable flush",
                std::error_code(errno, std::generic_category()))};
  }
  const bool flushed = ::fsync(file) == 0;
  const int error = flushed ? 0 : errno;
  (void)::close(file);
  return flushed
             ? QdrAtomicWriteResult{true, {}}
             : QdrAtomicWriteResult{
                   false,
                   system_error_message(
                       "Could not durably flush temporary QDR file",
                       std::error_code(error, std::generic_category()))};
#endif
}

[[nodiscard]] QdrAtomicWriteResult
write_file(const std::filesystem::path &temp_path, std::string_view bytes) {
  QDR_PROFILE_FUNCTION();
  std::ofstream stream(temp_path,
                       std::ios::out | std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    return {false,
            "Could not create temporary QDR file: " + temp_path.string()};
  }
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!stream.good()) {
    return {false,
            "Could not write temporary QDR file: " + temp_path.string()};
  }
  stream.flush();
  if (!stream.good()) {
    return {false,
            "Could not flush temporary QDR file: " + temp_path.string()};
  }
  stream.close();
  if (!stream.good()) {
    return {false,
            "Could not close temporary QDR file: " + temp_path.string()};
  }
  return flush_file_to_storage(temp_path);
}

} // namespace

QdrAtomicFileTransaction::~QdrAtomicFileTransaction() {
  if (active_) {
    try {
      (void)rollback();
    } catch (...) {
    }
  }
  release_published_guard(published_guard_);
}

QdrAtomicWriteResult QdrAtomicFileTransaction::prepare(
    const std::filesystem::path &target) {
  QDR_PROFILE_FUNCTION();
  if (active_) {
    return {false, "QDR file transaction is already active."};
  }
  if (target.empty() || target.filename().empty()) {
    return {false, "QDR file path is empty."};
  }
  const std::filesystem::path directory =
      target_directory_for_atomic_write(target);
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) {
    return {false,
            system_error_message("QDR output directory does not exist",
                                 error)};
  }
  const bool target_exists = std::filesystem::exists(target, error);
  if (error) {
    return {false,
            system_error_message("Could not inspect QDR destination", error)};
  }
  if (target_exists && !std::filesystem::is_regular_file(target, error)) {
    return {false, system_error_message(
                       "QDR destination is not a regular file", error)};
  }
  if (error) {
    return {false,
            system_error_message("Could not inspect QDR destination", error)};
  }

  constexpr std::uint64_t kMaxAttempts = 32;
  for (std::uint64_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
    const std::filesystem::path candidate =
        make_private_path(target, directory, attempt, ".stage");
    ReservePrivateFileResult reserved = reserve_private_file(candidate);
    if (reserved.status == ReservePrivateFileStatus::AlreadyExists) {
      continue;
    }
    if (reserved.status == ReservePrivateFileStatus::Failed) {
      return {false, std::move(reserved.message)};
    }
    target_ = target;
    staging_ = candidate;
    backup_.clear();
    recovery_.clear();
    staged_file_seal_ = {};
    backup_cleanup_failure_message_.clear();
    release_published_guard(published_guard_);
    target_existed_ = target_exists;
    published_ = false;
    cleanup_pending_ = false;
    active_ = true;
    return {true, {}};
  }
  return {false, "Could not allocate a unique QDR staging file."};
}

QdrAtomicWriteResult QdrAtomicFileTransaction::publish() {
  QDR_PROFILE_FUNCTION();
  if (!active_ || published_) {
    return {false, "QDR file transaction is not ready to publish."};
  }
  QdrAtomicWriteResult sealed =
      capture_file_seal(staging_, staged_file_seal_);
  if (!sealed.ok) {
    return sealed;
  }
  std::array<std::uint64_t, 6> confirmed_seal{};
  sealed = capture_file_seal(staging_, confirmed_seal);
  if (!sealed.ok) {
    return sealed;
  }
  if (confirmed_seal != staged_file_seal_) {
    return {false, "QDR staging file changed before publication."};
  }

  std::string durability_warning;
  if (target_existed_) {
#ifdef _WIN32
    std::filesystem::path backup_candidate;
    QdrAtomicWriteResult allocated = find_unique_transaction_path(
        target_, ".rollback", backup_candidate);
    if (!allocated.ok) {
      return allocated;
    }
    backup_ = std::move(backup_candidate);
#else
    backup_ = staging_;
#endif
    try {
      backup_cleanup_failure_message_ =
          "The QDR file was committed, but its obsolete rollback backup "
          "could not be removed: " +
          backup_.string();
    } catch (...) {
      backup_.clear();
      return {false,
              "Could not prepare QDR rollback-backup diagnostics."};
    }

    QdrAtomicWriteResult replaced =
        atomic_replace_with_backup(staging_, target_, backup_);
    if (!replaced.ok) {
      backup_.clear();
      backup_cleanup_failure_message_.clear();
      return replaced;
    }
    durability_warning = std::move(replaced.message);
  } else {
    AtomicMoveResult installed = atomic_move_no_replace(
        staging_, target_, "Could not publish staged QDR file");
    if (installed.status != AtomicMoveStatus::Moved) {
      return {false,
              installed.message.empty()
                  ? "Could not publish staged QDR file without replacement."
                  : std::move(installed.message)};
    }
    durability_warning = std::move(installed.message);
  }

  staging_.clear();
  published_ = true;

  QdrAtomicWriteResult guarded =
      retain_published_guard(target_, published_guard_);
  if (!guarded.ok) {
    return guarded;
  }
  QdrAtomicWriteResult verified = verify_published();
  if (!verified.ok) {
    return verified;
  }
  return {true, std::move(durability_warning)};
}

QdrAtomicWriteResult QdrAtomicFileTransaction::verify_published() const {
  QDR_PROFILE_FUNCTION();
  if (!active_ || !published_) {
    return {false, "QDR file transaction is not published."};
  }
#ifdef _WIN32
  if (published_guard_ == nullptr) {
    return {false, "Published QDR destination is not guarded."};
  }
#endif
  return verify_published_bytes();
}

QdrAtomicWriteResult QdrAtomicFileTransaction::verify_published_bytes() const {
  if (!active_ || !published_) {
    return {false, "QDR file transaction is not published."};
  }
  std::array<std::uint64_t, 6> current_seal{};
  QdrAtomicWriteResult captured = capture_file_seal(target_, current_seal);
  if (!captured.ok) {
    return captured;
  }
  if (current_seal != staged_file_seal_) {
    return {false, "Published QDR destination changed unexpectedly."};
  }
  return {true, {}};
}

QdrAtomicWriteResult QdrAtomicFileTransaction::rollback() {
  QDR_PROFILE_FUNCTION();
  if (!active_) {
    return {true, {}};
  }

  const auto clear_state = [&]() noexcept {
    release_published_guard(published_guard_);
    target_.clear();
    staging_.clear();
    backup_.clear();
    recovery_.clear();
    staged_file_seal_ = {};
    backup_cleanup_failure_message_.clear();
    target_existed_ = false;
    published_ = false;
    cleanup_pending_ = false;
    active_ = false;
  };

  const auto remove_transaction_file =
      [](std::filesystem::path &path,
         std::string_view action) -> QdrAtomicWriteResult {
    if (path.empty()) {
      return {true, {}};
    }
    std::error_code error;
    (void)std::filesystem::remove(path, error);
    if (error) {
      return {false, system_error_message(std::string(action), error)};
    }
    path.clear();
    return {true, {}};
  };

  if (cleanup_pending_) {
    QdrAtomicWriteResult removed = remove_transaction_file(
        staging_, "Could not remove QDR staging file");
    if (!removed.ok) {
      return removed;
    }
    removed = remove_transaction_file(
        backup_, "Could not remove obsolete QDR rollback backup");
    if (!removed.ok) {
      return removed;
    }
    removed = remove_transaction_file(
        recovery_, "Could not remove rolled-back QDR recovery file");
    if (!removed.ok) {
      return removed;
    }
    clear_state();
    return {true, {}};
  }

  if (!published_) {
    release_published_guard(published_guard_);
    if (!backup_.empty() || !recovery_.empty()) {
      return {false,
              "QDR recovery state is incomplete; preserved transaction files "
              "were not removed."};
    }
    QdrAtomicWriteResult removed = remove_transaction_file(
        staging_, "Could not remove QDR staging file");
    if (!removed.ok) {
      return removed;
    }
    clear_state();
    return {true, {}};
  }

  QdrAtomicWriteResult verified = verify_published_bytes();
  if (!verified.ok) {
    return verified;
  }
  release_published_guard(published_guard_);

  if (target_existed_) {
    if (backup_.empty()) {
      return {false,
              "Prior QDR destination backup is unavailable for rollback."};
    }
    if (recovery_.empty()) {
#ifdef _WIN32
      std::filesystem::path recovery_candidate;
      QdrAtomicWriteResult allocated = find_unique_transaction_path(
          target_, ".quarantine", recovery_candidate);
      if (!allocated.ok) {
        return allocated;
      }
      recovery_ = std::move(recovery_candidate);
#else
      recovery_ = backup_;
#endif
    }

    QdrAtomicWriteResult restored =
        atomic_restore_from_backup(backup_, target_, recovery_);
    if (!restored.ok) {
      QdrAtomicWriteResult guarded =
          retain_published_guard(target_, published_guard_);
      if (!guarded.ok) {
        restored.message += restored.message.empty() ? "" : "; ";
        restored.message += "published guard reacquisition failed: " +
                            guarded.message;
      }
      return restored;
    }

    std::array<std::uint64_t, 6> recovered_seal{};
    QdrAtomicWriteResult captured =
        capture_file_seal(recovery_, recovered_seal);
    if (!captured.ok || recovered_seal != staged_file_seal_) {
      QdrAtomicWriteResult reversed =
          atomic_restore_from_backup(recovery_, target_, backup_);
      if (reversed.ok) {
        recovery_.clear();
      }
      return {
          false,
          reversed.ok
              ? "QDR destination changed during rollback; the conflicting "
                "destination was restored and the prior file remains at " +
                    backup_.string()
              : "QDR destination changed during rollback; recovery files "
                "remain at " +
                    backup_.string() + " and " + recovery_.string()};
    }

    backup_.clear();
    published_ = false;
    cleanup_pending_ = true;
    QdrAtomicWriteResult removed = remove_transaction_file(
        recovery_, "Could not remove rolled-back QDR recovery file");
    if (!removed.ok) {
      return removed;
    }
    clear_state();
    return {true, std::move(restored.message)};
  }

  UniqueAtomicMoveResult quarantined = move_to_unique_transaction_path(
      target_, target_, ".quarantine",
      "Could not quarantine published QDR destination");
  if (quarantined.status == AtomicMoveStatus::SourceMissing) {
    return {false,
            "Published QDR destination disappeared during rollback; recovery "
            "state was retained."};
  }
  if (quarantined.status != AtomicMoveStatus::Moved) {
    return {false, quarantined.message.empty()
                       ? "Could not quarantine published QDR destination."
                       : std::move(quarantined.message)};
  }
  recovery_ = std::move(quarantined.destination);

  std::array<std::uint64_t, 6> recovered_seal{};
  QdrAtomicWriteResult captured =
      capture_file_seal(recovery_, recovered_seal);
  if (!captured.ok || recovered_seal != staged_file_seal_) {
    AtomicMoveResult returned = atomic_move_no_replace(
        recovery_, target_, "Could not return conflicting QDR destination");
    if (returned.status == AtomicMoveStatus::Moved) {
      recovery_.clear();
    }
    return {false,
            returned.status == AtomicMoveStatus::Moved
                ? "QDR destination changed during rollback; the conflicting "
                  "destination was restored and transaction state retained."
                : "QDR destination changed during rollback; its recovery path "
                  "was retained at " +
                      recovery_.string()};
  }

  published_ = false;
  cleanup_pending_ = true;
  QdrAtomicWriteResult removed = remove_transaction_file(
      recovery_, "Could not remove rolled-back QDR recovery file");
  if (!removed.ok) {
    return removed;
  }
  clear_state();
  return {true, std::move(quarantined.message)};
}

QdrAtomicWriteResult QdrAtomicFileTransaction::commit() {
  if (!active_ || !published_) {
    return {false, "QDR file transaction is not published."};
  }

  QdrAtomicWriteResult verified = verify_published();
  if (!verified.ok) {
    return verified;
  }
  published_ = false;
  cleanup_pending_ = true;
  try {
    std::error_code error;
    if (!backup_.empty()) {
      (void)std::filesystem::remove(backup_, error);
    }
    if (error) {
      return {true, std::move(backup_cleanup_failure_message_)};
    }
  } catch (...) {
    return {true, std::move(backup_cleanup_failure_message_)};
  }

  release_published_guard(published_guard_);
  target_.clear();
  staging_.clear();
  backup_.clear();
  recovery_.clear();
  staged_file_seal_ = {};
  backup_cleanup_failure_message_.clear();
  target_existed_ = false;
  cleanup_pending_ = false;
  active_ = false;
  return {true, {}};
}

bool QdrAtomicFileTransaction::preserve_published_for_recovery() noexcept {
  if (!active_ || !published_) {
    return false;
  }
  release_published_guard(published_guard_);
  published_ = false;
  cleanup_pending_ = false;
  active_ = false;
  return true;
}

QdrAtomicWriteResult write_qdr_text_atomic(
    const std::filesystem::path &target, std::string_view bytes,
    QdrAtomicWriteValidator validate_temp) {
  QDR_PROFILE_FUNCTION();
  QdrAtomicFileTransaction transaction;
  const auto fail_after_rollback =
      [&transaction](QdrAtomicWriteResult failure) -> QdrAtomicWriteResult {
    QdrAtomicWriteResult rolled_back = transaction.rollback();
    if (!rolled_back.ok) {
      failure.message += failure.message.empty() ? "" : "; ";
      failure.message += "QDR transaction rollback failed: " +
                         rolled_back.message;
    }
    return failure;
  };

  QdrAtomicWriteResult prepared = transaction.prepare(target);
  if (!prepared.ok) {
    return prepared;
  }

  QdrAtomicWriteResult written = [&]() {
    QDR_PROFILE_SCOPE("QDR write staging file");
    return write_file(transaction.staging_path(), bytes);
  }();
  if (!written.ok) {
    return fail_after_rollback(std::move(written));
  }

  if (validate_temp) {
    QdrAtomicWriteResult validated;
    try {
      validated = [&]() {
        QDR_PROFILE_SCOPE("QDR validate staging file");
        return validate_temp(transaction.staging_path());
      }();
    } catch (const std::exception &exception) {
      QdrAtomicWriteResult rolled_back = transaction.rollback();
      std::string message =
          std::string("QDR temp validation threw: ") + exception.what();
      if (!rolled_back.ok) {
        message += "; QDR transaction rollback failed: " +
                   rolled_back.message;
      }
      return {false, std::move(message)};
    } catch (...) {
      return fail_after_rollback(
          {false, "QDR temp validation threw an unknown exception."});
    }
    if (!validated.ok) {
      return fail_after_rollback(std::move(validated));
    }
  }

  QdrAtomicWriteResult published = [&]() {
    QDR_PROFILE_SCOPE("QDR publish staging file");
    return transaction.publish();
  }();
  if (!published.ok) {
    return fail_after_rollback(std::move(published));
  }

  QdrAtomicWriteResult committed = transaction.commit();
  if (!committed.ok) {
    return fail_after_rollback(std::move(committed));
  }
  if (published.message.empty()) {
    return committed;
  }
  if (!committed.message.empty()) {
    published.message += "; " + committed.message;
  }
  return published;
}

} // namespace quader_io
