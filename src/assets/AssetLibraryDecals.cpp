#include "AssetLibrary.h"
#include "resources/TextureStreamer.h"
#include <crimson/renderer/forward/DecalRenderTypes.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace crimson {
namespace {
std::string filename(const std::filesystem::path& path) {const auto text=path.stem().generic_u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
std::uint64_t mix(std::uint64_t hash,std::uint64_t value) {return (hash^value)*1099511628211ull;}
std::string decalMap(const AssetReferenceManager& references,const std::string& id) {
    const auto resolved=references.resolve(id);
    if(references.deleted(id)&&(resolved.empty()||resolved==GrayTextureId||resolved==NeutralNormalTextureId))return {};
    return resolved;
}
}
void AssetLibrary::refreshDecals() {
    std::erase_if(catalog_,[](const auto& entry){return entry.kind==AssetKind::Decal;});std::unordered_set<std::string> present;
    for(const auto& file:storedAssetPaths(projectRoot_))if(file.file.extension()==".crimson-decal"&&std::filesystem::is_regular_file(file.file)&&!references_.deleted(file.id)) {
        present.insert(file.id);auto& version=decals_[file.id];
        if(file.id!=decalDocumentId_||!version.definition)try {
            auto asset=std::make_shared<DecalAsset>(loadDecalAsset(projectRoot_,file.id));
            if(!version.definition||*version.definition!=*asset){version.definition=std::move(asset);version.revision=nextDecalRevision_++;}version.diagnostic.clear();
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
    auto asset=std::make_shared<const DecalAsset>(createDecalAsset(projectRoot_,name));publishDecal(asset);noteAssetWrite(asset->id);select(asset->id);return asset;
}
std::shared_ptr<const DecalAsset> AssetLibrary::copyDecal(const std::string& id,const std::string& name) {
    const auto original=findDecal(id);if(!original)throw std::runtime_error("Decal surface is unavailable.");
    auto asset=std::make_shared<const DecalAsset>(saveDecalAssetAs(projectRoot_,*original,name));publishDecal(asset);noteAssetWrite(asset->id);select(asset->id);return asset;
}
std::shared_ptr<const PreparedDecalSurface> AssetLibrary::preparedDecalMaterial(const std::string& id,std::uint32_t edge,ResourcePriority priority) const {
    const auto asset=findDecal(id);if(!asset){auto failed=std::make_shared<PreparedDecalSurface>();failed->diagnostic=decalDiagnostic(id);return failed;}
    auto result=prepareDecalSurface(*asset,"asset:"+asset->id+":"+std::to_string(std::max(1u,edge)),decalRevision(id),edge,priority);
    const auto diagnostic=decalDiagnostic(id);if(!diagnostic.empty()){auto failed=std::make_shared<PreparedDecalSurface>(*result);failed->current=false;failed->diagnostic=diagnostic;return failed;}return result;
}
std::shared_ptr<const PreparedDecalSurface> AssetLibrary::preparedDecalMaterial(const DecalAsset& asset,std::uint64_t revision,std::uint32_t edge,ResourcePriority priority) const {
    return prepareDecalSurface(asset,"preview:"+asset.id+":"+std::to_string(std::max(1u,edge)),revision,edge,priority);
}
std::shared_ptr<const PreparedDecalSurface> AssetLibrary::prepareDecalSurface(const DecalAsset& source,const std::string& key,std::uint64_t revision,std::uint32_t edge,ResourcePriority priority) const {
    const auto now=std::chrono::steady_clock::now();decalMaterialInterest_[key]=now;const auto grace=resources_?resources_->config().idleGrace:std::chrono::seconds(5);
    std::erase_if(preparedDecalMaterials_,[&](const auto& entry){const auto found=decalMaterialInterest_.find(entry.first);return entry.first!=key&&(found==decalMaterialInterest_.end()||now-found->second>grace);});
    std::erase_if(decalMaterialInterest_,[&](const auto& entry){return entry.first!=key&&now-entry.second>grace;});
    const auto previous=preparedDecalMaterials_.find(key);auto result=std::make_shared<PreparedDecalSurface>();
    try {
        auto asset=source;canonicalizeDecalAsset(asset);
        rewriteDecalTextureReferences(asset,[&](const auto& id,bool){return decalMap(references_,id);});
        auto material=std::make_shared<PreparedDecalMaterial>();material->colorFactor=asset.color;material->colorStrength=asset.colorStrength;material->normalStrength=asset.normalStrength;
        material->roughnessStrength=asset.roughnessStrength;material->roughness=asset.roughness;material->coverageChannel=static_cast<std::uint8_t>(asset.coverageChannel);material->roughnessChannel=static_cast<std::uint8_t>(asset.roughnessChannel);
        material->colorEnabled=asset.colorEnabled&&asset.colorStrength>0;material->normalEnabled=asset.normalEnabled&&asset.normalStrength>0&&!asset.normalTexture.empty();material->roughnessEnabled=asset.roughnessEnabled&&asset.roughnessStrength>0;
        const bool active=material->colorEnabled||material->normalEnabled||material->roughnessEnabled;
        material->coverageSource=asset.coverageTexture.empty()?(asset.colorTexture.empty()?DecalCoverageSource::Constant:DecalCoverageSource::ColorAlpha):DecalCoverageSource::Texture;
        const std::array<std::string,4> maps{asset.colorTexture.id,asset.coverageTexture.id,asset.normalTexture.id,asset.roughnessTexture.id};
        const std::array<bool,4> needed{active&&!maps[0].empty()&&(material->colorEnabled||material->coverageSource==DecalCoverageSource::ColorAlpha),active&&material->coverageSource==DecalCoverageSource::Texture,material->normalEnabled,material->roughnessEnabled&&!maps[3].empty()};
        std::uint64_t signature=14695981039346656037ull,sourceFingerprint=signature;
        for(const auto byte:serializeDecalAsset(asset))signature=mix(signature,static_cast<unsigned char>(byte));signature=mix(signature,revision);bool ready=true;std::string diagnostic;
        for(std::size_t slot=0;slot<maps.size();++slot)if(needed[slot]) {
            if(!textureStreamer_)throw std::runtime_error("Decal texture streaming owner is unavailable.");
            const auto file=resolveAssetPath(projectRoot_,maps[slot]);auto ext=file.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(ext!=".png"&&ext!=".jpg"&&ext!=".jpeg"&&ext!=".bmp"&&ext!=".tga")throw std::runtime_error("Decal maps require ordinary image assets.");
            const auto role=slot==0?streaming::TextureRole::Color:slot==2?streaming::TextureRole::Normal:streaming::TextureRole::Data;
            const auto texture=textureStreamer_->request(maps[slot],file,role,true,std::max(1u,edge),priority);
            result->sourceIds[slot]=maps[slot];result->sourceFingerprints[slot]=texture.sourceFingerprint;
            material->textures[slot]=texture.handle;material->leases[slot]=texture.lease;material->samplerFlags[slot]=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_ANISOTROPIC|BGFX_SAMPLER_MAG_ANISOTROPIC;
            signature=mix(signature,texture.signature);sourceFingerprint=mix(sourceFingerprint,texture.sourceFingerprint);
            if(!texture.complete||!texture.sourceReady||!bgfx::isValid(texture.handle)){ready=false;if(diagnostic.empty())diagnostic=texture.diagnostic.empty()?"Preparing decal surface textures.":texture.diagnostic;}
        }
        if(!ready)throw std::runtime_error(diagnostic);
        material->fingerprint=signature;result->render=std::move(material);result->ready=result->current=true;result->signature=signature;result->sourceFingerprint=mix(sourceFingerprint,signature);
        preparedDecalMaterials_[key]=result;
    }catch(const std::exception& error){if(previous!=preparedDecalMaterials_.end()&&previous->second->ready)*result=*previous->second;result->current=false;result->diagnostic=error.what();}
    return result;
}
}
