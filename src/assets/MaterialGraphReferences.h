#pragma once
#include "MaterialGraph.h"
#include <algorithm>
#include <array>

namespace crimson {
// Mesh UV requirements follow runtime evaluation, not the private UV0 lattice
// of a cached image. Share this rule across publication and deletion recovery.
inline std::uint8_t materialGraphRequiredUvs(const MaterialGraph& graph) {
    const auto& output=materialOutputNode(graph).output;
    std::uint8_t required{};
    std::array<MaterialNodeId,MaterialGraph::MaximumNodes> visited{};
    std::size_t count{};
    const auto demandMapping=[&](const MaterialUvMapping& mapping) {
        if(mapping.mode==MaterialUvMode::UV0)required|=1;
        else if(mapping.mode==MaterialUvMode::UV1)required|=2;
    };
    const auto visit=[&](auto&& self,MaterialNodeId id)->void {
        if(std::find(visited.begin(),visited.begin()+count,id)!=visited.begin()+count)return;
        const auto* node=findMaterialNode(graph,id);if(!node||count>=visited.size())return;
        visited[count++]=id;
        if(materialNodeCachedImage(node->kind)) {
            demandMapping(node->kind==MaterialNodeKind::ImageTransform?materialTextureMapping(graph,id):MaterialUvMapping{});
            return;
        }
        if(node->kind==MaterialNodeKind::Noise||node->kind==MaterialNodeKind::RadialGradient) {
            const auto* mapping=materialInputLink(graph,id,MaterialPin::UV);
            demandMapping(mapping?resolveMaterialUvMapping(graph,mapping->fromNode):MaterialUvMapping{});
        } else if(node->kind==MaterialNodeKind::Grunge||node->kind==MaterialNodeKind::Texture2D||
            (node->kind==MaterialNodeKind::ImageInput&&node->atomic.enabled&&!node->texture.image.empty()))
            demandMapping(materialTextureMapping(graph,id));
        for(const auto& pin:materialNodeInputs(graph,*node,output.shading,output.blend))
            if(const auto* link=materialInputLink(graph,id,pin.pin))self(self,link->fromNode);
    };
    visit(visit,materialOutputNode(graph).id);
    return required;
}
// Authored dependency policy visits all nodes; residency visits only active ones.
template<class Visitor> void visitMaterialTextures(const ModelMaterial& material,bool activeOnly,Visitor&& visitor) {
    if(material.graph) {
        const auto& graph=*material.graph;
        std::vector<MaterialNodeId> active;
        if(activeOnly){const auto& output=materialOutputNode(graph).output;active=materialGraphOrder(graph,output.shading,output.blend);}
        for(const auto& node:graph.nodes)if((node.kind==MaterialNodeKind::Texture2D||node.kind==MaterialNodeKind::ImageInput) && !node.texture.image.empty() &&
            (!activeOnly||((node.kind!=MaterialNodeKind::ImageInput||node.atomic.enabled)&&std::find(active.begin(),active.end(),node.id)!=active.end())))visitor(node.texture.image.id,node.texture.role);
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
