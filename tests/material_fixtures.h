#pragma once
#include "assets/MaterialAsset.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>

namespace material_test {
// Explicit legacy construction for compatibility fixtures. Native graph edits
// must never depend on mutating the derived surface transport.
inline void legacyFixture(crimson::MaterialAsset& material) {material.graph={};material.surface.graph.reset();}
inline nlohmann::json legacyMaterialJson(const crimson::MaterialAsset& material,int version=3) {
    using namespace crimson;using Json=nlohmann::json;const auto& m=material.surface;
    const auto sampler=[](const ModelSampler& s){return Json{{"wrapU",static_cast<int>(s.wrapU)},{"wrapV",static_cast<int>(s.wrapV)},
        {"minFilter",static_cast<int>(s.minFilter)},{"magFilter",static_cast<int>(s.magFilter)}};};
    const auto texture=[&](const ModelTexture& t){auto j=sampler({t.wrapU,t.wrapV,t.minFilter,t.magFilter});j["image"]=t.image.id;j["uv"]=t.texCoord;return j;};
    const auto blend=material.blend==MaterialBlend::Opaque&&m.alphaMask?MaterialBlend::Mask:material.blend;
    Json result{{"version",version},{"type","crimson-material"},{"id",material.id},{"name",material.name},{"originModel",material.originModel},
        {"baseColor",m.baseColor},{"roughness",m.roughness},{"metallic",m.metallic},{"normalScale",m.normalScale},
        {"occlusionStrength",m.occlusionStrength},{"emissive",m.emissive},{"alphaMask",blend==MaterialBlend::Mask},
        {"doubleSided",m.doubleSided},{"alphaCutoff",m.alphaCutoff},{"baseColorTexture",texture(m.baseColorTexture)},
        {"metallicRoughnessTexture",texture(m.metallicRoughnessTexture)},{"normalTexture",texture(m.normalTexture)},
        {"occlusionTexture",texture(m.occlusionTexture)},{"emissiveTexture",texture(m.emissiveTexture)}};
    if(version>=2){result["shading"]=material.shading==MaterialShading::StandardPbr?"standard-pbr":"unlit";
        result["blend"]=blend==MaterialBlend::Opaque?"opaque":blend==MaterialBlend::Mask?"mask":blend==MaterialBlend::Alpha?"alpha":"additive";}
    if(version>=3){result["uvScale"]=m.uvScale;result["uvOffset"]=m.uvOffset;result["uvRotation"]=m.uvRotation;
        result["uvScaleLinked"]=m.uvScaleLinked;result["triplanar"]=m.triplanar;result["triplanarWorld"]=m.triplanarWorld;
        result["sharedSampler"]=m.sharedSampler?sampler(*m.sharedSampler):Json(nullptr);}
    return result;
}
}
