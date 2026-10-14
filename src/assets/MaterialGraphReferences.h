#pragma once
#include "MaterialGraph.h"
#include <algorithm>

namespace crimson {
// Authored dependency policy visits all nodes; residency visits only active ones.
template<class Visitor> void visitMaterialTextures(const ModelMaterial& material,bool activeOnly,Visitor&& visitor) {
    if(material.graph) {
        const auto& graph=*material.graph;
        std::vector<MaterialNodeId> active;
        if(activeOnly){const auto& output=materialOutputNode(graph).output;active=materialGraphOrder(graph,output.shading,output.blend);}
        for(const auto& node:graph.nodes)if(node.kind==MaterialNodeKind::Texture2D && !node.texture.image.empty() &&
            (!activeOnly||std::find(active.begin(),active.end(),node.id)!=active.end()))visitor(node.texture.image.id,node.texture.role);
        return;
    }
    for(const auto* texture:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture})
        if(!texture->image.empty())visitor(texture->image.id,texture==&material.normalTexture?MaterialTextureRole::Normal:
            texture==&material.baseColorTexture||texture==&material.emissiveTexture?MaterialTextureRole::Color:MaterialTextureRole::Data);
}
inline std::uint64_t materialGraphRenderHash(const ModelMaterial& material) {
    if(!material.graph)return 0;
    const auto& graph=*material.graph;const auto& output=materialOutputNode(graph).output;
    return materialGraphValueHash(graph,output.shading,output.blend)^(materialGraphTopologyHash(graph,output.shading,output.blend)*1099511628211ull);
}
}
