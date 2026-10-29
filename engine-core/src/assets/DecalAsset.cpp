#include "DecalAsset.h"
#include "MaterialAsset.h"
#include "AssetTransaction.h"
#include "ContentStorage.h"
#include "resources/AssetId.h"
#include "resources/AssetRefJson.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
using Json=nlohmann::json;
constexpr std::size_t MaximumBytes=1024*1024;
std::string utf8(const std::filesystem::path& p){const auto s=p.generic_u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
void unit(float v,const char* label){if(!math::finite(v)||v<0||v>1)throw std::runtime_error(std::string(label)+" must be between zero and one.");}
MaterialPin channel(const Json& j){const auto s=j.get<std::string>();if(s=="r")return MaterialPin::R;if(s=="g")return MaterialPin::G;if(s=="b")return MaterialPin::B;if(s=="a")return MaterialPin::A;throw std::runtime_error("Invalid legacy decal channel.");}
std::shared_ptr<MaterialAsset> materialSeed(const std::string& id,const std::string& name) {
    auto m=std::make_shared<MaterialAsset>();m->id=derivedAssetId(id,"decal-material-v2");m->name=name+" Material";
    auto& o=addMaterialNode(m->graph,MaterialNodeKind::Output,{600,0}).output;
    o.shading=MaterialShading::Decal;o.baseColor={1,1,1};o.roughness=1;o.decalColorEnabled=true;o.decalNormalEnabled=o.decalRoughnessEnabled=false;return m;
}
std::shared_ptr<const MaterialAsset> legacyMaterial(const Json& j,const std::string& id,const std::string& name) {
    auto m=materialSeed(id,name);m->graph.nodes.reserve(16);const auto outputId=materialOutputNode(m->graph).id;
    const auto& c=j.at("color");const auto& n=j.at("normal");const auto& r=j.at("roughness");const auto& a=j.at("coverage");
    const auto image=[](const Json& v){return v.get<AssetRef<DecodedImage>>().id;};
    const auto ci=image(c.at("texture")),ni=image(n.at("texture")),ri=image(r.at("texture")),ai=image(a.at("texture"));
    const auto texture=[&](const std::string& imageId,MaterialTextureRole role,Vec2 position) {
        const auto node=addMaterialNode(m->graph,MaterialNodeKind::Texture2D,position).id;auto& t=findMaterialNode(m->graph,node)->texture;
        t.image.id=imageId;t.role=role;t.sampler.wrapU=t.sampler.wrapV=ModelWrap::Clamp;return node;
    };
    auto& o=materialOutputNode(m->graph).output;o.baseColor=c.at("factor").get<Vec3>();for(float v:o.baseColor)unit(v,"Legacy color");
    o.decalColorEnabled=c.at("enabled").get<bool>();o.decalNormalEnabled=n.at("enabled").get<bool>()&&!ni.empty();o.decalRoughnessEnabled=r.at("enabled").get<bool>();
    o.decalColorStrength=c.at("strength").get<float>();o.decalNormalStrength=n.at("strength").get<float>();o.decalRoughnessStrength=r.at("strength").get<float>();o.roughness=r.at("value").get<float>();
    unit(o.decalColorStrength,"Legacy color strength");unit(o.decalNormalStrength,"Legacy normal strength");unit(o.decalRoughnessStrength,"Legacy roughness strength");unit(o.roughness,"Legacy roughness");
    const auto factor=o.baseColor;MaterialNodeId colorNode{};
    if(!ci.empty()){colorNode=texture(ci,MaterialTextureRole::Color,{-500,-180});const auto product=addMaterialNode(m->graph,MaterialNodeKind::Multiply,{-120,-180}).id;
        findMaterialNode(m->graph,product)->right={MaterialValueType::Color,{factor[0],factor[1],factor[2],1}};
        connectMaterialNodes(m->graph,colorNode,MaterialPin::RGB,product,MaterialPin::Left);connectMaterialNodes(m->graph,product,MaterialPin::Value,outputId,MaterialPin::BaseColor);}
    if(!ai.empty()){const auto node=texture(ai,MaterialTextureRole::Data,{-500,40});connectMaterialNodes(m->graph,node,channel(a.at("channel")),outputId,MaterialPin::Opacity);}
    else if(colorNode)connectMaterialNodes(m->graph,colorNode,MaterialPin::A,outputId,MaterialPin::Opacity);
    if(!ni.empty()){const auto node=texture(ni,MaterialTextureRole::Normal,{-500,260});connectMaterialNodes(m->graph,node,MaterialPin::Normal,outputId,MaterialPin::Normal);}
    if(!ri.empty()){const auto node=texture(ri,MaterialTextureRole::Data,{-500,480});connectMaterialNodes(m->graph,node,channel(r.at("channel")),outputId,MaterialPin::Roughness);}
    (void)channel(a.at("channel"));(void)channel(r.at("channel"));canonicalizeMaterialAsset(*m);validateMaterialAsset(*m);return m;
}
std::string unusedName(const std::filesystem::path& root,const std::string& requested) {
    const auto base=requested.empty()?std::string("New Decal"):requested;std::unordered_set<std::string> names;for(const auto& e:scanDecalAssets(root))names.insert(e.name);
    if(!names.contains(base))return base;for(std::uint64_t n=2;;++n){const auto name=base+" "+std::to_string(n);if(name.size()>1024)throw std::runtime_error("Decal name is too long.");if(!names.contains(name))return name;}
}
}
DecalAsset defaultDecalAsset(const std::string& name){DecalAsset a;a.name=name;return a;}
void validateDecalAsset(const DecalAsset& a) {
    if(!a.id.empty()&&(!isAssetUuid(a.id)||isEngineAssetId(a.id)))throw std::runtime_error("Decal requires a writable managed UUID.");
    if(a.name.empty()||a.name.size()>1024||a.name.find_first_not_of(" \t\r\n")==std::string::npos||a.name.find('\0')!=std::string::npos)throw std::runtime_error("Decal name must contain 1 to 1024 UTF-8 bytes.");
    if(!a.material.empty()&&!isAssetUuid(a.material.id))throw std::runtime_error("Decal materials require managed UUIDs.");
    if(a.stagedMaterial&&a.material.id==a.stagedMaterial->id){validateMaterialAsset(*a.stagedMaterial);if(a.stagedMaterial->shading!=MaterialShading::Decal)throw std::runtime_error("Decal material requires Decal shading.");}
}
void canonicalizeDecalAsset(DecalAsset& a){validateDecalAsset(a);a.id=canonicalAssetId(a.id);a.material.id=canonicalAssetId(a.material.id);}
std::vector<std::string> decalMaterialReferences(const DecalAsset& a){return a.material.empty()?std::vector<std::string>{}:std::vector<std::string>{a.material.id};}
std::shared_ptr<const MaterialAsset> stagedDecalMaterial(const std::filesystem::path& root,const DecalAsset& a) {
    if(!a.stagedMaterial||a.material.id!=a.stagedMaterial->id)return {};
    const auto stored=storedAssetPaths(root);if(std::any_of(stored.begin(),stored.end(),[&](const auto& p){return p.id==a.material.id;})) {
        const auto durable=loadMaterialAsset(root,a.material.id);if(durable.shading!=MaterialShading::Decal)throw std::runtime_error("Assigned material no longer uses Decal shading.");return {};
    }return a.stagedMaterial;
}
std::filesystem::path decalAssetPath(const std::filesystem::path& root,const std::string& id) {
    if(!isAssetUuid(id)||isEngineAssetId(id))throw std::runtime_error("Decal file requires a writable managed UUID.");
    for(const auto& p:storedAssetPaths(root))if(p.id==canonicalAssetId(id)){if(p.file.extension()!=".crimson-decal")throw std::runtime_error("Asset is not a decal file.");return p.file;}
    try{const auto file=resolveAssetPath(root,id);if(file.extension()==".crimson-decal")return file;}catch(const std::exception&){}
    return resolveAssetPath(root,"assets/decals")/(canonicalAssetId(id)+".crimson-decal");
}
std::string serializeDecalAsset(const DecalAsset& value){auto a=value;canonicalizeDecalAsset(a);if(a.id.empty())throw std::runtime_error("Saving a decal requires its managed identity.");
    return Json{{"version",2},{"type","crimson-decal"},{"id",a.id},{"name",a.name},{"material",a.material}}.dump(2)+"\n";}
DecalAsset deserializeDecalAsset(std::string_view text,const std::string& expectedId) {
    if(text.size()>MaximumBytes)throw std::runtime_error("Decal exceeds 1 MiB.");const auto j=Json::parse(text);const auto version=j.at("version").get<int>();
    if((version!=1&&version!=2)||j.at("type")!="crimson-decal")throw std::runtime_error("Unsupported decal format.");
    DecalAsset a;a.id=j.at("id").get<std::string>();a.name=j.at("name").get<std::string>();
    if(version==2)a.material=j.at("material").get<AssetRef<MaterialAsset>>();else{a.stagedMaterial=legacyMaterial(j,a.id,a.name);a.material.id=a.stagedMaterial->id;}
    canonicalizeDecalAsset(a);if(a.id.empty()||(!expectedId.empty()&&a.id!=canonicalAssetId(expectedId)))throw std::runtime_error("Decal identity does not match its managed file.");return a;
}
DecalAsset loadDecalAsset(const std::filesystem::path& root,const std::string& id) {
    const auto file=decalAssetPath(root,id);if(std::filesystem::file_size(file)>MaximumBytes)throw std::runtime_error("Decal exceeds 1 MiB.");
    std::ifstream in(file,std::ios::binary);if(!in)throw std::runtime_error("Cannot read decal.");const std::string text{std::istreambuf_iterator<char>(in),{}};if(in.bad())throw std::runtime_error("Incomplete decal read.");return deserializeDecalAsset(text,id);
}
void saveDecalAsset(const std::filesystem::path& root,const DecalAsset& a) {
    if(!contentStorageReady(root))initializeContentStorage(root);const auto text=serializeDecalAsset(a);const auto file=decalAssetPath(root,a.id);
    std::vector<AssetFileChange> changes;std::vector<StoredAssetPath> locations;
    if(const auto m=stagedDecalMaterial(root,a)){const auto target=checkedContentPath(root,materialAssetPath(root,m->id));
        if(std::filesystem::exists(target))throw std::runtime_error("Unregistered converted material already exists; source preserved.");
        changes.push_back({utf8(target.lexically_relative(root)),serializeMaterialAsset(*m)});locations.push_back({m->id,target});}
    (void)checkedContentPath(root,file);changes.push_back({utf8(file.lexically_relative(root)),text});locations.push_back({a.id,file});
    changes.push_back({".crimson/asset-locations.json",prepareAssetPathRegistration(root,locations)});commitAssetTransaction(root,changes);refreshContentStorage(root);
}
DecalAsset createDecalAsset(const std::filesystem::path& root,const std::string& name) {
    if(!contentStorageReady(root))initializeContentStorage(root);auto a=defaultDecalAsset(unusedName(root,name));a.id=newAssetId();auto m=materialSeed(a.id,a.name);canonicalizeMaterialAsset(*m);
    a.stagedMaterial=m;a.material.id=m->id;saveDecalAsset(root,a);return a;
}
DecalAsset saveDecalAssetAs(const std::filesystem::path& root,const DecalAsset& source,const std::string& name) {
    validateDecalAsset(source);if(!contentStorageReady(root))initializeContentStorage(root);auto a=source;a.id=newAssetId();a.name=unusedName(root,name.empty()?source.name:name);saveDecalAsset(root,a);return a;
}
std::vector<DecalAssetEntry> scanDecalAssets(const std::filesystem::path& root,std::vector<std::string>* diagnostics) {
    std::vector<DecalAssetEntry> result;if(!contentStorageReady(root))return result;for(const auto& p:storedAssetPaths(root))if(p.file.extension()==".crimson-decal"&&std::filesystem::is_regular_file(p.file)) {
        try{auto a=loadDecalAsset(root,p.id);result.push_back({std::move(a.id),std::move(a.name)});}catch(const std::exception& e){if(diagnostics)diagnostics->push_back("Decal "+p.id+": "+e.what());}}
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.name!=b.name?a.name<b.name:a.id<b.id;});return result;
}
}
