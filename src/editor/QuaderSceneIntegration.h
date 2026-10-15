#pragma once
#include "quader/QuaderBridge.h"
#include "scene/SceneDocument.h"
namespace crimson {
bool applyQuaderSceneImport(SceneDocument&,EntityId,const QuaderLink&,const QuaderImportResult&);
}
