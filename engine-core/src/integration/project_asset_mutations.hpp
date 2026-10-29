#pragma once

#include "editor/assets/project_asset_service.hpp"

namespace crimson {
class AssetLibrary;
class SceneDocument;
namespace integration {

// Prepares Engine identity/reference metadata for the one project filesystem
// transaction. The caller clears its callback before these owners close.
quader::ProjectIoResult<quader::ProjectAssetMutationPreparation>
prepare_project_asset_mutation(AssetLibrary &assets, SceneDocument *active,
                              const quader::ProjectAssetMutation &mutation);

} // namespace integration
} // namespace crimson
