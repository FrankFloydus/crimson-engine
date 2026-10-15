#pragma once
#include "crimson-renderer/Renderer.h"
#include "resources/CacheIO.h"

namespace crimson {
// Prepared graph packets already fingerprint their complete program, uniforms,
// sampler state and immutable texture versions. Frame preparation must not
// serialize and validate the authored graph again for every view and caster.
inline std::uint64_t preparedModelSignature(const ModelAsset& asset,std::uint64_t revision,
    std::span<const ModelMaterial> materials,std::span<const ModelMaterialTextures> textures) {
    using streaming::detail::hash;
    auto result=streaming::detail::HashSeed;hash(result,revision);hash(result,asset.id.data(),asset.id.size());
    for(const auto& material:materials) {
        // These also describe the explicit fallback while a graph is compiling.
        hash(result,material.baseColor);hash(result,material.metallic);hash(result,material.roughness);hash(result,material.specular);
        hash(result,material.normalScale);hash(result,material.occlusionStrength);hash(result,material.emissive);
        hash(result,material.alphaMask);hash(result,material.alphaCutoff);hash(result,material.doubleSided);hash(result,material.receiveFog);
        hash(result,material.uvScale);hash(result,material.uvOffset);hash(result,material.uvRotation);hash(result,material.triplanar);hash(result,material.triplanarWorld);
        hash(result,material.sharedSampler.has_value());
        if(material.sharedSampler) {
            hash(result,material.sharedSampler->wrapU);hash(result,material.sharedSampler->wrapV);
            hash(result,material.sharedSampler->minFilter);hash(result,material.sharedSampler->magFilter);
        }
        for(const auto* slot:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
            hash(result,slot->image.id.data(),slot->image.id.size());hash(result,slot->texCoord);hash(result,slot->wrapU);hash(result,slot->wrapV);
            hash(result,slot->minFilter);hash(result,slot->magFilter);
        }
    }
    for(const auto& binding:textures) {
        hash(result,binding.graphPending);
        if(binding.graph) {
            hash(result,binding.graph->signature);hash(result,binding.graph->shading);hash(result,binding.graph->blend);
            hash(result,binding.graph->nodePreview);hash(result,binding.graph->dynamicCoverage);hash(result,binding.graph->receiveFog);
        }
        for(std::size_t slot=0;slot<binding.handles.size();++slot) {
            hash(result,binding.handles[slot].idx);hash(result,binding.flags[slot]);
            hash(result,binding.signatures[slot]);hash(result,binding.sourceFingerprints[slot]);
        }
    }
    return result;
}
} // namespace crimson
