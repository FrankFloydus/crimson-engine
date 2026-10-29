#pragma once

#include "scene/SceneBlendSettings.h"
#include <memory>
#include <unordered_map>

namespace crimson {
// Apply occurrence appearance before source expansion. Cached image origins,
// referenced material identities and every authored map/link remain intact.
inline std::shared_ptr<const MaterialGraph> modelBlendGraph(const std::shared_ptr<const MaterialGraph>& source,
    const std::array<std::optional<MaterialBlendLayer>,4>& layers) {
    if(!source||source->empty()||materialOutputNode(*source).output.shading!=MaterialShading::BlendPbr)return source;
    constexpr std::array pins{MaterialPin::Base,MaterialPin::Red,MaterialPin::Green,MaterialPin::Blue};
    const auto output=materialOutputNode(*source).id;
    std::shared_ptr<MaterialGraph> result;
    for(std::size_t layer=0;layer<pins.size();++layer) {
        if(!layers[layer])continue;
        const auto* input=materialInputLink(*source,output,pins[layer]);
        const auto* reference=input?findMaterialNode(*source,input->fromNode):nullptr;
        if(!reference||(reference->kind!=MaterialNodeKind::MaterialReference&&reference->kind!=MaterialNodeKind::ResolvedSurface))continue;
        const auto uses=std::count_if(source->links.begin(),source->links.end(),[&](const auto& link) {
            return link.fromNode==reference->id&&link.toNode==output;
        });
        if(uses==1&&reference->blendLayer==*layers[layer])continue;
        validateSceneBlendLayer(*layers[layer]);
        if(!result)result=std::make_shared<MaterialGraph>(*source);
        auto selected=reference->id;
        if(uses>1) {
            // One reference can feed several layer pins. Split only this pin,
            // retaining its parent-authored Height branch and deterministic IDs.
            auto copy=*reference;copy.id=result->nextNodeId++;copy.blendLayer=*layers[layer];
            const auto id=copy.id;selected=id;result->nodes.push_back(std::move(copy));
            for(const auto& link:source->links)if(link.toNode==reference->id) {
                auto incoming=link;incoming.id=result->nextLinkId++;incoming.toNode=id;
                result->links.push_back(std::move(incoming));
            }
            for(auto& link:result->links)if(link.id==input->id){link.fromNode=id;break;}
        } else findMaterialNode(*result,reference->id)->blendLayer=*layers[layer];
        if(reference->kind==MaterialNodeKind::ResolvedSurface&&layers[layer]->mappingOverride) {
            // Inline layers already contain their complete surface. Clone only
            // its runtime expression tree so another layer cannot inherit this
            // occurrence's placement. Cached pixels retain the original recipe.
            std::unordered_map<MaterialNodeId,MaterialNodeId> copies;
            const auto clone=[&](auto&& self,MaterialNodeId original)->MaterialNodeId {
                if(const auto found=copies.find(original);found!=copies.end())return found->second;
                const auto* authored=findMaterialNode(*source,original);if(!authored)return original;
                auto copy=*authored;copy.id=result->nextNodeId++;
                const auto id=copy.id;copies.emplace(original,id);
                const bool cached=materialNodeCachedImage(copy.kind);
                const bool sampled=cached||copy.kind==MaterialNodeKind::Texture2D||copy.kind==MaterialNodeKind::Grunge||
                    copy.kind==MaterialNodeKind::Noise||copy.kind==MaterialNodeKind::RadialGradient||copy.kind==MaterialNodeKind::ImageInput;
                if(cached) {
                    copy.sourceGraph=authored->sourceGraph?authored->sourceGraph:source;
                    copy.sourceNode=authored->sourceGraph?authored->sourceNode:original;
                    copy.left.type=materialOutputType(*source,original,MaterialPin::Value);
                    copy.kind=MaterialNodeKind::ImageTransform;
                }
                if(sampled){copy.uv=layers[layer]->mapping;copy.layerMapping.reset();}
                result->nodes.push_back(std::move(copy));
                if(!cached)for(const auto& link:source->links)if(link.toNode==original&&!(sampled&&link.toPin==MaterialPin::UV)) {
                    auto incoming=link;incoming.id=result->nextLinkId++;incoming.fromNode=self(self,link.fromNode);incoming.toNode=id;
                    result->links.push_back(std::move(incoming));
                }
                return id;
            };
            std::vector<MaterialLink> incoming;
            for(const auto& link:result->links)if(link.toNode==selected)incoming.push_back(link);
            for(const auto& link:incoming) {
                const auto replacement=clone(clone,link.fromNode);
                for(auto& target:result->links)if(target.id==link.id){target.fromNode=replacement;break;}
            }
        }
    }
    return result?std::shared_ptr<const MaterialGraph>{std::move(result)}:source;
}
}
