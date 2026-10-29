#pragma once
#include "scene/SceneDocument.h"
#include "scene/SceneBlendSettings.h"
#include <algorithm>

namespace crimson {
struct VertexBlendLayerTarget {
    EntityId entity{};
    std::uint32_t slot{},layer{};
    std::string material;
    bool operator==(const VertexBlendLayerTarget&) const = default;
};
// A layer-property gesture owns one scene command, independently of paint
// strokes. Never finish or cancel a transaction installed by another owner.
class VertexBlendLayerEdit {
public:
    bool active() const {return owner_!=nullptr;}
    bool valid(const SceneDocument& document) const {
        return owner_==&document&&generation_==document.documentGeneration()&&
            transaction_==document.transactionId()&&document.transactionActive();
    }
    const VertexBlendLayerTarget& target() const {return target_;}
    bool begin(SceneDocument& document,const VertexBlendLayerTarget& target,const char* label) {
        if(active()||document.transient()||target.slot>=MaximumModelMaterialSlots||target.layer>=4||target.material.empty())return false;
        const auto record=document.scene().find(target.entity);
        if(!record||!record->model||!document.begin(label))return false;
        owner_=&document;generation_=document.documentGeneration();transaction_=document.transactionId();target_=target;return true;
    }
    bool update(SceneDocument& document,std::optional<MaterialBlendLayer> value) {
        if(!valid(document))return false;
        const auto target=target_;
        return document.update([&](auto& records,auto&) {
            const auto record=std::find_if(records.begin(),records.end(),[&](const auto& r){return r.id==target.entity;});
            if(record!=records.end())setSceneBlendLayer(record->blendSettings,target.slot,target.material,target.layer,value);
        });
    }
    bool finish(SceneDocument& document) {
        const bool owned=valid(document);owner_=nullptr;
        if(!owned)return false;
        if(document.commit())return true;
        document.cancel();return false;
    }
    void cancel(SceneDocument& document) {
        const bool owned=valid(document);owner_=nullptr;if(owned)document.cancel();
    }
private:
    SceneDocument* owner_{};
    std::uint64_t generation_{},transaction_{};
    VertexBlendLayerTarget target_;
};
}
