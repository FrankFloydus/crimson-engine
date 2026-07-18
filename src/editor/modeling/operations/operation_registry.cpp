////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/operations/operation_registry.hpp>

#include <editor/modeling/operations/portable_operation_mapping.hpp>

#include <array>

namespace quader_editor_core {

namespace {

constexpr std::size_t kOperationCount =
    detail::kPortableOperationMappings.size();

std::span<const EditorOperation> operation_values() {
  static constexpr std::array<EditorOperation, kOperationCount> kOperations =
      [] {
        std::array<EditorOperation, kOperationCount> result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
          const PortableOperationMapping &mapping =
              detail::kPortableOperationMappings[index];
          result[index] = EditorOperation(
              mapping.native_id,
              quader::modeling::operation_descriptor(mapping.portable_kind),
              mapping.native_label);
        }
        return result;
      }();
  return kOperations;
}

std::span<const EditorOperationRegistryEntry> operation_registry_entries() {
  static const std::array<EditorOperationRegistryEntry, kOperationCount>
      kEntries = [] {
        std::array<EditorOperationRegistryEntry, kOperationCount> result{};
        const std::span<const EditorOperation> operations = operation_values();
        for (std::size_t index = 0; index < result.size(); ++index) {
          const EditorOperation &operation = operations[index];
          result[index] = {
              operation.operation_id(),
              operation.id(),
              "PortableOperationDescriptor",
              &operation,
          };
        }
        return result;
      }();
  return kEntries;
}

std::span<const EditorOperation *const> operation_entries() {
  static const std::array<const EditorOperation *, kOperationCount>
      kOperations = [] {
        std::array<const EditorOperation *, kOperationCount> result{};
        const std::span<const EditorOperationRegistryEntry> entries =
            operation_registry_entries();
        for (std::size_t index = 0; index < entries.size(); ++index) {
          result[index] = entries[index].operation;
        }
        return result;
      }();
  return kOperations;
}

} // namespace

std::span<const EditorOperationRegistryEntry>
OperationRegistry::entries() const {
  return operation_registry_entries();
}

std::span<const EditorOperation *const> OperationRegistry::operations() const {
  return operation_entries();
}

const EditorOperation *
OperationRegistry::find(EditorOperationId operation_id) const {
  for (const EditorOperationRegistryEntry &entry : entries()) {
    if (entry.operation_id == operation_id) {
      return entry.operation;
    }
  }
  return nullptr;
}

const EditorOperation *OperationRegistry::find(std::string_view id) const {
  for (const EditorOperationRegistryEntry &entry : entries()) {
    if (entry.id == id) {
      return entry.operation;
    }
  }
  return nullptr;
}

const OperationRegistry &operation_registry() {
  static const OperationRegistry kRegistry;
  return kRegistry;
}

std::span<const EditorOperation *const> editor_operation_registry() {
  return operation_registry().operations();
}

const EditorOperation *editor_operation_for_id(EditorOperationId operation_id) {
  return operation_registry().find(operation_id);
}

const EditorOperation *editor_operation_for_id(std::string_view id) {
  return operation_registry().find(id);
}

} // namespace quader_editor_core
