////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_selection_diagnostics_api.hpp"

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_status_internal.hpp"
#include "core/user_config_paths.hpp"
#include "editor/diagnostics/selection_diagnostic_service.hpp"

#include <exception>
#include <utility>

namespace quader {

class QuaderSelectionDiagnosticsApi::Impl final {
public:
  explicit Impl(QuaderApiSession initial_session) : session(initial_session) {}
  QuaderApiSession session;
  QuaderSelectionDiagnosticsRuntimeHooks hooks;
  SelectionDiagnosticService service;
  QuaderRevisionStamp captured_revisions;
};

QuaderSelectionDiagnosticsApi::QuaderSelectionDiagnosticsApi(QuaderApiSession session)
    : impl_(std::make_unique<Impl>(session)) {}

QuaderSelectionDiagnosticsApi::~QuaderSelectionDiagnosticsApi() = default;

void QuaderSelectionDiagnosticsApi::set_runtime_hooks(QuaderSelectionDiagnosticsRuntimeHooks hooks) {
  impl_->hooks = std::move(hooks);
}

void QuaderSelectionDiagnosticsApi::rebind_session(QuaderApiSession session) {
  impl_->session = session;
}

QuaderApiResult<QuaderSelectionDumpTicket> QuaderSelectionDiagnosticsApi::request_dump() {
  const SceneSession &scene = QuaderApiSessionInternalAccess::read_scene_session(impl_->session);
  try {
    const auto directory = impl_->hooks.dump_directory
        ? impl_->hooks.dump_directory()
        : quader_user_config_directory(std::filesystem::current_path()) / "selection-dumps";
    const auto request = impl_->service.request_dump(
        scene, QuaderApiSessionInternalAccess::project_session(impl_->session),
        QuaderApiSessionInternalAccess::scene_session_id(impl_->session), directory);
    const auto status = quader_api_void_status(scene, request.ok, false, request.message,
                                              QuaderMutationKind::None);
    if (request.ok) impl_->captured_revisions = status.revisions;
    return quader_api_result_from_status(status, QuaderSelectionDumpTicket{request.id});
  } catch (const std::exception &error) {
    return quader_api_result_from_status<QuaderSelectionDumpTicket>(
        quader_api_failure_status(scene, "Could not capture selection: " + std::string(error.what())));
  }
}

bool QuaderSelectionDiagnosticsApi::pending() const noexcept { return impl_->service.pending(); }

std::optional<QuaderApiResult<QuaderSelectionDumpReply>> QuaderSelectionDiagnosticsApi::take_completed() {
  auto reply = impl_->service.take_completed();
  if (!reply) return std::nullopt;
  QuaderApiStatus status{
      .ok = reply->ok,
      .changed = false,
      .message = std::move(reply->message),
      .mutation_kind = QuaderMutationKind::None,
      .revisions = impl_->captured_revisions,
  };
  return quader_api_result_from_status(status, QuaderSelectionDumpReply{
      .id = std::move(reply->id), .absolute_path = std::move(reply->absolute_path),
      .summary = std::move(reply->summary)});
}

} // namespace quader
