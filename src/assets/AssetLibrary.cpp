#include "AssetLibrary.h"

#include <SDL3/SDL.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace crimson {
namespace {

std::string utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
std::filesystem::path fromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

bool textureFile(const std::filesystem::path& path) {
    const auto type = extension(path);
    return type == ".png" || type == ".jpg" || type == ".jpeg" || type == ".tga" || type == ".bmp" || type == ".hdr";
}

struct Pixels {
    uint32_t width{}, height{};
    std::vector<uint8_t> rgba;
};

Pixels decode(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (!input || length <= 0 || static_cast<uint64_t>(length) > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Could not read texture: " + utf8(path.filename()));
    std::vector<uint8_t> encoded(static_cast<size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    if (!input) throw std::runtime_error("Texture file could not be read completely.");
    static bx::DefaultAllocator allocator;
    bx::Error error;
    std::unique_ptr<bimg::ImageContainer, decltype(&bimg::imageFree)> image(
        bimg::imageParse(&allocator, encoded.data(), static_cast<uint32_t>(encoded.size()), bimg::TextureFormat::Count, &error), bimg::imageFree);
    const bool parsed = error.isOk();
    if (!parsed || !image || image->m_cubeMap || image->m_numLayers != 1 || image->m_depth > 1)
        throw std::runtime_error("Could not decode a 2D texture: " + utf8(path.filename()));
    const uint32_t limit = bgfx::getCaps()->limits.maxTextureSize;
    if (!image->m_width || !image->m_height || image->m_width > limit || image->m_height > limit)
        throw std::runtime_error("Texture dimensions exceed the GPU limit.");
    const auto sourceFormat = image->m_format;
    if (extension(path) == ".hdr") {
        // Keep HDR values until thumbnail tone mapping. The renderer decodes the
        // original file independently for environment preprocessing.
        if (sourceFormat != bimg::TextureFormat::RGBA32F) {
            auto* converted = bimg::imageConvert(&allocator, bimg::TextureFormat::RGBA32F, *image, false);
            if (!converted) throw std::runtime_error("HDR pixel conversion failed.");
            image.reset(converted);
        }
        bimg::ImageMip mip{};
        if (!bimg::imageGetRawData(*image, 0, 0, image->m_data, image->m_size, mip) ||
            mip.m_format != bimg::TextureFormat::RGBA32F)
            throw std::runtime_error("HDR decoder did not return floating point pixels.");
        const size_t channels = static_cast<size_t>(mip.m_width) * mip.m_height * 4;
        if (channels * sizeof(float) > mip.m_size) throw std::runtime_error("HDR pixel buffer is incomplete.");
        Pixels result{mip.m_width, mip.m_height, std::vector<uint8_t>(channels)};
        for (size_t index = 0; index < channels; ++index) {
            if (index % 4 == 3) { result.rgba[index] = 255; continue; }
            float value{};
            std::memcpy(&value, mip.m_data + index * sizeof(float), sizeof(value));
            value = std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
            const float mapped = value / (1.0f + value);
            const float srgb = mapped <= 0.0031308f ? 12.92f * mapped : 1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
            result.rgba[index] = static_cast<uint8_t>(std::clamp(std::round(srgb * 255.0f), 0.0f, 255.0f));
        }
        return result;
    }
    const bool grayscale = sourceFormat == bimg::TextureFormat::R8 || sourceFormat == bimg::TextureFormat::R16 ||
                           sourceFormat == bimg::TextureFormat::RG8 || sourceFormat == bimg::TextureFormat::RG16;
    const bool grayAlpha = sourceFormat == bimg::TextureFormat::RG8 || sourceFormat == bimg::TextureFormat::RG16;
    if (sourceFormat != bimg::TextureFormat::RGBA8) {
        auto* converted = bimg::imageConvert(&allocator, bimg::TextureFormat::RGBA8, *image, false);
        if (!converted) throw std::runtime_error("Texture pixel conversion failed.");
        image.reset(converted);
    }
    bimg::ImageMip mip{};
    if (!bimg::imageGetRawData(*image, 0, 0, image->m_data, image->m_size, mip) || mip.m_format != bimg::TextureFormat::RGBA8)
        throw std::runtime_error("Texture decoder did not return RGBA pixels.");
    const size_t size = static_cast<size_t>(mip.m_width) * mip.m_height * 4;
    if (size > mip.m_size) throw std::runtime_error("Texture pixel buffer is incomplete.");
    Pixels result{mip.m_width, mip.m_height, {mip.m_data, mip.m_data + size}};
    // bimg expands scalar data into R, not RGB luminance. Restore image grayscale
    // for both the browser and albedo while retaining R for material data maps.
    if (grayscale) for (size_t pixel = 0; pixel < size; pixel += 4) {
        result.rgba[pixel + 3] = grayAlpha ? result.rgba[pixel + 1] : 255;
        result.rgba[pixel + 1] = result.rgba[pixel + 2] = result.rgba[pixel];
    }
    return result;
}

bgfx::TextureHandle upload(const Pixels& image, bool srgb) {
    const uint64_t flags = srgb ? BGFX_TEXTURE_SRGB : BGFX_TEXTURE_NONE;
    const auto handle = bgfx::createTexture2D(static_cast<uint16_t>(image.width), static_cast<uint16_t>(image.height),
        false, 1, bgfx::TextureFormat::RGBA8, flags,
        bgfx::copy(image.rgba.data(), static_cast<uint32_t>(image.rgba.size())));
    if (!bgfx::isValid(handle)) throw std::runtime_error("GPU texture upload failed.");
    return handle;
}

Pixels thumbnail(const Pixels& source) {
    const float scale = std::min(1.0f, 128.0f / static_cast<float>(std::max(source.width, source.height)));
    const uint32_t width = std::max(1u, static_cast<uint32_t>(source.width * scale));
    const uint32_t height = std::max(1u, static_cast<uint32_t>(source.height * scale));
    Pixels result{width, height, std::vector<uint8_t>(static_cast<size_t>(width) * height * 4)};
    for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
        const uint32_t sx = std::min(source.width - 1, static_cast<uint32_t>((x + 0.5f) / scale));
        const uint32_t sy = std::min(source.height - 1, static_cast<uint32_t>((y + 0.5f) / scale));
        std::copy_n(source.rgba.data() + (static_cast<size_t>(sy) * source.width + sx) * 4, 4,
                    result.rgba.data() + (static_cast<size_t>(y) * width + x) * 4);
    }
    return result;
}

std::string& slotPath(Material& material, MaterialSlot slot) {
    if (slot == MaterialSlot::Albedo) return material.albedoMap;
    if (slot == MaterialSlot::Roughness) return material.roughnessMap;
    if (slot == MaterialSlot::Normal) return material.normalMap;
    return material.metallicMap;
}

void destroyTexture(bgfx::TextureHandle handle) { if (bgfx::isValid(handle)) bgfx::destroy(handle); }

} // namespace

struct AssetLibrary::ImportQueue {
    std::mutex mutex;
    std::vector<std::string> paths;
    std::string error;
};

AssetLibrary::AssetLibrary(std::filesystem::path root)
    : imports_(std::make_shared<ImportQueue>()), projectRoot_(std::filesystem::absolute(std::move(root)).lexically_normal()) {
    const auto directory = projectRoot_ / "assets" / "textures";
    std::filesystem::create_directories(directory);
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && textureFile(entry.path())) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        try { assets_.push_back(makeAsset(file)); }
        catch (const std::exception& error) { status_ = error.what(); }
    }
}

AssetLibrary::~AssetLibrary() {
    for (const auto& asset : assets_) {
        destroyTexture(asset.thumbnail); destroyTexture(asset.colorTexture); destroyTexture(asset.dataTexture);
    }
}

const TextureAsset* AssetLibrary::find(const std::string& id) const {
    const auto found = std::find_if(assets_.begin(), assets_.end(), [&](const auto& asset) { return asset.id == id; });
    return found == assets_.end() ? nullptr : &*found;
}
TextureAsset* AssetLibrary::findMutable(const std::string& id) {
    const auto found = std::find_if(assets_.begin(), assets_.end(), [&](const auto& asset) { return asset.id == id; });
    return found == assets_.end() ? nullptr : &*found;
}

TextureAsset AssetLibrary::makeAsset(const std::filesystem::path& path) {
    const auto pixels = decode(path);
    TextureAsset asset;
    asset.id = utf8(std::filesystem::relative(path, projectRoot_));
    asset.name = utf8(path.filename());
    asset.width = pixels.width; asset.height = pixels.height;
    asset.hdr = extension(path) == ".hdr";
    asset.thumbnail = upload(thumbnail(pixels), false);
    bgfx::setName(asset.thumbnail, asset.name.c_str());
    return asset;
}

const TextureAsset* AssetLibrary::importTexture(const std::filesystem::path& source) {
    try {
        if (!textureFile(source)) throw std::runtime_error("Choose a PNG, JPEG, TGA, BMP or HDR image.");
        // Decode before copying so a failed import does not leave a broken asset.
        const auto pixels = decode(source);
        const auto folder = projectRoot_ / "assets" / "textures";
        auto destination = folder / source.filename();
        if (std::filesystem::exists(destination) && std::filesystem::equivalent(source, destination)) {
            const auto id = utf8(std::filesystem::relative(destination, projectRoot_));
            select(id);
            return find(id);
        }
        for (uint32_t suffix = 2; std::filesystem::exists(destination); ++suffix) {
            destination = folder / (source.stem().native() + L"-" + std::to_wstring(suffix) + source.extension().native());
        }
        std::filesystem::copy_file(source, destination);
        TextureAsset asset;
        asset.id = utf8(std::filesystem::relative(destination, projectRoot_));
        asset.name = utf8(destination.filename());
        asset.width = pixels.width; asset.height = pixels.height;
        asset.hdr = extension(destination) == ".hdr";
        asset.thumbnail = upload(thumbnail(pixels), false);
        bgfx::setName(asset.thumbnail, asset.name.c_str());
        assets_.push_back(std::move(asset));
        selectedId_ = assets_.back().id;
        status_ = "Imported " + assets_.back().name;
        return &assets_.back();
    } catch (const std::exception& error) { status_ = error.what(); return nullptr; }
}

void AssetLibrary::requestImport(SDL_Window* window) {
    static const SDL_DialogFileFilter filters[]{{"Textures and environments", "png;jpg;jpeg;tga;bmp;hdr"}};
    auto* callbackOwner = new std::shared_ptr<ImportQueue>(imports_);
    SDL_ShowOpenFileDialog([](void* data, const char* const* files, int) {
        std::unique_ptr<std::shared_ptr<ImportQueue>> owner(static_cast<std::shared_ptr<ImportQueue>*>(data));
        std::lock_guard lock((*owner)->mutex);
        if (!files) { (*owner)->error = SDL_GetError(); return; }
        for (size_t index = 0; files[index]; ++index) (*owner)->paths.emplace_back(files[index]);
    }, callbackOwner, window, filters, 1, nullptr, true);
}

void AssetLibrary::processImports() {
    std::vector<std::string> paths;
    {
        std::lock_guard lock(imports_->mutex);
        paths.swap(imports_->paths);
        if (!imports_->error.empty()) { status_ = std::move(imports_->error); imports_->error.clear(); }
    }
    for (const auto& path : paths) importTexture(fromUtf8(path));
}

bool AssetLibrary::assign(Material& material, MaterialSlot slot, const std::string& id) {
    const auto* asset = find(id);
    if (!asset) return false;
    if (asset->hdr) { status_ = "HDR images are supported in the environment slot only."; return false; }
    auto& path = slotPath(material, slot);
    if (path.empty()) {
        if (slot == MaterialSlot::Albedo) material.baseColor = {1, 1, 1};
        else if (slot == MaterialSlot::Roughness) material.roughness = 1;
        else if (slot == MaterialSlot::Metallic) material.metallic = 1;
    }
    path = id;
    saveMaterial(material);
    return true;
}
void AssetLibrary::clear(Material& material, MaterialSlot slot) {
    slotPath(material, slot).clear();
    saveMaterial(material);
}

bgfx::TextureHandle AssetLibrary::fullTexture(TextureAsset& asset, bool srgb) {
    if (asset.hdr) throw std::runtime_error("HDR images are supported in the environment slot only.");
    auto& handle = srgb ? asset.colorTexture : asset.dataTexture;
    auto& attempted = srgb ? asset.colorAttempted : asset.dataAttempted;
    if (!bgfx::isValid(handle) && !attempted) {
        attempted = true;
        handle = upload(decode(projectRoot_ / fromUtf8(asset.id)), srgb);
        bgfx::setName(handle, asset.name.c_str());
    }
    return handle;
}
MaterialTextures AssetLibrary::bindings(const Material& material) {
    MaterialTextures textures;
    try {
        if (auto* asset = findMutable(material.albedoMap)) textures.albedo = fullTexture(*asset, true);
        if (auto* asset = findMutable(material.roughnessMap)) textures.roughness = fullTexture(*asset, false);
        if (auto* asset = findMutable(material.metallicMap)) textures.metallic = fullTexture(*asset, false);
        if (auto* asset = findMutable(material.normalMap)) textures.normal = fullTexture(*asset, false);
    } catch (const std::exception& error) { status_ = error.what(); }
    return textures;
}

void AssetLibrary::loadMaterial(Material& material) {
    const auto file = projectRoot_ / "assets" / "material.json";
    if (!std::filesystem::exists(file)) return;
    try {
        std::ifstream input(file);
        const auto json = nlohmann::json::parse(input);
        Material restored;
        if (json.contains("baseColor")) restored.baseColor = json.at("baseColor").get<Vec3>();
        restored.roughness = json.value("roughness", restored.roughness);
        restored.metallic = json.value("metallic", restored.metallic);
        restored.normalStrength = json.value("normalStrength", restored.normalStrength);
        for (float& channel : restored.baseColor) channel = std::isfinite(channel) ? std::clamp(channel, 0.0f, 1.0f) : 1;
        restored.roughness = std::isfinite(restored.roughness) ? std::clamp(restored.roughness, 0.0f, 1.0f) : 0.35f;
        restored.metallic = std::isfinite(restored.metallic) ? std::clamp(restored.metallic, 0.0f, 1.0f) : 0;
        restored.normalStrength = std::isfinite(restored.normalStrength) ? std::clamp(restored.normalStrength, 0.0f, 2.0f) : 1;
        restored.albedoMap = json.value("albedoMap", std::string{});
        restored.roughnessMap = json.value("roughnessMap", std::string{});
        restored.metallicMap = json.value("metallicMap", std::string{});
        restored.normalMap = json.value("normalMap", std::string{});
        const auto validMaterialMap = [&](const std::string& id) { const auto* asset = find(id); return asset && !asset->hdr; };
        if (!restored.albedoMap.empty() && !validMaterialMap(restored.albedoMap)) { status_ = "Albedo texture is missing or unsupported."; restored.albedoMap.clear(); }
        if (!restored.roughnessMap.empty() && !validMaterialMap(restored.roughnessMap)) { status_ = "Roughness texture is missing or unsupported."; restored.roughnessMap.clear(); }
        if (!restored.metallicMap.empty() && !validMaterialMap(restored.metallicMap)) { status_ = "Metalness texture is missing or unsupported."; restored.metallicMap.clear(); }
        if (!restored.normalMap.empty() && !validMaterialMap(restored.normalMap)) { status_ = "Normal texture is missing or unsupported."; restored.normalMap.clear(); }
        // A mapped roughness factor can be zero; the BRDF applies its floor after
        // multiplication. Keep the standalone property within its editor range.
        if (restored.roughnessMap.empty()) restored.roughness = std::max(restored.roughness, 0.045f);
        material = std::move(restored);
    } catch (const std::exception& error) { status_ = std::string("Material load failed: ") + error.what(); }
}
void AssetLibrary::saveMaterial(const Material& material) {
    try {
        const nlohmann::json json{{"version", 2}, {"baseColor", material.baseColor}, {"roughness", material.roughness}, {"metallic", material.metallic},
            {"albedoMap", material.albedoMap}, {"roughnessMap", material.roughnessMap}, {"metallicMap", material.metallicMap},
            {"normalMap", material.normalMap}, {"normalStrength", material.normalStrength}};
        std::ofstream output(projectRoot_ / "assets" / "material.json");
        output << json.dump(2) << '\n';
        if (!output) throw std::runtime_error("Could not write material.json.");
    } catch (const std::exception& error) { status_ = error.what(); }
}

bool AssetLibrary::assignEnvironment(EnvironmentSettings& environment, const std::string& id) {
    if (!find(id)) return false;
    environment.map = id;
    saveEnvironment(environment);
    return true;
}

void AssetLibrary::clearEnvironment(EnvironmentSettings& environment) {
    environment.map.clear();
    saveEnvironment(environment);
}

void AssetLibrary::loadEnvironment(EnvironmentSettings& environment) {
    const auto file = projectRoot_ / "assets" / "environment.json";
    if (!std::filesystem::exists(file)) return;
    try {
        std::ifstream input(file);
        const auto json = nlohmann::json::parse(input);
        EnvironmentSettings restored;
        restored.map = json.value("map", std::string{});
        restored.enabled = json.value("enabled", restored.enabled);
        restored.intensity = json.value("intensity", restored.intensity);
        restored.rotation = json.value("rotation", restored.rotation);
        restored.intensity = std::isfinite(restored.intensity) ? std::clamp(restored.intensity, 0.0f, 10.0f) : 1.0f;
        restored.rotation = std::isfinite(restored.rotation) ? std::clamp(restored.rotation, -180.0f, 180.0f) : 0.0f;
        // Only IDs enumerated from the managed assets folder can be assigned.
        if (!restored.map.empty() && !find(restored.map)) {
            status_ = "Environment image is missing; using the built-in studio.";
            restored.map.clear();
        }
        environment = std::move(restored);
    } catch (const std::exception& error) { status_ = std::string("Environment load failed: ") + error.what(); }
}

void AssetLibrary::saveEnvironment(const EnvironmentSettings& environment) {
    try {
        const nlohmann::json json{{"version", 1}, {"map", environment.map}, {"enabled", environment.enabled},
            {"intensity", environment.intensity}, {"rotation", environment.rotation}};
        std::ofstream output(projectRoot_ / "assets" / "environment.json");
        output << json.dump(2) << '\n';
        if (!output) throw std::runtime_error("Could not write environment.json.");
    } catch (const std::exception& error) { status_ = error.what(); }
}

} // namespace crimson
