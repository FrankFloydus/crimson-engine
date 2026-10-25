#pragma once

#include "scene/SceneBlendSettings.h"
#include <memory>

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
        if(!reference||reference->kind!=MaterialNodeKind::MaterialReference)continue;
        const auto uses=std::count_if(source->links.begin(),source->links.end(),[&](const auto& link) {
            return link.fromNode==reference->id&&link.toNode==output;
        });
        if(uses==1&&reference->blendLayer==*layers[layer])continue;
        validateSceneBlendLayer(*layers[layer]);
        if(!result)result=std::make_shared<MaterialGraph>(*source);
        if(uses>1) {
            // One reference can feed several layer pins. Split only this pin,
            // retaining its parent-authored Height branch and deterministic IDs.
            auto copy=*reference;copy.id=result->nextNodeId++;copy.blendLayer=*layers[layer];
            const auto id=copy.id;result->nodes.push_back(std::move(copy));
            for(const auto& link:source->links)if(link.toNode==reference->id) {
                auto incoming=link;incoming.id=result->nextLinkId++;incoming.toNode=id;
                result->links.push_back(std::move(incoming));
            }
            for(auto& link:result->links)if(link.id==input->id){link.fromNode=id;break;}
        } else findMaterialNode(*result,reference->id)->blendLayer=*layers[layer];
    }
    return result?std::shared_ptr<const MaterialGraph>{std::move(result)}:source;
}
}
