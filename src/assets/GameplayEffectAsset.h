#pragma once
#include "gameplay/Effects.h"
#include "GameplayEffectIds.h"
#include <filesystem>
#include <memory>
#include <string_view>

namespace crimson {
std::shared_ptr<const GameplayEffectAsset> playerStartingEffect();
std::shared_ptr<const GameplayEffectAsset> enemyStartingEffect();
std::shared_ptr<const GameplayEffectAsset> engineGameplayEffect(const std::string& id);
// Starter effects keep their reserved identities while their game values remain editable.
bool editableGameplayEffect(const std::string& id);
void validateGameplayEffectAsset(const GameplayEffectAsset&);
std::string serializeGameplayEffectAsset(const GameplayEffectAsset&);
GameplayEffectAsset deserializeGameplayEffectAsset(std::string_view,const std::string& expectedId={});
std::filesystem::path gameplayEffectAssetPath(const std::filesystem::path& root,const std::string& id);
GameplayEffectAsset loadGameplayEffectAsset(const std::filesystem::path& root,const std::string& id);
void saveGameplayEffectAsset(const std::filesystem::path& root,const GameplayEffectAsset&);
GameplayEffectAsset createGameplayEffectAsset(const std::filesystem::path& root,const std::string& name="New Gameplay Effect");
GameplayEffectAsset copyGameplayEffectAsset(const std::filesystem::path& root,const GameplayEffectAsset&,const std::string& name={});
void ensureEngineGameplayEffects(const std::filesystem::path& root);
}
