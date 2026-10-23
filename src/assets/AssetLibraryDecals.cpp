#include "AssetLibrary.h"
#include "resources/TextureStreamer.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include <crimson/renderer/forward/DecalRenderTypes.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace crimson {
namespace {
std::string filename(const std::filesystem::path& path) {const auto text=path.stem().generic_u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
std::uint64_t mix(std::uint64_t hash,std::uint64_t value) {return (hash^value)*1099511628211ull;}
bool sameSource(const DecalAsset& a,const DecalAsset& b) {
    if(!(a==b)||bool(a.stagedMaterial)!=bool(b.stagedMaterial))return false;
    return !a.stagedMaterial||serializeMaterialAsset(*a.stagedMaterial)==serializeMaterialAsset(*b.stagedMaterial);
}
}
void AssetLibrary::refreshDecals() {
    std::erase_if(catalog_,[](const auto& entry){return entry.kind==AssetKind::Decal;});std::unordered_set<std::string> present;
    for(const auto& file:storedAssetPaths(projectRoot_))if(file.file.extension()==".crimson-decal"&&std::filesystem::is_regular_file(file.file)&&!references_.deleted(file.id)) {
        present.insert(file.id);auto& version=decals_[file.id];
        if(file.id!=decalDocumentId_||!version.definition)try {
            auto asset=std::make_shared<DecalAsset>(loadDecalAsset(projectRoot_,file.id));
            if(!references_.deleted(asset->material.id)&&asset->material.id!=materialDocumentId_)if(const auto staged=stagedDecalMaterial(projectRoot_,*asset))publishMaterial(staged);
            if(!version.definition||!sameSource(*version.definition,*asset)){version.definition=std::move(asset);version.revision=nextDecalRevision_++;}version.diagnostic.clear();
        }catch(const std::exception& error){version.diagnostic=error.what();}
        catalog_.push_back({file.id,version.definition?version.definition->name:filename(file.file),AssetKind::Decal});
    }
    std::erase_if(decals_,[&](const auto& entry){return !present.contains(entry.first)&&entry.first!=decalDocumentId_;});++catalogRevision_;
}
std::shared_ptr<const DecalAsset> AssetLibrary::findDecal(const std::string& id) const {
    try{const auto found=decals_.find(references_.resolveDecal(id));return found==decals_.end()?nullptr:found->second.definition;}catch(const std::exception&){return {};}
}
std::uint64_t AssetLibrary::decalRevision(const std::string& id) const {
    try{const auto found=decals_.find(references_.resolveDecal(id));return found==decals_.end()?0:found->second.revision;}catch(const std::exception&){return 0;}
}
std::string AssetLibrary::decalDiagnostic(const std::string& id) const {
    try{if(id.empty())return "No decal surface assigned.";const auto found=decals_.find(references_.resolveDecal(id));return found==decals_.end()?"Decal surface unavailable: "+id:found->second.diagnostic;}catch(const std::exception& error){return error.what();}
}
void AssetLibrary::publishDecal(std::shared_ptr<const DecalAsset> asset) {
    if(!asset)return;auto next=std::make_shared<DecalAsset>(*asset);canonicalizeDecalAsset(*next);
    if(next->id.empty()||references_.deleted(next->id))throw std::runtime_error("Unidentified or deleted decal cannot be published.");
    auto& version=decals_[next->id];bool changed=!version.definition||*version.definition!=*next;
    if(changed){version.definition=std::move(next);version.revision=nextDecalRevision_++;}version.diagnostic.clear();
    const auto item=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& entry){return entry.id==version.definition->id;});
    if(item==catalog_.end()){catalog_.push_back({version.definition->id,version.definition->name,AssetKind::Decal});changed=true;}
    else if(item->name!=version.definition->name){item->name=version.definition->name;changed=true;}
    if(changed)++catalogRevision_;
}
std::shared_ptr<const DecalAsset> AssetLibrary::createDecal(const std::string& name) {
    auto asset=std::make_shared<const DecalAsset>(createDecalAsset(projectRoot_,name));refreshMaterials();publishDecal(asset);noteAssetWrite(asset->id);select(asset->id);return asset;
}
std::shared_ptr<const DecalAsset> AssetLibrary::copyDecal(const std::string& id,const std::string& name) {
    const auto original=findDecal(id);if(!original)throw std::runtime_error("Decal surface is unavailable.");
    auto asset=std::make_shared<const DecalAsset>(saveDecalAssetAs(projectRoot_,*original,name));refreshMaterials();publishDecal(asset);noteAssetWrite(asset->id);select(asset->id);return asset;
}
std::shared_ptr<const PreparedDecalSurface> AssetLibrary::preparedDecalMaterial(const std::string& id,std::uint32_t edge,ResourcePriority priority) const {
    const auto asset=findDecal(id);if(!asset){auto failed=std::make_shared<PreparedDecalSurface>();failed->diagnostic=decalDiagnostic(id);return failed;}
    auto result=prepareDecalSurface(*asset,"asset:"+asset->id+":"+std::to_string(std::max(1u,edge)),decalRevision(id),edge,priority);
    const auto diagnostic=decalDiagnostic(id);if(!diagnostic.empty()){auto failed=std::make_shared<PreparedDecalSurface>(*result);failed->current=false;failed->diagnostic=diagnostic;return failed;}return result;
}
std::shared_ptr<const PreparedDecalSurface> AssetLibrary::preparedDecalMaterial(const DecalAsset& asset,std::uint64_t revision,std::uint32_t edge,ResourcePriority priority) const {
    return prepareDecalSurface(asset,"preview:"+asset.id+":"+std::to_string(std::max(1u,edge)),revision,edge,priority);
}
std::shared_ptr<const PreparedDecalSurface> AssetLibrary::preparedDecalMaterial(const MaterialAsset& material,std::uint64_t revision,std::uint32_t edge,ResourcePriority priority) const {
    return prepareDecalGraph(material,"material-preview:"+material.id+":"+std::to_string(std::max(1u,edge)),revision,edge,priority);
}
std::shared_ptr<const PreparedDecalSurface> AssetLibrary::prepareDecalSurface(const DecalAsset& asset,const std::string& key,std::uint64_t revision,std::uint32_t edge,ResourcePriority priority) const {
    try {
        validateDecalAsset(asset);const auto id=references_.resolveDecalMaterial(asset.material.id);
        if(id.empty())throw std::runtime_error("No decal material assigned.");
        auto material=findMaterial(id);
        if(!material&&id==asset.material.id)material=stagedDecalMaterial(projectRoot_,asset);
        if(!material)throw std::runtime_error("Decal material unavailable: "+id);
        return prepareDecalGraph(*material,key,mix(revision,materialRevision(id)),edge,priority);
    }catch(const std::exception& error){auto result=std::make_shared<PreparedDecalSurface>();result->diagnostic=error.what();return result;}
}
std::shared_ptr<const PreparedDecalSurface> AssetLibrary::prepareDecalGraph(const MaterialAsset& source,const std::string& key,std::uint64_t revision,std::uint32_t edge,ResourcePriority priority) const {
    const auto now=std::chrono::steady_clock::now();decalMaterialInterest_[key]=now;const auto grace=resources_?resources_->config().idleGrace:std::chrono::seconds(5);
    std::erase_if(preparedDecalMaterials_,[&](const auto& entry){const auto found=decalMaterialInterest_.find(entry.first);return entry.first!=key&&(found==decalMaterialInterest_.end()||now-found->second>grace);});
    std::erase_if(decalMaterialInterest_,[&](const auto& entry){return entry.first!=key&&now-entry.second>grace;});
    const auto previous=preparedDecalMaterials_.find(key);auto result=std::make_shared<PreparedDecalSurface>();
    try {
        if(!decalMaterialCompatible(source))throw std::runtime_error("Assigned material must use Decal shading.");
        if(!graphPrograms_)throw std::runtime_error("Decal material shader owner is unavailable.");
        // Canonical committed materials retain an immutable graph pointer. Draft
        // material editors create a new pointer for each transactional revision.
        auto graph=source.surface.graph;
        if(!graph)graph=std::make_shared<const MaterialGraph>(source.graph);
        const auto bindings=graphMaterialBindings(graph,std::max(1u,edge),priority,false);
        if(bindings.graphPending||!bindings.graph||!bindings.complete||!bindings.sourceReady)
            throw std::runtime_error(bindings.diagnostic.empty()?"Preparing decal material...":bindings.diagnostic);
        if(bindings.graph->shading!=MaterialShading::Decal||!bgfx::isValid(bindings.graph->program(MaterialGraphPass::Decal)))throw std::runtime_error("Decal material program unavailable.");
        auto material=std::make_shared<PreparedDecalMaterial>();material->graph=bindings.graph;
        auto signature=mix(bindings.graph->signature,revision);for(const auto c:source.id)signature=mix(signature,static_cast<unsigned char>(c));
        material->fingerprint=signature;result->render=std::move(material);result->ready=result->current=true;result->signature=signature;result->sourceFingerprint=signature;
        for(const auto& binding:bindings.graph->sources)if(binding.slot<result->sourceIds.size()) {
            result->sourceIds[binding.slot]=binding.image.id;result->sourceFingerprints[binding.slot]=bindings.graph->sourceFingerprints[binding.slot];
        }
        preparedDecalMaterials_[key]=result;
    }catch(const std::exception& error){if(previous!=preparedDecalMaterials_.end()&&previous->second->ready)*result=*previous->second;result->current=false;result->diagnostic=error.what();}
    return result;
}
}
