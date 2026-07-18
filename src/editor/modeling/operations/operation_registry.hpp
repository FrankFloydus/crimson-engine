////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/operations/editor_operation.hpp>

#include <span>
#include <string_view>

namespace quader_editor_core {

/**
 * Represents an Editor Operation Registry Entry value used by the modeling
 * runtime domain.
 */
struct EditorOperationRegistryEntry final {
  EditorOperationId operation_id;
  std::string_view id;
  std::string_view class_name;
  const EditorOperation *operation;
};

/**
 * Owns Operation Registry lookup and registration for the modeling runtime
 * domain.
 */
class OperationRegistry final {
public:
  [[nodiscard]] std::span<const EditorOperationRegistryEntry> entries() const;
  [[nodiscard]] std::span<const EditorOperation *const> operations() const;
  [[nodiscard]] const EditorOperation *
  find(EditorOperationId operation_id) const;
  [[nodiscard]] const EditorOperation *find(std::string_view id) const;
};

[[nodiscard]] const OperationRegistry &operation_registry();

[[nodiscard]] std::span<const EditorOperation *const>
editor_operation_registry();
[[nodiscard]] const EditorOperation *
editor_operation_for_id(EditorOperationId operation_id);
[[nodiscard]] const EditorOperation *
editor_operation_for_id(std::string_view id);

} // namespace quader_editor_core
