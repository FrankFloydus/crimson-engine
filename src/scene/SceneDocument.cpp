#include "SceneDocument.h"
#include "SceneComponents.h"
#include "CameraTemplates.h"
#include "BuiltinModels.h"
#include "SceneNames.h"
#include "EnvironmentSerialization.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "assets/GameplayEffectAsset.h"
#include "assets/AnimationAsset.h"
#include "resources/AssetId.h"
#include "math/Json.h"
#include "resources/AssetRefJson.h"
#include "point_light.sh"

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
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
namespace {
using Json = nlohmann::json;
// Legacy component-row metadata is validated during conversion, never written.
constexpr std::array<std::string_view,17> LegacyComponentTokens{
    "transform","actor","motor","controller","navigation","playerInput","lightRig",
    "darknessReveal","camera","cameraFollow","model","collider","directional","point",
    "environment","particle","quader"};
void preserveAuthoredActorComponents(EntityRecord& target,const EntityRecord& previous) {
    target.actor=previous.actor;target.movement=previous.movement;target.controller=previous.controller;
    target.navigation=previous.navigation;target.enemy=previous.enemy;target.playerInput=previous.playerInput;
    target.tags=previous.tags;target.effects=previous.effects;target.decal=previous.decal;target.receiveDecals=previous.receiveDecals;
    target.cameraFollow=previous.cameraFollow;
    if(previous.camera)target.camera=previous.camera;
}
std::uint32_t readCollisionBits(const Json& value) {
    if(!value.is_number_integer()||(value.is_number_unsigned()?value.get<std::uint64_t>()>UINT32_MAX:value.get<std::int64_t>()<0||value.get<std::int64_t>()>UINT32_MAX))
        throw std::runtime_error("Collision layers and masks must be unsigned 32-bit integers.");
    return value.get<std::uint32_t>();
}
CollisionFilter readCollisionFilter(const Json& value) {
    if(!value.is_object())throw std::runtime_error("Collision filter must be an object.");
    return {readCollisionBits(value.at("layers")),readCollisionBits(value.at("mask"))};
}
Json collisionFilterJson(const CollisionFilter& value) {return {{"layers",value.layers},{"mask",value.mask}};}

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
std::string paintBase64(std::span<const std::uint8_t> bytes) {
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;out.reserve((bytes.size()+2)/3*4);
    for(std::size_t i=0;i<bytes.size();i+=3){const unsigned a=bytes[i],b=i+1<bytes.size()?bytes[i+1]:0,c=i+2<bytes.size()?bytes[i+2]:0;
        out+=alphabet[a>>2];out+=alphabet[((a&3)<<4)|(b>>4)];out+=i+1<bytes.size()?alphabet[((b&15)<<2)|(c>>6)]:'=';out+=i+2<bytes.size()?alphabet[c&63]:'=';}
    return out;
}
std::vector<std::uint8_t> readPaintBase64(const Json& json,std::uint32_t count) {
    if(!json.is_string()||count>MaximumMeshPaintVertices)throw std::runtime_error("Invalid paint encoding or vertex budget.");
    const auto& value=json.get_ref<const std::string&>();const auto expected=std::size_t(count)*3;
    if(value.size()!=(expected+2)/3*4)throw std::runtime_error("Paint data length does not match its vertex count.");
    const auto digit=[](char c)->int {if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;return c=='+'?62:c=='/'?63:-1;};
    std::vector<std::uint8_t> bytes;bytes.reserve(expected);
    for(std::size_t i=0;i<value.size();i+=4){const int a=digit(value[i]),b=digit(value[i+1]),c=digit(value[i+2]),d=digit(value[i+3]);
        // RGB records always contain a multiple of three bytes; padding is invalid.
        if(a<0||b<0||c<0||d<0)throw std::runtime_error("Paint data must use canonical RGB8 base64.");
        bytes.push_back(static_cast<std::uint8_t>((a<<2)|(b>>4)));bytes.push_back(static_cast<std::uint8_t>((b<<4)|(c>>2)));bytes.push_back(static_cast<std::uint8_t>((c<<6)|d));}
    return bytes;
}
SceneMeshPaint readMeshPaint(const Json& json) {
    if(!json.is_object())throw std::runtime_error("Mesh paint must be an object.");
    SceneMeshPaint paint;paint.recipeVersion=readCollisionBits(json.at("recipe"));paint.subdivisionLevel=readCollisionBits(json.at("level"));
    if(paint.recipeVersion!=MeshPaintRecipeVersion||paint.subdivisionLevel>MaximumMeshPaintLevel)throw std::runtime_error("Unsupported mesh paint recipe or subdivision level.");
    const auto& draws=json.at("draws");if(!draws.is_array()||draws.size()>MaximumMeshPaintVertices)throw std::runtime_error("Invalid paint draw table.");
    std::uint64_t total{};
    for(const auto& source:draws){SceneMeshPaintDraw draw;draw.draw=readCollisionBits(source.at("draw"));draw.primitive=readCollisionBits(source.at("primitive"));draw.level=readCollisionBits(source.at("level"));draw.sourceSignature=parseId(source.at("source"));
        const auto count=readCollisionBits(source.at("vertices"));total+=count;if(!count||total>MaximumMeshPaintVertices)throw std::runtime_error("Entity paint exceeds its vertex budget.");
        draw.weights=makeMeshPaintPayload(count,readPaintBase64(source.at("rgb8"),count));paint.draws.push_back(std::move(draw));}
    return paint;
}
Json meshPaintJson(const SceneMeshPaint& paint) {
    Json draws=Json::array();for(const auto& draw:paint.draws)draws.push_back({{"draw",draw.draw},{"primitive",draw.primitive},{"level",draw.level},{"source",hexId(draw.sourceSignature)},{"vertices",draw.weights->vertexCount},{"rgb8",paintBase64(meshPaintBytes(*draw.weights))}});
    return {{"recipe",paint.recipeVersion},{"level",paint.subdivisionLevel},{"draws",std::move(draws)}};
}
Json blendLayerJson(const MaterialBlendLayer& layer) {
    return {{"heightBlend",layer.heightBlend},{"heightInfluence",layer.heightInfluence},{"blendSoftness",layer.blendSoftness},
        {"heightScale",layer.heightScale},{"heightMidpoint",layer.heightMidpoint},{"normalInfluence",layer.normalInfluence},
        {"roughnessInfluence",layer.roughnessInfluence},{"colorInfluence",layer.colorInfluence},{"metallicInfluence",layer.metallicInfluence},
        {"occlusionInfluence",layer.occlusionInfluence},{"tint",layer.tint},{"roughnessScale",layer.roughnessScale},
        {"mappingOverride",layer.mappingOverride},{"mapping",{{"mode",static_cast<int>(layer.mapping.mode)},
        {"scale",layer.mapping.scale},{"offset",layer.mapping.offset},{"rotation",layer.mapping.rotation},{"scaleLinked",layer.mapping.scaleLinked}}}};
}
MaterialBlendLayer readBlendLayer(const Json& json) {
    if(!json.is_object())throw std::runtime_error("Occurrence Blend layer must be an object.");
    MaterialBlendLayer layer;
    layer.heightBlend=json.value("heightBlend",layer.heightBlend);layer.heightInfluence=json.value("heightInfluence",layer.heightInfluence);
    layer.blendSoftness=json.value("blendSoftness",layer.blendSoftness);layer.heightScale=json.value("heightScale",layer.heightScale);
    layer.heightMidpoint=json.value("heightMidpoint",layer.heightMidpoint);layer.normalInfluence=json.value("normalInfluence",layer.normalInfluence);
    layer.roughnessInfluence=json.value("roughnessInfluence",layer.roughnessInfluence);layer.colorInfluence=json.value("colorInfluence",layer.colorInfluence);
    layer.metallicInfluence=json.value("metallicInfluence",layer.metallicInfluence);layer.occlusionInfluence=json.value("occlusionInfluence",layer.occlusionInfluence);
    layer.tint=json.value("tint",layer.tint);layer.roughnessScale=json.value("roughnessScale",layer.roughnessScale);
    layer.mappingOverride=json.value("mappingOverride",layer.mappingOverride);
    if(json.contains("mapping")) {
        const auto& mapping=json.at("mapping");if(!mapping.is_object())throw std::runtime_error("Occurrence Blend mapping must be an object.");
        const auto mode=readCollisionBits(mapping.at("mode"));if(mode>3)throw std::runtime_error("Invalid occurrence Blend mapping mode.");
        layer.mapping.mode=static_cast<MaterialUvMode>(mode);layer.mapping.scale=mapping.at("scale").get<Vec3>();
        layer.mapping.offset=mapping.at("offset").get<Vec3>();layer.mapping.rotation=mapping.at("rotation").get<Vec3>();
        layer.mapping.scaleLinked=mapping.value("scaleLinked",layer.mapping.scaleLinked);
    }
    validateSceneBlendLayer(layer);return layer;
}
SceneBlendSettings readBlendSettings(const Json& json) {
    if(!json.is_array()||json.size()>MaximumModelMaterialSlots)throw std::runtime_error("Invalid occurrence Blend settings table.");
    SceneBlendSettings settings;settings.reserve(json.size());
    for(const auto& item:json) {
        SceneBlendMaterialSettings binding;binding.slot=readCollisionBits(item.at("slot"));binding.material=item.at("material").get<AssetRef<MaterialAsset>>();
        const auto& layers=item.at("layers");if(!layers.is_array()||layers.size()!=4)throw std::runtime_error("Occurrence Blend settings require Base/R/G/B layers.");
        for(std::size_t index=0;index<4;++index)if(!layers[index].is_null())binding.layers[index]=readBlendLayer(layers[index]);
        settings.push_back(std::move(binding));
    }
    validateSceneBlendSettings(settings);return settings;
}
Json blendSettingsJson(const SceneBlendSettings& settings) {
    Json json=Json::array();
    for(const auto& binding:settings) {
        Json layers=Json::array();for(const auto& layer:binding.layers)layers.push_back(layer?blendLayerJson(*layer):Json{});
        json.push_back({{"slot",binding.slot},{"material",binding.material},{"layers",std::move(layers)}});
    }
    return json;
}
SceneDecal readDecal(const Json& json) {
    SceneDecal decal;decal.asset=json.at("asset").get<AssetRef<DecalAsset>>();
    if(json.contains("projection")) {
        const auto projection=json.at("projection").get<std::string>();
        if(projection=="Box")decal.projection=SceneDecalProjection::Box;
        else if(projection!="Surface")throw std::runtime_error("Unknown decal projection.");
    }
    if(json.contains("seed")) {
    const auto& seed=json.at("seed");decal.seed.receiver=parseId(seed.at("receiver"));
    decal.seed.modelId=seed.at("model").get<std::string>();decal.seed.fingerprint=parseId(seed.at("fingerprint"));
    decal.seed.draw=readCollisionBits(seed.at("draw"));decal.seed.primitive=readCollisionBits(seed.at("primitive"));decal.seed.triangle=readCollisionBits(seed.at("triangle"));
    decal.seed.barycentric=seed.at("barycentric").get<Vec3>();decal.seed.localPoint=seed.at("localPoint").get<Vec3>();decal.seed.localNormal=seed.at("localNormal").get<Vec3>();
    } else if(decal.projection==SceneDecalProjection::Surface)throw std::runtime_error("Surface decal requires seed provenance.");
    decal.tint=json.at("tint").get<Vec3>();decal.opacity=json.at("opacity").get<float>();decal.edgeFeather=json.at("edgeFeather").get<float>();
    const auto& priority=json.at("priority");if(!priority.is_number_integer()||(priority.is_number_unsigned()?priority.get<uint64_t>()>INT32_MAX:priority.get<int64_t>()<INT32_MIN||priority.get<int64_t>()>INT32_MAX))throw std::runtime_error("Decal layer priority must be a signed 32-bit integer.");
    decal.priority=priority.get<int32_t>();return decal;
}
Json decalJson(const SceneDecal& decal) {
    const auto& seed=decal.seed;
    Json result={{"asset",decal.asset},{"seed",{{"receiver",hexId(seed.receiver)},{"model",seed.modelId},{"fingerprint",hexId(seed.fingerprint)},
        {"draw",seed.draw},{"primitive",seed.primitive},{"triangle",seed.triangle},{"barycentric",seed.barycentric},{"localPoint",seed.localPoint},{"localNormal",seed.localNormal}}},
        {"tint",decal.tint},{"opacity",decal.opacity},{"edgeFeather",decal.edgeFeather},{"priority",decal.priority}};
    if(decal.projection==SceneDecalProjection::Box){result["projection"]="Box";result.erase("seed");}
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
    return {{"receiverMask",roles.receiverMask},{"shadowCasterMask",roles.shadowCasterMask}};
}
LightRoles readRoles(const Json& source,LightRoles value={}) {
    if(!source.is_object())throw std::runtime_error("Light roles must be an object.");
    value.receiverMask=readLightingMask(source,"receiverMask",value.receiverMask);
    value.shadowCasterMask=readLightingMask(source,"shadowCasterMask",value.shadowCasterMask);
    return value;
}
float readIndirectEnergy(const Json& source,float fallback=1.f) {
    if(!source.contains("indirectEnergy"))return fallback;
    const auto& encoded=source.at("indirectEnergy");
    if(!encoded.is_number())throw std::runtime_error("Light GI energy must be a number.");
    const auto value=encoded.get<float>();
    if(!validIndirectEnergy(value))throw std::runtime_error("Light GI energy must be finite and between zero and sixteen.");
    return value;
}
float readShadowAngularDiameter(const Json& source) {
    if(!source.contains("shadowAngularDiameter"))return .53f;
    const auto& encoded=source.at("shadowAngularDiameter");
    if(!encoded.is_number())throw std::runtime_error("Directional shadow angular diameter must be a number.");
    const auto value=encoded.get<float>();
    if(!std::isfinite(value)||value<0||value>2)
        throw std::runtime_error("Directional shadow angular diameter must be finite and between zero and two degrees.");
    return value;
}
template<class T> void readLightField(const Json& object,const char* key,T& target) {
    const auto found=object.find(key);
    if(found==object.end())return;
    if constexpr(std::is_same_v<T,bool>) {
        if(!found->is_boolean())throw std::runtime_error("Light toggle must be a boolean.");
    } else if constexpr(std::is_same_v<T,std::uint32_t>) {
        if(!found->is_number_integer() || (found->is_number_unsigned()?found->get<std::uint64_t>()>UINT32_MAX:
            found->get<std::int64_t>()<0 || found->get<std::int64_t>()>UINT32_MAX))
            throw std::runtime_error("Light seed and counts must be unsigned 32-bit integers.");
    } else if constexpr(std::is_same_v<T,float>) {
        if(!found->is_number())throw std::runtime_error("Light numeric property must be a number.");
    }
    target=found->get<T>();
}
template<class T,class Visitor> void visitFlickerFields(T& value,const Visitor& field) {
    field("enabled",value.enabled);field("speed",value.speed);field("duration",value.duration);
    field("startDelay",value.startDelay);field("timeOffset",value.timeOffset);field("seed",value.seed);
    field("timingRandomness",value.timingRandomness);field("brightnessRandomness",value.brightnessRandomness);
    field("slowAmount",value.slowAmount);field("slowRate",value.slowRate);
    field("flutterAmount",value.flutterAmount);field("flutterRate",value.flutterRate);
    field("smoothness",value.smoothness);field("brightnessBias",value.brightnessBias);
    field("stableMinimum",value.stableMinimum);field("stableMaximum",value.stableMaximum);
    field("failureMinimum",value.failureMinimum);field("failureMaximum",value.failureMaximum);field("failureLevel",value.failureLevel);
    field("flashesMinimum",value.flashesMinimum);field("flashesMaximum",value.flashesMaximum);
    field("flashMinimum",value.flashMinimum);field("flashMaximum",value.flashMaximum);
    field("gapMinimum",value.gapMinimum);field("gapMaximum",value.gapMaximum);
    field("restMinimum",value.restMinimum);field("restMaximum",value.restMaximum);field("eventFrequency",value.eventFrequency);
    field("riseTime",value.riseTime);field("fallTime",value.fallTime);field("recoveryTime",value.recoveryTime);field("afterglow",value.afterglow);
    field("period",value.period);field("pulseWidth",value.pulseWidth);
    field("secondaryStrength",value.secondaryStrength);field("secondarySpacing",value.secondarySpacing);
}
template<class T,class Visitor> void visitFlickerChannel(T& value,const Visitor& field) {
    field("enabled",value.enabled);field("minimum",value.minimum);field("maximum",value.maximum);
    field("invert",value.invert);field("response",value.response);
}
template<class T,class Visitor> void visitFlickerColor(T& value,const Visitor& field) {
    field("enabled",value.enabled);field("minimumTint",value.minimumTint);field("maximumTint",value.maximumTint);
    field("minimumTemperatureShift",value.minimumTemperatureShift);field("maximumTemperatureShift",value.maximumTemperatureShift);
    field("invert",value.invert);field("response",value.response);field("amount",value.amount);
}
Json flickerJson(const LightFlickerSettings& value) {
    Json result=Json::object();
    visitFlickerFields(value,[&](const char* key,const auto& field){result[key]=field;});
    result["behavior"]=lightFlickerBehaviorId(value.behavior);
    result["playback"]=value.playback==LightFlickerPlayback::Loop?"Loop":"Once";
    const auto encode=[&](Json& object){return [&object](const char* key,const auto& field){object[key]=field;};};
    result["intensity"]=Json::object();visitFlickerChannel(value.intensity,encode(result["intensity"]));
    result["radius"]=Json::object();visitFlickerChannel(value.radius,encode(result["radius"]));
    result["color"]=Json::object();visitFlickerColor(value.color,encode(result["color"]));
    return result;
}
LightFlickerSettings readFlicker(const Json& source) {
    if(!source.is_object())throw std::runtime_error("Light flickering must be an object.");
    LightFlickerSettings result;
    visitFlickerFields(result,[&](const char* key,auto& field){readLightField(source,key,field);});
    if(source.contains("behavior")) {
        const auto behavior=lightFlickerBehaviorFromId(source.at("behavior").get<std::string>());
        if(!behavior)throw std::runtime_error("Unknown light flicker behavior.");
        result.behavior=*behavior;
    }
    if(source.contains("playback")) {
        const auto playback=source.at("playback").get<std::string>();
        if(playback=="Loop")result.playback=LightFlickerPlayback::Loop;
        else if(playback=="Once")result.playback=LightFlickerPlayback::Once;
        else throw std::runtime_error("Unknown light flicker playback.");
    }
    const auto decode=[&](const Json& object){return [&object](const char* key,auto& field){readLightField(object,key,field);};};
    const auto object=[&](const char* key)->const Json& {
        const auto& value=source.at(key);
        if(!value.is_object())throw std::runtime_error("Light flicker channel must be an object.");
        return value;
    };
    if(source.contains("intensity"))visitFlickerChannel(result.intensity,decode(object("intensity")));
    if(source.contains("radius"))visitFlickerChannel(result.radius,decode(object("radius")));
    if(source.contains("color"))visitFlickerColor(result.color,decode(object("color")));
    if(!validLightFlicker(result))throw std::runtime_error("Light flickering properties are outside their supported ranges.");
    return result;
}
template<class Light> Json lightJson(const Light& light) {
    Json result{{"color",light.color}, {"intensity",light.intensity}, {"enabled",light.enabled},{"indirectEnergy",light.indirectEnergy}};
    if(light.colorMode!=LightColorMode::Color)result["colorMode"]="Temperature";
    if(light.temperatureKelvin!=6500.f || light.colorMode==LightColorMode::Temperature)result["temperatureKelvin"]=light.temperatureKelvin;
    if(light.flicker!=LightFlickerSettings{})result["flicker"]=flickerJson(light.flicker);
    if constexpr(requires { light.radius; })result["radius"]=light.radius;
    if constexpr(requires { light.nearFalloffRadius; }) {
        result["nearFalloffRadius"]=light.nearFalloffRadius;result["specularStrength"]=light.specularStrength;
        result["rangeFalloffExponent"]=light.rangeFalloffExponent;result["edgeSoftness"]=light.edgeSoftness;
        result["sourceRadius"]=light.sourceRadius;
        result["attenuation"]=light.attenuation;
    }
    if constexpr(requires { light.shadowDistance; })result["shadowDistance"]=light.shadowDistance;
    if constexpr(requires { light.shadowAngularDiameter; })result["shadowAngularDiameter"]=light.shadowAngularDiameter;
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
    result.indirectEnergy=readIndirectEnergy(json);
    if(json.contains("colorMode")) {
        const auto mode=json.at("colorMode").get<std::string>();
        if(mode=="Color")result.colorMode=LightColorMode::Color;
        else if(mode=="Temperature")result.colorMode=LightColorMode::Temperature;
        else throw std::runtime_error("Unknown light color mode.");
    }
    readLightField(json,"temperatureKelvin",result.temperatureKelvin);
    if(!validLightTemperature(result.temperatureKelvin))throw std::runtime_error("Light temperature must be between 1000 and 15000 Kelvin.");
    if(json.contains("flicker"))result.flicker=readFlicker(json.at("flicker"));
    if constexpr(requires { result.radius; })result.radius=json.value("radius",20.f);
    if constexpr(requires { result.nearFalloffRadius; }) {
        result.nearFalloffRadius=json.value("nearFalloffRadius",0.f);result.specularStrength=json.value("specularStrength",1.f);
        result.rangeFalloffExponent=json.value("rangeFalloffExponent",4.f);result.edgeSoftness=json.value("edgeSoftness",1.f);
        result.sourceRadius=json.contains("sourceRadius")?json.at("sourceRadius").get<float>():json.value("softSourceRadius",0.f);
        result.attenuation=json.value("attenuation",2.f);
    }
    if constexpr(requires { result.shadowDistance; })result.shadowDistance=json.value("shadowDistance",100.f);
    if constexpr(requires { result.shadowAngularDiameter; })result.shadowAngularDiameter=readShadowAngularDiameter(json);
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
SceneSnapshot readScene(const std::filesystem::path& path,const std::string& owner,const std::filesystem::path& root) {
    const auto contents=readText(path);
    const Json json=Json::parse(contents);
    const auto version=json.at("version").get<int>();
    if ((version<1 || version>25) || json.at("type") != "crimson-scene")
        throw std::runtime_error("Unsupported scene file type or version.");
    SceneSnapshot result; result.fingerprint=fingerprint(contents);
    std::unordered_map<EntityId,AttributeSeeds> legacyAttributes;
    std::unordered_set<EntityId> legacyPlayerInputs;
    const auto legacyController=[&](EntityRecord& record,float height,float radius) {
        AgentTraversalSettings traversal;traversal.height=height;traversal.radius=radius;
        if(!validAgentTraversal(traversal))throw std::runtime_error("Legacy Character capsule dimensions are invalid.");
        record.controller.emplace();
        if(traversal==AgentTraversalSettings{})return;
        const auto name=record.name.size()<=1018?record.name+" Agent":record.name;
        auto profile=std::make_shared<const NavigationAgentProfile>(NavigationAgentProfile{newAssetId(),name,traversal});
        record.controller->profile=AssetRef<NavigationAgentProfile>{profile->id};result.stagedNavigationControllers.emplace_back(record.id,profile->id);result.stagedNavigationProfiles.push_back(std::move(profile));
    };
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
        if(version>=25&&entity.contains("meshPaint"))record.meshPaint=readMeshPaint(entity.at("meshPaint"));
        if(entity.contains("blendSettings"))record.blendSettings=readBlendSettings(entity.at("blendSettings"));
        if(entity.contains("primitive")) {
            const auto& source=entity.at("primitive");
            if(!source.is_object() || !record.model || !isBuiltinModel(record.model->id))
                throw std::runtime_error("Primitive geometry settings require a built-in mesh and an object.");
            auto& primitive=record.primitive.emplace();
            const auto integer=[&](const char* field,std::uint32_t fallback) {
                if(!source.contains(field))return fallback;
                const auto& value=source.at(field);
                if(!value.is_number_integer() ||
                    (value.is_number_unsigned()?value.get<std::uint64_t>()>std::numeric_limits<std::uint32_t>::max():
                        value.get<std::int64_t>()<0 || value.get<std::int64_t>()>std::numeric_limits<std::uint32_t>::max()))
                    throw std::runtime_error("Primitive sides and rings must be nonnegative integers.");
                return value.get<std::uint32_t>();
            };
            const auto base=canonicalAssetId(record.model->id);
            if(base==BuiltinBoxModelId)primitive.size=source.value("size",primitive.size);
            else {
                primitive.radius=source.value("radius",primitive.radius);
                primitive.sides=integer("sides",primitive.sides);
                if(base==BuiltinSphereModelId)primitive.rings=integer("rings",primitive.rings);
                else primitive.height=source.value("height",primitive.height);
            }
        }
        if(entity.contains("meshShading")) {
            const auto& shading=entity.at("meshShading");
            if(!shading.is_number_integer() ||
               (shading.is_number_unsigned()?shading.get<std::uint64_t>()>2:
                   shading.get<std::int64_t>()<0 || shading.get<std::int64_t>()>2))
                throw std::runtime_error("Mesh shading must be an integer from 0 to 2.");
            record.meshShading=static_cast<MeshShading>(shading.get<unsigned>());
        }
        if(entity.contains("modelRagdoll")) {
            const auto& source=entity.at("modelRagdoll");
            if(!source.is_object())throw std::runtime_error("Model ragdoll settings must be an object.");
            if(!source.at("asset").is_string())throw std::runtime_error("Ragdoll reference must be an asset UUID.");
            const auto reference=source.at("asset").get<std::string>();
            if(!reference.empty()&&!isAssetUuid(reference))throw std::runtime_error("Ragdoll reference must be an asset UUID.");
            record.modelRagdoll.asset=source.at("asset").get<AssetRef<RagdollAsset>>();
            if(source.contains("startSimulated"))record.modelRagdoll.startSimulated=source.at("startSimulated").get<bool>();
        }
        if(version>=10 && entity.contains("modelAnimation")) {
            const auto& source=entity.at("modelAnimation");
            if(!source.is_object())throw std::runtime_error("Model animation settings must be an object.");
            auto& animation=record.modelAnimation;
            if(source.contains("asset"))animation.asset=source.at("asset").get<AssetRef<AnimationAsset>>();
            if(source.contains("clip")) {
                const auto& clip=source.at("clip");
                if(!clip.is_number_integer() ||
                    (clip.is_number_unsigned()?clip.get<std::uint64_t>()>std::uint64_t(std::numeric_limits<std::int32_t>::max()):
                        clip.get<std::int64_t>()<-1 || clip.get<std::int64_t>()>std::numeric_limits<std::int32_t>::max()))
                    throw std::runtime_error("Model animation clip must be an integer from -1 to 2147483647.");
                const auto index=clip.get<std::int32_t>();
                if(!source.contains("asset") && index>=0 && record.model)animation.asset.id=importedAnimationId(record.model->id,static_cast<std::uint32_t>(index));
            }
            animation.playing=source.value("playing",animation.playing);
            if(source.contains("loopOverride"))animation.loopOverride=source.at("loopOverride").get<bool>();
            else if(source.contains("clip") && source.contains("loop"))animation.loopOverride=source.at("loop").get<bool>();
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
        if(entity.contains("receiveDecals")&&!entity.at("receiveDecals").is_boolean())throw std::runtime_error("Receive Decals must be boolean.");record.receiveDecals=entity.value("receiveDecals",true);
        if(entity.contains("lightingScopes")) {
            const auto& scopes=entity.at("lightingScopes");
            if(!scopes.is_object())throw std::runtime_error("Surface lighting scopes must be an object.");
            record.lightingScopes.directReceiverMask=readLightingMask(scopes,"directReceiverMask",lighting_scope::All);
            record.lightingScopes.shadowCasterMask=readLightingMask(scopes,"shadowCasterMask",lighting_scope::All);
        }
        if (entity.contains("directional")) record.directional = readLight<SceneDirectionalLight>(entity.at("directional"));
        if (entity.contains("point")) record.point = readLight<ScenePointLight>(entity.at("point"));
        if (version>=2 && entity.contains("environment")) record.environment=readEnvironment(entity.at("environment"),version>=6);
        if (version>=3 && entity.contains("particle")) record.particle=entity.at("particle").get<AssetRef<ParticleEffectAsset>>();
        if(version>=24&&entity.contains("decal"))record.decal=readDecal(entity.at("decal"));
        if (version>=4 && version<=15 && entity.contains("character")) {
            const auto& c=entity.at("character");
            record.actor.emplace();record.movement.emplace();
            legacyAttributes[record.id]={{stats::MovementSpeed,c.at("moveSpeed").get<double>()}};
            legacyController(record,c.at("height").get<float>(),c.at("radius").get<float>());
            record.navigation=SceneNavigationAgent{c.value("showRoute",true)};record.playerInput.emplace();legacyPlayerInputs.insert(record.id);
            record.controller->showCollisionInGame=c.value("showCollisionInGame",false);
            record.controller->collisionColor=readCollisionColor(c,record.controller->collisionColor);
        }
        if(version>=16) {
            if(entity.contains("actor")) {if(!entity.at("actor").is_object())throw std::runtime_error("Actor must be an object.");record.actor.emplace();}
            if(entity.contains("movement") || entity.contains("motor")) {
                const auto& c=entity.at(entity.contains("movement")?"movement":"motor");record.movement=MovementComponent{c.value("enabled",true)};
                auto& movement=*record.movement;
                movement.jumpHeight=c.value("jumpHeight",movement.jumpHeight);
                movement.groundAcceleration=c.value("groundAcceleration",movement.groundAcceleration);
                movement.groundFriction=c.value("groundFriction",movement.groundFriction);
                movement.airAcceleration=c.value("airAcceleration",movement.airAcceleration);
                movement.airControl=c.value("airControl",movement.airControl);
                movement.strafeTilt=c.value("strafeTilt",movement.strafeTilt);
                movement.tiltResponse=c.value("tiltResponse",movement.tiltResponse);
                movement.stairSmoothingTime=c.value("stairSmoothingTime",movement.stairSmoothingTime);

                if(version<=20)legacyAttributes[record.id]={{stats::MovementSpeed,c.value("moveSpeed",5.0)}};
            }
            if(entity.contains("controller")) {
                const auto& c=entity.at("controller");
                if(version<=17)legacyController(record,c.value("height",1.8f),c.value("radius",.3f));
                else {record.controller.emplace();record.controller->profile=c.at("profile").get<AssetRef<NavigationAgentProfile>>();}
                record.controller->showCollisionInGame=c.value("showCollisionInGame",false);
                record.controller->collisionColor=readCollisionColor(c,record.controller->collisionColor);
                if(version>=20)record.controller->collision=readCollisionFilter(c.at("collision"));
            }
            if(entity.contains("navigation")) {const auto& c=entity.at("navigation");record.navigation=SceneNavigationAgent{c.value("showRoute",true),c.value("enabled",true)};}
            if(entity.contains("playerInput")) {
                const auto& c=entity.at("playerInput");
                if(!c.is_object())throw std::runtime_error("Player Input must be an object.");
                auto& input=record.playerInput.emplace();input.enabled=c.value("enabled",input.enabled);
                input.mouseSensitivity=c.value("mouseSensitivity",input.mouseSensitivity);input.invertY=c.value("invertY",input.invertY);
                if(c.contains("traceTest")) {
                    const auto& trace=c.at("traceTest");
                    if(!trace.is_object())throw std::runtime_error("Player Trace Test must be an object.");
                    input.traceTest.duration=trace.value("duration",input.traceTest.duration);
                    input.traceTest.length=trace.value("length",input.traceTest.length);
                    input.traceTest.collisionQuadSize=trace.value("collisionQuadSize",input.traceTest.collisionQuadSize);
                    if(trace.contains("collisionMask"))input.traceTest.collisionMask=readCollisionBits(trace.at("collisionMask"));
                    if(trace.contains("hitImpulse")) {
                        if(!trace.at("hitImpulse").is_number())throw std::runtime_error("Player Trace Test hit impulse must be a number.");
                        input.traceTest.hitImpulse=trace.at("hitImpulse").get<float>();
                    }
                }
                if(!entity.contains("movement") && record.movement) {
                    auto& movement=*record.movement;
                    movement.jumpHeight=c.value("jumpHeight",movement.jumpHeight);
                    movement.groundAcceleration=c.value("groundAcceleration",movement.groundAcceleration);
                    movement.groundFriction=c.value("groundFriction",movement.groundFriction);
                    movement.airAcceleration=c.value("airAcceleration",movement.airAcceleration);
                    movement.airControl=c.value("airControl",movement.airControl);
                    movement.strafeTilt=c.value("strafeTilt",movement.strafeTilt);
                    movement.tiltResponse=c.value("tiltResponse",movement.tiltResponse);
                    movement.stairSmoothingTime=c.value("stairSmoothingTime",movement.stairSmoothingTime);
                }
                constexpr std::array firstPersonFields{"mouseSensitivity","invertY","jumpHeight","groundAcceleration","groundFriction",
                    "airAcceleration","airControl","strafeTilt","tiltResponse","stairSmoothingTime","traceTest"};
                if(!entity.contains("movement") && std::none_of(firstPersonFields.begin(),firstPersonFields.end(),[&](const char* field){return c.contains(field);}))
                    legacyPlayerInputs.insert(record.id);
            }
        }
        if(version>=21) {
            const auto readTag=[](const Json& source) {
                const auto name=source.get<std::string>();
                return nativeGameplayTags().request(name).value_or(GameplayTag{name});
            };
            if(entity.contains("tags")) {
                const auto& source=entity.at("tags");if(!source.is_array())throw std::runtime_error("Tags must be an array of qualified names.");
                record.tags.emplace();for(const auto& item:source)record.tags->add(readTag(item));
            }
            if(version<=22&&entity.contains("attributes")) {
                const auto& source=entity.at("attributes");if(!source.is_array())throw std::runtime_error("Attributes must be an array of qualified attribute names.");
                auto& seeds=legacyAttributes[record.id];
                for(const auto& item:source) {
                    const auto tag=version==22?readTag(item):readTag(item.at("tag"));
                    const auto* definition=nativeAttributeCatalog().find(tag);
                    seeds.push_back({tag,version==22?(definition?definition->defaultBase:0):item.at("startingBase").get<double>()});
                }
            }
        }
        if(version>=22&&entity.contains("effects")) {
            const auto& source=entity.at("effects");
            if(!source.is_object()||!source.at("starting").is_array())throw std::runtime_error("Effects must contain an ordered Starting Effects array.");
            record.effects=SceneEffects{source.at("starting").get<std::vector<AssetRef<GameplayEffectAsset>>>()};
        }
        if(version>=19&&entity.contains("enemy")) {
            const auto& source=entity.at("enemy");if(!source.is_object())throw std::runtime_error("Enemy must be an object.");
            const auto behaviour=source.at("behaviour").get<std::string>();record.enemy.emplace();auto& enemy=*record.enemy;
            if(behaviour=="idle")enemy.behaviour=EnemyBehaviour::Idle;
            else if(behaviour=="chasePlayer")enemy.behaviour=EnemyBehaviour::ChasePlayer;
            else if(behaviour=="randomMove")enemy.behaviour=EnemyBehaviour::RandomMove;
            else throw std::runtime_error("Unknown Enemy behaviour.");
            enemy.radius=source.at("radius").get<float>();enemy.minWait=source.at("minWait").get<float>();enemy.maxWait=source.at("maxWait").get<float>();
        }
        if(version>=18&&entity.contains("navigationVolume")) {
            const auto& source=entity.at("navigationVolume");record.navigationVolume.emplace();auto& volume=*record.navigationVolume;
            volume.size=source.at("size").get<Vec3>();volume.enabled=source.value("enabled",true);
            volume.profiles=source.at("profiles").get<std::vector<AssetRef<NavigationAgentProfile>>>();
            const auto& bake=source.at("bake");volume.bake={bake.at("cellSize").get<float>(),bake.at("cellHeight").get<float>(),
                bake.at("tileSize").get<std::uint32_t>(),bake.at("maxSimplificationError").get<float>()};
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
            if(version>=20) {
                const auto mode=c.at("mode").get<std::string>();
                if(mode=="solid")record.collider->mode=ColliderMode::Solid;
                else if(mode=="trigger")record.collider->mode=ColliderMode::Trigger;
                else throw std::runtime_error("Unknown Collider mode.");
                record.collider->collision=readCollisionFilter(c.at("collision"));
            }
        }
        if (version>=4 && entity.contains("camera")) {
            const auto& c=entity.at("camera");
            record.camera=SceneCamera{c.at("verticalFov").get<float>(),c.at("near").get<float>(),c.at("far").get<float>()};
            if(version<=15) {
                const auto target=parseId(c.at("followTarget"));
                if(target)record.cameraFollow=SceneCameraFollow{target,c.at("followOffset").get<Vec3>()};
            }
        }
        if(version>=16 && entity.contains("cameraFollow")) {
            const auto& c=entity.at("cameraFollow");
            record.cameraFollow=SceneCameraFollow{parseId(c.at("target")),c.at("offset").get<Vec3>(),c.at("worldBasis").get<Mat4>()};
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
        if(version>=16 && entity.contains("componentOrder")) {
            const auto& authored=entity.at("componentOrder");
            if(!authored.is_array() || authored.size()>AllSceneComponentKinds.size())throw std::runtime_error("Invalid component display order.");
            std::array<bool,AllSceneComponentKinds.size()> seen{};
            for(const auto& item:authored) {
                const auto token=item.get<std::string>();
                const auto found=std::find(LegacyComponentTokens.begin(),LegacyComponentTokens.end(),token);
                if(found==LegacyComponentTokens.end())throw std::runtime_error("Unknown component display-order identity.");
                const auto index=static_cast<size_t>(found-LegacyComponentTokens.begin());
                if(seen[index])throw std::runtime_error("Duplicate component display-order identity.");
                seen[index]=true;
            }
        }
        if(version>=17&&entity.contains("outlinerBefore")) {
            const auto token=entity.at("outlinerBefore").get<std::string>();
            const auto found=std::find(LegacyComponentTokens.begin(),LegacyComponentTokens.end(),token);
            if(found==LegacyComponentTokens.end()||found==LegacyComponentTokens.begin())
                throw std::runtime_error("Invalid Outliner child anchor.");
        }
        if(version>=17&&entity.contains("outlinerStyle")) {
            const auto& style=entity.at("outlinerStyle");
            if(!style.is_object())throw std::runtime_error("Invalid Outliner style.");
            record.outlinerStyle.emplace();
            if(style.contains("color"))record.outlinerStyle->color=style.at("color").get<Vec4>();
            if(style.contains("components")) {
                const auto& colors=style.at("components");
                if(!colors.is_object()||colors.size()>AllSceneComponentKinds.size())throw std::runtime_error("Invalid component Outliner colors.");
                std::array<std::optional<Vec4>,AllSceneComponentKinds.size()> legacyColors{};
                for(const auto& [token,value]:colors.items()) {
                    const auto found=std::find(LegacyComponentTokens.begin(),LegacyComponentTokens.end(),token);
                    if(found==LegacyComponentTokens.end())throw std::runtime_error("Unknown component Outliner color identity.");
                    const auto color=value.get<Vec4>();
                    if(!math::finite(color)||std::any_of(color.begin(),color.end(),[](float channel){return channel<0||channel>1;}))
                        throw std::runtime_error("Outliner colors require finite RGBA values from zero to one.");
                    legacyColors[static_cast<size_t>(found-LegacyComponentTokens.begin())]=color;
                }
                // Entity overrides win; otherwise retain the first canonical
                // component color as the single entity appearance.
                if(!record.outlinerStyle->color)for(const auto& color:legacyColors)if(color) {record.outlinerStyle->color=color;break;}
            }
            normalizeSceneOutlinerStyle(record);
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
    if(version<=15) {
        // Legacy following cameras stored orientation in their affine hierarchy.
        // Capture that exact world basis once; subsequent target yaw must not
        // rotate the newly independent following-camera orientation.
        std::unordered_map<EntityId,const EntityRecord*> indexed;
        for(const auto& record:result.records)indexed.emplace(record.id,&record);
        for(auto& record:result.records)if(record.cameraFollow) {
            Mat4 world;auto id=record.id;auto remaining=result.records.size();
            while(id && remaining--) {
                const auto found=indexed.find(id);if(found==indexed.end())throw std::runtime_error("Scene references a missing parent entity.");
                world=found->second->local*world;id=found->second->parent;
            }
            if(id)throw std::runtime_error("Scene hierarchy contains a cycle.");
            const auto basis=math::withAffinePosition(world,{0,0,0});
            if(!basis)throw std::runtime_error("Legacy Camera Follow world basis is invalid.");
            record.cameraFollow->worldBasis=*basis;
        }
    }
    if(version<=20)for(auto& record:result.records)if(record.actor) {
        const bool player=record.playerInput||record.id==result.properties.playerCharacter;
        if(player||record.enemy) {
            auto& seeds=legacyAttributes[record.id];
            const auto defaults=player?AttributeSeeds{{stats::Health,0},{stats::MaxHealth,0},{stats::Mana,0},{stats::MaxMana,0},
                {stats::MovementSpeed,5},{stats::Vitality,50},{stats::Spirit,50},{stats::HealthRegen,0},{stats::ManaRegen,0}}:
                AttributeSeeds{{stats::Health,0},{stats::MaxHealth,100},{stats::MovementSpeed,5}};
            for(const auto& seed:defaults)if(!seedBase(seeds,seed.tag))seeds.push_back(seed);
        }
    }
    if(version<=22)for(auto& record:result.records)if(const auto found=legacyAttributes.find(record.id);found!=legacyAttributes.end()) {
        validateAttributeSeeds(found->second);
        GameplayEffectAsset effect;
        effect.name=record.name.size()<=1007?record.name+" Initial Values":record.name;
        auto ordered=found->second;
        std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.tag.name()<b.tag.name();});
        std::vector<GameplayTag> activated;
        if(version==22&&record.effects)for(const auto& reference:record.effects->starting) {
            try {
                const auto existing=loadGameplayEffectAsset(root,reference.id);
                for(const auto& modifier:existing.modifiers)if(nativeAttributeCatalog().find(modifier.attribute))activated.push_back(modifier.attribute);
            }catch(const std::exception&) { /* Preserve unavailable references for runtime diagnosis. */ }
        }
        const auto closure=nativeAttributeCatalog().activationClosure(activated);
        auto provenance=Json::array();
        for(const auto& seed:ordered)if(seed.tag!=stats::Health&&seed.tag!=stats::Mana&&
            (version<=21||std::find(closure.begin(),closure.end(),seed.tag)==closure.end())) {
            effect.modifiers.push_back({seed.tag,EffectModifierOperation::Override,{seed.startingBase,{},{}}});
            provenance.push_back({seed.tag.name(),seed.startingBase});
        }
        if(!effect.modifiers.empty()) {
            // Keep <=21 identities stable across the subsequent schema removal.
            const auto conversion=version<=21?"scene-v22-starting:":"scene-v23-starting:";
            effect.id=derivedAssetId(owner,conversion+hexId(record.id)+":"+hexId(fingerprint(provenance.dump())));
            validateGameplayEffectAsset(effect);
            if(!record.effects)record.effects.emplace();
            record.effects->starting.insert(record.effects->starting.begin(),AssetRef<GameplayEffectAsset>{effect.id});
            result.stagedGameplayEffectEntities.emplace_back(record.id,effect.id);
            result.stagedGameplayEffects.push_back(std::make_shared<const GameplayEffectAsset>(std::move(effect)));
        }
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
    constexpr float cameraRadians=3.14159265358979323846f/180.f;
    const auto oldPlayerCameraBasis=math::eulerRotationXYZ(Vec3{-55,45,0}*cameraRadians);
    const auto legacyPlayerCamera=[&](const EntityRecord& camera) {
        return legacyPlayerInputs.contains(result.properties.playerCharacter) &&
            camera.camera==std::optional<SceneCamera>{SceneCamera{35.f}} && camera.cameraFollow &&
            camera.cameraFollow->target==result.properties.playerCharacter && camera.cameraFollow->offset==Vec3{6.5f,13.1f,6.5f} &&
            camera.cameraFollow->worldBasis==oldPlayerCameraBasis;
    };
    if(version<=16 && result.properties.playerCharacter && result.properties.activeCamera) {
        const auto actor=std::find_if(result.records.begin(),result.records.end(),[&](const auto& record){return record.id==result.properties.playerCharacter;});
        const auto camera=std::find_if(result.records.begin(),result.records.end(),[&](const auto& record){return record.id==result.properties.activeCamera;});
        if(actor!=result.records.end() && actor->actor && camera!=result.records.end() && !camera->parent &&
           camera->cameraFollow && camera->cameraFollow->target==actor->id) {
            // Adopt a root-facing eye camera or the proven built-in recipe.
            // Custom world-facing cameras remain standalone follows so making
            // the Player's own eye root-oriented cannot change their view.
            Scene previous;previous.apply(result.records,result.properties);
            const auto parentWorld=previous.worldTransform(actor->id),cameraWorld=previous.worldTransform(camera->id);
            const auto inverseParent=parentWorld?math::inverse(*parentWorld):std::nullopt;
            if(!inverseParent || !cameraWorld)throw std::runtime_error("The legacy player camera has an invalid parent transform.");
            const auto rootBasis=firstPersonCameraBasis(*parentWorld);
            const auto basis=math::withAffinePosition(*cameraWorld,{});
            bool rootFacing=basis.has_value();
            if(basis)for(unsigned index=0;index<16;++index)rootFacing=rootFacing && std::abs((*basis)[index]-rootBasis[index])<=1e-5f;
            if(rootFacing || legacyPlayerCamera(*camera)) {
                camera->parent=actor->id;camera->order=nextOrder(result.records,actor->id);
                camera->local=*inverseParent * *cameraWorld;
            }
        }
    }
    if(version<=19)for(auto& record:result.records)if(record.controller)
        record.controller->collision={record.playerInput?PlayerCollisionLayer:record.enemy?EnemyCollisionLayer:CharacterCollisionLayer,AllCollisionLayers};
    if(legacyPlayerInputs.contains(result.properties.playerCharacter)) {
        // Only the exact former built-in recipe is safe to replace. Custom
        // camera framing, standalone overrides and all authored entities survive.
        for(auto& camera:result.records)if(camera.id==result.properties.activeCamera&&camera.parent==result.properties.playerCharacter&&
            legacyPlayerCamera(camera)) {
            Scene previous;previous.apply(result.records,result.properties);
            const auto targetWorld=previous.worldTransform(camera.cameraFollow->target),parentWorld=previous.worldTransform(camera.parent);
            const auto inverseParent=parentWorld?math::inverse(*parentWorld):std::nullopt;
            if(!targetWorld||!inverseParent)throw std::runtime_error("The legacy player camera has an invalid parent transform.");
            const auto firstPerson=defaultPlayerCameraTemplate();
            camera.camera=firstPerson.lens;camera.cameraFollow->offset=firstPerson.offset;camera.cameraFollow->worldBasis=firstPerson.worldBasis;
            camera.local=*inverseParent * *math::withAffinePosition(firstPerson.worldBasis,math::transformPoint(*targetWorld,{})+firstPerson.offset);
        }
    }
    Scene::validate(result.records,result.properties);
    return result;
}
void canonicalizeSceneAssetRefs(SceneSnapshot& snapshot) {
    snapshot.id=canonicalAssetId(snapshot.id);
    for(auto& record:snapshot.records) {
        if(record.model)record.model->id=canonicalAssetId(record.model->id);
        record.modelRagdoll.asset.id=canonicalAssetId(record.modelRagdoll.asset.id);
        for(auto& material:record.modelMaterials)material.id=canonicalAssetId(material.id);
        for(auto& binding:record.blendSettings)binding.material.id=canonicalAssetId(binding.material.id);
        if(record.particle)record.particle->id=canonicalAssetId(record.particle->id);
        if(record.decal){record.decal->asset.id=canonicalAssetId(record.decal->asset.id);record.decal->seed.modelId=canonicalAssetId(record.decal->seed.modelId);}
        if(record.collider)record.collider->source.id=canonicalAssetId(record.collider->source.id);
        if(record.controller)record.controller->profile.id=canonicalAssetId(record.controller->profile.id);
        if(record.navigationVolume)for(auto& profile:record.navigationVolume->profiles)profile.id=canonicalAssetId(profile.id);
        if(record.effects)for(auto& effect:record.effects->starting)effect.id=canonicalAssetId(effect.id);
        if(record.environment)record.environment->environment.map=canonicalAssetId(record.environment->environment.map);
    }
}
Json writeScene(const SceneSnapshot& snapshot) {
    Scene::validate(snapshot.records,snapshot.properties);
    Json entities = Json::array();
    for (const auto& record : snapshot.records) {
        Json entity{{"id",hexId(record.id)}, {"name",record.name}, {"parent",hexId(record.parent)},
            {"order",record.order}, {"local",record.local}, {"visible",record.visible}};
        if(record.outlinerStyle && record.outlinerStyle->color)
            entity["outlinerStyle"]={{"color",*record.outlinerStyle->color}};

        if (record.model) {
            entity["model"] = *record.model;
            if(record.meshPaint)entity["meshPaint"]=meshPaintJson(*record.meshPaint);
            if(!record.blendSettings.empty())entity["blendSettings"]=blendSettingsJson(record.blendSettings);
            if(record.primitive) {
                const auto& primitive=*record.primitive;
                const auto base=canonicalAssetId(record.model->id);
                if(base==BuiltinBoxModelId)entity["primitive"]={{"size",primitive.size}};
                else if(base==BuiltinSphereModelId)entity["primitive"]={{"radius",primitive.radius},{"sides",primitive.sides},{"rings",primitive.rings}};
                else entity["primitive"]={{"radius",primitive.radius},{"height",primitive.height},{"sides",primitive.sides}};
            }
            entity["meshShading"]=static_cast<unsigned>(record.meshShading);
            entity["castShadows"]=record.castShadows;entity["receiveShadows"]=record.receiveShadows;
            entity["lightingScopes"]={{"directReceiverMask",record.lightingScopes.directReceiverMask},
                {"shadowCasterMask",record.lightingScopes.shadowCasterMask}};
            if(!record.modelRagdoll.asset.empty())entity["modelRagdoll"]={{"asset",record.modelRagdoll.asset},{"startSimulated",record.modelRagdoll.startSimulated}};
            const auto& animation=record.modelAnimation;
            entity["modelAnimation"]={{"asset",animation.asset},{"playing",animation.playing},
                {"speed",animation.speed},{"startTime",animation.startTime}};
            if(animation.loopOverride)entity["modelAnimation"]["loopOverride"]=*animation.loopOverride;
        }
        auto materials=record.modelMaterials;trimModelMaterialOverrides(materials);
        if(!materials.empty())entity["modelMaterials"]=materials;
        if (record.particle) entity["particle"] = *record.particle;
        if(!record.receiveDecals)entity["receiveDecals"]=false;
        if(record.decal)entity["decal"]=decalJson(*record.decal);
        if (record.directional) entity["directional"] = lightJson(*record.directional);
        if (record.point) entity["point"] = lightJson(*record.point);
        if (record.environment) entity["environment"]=environmentJson(*record.environment);
        if(record.actor)entity["actor"]=Json::object();
        if(record.movement) {const auto& c=*record.movement;entity["movement"]={{"enabled",c.enabled},
            {"jumpHeight",c.jumpHeight},{"groundAcceleration",c.groundAcceleration},{"groundFriction",c.groundFriction},
            {"airAcceleration",c.airAcceleration},{"airControl",c.airControl},{"strafeTilt",c.strafeTilt},
            {"tiltResponse",c.tiltResponse},{"stairSmoothingTime",c.stairSmoothingTime}};}
        if(record.tags) {
            auto tags=Json::array();for(const auto& tag:record.tags->tags())tags.push_back(tag.name());entity["tags"]=std::move(tags);
        }
        if(record.effects)entity["effects"]={{"starting",record.effects->starting}};
        if(record.controller) {const auto& c=*record.controller;entity["controller"]={{"profile",c.profile},{"collision",collisionFilterJson(c.collision)},{"showCollisionInGame",c.showCollisionInGame},{"collisionColor",c.collisionColor}};}
        if(record.navigation)entity["navigation"]={{"showRoute",record.navigation->showRoute},{"enabled",record.navigation->enabled}};
        if(record.enemy) {const auto& enemy=*record.enemy;
            const auto behaviour=enemy.behaviour==EnemyBehaviour::Idle?"idle":enemy.behaviour==EnemyBehaviour::ChasePlayer?"chasePlayer":"randomMove";
            entity["enemy"]={{"behaviour",behaviour},{"radius",enemy.radius},{"minWait",enemy.minWait},{"maxWait",enemy.maxWait}};
        }
        if(record.navigationVolume) {const auto& volume=*record.navigationVolume;const auto& bake=volume.bake;
            entity["navigationVolume"]={{"size",volume.size},{"enabled",volume.enabled},{"profiles",volume.profiles},
                {"bake",{{"cellSize",bake.cellSize},{"cellHeight",bake.cellHeight},{"tileSize",bake.tileSize},{"maxSimplificationError",bake.maxSimplificationError}}}};}
        if(record.playerInput) {const auto& c=*record.playerInput;entity["playerInput"]={{"enabled",c.enabled},
            {"mouseSensitivity",c.mouseSensitivity},{"invertY",c.invertY},
            {"traceTest",{{"duration",c.traceTest.duration},{"length",c.traceTest.length},
                {"collisionQuadSize",c.traceTest.collisionQuadSize},{"collisionMask",c.traceTest.collisionMask},{"hitImpulse",c.traceTest.hitImpulse}}}};}
        if (record.collider) { const auto& c=*record.collider;
            const auto shape=c.shape==ColliderShape::Box?"box":c.shape==ColliderShape::Sphere?"sphere":c.shape==ColliderShape::Cylinder?"cylinder":c.shape==ColliderShape::Capsule?"capsule":"trimesh";
            entity["collider"]={{"shape",shape},{"mode",c.mode==ColliderMode::Solid?"solid":"trigger"},{"collision",collisionFilterJson(c.collision)},{"size",c.size},{"radius",c.radius},{"height",c.height},{"source",c.source},
                {"showCollisionInGame",c.showCollisionInGame},{"collisionColor",c.collisionColor}};
            if(c.mesh)entity["collider"]["mesh"]={{"positions",c.mesh->positions},{"indices",c.mesh->indices}}; }
        if (record.camera) { const auto& c=*record.camera;entity["camera"]={{"verticalFov",c.verticalFov},{"near",c.nearPlane},{"far",c.farPlane}}; }
        if(record.cameraFollow) {const auto& c=*record.cameraFollow;entity["cameraFollow"]={{"target",hexId(c.target)},{"offset",c.offset},{"worldBasis",c.worldBasis}};}
        if(record.quader) {const auto& q=*record.quader;entity["quader"]={{"projectPath",q.projectPath},{"projectId",q.projectId},
            {"sceneId",q.sceneId},{"importScale",q.importScale},{"autoSync",q.autoSync},{"refreshMaterials",q.refreshMaterials},
            {"saveKey",q.saveKey},{"syncLights",q.syncLights}};}
        if(record.quaderGenerated) {const auto& q=*record.quaderGenerated;entity["quaderGenerated"]={{"owner",hexId(q.owner)},{"sourceKey",q.sourceKey}};}
        entities.push_back(std::move(entity));
    }
    Json result{{"version",25},{"type","crimson-scene"},{"name",snapshot.name},{"entities",std::move(entities)},
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
enum EntityField : uint64_t {
    Name=1ull<<0, Parent=1ull<<1, Order=1ull<<2, Local=1ull<<3, Model=1ull<<4, Particle=1ull<<5,
    Directional=1ull<<6, Point=1ull<<7, Environment=1ull<<8, ActorField=1ull<<9, Collider=1ull<<10, CameraField=1ull<<11,
    ModelMaterialField=1ull<<12, CastShadows=1ull<<13, ReceiveShadows=1ull<<14,
    QuaderField=1ull<<15,QuaderGeneratedField=1ull<<16,LightingScopesField=1ull<<17,
    ModelAnimationField=1ull<<18,VisibleField=1ull<<20,MotorField=1ull<<21,ControllerField=1ull<<22,NavigationField=1ull<<23,
    PlayerInputField=1ull<<24,CameraFollowField=1ull<<27,
    OutlinerStyleField=1ull<<28,NavigationVolumeField=1ull<<29,EnemyField=1ull<<30,
    // Bit31 was authored Attributes; it is permanently reserved.
    TagsField=1ull<<32,EffectsField=1ull<<33,MeshShadingField=1ull<<34,PrimitiveMeshField=1ull<<35,DecalField=1ull<<36,ReceiveDecalsField=1ull<<37,
    ModelRagdollField=1ull<<38,MeshPaintField=1ull<<39,BlendSettingsField=1ull<<40,
    AllEntityFields=((1ull<<41)-1)&~(1ull<<31)
};
enum PropertyField : uint32_t { ActiveEnvironment=1u<<0,ActiveCamera=1u<<1,PlayerCharacter=1u<<2 };
template<class T> bool equalLight(const std::optional<T>& a,const std::optional<T>& b) {
    return a==b;
}
uint64_t changedFields(const EntityRecord& a,const EntityRecord& b) {
    uint64_t fields=0;
    if(a.name!=b.name)fields|=Name;if(a.parent!=b.parent)fields|=Parent;if(a.order!=b.order)fields|=Order;
    if(a.local!=b.local)fields|=Local;if(a.model!=b.model)fields|=Model;if(a.particle!=b.particle)fields|=Particle;
    if(!equalLight(a.directional,b.directional))fields|=Directional;if(!equalLight(a.point,b.point))fields|=Point;
    if(a.environment.has_value()!=b.environment.has_value() || (a.environment && !sameEnvironment(*a.environment,*b.environment)))fields|=Environment;
    if(a.actor!=b.actor)fields|=ActorField;if(a.collider!=b.collider)fields|=Collider;if(a.camera!=b.camera)fields|=CameraField;
    if(a.modelMaterials!=b.modelMaterials)fields|=ModelMaterialField;
    if(a.castShadows!=b.castShadows)fields|=CastShadows;if(a.receiveShadows!=b.receiveShadows)fields|=ReceiveShadows;
    if(a.quader!=b.quader)fields|=QuaderField;
    if(a.quaderGenerated!=b.quaderGenerated)fields|=QuaderGeneratedField;
    if(a.lightingScopes!=b.lightingScopes)fields|=LightingScopesField;
    if(a.modelAnimation!=b.modelAnimation)fields|=ModelAnimationField;
    if(a.modelRagdoll!=b.modelRagdoll)fields|=ModelRagdollField;
    if(a.meshShading!=b.meshShading)fields|=MeshShadingField;
    if(a.primitive!=b.primitive)fields|=PrimitiveMeshField;
    if(a.meshPaint!=b.meshPaint)fields|=MeshPaintField;
    if(a.blendSettings!=b.blendSettings)fields|=BlendSettingsField;
    if(a.decal!=b.decal)fields|=DecalField;if(a.receiveDecals!=b.receiveDecals)fields|=ReceiveDecalsField;
    if(a.visible!=b.visible)fields|=VisibleField;
    if(a.movement!=b.movement)fields|=MotorField;
    if(a.controller!=b.controller)fields|=ControllerField;
    if(a.navigation!=b.navigation)fields|=NavigationField;
    if(a.enemy!=b.enemy)fields|=EnemyField;
    if(a.navigationVolume!=b.navigationVolume)fields|=NavigationVolumeField;
    if(a.playerInput!=b.playerInput)fields|=PlayerInputField;
    if(a.cameraFollow!=b.cameraFollow)fields|=CameraFollowField;
    if(a.outlinerStyle!=b.outlinerStyle)fields|=OutlinerStyleField;
    if(a.tags!=b.tags)fields|=TagsField;
    if(a.effects!=b.effects)fields|=EffectsField;
    return fields;
}
void copyFields(EntityRecord& target,const EntityRecord& source,uint64_t fields) {
    if(fields&Name)target.name=source.name;if(fields&Parent)target.parent=source.parent;if(fields&Order)target.order=source.order;
    if(fields&Local)target.local=source.local;if(fields&Model)target.model=source.model;if(fields&Particle)target.particle=source.particle;
    if(fields&Directional)target.directional=source.directional;if(fields&Point)target.point=source.point;
    if(fields&Environment)target.environment=source.environment;if(fields&ActorField)target.actor=source.actor;
    if(fields&Collider)target.collider=source.collider;if(fields&CameraField)target.camera=source.camera;
    if(fields&ModelMaterialField)target.modelMaterials=source.modelMaterials;
    if(fields&CastShadows)target.castShadows=source.castShadows;if(fields&ReceiveShadows)target.receiveShadows=source.receiveShadows;
    if(fields&QuaderField)target.quader=source.quader;
    if(fields&QuaderGeneratedField)target.quaderGenerated=source.quaderGenerated;
    if(fields&LightingScopesField)target.lightingScopes=source.lightingScopes;
    if(fields&ModelAnimationField)target.modelAnimation=source.modelAnimation;
    if(fields&ModelRagdollField)target.modelRagdoll=source.modelRagdoll;
    if(fields&MeshShadingField)target.meshShading=source.meshShading;
    if(fields&PrimitiveMeshField)target.primitive=source.primitive;
    if(fields&MeshPaintField)target.meshPaint=source.meshPaint;
    if(fields&BlendSettingsField)target.blendSettings=source.blendSettings;
    if(fields&DecalField)target.decal=source.decal;if(fields&ReceiveDecalsField)target.receiveDecals=source.receiveDecals;
    if(fields&VisibleField)target.visible=source.visible;
    if(fields&MotorField)target.movement=source.movement;
    if(fields&ControllerField)target.controller=source.controller;
    if(fields&NavigationField)target.navigation=source.navigation;
    if(fields&EnemyField)target.enemy=source.enemy;
    if(fields&NavigationVolumeField)target.navigationVolume=source.navigationVolume;
    if(fields&PlayerInputField)target.playerInput=source.playerInput;
    if(fields&CameraFollowField)target.cameraFollow=source.cameraFollow;
    if(fields&OutlinerStyleField)target.outlinerStyle=source.outlinerStyle;
    if(fields&TagsField)target.tags=source.tags;
    if(fields&EffectsField)target.effects=source.effects;
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
    if(std::none_of(records.begin(),records.end(),[](const auto& record){return record.cameraFollow.has_value();}))return;
    Scene::validate(records,properties);
    std::unordered_map<EntityId,const EntityRecord*> indexed;
    for(const auto& record:records)indexed.emplace(record.id,&record);
    const auto rawWorld=[&](EntityId id) {
        Mat4 world;
        while(id){const auto& record=*indexed.at(id);world=record.local*world;id=record.parent;}
        return world;
    };
    for(auto& record:records) {
        if(!record.cameraFollow)continue;
        const auto before=previous.find(record.id);
        if(!before || before->cameraFollow!=record.cameraFollow || before->parent!=record.parent || before->local==record.local)continue;
        // Only a deliberate camera-local pose edit changes follow settings.
        // Editing an ancestor/target leaves local untouched and retains the
        // captured world-space target offset. An FPS camera's root owns its
        // orientation; generic follows retain their captured world basis.
        const auto world=rawWorld(record.id),target=rawWorld(record.cameraFollow->target);
        const auto basis=math::withAffinePosition(world,{0,0,0});
        if(!basis)throw std::runtime_error("Camera Follow edit produced an invalid world basis.");
        record.cameraFollow->offset=math::transformPoint(world,{0,0,0})-math::transformPoint(target,{0,0,0});
        record.cameraFollow->worldBasis=isFirstPersonCamera(previous,record.id)?Mat4{}:*basis;
    }
}
}

std::filesystem::path sceneAssetPath(const std::filesystem::path& root,const std::string& id) { return scenePath(root,id); }
SceneSnapshot readSceneSnapshot(const std::filesystem::path& root,const std::string& id) {
    auto result=readScene(scenePath(root,id),canonicalAssetId(id),root); result.id=id;
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
    if((!snapshot.stagedNavigationProfiles.empty()||!snapshot.stagedGameplayEffects.empty())&&!contentStorageReady(root))initializeContentStorage(root);
    auto plan=prepareScenePersistence(root,id,snapshot);
    if(!plan.registrations.empty())plan.files.push_back({".crimson/asset-locations.json",prepareAssetPathRegistration(root,plan.registrations)});
    commitAssetTransaction(root,plan.files);if(!plan.registrations.empty())refreshContentStorage(root);
}
ScenePersistencePlan prepareScenePersistence(const std::filesystem::path& root,const std::string& id,const SceneSnapshot& snapshot,const std::filesystem::path& sceneFile) {
    auto current=snapshot;current.id=contentStorageReady(root)?canonicalAssetId(id):id;
    if(contentStorageReady(root))canonicalizeSceneAssetRefs(current);
    std::unordered_set<std::string> referenced;
    for(const auto& record:current.records) {
        if(record.controller)referenced.insert(record.controller->profile.id);
        if(record.navigationVolume)for(const auto& profile:record.navigationVolume->profiles)referenced.insert(profile.id);
    }
    ScenePersistencePlan plan;
    const auto indexed=contentStorageReady(root)?storedAssetPaths(root):std::vector<StoredAssetPath>{};
    std::unordered_set<std::string> staged;
    for(const auto& profile:current.stagedNavigationProfiles)if(profile&&referenced.contains(profile->id)&&staged.insert(profile->id).second) {
        validateNavigationAgentProfile(*profile);
        const auto existing=std::find_if(indexed.begin(),indexed.end(),[&](const auto& entry){return entry.id==profile->id;});
        if(existing!=indexed.end()) {
            // An independently saved/edited profile is now authoritative. Do
            // not resurrect a deleted file or overwrite it with conversion data.
            (void)loadNavigationAgentProfile(root,profile->id);continue;
        }
        if(!contentStorageReady(root))throw std::runtime_error("Initialize Content storage before saving converted Navigation Agent Profiles.");
        const auto file=checkedContentPath(root,root/"Content"/"Navigation"/fromUtf8(profile->id+".crimson-agent-profile"));
        if(std::filesystem::exists(file))throw std::runtime_error("A converted Navigation Agent Profile destination already exists without registration.");
        plan.files.push_back({utf8(file.lexically_relative(root)),serializeNavigationAgentProfile(*profile)});
        plan.registrations.push_back({profile->id,file});
    }
    std::unordered_set<std::string> effectReferences;
    for(const auto& record:current.records)if(record.effects)for(const auto& effect:record.effects->starting)effectReferences.insert(effect.id);
    staged.clear();
    for(const auto& effect:current.stagedGameplayEffects)if(effect&&effectReferences.contains(effect->id)&&staged.insert(effect->id).second) {
        validateGameplayEffectAsset(*effect);
        const auto existing=std::find_if(indexed.begin(),indexed.end(),[&](const auto& entry){return entry.id==effect->id;});
        if(existing!=indexed.end()) {
            // Once authored independently, even a missing/corrupt managed file
            // must fail instead of reviving or overwriting this conversion seed.
            (void)loadGameplayEffectAsset(root,effect->id);continue;
        }
        if(!contentStorageReady(root))throw std::runtime_error("Initialize Content storage before saving converted Gameplay Effects.");
        const auto file=checkedContentPath(root,root/"Content"/"Gameplay"/fromUtf8(effect->id+".crimson-effect"));
        if(std::filesystem::exists(file))throw std::runtime_error("A converted Gameplay Effect destination already exists without registration.");
        plan.files.push_back({utf8(file.lexically_relative(root)),serializeGameplayEffectAsset(*effect)});
        plan.registrations.push_back({effect->id,file});
    }
    const auto file=sceneFile.empty()?scenePath(root,id):sceneFile;
    plan.files.push_back({utf8(file.lexically_relative(root)),serializeSceneSnapshot(current)});
    if(contentStorageReady(root))plan.registrations.push_back({current.id,file});
    return plan;
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
    // Account shared immutable pages once, even when unchanged payloads appear
    // in many transform commands. Remove whole old commands under pressure.
    std::unordered_map<const MeshPaintPage*,std::size_t> references;
    const auto pages=[&](const SceneCommand& entry,const auto& visit){for(const auto& change:entry.entities)for(const auto* record:{&change.before,&change.after})if(*record&&(*record)->meshPaint)for(const auto& draw:(*record)->meshPaint->draws)if(draw.weights)for(const auto& page:draw.weights->pages)if(page)visit(page.get());};
    for(const auto& entry:commands_)pages(entry,[&](const auto* page){++references[page];});
    constexpr std::size_t paintHistoryBytes=128ull*1024*1024;
    std::size_t first=0;while(commands_.size()-first>1&&references.size()*sizeof(MeshPaintPage)>paintHistoryBytes){pages(commands_[first++],[&](const auto* page){const auto found=references.find(page);if(--found->second==0)references.erase(found);});}
    if(first)commands_.erase(commands_.begin(),commands_.begin()+static_cast<std::ptrdiff_t>(first));
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
        previewCamera_=source.previewCamera_;loadedFingerprint_=source.loadedFingerprint_;stagedNavigationProfiles_=source.stagedNavigationProfiles_;stagedNavigationControllers_=source.stagedNavigationControllers_;
        stagedGameplayEffects_=source.stagedGameplayEffects_;stagedGameplayEffectEntities_=source.stagedGameplayEffectEntities_;transaction_.reset();history_.reset(true);
        transient_=true;++documentGeneration_;status_.clear();return true;
    }catch(const std::exception& error){status_=error.what();return false;}
}
std::shared_ptr<const NavigationAgentProfile> SceneDocument::navigationAgentProfile(const std::string& id) const {
    if(id==DefaultNavigationAgentProfileId)return defaultNavigationAgentProfile();
    const auto found=stagedNavigationProfiles_.find(id);if(found==stagedNavigationProfiles_.end())return nullptr;
    // Index membership means this identity became authored independently. Even
    // a subsequently missing/corrupt file must not resurrect its migration seed.
    if(contentStorageReady(projectRoot_))for(const auto& entry:storedAssetPaths(projectRoot_))if(entry.id==id)return nullptr;
    return found->second;
}
std::vector<std::shared_ptr<const NavigationAgentProfile>> SceneDocument::stagedNavigationAgentProfiles() const {
    std::vector<std::shared_ptr<const NavigationAgentProfile>> result;result.reserve(stagedNavigationProfiles_.size());
    for(const auto& [id,profile]:stagedNavigationProfiles_){(void)id;result.push_back(profile);}
    return result;
}
std::shared_ptr<const NavigationAgentProfile> SceneDocument::legacyNavigationAgentProfileSeedForController(EntityId id) const {
    const auto owner=stagedNavigationControllers_.find(id);if(owner==stagedNavigationControllers_.end())return nullptr;
    const auto seed=stagedNavigationProfiles_.find(owner->second);return seed==stagedNavigationProfiles_.end()?nullptr:seed->second;
}
std::vector<std::string> SceneDocument::historicalNavigationAgentProfileReferences() const {
    std::unordered_set<std::string> references;
    const auto append=[&](const EntityRecord& record) {
        if(record.controller)references.insert(record.controller->profile.id);
        if(record.navigationVolume)for(const auto& profile:record.navigationVolume->profiles)references.insert(profile.id);
    };
    for(const auto& command:history_.commands_)for(const auto& change:command.entities){if(change.before)append(*change.before);if(change.after)append(*change.after);}
    if(transaction_)for(const auto& change:transaction_->command.entities){if(change.before)append(*change.before);if(change.after)append(*change.after);}
    std::vector<std::string> result(references.begin(),references.end());std::sort(result.begin(),result.end());return result;
}
std::shared_ptr<const GameplayEffectAsset> SceneDocument::gameplayEffect(const std::string& id) const {
    if(engineGameplayEffect(id)) {
        try {return std::make_shared<const GameplayEffectAsset>(loadGameplayEffectAsset(projectRoot_,id));}
        catch(const std::exception&) {return nullptr;}
    }
    const auto found=stagedGameplayEffects_.find(id);if(found==stagedGameplayEffects_.end())return nullptr;
    if(contentStorageReady(projectRoot_))for(const auto& entry:storedAssetPaths(projectRoot_))if(entry.id==id)return nullptr;
    if(std::filesystem::exists(projectRoot_/"Content"/"Gameplay"/fromUtf8(id+".crimson-effect")))return nullptr;
    return found->second;
}
std::vector<std::shared_ptr<const GameplayEffectAsset>> SceneDocument::stagedGameplayEffects() const {
    std::vector<std::shared_ptr<const GameplayEffectAsset>> result;result.reserve(stagedGameplayEffects_.size());
    for(const auto& [id,effect]:stagedGameplayEffects_){(void)id;result.push_back(effect);}
    return result;
}
std::shared_ptr<const GameplayEffectAsset> SceneDocument::legacyGameplayEffectSeedForEntity(EntityId id) const {
    const auto owner=stagedGameplayEffectEntities_.find(id);if(owner==stagedGameplayEffectEntities_.end())return nullptr;
    const auto seed=stagedGameplayEffects_.find(owner->second);return seed==stagedGameplayEffects_.end()?nullptr:seed->second;
}
std::vector<std::string> SceneDocument::historicalGameplayEffectReferences() const {
    std::unordered_set<std::string> references;
    const auto append=[&](const EntityRecord& record){if(record.effects)for(const auto& effect:record.effects->starting)references.insert(effect.id);};
    for(const auto& command:history_.commands_)for(const auto& change:command.entities){if(change.before)append(*change.before);if(change.after)append(*change.after);}
    if(transaction_)for(const auto& change:transaction_->command.entities){if(change.before)append(*change.before);if(change.after)append(*change.after);}
    std::vector<std::string> result(references.begin(),references.end());std::sort(result.begin(),result.end());return result;
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
    stagedNavigationProfiles_.clear();
    stagedNavigationControllers_.clear();
    stagedGameplayEffects_.clear();stagedGameplayEffectEntities_.clear();
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
        if((!stagedNavigationProfiles_.empty()||!stagedGameplayEffects_.empty())&&!contentStorageReady(projectRoot_)) {
            if(!assetId_.empty()&&!overwriteExternal&&externalFileChanged())throw std::runtime_error("The scene changed outside the editor before profile conversion could be saved.");
            initializeContentStorage(projectRoot_);
            if(!assetId_.empty())loadedFingerprint_=sceneFileFingerprint(projectRoot_,assetId_);
        }
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
                    const auto candidateFilename=filename+(suffix>1?" "+std::to_string(suffix):std::string{})+".crimson-scene";
                    file=checkedContentPath(projectRoot_,projectRoot_/"Content"/fromUtf8(candidateFilename));
                    if(!std::filesystem::exists(file))break;
                }
            } else {
                do { savedId = "assets/scenes/" + uniqueToken() + ".crimson-scene"; }
                while (std::filesystem::exists(scenePath(projectRoot_,savedId)));
            }
        }
        if (!saveAs && !assetId_.empty() && !overwriteExternal && externalFileChanged())
            throw std::runtime_error("The scene file changed outside the editor. Reload, Save As, or explicitly overwrite it.");
        SceneSnapshot snapshot{savedId,savedName,scene_.records(),scene_.properties(),previewCamera_};snapshot.stagedNavigationProfiles=stagedNavigationAgentProfiles();snapshot.stagedGameplayEffects=stagedGameplayEffects();
        const auto contents=serializeSceneSnapshot(snapshot);
        if(file.empty())file=scenePath(projectRoot_,savedId);
        auto plan=prepareScenePersistence(projectRoot_,savedId,snapshot,file);
        if(!plan.registrations.empty())plan.files.push_back({".crimson/asset-locations.json",prepareAssetPathRegistration(projectRoot_,plan.registrations)});
        commitAssetTransaction(projectRoot_,plan.files);if(!plan.registrations.empty())refreshContentStorage(projectRoot_);
        if(contentStorageReady(projectRoot_))for(const auto& entry:storedAssetPaths(projectRoot_)){stagedNavigationProfiles_.erase(entry.id);stagedGameplayEffects_.erase(entry.id);}
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
        if(record.decal&&!record.decal->seed.modelId.empty())references.insert(record.decal->seed.modelId);
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
std::vector<std::string> SceneDocument::historicalDecalReferences() const {
    std::unordered_set<std::string> references;const auto add=[&](const EntityRecord& record){if(record.decal&&!record.decal->asset.empty())references.insert(record.decal->asset.id);};
    for(const auto& command:history_.commands_)for(const auto& change:command.entities){if(change.before)add(*change.before);if(change.after)add(*change.after);}
    if(transaction_)for(const auto& change:transaction_->command.entities){if(change.before)add(*change.before);if(change.after)add(*change.after);}
    std::vector<std::string> result(references.begin(),references.end());std::sort(result.begin(),result.end());return result;
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
        for(const auto& binding:record.blendSettings)references.insert(binding.material.id);
    };
    for(const auto& command:history_.commands_)for(const auto& change:command.entities) {
        if(change.before)add(*change.before);if(change.after)add(*change.after);
    }
    if(transaction_)for(const auto& change:transaction_->command.entities) {if(change.before)add(*change.before);if(change.after)add(*change.after);}
    std::vector<std::string> result(references.begin(),references.end());std::sort(result.begin(),result.end());return result;
}
std::vector<std::string> SceneDocument::historicalAnimationReferences() const {
    std::unordered_set<std::string> references;
    const auto add=[&](const EntityRecord& record) {
        if(!record.modelAnimation.asset.empty())references.insert(record.modelAnimation.asset.id);
    };
    for(const auto& command:history_.commands_)for(const auto& change:command.entities) {
        if(change.before)add(*change.before);if(change.after)add(*change.after);
    }
    if(transaction_)for(const auto& change:transaction_->command.entities) {if(change.before)add(*change.before);if(change.after)add(*change.after);}
    std::vector<std::string> result(references.begin(),references.end());std::sort(result.begin(),result.end());return result;
}
std::vector<std::string> SceneDocument::historicalRagdollReferences() const {
    std::unordered_set<std::string> references;
    const auto add=[&](const EntityRecord& record) {if(!record.modelRagdoll.asset.empty())references.insert(record.modelRagdoll.asset.id);};
    for(const auto& command:history_.commands_)for(const auto& change:command.entities) {if(change.before)add(*change.before);if(change.after)add(*change.after);}
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
        std::unordered_map<std::string,std::shared_ptr<const NavigationAgentProfile>> stagedProfiles;
        for(const auto& profile:current.stagedNavigationProfiles)if(profile){validateNavigationAgentProfile(*profile);stagedProfiles.emplace(profile->id,profile);}
        std::unordered_map<EntityId,std::string> stagedControllers(current.stagedNavigationControllers.begin(),current.stagedNavigationControllers.end());
        std::unordered_map<std::string,std::shared_ptr<const GameplayEffectAsset>> stagedEffects;
        for(const auto& effect:current.stagedGameplayEffects)if(effect){validateGameplayEffectAsset(*effect);stagedEffects.emplace(effect->id,effect);}
        std::unordered_map<EntityId,std::string> stagedEffectEntities(current.stagedGameplayEffectEntities.begin(),current.stagedGameplayEffectEntities.end());
        scene_.apply(current.records,current.properties);
        assetId_=std::move(preparedId); name_=std::move(preparedName); previewCamera_=current.previewCamera;
        loadedFingerprint_=current.fingerprint; transaction_.reset(); history_.reset(true);
        stagedNavigationProfiles_=std::move(stagedProfiles);
        stagedNavigationControllers_=std::move(stagedControllers);
        stagedGameplayEffects_=std::move(stagedEffects);stagedGameplayEffectEntities_=std::move(stagedEffectEntities);
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
    std::unordered_set<std::string> effectReferences;
    for(const auto& record:records)if(record.effects)for(const auto& effect:record.effects->starting)effectReferences.insert(effect.id);
    for(const auto& [id,effect]:stagedGameplayEffects_)if(effectReferences.contains(id))prepared.stagedEffects.emplace(id,effect);
    for(const auto& [entity,id]:stagedGameplayEffectEntities_)if(prepared.scene->contains(entity)&&effectReferences.contains(id))prepared.stagedEffectEntities.emplace(entity,id);
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
    stagedGameplayEffects_.swap(p.stagedEffects);stagedGameplayEffectEntities_.swap(p.stagedEffectEntities);
    history_.reset(!p.wasDirty);transaction_.reset();loadedFingerprint_=p.savedFingerprint;status_.swap(p.status);
    if(p.newDocument){assetId_.clear();name_.swap(p.name);previewCamera_.reset();stagedNavigationProfiles_.clear();stagedNavigationControllers_.clear();stagedGameplayEffects_.clear();stagedGameplayEffectEntities_.clear();++documentGeneration_;}
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
        for(auto& record:records)normalizeSceneOutlinerStyle(record);
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
        for(auto& record:records)normalizeSceneOutlinerStyle(record);
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
    const ModelMaterialRemap& remapMaterials,const ModelPaintRemap& remapPaint,bool preservePlacement,const ModelBlendRemap& remapBlendSettings) {
    if(transient_ || transaction_){status_="Quader import waits for Edit mode and the current scene edit.";return false;}
    const auto root=scene_.find(owner);
    if(!root || !root->quader){status_="Quader entity no longer exists.";return false;}
    return edit("Import Quader",[&](auto& records,auto& properties) {
        std::unordered_set<EntityId> removed;
        std::unordered_map<std::string,EntityId> existing;
        for(const auto& record:records)if(record.quaderGenerated && record.quaderGenerated->owner==owner) {
            existing.try_emplace(record.quaderGenerated->sourceKey,record.id);
            if(preservePlacement)removed.insert(record.id);
            else for(const auto id:scene_.subtree(record.id))removed.insert(id);
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
        if(const auto previous=scene_.find(imported.id)){
            imported.visible=previous->visible;imported.outlinerStyle=previous->outlinerStyle;
            if(preservePlacement){imported.name=previous->name;imported.local=previous->local;imported.parent=previous->parent;imported.order=previous->order;}
            preserveAuthoredActorComponents(imported,*previous);
        }
        replacements.push_back(imported);
        for(const auto& input:generated) {
            if(!input.id || !input.quaderGenerated)throw std::runtime_error("Quader import record has no temporary identity.");
            const auto id=allocate(input.quaderGenerated->sourceKey);
            if(!remap.emplace(input.id,id).second)throw std::runtime_error("Quader import has duplicate temporary identities.");
        }
        for(const auto& input:generated) {
            auto record=input;record.id=remap.at(input.id);record.parent=input.parent?remap.at(input.parent):imported.id;
            // Quader has no representation for Crimson's lighting roles, flicker or GI energy. Keep
            // those authored values on matching source identities/kinds while
            // source transforms, energy, geometry and shadow flags still sync.
            if(const auto previous=scene_.find(record.id)) {
                if(preservePlacement) {
                    record.name=previous->name;record.local=previous->local;
                    record.parent=previous->parent;record.order=previous->order;
                }
                record.visible=previous->visible;
                record.outlinerStyle=previous->outlinerStyle;
                preserveAuthoredActorComponents(record,*previous);
                if(record.model && previous->model) {
                    record.meshPaint=remapPaint?remapPaint(*previous,record):previous->meshPaint;
                    if(remapBlendSettings)record.blendSettings=remapBlendSettings(*previous,record);
                    else if(record.model==previous->model)record.blendSettings=previous->blendSettings;
                    else if(!previous->blendSettings.empty())throw std::runtime_error("Quader sync needs source material slots to preserve Blend settings on "+record.name+".");
                    record.lightingScopes=previous->lightingScopes;
                    record.meshShading=previous->meshShading;
                    if(remapMaterials)record.modelMaterials=remapMaterials(*previous,record);
                    else if(record.model==previous->model)record.modelMaterials=previous->modelMaterials;
                    else if(!previous->modelMaterials.empty())
                        throw std::runtime_error("Quader sync needs source material slots to preserve assignments on "+record.name+".");
                }
                if(record.point && previous->point) {
                    record.point->roles=previous->point->roles;
                    record.point->indirectEnergy=previous->point->indirectEnergy;
                    record.point->temperatureKelvin=previous->point->temperatureKelvin;
                    record.point->flicker=previous->point->flicker;
                    record.point->colorMode=LightColorMode::Color; // Synchronized source RGB owns the active color.
                }
                if(record.directional && previous->directional) {
                    record.directional->roles=previous->directional->roles;
                    record.directional->indirectEnergy=previous->directional->indirectEnergy;
                    record.directional->shadowAngularDiameter=previous->directional->shadowAngularDiameter;
                    record.directional->temperatureKelvin=previous->directional->temperatureKelvin;
                    record.directional->flicker=previous->directional->flicker;
                    record.directional->colorMode=LightColorMode::Color;
                }
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
            if(record.cameraFollow && lost(record.cameraFollow->target)){record.cameraFollow.reset();}
            if(record.environment && lost(record.environment->sun))record.environment->sun=NoEntity;
        }
    });
}
EntityId SceneDocument::createModel(const std::string& assetId, const std::string& name, const Mat4& local) {
    EntityRecord record; record.name = name; record.model=AssetRef<ModelAsset>{assetId}; record.local = local;
    return create(std::move(record),"Place model");
}
EntityId SceneDocument::createBoxDecal(const std::string& name,EntityId parent) {
    EntityRecord record;record.name=name;record.parent=parent;record.decal.emplace();
    record.decal->projection=SceneDecalProjection::Box;
    return create(std::move(record),"Create box decal");
}
EntityId SceneDocument::createDecal(const SceneDecal& decal,const std::string& name,const Mat4& local) {
    EntityRecord record;record.name=name;record.local=local;record.decal=decal;return create(std::move(record),"Place decal");
}
EntityId SceneDocument::createParticle(const std::string& assetId, const std::string& name, const Mat4& local) {
    EntityRecord record; record.name=name; record.particle=AssetRef<ParticleEffectAsset>{assetId}; record.local=local;
    return create(std::move(record),"Place particle effect");
}
std::vector<EntityId> SceneDocument::createModels(const std::vector<ModelPlacement>& models,EntityId parent) {
    if (models.empty() || (parent && !scene_.contains(parent)) || !begin("Place models")) return {};
    std::vector<EntityId> ids;
    if (!update([&](auto& records,auto&) {
        for (const auto& model : models) {
            EntityRecord record;
            record.id=unusedId(records); record.name=model.name; record.model=AssetRef<ModelAsset>{model.assetId}; record.local=model.local;
            record.parent=parent;record.order=nextOrder(records,parent); ids.push_back(record.id); records.push_back(std::move(record));
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
EntityId SceneDocument::createPlayer(const std::string& name) {
    if(!begin("Create player"))return NoEntity;
    EntityRecord player;player.name=name;player.actor.emplace();player.movement.emplace();player.controller.emplace();
    initializePlayerEffects(player);
    player.controller->collision.layers=PlayerCollisionLayer;
    player.playerInput.emplace();
    const auto initialCamera=defaultPlayerCameraTemplate();
    EntityRecord camera;camera.name="Camera";camera.camera=initialCamera.lens;
    camera.local=*math::withAffinePosition(initialCamera.worldBasis,initialCamera.offset);
    if(!update([&](auto& records,auto& properties){
        player.id=unusedId(records);player.order=nextOrder(records,NoEntity);records.push_back(player);
        camera.id=unusedId(records);camera.parent=player.id;camera.order=0;
        camera.cameraFollow=SceneCameraFollow{player.id,initialCamera.offset,initialCamera.worldBasis};records.push_back(camera);
        if(!properties.playerCharacter)properties.playerCharacter=player.id;
        if(!properties.activeCamera)properties.activeCamera=camera.id;
    })){cancel();return NoEntity;}
    selection_={{player.id},player.id};if(!commit())return NoEntity;return player.id;
}
EntityId SceneDocument::createEnemy(const std::string& name) {
    EntityRecord enemy;enemy.name=name;enemy.actor.emplace();enemy.movement.emplace();enemy.controller.emplace();
    initializeEnemyEffects(enemy);
    enemy.controller->collision.layers=EnemyCollisionLayer;
    enemy.navigation.emplace();enemy.enemy.emplace();
    return create(std::move(enemy),"Create enemy");
}
EntityId SceneDocument::createCollider(const std::string& name,EntityId parent) {
    EntityRecord record;record.name=name;record.parent=parent;record.collider.emplace();
    return create(std::move(record),"Create collider");
}
EntityId SceneDocument::createNavigationVolume(const std::string& name,EntityId parent) {
    const auto records=scene_.records();
    for(const auto& record:records)if(record.navigationVolume&&record.navigationVolume->enabled){select({record.id},record.id);return record.id;}
    EntityRecord record;record.name=name;record.parent=parent;record.navigationVolume.emplace();record.navigationVolume->profiles.clear();
    std::unordered_set<std::string> profiles;
    for(const auto& actor:records)if(actor.controller&&profiles.insert(actor.controller->profile.id).second)record.navigationVolume->profiles.push_back(actor.controller->profile);
    if(record.navigationVolume->profiles.empty())record.navigationVolume->profiles.emplace_back(DefaultNavigationAgentProfileId);
    return create(std::move(record),"Create Navigation Volume");
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
EntityId SceneDocument::createSphere(const std::string& name) {
    return createModel(BuiltinSphereModelId,name);
}
EntityId SceneDocument::createCylinder(const std::string& name) {
    return createModel(BuiltinCylinderModelId,name);
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
    auto updated=*record;
    if(target) {
        const auto targetWorld=scene_.worldTransform(target);
        const auto basis=math::withAffinePosition(*world,{0,0,0});
        if(!targetWorld || !basis){status_="Camera follow target or world basis is invalid.";return false;}
        updated.cameraFollow=SceneCameraFollow{target,math::transformPoint(*world,{0,0,0})-math::transformPoint(*targetWorld,{0,0,0}),*basis};
    } else {
        // Follow has already materialized the full world pose into local.
        updated.cameraFollow.reset();
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
        for(auto& record:records)if(record.cameraFollow && removed.contains(record.cameraFollow->target)) {
            // Follow is already materialized in the local matrix. Clearing the
            // reference leaves the visible camera pose and its children intact.
            record.cameraFollow.reset();
        }
        for(auto& record:records)if(record.environment && removed.contains(record.environment->sun))record.environment->sun=NoEntity;
        for(auto& record:records)if(record.quaderGenerated && removed.contains(record.quaderGenerated->owner))record.quaderGenerated.reset();
    })) { cancel(); return false; }
    selection_ = {}; return commit();
}
bool SceneDocument::duplicateSelection() {
    const auto roots=scene_.topmost(selection_.ids);
    if(roots.empty()||!begin("Duplicate entities"))return false;
    if(!duplicateInTransaction(roots)){cancel();return false;}
    return commit();
}
bool SceneDocument::beginDuplicateTransform(const std::string& label,std::vector<EntityId> roots) {
    roots=scene_.topmost(roots);
    if(roots.empty()||!begin(label))return false;
    std::vector<EntityId> copiedRoots;
    if(!duplicateInTransaction(roots,&copiedRoots)){cancel();return false;}
    setTransformEditRoots(std::move(copiedRoots));return true;
}
bool SceneDocument::duplicateInTransaction(const std::vector<EntityId>& roots,std::vector<EntityId>* copiedRoots) {
    if(!transaction_||roots.empty())return false;
    const auto previousSelection = selection_;
    std::vector<EntityId> originals;
    for (auto root : roots) {
        const auto branch = scene_.subtree(root); originals.insert(originals.end(),branch.begin(),branch.end());
    }
    std::unordered_map<EntityId,EntityId> copied;
    std::unordered_set<EntityId> copiedIds;
    const auto sourceRecords=scene_.records();SceneDuplicateNameAllocator names(sourceRecords);
    if (!update([&](auto& records,auto&) {
        for (auto original : originals) {
            auto record = *scene_.find(original);
            record.id = unusedId(records); copied.emplace(original,record.id);
            copiedIds.insert(record.id);
            if (const auto parent = copied.find(record.parent); parent != copied.end()) record.parent = parent->second;
            record.name=names.next(record.name);
            record.order = nextOrder(records,record.parent);
            records.push_back(std::move(record));
        }
        for(auto& record:records)if(copiedIds.contains(record.id)) {
            if(record.cameraFollow)if(const auto target=copied.find(record.cameraFollow->target);target!=copied.end())record.cameraFollow->target=target->second;
            if(record.decal) {
                if(const auto target=copied.find(record.decal->seed.receiver);target!=copied.end())record.decal->seed.receiver=target->second;
            }
            if(record.environment)if(const auto sun=copied.find(record.environment->sun);sun!=copied.end())record.environment->sun=sun->second;
            if(record.quaderGenerated) {
                const auto owner=copied.find(record.quaderGenerated->owner);
                if(owner!=copied.end())record.quaderGenerated->owner=owner->second;
                else record.quaderGenerated.reset();
            }
        }
    })) return false;
    selection_ = {};
    for (auto original : previousSelection.ids) if (copied.contains(original)) selection_.ids.push_back(copied.at(original));
    if(selection_.ids.empty())for(const auto root:roots)selection_.ids.push_back(copied.at(root));
    if (copied.contains(previousSelection.primary)) selection_.primary = copied.at(previousSelection.primary);
    if(copiedRoots) {copiedRoots->clear();for(const auto root:roots)copiedRoots->push_back(copied.at(root));}
    sanitizeSelection(); return true;
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
        if (record && !record->model && !record->particle && !record->decal && !record->directional && !record->point && !record->environment &&
            !record->actor && !record->movement && !record->controller && !record->navigation && !record->tags && !record->effects && !record->playerInput && !record->collider && !record->quader && !record->quaderGenerated && !record->camera && !scene_.children(id).empty()) groups.push_back(id);
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
        for(auto& record:records)if(record.cameraFollow && std::find(groups.begin(),groups.end(),record.cameraFollow->target)!=groups.end()) {
            record.cameraFollow.reset();
        }
    })) { cancel(); return false; }
    selection_={};
    for (const auto& move : moves) selection_.ids.push_back(move.id);
    sanitizeSelection();
    return commit();
}

} // namespace crimson
