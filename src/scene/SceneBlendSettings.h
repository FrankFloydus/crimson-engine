#pragma once

#include "assets/ModelMaterialOverrides.h"
#include <crimson/renderer/forward/MaterialGraph.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crimson {
// Occurrence-owned appearance, guarded by the source Blend asset identity.
// Missing layers inherit that asset's authored settings without conversion.
struct SceneBlendMaterialSettings {
    std::uint32_t slot{};
    AssetRef<MaterialAsset> material;
    std::array<std::optional<MaterialBlendLayer>,4> layers;
    bool operator==(const SceneBlendMaterialSettings&) const = default;
};
using SceneBlendSettings=std::vector<SceneBlendMaterialSettings>;

inline const SceneBlendMaterialSettings* findSceneBlendMaterialSettings(const SceneBlendSettings& settings,std::uint32_t slot,const std::string& materialId) {
    const auto found=std::find_if(settings.begin(),settings.end(),[&](const auto& value){return value.slot==slot&&value.material.id==materialId;});
    return found==settings.end()?nullptr:&*found;
}
inline SceneBlendMaterialSettings* findSceneBlendMaterialSettings(SceneBlendSettings& settings,std::uint32_t slot,const std::string& materialId) {
    const auto found=std::find_if(settings.begin(),settings.end(),[&](const auto& value){return value.slot==slot&&value.material.id==materialId;});
    return found==settings.end()?nullptr:&*found;
}
inline const MaterialBlendLayer* findSceneBlendLayer(const SceneBlendSettings& settings,std::uint32_t slot,const std::string& materialId,std::uint32_t layer) {
    if(layer>=4)return nullptr;
    const auto* value=findSceneBlendMaterialSettings(settings,slot,materialId);
    return value&&value->layers[layer]?&*value->layers[layer]:nullptr;
}
inline void validateSceneBlendLayer(const MaterialBlendLayer& layer) {
    for(const float value:{layer.heightInfluence,layer.blendSoftness,layer.heightMidpoint,layer.normalInfluence,layer.roughnessInfluence,
        layer.colorInfluence,layer.metallicInfluence,layer.occlusionInfluence})
        if(!std::isfinite(value)||value<0||value>1)throw std::runtime_error("Blend layer influences, midpoint and softness must be finite and in 0-1.");
    if(!std::isfinite(layer.heightScale)||std::abs(layer.heightScale)>16||!std::isfinite(layer.roughnessScale)||layer.roughnessScale<0||layer.roughnessScale>4||
        !math::finite(layer.tint)||std::any_of(layer.tint.begin(),layer.tint.end(),[](float value){return value<0;}))
        throw std::runtime_error("Blend layer requires Height Scale in -16-16, Roughness Scale in 0-4 and finite nonnegative Tint.");
    if(static_cast<unsigned>(layer.mapping.mode)>3||!math::finite(layer.mapping.scale)||!math::finite(layer.mapping.offset)||!math::finite(layer.mapping.rotation))
        throw std::runtime_error("Invalid blend layer UV mapping.");
}
inline void validateSceneBlendSettings(const SceneBlendSettings& settings) {
    if(settings.size()>MaximumModelMaterialSlots)throw std::runtime_error("Too many occurrence Blend settings.");
    std::unordered_map<std::uint32_t,std::unordered_set<std::string>> keys;
    for(const auto& value:settings) {
        if(value.slot>=MaximumModelMaterialSlots||value.material.empty()||value.material.id.size()>4096||
            !keys[value.slot].insert(value.material.id).second||
            std::none_of(value.layers.begin(),value.layers.end(),[](const auto& layer){return layer.has_value();}))
            throw std::runtime_error("Invalid or duplicate occurrence Blend settings binding.");
        for(const auto& layer:value.layers)if(layer)validateSceneBlendLayer(*layer);
    }
}
inline void setSceneBlendLayer(SceneBlendSettings& settings,std::uint32_t slot,const std::string& materialId,std::uint32_t layer,std::optional<MaterialBlendLayer> value) {
    if(slot>=MaximumModelMaterialSlots||materialId.empty()||materialId.size()>4096||layer>=4)throw std::runtime_error("Invalid occurrence Blend layer binding.");
    if(value)validateSceneBlendLayer(*value);
    auto* binding=findSceneBlendMaterialSettings(settings,slot,materialId);
    if(!binding) {
        if(!value)return;
        if(settings.size()>=MaximumModelMaterialSlots)throw std::runtime_error("Too many occurrence Blend settings.");
        settings.push_back({slot,AssetRef<MaterialAsset>{materialId},{}});binding=&settings.back();
    }
    binding->layers[layer]=std::move(value);
    std::erase_if(settings,[](const auto& entry){return std::none_of(entry.layers.begin(),entry.layers.end(),[](const auto& current){return current.has_value();});});
}
// Match original source slots, including dormant material guards. Ambiguous or
// removed slots cannot transfer settings onto a different receiver surface.
inline SceneBlendSettings remapSceneBlendSettings(const SceneBlendSettings& settings,const std::vector<std::string>& before,const std::vector<std::string>& after) {
    SceneBlendSettings result;
    if(before==after) {
        for(const auto& value:settings)if(value.slot<after.size())result.push_back(value);
        return result;
    }
    std::unordered_map<std::string,std::size_t> beforeCounts,afterCounts,afterSlots;
    for(const auto& id:before)++beforeCounts[id];
    for(std::size_t slot=0;slot<after.size();++slot){++afterCounts[after[slot]];afterSlots[after[slot]]=slot;}
    for(auto value:settings)if(value.slot<before.size()&&beforeCounts[before[value.slot]]==1&&afterCounts[before[value.slot]]==1) {
        value.slot=static_cast<std::uint32_t>(afterSlots.at(before[value.slot]));result.push_back(std::move(value));
    }
    return result;
}
}
