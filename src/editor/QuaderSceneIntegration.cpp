#include "QuaderSceneIntegration.h"
namespace crimson {
bool applyQuaderSceneImport(SceneDocument& document,EntityId owner,const QuaderLink& requested,const QuaderImportResult& result) {
    std::vector<EntityRecord> records;
    const auto append=[&](std::string key,std::string name,const Mat4& local,EntityId parent=NoEntity)->EntityRecord& {
        EntityRecord record;record.id=records.size()+1;record.name=name.empty()?"Quader object":std::move(name);
        record.parent=parent;record.order=static_cast<std::uint32_t>(records.size());record.local=local;
        record.quaderGenerated=QuaderGenerated{owner,std::move(key)};records.push_back(std::move(record));return records.back();
    };
    for(const auto& mesh:result.meshes) {
        auto& visual=append(mesh.sourceKey,mesh.name,mesh.local);const auto parent=visual.id;
        if(mesh.visible && !mesh.modelId.empty())visual.model=AssetRef<ModelAsset>{mesh.modelId};
        if(!mesh.collisionModelId.empty())append(mesh.sourceKey+"/collision","Mesh Collider",Mat4{},parent).meshCollider=SceneMeshCollider{AssetRef<ModelAsset>{mesh.collisionModelId}};
    }
    if(requested.syncLights)for(const auto& light:result.lights) {
        auto& record=append(light.sourceKey,light.name,light.local);
        if(light.point) {
            record.point=ScenePointLight{light.color,light.intensity,light.visible,light.radius,light.castShadows};
        } else {
            record.directional=SceneDirectionalLight{light.color,light.intensity,light.visible,light.castShadows};
        }
    }
    auto link=requested;link.projectId=result.projectId;link.sceneId=result.sceneId;link.saveKey=result.sourceKey;
    return document.replaceQuaderImport(owner,link,records);
}
}
