#include "native_scene_projection.hpp"
#include "scene/scene_document.hpp"
#include "scene/light_properties.hpp"
#include "scene/SceneExtraction.h"
#include "math/Json.h"
#include "assets/AssetReferenceReport.h"
#include "resources/AssetId.h"
#include "render/native_instance_identity.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace crimson::integration {
namespace {
using Json=nlohmann::json;
std::string encoded_id(EntityId id){constexpr char kDigits[]="0123456789abcdef";std::string value(16,'0');for(unsigned i=0;i<16;++i){value[15-i]=kDigits[id&15];id>>=4;}return value;}
Json component_object(std::string_view text){auto value=text.empty()?Json::object():Json::parse(text);if(!value.is_object())throw std::runtime_error("Engine components must be a JSON object.");for(const auto* key:{"id","name","parent","order","local","visible","outlinerStyle","__nativeGeometry"})if(value.contains(key))throw std::runtime_error("Engine component envelope contains native entity metadata.");return value;}
void apply_light(EntityRecord& record,const quader::LightComponent& source){
    const auto color=quader::light_linear_to_srgb(source.color);const auto intensity=quader::light_engine_values(source).preview_energy*std::numbers::pi_v<float>;
    const auto common=[&](auto& value){value.color={color.x,color.y,color.z};value.intensity=intensity;value.enabled=intensity>0;value.castShadows=source.casts_shadows;value.colorMode=LightColorMode::Color;value.temperatureKelvin=source.color_temperature_kelvin;};
    if(source.type==quader::LightType::Directional){auto value=record.directional.value_or(SceneDirectionalLight{});common(value);record.directional=value;record.point.reset();record.spot.reset();}
    else if(source.type==quader::LightType::Spot){auto value=record.spot.value_or(SceneSpotLight{});common(value);value.radius=source.range;value.attenuation=source.distance_attenuation;record.spot=value;record.point.reset();record.directional.reset();}
    else{auto value=record.point.value_or(ScenePointLight{});common(value);value.radius=source.range;value.attenuation=source.distance_attenuation;record.point=value;record.spot.reset();record.directional.reset();}
}
}
struct NativeSceneProjection::Impl {
    std::string scene_uuid;
    std::uint64_t revision{};
    EntityId next_id{1};
    std::unordered_map<std::string,EntityId> ids;
    std::unordered_map<EntityId,std::string> uuids;
    std::unordered_set<EntityId> native_lights;
    std::unordered_map<EntityId,quader::EngineComponentsComponent> originals;
    SceneSnapshot snapshot;
    bool prepared{};
};
NativeSceneProjection::NativeSceneProjection():impl_(std::make_unique<Impl>()){}
NativeSceneProjection::~NativeSceneProjection()=default;
const SceneSnapshot& NativeSceneProjection::prepare(const quader::SceneDocument& native,std::string scene_uuid,std::uint64_t revision,const std::filesystem::path& root){
    auto& state=*impl_;if(state.prepared&&state.scene_uuid==scene_uuid&&state.revision==revision)return state.snapshot;
    Impl next;if(state.scene_uuid==scene_uuid){next.ids=state.ids;next.uuids=state.uuids;next.next_id=state.next_id;}
    next.scene_uuid=std::move(scene_uuid);next.revision=revision;
    const auto entities=native.entities();std::unordered_set<std::string> live;
    for(const auto id:entities){const auto uuid=native.uuid(id);if(uuid.empty()||!live.insert(uuid).second)throw std::runtime_error("Native entity UUID is empty or duplicated.");if(!next.ids.contains(uuid)){if(next.next_id==std::numeric_limits<EntityId>::max())throw std::runtime_error("Native entity projection identity space is exhausted.");const auto projected=next.next_id++;next.ids.emplace(uuid,projected);next.uuids.emplace(projected,uuid);}}
    const auto resolve=[&](std::string_view uuid){if(!live.contains(std::string(uuid)))return NoEntity;const auto found=next.ids.find(std::string(uuid));return found==next.ids.end()?NoEntity:found->second;};
    Json document{{"version",25},{"type","crimson-scene"},{"name","Native scene projection"},{"entities",Json::array()},{"settings",{{"activeEnvironment",encoded_id(0)},{"activeCamera",encoded_id(0)},{"playerCharacter",encoded_id(0)}}}};
    if(const auto* settings=native.engine_settings()){
        const auto value=settings->serialized_settings.empty()?Json::object():Json::parse(settings->serialized_settings);if(!value.is_object())throw std::runtime_error("Engine scene settings must be a JSON object.");
        for(const auto* key:{"activeEnvironment","activeCamera","playerCharacter"})if(value.contains(key))throw std::runtime_error("Engine scene selectors must use native UUID references.");
        document["settings"]=value;document["settings"]["activeEnvironment"]=encoded_id(resolve(settings->environment_uuid));document["settings"]["activeCamera"]=encoded_id(resolve(settings->active_camera_uuid));document["settings"]["playerCharacter"]=encoded_id(resolve(settings->player_uuid));
    }
    const auto roots=native.root_entities();
    for(const auto id:entities){const auto uuid=native.uuid(id);const auto* envelope=native.engine_components(id);if(envelope)next.originals.emplace(resolve(uuid),*envelope);auto record=envelope?component_object(envelope->text()):Json::object();
        if(envelope)for(const auto& reference:envelope->entity_references){const auto target=resolve(reference.entity_uuid);if(reference.slot=="cameraFollow.target"){if(target&&record.contains("cameraFollow"))record["cameraFollow"]["target"]=encoded_id(target);else record.erase("cameraFollow");}else if(reference.slot=="environment.sun"){if(record.contains("environment"))record["environment"]["sun"]=encoded_id(target);}else if(reference.slot=="decal.seed.receiver"){if(record.contains("decal"))record["decal"]["seed"]["receiver"]=encoded_id(target);}}
        if(record.contains("cameraFollow")&&!record["cameraFollow"].contains("target"))record.erase("cameraFollow");
        if(record.contains("environment")&&!record["environment"].contains("sun"))record["environment"]["sun"]=encoded_id(0);
        if(record.contains("decal")&&record["decal"].value("projection",std::string("Surface"))!="Box"&&!record["decal"]["seed"].contains("receiver"))record["decal"]["seed"]["receiver"]=encoded_id(0);
        if(record.contains("decal")&&record["decal"].contains("seed")&&record["decal"]["seed"].contains("nativeSource")){
            auto& seed=record["decal"]["seed"];auto& source=seed["nativeSource"];std::string receiver,member;
            if(source.contains("member"))throw std::runtime_error("Native decal member UUID must use native reference metadata.");
            if(envelope)for(const auto& reference:envelope->entity_references){if(reference.slot=="decal.seed.receiver")receiver=reference.entity_uuid;else if(reference.slot=="decal.seed.member")member=reference.entity_uuid;}
            source["member"]=member;
            seed["sourceInstance"]=encoded_id(quader::native_source_instance_id(receiver,member,source.at("indexOffset").get<std::uint32_t>(),source.at("indexCount").get<std::uint32_t>()));
        }
        record["__nativeGeometry"]=native.mesh(id)!=nullptr||native.instance_occurrence(id)!=nullptr||(native.curve(id)&&native.curve(id)->solidify);record["id"]=encoded_id(resolve(uuid));record["name"]=native.name(id)?native.name(id)->name:std::string("Entity");record["parent"]=encoded_id(resolve(native.uuid(native.parent(id))));
        const auto parent=native.parent(id);const auto* hierarchy=parent?native.hierarchy(parent):nullptr;const auto& siblings=hierarchy?hierarchy->children:roots;record["order"]=static_cast<std::uint32_t>(std::distance(siblings.begin(),std::find(siblings.begin(),siblings.end(),id)));
        Mat4 local;local.values=native.local_transform_matrix(id).values;record["local"]=local;record["visible"]=native.is_effectively_visible(id);document["entities"].push_back(std::move(record));
    }
    next.snapshot=parseSceneSnapshot(document.dump(),root);
    for(const auto id:entities)if(const auto* light=native.light(id)){const auto projected=resolve(native.uuid(id));const auto found=std::find_if(next.snapshot.records.begin(),next.snapshot.records.end(),[&](const auto& record){return record.id==projected;});if(found!=next.snapshot.records.end()){apply_light(*found,*light);next.native_lights.insert(projected);}}
    Scene::validate(next.snapshot.records,next.snapshot.properties);next.prepared=true;state=std::move(next);return state.snapshot;
}
const SceneSnapshot& NativeSceneProjection::snapshot()const{return impl_->snapshot;}
EntityId NativeSceneProjection::entity_id(std::string_view uuid)const{const auto found=impl_->ids.find(std::string(uuid));return found==impl_->ids.end()?NoEntity:found->second;}
std::string NativeSceneProjection::entity_uuid(EntityId id)const{const auto found=impl_->uuids.find(id);return found==impl_->uuids.end()?std::string{}:found->second;}
std::uint64_t NativeSceneProjection::source_revision()const{return impl_->revision;}
const std::string& NativeSceneProjection::scene_uuid()const{return impl_->scene_uuid;}
quader::EngineComponentsComponent NativeSceneProjection::engine_components(const EntityRecord& source,EntityUuidResolver resolver)const{
    if(!resolver)resolver=[this](EntityId id){return entity_uuid(id);};
    auto record=source;const auto follow=record.cameraFollow;record.cameraFollow.reset();if(record.environment)record.environment->sun=NoEntity;if(record.decal)record.decal->seed.receiver=NoEntity;record.parent=NoEntity;record.id=record.id?record.id:1;record.name=record.name.empty()?"Entity":record.name;
    SceneSnapshot snapshot;snapshot.name="Component validation";snapshot.records.push_back(std::move(record));auto value=Json::parse(serializeSceneSnapshot(snapshot))["entities"].front();
    for(const auto* key:{"id","name","parent","order","local","visible","outlinerStyle","__nativeGeometry"})value.erase(key);
    std::vector<quader::EngineEntityReference> references;const auto reference=[&](const char* slot,EntityId id){if(id)if(auto uuid=resolver(id);!uuid.empty())references.push_back({slot,std::move(uuid)});};
    if(follow){value["cameraFollow"]={{"offset",follow->offset},{"worldBasis",follow->worldBasis}};reference("cameraFollow.target",follow->target);}
    if(source.environment){value["environment"].erase("sun");reference("environment.sun",source.environment->sun);}
    if(source.decal&&source.decal->projection==SceneDecalProjection::Surface){auto& seed=value["decal"]["seed"];seed.erase("receiver");reference("decal.seed.receiver",source.decal->seed.receiver);
        if(source.decal->seed.nativeSource){seed.erase("sourceInstance");seed["nativeSource"].erase("member");
            if(!source.decal->seed.nativeSource->memberUuid.empty())references.push_back({"decal.seed.member",source.decal->seed.nativeSource->memberUuid});}}
    const auto baseline=std::find_if(impl_->snapshot.records.begin(),impl_->snapshot.records.end(),[&](const auto& record){return record.id==source.id;});
    if(const auto original=impl_->originals.find(source.id);original!=impl_->originals.end()&&baseline!=impl_->snapshot.records.end()){
        const auto retained=component_object(original->second.text());const auto retain_reference=[&](const char* slot){const auto found=std::find_if(original->second.entity_references.begin(),original->second.entity_references.end(),[&](const auto& entry){return entry.slot==slot;});if(found!=original->second.entity_references.end())references.push_back(*found);};
        if(!source.cameraFollow&&!baseline->cameraFollow&&retained.contains("cameraFollow")){value["cameraFollow"]=retained["cameraFollow"];retain_reference("cameraFollow.target");}
        if(source.environment&&baseline->environment&&!source.environment->sun&&!baseline->environment->sun)retain_reference("environment.sun");
        if(source.decal&&baseline->decal&&!source.decal->seed.receiver&&!baseline->decal->seed.receiver)retain_reference("decal.seed.receiver");
    }
    quader::EngineComponentsComponent result{1u,value.dump()};result.entity_references=std::move(references);
    std::vector<AssetUsage> usages;appendSceneAssetUsages(usages,std::span<const EntityRecord>(&source,1),{}, {},false);
    for(const auto& usage:usages){auto id=canonicalAssetId(usage.targetId);if(isAssetUuid(id)&&!isEngineAssetId(id))result.asset_references.push_back(std::move(id));}
    std::sort(result.asset_references.begin(),result.asset_references.end());result.asset_references.erase(std::unique(result.asset_references.begin(),result.asset_references.end()),result.asset_references.end());result.asset_references_complete=true;return result;
}
quader::EngineSceneSettings NativeSceneProjection::engine_settings(const SceneProperties& properties,EntityUuidResolver resolver)const{
    if(!resolver)resolver=[this](EntityId id){return entity_uuid(id);};quader::EngineSceneSettings result;result.serialized_settings="{}";result.active_camera_uuid=resolver(properties.activeCamera);result.player_uuid=resolver(properties.playerCharacter);result.environment_uuid=resolver(properties.activeEnvironment);return result;
}
void NativeSceneProjection::apply_native_light_directions(ExtractedScene& extracted,const Scene& scene)const{
    const auto direction=[&](EntityId id){const auto world=scene.worldTransform(id);return world?math::normalized(math::transformDirection(*world,{0,-1,0}),{0,-1,0}):Vec3{0,-1,0};};
    for(auto& light:extracted.directional)if(impl_->native_lights.contains(light.entityId))light.direction=direction(light.entityId);
    for(auto& light:extracted.points)if(impl_->native_lights.contains(light.entityId))light.direction=direction(light.entityId);
}
}
