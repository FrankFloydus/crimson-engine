////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_api_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace quader {

class QuaderApiMutationRunner;

/// Defines the scene's editing context independently of its selection kind.
enum class QuaderEditorMode { Object, Edit };

/// Detached, read-only-by-value view of the scene-owned mode context.
struct QuaderEditorModeState final {
  QuaderEditorMode mode = QuaderEditorMode::Object;
  std::uint64_t revision = 0u;
  std::uint64_t generation = 0u;
  QuaderSelectionMode component_kind = QuaderSelectionMode::Vertex;
  QuaderEntityRef active_entity;
  std::vector<QuaderEntityRef> participants;
  std::vector<QuaderEntityRef> object_results;
};

/// Public mode transitions delegate to the active scene's single context owner.
class QuaderEditorModeApi final {
public:
  QuaderEditorModeApi(QuaderApiSession session,
                      QuaderApiMutationRunner &mutations);

  [[nodiscard]] QuaderEditorMode current() const;
  [[nodiscard]] std::uint64_t revision() const;
  [[nodiscard]] std::uint64_t generation() const;
  [[nodiscard]] QuaderEditorModeState state() const;
  [[nodiscard]] bool can_set(QuaderEditorMode mode) const;
  QuaderApiVoidResult set(QuaderEditorMode mode);
  QuaderApiVoidResult toggle_edit();
  void rebind_session(QuaderApiSession session);

private:
  QuaderApiSession session_;
  QuaderApiMutationRunner &mutations_;
};

} // namespace quader
