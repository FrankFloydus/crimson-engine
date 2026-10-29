#pragma once
#include "resources/AssetId.h"

namespace crimson {
// Reference policy can identify protected roles without importing evaluation data.
inline const std::string PlayerStartingEffectId=canonicalAssetId("builtin:gameplay-effect/player-defaults");
inline const std::string EnemyStartingEffectId=canonicalAssetId("builtin:gameplay-effect/enemy-defaults");
}
