#include "QuaderSceneIntegration.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
namespace crimson {
bool applyQuaderSceneImport(SceneDocument& document,EntityId owner,const QuaderLink& requested,const QuaderImportResult& result,
    const ModelMaterialSourceQuery& modelMaterials) {
    std::vector<EntityRecord> records;
    std::unordered_map<std::string,EntityId> sourceIds;
    std::vector<std::pair<std::size_t,std::string>> parents;
    const auto append=[&](std::string key,std::string name,const Mat4& local,EntityId parent=NoEntity)->EntityRecord& {
        EntityRecord record;record.id=records.size()+1;record.name=name.empty()?"Quader object":std::move(name);
        record.parent=parent;record.order=static_cast<std::uint32_t>(records.size());record.local=local;
        record.quaderGenerated=QuaderGenerated{owner,std::move(key)};records.push_back(std::move(record));return records.back();
    };
    const auto hierarchy=[&](EntityRecord& record,const std::string& key,const std::string& parent,std::uint32_t order) {
        sourceIds.emplace(key,record.id);record.order=order;
        if(!parent.empty())parents.emplace_back(records.size()-1,parent);
    };
    for(const auto& group:result.groups) {
        auto& record=append(group.sourceKey,group.name,group.local);record.visible=group.visible;
        hierarchy(record,group.sourceKey,group.parentKey,group.order);
    }
    for(const auto& mesh:result.meshes) {
        auto& visual=append(mesh.sourceKey,mesh.name,mesh.local);const auto parent=visual.id;
        hierarchy(visual,mesh.sourceKey,mesh.parentKey,mesh.order);
        visual.visible=mesh.visible;
        if(!mesh.modelId.empty())visual.model=AssetRef<ModelAsset>{mesh.modelId};
        if(mesh.collision) {
            auto& collision=append(mesh.sourceKey+"/collision","Mesh Collider",Mat4{},parent);collision.collider.emplace();
            collision.collider->shape=ColliderShape::Trimesh;collision.collider->mesh=mesh.collision;
        }
    }
    if(requested.syncLights)for(const auto& light:result.lights) {
        auto& record=append(light.sourceKey,light.name,light.local);
        hierarchy(record,light.sourceKey,light.parentKey,light.order);
        if(light.point) {
            record.point=ScenePointLight{light.color,light.intensity,light.visible,light.radius,light.castShadows};
        } else {
            record.directional=SceneDirectionalLight{light.color,light.intensity,light.visible,light.castShadows};
        }
    }
    for(const auto& [index,key]:parents) {
        const auto found=sourceIds.find(key);
        // SceneDocument validates the complete replacement before committing;
        // unresolved parents must fail there rather than flattening the tree.
        records[index].parent=found==sourceIds.end()?std::numeric_limits<EntityId>::max():found->second;
    }
    auto link=requested;link.projectId=result.projectId;link.sceneId=result.sceneId;link.saveKey=result.sourceKey;
    const auto sources=[&](const std::string& id)->std::optional<std::vector<std::string>> {
        for(const auto& model:result.preparedModels)if(model && model->id==id)return model->materialIds;
        return modelMaterials?modelMaterials(id):std::nullopt;
    };
    return document.replaceQuaderImport(owner,link,records,[&](const EntityRecord& previous,const EntityRecord& incoming) {
        if(previous.modelMaterials.empty() || previous.model==incoming.model)return previous.modelMaterials;
        const auto before=sources(previous.model->id),after=sources(incoming.model->id);
        if(!before || !after)throw std::runtime_error("Quader sync is waiting for source material slots to preserve assignments on "+incoming.name+".");
        if(*before==*after) {
            auto assignments=previous.modelMaterials;
            // A changed package can retain the same real slots while dropping
            // an old unused fallback. Preserve numbered assignments to faces.
            if(assignments.size()>after->size())assignments.resize(after->size());
            trimModelMaterialOverrides(assignments);return assignments;
        }
        std::unordered_map<std::string,std::size_t> beforeCounts,afterCounts,afterSlots;
        for(const auto& id:*before)++beforeCounts[id];
        for(std::size_t index=0;index<after->size();++index){++afterCounts[(*after)[index]];afterSlots[(*after)[index]]=index;}
        ModelMaterialOverrides remapped;
        for(std::size_t index=0;index<previous.modelMaterials.size();++index)if(!previous.modelMaterials[index].empty()) {
            // Removed or ambiguous source slots no longer own an instance
            // assignment. Drop only those overrides and import the new layout.
            if(index>=before->size() || beforeCounts[(*before)[index]]!=1 || afterCounts[(*before)[index]]!=1)
                continue;
            setModelMaterialOverride(remapped,afterSlots.at((*before)[index]),previous.modelMaterials[index]);
        }
        return remapped;
    });
}
}
