#include "MaterialAsset.h"
#include "AtomicFile.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
using Json=nlohmann::json;
std::filesystem::path path(const std::string& id) { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(id.data()),id.size())); }
std::filesystem::path materialPath(const std::filesystem::path& root,const std::string& id) {
    const auto relative=path(id);
    if (relative.has_root_path() || relative.parent_path()!=std::filesystem::path("assets/materials") || relative.extension()!=".crimson-material" || id.find(':')!=std::string::npos)
        throw std::runtime_error("Material asset must be stored directly under assets/materials.");
    const auto result=std::filesystem::weakly_canonical(root/relative),directory=std::filesystem::weakly_canonical(root/"assets/materials");
    const auto containment=result.lexically_relative(std::filesystem::weakly_canonical(root));
    if (result.parent_path()!=directory || containment.empty() || containment.is_absolute() || *containment.begin()=="..")
        throw std::runtime_error("Material path escapes its managed directory.");
    return result;
}
bool safeImage(const std::string& id) {
    if (id.empty() || id==GrayTextureId || id==NeutralNormalTextureId) return true;
    const auto relative=path(id);
    if (relative.has_root_path() || relative.empty() || id.find(':')!=std::string::npos) return false;
    for(const auto& part:relative) if(part=="..") return false;
    return relative.begin()!=relative.end() && *relative.begin()=="assets";
}
Json textureJson(const ModelTexture& t) {
    return {{"image",t.image.id},{"uv",t.texCoord},{"wrapU",static_cast<int>(t.wrapU)},{"wrapV",static_cast<int>(t.wrapV)},
        {"minFilter",static_cast<int>(t.minFilter)},{"magFilter",static_cast<int>(t.magFilter)}};
}
ModelTexture readTexture(const Json& j) {
    ModelTexture t; t.image=AssetRef<DecodedImage>{j.at("image").get<std::string>()}; t.texCoord=j.at("uv").get<uint32_t>();
    const int u=j.at("wrapU").get<int>(),v=j.at("wrapV").get<int>(),min=j.at("minFilter").get<int>(),mag=j.at("magFilter").get<int>();
    if(u<0||u>2||v<0||v>2||min<0||min>5||mag<0||mag>1) throw std::runtime_error("Material sampler is invalid.");
    t.wrapU=static_cast<ModelWrap>(u);t.wrapV=static_cast<ModelWrap>(v);t.minFilter=static_cast<ModelFilter>(min);t.magFilter=static_cast<ModelFilter>(mag);return t;
}
const char* shadingName(MaterialShading shading) {
    switch(shading){case MaterialShading::StandardPbr:return "standard-pbr";case MaterialShading::Unlit:return "unlit";}
    throw std::runtime_error("Unsupported material shading mode.");
}
const char* blendName(MaterialBlend blend) {
    switch(blend){case MaterialBlend::Opaque:return "opaque";case MaterialBlend::Mask:return "mask";case MaterialBlend::Alpha:return "alpha";case MaterialBlend::Additive:return "additive";}
    throw std::runtime_error("Unsupported material blend mode.");
}
MaterialShading readShading(const std::string& value) {
    if(value=="standard-pbr")return MaterialShading::StandardPbr;if(value=="unlit")return MaterialShading::Unlit;
    throw std::runtime_error("Unsupported material shading mode.");
}
MaterialBlend readBlend(const std::string& value) {
    if(value=="opaque")return MaterialBlend::Opaque;if(value=="mask")return MaterialBlend::Mask;
    if(value=="alpha")return MaterialBlend::Alpha;if(value=="additive")return MaterialBlend::Additive;
    throw std::runtime_error("Unsupported material blend mode.");
}
std::string uniqueId(const std::filesystem::path& root) {
    static std::atomic<uint64_t> sequence{};
    for(;;){char buffer[40]{};const auto tick=static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        auto first=std::to_chars(buffer,buffer+sizeof buffer,tick,16);*first.ptr++='-';
        const auto last=std::to_chars(first.ptr,buffer+sizeof buffer,sequence.fetch_add(1),16);
        const auto id="assets/materials/"+std::string(buffer,last.ptr)+".crimson-material";
        if(!std::filesystem::exists(materialPath(root,id)))return id;
    }
}
std::string uniqueName(const std::filesystem::path& root,const std::string& requested) {
    const std::string base=requested.empty()?"New Material":requested;if(base.size()>1024)throw std::runtime_error("Material name exceeds 1024 UTF-8 bytes.");
    std::unordered_set<std::string> names;const auto directory=root/"assets/materials";
    if(std::filesystem::exists(directory))for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.is_regular_file()&&entry.path().extension()==".crimson-material")
        try{std::ifstream input(entry.path());const auto json=Json::parse(input);names.insert(json.at("name").get<std::string>());}catch(const std::exception&){/* Damaged files remain untouched. */}
    if(!names.contains(base))return base;
    for(uint64_t suffix=2;;++suffix){const auto name=base+" "+std::to_string(suffix);if(name.size()>1024)throw std::runtime_error("Material name is too long for a unique suffix.");if(!names.contains(name))return name;}
}
}
std::filesystem::path materialAssetPath(const std::filesystem::path& root,const std::string& id){return materialPath(root,id);}
std::string importedMaterialId(const std::string& modelId,std::size_t index) {
    const auto token=path(modelId).parent_path().filename().generic_u8string();
    if(token.empty()) throw std::runtime_error("Model package identity is empty.");
    return "assets/materials/"+std::string(reinterpret_cast<const char*>(token.data()),token.size())+"-m"+std::to_string(index)+".crimson-material";
}
MaterialAsset defaultMaterialAsset() {
    MaterialAsset m; m.id=DefaultMaterialId;m.name="Default material";m.surface.name=m.name;
    m.surface.baseColor={.5f,.5f,.5f,1};m.surface.roughness=1;m.surface.metallic=0;return m;
}
MaterialAsset defaultParticleMaterialAsset() {
    MaterialAsset material;material.id=DefaultParticleMaterialId;material.name="Default particle material";material.surface.name=material.name;
    material.surface.baseColor={1,1,1,1};material.surface.metallic=0;material.surface.roughness=1;
    material.surface.baseColorTexture.wrapU=material.surface.baseColorTexture.wrapV=ModelWrap::Clamp;
    material.shading=MaterialShading::Unlit;material.blend=MaterialBlend::Additive;return material;
}
bool particleMaterialCompatible(const MaterialAsset& material) {
    const auto& surface=material.surface;
    return material.shading==MaterialShading::Unlit&&(material.blend==MaterialBlend::Alpha||material.blend==MaterialBlend::Additive)&&
        surface.metallicRoughnessTexture.image.empty()&&surface.normalTexture.image.empty()&&surface.occlusionTexture.image.empty()&&surface.emissiveTexture.image.empty()&&
        (surface.baseColorTexture.image.empty()||surface.baseColorTexture.texCoord==0);
}
bool modelMaterialCompatible(const MaterialAsset& material) {
    return material.shading==MaterialShading::StandardPbr&&(material.blend==MaterialBlend::Opaque||material.blend==MaterialBlend::Mask);
}
void validateMaterialAsset(const MaterialAsset& a) {
    (void)shadingName(a.shading);(void)blendName(a.blend);
    const auto& m=a.surface;
    if(a.id.empty()||a.name.empty()||a.name.size()>1024||!math::finite(m.baseColor)||!math::finite(m.emissive)||
        !math::finite(m.metallic)||!math::finite(m.roughness)||!math::finite(m.normalScale)||!math::finite(m.occlusionStrength)||!math::finite(m.alphaCutoff))
        throw std::runtime_error("Material contains invalid identity or nonfinite factors.");
    if(m.metallic<0||m.metallic>1||m.roughness<0||m.roughness>1||m.occlusionStrength<0||m.occlusionStrength>1||m.alphaCutoff<0||
        std::any_of(m.baseColor.begin(),m.baseColor.end(),[](float x){return x<0||x>1;})||std::any_of(m.emissive.begin(),m.emissive.end(),[](float x){return x<0;}))
        throw std::runtime_error("Material factors exceed supported ranges.");
    for(const auto* t:{&m.baseColorTexture,&m.metallicRoughnessTexture,&m.normalTexture,&m.occlusionTexture,&m.emissiveTexture}) {
        if(t->texCoord>1||!safeImage(t->image.id)) throw std::runtime_error("Material texture reference or UV set is invalid.");
        const int u=static_cast<int>(t->wrapU),v=static_cast<int>(t->wrapV),min=static_cast<int>(t->minFilter),mag=static_cast<int>(t->magFilter);
        if(u<0||u>2||v<0||v>2||min<0||min>5||mag<0||mag>1)throw std::runtime_error("Material sampler is invalid.");
    }
}
std::string serializeMaterialAsset(const MaterialAsset& a) {
    validateMaterialAsset(a);const auto& m=a.surface;
    const auto blend=a.blend==MaterialBlend::Opaque&&m.alphaMask?MaterialBlend::Mask:a.blend;
    return Json{{"version",2},{"type","crimson-material"},{"id",a.id},{"name",a.name},{"originModel",a.originModel},
        {"shading",shadingName(a.shading)},{"blend",blendName(blend)},
        {"baseColor",m.baseColor},{"metallic",m.metallic},{"roughness",m.roughness},{"normalScale",m.normalScale},
        {"occlusionStrength",m.occlusionStrength},{"emissive",m.emissive},{"alphaMask",blend==MaterialBlend::Mask},{"doubleSided",m.doubleSided},{"alphaCutoff",m.alphaCutoff},
        {"baseColorTexture",textureJson(m.baseColorTexture)},{"metallicRoughnessTexture",textureJson(m.metallicRoughnessTexture)},
        {"normalTexture",textureJson(m.normalTexture)},{"occlusionTexture",textureJson(m.occlusionTexture)},{"emissiveTexture",textureJson(m.emissiveTexture)}}.dump(2)+"\n";
}
MaterialAsset loadMaterialAsset(const std::filesystem::path& root,const std::string& id) {
    if(id==DefaultMaterialId)return defaultMaterialAsset();
    if(id==DefaultParticleMaterialId)return defaultParticleMaterialAsset();
    const auto file=materialPath(root,id);if(std::filesystem::file_size(file)>1024u*1024u)throw std::runtime_error("Material file exceeds 1 MiB.");
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read material asset.");const auto j=Json::parse(input);
    if(!j.at("version").is_number_integer()||(j.at("version")!=1&&j.at("version")!=2)||j.at("type")!="crimson-material"||j.at("id")!=id)throw std::runtime_error("Material type, version or identity is invalid.");
    MaterialAsset a;a.id=id;a.name=j.at("name").get<std::string>();a.originModel=j.value("originModel",std::string{});auto& m=a.surface;m.name=a.name;
    m.baseColor=j.at("baseColor").get<Vec4>();m.metallic=j.at("metallic").get<float>();m.roughness=j.at("roughness").get<float>();m.normalScale=j.at("normalScale").get<float>();
    m.occlusionStrength=j.at("occlusionStrength").get<float>();m.emissive=j.at("emissive").get<Vec3>();m.alphaMask=j.at("alphaMask").get<bool>();m.doubleSided=j.at("doubleSided").get<bool>();m.alphaCutoff=j.at("alphaCutoff").get<float>();
    if(j.at("version")==1)a.blend=m.alphaMask?MaterialBlend::Mask:MaterialBlend::Opaque;
    else {a.shading=readShading(j.at("shading").get<std::string>());a.blend=readBlend(j.at("blend").get<std::string>());m.alphaMask=a.blend==MaterialBlend::Mask;}
    m.baseColorTexture=readTexture(j.at("baseColorTexture"));m.metallicRoughnessTexture=readTexture(j.at("metallicRoughnessTexture"));m.normalTexture=readTexture(j.at("normalTexture"));
    m.occlusionTexture=readTexture(j.at("occlusionTexture"));m.emissiveTexture=readTexture(j.at("emissiveTexture"));validateMaterialAsset(a);return a;
}
void saveMaterialAsset(const std::filesystem::path& root,const MaterialAsset& a) {
    if(a.id.starts_with("builtin:"))throw std::runtime_error("Built-in materials cannot be edited.");
    writeFileAtomically(materialPath(root,a.id),serializeMaterialAsset(a));
}
MaterialAsset createMaterialAsset(const std::filesystem::path& root,const std::string& name) {
    auto material=defaultParticleMaterialAsset();material.id=uniqueId(root);material.name=uniqueName(root,name);material.surface.name=material.name;
    saveMaterialAsset(root,material);return material;
}
MaterialAsset copyMaterialAsset(const std::filesystem::path& root,const MaterialAsset& source,const std::string& name) {
    validateMaterialAsset(source);auto material=source;material.id=uniqueId(root);material.name=uniqueName(root,name.empty()?source.name:name);
    material.surface.name=material.name;material.originModel.clear();saveMaterialAsset(root,material);return material;
}
void promoteModelMaterials(const std::filesystem::path& root,const ModelAsset& model,const std::function<bool(const std::string&)>& deleted) {
    for(std::size_t i=0;i<model.materials.size();++i){const auto id=importedMaterialId(model.id,i);
        if((deleted&&deleted(id))||std::filesystem::exists(materialPath(root,id)))continue;
        MaterialAsset a{id,model.materials[i].name,model.id,model.materials[i]};a.blend=a.surface.alphaMask?MaterialBlend::Mask:MaterialBlend::Opaque;saveMaterialAsset(root,a);
    }
}
}
