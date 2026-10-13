#include "ParticleAsset.h"
#include "AtomicFile.h"
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
    return Json{{"version",1},{"type","crimson-particle"},{"id",asset.id},{"name",asset.name},{"defaultSeed",hex(asset.defaultSeed)},
        {"emitters",Json::array({Json{{"id",hex(e.id)},{"name",e.name},{"enabled",e.enabled},{"rate",e.rate},{"lifetime",e.lifetime},
            {"initialVelocity",e.initialVelocity},{"gravity",e.gravity},{"size",e.size},{"color",e.color},{"intensity",e.intensity},
            {"capacity",e.capacity},{"space","world"},{"renderer","procedural-additive"},{"colorSpace","srgb"}}})}}.dump(2)+"\n";
}
ParticleEffectAsset deserializeParticleAsset(std::string_view text,const std::string& expectedId) {
    if (text.size()>MaximumParticleAssetBytes) throw std::runtime_error("Particle asset exceeds 1 MiB.");
    const auto json=Json::parse(text);
    if (!json.at("version").is_number_integer() || json.at("version")!=1 || json.at("type")!="crimson-particle")
        throw std::runtime_error("Unsupported particle asset type or version.");
    ParticleEffectAsset asset;
    asset.id=json.at("id").get<std::string>();
    validateId(asset.id);
    if (!expectedId.empty() && asset.id!=expectedId) throw std::runtime_error("Particle asset identity differs from its managed file.");
    asset.name=json.at("name").get<std::string>(); asset.defaultSeed=readHex(json.at("defaultSeed"));
    const auto& emitters=json.at("emitters");
    if (!emitters.is_array() || emitters.size()!=1) throw std::runtime_error("This particle asset version requires exactly one emitter.");
    const auto& source=emitters.at(0);
    if (source.value("space",std::string{"world"})!="world" || source.value("renderer",std::string{"procedural-additive"})!="procedural-additive" ||
        source.value("colorSpace",std::string{"srgb"})!="srgb")
        throw std::runtime_error("Unsupported particle space, renderer or authored color convention.");
    auto& e=asset.emitter;
    e.id=readHex(source.at("id")); e.name=source.at("name").get<std::string>(); e.enabled=source.at("enabled").get<bool>();
    e.rate=source.at("rate").get<float>(); e.lifetime=source.at("lifetime").get<float>();
    e.initialVelocity=source.at("initialVelocity").get<Vec3>(); e.gravity=source.at("gravity").get<Vec3>();
    e.size=source.at("size").get<float>(); e.color=source.at("color").get<Vec3>(); e.intensity=source.at("intensity").get<float>();
    const auto& capacity=source.at("capacity");
    if (!capacity.is_number_integer() || (capacity.is_number_integer() && !capacity.is_number_unsigned() && capacity.get<int64_t>()<1) ||
        capacity.get<uint64_t>()>MaximumParticleCapacity)
        throw std::runtime_error("Particle capacity must be an integer between 1 and 65536.");
    e.capacity=static_cast<uint32_t>(capacity.get<uint64_t>());
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
