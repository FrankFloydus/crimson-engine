////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/TextureBindingService.h"

#include "core/Logger.h"
#include "render/FilamentBufferDescriptorFactory.h"

#include <lodepng.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

std::uint8_t mipLevelCount(unsigned width, unsigned height) {
    unsigned dimension = std::max(width, height);
    std::uint8_t levels = 1;
    while (dimension > 1u) {
        dimension /= 2u;
        ++levels;
    }
    return levels;
}

} // namespace

TextureBindingService::~TextureBindingService() {
    shutdown();
}

void TextureBindingService::initialize(filament::Engine& engine, std::filesystem::path resourceRoot) {
    engine_ = &engine;
    resourceRoot_ = std::move(resourceRoot);
    fallbackAlbedoTexture_ = createSolidTexture(255, 255, 255, 255, TextureColorSpace::Srgb);
    fallbackRoughnessTexture_ = createSolidTexture(255, 255, 255, 255, TextureColorSpace::Linear);
}

void TextureBindingService::shutdown() {
    if (!engine_) {
        return;
    }

    for (auto& [id, texture] : textureCache_) {
        destroyTexture(texture);
    }
    textureCache_.clear();
    destroyTexture(fallbackAlbedoTexture_);
    destroyTexture(fallbackRoughnessTexture_);
    resourceRoot_.clear();
    engine_ = nullptr;
}

filament::Texture* TextureBindingService::resolveTexture(const AssetRegistry& assets,
        const MaterialDocument* material,
        const char* slotName,
        filament::Texture* fallback) {
    if (!material) {
        return fallback;
    }
    auto it = material->textureSlots.find(slotName);
    if (it == material->textureSlots.end()) {
        std::string alternateSlot = slotName;
        if (!alternateSlot.ends_with("Texture")) {
            alternateSlot += "Texture";
        }
        it = material->textureSlots.find(alternateSlot);
        if (it == material->textureSlots.end()) {
            return fallback;
        }
    }
    const TextureAsset* texture = assets.texture(it->second);
    if (!texture) {
        return fallback;
    }
    const TextureCacheKey cacheKey{
            .id = it->second,
            .revision = texture->revision.value,
            .colorSpace = texture->colorSpace,
    };
    if (const auto cached = textureCache_.find(cacheKey); cached != textureCache_.end()) {
        return cached->second ? cached->second : fallback;
    }
    filament::Texture* loaded = loadTextureAsset(*texture);
    textureCache_[cacheKey] = loaded;
    return loaded ? loaded : fallback;
}

filament::Texture* TextureBindingService::loadTextureAsset(const TextureAsset& texture) {
    if (!engine_ || texture.sourcePath.empty()) {
        return nullptr;
    }

    std::filesystem::path path = texture.sourcePath;
    if (path.is_relative()) {
        path = resourceRoot_ / path;
    }

    std::vector<unsigned char> pixels;
    unsigned width = 0;
    unsigned height = 0;
    const unsigned error = lodepng::decode(pixels, width, height, path.string());
    if (error != 0u) {
        LOG_ERROR("Failed to decode material texture '{}': {}", path.string(), lodepng_error_text(error));
        return nullptr;
    }

    const std::uint8_t levels = mipLevelCount(width, height);
    filament::Texture* loaded = filament::Texture::Builder()
            .width(width)
            .height(height)
            .levels(levels)
            .sampler(filament::Texture::Sampler::SAMPLER_2D)
            .format(texture.colorSpace == TextureColorSpace::Srgb ? filament::Texture::InternalFormat::SRGB8_A8 : filament::Texture::InternalFormat::RGBA8)
            .usage(filament::Texture::Usage::UPLOADABLE |
                    filament::Texture::Usage::SAMPLEABLE |
                    filament::Texture::Usage::GEN_MIPMAPPABLE)
            .build(*engine_);
    loaded->setImage(*engine_, 0, FilamentBufferDescriptorFactory::rgbaPixels(pixels.data(), pixels.size()));
    if (levels > 1u) {
        loaded->generateMipmaps(*engine_);
    }
    LOG_INFO("Loaded material texture '{}' ({}x{})", path.string(), width, height);
    return loaded;
}

filament::Texture* TextureBindingService::createSolidTexture(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a, TextureColorSpace colorSpace) {
    if (!engine_) {
        return nullptr;
    }
    filament::Texture* texture = filament::Texture::Builder()
            .width(1)
            .height(1)
            .levels(1)
            .sampler(filament::Texture::Sampler::SAMPLER_2D)
            .format(colorSpace == TextureColorSpace::Srgb ? filament::Texture::InternalFormat::SRGB8_A8 : filament::Texture::InternalFormat::RGBA8)
            .usage(filament::Texture::Usage::UPLOADABLE | filament::Texture::Usage::SAMPLEABLE)
            .build(*engine_);
    const std::array<unsigned char, 4> pixel{{r, g, b, a}};
    texture->setImage(*engine_, 0, FilamentBufferDescriptorFactory::rgbaPixels(pixel.data(), pixel.size()));
    return texture;
}

void TextureBindingService::destroyTexture(filament::Texture*& texture) {
    if (engine_ && texture) {
        engine_->destroy(texture);
    }
    texture = nullptr;
}

} // namespace quader
