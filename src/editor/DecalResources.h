#pragma once
#include "decals/DecalModelSurface.h"
#include "resources/ResourceManager.h"
#include "scene/SceneExtraction.h"
#include <crimson/renderer/forward/DecalRenderTypes.h>
#include <unordered_map>
#include <optional>
namespace crimson {
class AssetLibrary;
struct DecalSourceResult {std::shared_ptr<const decals::ModelSurface> surface;bool current{};uint64_t revision{},fingerprint{};std::string diagnostic;bool failed{};};
struct DecalPlacementResult {SceneDecalSeed seed;Mat4 frame{};bool ready{},failed{};std::string diagnostic;};
struct DecalPreparedScene {std::vector<RenderDecal> decals;std::vector<EntityId> receivers;bool ready{true},refined{true};std::string status;};
class DecalResources {
public:
    DecalResources(AssetLibrary&,ResourceManager&);
    DecalSourceResult modelSurface(const ExtractedModel&,ResourcePriority=ResourcePriority::Visible);
    DecalPlacementResult resolvePlacement(const Scene&,uint64_t entity,uint32_t draw,uint32_t primitive,uint64_t resourceRevision,Vec3 visibleWorldPoint);
    DecalPreparedScene prepare(const Scene&,const ExtractedScene&,ResourcePriority=ResourcePriority::Visible);
    DecalPreparedScene prepareTransient(const Scene&,const ExtractedScene&,const SceneDecal&,const Mat4&,uint64_t transientId=UINT64_MAX,ResourcePriority=ResourcePriority::Placement);
    std::string status(EntityId)const;
    std::shared_ptr<const DecalPatchData> patch(EntityId)const;
    std::optional<Mat4> surfaceFrame(EntityId)const;
    void collect();
    void clear();
private:
    struct SourceEntry {ResourceHandle<decals::ModelSurface> resource;uint64_t revision{},fingerprint{};std::chrono::steady_clock::time_point used;bool repairRequested{};};
    struct PatchEntry {ResourceHandle<decals::DecalPatch> resource;uint64_t signature{},publishedRevision{},publishedBindings{};std::shared_ptr<const DecalPatchData> render;std::optional<Mat4> surfaceFrame;std::chrono::steady_clock::time_point used;std::string status;bool current{};};
    AssetLibrary&assets_;ResourceManager&resources_;
    std::unordered_map<std::string,SourceEntry>sources_;
    std::unordered_map<uint64_t,PatchEntry>patches_;
    void append(DecalPreparedScene&,const Scene&,const ExtractedScene&,const ExtractedDecal&,ResourcePriority);
};
}
