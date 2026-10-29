#include "AnimationAsset.h"
#include "AssetTransaction.h"
#include "ContentStorage.h"
#include "resources/AssetId.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
using Json=nlohmann::json;
constexpr std::uintmax_t MaximumAnimationBytes=65536;
constexpr const char* BindingNamespace="bf8e96cc-335d-5c7b-90d8-044cd7a71313";
std::filesystem::path fromUtf8(const std::string& value){return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));}
std::string utf8(const std::filesystem::path& value){const auto text=value.generic_u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
std::string clipIdentity(const ModelAsset& model,std::size_t index) {
    if(index>=model.animations.size())throw std::runtime_error("Animation source clip is unavailable.");
    const auto& clip=model.animations[index];
    Json tracks=Json::array();
    for(const auto& channel:clip.channels)tracks.push_back({{"node",channel.node},{"path",int(channel.path)},
        {"interpolation",int(channel.interpolation)},{"components",channel.components},{"times",channel.times},{"values",channel.values}});
    return derivedAssetId(model.id,Json{{"name",clip.name},{"start",clip.start},{"end",clip.end},{"tracks",std::move(tracks)}}.dump());
}
std::int32_t defaultRoot(const ModelAsset& model) {
    std::vector<std::int32_t> parents(model.nodes.size(),-1);
    for(std::size_t i=0;i<model.nodes.size();++i)for(const auto child:model.nodes[i].children)
        if(child<parents.size())parents[child]=static_cast<std::int32_t>(i);
    for(const auto& skin:model.skins) {
        std::unordered_set<std::uint32_t> joints(skin.joints.begin(),skin.joints.end());
        for(const auto joint:skin.joints)if(joint<model.nodes.size()) {
            auto name=model.nodes[joint].name;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(name=="root")return static_cast<std::int32_t>(joint);
        }
        if(skin.skeleton>=0&&std::size_t(skin.skeleton)<model.nodes.size())return skin.skeleton;
        for(const auto joint:skin.joints)if(joint<parents.size()&&(parents[joint]<0||!joints.contains(std::uint32_t(parents[joint]))))return static_cast<std::int32_t>(joint);
    }
    return model.roots.empty()?(model.nodes.empty()?-1:0):static_cast<std::int32_t>(model.roots.front());
}
}

std::string importedAnimationId(const std::string& modelId,std::uint32_t clip) {
    return derivedAssetId(modelId,"animation:"+std::to_string(clip));
}
std::string animationBindingSignature(const ModelAsset& model) {
    Json nodes=Json::array(),skins=Json::array();
    for(const auto& node:model.nodes) {
        Json local=Json::array();for(const auto value:node.local.values)local.push_back(value);
        const auto& weights=node.weights.empty()&&node.mesh>=0&&std::size_t(node.mesh)<model.meshes.size()?model.meshes[std::size_t(node.mesh)].weights:node.weights;
        Json targets=Json::array();
        if(node.mesh>=0&&std::size_t(node.mesh)<model.meshes.size())for(const auto primitive:model.meshes[std::size_t(node.mesh)].primitives)
            targets.push_back(primitive<model.primitives.size()?model.primitives[primitive].morphTargets.size():0);
        nodes.push_back({{"name",node.name},{"children",node.children},{"local",std::move(local)},
            {"matrixAuthored",node.matrixAuthored},{"translation",{node.translation[0],node.translation[1],node.translation[2]}},
            {"rotation",{node.rotation[0],node.rotation[1],node.rotation[2],node.rotation[3]}},{"scale",{node.scale[0],node.scale[1],node.scale[2]}},
            {"mesh",node.mesh},{"skin",node.skin},{"weights",weights},{"targets",std::move(targets)}});
    }
    for(const auto& skin:model.skins)skins.push_back({{"joints",skin.joints},{"skeleton",skin.skeleton}});
    return derivedAssetId(BindingNamespace,Json{{"nodes",std::move(nodes)},{"skins",std::move(skins)}}.dump());
}
AnimationAsset importedAnimationAsset(const ModelAsset& model,std::size_t clip) {
    if(clip>=model.animations.size()||clip>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("Animation source clip is unavailable.");
    AnimationAsset asset;asset.id=importedAnimationId(model.id,static_cast<std::uint32_t>(clip));
    asset.name=model.animations[clip].name.empty()?"Animation "+std::to_string(clip+1):model.animations[clip].name;
    asset.sourceModel.id=model.id;asset.sourceClip=static_cast<std::uint32_t>(clip);asset.sourceClipIdentity=clipIdentity(model,clip);
    asset.bindingSignature=animationBindingSignature(model);asset.endTime=model.animations[clip].end-model.animations[clip].start;asset.rootNode=defaultRoot(model);
    validateAnimationAsset(asset);return asset;
}
void validateAnimationAsset(const AnimationAsset& asset) {
    if(!isAssetUuid(asset.id)||!isAssetUuid(asset.sourceModel.id)||!isAssetUuid(asset.sourceClipIdentity)||!isAssetUuid(asset.bindingSignature))
        throw std::runtime_error("Animation requires valid asset, source clip and skeleton binding identities.");
    if(asset.name.empty()||asset.name.size()>1024||asset.name.find_first_not_of(" \t\r\n")==std::string::npos)
        throw std::runtime_error("Animation requires a nonempty name of at most 1024 bytes.");
    if(!std::isfinite(asset.startTime)||!std::isfinite(asset.endTime)||asset.startTime<0||asset.endTime<asset.startTime||asset.endTime>1e9f)
        throw std::runtime_error("Animation range requires finite ordered times from 0 to 1000000000 seconds.");
    if(!std::isfinite(asset.playbackRate)||asset.playbackRate<0||asset.playbackRate>100)
        throw std::runtime_error("Animation playback rate must be finite and from 0 to 100.");
    if(asset.rootNode<-1||(asset.forceRootLock&&asset.rootNode<0)||asset.rootLockReference<AnimationRootLockReference::FirstFrame||asset.rootLockReference>AnimationRootLockReference::Zero)
        throw std::runtime_error("Animation root lock requires a valid root node and reference mode.");
}
void validateAnimationSource(const AnimationAsset& asset,const ModelAsset& source) {
    validateAnimationAsset(asset);
    if(asset.sourceModel.id!=source.id||asset.sourceClip>=source.animations.size()||asset.sourceClipIdentity!=clipIdentity(source,asset.sourceClip))
        throw std::runtime_error("Animation source clip no longer matches its retained imported tracks.");
    if(asset.bindingSignature!=animationBindingSignature(source))throw std::runtime_error("Animation skeleton binding no longer matches its imported source.");
    const auto duration=source.animations[asset.sourceClip].end-source.animations[asset.sourceClip].start;
    if(asset.endTime>duration+std::max(1e-5f,std::abs(duration)*1e-6f))throw std::runtime_error("Animation playback range exceeds its imported source clip.");
    if(asset.rootNode>=0&&std::size_t(asset.rootNode)>=source.nodes.size())throw std::runtime_error("Animation root node is unavailable in its source hierarchy.");
}
std::string serializeAnimationAsset(const AnimationAsset& asset) {
    validateAnimationAsset(asset);
    return Json{{"type","crimson-animation"},{"version",1},{"id",asset.id},{"name",asset.name},
        {"sourceModel",asset.sourceModel.id},{"sourceClip",asset.sourceClip},{"sourceClipIdentity",asset.sourceClipIdentity},{"bindingSignature",asset.bindingSignature},
        {"startTime",asset.startTime},{"endTime",asset.endTime},{"playbackRate",asset.playbackRate},{"loop",asset.loop},
        {"rootNode",asset.rootNode},{"forceRootLock",asset.forceRootLock},{"rootLockReference",int(asset.rootLockReference)}}.dump(2)+'\n';
}
AnimationAsset deserializeAnimationAsset(std::string_view text,const std::string& expectedId) {
    if(text.size()>MaximumAnimationBytes)throw std::runtime_error("Animation exceeds 64 KiB.");
    const auto data=Json::parse(text);
    if(data.at("type")!="crimson-animation"||data.at("version")!=1)throw std::runtime_error("Unsupported Animation type or version.");
    if(!data.at("sourceClip").is_number_unsigned()&&!data.at("sourceClip").is_number_integer())throw std::runtime_error("Animation source clip must be a nonnegative integer.");
    if(data.at("sourceClip").is_number_unsigned()&&data.at("sourceClip").get<std::uint64_t>()>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("Animation source clip is outside the supported range.");
    const auto clip=data.at("sourceClip").get<std::int64_t>();if(clip<0||clip>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("Animation source clip is outside the supported range.");
    if(!data.at("rootNode").is_number_integer()||!data.at("rootLockReference").is_number_integer())throw std::runtime_error("Animation root node and reference must be integers.");
    if((data.at("rootNode").is_number_unsigned()&&data.at("rootNode").get<std::uint64_t>()>std::numeric_limits<std::int32_t>::max())||
        (data.at("rootLockReference").is_number_unsigned()&&data.at("rootLockReference").get<std::uint64_t>()>2))throw std::runtime_error("Animation root settings are outside the supported range.");
    const auto root=data.at("rootNode").get<std::int64_t>(),reference=data.at("rootLockReference").get<std::int64_t>();
    if(root<-1||root>std::numeric_limits<std::int32_t>::max()||reference<0||reference>2)throw std::runtime_error("Animation root settings are outside the supported range.");
    AnimationAsset asset;asset.id=data.at("id").get<std::string>();asset.name=data.at("name").get<std::string>();asset.sourceModel.id=data.at("sourceModel").get<std::string>();
    asset.sourceClip=static_cast<std::uint32_t>(clip);asset.sourceClipIdentity=data.at("sourceClipIdentity").get<std::string>();asset.bindingSignature=data.at("bindingSignature").get<std::string>();
    asset.startTime=data.at("startTime").get<float>();asset.endTime=data.at("endTime").get<float>();asset.playbackRate=data.at("playbackRate").get<float>();
    asset.loop=data.at("loop").get<bool>();asset.forceRootLock=data.at("forceRootLock").get<bool>();asset.rootNode=static_cast<std::int32_t>(root);asset.rootLockReference=static_cast<AnimationRootLockReference>(reference);
    validateAnimationAsset(asset);if(!expectedId.empty()&&asset.id!=expectedId)throw std::runtime_error("Animation has a different asset identity.");return asset;
}
std::filesystem::path animationAssetPath(const std::filesystem::path& root,const std::string& id) {
    if(!isAssetUuid(id))throw std::runtime_error("Animation reference must be a UUID.");
    const auto file=resolveAssetPath(root,id);if(assetPathExtension(file)!=".crimson-animation")throw std::runtime_error("Animation reference has an incompatible asset kind.");return file;
}
AnimationAsset loadAnimationAsset(const std::filesystem::path& root,const std::string& id) {
    const auto file=animationAssetPath(root,id);if(std::filesystem::file_size(file)>MaximumAnimationBytes)throw std::runtime_error("Animation exceeds 64 KiB.");
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read Animation.");
    const std::string contents{std::istreambuf_iterator<char>(input),{}};return deserializeAnimationAsset(contents,id);
}
void saveAnimationAsset(const std::filesystem::path& root,const AnimationAsset& asset) {
    if(isEngineAssetId(asset.id))throw std::runtime_error("Engine animation assets are read-only.");
    if(!contentStorageReady(root))initializeContentStorage(root);
    const auto contents=serializeAnimationAsset(asset);const auto entries=storedAssetPaths(root);
    const auto existing=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.id==asset.id;});
    const auto file=existing==entries.end()?checkedContentPath(root,root/fromUtf8(asset.id+".crimson-animation")):animationAssetPath(root,asset.id);
    (void)checkedContentPath(root,file);
    if(existing==entries.end()&&std::filesystem::exists(file))throw std::runtime_error("An unregistered Animation destination already exists.");
    const std::array<StoredAssetPath,1> registration{{{asset.id,file}}};
    const std::array<AssetFileChange,2> changes{{{utf8(file.lexically_relative(root)),contents},{".crimson/asset-locations.json",prepareAssetPathRegistration(root,registration)}}};
    commitAssetTransaction(root,changes);refreshContentStorage(root);
}
AnimationAsset copyAnimationAsset(const std::filesystem::path& root,const AnimationAsset& original,const std::string& name) {
    auto asset=original;asset.id=newAssetId();if(!name.empty())asset.name=name;saveAnimationAsset(root,asset);return asset;
}
} // namespace crimson
