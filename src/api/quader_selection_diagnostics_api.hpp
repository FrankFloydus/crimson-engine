////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace quader {

/// Identifies one immutable selection capture queued for durable storage.
struct QuaderSelectionDumpTicket {
  std::string id;
};

/// Reports a completed durable capture and a bounded human-readable summary.
struct QuaderSelectionDumpReply {
  std::string id;
  std::filesystem::path absolute_path;
  std::string summary;
};

/// Native hosts and tests may supply the durable diagnostic directory.
struct QuaderSelectionDiagnosticsRuntimeHooks final {
  std::function<std::filesystem::path()> dump_directory;
};

/** Read-only selection diagnostics available in every product release. */
class QuaderSelectionDiagnosticsApi final {
public:
  explicit QuaderSelectionDiagnosticsApi(QuaderApiSession session);
  ~QuaderSelectionDiagnosticsApi();
  QuaderSelectionDiagnosticsApi(const QuaderSelectionDiagnosticsApi &) = delete;
  QuaderSelectionDiagnosticsApi &operator=(const QuaderSelectionDiagnosticsApi &) = delete;

  void set_runtime_hooks(QuaderSelectionDiagnosticsRuntimeHooks hooks);
  void rebind_session(QuaderApiSession session);
  /// Pins current SDK topology, selection and preview without settling it.
  [[nodiscard]] QuaderApiResult<QuaderSelectionDumpTicket> request_dump();
  /// O(1) readiness polling; never queries the live document or command catalog.
  [[nodiscard]] bool pending() const noexcept;
  [[nodiscard]] std::optional<QuaderApiResult<QuaderSelectionDumpReply>> take_completed();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
