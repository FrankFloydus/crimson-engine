////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/light_creation_tool_service.hpp"
#include "core/release_features.hpp"

namespace quader {

std::string_view LightCreationToolService::active_tool_id() const {
  if (!active_kind_.has_value()) {
    return {};
  }
  return *active_kind_ == LightCreationKind::Point
             ? kCreatePointLightToolId
             : kCreateDirectionalLightToolId;
}

CommandResult LightCreationToolService::begin(LightCreationKind kind) {
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (active_kind_ == kind) {
    return command_unchanged_result();
  }
  active_kind_ = kind;
  ++generation_;
  return command_changed_result();
}

CommandResult LightCreationToolService::cancel() {
  if (!active_kind_.has_value()) {
    return command_unchanged_result();
  }
  active_kind_.reset();
  ++generation_;
  return command_changed_result();
}

CommandResult
LightCreationToolService::complete(LightCreationOperation operation) {
  if (active_operation() != operation) {
    return command_unchanged_result();
  }
  active_kind_.reset();
  ++generation_;
  return command_changed_result();
}

void LightCreationToolService::reset() {
  if (active_kind_.has_value()) {
    active_kind_.reset();
    ++generation_;
  }
}

} // namespace quader
