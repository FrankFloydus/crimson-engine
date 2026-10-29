#pragma once
#include "quader/QuaderBridge.h"
#include "scene/SceneDocument.h"
namespace crimson {
using ModelMaterialSourceQuery = std::function<std::optional<std::vector<std::string>>(const std::string&)>;
bool applyQuaderSceneImport(SceneDocument&,EntityId,const QuaderLink&,const QuaderImportResult&,
    const ModelMaterialSourceQuery& modelMaterials={});
}
