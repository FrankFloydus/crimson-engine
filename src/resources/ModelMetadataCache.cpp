#include "StreamingModel.h"
#include "assets/ModelAnimation.h"
#include "CacheIO.h"
#include "math/Json.h"
#include "resources/AssetRefJson.h"
#include "resources/Paths.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

namespace crimson::streaming {
namespace {
using Json=nlohmann::json;
std::string utf8(const std::filesystem::path& path) { const auto text=path.generic_u8string(); return {text.begin(),text.end()}; }
std::filesystem::path path(const std::string& text) { return pathFromUtf8(text); }
bool inside(const std::filesystem::path& file,const std::filesystem::path& root) {
    const auto relative=file.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin()!="..";
}
std::uint64_t fileHash(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary); if (!input) throw std::runtime_error("Model cache dependency is missing.");
    auto key=detail::HashSeed; std::array<char,65536> block{};
    while (input) { input.read(block.data(),block.size()); detail::hash(key,block.data(),static_cast<std::size_t>(input.gcount())); }
    if (!input.eof()) throw std::runtime_error("Model cache dependency could not be read.");
    return key;
}
std::filesystem::path indexFile(const std::filesystem::path& root,const std::string& id) {
    auto key=detail::HashSeed; detail::hash(key,id.data(),id.size());
    return root/"assets"/"cache"/"mesh-lods"/"index"/(detail::hex(key)+".json");
}
Json dependencies(const std::filesystem::path& root,const std::string& id) {
    const auto manifest=std::filesystem::weakly_canonical(root/path(id));
    if (!inside(manifest,std::filesystem::weakly_canonical(root/"assets"/"models"))) throw std::runtime_error("Model metadata cache asset identity is outside managed models.");
    std::ifstream input(manifest); const auto data=Json::parse(input);
    std::vector<std::filesystem::path> files{manifest,manifest.parent_path()/path(data.at("document").get<std::string>())};
    for (const auto& dependency:data.at("dependencies")) if (dependency.at("kind")=="buffer") files.push_back(manifest.parent_path()/path(dependency.at("path").get<std::string>()));
    auto result=Json::array();
    for (const auto& file:files) {
        const auto canonical=std::filesystem::weakly_canonical(file);
        if (!inside(canonical,manifest.parent_path())) throw std::runtime_error("Model metadata cache dependency escaped its package.");
        result.push_back({{"file",utf8(canonical.lexically_relative(root))},{"hash",fileHash(canonical)}});
    }
    return result;
}
Json texture(const ModelTexture& value) {
    return {{"image",value.image.id},{"uv",value.texCoord},{"u",static_cast<int>(value.wrapU)},{"v",static_cast<int>(value.wrapV)},
        {"min",static_cast<int>(value.minFilter)},{"mag",static_cast<int>(value.magFilter)}};
}
ModelTexture texture(const Json& value) {
    const auto u=value.at("u").get<int>(),v=value.at("v").get<int>(),min=value.at("min").get<int>(),mag=value.at("mag").get<int>();
    if (u<0 || u>2 || v<0 || v>2 || min<0 || min>5 || mag<0 || mag>1) throw std::runtime_error("Cached model sampler is invalid.");
    return {AssetRef<DecodedImage>{value.at("image").get<std::string>()},value.at("uv").get<std::uint32_t>(),static_cast<ModelWrap>(u),static_cast<ModelWrap>(v),static_cast<ModelFilter>(min),static_cast<ModelFilter>(mag)};
}
Json material(const ModelMaterial& value) {
    auto slots=Json::array(); for (const auto* slot:{&value.baseColorTexture,&value.metallicRoughnessTexture,&value.normalTexture,&value.occlusionTexture,&value.emissiveTexture}) slots.push_back(texture(*slot));
    Json sampler=nullptr;
    if(value.sharedSampler)sampler={{"u",static_cast<int>(value.sharedSampler->wrapU)},{"v",static_cast<int>(value.sharedSampler->wrapV)},
        {"min",static_cast<int>(value.sharedSampler->minFilter)},{"mag",static_cast<int>(value.sharedSampler->magFilter)}};
    return {{"name",value.name},{"base",value.baseColor},{"metal",value.metallic},{"rough",value.roughness},{"normal",value.normalScale},{"occlusion",value.occlusionStrength},
        {"emissive",value.emissive},{"mask",value.alphaMask},{"alphaBlend",value.alphaBlend},{"double",value.doubleSided},{"receiveFog",value.receiveFog},{"cutoff",value.alphaCutoff},{"textures",std::move(slots)},
        {"uvScale",value.uvScale},{"uvOffset",value.uvOffset},{"uvRotation",value.uvRotation},{"triplanar",value.triplanar},{"triplanarWorld",value.triplanarWorld},{"uvScaleLinked",value.uvScaleLinked},{"sharedSampler",std::move(sampler)}};
}
ModelMaterial material(const Json& value) {
    ModelMaterial result; result.name=value.at("name").get<std::string>(); result.baseColor=value.at("base").get<Vec4>(); result.metallic=value.at("metal").get<float>(); result.roughness=value.at("rough").get<float>();
    result.normalScale=value.at("normal").get<float>(); result.occlusionStrength=value.at("occlusion").get<float>(); result.emissive=value.at("emissive").get<Vec3>();
    result.alphaMask=value.at("mask").get<bool>(); result.doubleSided=value.at("double").get<bool>(); result.alphaCutoff=value.at("cutoff").get<float>();
    result.receiveFog=value.value("receiveFog",true);
    result.alphaBlend=value.value("alphaBlend",false);
    // Old metadata contains imported identity mapping and per-slot samplers.
    // Geometry payloads are unchanged and remain usable without a cache rebuild.
    result.uvScale=value.value("uvScale",Vec3{1,1,1});result.uvOffset=value.value("uvOffset",Vec3{});result.uvRotation=value.value("uvRotation",Vec3{});
    result.triplanar=value.value("triplanar",false);result.triplanarWorld=value.value("triplanarWorld",false);result.uvScaleLinked=value.value("uvScaleLinked",true);
    if(!math::finite(result.uvScale)||!math::finite(result.uvOffset)||!math::finite(result.uvRotation))throw std::runtime_error("Cached material mapping is invalid.");
    if(value.contains("sharedSampler") && !value.at("sharedSampler").is_null()) {
        const auto& sampler=value.at("sharedSampler");const auto u=sampler.at("u").get<int>(),v=sampler.at("v").get<int>(),min=sampler.at("min").get<int>(),mag=sampler.at("mag").get<int>();
        if(u<0||u>2||v<0||v>2||min<0||min>5||mag<0||mag>1)throw std::runtime_error("Cached material sampler is invalid.");
        result.sharedSampler=ModelSampler{static_cast<ModelWrap>(u),static_cast<ModelWrap>(v),static_cast<ModelFilter>(min),static_cast<ModelFilter>(mag)};
    }
    const auto& slots=value.at("textures"); if (slots.size()!=5) throw std::runtime_error("Cached model material slots are invalid.");
    std::array<ModelTexture*,5> targets{&result.baseColorTexture,&result.metallicRoughnessTexture,&result.normalTexture,&result.occlusionTexture,&result.emissiveTexture};
    for (std::size_t i=0;i<5;++i) *targets[i]=texture(slots[i]); return result;
}
Json bounds(const Aabb& value) { return {{"min",value.minimum},{"max",value.maximum},{"valid",value.valid}}; }
Aabb bounds(const Json& value) { return {value.at("min").get<Vec3>(),value.at("max").get<Vec3>(),value.at("valid").get<bool>()}; }
}
void writeModelMetadataCache(const ModelAsset& asset,const ModelStream& stream,const std::filesystem::path& root) {
    // Synthetic CPU fixtures do not have a managed package; their independent LOD files remain usable.
    if (!std::filesystem::exists(root/path(asset.id))) return;
    Json data{{"version",ModelCacheVersion},{"meshoptimizer",1010},{"id",asset.id},{"key",stream.key},{"name",asset.name},{"format",asset.format},
        {"bounds",bounds(asset.bounds)},{"images",asset.images},{"materialIds",asset.materialIds},{"roots",asset.roots},{"warnings",asset.warnings},
        {"dependencies",dependencies(root,asset.id)}};
    data["materials"]=Json::array(); for (const auto& value:asset.materials) data["materials"].push_back(material(value));
    data["nodes"]=Json::array(); for (const auto& value:asset.nodes) data["nodes"].push_back({{"name",value.name},{"local",value.local},{"children",value.children},{"mesh",value.mesh},
        {"skin",value.skin},{"parent",value.parent},{"translation",value.translation},{"rotation",value.rotation},{"scale",value.scale},{"matrixAuthored",value.matrixAuthored},{"weights",value.weights}});
    data["meshes"]=Json::array(); for (const auto& value:asset.meshes) data["meshes"].push_back({{"name",value.name},{"primitives",value.primitives},{"weights",value.weights}});
    data["draws"]=Json::array(); for (const auto& value:asset.draws) data["draws"].push_back({{"primitive",value.primitive},{"transform",value.transform},{"node",value.node}});
    data["skins"]=Json::array();for(const auto& value:asset.skins)data["skins"].push_back({{"name",value.name},{"joints",value.joints},{"inverseBindMatrices",value.inverseBindMatrices},{"skeleton",value.skeleton}});
    data["animations"]=Json::array();for(const auto& value:asset.animations) {
        auto channels=Json::array();for(const auto& channel:value.channels)channels.push_back({{"node",channel.node},{"components",channel.components},
            {"path",static_cast<int>(channel.path)},{"interpolation",static_cast<int>(channel.interpolation)},{"times",channel.times},{"values",channel.values}});
        data["animations"].push_back({{"name",value.name},{"start",value.start},{"end",value.end},{"channels",std::move(channels)}});
    }
    data["primitives"]=Json::array();
    for (std::size_t p=0;p<stream.primitives.size();++p) {
        const auto& descriptor=stream.primitives[p]; const auto& primitive=asset.primitives[p];
        auto levels=Json::array(); for (const auto& value:descriptor.levels) levels.push_back({{"file",utf8(value.file.lexically_relative(root))},{"key",value.key},
            {"vertices",value.vertexCount},{"indices",value.indexCount},{"error",value.error}});
        auto targets=Json::array();for(const auto& target:primitive.morphTargets)targets.push_back(bounds(target.positionBounds));
        auto joints=Json::array();for(const auto& joint:primitive.skinJointBounds)joints.push_back(bounds(joint));
        data["primitives"].push_back({{"material",primitive.material},{"uv",primitive.uvMask},{"tangentUv",primitive.tangentUv},{"hasSkinning",primitive.hasSkinning},{"morphBounds",std::move(targets)},{"skinJointBounds",std::move(joints)},
            {"vertices",primitive.vertices.empty() ? primitive.sourceVertexCount:primitive.vertices.size()},
            {"indices",primitive.indices.empty() ? primitive.sourceIndexCount:primitive.indices.size()},
            {"bounds",bounds(descriptor.bounds)},{"uvSpan",descriptor.uvSpan},{"levels",std::move(levels)}});
    }
    detail::writeCacheFileAtomically(indexFile(root,asset.id),data.dump());
}
std::shared_ptr<ModelAsset> loadModelMetadataCache(const std::filesystem::path& manifest,const std::filesystem::path& projectRoot) {
    try {
        const auto root=std::filesystem::weakly_canonical(projectRoot);
        const auto canonical=std::filesystem::weakly_canonical(manifest.is_absolute() ? manifest:root/manifest);
        const auto id=utf8(canonical.lexically_relative(root)); std::ifstream input(indexFile(root,id)); if (!input) return {};
        const auto data=Json::parse(input);input.close();
        if (data.at("version")!=ModelCacheVersion || data.at("meshoptimizer")!=1010 || data.at("id")!=id || data.at("dependencies")!=dependencies(root,id)) return {};
        auto asset=std::make_shared<ModelAsset>(); asset->id=id; asset->name=data.at("name").get<std::string>(); asset->format=data.at("format").get<std::string>(); asset->bounds=bounds(data.at("bounds"));
        asset->images=data.at("images").get<std::vector<std::string>>(); asset->materialIds=data.at("materialIds").get<decltype(asset->materialIds)>();
        asset->roots=data.at("roots").get<std::vector<std::uint32_t>>(); asset->warnings=data.at("warnings").get<std::vector<std::string>>();
        for (const auto& value:data.at("materials")) asset->materials.push_back(material(value));
        for (const auto& value:data.at("nodes")) {
            ModelNode node{value.at("name").get<std::string>(),value.at("local").get<Mat4>(),value.at("children").get<std::vector<std::uint32_t>>(),value.at("mesh").get<std::int32_t>()};
            node.skin=value.at("skin").get<std::int32_t>();node.parent=value.at("parent").get<std::int32_t>();
            node.translation=value.at("translation").get<Vec3>();node.rotation=value.at("rotation").get<Quat>();node.scale=value.at("scale").get<Vec3>();
            node.matrixAuthored=value.at("matrixAuthored").get<bool>();node.weights=value.at("weights").get<std::vector<float>>();asset->nodes.push_back(std::move(node));
        }
        for (const auto& value:data.at("meshes")) asset->meshes.push_back({value.at("name").get<std::string>(),value.at("primitives").get<std::vector<std::uint32_t>>(),value.at("weights").get<std::vector<float>>()});
        for (const auto& value:data.at("draws")) asset->draws.push_back({value.at("primitive").get<std::uint32_t>(),value.at("transform").get<Mat4>(),value.at("node").get<std::int32_t>()});
        for(const auto& value:data.at("skins"))asset->skins.push_back({value.at("name").get<std::string>(),value.at("joints").get<std::vector<std::uint32_t>>(),
            value.at("inverseBindMatrices").get<std::vector<Mat4>>(),value.at("skeleton").get<std::int32_t>()});
        for(const auto& value:data.at("animations")) {
            ModelAnimationClip clip{value.at("name").get<std::string>(),value.at("start").get<float>(),value.at("end").get<float>()};
            for(const auto& item:value.at("channels")) {
                const auto path=item.at("path").get<int>(),interpolation=item.at("interpolation").get<int>();
                if(path<0||path>3||interpolation<0||interpolation>2)return {};
                clip.channels.push_back({item.at("node").get<std::uint32_t>(),item.at("components").get<std::uint32_t>(),static_cast<ModelAnimationPath>(path),
                    static_cast<ModelAnimationInterpolation>(interpolation),item.at("times").get<std::vector<float>>(),item.at("values").get<std::vector<float>>()});
            }
            asset->animations.push_back(std::move(clip));
        }
        auto stream=std::make_shared<ModelStream>(); stream->id=id; stream->key=data.at("key").get<std::uint64_t>();
        for (const auto& value:data.at("primitives")) {
            ModelPrimitive primitive; primitive.material=value.at("material").get<std::uint32_t>(); primitive.uvMask=value.at("uv").get<std::uint8_t>(); primitive.tangentUv=value.at("tangentUv").get<std::uint8_t>();
            primitive.sourceVertexCount=value.at("vertices").get<std::uint64_t>(); primitive.sourceIndexCount=value.at("indices").get<std::uint64_t>();
            primitive.hasSkinning=value.at("hasSkinning").get<bool>();primitive.bounds=bounds(value.at("bounds"));
            for(const auto& item:value.at("skinJointBounds"))primitive.skinJointBounds.push_back(bounds(item));
            for(const auto& item:value.at("morphBounds")) {ModelMorphTarget target;target.positionBounds=bounds(item);primitive.morphTargets.push_back(std::move(target));}
            if (primitive.material>=asset->materials.size()) throw std::runtime_error("Cached primitive material is invalid.");
            asset->primitives.push_back(primitive); PrimitiveLods descriptor{primitive.material,bounds(value.at("bounds")),{}};
            descriptor.uvSpan=value.at("uvSpan").get<std::array<Vec2,2>>();
            for (const auto& item:value.at("levels")) {
                auto file=std::filesystem::weakly_canonical(root/path(item.at("file").get<std::string>()));
                if (!inside(file,std::filesystem::weakly_canonical(root/"assets"/"cache"/"mesh-lods")) || !std::filesystem::is_regular_file(file)) return {};
                descriptor.levels.push_back({file,item.at("key").get<std::uint64_t>(),item.at("vertices").get<std::uint32_t>(),item.at("indices").get<std::uint32_t>(),item.at("error").get<float>()});
            }
            if (descriptor.levels.empty()) return {}; stream->primitives.push_back(std::move(descriptor));
        }
        if (asset->primitives.empty() || asset->materialIds.size()!=asset->materials.size()) return {};
        for (const auto& draw:asset->draws) if (draw.primitive>=asset->primitives.size() || !math::isAffine(draw.transform)) return {};
        validateModelAnimationData(*asset);
        asset->stream=std::move(stream); return asset;
    } catch (...) { return {}; }
}
}
