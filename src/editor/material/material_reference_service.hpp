////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_reference.hpp"
#include "core/i_undoable_command.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace quader {

class AssetRegistry;
class ModelingSessionService;
class SceneDocument;

/// Selects the semantic scene universe used by material-reference operations.
enum class MaterialReferenceUsageScope {
  Selection,
  ActiveScene,
};

/// Reports preflight status and the prepared one-shot history command.
struct MaterialReferenceReplacementPreparation final {
  bool ok = true;
  bool changed = false;
  std::string message;
  std::unique_ptr<IUndoableCommand> command;
};

/**
 * Owns scene/modeling coordination for live material-reference queries and
 * atomic reference replacement. Presentation and project-catalog metadata stay
 * outside this service; callers supply destination validation at the project
 * boundary.
 */
class MaterialReferenceService final {
public:
  using DestinationValidator =
      std::function<bool(const MaterialReference &)>;
  using ReferenceResolver =
      std::function<MaterialReference(MaterialReference)>;

  MaterialReferenceService(SceneDocument &scene, AssetRegistry &assets,
                           ModelingSessionService &modeling,
                           ReferenceResolver resolve_reference = {});

  [[nodiscard]] std::vector<MaterialReference>
  used_materials(MaterialReferenceUsageScope scope) const;

  [[nodiscard]] MaterialReferenceReplacementPreparation prepare_replacement(
      MaterialReference source, MaterialReference replacement,
      MaterialReferenceUsageScope scope,
      const DestinationValidator &destination_is_valid) const;

private:
  SceneDocument *scene_ = nullptr;
  AssetRegistry *assets_ = nullptr;
  ModelingSessionService *modeling_ = nullptr;
  ReferenceResolver resolve_reference_;
};

} // namespace quader
