#include "RagdollAsset.h"
#include "AnimationAsset.h"
#include "AssetTransaction.h"
#include "ContentStorage.h"
#include "resources/AssetId.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
namespace {
using Json=nlohmann::json;
constexpr std::size_t MaximumRagdollBytes=1024*1024,MaximumBodies=256;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::filesystem::path fromUtf8(const std::string& value){return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));}
std::string utf8(const std::filesystem::path& value){const auto text=value.generic_u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
bool range(float value,float minimum,float maximum){return std::isfinite(value)&&value>=minimum&&value<=maximum;}
void quaternion(const Quat& q){double length{};for(const float v:q){check(std::isfinite(v),"Ragdoll rotations must be finite unit quaternions.");length+=double(v)*v;}check(std::abs(length-1)<=1e-4,"Ragdoll rotations must be unit quaternions.");}
void offset(const Vec3& p){for(float v:p)check(range(v,-1e6f,1e6f),"Ragdoll offsets must be finite and within one million metres.");}
std::int64_t integer(const Json& value,std::int64_t minimum,std::int64_t maximum){
    check(value.is_number_integer()||value.is_number_unsigned(),"Ragdoll indices and enums must be integers.");
    if(value.is_number_unsigned()){const auto v=value.get<std::uint64_t>();check(v<=std::uint64_t(maximum),"Ragdoll integer is outside the supported range.");return std::int64_t(v);}
    const auto v=value.get<std::int64_t>();check(v>=minimum&&v<=maximum,"Ragdoll integer is outside the supported range.");return v;
}
float scalar(const Json& value){check(value.is_number(),"Ragdoll scalar must be numeric.");const double v=value.get<double>();check(std::isfinite(v)&&std::abs(v)<=std::numeric_limits<float>::max(),"Ragdoll scalar must be finite.");return float(v);}
Vec3 vector(const Json& value){check(value.is_array()&&value.size()==3,"Ragdoll vector must contain three numbers.");return {scalar(value[0]),scalar(value[1]),scalar(value[2])};}
Quat rotation(const Json& value){check(value.is_array()&&value.size()==4,"Ragdoll quaternion must contain four numbers.");return {scalar(value[0]),scalar(value[1]),scalar(value[2]),scalar(value[3])};}
Json array(const Vec3& v){return Json::array({v[0],v[1],v[2]});}
Json array(const Quat& q){return Json::array({q[0],q[1],q[2],q[3]});}
struct Hierarchy {std::vector<std::int32_t> parent;std::vector<std::uint32_t> order;};
Hierarchy hierarchy(const ModelAsset& model){
    check(!model.nodes.empty()&&model.nodes.size()<=65536,"Ragdoll source must contain a bounded node hierarchy.");
    Hierarchy result;result.parent.assign(model.nodes.size(),-1);
    for(std::size_t i=0;i<model.nodes.size();++i)for(const auto child:model.nodes[i].children){check(child<model.nodes.size()&&child!=i,"Ragdoll source hierarchy has an invalid child.");check(result.parent[child]<0,"Ragdoll source hierarchy has duplicate parents.");result.parent[child]=std::int32_t(i);}
    for(std::size_t i=0;i<model.nodes.size();++i)if(result.parent[i]<0)result.order.push_back(std::uint32_t(i));
    for(std::size_t i=0;i<result.order.size();++i)for(const auto child:model.nodes[result.order[i]].children)result.order.push_back(child);
    check(result.order.size()==model.nodes.size(),"Ragdoll source hierarchy contains a cycle.");return result;
}
std::unordered_set<std::uint32_t> sourceJoints(const ModelAsset& model,std::uint32_t skin){
    check(skin<model.skins.size(),"Ragdoll skin is unavailable in the source model.");const auto& value=model.skins[skin];
    check(!value.joints.empty(),"Ragdoll source skin has no joints.");std::unordered_set<std::uint32_t> joints;
    for(auto node:value.joints){check(node<model.nodes.size()&&joints.insert(node).second,"Ragdoll skin contains a missing or duplicate joint.");}return joints;
}
std::int32_t physicalParent(std::uint32_t node,const Hierarchy& tree,const std::unordered_map<std::uint32_t,std::int32_t>& indices){
    auto parent=tree.parent[node];while(parent>=0){if(const auto found=indices.find(std::uint32_t(parent));found!=indices.end())return found->second;parent=tree.parent[std::size_t(parent)];}return -1;
}
Quat orientY(const Vec3& direction){const auto y=math::normalized(direction);const auto cosine=math::dot(y,{0,1,0});if(cosine<-.999999)return {1,0,0,0};const auto axis=math::cross({0,1,0},y);const double w=1+cosine,length=std::sqrt(math::dot(axis,axis)+w*w);return {float(axis[0]/length),float(axis[1]/length),float(axis[2]/length),float(w/length)};}
Quat orientX(const Vec3& direction){const auto x=math::normalized(direction);const auto cosine=math::dot(x,{1,0,0});if(cosine<-.999999)return {0,1,0,0};const auto axis=math::cross({1,0,0},x);const double w=1+cosine,length=std::sqrt(math::dot(axis,axis)+w*w);return {float(axis[0]/length),float(axis[1]/length),float(axis[2]/length),float(w/length)};}
}

std::vector<Mat4> ragdollReferenceTransforms(const ModelAsset& model){
    const auto tree=hierarchy(model);std::vector<Mat4> worlds(model.nodes.size()),result(model.nodes.size());
    for(const auto node:tree.order){const auto& local=model.nodes[node].local;check(math::isAffine(local),"Ragdoll reference pose must contain finite affine transforms.");const auto parent=tree.parent[node];worlds[node]=parent<0?local:math::multiply(worlds[std::size_t(parent)],local);
        check(math::handedness(worlds[node])>0,"Ragdoll reference pose cannot contain reflected or singular transforms.");
        auto x=math::transformDirection(worlds[node],{1,0,0}),y=math::transformDirection(worlds[node],{0,1,0});
        check(math::length(x)>1e-8,"Ragdoll reference frame is singular.");x=math::normalized(x);y=y-x*math::dot(x,y);check(math::length(y)>1e-8,"Ragdoll reference frame is singular.");y=math::normalized(y);const auto z=math::cross(x,y);
        auto& frame=result[node];for(std::size_t axis=0;axis<3;++axis){frame[axis]=x[axis];frame[4+axis]=y[axis];frame[8+axis]=z[axis];frame[12+axis]=worlds[node][12+axis];}
    }return result;
}
void validateRagdollAsset(const RagdollAsset& asset){
    check(isAssetUuid(asset.id)&&isAssetUuid(asset.sourceModel.id)&&isAssetUuid(asset.bindingSignature),"Ragdoll requires valid asset, source model and binding identities.");
    check(!asset.name.empty()&&asset.name.size()<=1024&&asset.name.find_first_not_of(" \t\r\n")!=std::string::npos,"Ragdoll requires a nonempty name of at most 1024 bytes.");
    check(range(asset.gravityFactor,0,100),"Ragdoll gravity factor must be finite and from zero to 100.");check(asset.skin<=65535,"Ragdoll skin index is outside the supported range.");
    check(asset.bodies.size()<=MaximumBodies,"Ragdoll exceeds 256 bodies.");std::unordered_set<std::uint32_t> nodes;
    for(std::size_t i=0;i<asset.bodies.size();++i){const auto& b=asset.bodies[i];check(b.node<=65535&&nodes.insert(b.node).second,"Ragdoll bodies require distinct valid node indices.");check(b.parent>=-1&&(b.parent<0||std::size_t(b.parent)<i),"Ragdoll parents must precede their children.");
        check(b.shape>=RagdollShape::Capsule&&b.shape<=RagdollShape::Sphere,"Unsupported ragdoll body shape.");offset(b.position);quaternion(b.rotation);
        for(float v:b.size)check(range(v,.0001f,10000),"Ragdoll box dimensions must be positive finite metres.");check(range(b.radius,.0001f,10000)&&range(b.height,.0002f,10000),"Ragdoll radius and height must be positive finite metres.");
        check(b.shape!=RagdollShape::Capsule||b.height>=2*b.radius,"Ragdoll capsule height must include both caps.");check(range(b.mass,.0001f,1e6f),"Ragdoll mass must be finite and positive.");
        check(range(b.friction,0,10)&&range(b.restitution,0,1),"Ragdoll friction or restitution is outside its supported range.");
        const auto& j=b.joint;offset(j.position);quaternion(j.rotation);check(range(j.swingY,0,179.9f)&&range(j.swingZ,0,179.9f),"Ragdoll swing limits must be from zero to 179.9 degrees.");
        check(range(j.twistMin,-180,180)&&range(j.twistMax,-180,180)&&j.twistMin<=j.twistMax,"Ragdoll twist limits must be ordered within -180 to 180 degrees.");check(range(j.friction,0,1e6f),"Ragdoll joint friction must be finite and nonnegative.");
    }
}
void validateRagdollSource(const RagdollAsset& asset,const ModelAsset& model){
    validateRagdollAsset(asset);check(asset.sourceModel.id==model.id,"Ragdoll source model identity does not match.");check(asset.bindingSignature==animationBindingSignature(model),"Ragdoll skeleton binding no longer matches its source.");
    const auto joints=sourceJoints(model,asset.skin);const auto tree=hierarchy(model);(void)ragdollReferenceTransforms(model);std::unordered_map<std::uint32_t,std::int32_t> indices;
    for(std::size_t i=0;i<asset.bodies.size();++i){check(joints.contains(asset.bodies[i].node),"Ragdoll body is not a joint of its selected skin.");indices.emplace(asset.bodies[i].node,std::int32_t(i));}
    for(const auto& body:asset.bodies)check(body.parent==physicalParent(body.node,tree,indices),"Ragdoll parent must be the nearest selected joint ancestor.");
}
RagdollAsset makeRagdollAsset(const ModelAsset& model,std::uint32_t skin,const std::string& name){
    (void)sourceJoints(model,skin);RagdollAsset asset;asset.id=newAssetId();asset.name=name.empty()?(model.name.empty()?"Ragdoll":model.name+" Ragdoll"):name;asset.sourceModel.id=model.id;asset.bindingSignature=animationBindingSignature(model);asset.skin=skin;validateRagdollSource(asset,model);return asset;
}
void generateRagdollBodies(RagdollAsset& asset,const ModelAsset& model,const std::vector<std::uint32_t>& selectedNodes){
    validateRagdollSource(asset,model);const auto joints=sourceJoints(model,asset.skin);const auto tree=hierarchy(model);const auto frames=ragdollReferenceTransforms(model);
    std::unordered_set<std::uint32_t> selected;
    for(auto node:selectedNodes.empty()?model.skins[asset.skin].joints:selectedNodes)check(joints.contains(node)&&selected.insert(node).second,"Generated ragdoll selection contains a missing or duplicate skin joint.");
    check(selected.size()<=MaximumBodies,"Select at most 256 joints for ragdoll generation.");auto result=asset;result.bodies.clear();std::unordered_map<std::uint32_t,std::int32_t> indices;
    for(auto node:tree.order)if(selected.contains(node)){RagdollBody body;body.node=node;body.parent=physicalParent(node,tree,indices);indices.emplace(node,std::int32_t(result.bodies.size()));result.bodies.push_back(body);}
    for(std::size_t i=0;i<result.bodies.size();++i){auto& body=result.bodies[i];const auto& reference=frames[body.node];const Vec3 origin{reference[12],reference[13],reference[14]};Vec3 delta{};double longest{};
        for(const auto& child:result.bodies)if(child.parent==std::int32_t(i)){const auto& c=frames[child.node];const Vec3 candidate=Vec3{c[12],c[13],c[14]}-origin;const double length=math::length(candidate);if(length>longest){delta=candidate;longest=length;}}
        if(longest<1e-5&&body.parent>=0){const auto& p=frames[result.bodies[std::size_t(body.parent)].node];delta=(origin-Vec3{p[12],p[13],p[14]})*.6;longest=math::length(delta);}
        if(longest<1e-5){delta=math::transformDirection(reference,{0,.15f,0});longest=.15;}
        const auto inverse=math::inverse(reference);check(inverse.has_value(),"Ragdoll reference frame could not be inverted.");const auto localDelta=math::transformDirection(*inverse,delta);body.position=localDelta*.5;body.rotation=orientY(localDelta);body.joint.rotation=orientX(localDelta);
        body.radius=float(std::clamp(longest*.18,.015,.25));body.height=float(std::max(2*double(body.radius),longest+2*body.radius));body.size={2*body.radius,body.height,2*body.radius};body.mass=float(std::max(.05,longest*4));
    }validateRagdollSource(result,model);asset=std::move(result);
}
void rebindRagdollSource(RagdollAsset& asset,const ModelAsset& model,std::uint32_t skin,const std::vector<std::uint32_t>& mappedNodes){
    check(mappedNodes.size()==asset.bodies.size(),"Explicit ragdoll source remapping requires one node per body.");auto result=asset;result.sourceModel.id=model.id;result.bindingSignature=animationBindingSignature(model);result.skin=skin;
    for(std::size_t i=0;i<result.bodies.size();++i)result.bodies[i].node=mappedNodes[i];validateRagdollSource(result,model);asset=std::move(result);
}
std::string serializeRagdollAsset(const RagdollAsset& asset){
    validateRagdollAsset(asset);Json bodies=Json::array();for(const auto& b:asset.bodies){const auto& j=b.joint;bodies.push_back({{"node",b.node},{"parent",b.parent},{"shape",int(b.shape)},{"position",array(b.position)},{"rotation",array(b.rotation)},
        {"size",array(b.size)},{"radius",b.radius},{"height",b.height},{"mass",b.mass},{"friction",b.friction},{"restitution",b.restitution},
        {"joint",{{"position",array(j.position)},{"rotation",array(j.rotation)},{"swingY",j.swingY},{"swingZ",j.swingZ},{"twistMin",j.twistMin},{"twistMax",j.twistMax},{"friction",j.friction}}}});}
    return Json{{"type","crimson-ragdoll"},{"version",1},{"id",asset.id},{"name",asset.name},{"sourceModel",asset.sourceModel.id},{"bindingSignature",asset.bindingSignature},{"skin",asset.skin},{"gravityFactor",asset.gravityFactor},{"bodies",std::move(bodies)}}.dump(2)+'\n';
}
RagdollAsset deserializeRagdollAsset(std::string_view text,const std::string& expectedId){
    check(text.size()<=MaximumRagdollBytes,"Ragdoll exceeds one MiB.");const auto data=Json::parse(text);check(data.at("type")=="crimson-ragdoll"&&integer(data.at("version"),1,1)==1,"Unsupported Ragdoll type or version.");
    RagdollAsset asset;asset.id=data.at("id").get<std::string>();asset.name=data.at("name").get<std::string>();asset.sourceModel.id=data.at("sourceModel").get<std::string>();asset.bindingSignature=data.at("bindingSignature").get<std::string>();asset.skin=std::uint32_t(integer(data.at("skin"),0,65535));asset.gravityFactor=scalar(data.at("gravityFactor"));
    check(data.at("bodies").is_array()&&data.at("bodies").size()<=MaximumBodies,"Ragdoll exceeds 256 bodies.");for(const auto& value:data.at("bodies")){RagdollBody b;b.node=std::uint32_t(integer(value.at("node"),0,65535));b.parent=std::int32_t(integer(value.at("parent"),-1,255));b.shape=RagdollShape(integer(value.at("shape"),0,2));b.position=vector(value.at("position"));b.rotation=rotation(value.at("rotation"));b.size=vector(value.at("size"));
        b.radius=scalar(value.at("radius"));b.height=scalar(value.at("height"));b.mass=scalar(value.at("mass"));b.friction=scalar(value.at("friction"));b.restitution=scalar(value.at("restitution"));const auto& joint=value.at("joint");b.joint.position=vector(joint.at("position"));b.joint.rotation=rotation(joint.at("rotation"));b.joint.swingY=scalar(joint.at("swingY"));b.joint.swingZ=scalar(joint.at("swingZ"));b.joint.twistMin=scalar(joint.at("twistMin"));b.joint.twistMax=scalar(joint.at("twistMax"));b.joint.friction=scalar(joint.at("friction"));asset.bodies.push_back(b);}
    validateRagdollAsset(asset);check(expectedId.empty()||asset.id==expectedId,"Ragdoll has a different asset identity.");return asset;
}
std::filesystem::path ragdollAssetPath(const std::filesystem::path& root,const std::string& id){check(isAssetUuid(id),"Ragdoll reference must be a UUID.");const auto file=resolveAssetPath(root,id);check(assetPathExtension(file)==".crimson-ragdoll","Ragdoll reference has an incompatible asset kind.");return file;}
RagdollAsset loadRagdollAsset(const std::filesystem::path& root,const std::string& id){const auto file=ragdollAssetPath(root,id);check(std::filesystem::file_size(file)<=MaximumRagdollBytes,"Ragdoll exceeds one MiB.");std::ifstream input(file,std::ios::binary);check(bool(input),"Cannot read Ragdoll.");const std::string text{std::istreambuf_iterator<char>(input),{}};return deserializeRagdollAsset(text,id);}
void saveRagdollAsset(const std::filesystem::path& root,const RagdollAsset& asset,const std::filesystem::path& initialDestination){
    check(!isEngineAssetId(asset.id),"Engine ragdoll assets are read-only.");if(!contentStorageReady(root))initializeContentStorage(root);const auto contents=serializeRagdollAsset(asset);const auto entries=storedAssetPaths(root);const auto existing=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.id==asset.id;});
    check(existing==entries.end()||initialDestination.empty(),"Existing Ragdoll assets retain their registered path; move them through the Asset Browser.");
    const auto destination=initialDestination.empty()?root/fromUtf8(asset.id+".crimson-ragdoll"):(initialDestination.is_absolute()?initialDestination:root/initialDestination);
    const auto file=existing==entries.end()?checkedContentPath(root,destination):ragdollAssetPath(root,asset.id);(void)checkedContentPath(root,file);check(assetPathExtension(file)==".crimson-ragdoll","Ragdoll destination must use the native .crimson-ragdoll extension.");check(existing!=entries.end()||!std::filesystem::exists(file),"An unregistered Ragdoll destination already exists.");
    const std::array<StoredAssetPath,1> registration{{{asset.id,file}}};const std::array<AssetFileChange,2> changes{{{utf8(file.lexically_relative(root)),contents},{".crimson/asset-locations.json",prepareAssetPathRegistration(root,registration)}}};commitAssetTransaction(root,changes);refreshContentStorage(root);
}
RagdollAsset copyRagdollAsset(const std::filesystem::path& root,const RagdollAsset& original,const std::string& name){auto result=original;result.id=newAssetId();if(!name.empty())result.name=name;saveRagdollAsset(root,result);return result;}
} // namespace crimson
