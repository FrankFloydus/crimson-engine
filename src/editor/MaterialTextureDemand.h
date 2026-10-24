#pragma once

#include "assets/MaterialGraph.h"
#include <array>
#include <algorithm>
#include <span>
#include <vector>

namespace crimson {
struct MaterialRuntimeTextureMapping {
    MaterialNodeId node{};
    MaterialUvMapping mapping;
};

// Streaming demand follows sampled runtime images. A materialized image's
// private source mappings describe its bake, not an additional runtime sampler.
// Keep the direct consumer of a shared private source independently visible.
inline std::vector<MaterialRuntimeTextureMapping> materialRuntimeTextureMappings(
    const MaterialGraph& graph,MaterialEvaluationContext context={}) {
    const auto& output=materialOutputNode(graph);
    std::vector<MaterialRuntimeTextureMapping> result;
    result.reserve(MaterialGraph::MaximumTextureBindings);
    std::array<MaterialNodeId,MaterialGraph::MaximumNodes> inlineVisited{};
    std::vector<MaterialNodeId> expandedVisited;
    std::span<MaterialNodeId> visited{inlineVisited};
    // Resolving four layers can legitimately exceed the authored graph limit.
    // Ordinary materials keep the allocation-free stack storage.
    if(graph.nodes.size()>inlineVisited.size()){expandedVisited.resize(graph.nodes.size());visited=std::span<MaterialNodeId>{expandedVisited};}
    std::size_t visitedCount{};
    const auto walk=[&](auto&& self,MaterialNodeId id)->void {
        if(std::find(visited.begin(),visited.begin()+visitedCount,id)!=visited.begin()+visitedCount)return;
        const auto* node=findMaterialNode(graph,id);if(!node)return;
        if(visitedCount>=visited.size())return; // Invalid graphs are diagnosed by their owning document.
        visited[visitedCount++]=id;
        if(materialNodeCachedImage(node->kind)) {
            result.push_back({id,node->layerMapping||node->kind==MaterialNodeKind::ImageTransform?
                materialTextureMapping(graph,id,context):MaterialUvMapping{}});
            return;
        }
        const bool bitmap=node->kind==MaterialNodeKind::Texture2D&&!node->texture.image.empty();
        const bool overrideImage=node->kind==MaterialNodeKind::ImageInput&&node->atomic.enabled&&!node->texture.image.empty();
        if(bitmap||overrideImage||node->kind==MaterialNodeKind::Grunge) {
            auto mapping=materialTextureMapping(graph,id,context);
            if(node->kind==MaterialNodeKind::Grunge) {
                for(unsigned axis=0;axis<3;++axis) {
                    mapping.scale[axis]*=static_cast<float>(node->grunge.tiling);
                    mapping.offset[axis]*=static_cast<float>(node->grunge.tiling);
                }
            }
            result.push_back({id,mapping});return;
        }
        for(const auto& input:materialNodeInputs(graph,*node,output.output.shading,output.output.blend))
            if(const auto* link=materialInputLink(graph,id,input.pin))self(self,link->fromNode);
    };
    walk(walk,output.id);return result;
}
} // namespace crimson
