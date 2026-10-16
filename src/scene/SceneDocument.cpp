#include "SceneDocument.h"
#include "BuiltinModels.h"
#include "EnvironmentSerialization.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include "math/Json.h"
#include "resources/AssetRefJson.h"
#include "crimson-renderer/shaders/point_light.sh"
#include "crimson-renderer/shaders/ambient_visibility_response.sh"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
namespace {
using Json = nlohmann::json;
std::string sceneNameKey(std::string_view name) {
    const auto first=name.find_first_not_of(" \t\r\n");
    if(first==std::string_view::npos)return {};
    std::string key(name.substr(first,name.find_last_not_of(" \t\r\n")-first+1));
    for(auto& character:key)if(character>='A'&&character<='Z')character=char(character-'A'+'a');
    return key;
}
std::filesystem::path fromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()),text.size()};
}
uint64_t randomId() {
    static std::mt19937_64 random([] {
        std::random_device device;
        std::seed_seq seed{device(),device(),device(),device(),device(),device(),device(),device()};
        return std::mt19937_64(seed);
    }());
    uint64_t result{};
    while (!result) result = random();
    return result;
}
std::string hexId(uint64_t id) {
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << id;
    return text.str();
}
EntityId parseId(const Json& value) {
    if (!value.is_string()) throw std::runtime_error("Scene entity IDs must be hexadecimal strings.");
    const auto text = value.get<std::string>();
    EntityId result{};
    const auto parsed = std::from_chars(text.data(),text.data()+text.size(),result,16);
    if (text.size() != 16 || parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size())
        throw std::runtime_error("Scene has an invalid entity ID.");
    return result;
}
std::string uniqueToken() { return hexId(randomId()) + hexId(randomId()); }
std::filesystem::path scenePath(const std::filesystem::path& root, const std::string& id) {
    const auto relative = fromUtf8(id);
    if (!isAssetUuid(id) && (relative.has_root_path() || relative.extension() != ".crimson-scene" ||
        relative.parent_path() != std::filesystem::path("assets/scenes")))
        throw std::runtime_error("Scene asset must be a .crimson-scene file directly under assets/scenes.");
    const auto path = std::filesystem::weakly_canonical(resolveAssetPath(root,id));
    if(path.extension()!=".crimson-scene")throw std::runtime_error("Scene identity does not reference a native scene file.");
    if(contentStorageReady(root))return checkedContentPath(root,path);
    const auto directory = std::filesystem::weakly_canonical(root / "assets/scenes");
    if (path.parent_path() != directory)
        throw std::runtime_error("Scene asset path escapes its managed directory.");
    // Also reject a redirected assets/scenes directory outside the project.
    const auto canonicalRoot = std::filesystem::weakly_canonical(root);
    const auto contained = directory.lexically_relative(canonicalRoot);
    if (contained.empty() || contained.is_absolute() || *contained.begin() == "..")
        throw std::runtime_error("Managed scene directory escapes the project.");
    return path;
}
uint32_t nextOrder(const std::vector<EntityRecord>& records, EntityId parent) {
    uint64_t result{};
    for (const auto& record : records) if (record.parent == parent)
        result = std::max(result,static_cast<uint64_t>(record.order)+1);
    if (result > std::numeric_limits<uint32_t>::max()) throw std::runtime_error("Scene sibling order is exhausted.");
    return static_cast<uint32_t>(result);
}
EntityId unusedId(const std::vector<EntityRecord>& records) {
    EntityId result{};
    do { result = randomId(); }
    while (std::any_of(records.begin(),records.end(),[result](const auto& record) { return record.id == result; }));
    return result;
}
uint32_t readLightingMask(const Json& object,const char* name,uint32_t fallback) {
    const auto field=object.find(name);
    if(field==object.end())return fallback;
    if(!field->is_number_integer() || field->get<int64_t>()<0 || field->get<uint64_t>()>lighting_scope::All)
        throw std::runtime_error("Lighting masks must contain only World and Character bits.");
    return field->get<uint32_t>();
}
Json rolesJson(const LightRoles& roles) {
    return {{"receiverMask",roles.receiverMask},{"shadowCasterMask",roles.shadowCasterMask},
        {"ambientRevealMask",roles.ambientRevealMask},{"ambientReveal",roles.ambientReveal},{"ambientRevealStrength",roles.ambientRevealStrength}};
}
LightRoles readRoles(const Json& source,LightRoles value={}) {
    if(!source.is_object())throw std::runtime_error("Light roles must be an object.");
    value.receiverMask=readLightingMask(source,"receiverMask",value.receiverMask);
    value.shadowCasterMask=readLightingMask(source,"shadowCasterMask",value.shadowCasterMask);
    value.ambientRevealMask=readLightingMask(source,"ambientRevealMask",value.ambientRevealMask);
    value.ambientReveal=source.value("ambientReveal",value.ambientReveal);
    value.ambientRevealStrength=source.value("ambientRevealStrength",value.ambientRevealStrength);
    return value;
}
Json characterLightJson(const CharacterLight& value) {
    return {{"enabled",value.enabled},{"direction",value.direction},{"color",value.color},
        {"intensity",value.intensity},{"diffuse",value.diffuse},{"specular",value.specular}};
}
CharacterLight readCharacterLight(const Json& source,CharacterLight value) {
    if(!source.is_object())throw std::runtime_error("Character lights must be objects.");
    value.enabled=source.value("enabled",value.enabled);value.direction=source.value("direction",value.direction);
    value.color=source.value("color",value.color);value.intensity=source.value("intensity",value.intensity);
    value.diffuse=source.value("diffuse",value.diffuse);value.specular=source.value("specular",value.specular);
    if(!validCharacterLight(value))throw std::runtime_error("Character light settings are invalid.");
    return value;
}
Json lightRigJson(const PlayerLightRigSettings& value) {
    const auto& local=value.localWorld;const auto& shadow=value.characterShadow;
    return {{"version",4},{"enabled",value.enabled},{"showGizmos",value.showGizmos},
        {"key",characterLightJson(value.key)},{"fill",characterLightJson(value.fill)},
        {"localWorld",{{"enabled",local.enabled},{"height",local.height},{"color",local.color},
            {"intensity",local.intensity},{"radius",local.radius},{"nearFalloffRadius",local.nearFalloffRadius},
            {"specularStrength",local.specularStrength},{"rangeFalloffExponent",local.rangeFalloffExponent},
            {"edgeSoftness",local.edgeSoftness},{"sourceRadius",local.sourceRadius},{"attenuation",local.attenuation},{"castShadows",local.castShadows},
            {"shadowDepthBias",local.shadowDepthBias},{"shadowNormalBias",local.shadowNormalBias}}},
        {"characterShadow",{{"enabled",shadow.enabled},{"strength",shadow.strength},
            {"distance",shadow.distance},{"shadowDepthBias",shadow.shadowDepthBias},{"shadowNormalBias",shadow.shadowNormalBias}}}};
}
// Read-only conversion data, never a runtime mode or writer representation.
struct FormerPointRigLight {
    bool enabled{true};Vec3 offset{-3,5,3},color{1,.96f,.9f};float intensity{35},radius{12};
    bool castShadows{true};float depthBias{.005f},normalBias{1};
};
FormerPointRigLight readFormerPointRigLight(const Json& source,FormerPointRigLight value) {
    if(!source.is_object())throw std::runtime_error("Former point rig lights must be objects.");
    value.enabled=source.value("enabled",value.enabled);value.offset=source.value("worldOffset",value.offset);
    value.color=source.value("color",value.color);value.intensity=source.value("intensity",value.intensity);
    value.radius=source.value("radius",value.radius);value.castShadows=source.value("castShadows",value.castShadows);
    value.depthBias=source.value("shadowDepthBias",value.depthBias);value.normalBias=source.value("shadowNormalBias",value.normalBias);
    if(!math::finite(value.offset)||std::any_of(value.offset.begin(),value.offset.end(),[](float v){return std::abs(v)>1000000;})||
        !validLightColor(value.color)||!validLightIntensity(value.intensity)||!validLightRange(value.radius)||
        !validShadowBiases(value.depthBias,value.normalBias)||
        (source.contains("roles")&&!validLightRoles(readRoles(source.at("roles")))))
        throw std::runtime_error("Former point rig light settings are invalid.");
    return value;
}
CharacterLight convertFormerPoint(const FormerPointRigLight& old) {
    CharacterLight value;value.enabled=old.enabled;value.color=old.color;
    value.direction=math::normalized(old.offset*-1.f,{-.5f,-.8f,-.6f});
    value.intensity=std::min(old.intensity*point_lighting::pointLightAttenuation(
        static_cast<float>(math::dot(old.offset,old.offset)),1.f/(old.radius*old.radius)),10000.f);
    return value;
}
float migratedPointIntensity(float irradiance,const LocalWorldLightSettings& light) {
    const float attenuation=point_lighting::pointLightAttenuation(light.height*light.height,
        1.f/(light.radius*light.radius),light.nearFalloffRadius*light.nearFalloffRadius,
        light.rangeFalloffExponent,light.edgeSoftness,light.attenuation);
    return static_cast<float>(std::min(double(irradiance)/std::max(double(attenuation),1e-12),1000000.));
}
PlayerLightRigSettings readLightRig(const Json& source,const CharacterRevealSettings& reveal,float characterHeight) {
    if(!source.is_object())throw std::runtime_error("Character light rig settings must be an object.");
    PlayerLightRigSettings value;
    value.enabled=source.value("enabled",value.enabled);value.showGizmos=source.value("showGizmos",value.showGizmos);
    if(source.contains("version")) {
        const auto& encodedVersion=source.at("version");
        if(!encodedVersion.is_number_integer()||encodedVersion<2||encodedVersion>4)
            throw std::runtime_error("Unsupported character light rig version.");
        const auto version=encodedVersion.get<int>();
        if(source.contains("key"))value.key=readCharacterLight(source.at("key"),value.key);
        if(source.contains("fill"))value.fill=readCharacterLight(source.at("fill"),value.fill);
        const char* worldKey=version==3?"worldFill":"localWorld";
        if(source.contains(worldKey)||version<4) {
            const Json empty=Json::object();const auto& old=source.contains(worldKey)?source.at(worldKey):empty;
            if(!old.is_object())throw std::runtime_error("Local World Light must be an object.");
            auto& local=value.localWorld;
            local.enabled=old.value("enabled",local.enabled);local.height=old.value("height",local.height);
            local.color=old.value("color",local.color);
            local.intensity=old.value("intensity",version==2?4.f:version==3?.64f:local.intensity);
            local.castShadows=old.value("castShadows",local.castShadows);
            local.shadowDepthBias=old.value("shadowDepthBias",local.shadowDepthBias);local.shadowNormalBias=old.value("shadowNormalBias",local.shadowNormalBias);
            if(version==3) {
                if(!validLocalWorldLightSettings(local)||!validCharacterRevealSettings(reveal)||
                    !std::isfinite(characterHeight)||characterHeight<=0)
                    throw std::runtime_error("Former World Fill settings are invalid.");
                local.radius=std::clamp(std::max(2.f*reveal.spread,1.25f*local.height),.01f,100000.f);
                const float center=characterLightingCenterHeight(characterHeight);
                const float footIrradiance=local.intensity*ambient_visibility::radialRevealWeightSquared(center*center,reveal.spread);
                local.intensity=migratedPointIntensity(footIrradiance,local);
            } else {
                local.radius=old.value("radius",local.radius);
                local.nearFalloffRadius=old.value("nearFalloffRadius",local.nearFalloffRadius);
                local.specularStrength=old.value("specularStrength",local.specularStrength);
                local.rangeFalloffExponent=version==2?4.f:old.value("rangeFalloffExponent",local.rangeFalloffExponent);
                local.edgeSoftness=version==2?1.f:old.value("edgeSoftness",local.edgeSoftness);
                local.sourceRadius=version==2?0.f:old.contains("sourceRadius")?old.at("sourceRadius").get<float>():old.value("softSourceRadius",0.f);
                local.attenuation=old.value("attenuation",2.f);
                if(version==2&&!validLightIntensity(local.intensity))throw std::runtime_error("Former Local World Light intensity is invalid.");
            }
        }
        if(source.contains("characterShadow")) {
            const auto& old=source.at("characterShadow");if(!old.is_object())throw std::runtime_error("Character Shadow must be an object.");
            auto& shadow=value.characterShadow;
            shadow.enabled=old.value("enabled",shadow.enabled);
            shadow.strength=old.value("strength",shadow.strength);shadow.distance=old.value("distance",shadow.distance);
            shadow.shadowDepthBias=old.value("shadowDepthBias",shadow.shadowDepthBias);shadow.shadowNormalBias=old.value("shadowNormalBias",shadow.shadowNormalBias);
        }
    } else if(source.contains("actorLights")) {
        const auto& lights=source.at("actorLights");
        if(!lights.is_array()||lights.size()!=3)throw std::runtime_error("Former directional rigs require three lights.");
        for(std::size_t index=0;index<lights.size();++index) {
            const auto converted=readCharacterLight(lights[index],CharacterLight{});
            if(index==0)value.key=converted;else if(index==1)value.fill=converted;
        }
        // This representation contained no world source. Do not create one on load.
        value.localWorld.enabled=false;
        value.characterShadow.enabled=value.key.enabled;
    } else {
        FormerPointRigLight key,fill;
        fill.enabled=false;fill.offset={3,3,-2};fill.color={.55f,.65f,1};fill.intensity=12;fill.radius=8;fill.castShadows=false;
        if(source.contains("key"))key=readFormerPointRigLight(source.at("key"),key);
        if(source.contains("fill"))fill=readFormerPointRigLight(source.at("fill"),fill);
        value.key=convertFormerPoint(key);value.fill=convertFormerPoint(fill);
        auto& local=value.localWorld;local.enabled=key.enabled;local.color=key.color;local.radius=key.radius;
        local.height=std::clamp(2.f,.01f,std::max(key.radius,.02f)*.5f);
        local.intensity=migratedPointIntensity(value.key.intensity,local);local.castShadows=key.castShadows;
        local.shadowDepthBias=key.depthBias;local.shadowNormalBias=key.normalBias;
        auto& shadow=value.characterShadow;shadow.enabled=key.enabled&&key.castShadows;
        shadow.shadowDepthBias=key.depthBias;shadow.shadowNormalBias=key.normalBias;
    }
    if(!validPlayerLightRigSettings(value))throw std::runtime_error("Character light rig settings are invalid.");
    return value;
}
template<class Light> Json lightJson(const Light& light) {
    Json result{{"color",light.color}, {"intensity",light.intensity}, {"enabled",light.enabled}};
    if constexpr(requires { light.radius; })result["radius"]=light.radius;
    if constexpr(requires { light.nearFalloffRadius; }) {
        result["nearFalloffRadius"]=light.nearFalloffRadius;result["specularStrength"]=light.specularStrength;
        result["rangeFalloffExponent"]=light.rangeFalloffExponent;result["edgeSoftness"]=light.edgeSoftness;
        result["sourceRadius"]=light.sourceRadius;
        result["attenuation"]=light.attenuation;
    }
    if constexpr(requires { light.shadowDistance; })result["shadowDistance"]=light.shadowDistance;
    result["roles"]=rolesJson(light.roles);
    if constexpr(requires { light.castShadows; }) {
        result["castShadows"]=light.castShadows;
        result["shadowDepthBias"]=light.shadowDepthBias;result["shadowNormalBias"]=light.shadowNormalBias;
    }
    return result;
}
template<class Light> Light readLight(const Json& json) {
    Light result;
    result.color = json.at("color").get<Vec3>();
    result.intensity = json.at("intensity").get<float>();
    result.enabled = json.at("enabled").get<bool>();
    if constexpr(requires { result.radius; })result.radius=json.value("radius",20.f);
    if constexpr(requires { result.nearFalloffRadius; }) {
        result.nearFalloffRadius=json.value("nearFalloffRadius",0.f);result.specularStrength=json.value("specularStrength",1.f);
        result.rangeFalloffExponent=json.value("rangeFalloffExponent",4.f);result.edgeSoftness=json.value("edgeSoftness",1.f);
        result.sourceRadius=json.contains("sourceRadius")?json.at("sourceRadius").get<float>():json.value("softSourceRadius",0.f);
        result.attenuation=json.value("attenuation",2.f);
    }
    if constexpr(requires { result.shadowDistance; })result.shadowDistance=json.value("shadowDistance",100.f);
    if(json.contains("roles"))result.roles=readRoles(json.at("roles"),result.roles);
    if constexpr(requires { result.castShadows; }) {
        result.castShadows=json.value("castShadows",false);
        result.shadowDepthBias=json.value("shadowDepthBias",.005f);result.shadowNormalBias=json.value("shadowNormalBias",1.f);
    }
    return result;
}
uint64_t fingerprint(std::string_view contents) {
    uint64_t result=14695981039346656037ull;
    for (const auto character:contents) { result^=static_cast<unsigned char>(character); result*=1099511628211ull; }
    return result;
}
std::string readText(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (!size || size > 64u*1024u*1024u) throw std::runtime_error("Scene file is empty or larger than 64 MiB.");
    std::ifstream input(path,std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read scene asset.");
    return std::string(std::istreambuf_iterator<char>(input),{});
}
Vec4 readCollisionColor(const Json& component,const Vec4& fallback) {
    const auto found=component.find("collisionColor");
    if(found==component.end())return fallback;
    // The first collision display documents stored RGB. Preserve their wire opacity.
    if(found->is_array() && found->size()==3) {
        const auto rgb=found->get<Vec3>();
        return {rgb[0],rgb[1],rgb[2],1.f};
    }
    return found->get<Vec4>();
}
SceneEnvironment readEnvironment(const Json& json,bool extended=false) {
    SceneEnvironment result;
    result.exposure=json.at("exposure").get<float>();
    const auto& environment=json.at("environment");
    for (const auto* field:{"map","enabled","intensity","rotation"}) (void)environment.at(field);
    result.environment=readEnvironmentSettings(environment,extended);
    if (extended && json.contains("sun")) result.sun=parseId(json.at("sun"));
    return result;
}
Json environmentJson(const SceneEnvironment& value) {
    return {{"exposure",value.exposure},{"sun",hexId(value.sun)},{"environment",environmentSettingsJson(value.environment)}};
}
bool validCamera(const Camera& camera) {
    return math::finite(camera.position) && math::finite(camera.target) && math::finite(camera.up) &&
        std::isfinite(camera.fov) && camera.fov>0 && camera.fov<180 && std::isfinite(camera.nearPlane) &&
        std::isfinite(camera.farPlane) && camera.nearPlane>0 && camera.farPlane>camera.nearPlane;
}
SceneSnapshot readScene(const std::filesystem::path& path) {
    const auto contents=readText(path);
    const Json json=Json::parse(contents);
    const auto version=json.at("version").get<int>();
    if ((version<1 || version>15) || json.at("type") != "crimson-scene")
        throw std::runtime_error("Unsupported scene file type or version.");
    SceneSnapshot result; result.fingerprint=fingerprint(contents);
    result.name = json.at("name").get<std::string>();
    if (result.name.empty() || result.name.size() > 1024) throw std::runtime_error("Scene name is invalid.");
    const auto& settings = json.at("settings");
    if (version>=2) result.properties.activeEnvironment=parseId(settings.at("activeEnvironment"));
    if (version>=4) {
        result.properties.activeCamera=parseId(settings.at("activeCamera"));
        result.properties.playerCharacter=parseId(settings.at("playerCharacter"));
    }
    const auto& entities = json.at("entities");
    if (!entities.is_array() || entities.size() > 100000) throw std::runtime_error("Invalid scene entity array.");
    result.records.reserve(entities.size());
    for (const auto& entity : entities) {
        EntityRecord record;
        record.id = parseId(entity.at("id"));
        record.name = entity.at("name").get<std::string>();
        record.parent = parseId(entity.at("parent"));
        const auto& order = entity.at("order");
        if (!order.is_number_unsigned() || order.get<uint64_t>() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("Scene has an invalid sibling order.");
        record.order = order.get<uint32_t>();
        record.local = entity.at("local").get<Mat4>();
        if(version>=14)record.visible=entity.value("visible",true);
        if (entity.contains("model")) record.model=entity.at("model").get<AssetRef<ModelAsset>>();
        if(version>=10 && entity.contains("modelAnimation")) {
            const auto& source=entity.at("modelAnimation");
            if(!source.is_object())throw std::runtime_error("Model animation settings must be an object.");
            auto& animation=record.modelAnimation;
            if(source.contains("clip")) {
                const auto& clip=source.at("clip");
                if(!clip.is_number_integer() ||
                    (clip.is_number_unsigned()?clip.get<std::uint64_t>()>std::uint64_t(std::numeric_limits<std::int32_t>::max()):
                        clip.get<std::int64_t>()<-1 || clip.get<std::int64_t>()>std::numeric_limits<std::int32_t>::max()))
                    throw std::runtime_error("Model animation clip must be an integer from -1 to 2147483647.");
                animation.clip=clip.get<std::int32_t>();
            }
            animation.playing=source.value("playing",animation.playing);animation.loop=source.value("loop",animation.loop);
            animation.speed=source.value("speed",animation.speed);animation.startTime=source.value("startTime",animation.startTime);
        }
        if(version>=11 && entity.contains("modelMaterials")) {
            const auto& materials=entity.at("modelMaterials");
            if(!materials.is_array() || materials.size()>MaximumModelMaterialSlots)
                throw std::runtime_error("Model materials must be a supported slot array.");
            record.modelMaterials=materials.get<ModelMaterialOverrides>();
            trimModelMaterialOverrides(record.modelMaterials);
        } else if(entity.contains("modelMaterial"))
            setModelMaterialOverride(record.modelMaterials,0,entity.at("modelMaterial").get<AssetRef<MaterialAsset>>());
        record.castShadows=entity.value("castShadows",true);record.receiveShadows=entity.value("receiveShadows",true);
        if(version>=13)record.fadeWhenOccluding=entity.value("fadeWhenOccluding",false);
        if(entity.contains("lightingScopes")) {
            const auto& scopes=entity.at("lightingScopes");
            if(!scopes.is_object())throw std::runtime_error("Surface lighting scopes must be an object.");
            record.lightingScopes.directReceiverMask=readLightingMask(scopes,"directReceiverMask",lighting_scope::All);
            record.lightingScopes.shadowCasterMask=readLightingMask(scopes,"shadowCasterMask",lighting_scope::All);
            record.lightingScopes.ambientReceiverMask=readLightingMask(scopes,"ambientReceiverMask",lighting_scope::All);
        }
        if (entity.contains("directional")) record.directional = readLight<SceneDirectionalLight>(entity.at("directional"));
        if (entity.contains("point")) record.point = readLight<ScenePointLight>(entity.at("point"));
        if (version>=2 && entity.contains("environment")) record.environment=readEnvironment(entity.at("environment"),version>=6);
        if (version>=3 && entity.contains("particle")) record.particle=entity.at("particle").get<AssetRef<ParticleEffectAsset>>();
        if (version>=4 && entity.contains("character")) {
            const auto& c=entity.at("character");
            record.character=SceneCharacter{c.at("height").get<float>(),c.at("radius").get<float>(),c.at("moveSpeed").get<float>()};
            record.character->showRoute=c.value("showRoute",true);
            record.character->showCollisionInGame=c.value("showCollisionInGame",false);
            record.character->collisionColor=readCollisionColor(c,record.character->collisionColor);
            if(c.contains("darknessReveal")) {
                const auto& reveal=c.at("darknessReveal");
                if(!reveal.is_object())throw std::runtime_error("Character darkness reveal settings must be an object.");
                if(reveal.contains("version")&&reveal.at("version")!=1&&reveal.at("version")!=2)
                    throw std::runtime_error("Unsupported Character darkness reveal version.");
                record.character->darknessReveal.enabled=reveal.value("enabled",true);
                record.character->darknessReveal.spread=reveal.contains("version")&&reveal.at("version")==2?
                    reveal.value("spread",8.f):reveal.value("radius",8.f);
            } else if(c.contains("lightRig")) {
                const auto& old=c.at("lightRig");
                if(!old.contains("actorLights")&&old.value("enabled",false)) {
                    if(old.contains("reveal")&&old.at("reveal").is_object()&&old.at("reveal").value("enabled",false))
                        record.character->darknessReveal.spread=old.at("reveal").value("radius",8.f);
                    else if(old.contains("key")&&old.at("key").is_object())
                        record.character->darknessReveal.spread=old.at("key").value("radius",8.f);
                }
            }
            if(c.contains("lightRig"))record.character->lightRig=readLightRig(c.at("lightRig"),record.character->darknessReveal,record.character->height);
        }
        if (version>=4 && entity.contains("collider")) {
            const auto& c=entity.at("collider");
            record.collider.emplace();
            record.collider->size=c.value("size",record.collider->size);
            if(version>=12) {
                const auto shape=c.at("shape").get<std::string>();
                if(shape=="box")record.collider->shape=ColliderShape::Box;
                else if(shape=="sphere")record.collider->shape=ColliderShape::Sphere;
                else if(shape=="cylinder")record.collider->shape=ColliderShape::Cylinder;
                else if(shape=="capsule")record.collider->shape=ColliderShape::Capsule;
                else if(shape=="trimesh")record.collider->shape=ColliderShape::Trimesh;
                else throw std::runtime_error("Unknown collider shape.");
                record.collider->radius=c.value("radius",record.collider->radius);
                record.collider->height=c.value("height",record.collider->height);
                if(c.contains("source"))record.collider->source=c.at("source").get<AssetRef<ModelAsset>>();
                if(version>=15 && c.contains("mesh")) {
                    const auto& mesh=c.at("mesh");
                    if(!mesh.is_object() || !mesh.at("positions").is_array() || mesh.at("positions").size()>4u*1024u*1024u ||
                       !mesh.at("indices").is_array() || mesh.at("indices").size()>12u*1024u*1024u)
                        throw std::runtime_error("Native collider mesh exceeds its storage limits.");
                    for(const auto& index:mesh.at("indices"))if(!index.is_number_integer() || index.get<std::uint64_t>()>UINT32_MAX)
                        throw std::runtime_error("Native collider mesh indices must be nonnegative 32-bit integers.");
                    record.collider->mesh=makeCollisionMesh(mesh.at("positions").get<std::vector<Vec3>>(),mesh.at("indices").get<std::vector<std::uint32_t>>());
                }
            }
            record.collider->showCollisionInGame=c.value("showCollisionInGame",false);
            record.collider->collisionColor=readCollisionColor(c,record.collider->collisionColor);
        }
        if (version>=4 && entity.contains("camera")) {
            const auto& c=entity.at("camera");
            record.camera=SceneCamera{c.at("verticalFov").get<float>(),c.at("near").get<float>(),c.at("far").get<float>(),
                parseId(c.at("followTarget")),c.at("followOffset").get<Vec3>()};
        }
        if(version>=9 && version<=11 && entity.contains("meshCollider")) {
            if(record.collider)throw std::runtime_error("Legacy scene contains both Box and mesh collision on one entity.");
            const auto& c=entity.at("meshCollider");record.collider.emplace();record.collider->shape=ColliderShape::Trimesh;
            record.collider->source=c.at("source").get<AssetRef<ModelAsset>>();
            record.collider->showCollisionInGame=c.value("showCollisionInGame",false);
            record.collider->collisionColor=readCollisionColor(c,record.collider->collisionColor);
        }
        if(version>=9 && entity.contains("quader")) {
            const auto& q=entity.at("quader");record.quader=QuaderLink{q.value("projectPath",std::string{}),
                q.value("projectId",std::string{}),q.value("sceneId",std::string{}),q.value("importScale",1.f),
                q.value("autoSync",true),q.value("refreshMaterials",false)};
            record.quader->saveKey=q.value("saveKey",std::string{});
            record.quader->syncLights=q.value("syncLights",true);
        }
        if(version>=9 && entity.contains("quaderGenerated")) {
            const auto& q=entity.at("quaderGenerated");record.quaderGenerated=QuaderGenerated{parseId(q.at("owner")),q.at("sourceKey").get<std::string>()};
        }
        result.records.push_back(std::move(record));
    }
    if (version==1) {
        // The old scene-level settings become one ordinary entity. Choosing the
        // first unused positive ID makes reopening old files deterministic.
        EntityRecord environment; environment.id=1; environment.name="Environment";
        while (std::any_of(result.records.begin(),result.records.end(),[&](const auto& record){return record.id==environment.id;})) ++environment.id;
        environment.order=nextOrder(result.records,NoEntity);
        environment.environment=readEnvironment(settings);
        result.properties.activeEnvironment=environment.id;
        result.records.push_back(std::move(environment));
    }
    // The optional visual sun reference never prevents an otherwise valid
    // scene from opening after its target was removed or changed externally.
    std::unordered_set<EntityId> directionalIds;
    for (const auto& record:result.records) if(record.directional) directionalIds.insert(record.id);
    for (auto& record:result.records) if(record.environment && !directionalIds.contains(record.environment->sun))
        record.environment->sun=NoEntity;
    if (json.contains("previewCamera")) {
        const auto& source=json.at("previewCamera"); Camera camera;
        camera.position=source.at("position").get<Vec3>(); camera.target=source.at("target").get<Vec3>();
        camera.up=source.at("up").get<Vec3>(); camera.fov=source.at("fov").get<float>();
        camera.nearPlane=source.at("near").get<float>(); camera.farPlane=source.at("far").get<float>();
        if (!validCamera(camera)) throw std::runtime_error("Scene preview camera is invalid.");
        result.previewCamera=camera;
    }
    Scene::validate(result.records,result.properties);
    return result;
}
void canonicalizeSceneAssetRefs(SceneSnapshot& snapshot) {
    snapshot.id=canonicalAssetId(snapshot.id);
    for(auto& record:snapshot.records) {
        if(record.model)record.model->id=canonicalAssetId(record.model->id);
        for(auto& material:record.modelMaterials)material.id=canonicalAssetId(material.id);
        if(record.particle)record.particle->id=canonicalAssetId(record.particle->id);
        if(record.collider)record.collider->source.id=canonicalAssetId(record.collider->source.id);
        if(record.environment)record.environment->environment.map=canonicalAssetId(record.environment->environment.map);
    }
}
Json writeScene(const SceneSnapshot& snapshot) {
    Scene::validate(snapshot.records,snapshot.properties);
    Json entities = Json::array();
    for (const auto& record : snapshot.records) {
        Json entity{{"id",hexId(record.id)}, {"name",record.name}, {"parent",hexId(record.parent)},
            {"order",record.order}, {"local",record.local}, {"visible",record.visible}};
        if (record.model) {
            entity["model"] = *record.model;
            entity["castShadows"]=record.castShadows;entity["receiveShadows"]=record.receiveShadows;
            entity["fadeWhenOccluding"]=record.fadeWhenOccluding;
            entity["lightingScopes"]={{"directReceiverMask",record.lightingScopes.directReceiverMask},
                {"shadowCasterMask",record.lightingScopes.shadowCasterMask},{"ambientReceiverMask",record.lightingScopes.ambientReceiverMask}};
            const auto& animation=record.modelAnimation;
            entity["modelAnimation"]={{"clip",animation.clip},{"playing",animation.playing},{"loop",animation.loop},
                {"speed",animation.speed},{"startTime",animation.startTime}};
        }
        auto materials=record.modelMaterials;trimModelMaterialOverrides(materials);
        if(!materials.empty())entity["modelMaterials"]=materials;
        if (record.particle) entity["particle"] = *record.particle;
        if (record.directional) entity["directional"] = lightJson(*record.directional);
        if (record.point) entity["point"] = lightJson(*record.point);
        if (record.environment) entity["environment"]=environmentJson(*record.environment);
        if (record.character) { const auto& c=*record.character;entity["character"]={{"height",c.height},{"radius",c.radius},{"moveSpeed",c.moveSpeed},
            {"showRoute",c.showRoute},{"showCollisionInGame",c.showCollisionInGame},{"collisionColor",c.collisionColor},{"lightRig",lightRigJson(c.lightRig)},
            {"darknessReveal",{{"version",2},{"enabled",c.darknessReveal.enabled},{"spread",c.darknessReveal.spread}}}}; }
        if (record.collider) { const auto& c=*record.collider;
            const auto shape=c.shape==ColliderShape::Box?"box":c.shape==ColliderShape::Sphere?"sphere":c.shape==ColliderShape::Cylinder?"cylinder":c.shape==ColliderShape::Capsule?"capsule":"trimesh";
            entity["collider"]={{"shape",shape},{"size",c.size},{"radius",c.radius},{"height",c.height},{"source",c.source},
                {"showCollisionInGame",c.showCollisionInGame},{"collisionColor",c.collisionColor}};
            if(c.mesh)entity["collider"]["mesh"]={{"positions",c.mesh->positions},{"indices",c.mesh->indices}}; }
        if (record.camera) { const auto& c=*record.camera;entity["camera"]={{"verticalFov",c.verticalFov},{"near",c.nearPlane},{"far",c.farPlane},
            {"followTarget",hexId(c.followTarget)},{"followOffset",c.followOffset}}; }
        if(record.quader) {const auto& q=*record.quader;entity["quader"]={{"projectPath",q.projectPath},{"projectId",q.projectId},
            {"sceneId",q.sceneId},{"importScale",q.importScale},{"autoSync",q.autoSync},{"refreshMaterials",q.refreshMaterials},
            {"saveKey",q.saveKey},{"syncLights",q.syncLights}};}
        if(record.quaderGenerated) {const auto& q=*record.quaderGenerated;entity["quaderGenerated"]={{"owner",hexId(q.owner)},{"sourceKey",q.sourceKey}};}
        entities.push_back(std::move(entity));
    }
    Json result{{"version",15},{"type","crimson-scene"},{"name",snapshot.name},{"entities",std::move(entities)},
        {"settings",{{"activeEnvironment",hexId(snapshot.properties.activeEnvironment)},
            {"activeCamera",hexId(snapshot.properties.activeCamera)},{"playerCharacter",hexId(snapshot.properties.playerCharacter)}}}};
    if(isAssetUuid(snapshot.id))result["id"]=snapshot.id;
    if (snapshot.previewCamera) {
        if (!validCamera(*snapshot.previewCamera)) throw std::runtime_error("Scene preview camera is invalid.");
        const auto& camera=*snapshot.previewCamera;
        result["previewCamera"]={{"position",camera.position},{"target",camera.target},{"up",camera.up},
            {"fov",camera.fov},{"near",camera.nearPlane},{"far",camera.farPlane}};
    }
    return result;
}
enum EntityField : uint32_t {
    Name=1u<<0, Parent=1u<<1, Order=1u<<2, Local=1u<<3, Model=1u<<4, Particle=1u<<5,
    Directional=1u<<6, Point=1u<<7, Environment=1u<<8, Character=1u<<9, Collider=1u<<10, CameraField=1u<<11,
    ModelMaterialField=1u<<12, CastShadows=1u<<13, ReceiveShadows=1u<<14,
    QuaderField=1u<<15,QuaderGeneratedField=1u<<16,LightingScopesField=1u<<17,
    ModelAnimationField=1u<<18,FadeWhenOccludingField=1u<<19,VisibleField=1u<<20,AllEntityFields=(1u<<21)-1
};
enum PropertyField : uint32_t { ActiveEnvironment=1u<<0,ActiveCamera=1u<<1,PlayerCharacter=1u<<2 };
template<class T> bool equalLight(const std::optional<T>& a,const std::optional<T>& b) {
    return a==b;
}
uint32_t changedFields(const EntityRecord& a,const EntityRecord& b) {
    uint32_t fields=0;
    if(a.name!=b.name)fields|=Name;if(a.parent!=b.parent)fields|=Parent;if(a.order!=b.order)fields|=Order;
    if(a.local!=b.local)fields|=Local;if(a.model!=b.model)fields|=Model;if(a.particle!=b.particle)fields|=Particle;
    if(!equalLight(a.directional,b.directional))fields|=Directional;if(!equalLight(a.point,b.point))fields|=Point;
    if(a.environment.has_value()!=b.environment.has_value() || (a.environment && !sameEnvironment(*a.environment,*b.environment)))fields|=Environment;
    if(a.character!=b.character)fields|=Character;if(a.collider!=b.collider)fields|=Collider;if(a.camera!=b.camera)fields|=CameraField;
    if(a.modelMaterials!=b.modelMaterials)fields|=ModelMaterialField;
    if(a.castShadows!=b.castShadows)fields|=CastShadows;if(a.receiveShadows!=b.receiveShadows)fields|=ReceiveShadows;
    if(a.quader!=b.quader)fields|=QuaderField;
    if(a.quaderGenerated!=b.quaderGenerated)fields|=QuaderGeneratedField;
    if(a.lightingScopes!=b.lightingScopes)fields|=LightingScopesField;
    if(a.modelAnimation!=b.modelAnimation)fields|=ModelAnimationField;
    if(a.fadeWhenOccluding!=b.fadeWhenOccluding)fields|=FadeWhenOccludingField;
    if(a.visible!=b.visible)fields|=VisibleField;
    return fields;
}
void copyFields(EntityRecord& target,const EntityRecord& source,uint32_t fields) {
    if(fields&Name)target.name=source.name;if(fields&Parent)target.parent=source.parent;if(fields&Order)target.order=source.order;
    if(fields&Local)target.local=source.local;if(fields&Model)target.model=source.model;if(fields&Particle)target.particle=source.particle;
    if(fields&Directional)target.directional=source.directional;if(fields&Point)target.point=source.point;
    if(fields&Environment)target.environment=source.environment;if(fields&Character)target.character=source.character;
    if(fields&Collider)target.collider=source.collider;if(fields&CameraField)target.camera=source.camera;
    if(fields&ModelMaterialField)target.modelMaterials=source.modelMaterials;
    if(fields&CastShadows)target.castShadows=source.castShadows;if(fields&ReceiveShadows)target.receiveShadows=source.receiveShadows;
    if(fields&QuaderField)target.quader=source.quader;
    if(fields&QuaderGeneratedField)target.quaderGenerated=source.quaderGenerated;
    if(fields&LightingScopesField)target.lightingScopes=source.lightingScopes;
    if(fields&ModelAnimationField)target.modelAnimation=source.modelAnimation;
    if(fields&FadeWhenOccludingField)target.fadeWhenOccluding=source.fadeWhenOccluding;
    if(fields&VisibleField)target.visible=source.visible;
}
uint32_t changedProperties(const SceneProperties& a,const SceneProperties& b) {
    return (a.activeEnvironment!=b.activeEnvironment?ActiveEnvironment:0u) |
        (a.activeCamera!=b.activeCamera?ActiveCamera:0u) | (a.playerCharacter!=b.playerCharacter?PlayerCharacter:0u);
}
void copyProperties(SceneProperties& target,const SceneProperties& source,uint32_t fields) {
    if(fields&ActiveEnvironment)target.activeEnvironment=source.activeEnvironment;
    if(fields&ActiveCamera)target.activeCamera=source.activeCamera;
    if(fields&PlayerCharacter)target.playerCharacter=source.playerCharacter;
}
SceneCommand difference(const std::string& label, const std::vector<EntityRecord>& before,
                        const std::vector<EntityRecord>& after, const SceneProperties& beforeProperties,
                        const SceneProperties& afterProperties, const SceneSelection& beforeSelection,
                        const SceneSelection& afterSelection) {
    SceneCommand command;
    command.label = label;
    command.beforeSelection = beforeSelection;
    command.afterSelection = afterSelection;
    std::unordered_map<EntityId,const EntityRecord*> previous;
    for (const auto& record : before) previous.emplace(record.id,&record);
    for (const auto& record : after) {
        const auto found = previous.find(record.id);
        if (found == previous.end()) command.entities.push_back({record.id,std::nullopt,record,AllEntityFields});
        else {
            if (const auto fields=changedFields(*found->second,record)) command.entities.push_back({record.id,*found->second,record,fields});
            previous.erase(found);
        }
    }
    for (const auto& [id,record] : previous) command.entities.push_back({id,*record,std::nullopt,AllEntityFields});
    std::sort(command.entities.begin(),command.entities.end(),[](const auto& a,const auto& b) { return a.id < b.id; });
    if (!sameProperties(beforeProperties,afterProperties)) {
        command.beforeProperties = beforeProperties; command.afterProperties = afterProperties;
        command.propertyFields=changedProperties(beforeProperties,afterProperties);
    }
    return command;
}
void mergeCommand(SceneCommand& accumulated,const SceneCommand& step) {
    for(const auto& next:step.entities) {
        auto current=std::find_if(accumulated.entities.begin(),accumulated.entities.end(),[&](const auto& change){return change.id==next.id;});
        if(current==accumulated.entities.end()){accumulated.entities.push_back(next);continue;}
        if(!next.after) {
            if(!current->before){accumulated.entities.erase(current);continue;}
            auto restored=*next.before;copyFields(restored,*current->before,current->fields);
            current->before=std::move(restored);current->after.reset();current->fields=AllEntityFields;
        } else if(!next.before || !current->after) {
            current->after=next.after;current->fields=current->before?changedFields(*current->before,*current->after):AllEntityFields;
        } else {
            if(current->before)copyFields(*current->before,*next.before,next.fields&~current->fields);
            copyFields(*current->after,*next.after,next.fields);current->fields|=next.fields;
        }
    }
    if(step.beforeProperties) {
        if(!accumulated.beforeProperties){accumulated.beforeProperties=step.beforeProperties;accumulated.afterProperties=step.afterProperties;}
        else {
            copyProperties(*accumulated.beforeProperties,*step.beforeProperties,step.propertyFields&~accumulated.propertyFields);
            copyProperties(*accumulated.afterProperties,*step.afterProperties,step.propertyFields);
        }
        accumulated.propertyFields|=step.propertyFields;
    }
    accumulated.afterSelection=step.afterSelection;
}
void pruneCommand(SceneCommand& command) {
    for(auto& change:command.entities) if(change.before && change.after)change.fields&=changedFields(*change.before,*change.after);
    std::erase_if(command.entities,[](const auto& change){return !change.fields;});
    if(command.beforeProperties && command.afterProperties)command.propertyFields&=changedProperties(*command.beforeProperties,*command.afterProperties);
    if(!command.propertyFields){command.beforeProperties.reset();command.afterProperties.reset();}
}
void updateFollowOffsets(const Scene& previous,std::vector<EntityRecord>& records,const SceneProperties& properties) {
    if(std::none_of(records.begin(),records.end(),[](const auto& record){return record.camera && record.camera->followTarget;}))return;
    Scene::validate(records,properties);
    std::unordered_map<EntityId,const EntityRecord*> indexed;
    for(const auto& record:records)indexed.emplace(record.id,&record);
    const auto rawWorld=[&](EntityId id) {
        Mat4 world;
        while(id){const auto& record=*indexed.at(id);world=record.local*world;id=record.parent;}
        return world;
    };
    for(auto& record:records) {
        if(!record.camera || !record.camera->followTarget)continue;
        const auto before=previous.find(record.id);
        if(!before || !before->camera || before->camera->followTarget!=record.camera->followTarget ||
            before->camera->followOffset!=record.camera->followOffset)continue;
        // Follow owns world translation. An ancestor rotation/scale must not
        // rotate its world-space offset; only a deliberate camera position edit
        // changes that offset. World-preserving reparenting retains it too.
        if(before->parent!=record.parent || math::transformPoint(before->local,{0,0,0})==math::transformPoint(record.local,{0,0,0}))continue;
        const auto oldWorld=previous.worldTransform(record.id);
        if(!oldWorld)continue;
        const auto delta=math::transformPoint(rawWorld(record.id),{0,0,0})-math::transformPoint(*oldWorld,{0,0,0});
        if(delta==Vec3{})continue;
        const auto oldTarget=previous.worldTransform(record.camera->followTarget);
        const auto targetDelta=oldTarget?math::transformPoint(rawWorld(record.camera->followTarget),{0,0,0})-math::transformPoint(*oldTarget,{0,0,0}):Vec3{};
        record.camera->followOffset=record.camera->followOffset+delta-targetDelta;
    }
}
}

std::filesystem::path sceneAssetPath(const std::filesystem::path& root,const std::string& id) { return scenePath(root,id); }
SceneSnapshot readSceneSnapshot(const std::filesystem::path& root,const std::string& id) {
    auto result=readScene(scenePath(root,id)); result.id=id;
    if(contentStorageReady(root))canonicalizeSceneAssetRefs(result);
    return result;
}
std::string serializeSceneSnapshot(const SceneSnapshot& snapshot) {
    if (snapshot.name.empty() || snapshot.name.size()>1024 || snapshot.name.find_first_not_of(" \t\r\n")==std::string::npos)
        throw std::runtime_error("Scene name must contain 1 to 1024 UTF-8 bytes.");
    auto current=snapshot;
    if(isAssetUuid(current.id))canonicalizeSceneAssetRefs(current);
    auto contents=writeScene(current).dump(2)+"\n";
    if(contents.size()>64u*1024u*1024u)throw std::runtime_error("Scene file exceeds the 64 MiB storage limit.");
    return contents;
}
void saveSceneSnapshot(const std::filesystem::path& root,const std::string& id,const SceneSnapshot& snapshot) {
    const auto file=scenePath(root,id);
    auto current=snapshot;current.id=id;
    if(contentStorageReady(root))canonicalizeSceneAssetRefs(current);
    writeFileAtomically(file,serializeSceneSnapshot(current));
    if(contentStorageReady(root))registerAssetPath(root,canonicalAssetId(id),file);
}
uint64_t sceneFileFingerprint(const std::filesystem::path& root,const std::string& id) { return fingerprint(readText(scenePath(root,id))); }
uint64_t sceneTextFingerprint(std::string_view contents) noexcept { return fingerprint(contents); }

std::vector<SceneAssetEntry> scanSceneAssets(const std::filesystem::path& root, std::vector<std::string>* diagnostics) {
    std::vector<SceneAssetEntry> entries;
    try {
        std::vector<StoredAssetPath> files;
        if(contentStorageReady(root))files=storedAssetPaths(root);
        else {
            const auto directory = root / "assets/scenes";
            if (!std::filesystem::exists(directory)) return entries;
            for(const auto& entry:std::filesystem::directory_iterator(directory))
                if(entry.is_regular_file())files.push_back({"assets/scenes/"+utf8(entry.path().filename()),entry.path()});
        }
        for (const auto& entry : files) {
            if (entry.file.extension() != ".crimson-scene" || !std::filesystem::is_regular_file(entry.file)) continue;
            const auto& id = entry.id;
            try {const auto scene=readSceneSnapshot(root,id);entries.push_back({scene.id,scene.name});}
            catch (const std::exception& error) { if (diagnostics) diagnostics->push_back(id + ": " + error.what()); }
        }
    } catch (const std::exception& error) { if (diagnostics) diagnostics->push_back(error.what()); }
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    });
    return entries;
}
std::string availableSceneName(const std::filesystem::path& root,const std::string& base) {
    std::unordered_set<std::string> names;
    for(const auto& entry:scanSceneAssets(root))names.insert(sceneNameKey(entry.name));
    std::string candidate=base;
    for(size_t suffix=2;names.contains(sceneNameKey(candidate));++suffix)candidate=base+" "+std::to_string(suffix);
    return candidate;
}
bool deleteSceneAsset(const std::filesystem::path& root, const std::string& id, std::string& error) {
    try {
        const auto path = scenePath(root,id);
        if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Scene asset no longer exists.");
        if (!std::filesystem::remove(path)) throw std::runtime_error("Could not delete scene asset.");
        error.clear();
        return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}

std::string UndoRedoManager::undoLabel() const { return canUndo() ? commands_[cursor_-1].label : std::string{}; }
std::string UndoRedoManager::redoLabel() const { return canRedo() ? commands_[cursor_].label : std::string{}; }
void UndoRedoManager::reset(bool saved) {
    commands_.clear(); cursor_ = 0;
    state_ = nextState_++;
    savedState_ = saved ? state_ : 0;
}
void UndoRedoManager::push(SceneCommand command) {
    command.beforeState = state_;
    command.afterState = nextState_++;
    commands_.erase(commands_.begin()+static_cast<std::ptrdiff_t>(cursor_),commands_.end());
    commands_.push_back(std::move(command));
    state_ = commands_.back().afterState;
    if (commands_.size() > MaximumEntries) commands_.erase(commands_.begin());
    cursor_ = commands_.size();
}

SceneDocument::SceneDocument(std::filesystem::path projectRoot) : projectRoot_(std::move(projectRoot)) { newScene(); }
void SceneDocument::sanitizeSelection() {
    std::unordered_set<EntityId> seen;
    std::erase_if(selection_.ids,[&](EntityId id) { return !scene_.contains(id) || !seen.insert(id).second; });
    if (std::find(selection_.ids.begin(),selection_.ids.end(),selection_.primary) == selection_.ids.end())
        selection_.primary = selection_.ids.empty() ? NoEntity : selection_.ids.back();
}
void SceneDocument::select(std::vector<EntityId> ids, EntityId primary) {
    if (transaction_) return;
    selection_ = {std::move(ids),primary}; sanitizeSelection();
}
bool SceneDocument::dirty() const {
    if (history_.dirty()) return true;
    if (!transaction_) return false;
    const auto& command=transaction_->command;
    if(command.beforeProperties && (changedProperties(*command.beforeProperties,*command.afterProperties)&command.propertyFields))return true;
    return std::any_of(command.entities.begin(),command.entities.end(),[](const auto& change){
        return !change.before || !change.after || (changedFields(*change.before,*change.after)&change.fields)!=0;
    });
}
bool SceneDocument::resetTransientFrom(const SceneDocument& source) {
    if(&source==this){status_="Cannot clone a runtime document into itself.";return false;}
    try {
        auto candidate=std::make_unique<Scene>();candidate->apply(source.scene().records(),source.scene().properties());
        auto selection=source.selection_;auto id=source.assetId_;auto name=source.name_;
        scene_.adoptPrepared(*candidate);selection_=std::move(selection);assetId_=std::move(id);name_=std::move(name);
        previewCamera_=source.previewCamera_;loadedFingerprint_=source.loadedFingerprint_;transaction_.reset();history_.reset(true);
        transient_=true;++documentGeneration_;status_.clear();return true;
    }catch(const std::exception& error){status_=error.what();return false;}
}
bool SceneDocument::applyRuntimePoses(std::span<const ScenePoseUpdate> poses) {
    if(!transient_){status_="Simulation poses require a transient Play document.";return false;}
    try{scene_.applyRuntimePoses(poses);return true;}catch(const std::exception& error){status_=error.what();return false;}
}
bool SceneDocument::beginTransform(const std::string& label,std::vector<EntityId> roots) {
    if(!begin(label))return false;
    setTransformEditRoots(std::move(roots));return true;
}
void SceneDocument::setTransformEditRoots(std::vector<EntityId> roots) {
    if(transaction_)transaction_->transformRoots=scene_.topmost(roots);
}
const std::vector<EntityId>& SceneDocument::transformEditRoots() const {
    static const std::vector<EntityId> empty;
    return transaction_?transaction_->transformRoots:empty;
}
void SceneDocument::newScene() {
    scene_.apply({},SceneProperties{});
    history_.reset(false); transaction_.reset(); selection_ = {};
    assetId_.clear(); name_ = "Untitled"; status_ = "New unsaved scene.";
    previewCamera_.reset(); loadedFingerprint_=0;
    ++documentGeneration_;
}
bool SceneDocument::load(const std::string& id) {
    try {
        return openPreparedSnapshot(readSceneSnapshot(projectRoot_,id));
    } catch (const std::exception& error) { status_ = "Could not open scene: " + std::string(error.what()); return false; }
}
bool SceneDocument::openPreparedSnapshot(const SceneSnapshot& snapshot) {
    return applyPreparedSnapshot(snapshot,false,true);
}
bool SceneDocument::save(const std::string& name, bool saveAs, bool overwriteExternal) {
    if(transient_){status_="Stop Play before saving a scene.";return false;}
    if (transaction_ && !commit()) return false;
    try {
        std::string savedName = name.empty() ? name_ : name;
        if (savedName.empty() || savedName.size() > 1024 || savedName.find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::runtime_error("Enter a scene name containing 1 to 1024 UTF-8 bytes.");
        // Existing colliding names remain editable; only new identities and
        // explicit renames must choose an unambiguous catalog name.
        if(assetId_.empty()||saveAs||sceneNameKey(savedName)!=sceneNameKey(name_)) {
            const auto key=sceneNameKey(savedName);
            for(const auto& entry:scanSceneAssets(projectRoot_))
                if((saveAs||entry.id!=assetId_)&&sceneNameKey(entry.name)==key)
                    throw std::runtime_error("A scene named '"+entry.name+"' already exists. Choose another name.");
        }
        std::string savedId = contentStorageReady(projectRoot_)?canonicalAssetId(assetId_):assetId_;std::filesystem::path file;
        if (savedId.empty() || saveAs) {
            if(contentStorageReady(projectRoot_)) {
                savedId=newAssetId();std::string filename=savedName;
                try {(void)contentFilename(filename+".crimson-scene");}catch(const std::exception&){filename=savedId;}
                for(std::size_t suffix=1;;++suffix) {
                    const auto name=filename+(suffix>1?" "+std::to_string(suffix):std::string{})+".crimson-scene";
                    file=checkedContentPath(projectRoot_,projectRoot_/"Content"/fromUtf8(name));
                    if(!std::filesystem::exists(file))break;
                }
            } else {
                do { savedId = "assets/scenes/" + uniqueToken() + ".crimson-scene"; }
                while (std::filesystem::exists(scenePath(projectRoot_,savedId)));
            }
        }
        if (!saveAs && !assetId_.empty() && !overwriteExternal && externalFileChanged())
            throw std::runtime_error("The scene file changed outside the editor. Reload, Save As, or explicitly overwrite it.");
        const SceneSnapshot snapshot{savedId,savedName,scene_.records(),scene_.properties(),previewCamera_};
        const auto contents=serializeSceneSnapshot(snapshot);
        if(file.empty())file=scenePath(projectRoot_,savedId);
        writeFileAtomically(file,contents);
        if(contentStorageReady(projectRoot_))registerAssetPath(projectRoot_,savedId,file);
        assetId_ = std::move(savedId); name_ = std::move(savedName);
        history_.savedState_ = history_.state_;
        loadedFingerprint_=fingerprint(contents);
        status_ = "Saved scene: " + name_;
        return true;
    } catch (const std::exception& error) { status_ = "Could not save scene: " + std::string(error.what()); return false; }
}
bool SceneDocument::externalFileChanged() const {
    if (assetId_.empty()) return false;
    try { return sceneFileFingerprint(projectRoot_,assetId_)!=loadedFingerprint_; }
    catch (...) { return true; }
}
std::vector<std::string> SceneDocument::historicalModelReferences() const {
    std::unordered_set<std::string> references;
    const auto add=[&](const EntityRecord& record) {
        if (record.model) references.insert(record.model->id);
        if(record.collider && !record.collider->source.empty())references.insert(record.collider->source.id);
    };
    for (const auto& command:history_.commands_) for (const auto& change:command.entities) {
        if (change.before) add(*change.before); if (change.after) add(*change.after);
    }
    if (transaction_) for (const auto& change:transaction_->command.entities) {if(change.before)add(*change.before);if(change.after)add(*change.after);}
    std::vector<std::string> result(references.begin(),references.end());
    std::sort(result.begin(),result.end()); return result;
}
bool SceneDocument::reloadFromDisk() {
    if (assetId_.empty()) return false;
    const auto selection=selection_; const auto id=assetId_;
    if (!load(id)) return false;
    selection_=selection; sanitizeSelection(); return true;
}
std::vector<std::string> SceneDocument::historicalParticleReferences() const {
    std::unordered_set<std::string> references;
    const auto add=[&](const EntityRecord& record) {
        if (record.particle && !record.particle->empty()) references.insert(record.particle->id);
    };
    for (const auto& command:history_.commands_) for (const auto& change:command.entities) {
        if (change.before) add(*change.before); if (change.after) add(*change.after);
    }
    if (transaction_) for (const auto& change:transaction_->command.entities) {if(change.before)add(*change.before);if(change.after)add(*change.after);}
    std::vector<std::string> result(references.begin(),references.end());
    std::sort(result.begin(),result.end()); return result;
}
std::vector<std::string> SceneDocument::historicalMaterialReferences() const {
    std::unordered_set<std::string> references;
    const auto add=[&](const EntityRecord& record) {
        for(const auto& material:record.modelMaterials)if(!material.empty())references.insert(material.id);
    };
    for(const auto& command:history_.commands_)for(const auto& change:command.entities) {
        if(change.before)add(*change.before);if(change.after)add(*change.after);
    }
    if(transaction_)for(const auto& change:transaction_->command.entities) {if(change.before)add(*change.before);if(change.after)add(*change.after);}
    std::vector<std::string> result(references.begin(),references.end());std::sort(result.begin(),result.end());return result;
}
bool SceneDocument::applyReloadSnapshot(const SceneSnapshot& snapshot,bool preserveSelection) {
    return applyPreparedSnapshot(snapshot,preserveSelection,false);
}
bool SceneDocument::applyPreparedSnapshot(const SceneSnapshot& snapshot,bool preserveSelection,bool opening) {
    try {
        auto current=snapshot;
        if(contentStorageReady(projectRoot_))canonicalizeSceneAssetRefs(current);
        if (current.id.empty()) throw std::runtime_error("Reloaded scene has no asset identity.");
        scenePath(projectRoot_,current.id);
        std::string preparedId=current.id, preparedName=current.name;
        if (preparedName.empty() || preparedName.size()>1024) throw std::runtime_error("Scene name is invalid.");
        if (current.previewCamera && !validCamera(*current.previewCamera)) throw std::runtime_error("Scene preview camera is invalid.");
        scene_.apply(current.records,current.properties);
        assetId_=std::move(preparedId); name_=std::move(preparedName); previewCamera_=current.previewCamera;
        loadedFingerprint_=current.fingerprint; transaction_.reset(); history_.reset(true);
        ++documentGeneration_;
        if (!preserveSelection) selection_={};
        sanitizeSelection(); status_=std::string(opening ? "Opened scene: " : "Reloaded scene: ")+name_; return true;
    } catch (const std::exception& error) { status_=std::string(opening ? "Could not open scene: " : "Could not reload scene: ")+error.what(); return false; }
}
bool SceneDocument::applyExternalAssetChanges(const std::vector<EntityRecord>& records,const SceneProperties& properties,
                                             const std::string& label,bool wasDirty) {
    try {
        auto saved=loadedFingerprint_;if(!assetId_.empty())try{saved=sceneFileFingerprint(projectRoot_,assetId_);}catch(...){saved=0;}
        auto prepared=prepareExternalAssetChanges(records,properties,label,wasDirty,saved);return commitExternalAssetChanges(std::move(prepared));
    } catch (const std::exception& error) { status_=error.what(); return false; }
}
SceneDocument::PreparedExternalChange SceneDocument::prepareExternalAssetChanges(const std::vector<EntityRecord>& records,const SceneProperties& properties,
                                                                               const std::string& label,bool wasDirty,uint64_t savedFingerprint) const {
    if(transaction_)throw std::runtime_error("Finish the active scene edit before deleting project assets.");
    PreparedExternalChange prepared;prepared.scene=std::make_unique<Scene>();prepared.scene->apply(records,properties);
    prepared.selection=selection_;std::unordered_set<EntityId> seen;
    std::erase_if(prepared.selection.ids,[&](EntityId id){return !prepared.scene->contains(id)||!seen.insert(id).second;});
    if(std::find(prepared.selection.ids.begin(),prepared.selection.ids.end(),prepared.selection.primary)==prepared.selection.ids.end())
        prepared.selection.primary=prepared.selection.ids.empty()?NoEntity:prepared.selection.ids.back();
    prepared.owner=this;prepared.expectedId=assetId_;prepared.expectedGeneration=documentGeneration_;prepared.expectedRevision=scene_.revision();
    prepared.expectedState=history_.state_;prepared.expectedSavedState=history_.savedState_;prepared.expectedFingerprint=loadedFingerprint_;
    prepared.status=label;prepared.wasDirty=wasDirty;prepared.savedFingerprint=savedFingerprint;return prepared;
}
SceneDocument::PreparedExternalChange SceneDocument::prepareNewSceneAfterAssetDeletion() const {
    auto prepared=prepareExternalAssetChanges({},SceneProperties{},"Deleted scene; new unsaved scene.",true,0);
    prepared.newDocument=true;prepared.selection={};prepared.name="Untitled";return prepared;
}
bool SceneDocument::canCommitExternalAssetChanges(const PreparedExternalChange& p)const noexcept {
    return p.owner==this&&p.scene&&!transaction_&&p.expectedId==assetId_&&p.expectedGeneration==documentGeneration_&&p.expectedRevision==scene_.revision()&&
        p.expectedState==history_.state_&&p.expectedSavedState==history_.savedState_&&p.expectedFingerprint==loadedFingerprint_;
}
bool SceneDocument::commitExternalAssetChanges(PreparedExternalChange&& p)noexcept {
    if(!canCommitExternalAssetChanges(p))return false;
    scene_.adoptPrepared(*p.scene);selection_.ids.swap(p.selection.ids);selection_.primary=p.selection.primary;
    history_.reset(!p.wasDirty);transaction_.reset();loadedFingerprint_=p.savedFingerprint;status_.swap(p.status);
    if(p.newDocument){assetId_.clear();name_.swap(p.name);previewCamera_.reset();++documentGeneration_;}
    p.owner=nullptr;return true;
}
bool SceneDocument::begin(const std::string& label) {
    if (transaction_) { status_ = "Finish the active edit first."; return false; }
    transaction_.emplace();transaction_->id=++transactionSerial_;transaction_->command.label=label;
    transaction_->command.beforeSelection=transaction_->command.afterSelection=selection_;
    status_.clear(); return true;
}
bool SceneDocument::update(const Mutation& mutation) {
    if (!transaction_) { status_ = "There is no active edit."; return false; }
    try {
        const auto before=scene_.records();const auto beforeProperties=scene_.properties();
        auto records = before; auto properties = beforeProperties;
        mutation(records,properties);
        updateFollowOffsets(scene_,records,properties);
        auto command=transaction_->command;
        mergeCommand(command,difference(command.label,before,records,beforeProperties,properties,selection_,selection_));
        scene_.apply(records,properties);
        transaction_->command=std::move(command);
        sanitizeSelection(); status_.clear(); return true;
    } catch (const std::exception& error) { status_ = error.what(); return false; }
}
bool SceneDocument::commit() {
    if (!transaction_) return true;
    auto command = transaction_->command;command.afterSelection=selection_;pruneCommand(command);
    if (!command.entities.empty() || command.beforeProperties) history_.push(std::move(command));
    transaction_.reset(); return true;
}
void SceneDocument::cancel() {
    if (!transaction_) return;
    const auto previousStatus=status_;
    auto command=transaction_->command;pruneCommand(command);
    if(applyHistory(command,false)){transaction_.reset();status_=previousStatus;}
}
bool SceneDocument::edit(const std::string& label, const Mutation& mutation) {
    if (!begin(label)) return false;
    if (!update(mutation)) { cancel(); return false; }
    return commit();
}
bool SceneDocument::applyHistory(const SceneCommand& command, bool forward) {
    try {
        auto records = scene_.records(); auto properties = scene_.properties();
        for (const auto& change : command.entities) {
            const auto& restored = forward ? change.after : change.before;
            const auto current=std::find_if(records.begin(),records.end(),[&](const auto& record){return record.id==change.id;});
            if(change.before && change.after) {
                if(current==records.end())throw std::runtime_error("Edited entity no longer exists.");
                copyFields(*current,*restored,change.fields);
            } else {
                if(current!=records.end())records.erase(current);
                if(restored)records.push_back(*restored);
            }
        }
        const auto& restoredProperties = forward ? command.afterProperties : command.beforeProperties;
        if (restoredProperties) copyProperties(properties,*restoredProperties,command.propertyFields);
        scene_.apply(records,properties);
        selection_ = forward ? command.afterSelection : command.beforeSelection;
        sanitizeSelection(); status_.clear(); return true;
    } catch (const std::exception& error) { status_ = error.what(); return false; }
}
bool SceneDocument::undo() {
    if (transaction_ || !history_.canUndo()) return false;
    const auto& command = history_.commands_[history_.cursor_-1];
    if (!applyHistory(command,false)) return false;
    history_.state_ = command.beforeState; --history_.cursor_; return true;
}
bool SceneDocument::redo() {
    if (transaction_ || !history_.canRedo()) return false;
    const auto& command = history_.commands_[history_.cursor_];
    if (!applyHistory(command,true)) return false;
    history_.state_ = command.afterState; ++history_.cursor_; return true;
}
EntityId SceneDocument::create(EntityRecord record, const std::string& label) {
    if (!begin(label)) return NoEntity;
    record.id = unusedId(scene_.records());
    const EntityId id = record.id;
    if (!update([&](auto& records,auto&) { record.order = nextOrder(records,record.parent); records.push_back(record); })) {
        cancel(); return NoEntity;
    }
    selection_ = {{id},id}; commit(); return id;
}
EntityId SceneDocument::createEmpty(const std::string& name) {
    EntityRecord record; record.name = name; return create(std::move(record),"Create entity");
}
EntityId SceneDocument::createQuader(const std::string& name) {
    EntityRecord record;record.name=name;record.quader.emplace();return create(std::move(record),"Create Quader");
}
bool SceneDocument::replaceQuaderImport(EntityId owner,const QuaderLink& link,const std::vector<EntityRecord>& generated,
    const ModelMaterialRemap& remapMaterials) {
    if(transient_ || transaction_){status_="Quader import waits for Edit mode and the current scene edit.";return false;}
    const auto root=scene_.find(owner);
    if(!root || !root->quader){status_="Quader entity no longer exists.";return false;}
    return edit("Import Quader",[&](auto& records,auto& properties) {
        std::unordered_set<EntityId> removed;
        std::unordered_map<std::string,EntityId> existing;
        for(const auto& record:records)if(record.quaderGenerated && record.quaderGenerated->owner==owner) {
            existing.try_emplace(record.quaderGenerated->sourceKey,record.id);
            for(const auto id:scene_.subtree(record.id))removed.insert(id);
        }
        std::vector<EntityRecord> replacements;replacements.reserve(generated.size()+1);
        std::unordered_map<EntityId,EntityId> remap;
        std::unordered_set<std::string> keys;
        const auto allocate=[&](const std::string& key) {
            if(!keys.insert(key).second)throw std::runtime_error("Quader import has duplicate source identities.");
            if(const auto found=existing.find(key);found!=existing.end())return found->second;
            EntityRecord reservation;reservation.id=unusedId(records);records.push_back(reservation);return reservation.id;
        };
        EntityRecord imported;imported.id=allocate("Imported");imported.name="Imported";imported.parent=owner;
        imported.order=nextOrder(records,owner);imported.quaderGenerated=QuaderGenerated{owner,"Imported"};
        if(const auto previous=scene_.find(imported.id))imported.visible=previous->visible;
        replacements.push_back(imported);
        for(const auto& input:generated) {
            if(!input.id || !input.quaderGenerated)throw std::runtime_error("Quader import record has no temporary identity.");
            const auto id=allocate(input.quaderGenerated->sourceKey);
            if(!remap.emplace(input.id,id).second)throw std::runtime_error("Quader import has duplicate temporary identities.");
        }
        for(const auto& input:generated) {
            auto record=input;record.id=remap.at(input.id);record.parent=input.parent?remap.at(input.parent):imported.id;
            // Quader has no representation for Crimson's lighting roles. Keep
            // those authored values on matching source identities/kinds while
            // source transforms, energy, geometry and shadow flags still sync.
            if(const auto previous=scene_.find(record.id)) {
                record.visible=previous->visible;
                if(record.model && previous->model) {
                    record.lightingScopes=previous->lightingScopes;
                    record.fadeWhenOccluding=previous->fadeWhenOccluding;
                    if(remapMaterials)record.modelMaterials=remapMaterials(*previous,record);
                    else if(record.model==previous->model)record.modelMaterials=previous->modelMaterials;
                    else if(!previous->modelMaterials.empty())
                        throw std::runtime_error("Quader sync needs source material slots to preserve assignments on "+record.name+".");
                }
                if(record.point && previous->point)record.point->roles=previous->point->roles;
                if(record.directional && previous->directional)record.directional->roles=previous->directional->roles;
                if(record.collider && previous->collider) {
                    const auto source=record.collider->source;
                    const auto mesh=record.collider->mesh;
                    record.collider=previous->collider;record.collider->source=source;record.collider->mesh=mesh;
                    // Primitive overrides own their local center/orientation as
                    // well as dimensions. One-sided resizing moves that center.
                    // The visual parent and generated mesh source still sync.
                    if(previous->collider->shape!=ColliderShape::Trimesh)record.local=previous->local;
                }
            }
            record.quaderGenerated->owner=owner;replacements.push_back(std::move(record));
        }
        std::unordered_set<EntityId> kept;
        for(const auto& record:replacements)kept.insert(record.id);
        std::erase_if(records,[&](const auto& record){return removed.contains(record.id) || kept.contains(record.id);});
        auto linked=std::find_if(records.begin(),records.end(),[&](const auto& record){return record.id==owner;});
        if(linked==records.end())throw std::runtime_error("Quader owner cannot be replaced by its imported content.");
        linked->quader=link;
        records.insert(records.end(),replacements.begin(),replacements.end());
        const auto lost=[&](EntityId id){return removed.contains(id) && !kept.contains(id);};
        if(lost(properties.activeEnvironment))properties.activeEnvironment=NoEntity;
        if(lost(properties.activeCamera))properties.activeCamera=NoEntity;
        if(lost(properties.playerCharacter))properties.playerCharacter=NoEntity;
        for(auto& record:records) {
            if(record.camera && lost(record.camera->followTarget)){record.camera->followTarget=NoEntity;record.camera->followOffset={};}
            if(record.environment && lost(record.environment->sun))record.environment->sun=NoEntity;
        }
    });
}
EntityId SceneDocument::createModel(const std::string& assetId, const std::string& name, const Mat4& local) {
    EntityRecord record; record.name = name; record.model=AssetRef<ModelAsset>{assetId}; record.local = local;
    return create(std::move(record),"Place model");
}
EntityId SceneDocument::createParticle(const std::string& assetId, const std::string& name, const Mat4& local) {
    EntityRecord record; record.name=name; record.particle=AssetRef<ParticleEffectAsset>{assetId}; record.local=local;
    return create(std::move(record),"Place particle effect");
}
std::vector<EntityId> SceneDocument::createModels(const std::vector<ModelPlacement>& models) {
    if (models.empty() || !begin("Place models")) return {};
    std::vector<EntityId> ids;
    if (!update([&](auto& records,auto&) {
        for (const auto& model : models) {
            EntityRecord record;
            record.id=unusedId(records); record.name=model.name; record.model=AssetRef<ModelAsset>{model.assetId}; record.local=model.local;
            record.order=nextOrder(records,NoEntity); ids.push_back(record.id); records.push_back(std::move(record));
        }
    })) { cancel(); return {}; }
    selection_={ids,ids.back()};
    commit();
    return ids;
}
EntityId SceneDocument::createDirectionalLight(const std::string& name) {
    EntityRecord record; record.name = name; record.directional.emplace();
    return create(std::move(record),"Create directional light");
}
EntityId SceneDocument::createPointLight(const std::string& name) {
    EntityRecord record; record.name = name; record.point.emplace();
    return create(std::move(record),"Create point light");
}
EntityId SceneDocument::createEnvironment(const std::string& name) {
    if (!begin("Create environment")) return NoEntity;
    EntityRecord record; record.id=unusedId(scene_.records()); record.name=name; record.environment.emplace();
    record.environment->environment.background.show=true;
    if (!update([&](auto& records,auto& properties) {
        record.order=nextOrder(records,NoEntity); records.push_back(record);
        if (!properties.activeEnvironment) properties.activeEnvironment=record.id;
    })) { cancel(); return NoEntity; }
    selection_={{record.id},record.id}; commit(); return record.id;
}
EntityId SceneDocument::createCharacter(const std::string& name) {
    if(!begin("Create character"))return NoEntity;
    EntityRecord record;record.id=unusedId(scene_.records());record.name=name;record.character.emplace();
    if(!update([&](auto& records,auto& properties){
        record.order=nextOrder(records,NoEntity);records.push_back(record);
        if(!properties.playerCharacter)properties.playerCharacter=record.id;
    })){cancel();return NoEntity;}
    selection_={{record.id},record.id};commit();return record.id;
}
EntityId SceneDocument::createCollider(const std::string& name,EntityId parent) {
    EntityRecord record;record.name=name;record.parent=parent;record.collider.emplace();
    return create(std::move(record),"Create collider");
}
EntityId SceneDocument::createCamera(const std::string& name) {
    if(!begin("Create camera"))return NoEntity;
    EntityRecord record;record.id=unusedId(scene_.records());record.name=name;record.camera.emplace();
    const auto selected=scene_.worldTransform(selection_.primary);
    const auto target=selected?math::transformPoint(*selected,{0,0,0}):Vec3{};
    record.local=math::inverse(math::lookAt(target+Vec3{8,10,8},target)).value_or(Mat4{});
    if(!update([&](auto& records,auto& properties){
        record.order=nextOrder(records,NoEntity);records.push_back(record);
        if(!properties.activeCamera)properties.activeCamera=record.id;
    })){cancel();return NoEntity;}
    selection_={{record.id},record.id};commit();return record.id;
}
EntityId SceneDocument::createCamera(const Camera& view,float aspect,const std::string& name) {
    const auto direction=view.target-view.position;
    const auto forward=math::normalized(direction,{0,0,0},0),up=math::normalized(view.up,{0,0,0},0);
    if(!math::finite(view.position)||!math::finite(direction)||!math::finite(view.up)||
        math::length(forward)<1e-8||math::length(math::cross(forward,up))<1e-8||
        !std::isfinite(aspect)||aspect<=0||!std::isfinite(view.fov)||view.fov<=0||view.fov>=180||
        !std::isfinite(view.nearPlane)||!std::isfinite(view.farPlane)||view.nearPlane<=0||view.farPlane<=view.nearPlane||
        (view.fovAxis!=CameraFovAxis::Vertical&&view.fovAxis!=CameraFovAxis::ShorterDimension)) {
        status_="Camera creation requires a finite view, valid projection and positive viewport aspect.";return NoEntity;
    }
    const auto local=math::inverse(view.viewMatrix());
    if(!local){status_="Camera view is not invertible.";return NoEntity;}
    if(!begin("Create camera"))return NoEntity;
    EntityRecord record;record.id=unusedId(scene_.records());record.name=name;record.local=*local;record.camera.emplace();
    record.camera->verticalFov=view.verticalFov(aspect);record.camera->nearPlane=view.nearPlane;record.camera->farPlane=view.farPlane;
    if(!update([&](auto& records,auto& properties){
        record.order=nextOrder(records,NoEntity);records.push_back(record);
        if(!properties.activeCamera)properties.activeCamera=record.id;
    })){cancel();return NoEntity;}
    selection_={{record.id},record.id};commit();return record.id;
}
EntityId SceneDocument::createBox(const std::string& name) {
    return createModel(BuiltinBoxModelId,name);
}
bool SceneDocument::setActiveEnvironment(EntityId id) {
    return edit("Activate environment",[id](auto&,auto& properties){properties.activeEnvironment=id;});
}
bool SceneDocument::setActiveCamera(EntityId id) {
    return edit("Activate camera",[id](auto&,auto& properties){properties.activeCamera=id;});
}
bool SceneDocument::setPlayerCharacter(EntityId id) {
    return edit("Set player character",[id](auto&,auto& properties){properties.playerCharacter=id;});
}
bool SceneDocument::setCameraFollowTarget(EntityId cameraId,EntityId target) {
    const auto record=scene_.find(cameraId);
    const auto world=scene_.worldTransform(cameraId);
    if(!record || !record->camera || !world){status_="Choose an existing Camera entity.";return false;}
    auto position=math::transformPoint(*world,{0,0,0});
    if(record->camera->followTarget)if(const auto oldTarget=scene_.worldTransform(record->camera->followTarget))
        position=math::transformPoint(*oldTarget,{0,0,0})+record->camera->followOffset;
    auto updated=*record;
    updated.camera->followTarget=target;
    if(target) {
        const auto targetWorld=scene_.worldTransform(target);
        if(!targetWorld){status_="Camera follow target no longer exists.";return false;}
        updated.camera->followOffset=position-math::transformPoint(*targetWorld,{0,0,0});
    } else {
        const auto parent=record->parent?scene_.worldTransform(record->parent):std::optional<Mat4>{Mat4{}};
        const auto inverse=parent?math::inverse(*parent):std::nullopt;
        if(!inverse){status_="Cannot clear camera follow under a singular parent.";return false;}
        const auto localPosition=math::transformPoint(*inverse,position);
        const auto local=math::withAffinePosition(updated.local,localPosition);
        if(!local){status_="Camera position is not finite under this parent.";return false;}
        updated.local=*local;updated.camera->followOffset={};
    }
    return updateEntity(cameraId,updated,"Change camera follow target");
}
bool SceneDocument::updateEntity(EntityId id, const EntityRecord& record, const std::string& label) {
    return edit(label,[&](auto& records,auto&) {
        const auto found = std::find_if(records.begin(),records.end(),[&](const auto& value) { return value.id == id; });
        if (found == records.end() || record.id != id) throw std::runtime_error("Entity no longer exists or its ID changed.");
        *found = record;
    });
}
bool SceneDocument::eraseSelection() {
    const auto roots = scene_.topmost(selection_.ids);
    if (roots.empty() || !begin("Delete entities")) return false;
    std::unordered_set<EntityId> removed;
    for (auto root : roots) for (auto id : scene_.subtree(root)) removed.insert(id);
    if (!update([&](auto& records,auto& properties) {
        std::erase_if(records,[&](const auto& record) { return removed.contains(record.id); });
        if (removed.contains(properties.activeEnvironment)) properties.activeEnvironment=NoEntity;
        if (removed.contains(properties.activeCamera)) properties.activeCamera=NoEntity;
        if (removed.contains(properties.playerCharacter)) properties.playerCharacter=NoEntity;
        for(auto& record:records)if(record.camera && removed.contains(record.camera->followTarget)) {
            // Follow is already materialized in the local matrix. Clearing the
            // reference leaves the visible camera pose and its children intact.
            record.camera->followTarget=NoEntity;record.camera->followOffset={};
        }
        for(auto& record:records)if(record.environment && removed.contains(record.environment->sun))record.environment->sun=NoEntity;
        for(auto& record:records)if(record.quaderGenerated && removed.contains(record.quaderGenerated->owner))record.quaderGenerated.reset();
    })) { cancel(); return false; }
    selection_ = {}; return commit();
}
bool SceneDocument::duplicateSelection() {
    const auto roots = scene_.topmost(selection_.ids);
    if (roots.empty() || !begin("Duplicate entities")) return false;
    const auto previousSelection = selection_;
    std::vector<EntityId> originals;
    for (auto root : roots) {
        const auto branch = scene_.subtree(root); originals.insert(originals.end(),branch.begin(),branch.end());
    }
    std::unordered_map<EntityId,EntityId> copied;
    std::unordered_set<EntityId> copiedIds;
    if (!update([&](auto& records,auto&) {
        for (auto original : originals) {
            auto record = *scene_.find(original);
            record.id = unusedId(records); copied.emplace(original,record.id);
            copiedIds.insert(record.id);
            if (const auto parent = copied.find(record.parent); parent != copied.end()) record.parent = parent->second;
            else record.name += " Copy";
            record.order = nextOrder(records,record.parent);
            records.push_back(std::move(record));
        }
        for(auto& record:records)if(copiedIds.contains(record.id)) {
            if(record.camera)if(const auto target=copied.find(record.camera->followTarget);target!=copied.end())record.camera->followTarget=target->second;
            if(record.environment)if(const auto sun=copied.find(record.environment->sun);sun!=copied.end())record.environment->sun=sun->second;
            if(record.quaderGenerated) {
                const auto owner=copied.find(record.quaderGenerated->owner);
                if(owner!=copied.end())record.quaderGenerated->owner=owner->second;
                else record.quaderGenerated.reset();
            }
        }
    })) { cancel(); return false; }
    selection_ = {};
    for (auto original : previousSelection.ids) if (copied.contains(original)) selection_.ids.push_back(copied.at(original));
    if (copied.contains(previousSelection.primary)) selection_.primary = copied.at(previousSelection.primary);
    sanitizeSelection(); return commit();
}
bool SceneDocument::reparentSelection(EntityId parent) {
    const auto roots = scene_.topmost(selection_.ids);
    if (roots.empty()) return false;
    if (parent && !scene_.contains(parent)) { status_ = "Parent entity no longer exists."; return false; }
    const auto parentWorld = parent ? scene_.worldTransform(parent) : std::optional<Mat4>(Mat4{});
    const auto parentInverse = parentWorld ? math::inverse(*parentWorld) : std::nullopt;
    if (!parentInverse) { status_ = "Cannot reparent under a singular transform."; return false; }
    std::unordered_map<EntityId,Mat4> locals;
    for (auto root : roots) {
        const auto branch = scene_.subtree(root);
        if (std::find(branch.begin(),branch.end(),parent) != branch.end()) {
            status_ = "Cannot parent an entity under itself or a descendant."; return false;
        }
        const auto world = scene_.worldTransform(root);
        if (!world) { status_ = "Cannot determine the entity world transform."; return false; }
        locals.emplace(root,*parentInverse * *world);
    }
    return edit("Reparent entities",[&](auto& records,auto&) {
        for (auto root : roots) {
            const auto order = nextOrder(records,parent);
            auto& record = *std::find_if(records.begin(),records.end(),[root](const auto& value) { return value.id == root; });
            record.parent = parent; record.order = order; record.local = locals.at(root);
        }
    });
}
bool SceneDocument::setProperties(const SceneProperties& properties, const std::string& label) {
    return edit(label,[&](auto&,auto& current) { current = properties; });
}

bool SceneDocument::groupSelection(const std::string& name) {
    const auto roots=scene_.topmost(selection_.ids);
    if (roots.empty()) return false;
    EntityId parent=scene_.find(roots.front())->parent;
    Vec3 center{};
    std::unordered_map<EntityId,Mat4> worlds;
    for (const auto id : roots) {
        const auto record=scene_.find(id);
        const auto world=scene_.worldTransform(id);
        if (!record || !world) { status_="Cannot resolve the selected entities."; return false; }
        if (record->parent != parent) parent=NoEntity;
        center=center+math::transformPoint(*world,{0,0,0})/static_cast<double>(roots.size());
        worlds.emplace(id,*world);
    }
    const auto parentWorld=parent ? scene_.worldTransform(parent) : std::optional<Mat4>{Mat4{}};
    const auto parentInverse=parentWorld ? math::inverse(*parentWorld) : std::nullopt;
    const auto groupWorld=math::translation(center);
    const auto groupInverse=math::inverse(groupWorld);
    if (!parentInverse || !groupInverse) { status_="Cannot group under a singular transform."; return false; }
    if (!begin("Group entities")) return false;
    EntityRecord group;
    group.id=unusedId(scene_.records()); group.name=name; group.parent=parent;
    group.local=*parentInverse*groupWorld;
    if (!update([&](auto& records,auto&) {
        group.order=nextOrder(records,parent); records.push_back(group);
        uint32_t order=0;
        for (const auto id : roots) {
            auto& record=*std::find_if(records.begin(),records.end(),[&](const auto& r){return r.id==id;});
            record.parent=group.id; record.order=order++; record.local=*groupInverse*worlds.at(id);
        }
    })) { cancel(); return false; }
    selection_={{group.id},group.id};
    return commit();
}

bool SceneDocument::ungroupSelection() {
    std::vector<EntityId> groups;
    for (const auto id : scene_.topmost(selection_.ids)) {
        const auto record=scene_.find(id);
        if (record && !record->model && !record->particle && !record->directional && !record->point && !record->environment &&
            !record->character && !record->collider && !record->quader && !record->quaderGenerated && !record->camera && !scene_.children(id).empty()) groups.push_back(id);
    }
    if (groups.empty()) { status_="Select a group to ungroup."; return false; }
    struct ChildMove { EntityId id,parent; Mat4 local; };
    std::vector<ChildMove> moves;
    for (const auto group : groups) {
        const auto parent=scene_.find(group)->parent;
        const auto world=parent ? scene_.worldTransform(parent) : std::optional<Mat4>{Mat4{}};
        const auto inverse=world ? math::inverse(*world) : std::nullopt;
        if (!inverse) { status_="Cannot ungroup under a singular transform."; return false; }
        for (const auto id : scene_.children(group)) {
            const auto childWorld=scene_.worldTransform(id);
            if (!childWorld) { status_="Cannot resolve the child transform."; return false; }
            moves.push_back({id,parent,*inverse * *childWorld});
        }
    }
    if (!begin("Ungroup entities")) return false;
    if (!update([&](auto& records,auto&) {
        for (const auto& move : moves) {
            const auto order=nextOrder(records,move.parent);
            auto& record=*std::find_if(records.begin(),records.end(),[&](const auto& r){return r.id==move.id;});
            record.parent=move.parent; record.order=order; record.local=move.local;
        }
        std::erase_if(records,[&](const auto& r){return std::find(groups.begin(),groups.end(),r.id)!=groups.end();});
        for(auto& record:records)if(record.camera && std::find(groups.begin(),groups.end(),record.camera->followTarget)!=groups.end()) {
            record.camera->followTarget=NoEntity;record.camera->followOffset={};
        }
    })) { cancel(); return false; }
    selection_={};
    for (const auto& move : moves) selection_.ids.push_back(move.id);
    sanitizeSelection();
    return commit();
}

} // namespace crimson
