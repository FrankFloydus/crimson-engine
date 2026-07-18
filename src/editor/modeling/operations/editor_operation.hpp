////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/operations/operation_schema.hpp>

#include <quader/modeling/commands/command.hpp>

#include <string_view>

namespace quader_editor_core {

/**
 * Native presentation view over one portable modeling operation descriptor.
 */
class EditorOperation final {
public:
  constexpr EditorOperation() = default;
  constexpr EditorOperation(
      EditorOperationId operation_id,
      const quader::modeling::OperationDescriptor *descriptor,
      std::string_view label) noexcept
      : operation_id_(operation_id), descriptor_(descriptor), label_(label) {}

  [[nodiscard]] constexpr EditorOperationId operation_id() const noexcept {
    return operation_id_;
  }
  [[nodiscard]] constexpr std::string_view id() const noexcept {
    return descriptor_ != nullptr ? descriptor_->id : std::string_view{};
  }
  [[nodiscard]] constexpr std::string_view label() const noexcept {
    return label_;
  }
  [[nodiscard]] constexpr bool supports_preview() const noexcept {
    return descriptor_ != nullptr && descriptor_->supports_preview;
  }
  [[nodiscard]] constexpr const quader::modeling::OperationDescriptor *
  portable_descriptor() const noexcept {
    return descriptor_;
  }

private:
  EditorOperationId operation_id_ = EditorOperationId::Bridge;
  const quader::modeling::OperationDescriptor *descriptor_ = nullptr;
  std::string_view label_;
};

} // namespace quader_editor_core
