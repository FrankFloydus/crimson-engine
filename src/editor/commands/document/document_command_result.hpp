////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "core/types.hpp"

#include <string>
#include <utility>
#include <vector>

namespace quader {

using DocumentCommandResult = CommandResult;

/**
 * Stores Create Entity Command payload data for document command callers.
 */
struct DocumentCreateEntityResult : DocumentCommandResult {
  EntityId entity = kInvalidEntityId;
};

/**
 * Stores Group Selection Command payload data for document command callers.
 */
struct DocumentGroupSelectionResult : DocumentCommandResult {
  EntityId group_entity = kInvalidEntityId;
};

/**
 * Stores Duplicate Entity Command payload data for document command callers.
 */
struct DocumentDuplicateEntityResult : DocumentCommandResult {
  EntityId duplicate_entity = kInvalidEntityId;
};

/**
 * Stores Duplicate Selected Hierarchy Command payload data for document command callers.
 */
struct DocumentDuplicateHierarchyResult : DocumentCommandResult {
  std::vector<EntityId> duplicate_roots;
};

inline DocumentCommandResult document_command_changed_result() {
  return {
      .ok = true,
      .changed = true,
  };
}

inline DocumentCommandResult document_command_unchanged_result() {
  return {
      .ok = true,
      .changed = false,
  };
}

inline DocumentCommandResult document_command_missing_entity_result() {
  return {
      .ok = false,
      .changed = false,
      .message = "Entity does not exist",
  };
}

inline DocumentCommandResult document_command_missing_command_result() {
  return {
      .ok = false,
      .changed = false,
      .message = "Document command is missing",
  };
}

inline DocumentCommandResult
document_command_invalid_request_result(std::string message) {
  return {
      .ok = false,
      .changed = false,
      .message = std::move(message),
  };
}

} // namespace quader
