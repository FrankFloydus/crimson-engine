#include "ParticleAsset.h"
#include "AtomicFile.h"
#include "MaterialAsset.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
using Json = nlohmann::json;
constexpr size_t MaximumParticleAssetBytes = 1024 * 1024;
std::filesystem::path fromUtf8(const std::string& value) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));
}
std::string utf8(const std::filesystem::path& value) {
    const auto text=value.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()),text.size()};
}
void validateId(const std::string& id) {
    const auto relative=fromUtf8(id);
    if (id.empty() || relative.has_root_path() || relative.parent_path()!=std::filesystem::path("assets/particles") ||
        relative.extension()!=".crimson-particle" || utf8(relative.filename()).find(':')!=std::string::npos)
        throw std::runtime_error("Particle asset must be stored directly under assets/particles with a .crimson-particle extension.");
}
std::string hex(uint64_t value) {
    char buffer[17]{};
    const auto result=std::to_chars(buffer,buffer+16,value,16);
    return {buffer,result.ptr};
}
uint64_t readHex(const Json& value) {
    const auto text=value.get<std::string>();
    uint64_t result{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result,16);
    if (text.empty() || parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
        throw std::runtime_error("Particle identity or seed is not a 64-bit hexadecimal string.");
    return result;
}
uint32_t boundedCount(const Json& value,uint32_t maximum,const char* description) {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>()<1) ||
        value.get<uint64_t>()>maximum)
        throw std::runtime_error(std::string(description)+" must be a positive bounded integer.");
    return static_cast<uint32_t>(value.get<uint64_t>());
}
Json rangeJson(const ParticleRandomRange& range) {
    return {{"enabled",range.enabled},{"minimum",range.minimum},{"maximum",range.maximum}};
}
ParticleRandomRange readRange(const Json& source) {
    return {source.at("enabled").get<bool>(),source.at("minimum").get<float>(),source.at("maximum").get<float>()};
}
Json behaviorJson(const ParticleBehavior& behavior) {
    return {{"present",behavior.present},{"enabled",behavior.enabled}};
}
ParticleBehavior readBehavior(const Json& source) {
    return {source.at("present").get<bool>(),source.at("enabled").get<bool>()};
}
void boundedFloat(float value,float minimum,float maximum,const char* description) {
    if (!math::finite(value) || value<minimum || value>maximum)
        throw std::runtime_error(std::string(description)+" must be finite and within its supported range.");
}
void validateRange(const ParticleRandomRange& range,float minimum,float maximum,const char* description) {
    boundedFloat(range.minimum,minimum,maximum,description);
    boundedFloat(range.maximum,minimum,maximum,description);
    if (range.minimum>range.maximum) throw std::runtime_error(std::string(description)+" minimum must not exceed maximum.");
}
const char* shapeName(ParticleShape shape) {
    switch (shape) {
    case ParticleShape::Point: return "point";
    case ParticleShape::Cone: return "cone";
    case ParticleShape::Sphere: return "sphere";
    case ParticleShape::Box: return "box";
    }
    throw std::runtime_error("Unsupported particle emission shape.");
}
ParticleShape readShape(const std::string& shape) {
    if (shape=="point") return ParticleShape::Point;
    if (shape=="cone") return ParticleShape::Cone;
    if (shape=="sphere") return ParticleShape::Sphere;
    if (shape=="box") return ParticleShape::Box;
    throw std::runtime_error("Unsupported particle emission shape.");
}
const char* spaceName(ParticleSpace space) {
    switch (space) { case ParticleSpace::World: return "world"; case ParticleSpace::Local: return "local"; }
    throw std::runtime_error("Unsupported particle simulation space.");
}
ParticleSpace readSpace(const std::string& space) {
    if (space=="world") return ParticleSpace::World;
    if (space=="local") return ParticleSpace::Local;
    throw std::runtime_error("Unsupported particle simulation space.");
}
const char* velocityName(ParticleVelocityMode mode) {
    switch (mode) { case ParticleVelocityMode::Vector: return "vector"; case ParticleVelocityMode::DirectionSpeed: return "direction-speed"; }
    throw std::runtime_error("Unsupported particle initial velocity mode.");
}
ParticleVelocityMode readVelocityMode(const std::string& mode) {
    if (mode=="vector") return ParticleVelocityMode::Vector;
    if (mode=="direction-speed") return ParticleVelocityMode::DirectionSpeed;
    throw std::runtime_error("Unsupported particle initial velocity mode.");
}
const char* directionName(ParticleDirectionMode mode) {
    switch (mode) {
    case ParticleDirectionMode::Fixed: return "fixed";
    case ParticleDirectionMode::RandomCone: return "random-cone";
    case ParticleDirectionMode::Outward: return "outward";
    }
    throw std::runtime_error("Unsupported particle direction mode.");
}
ParticleDirectionMode readDirectionMode(const std::string& mode) {
    if (mode=="fixed") return ParticleDirectionMode::Fixed;
    if (mode=="random-cone") return ParticleDirectionMode::RandomCone;
    if (mode=="outward") return ParticleDirectionMode::Outward;
    throw std::runtime_error("Unsupported particle direction mode.");
}
std::string unusedId(const std::filesystem::path& root) {
    static std::atomic<uint64_t> sequence{};
    for (;;) {
        const auto ticks=static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto id="assets/particles/"+hex(ticks)+"-"+hex(sequence.fetch_add(1))+".crimson-particle";
        if (!std::filesystem::exists(particleAssetPath(root,id))) return id;
    }
}
std::string unusedName(const std::filesystem::path& root,const std::string& requested) {
    const std::string base=requested.empty() ? "New Particle Effect" : requested;
    if (base.size()>1024) throw std::runtime_error("Particle effect name must contain 1 to 1024 UTF-8 bytes.");
    std::unordered_set<std::string> names;
    for (const auto& asset:scanParticleAssets(root)) names.insert(asset.name);
    if (!names.contains(base)) return base;
    for (uint64_t suffix=2;;++suffix) {
        const auto name=base+" "+std::to_string(suffix);
        if (name.size()>1024) throw std::runtime_error("Particle effect name is too long to add a unique suffix.");
        if (!names.contains(name)) return name;
    }
}
}

ParticleEffectAsset defaultParticleEffect(const std::string& name) {
    ParticleEffectAsset result;
    result.name=name;
    return result;
}
void validateParticleAsset(const ParticleEffectAsset& asset) {
    if (!asset.id.empty()) validateId(asset.id);
    const auto& emitter=asset.emitter;
    const auto material=fromUtf8(emitter.material.id);
    if(!emitter.material.empty()&&emitter.material.id!=DefaultParticleMaterialId&&
        (material.has_root_path()||material.parent_path()!=std::filesystem::path("assets/materials")||material.extension()!=".crimson-material"||emitter.material.id.find(':')!=std::string::npos))
        throw std::runtime_error("Particle material must reference a managed material or the protected particle default.");
    if(!math::finite(emitter.opacity)||emitter.opacity<0||emitter.opacity>1)throw std::runtime_error("Particle opacity must be finite and between 0 and 1.");
    if (asset.name.empty() || asset.name.size()>1024 || !emitter.id || emitter.name.empty() || emitter.name.size()>1024)
        throw std::runtime_error("Particle effect and emitter require valid names and a persistent emitter identity.");
    if (!math::finite(emitter.rate) || emitter.rate<0 || emitter.rate>MaximumParticleRate)
        throw std::runtime_error("Emission rate must be finite and between 0 and 100000 particles/s.");
    if (!math::finite(emitter.lifetime) || emitter.lifetime<0.001f || emitter.lifetime>3600)
        throw std::runtime_error("Particle lifetime must be finite and between 0.001 and 3600 seconds.");
    if (!math::finite(emitter.size) || emitter.size<0.0001f || emitter.size>10000)
        throw std::runtime_error("Particle size must be finite and between 0.0001 and 10000 meters.");
    if (!emitter.capacity || emitter.capacity>MaximumParticleCapacity)
        throw std::runtime_error("Particle capacity must be between 1 and 65536.");
    if (!math::finite(emitter.intensity) || emitter.intensity<0 || emitter.intensity>100000)
        throw std::runtime_error("Particle intensity must be finite and between 0 and 100000.");
    if (!math::finite(emitter.color) || std::any_of(emitter.color.begin(),emitter.color.end(),[](float value){return value<0 || value>1;}))
        throw std::runtime_error("Particle color must contain finite sRGB values between 0 and 1.");
    for (const auto& vector:{emitter.initialVelocity,emitter.gravity})
        if (!math::finite(vector) || std::any_of(vector.begin(),vector.end(),[](float value){return std::abs(value)>100000;}))
            throw std::runtime_error("Particle velocity and gravity must be finite and within the supported magnitude.");
    // Validate inactive values too: enabling a saved behavior or range must not
    // expose an invalid definition. Enum identities are serialized as names.
    (void)shapeName(emitter.shape); (void)spaceName(emitter.space);
    (void)velocityName(emitter.velocityMode); (void)directionName(emitter.directionMode);
    boundedFloat(emitter.radius,0,MaximumParticleShapeSize,"Emission radius");
    for (float value:emitter.boxSize) boundedFloat(value,0,MaximumParticleShapeSize,"Emission box size");
    validateRange(emitter.lifetimeRange,MinimumParticleDuration,MaximumParticleTime,"Lifetime range");
    boundedFloat(emitter.speed,0,MaximumParticleSpeed,"Initial speed");
    validateRange(emitter.speedRange,0,MaximumParticleSpeed,"Speed range");
    boundedFloat(emitter.directionSpread,0,180,"Direction spread");
    boundedFloat(emitter.directionFlatness,0,1,"Direction flatness");
    if (!math::finite(emitter.direction) || std::any_of(emitter.direction.begin(),emitter.direction.end(),[](float value){return std::abs(value)>MaximumParticleForce;}))
        throw std::runtime_error("Particle direction must contain finite supported values.");
    if (emitter.velocityMode==ParticleVelocityMode::DirectionSpeed && math::dot(emitter.direction,emitter.direction)<=1e-12f)
        throw std::runtime_error("Particle launch direction must be nonzero in Direction and Speed mode.");
    boundedFloat(emitter.delay,0,MaximumParticleTime,"Emitter delay");
    boundedFloat(emitter.duration,MinimumParticleDuration,MaximumParticleTime,"Emitter duration");
    if (emitter.loop && !emitter.finiteDuration) throw std::runtime_error("Emitter looping requires a finite duration.");
    if (emitter.bursts.size()>MaximumParticleBursts) throw std::runtime_error("An emitter supports at most 64 bursts.");
    std::unordered_set<uint64_t> burstIds;
    for (const auto& burst:emitter.bursts) {
        if (!burst.id || !burstIds.insert(burst.id).second) throw std::runtime_error("Burst identities must be nonzero and unique within their emitter.");
        boundedFloat(burst.time,0,MaximumParticleTime,"Burst time");
        if (emitter.finiteDuration && burst.time>=emitter.duration) throw std::runtime_error("Burst time must occur before the emitter duration ends.");
        if (!burst.count || burst.count>MaximumParticleCapacity) throw std::runtime_error("Burst count must be between 1 and 65536 particles.");
    }
    if (!math::finite(emitter.acceleration) || std::any_of(emitter.acceleration.begin(),emitter.acceleration.end(),[](float value){return std::abs(value)>MaximumParticleForce;}))
        throw std::runtime_error("Particle acceleration must contain finite supported values.");
    boundedFloat(emitter.drag,0,MaximumParticleDrag,"Particle drag");
}
std::filesystem::path particleAssetPath(const std::filesystem::path& root,const std::string& id) {
    validateId(id);
    const auto resolvedRoot=std::filesystem::weakly_canonical(std::filesystem::absolute(root));
    const auto directory=std::filesystem::weakly_canonical(resolvedRoot/"assets/particles");
    const auto result=std::filesystem::weakly_canonical(resolvedRoot/fromUtf8(id));
    const auto relative=result.lexically_relative(resolvedRoot);
    if (result.parent_path()!=directory || relative.empty() || relative.is_absolute() || *relative.begin()=="..")
        throw std::runtime_error("Particle asset path escapes the managed project directory.");
    return result;
}
std::string serializeParticleAsset(const ParticleEffectAsset& asset) {
    validateParticleAsset(asset); validateId(asset.id);
    const auto& e=asset.emitter;
    Json bursts=Json::array();
    for (const auto& burst:e.bursts) bursts.push_back({{"id",hex(burst.id)},{"time",burst.time},{"count",burst.count}});
    return Json{{"version",4},{"type","crimson-particle"},{"id",asset.id},{"name",asset.name},{"defaultSeed",hex(asset.defaultSeed)},
        {"emitters",Json::array({Json{{"id",hex(e.id)},{"name",e.name},{"enabled",e.enabled},{"rate",e.rate},{"lifetime",e.lifetime},
            {"initialVelocity",e.initialVelocity},{"gravity",e.gravity},{"size",e.size},{"color",e.color},{"intensity",e.intensity},
            {"capacity",e.capacity},{"material",e.material.id},{"opacity",e.opacity},{"space",spaceName(e.space)},
            {"shape",shapeName(e.shape)},{"radius",e.radius},{"boxSize",e.boxSize},{"lifetimeRange",rangeJson(e.lifetimeRange)},
            {"velocityMode",velocityName(e.velocityMode)},{"direction",e.direction},{"directionMode",directionName(e.directionMode)},
            {"directionSpread",e.directionSpread},{"directionFlatness",e.directionFlatness},{"speed",e.speed},{"speedRange",rangeJson(e.speedRange)},
            {"timing",{{"delay",e.delay},{"finiteDuration",e.finiteDuration},{"duration",e.duration},{"loop",e.loop}}},
            {"bursts",std::move(bursts)},{"acceleration",e.acceleration},{"drag",e.drag},
            {"behaviors",{{"gravity",behaviorJson(e.gravityBehavior)},{"acceleration",behaviorJson(e.accelerationBehavior)},{"drag",behaviorJson(e.dragBehavior)}}},
            {"renderer","sprite"},{"colorSpace","srgb"}}})}}.dump(2)+"\n";
}
ParticleEffectAsset deserializeParticleAsset(std::string_view text,const std::string& expectedId) {
    if (text.size()>MaximumParticleAssetBytes) throw std::runtime_error("Particle asset exceeds 1 MiB.");
    const auto json=Json::parse(text);
    if (!json.at("version").is_number_integer() || (json.at("version")!=1&&json.at("version")!=2&&json.at("version")!=3&&json.at("version")!=4) || json.at("type")!="crimson-particle")
        throw std::runtime_error("Unsupported particle asset type or version.");
    ParticleEffectAsset asset;
    asset.id=json.at("id").get<std::string>();
    validateId(asset.id);
    if (!expectedId.empty() && asset.id!=expectedId) throw std::runtime_error("Particle asset identity differs from its managed file.");
    asset.name=json.at("name").get<std::string>(); asset.defaultSeed=readHex(json.at("defaultSeed"));
    const auto& emitters=json.at("emitters");
    if (!emitters.is_array() || emitters.size()!=1) throw std::runtime_error("This particle asset version requires exactly one emitter.");
    const auto& source=emitters.at(0);
    const bool legacy=json.at("version")==1;
    const bool extended=json.at("version")==3 || json.at("version")==4;
    const bool flatnessVersion=json.at("version")==4;
    const std::string renderer=legacy?"procedural-additive":"sprite";
    if ((!extended && source.value("space",std::string{"world"})!="world") || source.value("renderer",renderer)!=renderer ||
        source.value("colorSpace",std::string{"srgb"})!="srgb")
        throw std::runtime_error("Unsupported particle space, renderer or authored color convention.");
    auto& e=asset.emitter;
    e.id=readHex(source.at("id")); e.name=source.at("name").get<std::string>(); e.enabled=source.at("enabled").get<bool>();
    e.rate=source.at("rate").get<float>(); e.lifetime=source.at("lifetime").get<float>();
    e.initialVelocity=source.at("initialVelocity").get<Vec3>(); e.gravity=source.at("gravity").get<Vec3>();
    e.size=source.at("size").get<float>(); e.color=source.at("color").get<Vec3>(); e.intensity=source.at("intensity").get<float>();
    if(!legacy){e.material=AssetRef<MaterialAsset>{source.at("material").get<std::string>()};e.opacity=source.at("opacity").get<float>();}
    e.capacity=boundedCount(source.at("capacity"),MaximumParticleCapacity,"Particle capacity");
    if (extended) {
        e.shape=readShape(source.at("shape").get<std::string>());
        e.space=readSpace(source.at("space").get<std::string>());
        e.radius=source.at("radius").get<float>(); e.boxSize=source.at("boxSize").get<Vec3>();
        e.lifetimeRange=readRange(source.at("lifetimeRange"));
        e.velocityMode=readVelocityMode(source.at("velocityMode").get<std::string>());
        e.direction=source.at("direction").get<Vec3>();
        e.directionMode=readDirectionMode(source.at("directionMode").get<std::string>());
        e.directionSpread=source.at("directionSpread").get<float>();
        // v3 outward directions ignored spread. v4 applies spread around the
        // radial axis, so migrate that formerly inactive value to zero. Fixed
        // and vector modes retain inactive values; random cones retain theirs.
        boundedFloat(e.directionSpread,0,180,"Direction spread");
        if (!flatnessVersion && e.directionMode==ParticleDirectionMode::Outward) e.directionSpread=0;
        if (flatnessVersion) e.directionFlatness=source.at("directionFlatness").get<float>();
        e.speed=source.at("speed").get<float>(); e.speedRange=readRange(source.at("speedRange"));
        const auto& timing=source.at("timing");
        e.delay=timing.at("delay").get<float>(); e.finiteDuration=timing.at("finiteDuration").get<bool>();
        e.duration=timing.at("duration").get<float>(); e.loop=timing.at("loop").get<bool>();
        const auto& bursts=source.at("bursts");
        if (!bursts.is_array() || bursts.size()>MaximumParticleBursts) throw std::runtime_error("An emitter supports an array of at most 64 bursts.");
        e.bursts.reserve(bursts.size());
        for (const auto& burst:bursts) e.bursts.push_back({readHex(burst.at("id")),burst.at("time").get<float>(),boundedCount(burst.at("count"),MaximumParticleCapacity,"Burst count")});
        e.acceleration=source.at("acceleration").get<Vec3>(); e.drag=source.at("drag").get<float>();
        const auto& behaviors=source.at("behaviors");
        if (!behaviors.is_object() || behaviors.size()!=3) throw std::runtime_error("Only the implemented gravity, acceleration and drag behaviors are supported.");
        e.gravityBehavior=readBehavior(behaviors.at("gravity"));
        e.accelerationBehavior=readBehavior(behaviors.at("acceleration"));
        e.dragBehavior=readBehavior(behaviors.at("drag"));
    }
    validateParticleAsset(asset);
    return asset;
}
ParticleEffectAsset loadParticleAsset(const std::filesystem::path& root,const std::string& id) {
    const auto file=particleAssetPath(root,id);
    if (std::filesystem::file_size(file)>MaximumParticleAssetBytes) throw std::runtime_error("Particle asset exceeds 1 MiB.");
    std::ifstream input(file,std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read particle asset.");
    const std::string contents{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    if (input.bad()) throw std::runtime_error("Could not read the complete particle asset.");
    return deserializeParticleAsset(contents,id);
}
void saveParticleAsset(const std::filesystem::path& root,const ParticleEffectAsset& asset) {
    const auto contents=serializeParticleAsset(asset);
    writeFileAtomically(particleAssetPath(root,asset.id),contents);
}
ParticleEffectAsset createParticleAsset(const std::filesystem::path& root,const std::string& name) {
    auto asset=defaultParticleEffect(unusedName(root,name));
    asset.id=unusedId(root);
    saveParticleAsset(root,asset);
    return asset;
}
ParticleEffectAsset saveParticleAssetAs(const std::filesystem::path& root,const ParticleEffectAsset& original,const std::string& name) {
    auto candidate=original;
    candidate.id=unusedId(root);
    candidate.name=unusedName(root,name.empty()?original.name:name);
    saveParticleAsset(root,candidate);
    return candidate;
}
std::vector<ParticleAssetEntry> scanParticleAssets(const std::filesystem::path& root,std::vector<std::string>* diagnostics) {
    std::vector<ParticleAssetEntry> entries;
    const auto directory=root/"assets/particles";
    if (!std::filesystem::exists(directory)) return entries;
    for (const auto& file:std::filesystem::directory_iterator(directory)) {
        if (!file.is_regular_file() || file.path().extension()!=".crimson-particle") continue;
        const auto id=utf8(file.path().lexically_relative(root));
        try { auto asset=loadParticleAsset(root,id); entries.push_back({std::move(asset.id),std::move(asset.name)}); }
        catch (const std::exception& error) { if (diagnostics) diagnostics->push_back("Particle asset "+id+": "+error.what()); }
    }
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.name!=b.name?a.name<b.name:a.id<b.id;});
    return entries;
}

} // namespace crimson
