#include "AssetLibrary.h"
#include "scene/EnvironmentSerialization.h"
#include "BuiltinGeometry.h"
#include "scene/SceneDocument.h"
#include "math/Json.h"
#include "AsyncImports.h"
#include "DeletionPreparation.h"
#include "AtomicFile.h"
#include "resources/TextureStreamer.h"
#include "resources/StreamingModel.h"

#include <SDL3/SDL.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <type_traits>

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
    struct Batch { std::vector<std::string> paths; std::string destination; };
    std::mutex mutex;
    std::vector<Batch> batches;
    std::string error;
};

AssetLibrary::AssetLibrary(std::filesystem::path root,ResourceManager* resources)
    : imports_(std::make_shared<ImportQueue>()), projectRoot_(std::filesystem::absolute(std::move(root)).lexically_normal()),resources_(resources), references_(projectRoot_),browserCatalog_(projectRoot_) {
    if(resources_) { textureStreamer_=std::make_unique<TextureStreamer>(*resources_,projectRoot_);asyncImports_=std::make_unique<AsyncImports>(projectRoot_,*resources_); }
    if(!references_.status().empty()) status_=references_.status();
    const auto directory = projectRoot_ / "assets" / "textures";
    std::filesystem::create_directories(directory);
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && textureFile(entry.path())) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        try { const auto id=utf8(std::filesystem::relative(file,projectRoot_));if(!references_.deleted(id))addTexture(makeAsset(file)); }
        catch (const std::exception& error) { status_ += std::string(error.what()) + '\n'; }
    }
    const auto modelsDirectory = projectRoot_ / "assets" / "models";
    std::filesystem::create_directories(modelsDirectory);
    files.clear();
    for (const auto& entry : std::filesystem::directory_iterator(modelsDirectory)) {
        if (entry.is_directory() && !utf8(entry.path().filename()).starts_with(".staging-model-") &&
            std::filesystem::exists(entry.path() / "manifest.json"))
            files.push_back(entry.path() / "manifest.json");
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        try {
            const auto id=utf8(std::filesystem::relative(file,projectRoot_));
            if(references_.deleted(id)) continue;
            if(resources_) {
                std::ifstream input(file); const auto manifest=nlohmann::json::parse(input);
                catalog_.push_back({id,manifest.value("name",utf8(file.parent_path().filename())),AssetKind::Model}); ++catalogRevision_;
                const auto packageRoot=projectRoot_;const auto resolvers=modelResolvers();
                ResourceRequestOptions metadataOptions;metadataOptions.variant="material-catalog";metadataOptions.priority=ResourcePriority::Background;
                metadataOptions.dependencies={file,file.parent_path()/"model.gltf"};
                materialCatalogResources_[id]=resources_->request<ModelAsset>(AssetRef<ModelAsset>{id},[file,packageRoot,resolvers](std::stop_token token){
                    if(token.stop_requested())throw std::runtime_error("Material catalog scan canceled.");
                    auto data=loadModelMaterialMetadata(file,packageRoot,resolvers);
                    return ResourceLoadResult<ModelAsset>{data,sizeof(ModelAsset)+data->materials.size()*sizeof(ModelMaterial),0,{}};
                },std::move(metadataOptions));
            } else registerModel(loadModelPackage(file, projectRoot_,modelResolvers()));
        }
        catch (const std::exception& error) {
            status_ += "Model load failed (" + utf8(file.parent_path().filename()) + "): " + error.what() + '\n';
        }
    }
    // Images are independent assets even after their original model is deleted.
    for(const auto& entry:std::filesystem::recursive_directory_iterator(modelsDirectory))if(entry.is_regular_file()&&textureFile(entry.path())&&entry.path().parent_path().filename()=="images"&&!utf8(entry.path().parent_path().parent_path().filename()).starts_with(".staging-")) {
        const auto id=utf8(std::filesystem::relative(entry.path(),projectRoot_));if(references_.deleted(id)||find(id))continue;
        try {addTexture(makeAsset(entry.path()));}catch(const std::exception& error){status_+=std::string(error.what())+'\n';}
    }
    refreshMaterials();
    refreshScenes();
    refreshParticles();
}

AssetLibrary::~AssetLibrary() {
    preparedParticleMaterials_.clear();
    asyncImports_.reset(); modelResources_.clear(); materialResources_.clear();materialCatalogResources_.clear();
    if(textureStreamer_) { textureStreamer_.reset(); return; }
    for (const auto& asset : assets_) {
        destroyTexture(asset.thumbnail); destroyTexture(asset.colorTexture); destroyTexture(asset.dataTexture);
    }
    for(const auto& asset:retiredTextures_){destroyTexture(asset.thumbnail);destroyTexture(asset.colorTexture);destroyTexture(asset.dataTexture);}
}

void AssetLibrary::refreshScenes() {
    std::erase_if(catalog_, [](const AssetEntry& entry) { return entry.kind == AssetKind::Scene; });
    std::vector<std::string> errors;
    for (const auto& scene : scanSceneAssets(projectRoot_, &errors))
        if(!references_.deleted(scene.id))catalog_.push_back({scene.id, scene.name, AssetKind::Scene});
    for (const auto& error : errors) status_ += error + '\n';
    ++catalogRevision_;
}

BrowserCatalog& AssetLibrary::browserCatalog() {
    if (browserRevision_ != catalogRevision_) {
        browserCatalog_.synchronize(catalog_);
        browserRevision_ = catalogRevision_;
    }
    return browserCatalog_;
}

void AssetLibrary::refreshParticles() {
    std::unordered_map<std::string,std::string> previous;
    for(const auto& entry:catalog_) if(entry.kind==AssetKind::Particle)previous.emplace(entry.id,entry.name);
    std::erase_if(catalog_,[](const AssetEntry& entry){return entry.kind==AssetKind::Particle;});
    const auto directory=projectRoot_/"assets/particles";
    std::filesystem::create_directories(directory);
    std::unordered_set<std::string> present;
    for(const auto& file:std::filesystem::directory_iterator(directory)) {
        if(!file.is_regular_file() || file.path().extension()!=".crimson-particle") continue;
        const auto id=utf8(file.path().lexically_relative(projectRoot_));
        if(references_.deleted(id)) continue;
        present.insert(id);
        std::string name=previous.contains(id)?previous.at(id):utf8(file.path().stem());
        if(id==particleDocumentId_ && particles_.contains(id)) name=particles_.at(id).definition->name;
        else try {
            auto effect=std::make_shared<ParticleEffectAsset>(loadParticleAsset(projectRoot_,id));name=effect->name;publishParticle(std::move(effect));
        } catch(const std::exception& error) { particles_[id].diagnostic=error.what(); }
        catalog_.push_back({id,std::move(name),AssetKind::Particle});
    }
    std::erase_if(particles_,[&](const auto& entry){return !present.contains(entry.first) && entry.first!=particleDocumentId_;});
    ++catalogRevision_;
}
std::shared_ptr<const ParticleEffectAsset> AssetLibrary::findParticle(const std::string& id) const {
    const auto resolved=references_.resolveParticle(id);
    const auto found=particles_.find(resolved);
    return found==particles_.end()?nullptr:found->second.definition;
}
std::uint64_t AssetLibrary::particleRevision(const std::string& id) const {
    const auto found=particles_.find(references_.resolveParticle(id));
    return found==particles_.end()?0:found->second.revision;
}
std::string AssetLibrary::particleDiagnostic(const std::string& id) const {
    if(id.empty()) return "No particle effect assigned.";
    const auto found=particles_.find(references_.resolveParticle(id));
    return found==particles_.end()?"Unresolved particle effect: "+id:found->second.diagnostic;
}
void AssetLibrary::publishParticle(std::shared_ptr<const ParticleEffectAsset> effect) {
    if(!effect) return;
    validateParticleAsset(*effect);
    auto& version=particles_[effect->id];
    if(version.definition && serializeParticleAsset(*version.definition)==serializeParticleAsset(*effect)) { version.diagnostic.clear();return; }
    version.definition=std::move(effect);version.revision=nextParticleRevision_++;version.diagnostic.clear();
}

std::string AssetLibrary::displayName(const std::string& id) const {
    if(id==BuiltinBoxModelId)return "Box";
    const auto name=browserCatalog_.name(id);
    if (!name.empty()) return name;
    const auto found=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& entry){return entry.id==id;});
    return found==catalog_.end() ? id : found->name;
}

bool AssetLibrary::deleteScene(const std::string& id) {
    std::string error;
    if (!deleteSceneAsset(projectRoot_, id, error)) { status_ = error; return false; }
    if (selectedId() == id) browserSelection_.clear();
    refreshScenes();
    status_ = "Scene deleted.";
    return true;
}

const TextureAsset* AssetLibrary::find(const std::string& id) const {
    const auto found = std::find_if(assets_.begin(), assets_.end(), [&](const auto& asset) { return asset.id == id; });
    if(found==assets_.end()) return nullptr;
    if(textureStreamer_) {
        const auto view=textureStreamer_->request(id,projectRoot_/fromUtf8(id),streaming::TextureRole::Color,false,thumbnailDemand_,ResourcePriority::Preview,true);
        found->thumbnail=view.handle;
        if(view.sourceWidth && view.sourceHeight) { found->width=view.sourceWidth;found->height=view.sourceHeight; }
    }
    return &*found;
}
TextureAsset* AssetLibrary::findMutable(const std::string& id) const {
    const auto found = std::find_if(assets_.begin(), assets_.end(), [&](const auto& asset) { return asset.id == id; });
    if(found!=assets_.end())return &*found;
    if(id==GrayTextureId||id==NeutralNormalTextureId){TextureAsset asset;asset.id=id;asset.name=id==GrayTextureId?"Default gray":"Neutral normal";asset.width=asset.height=1;assets_.push_back(std::move(asset));return &assets_.back();}
    return nullptr;
}

std::shared_ptr<const ModelAsset> AssetLibrary::findModel(const std::string& id) const {
    const auto resolved=references_.resolve(id);
    if(resolved.empty()) return {};
    if(resolved==BuiltinBoxModelId) return builtinBoxGeometry();
    if(resources_) {
        modelInterest_[resolved]=std::chrono::steady_clock::now();
        auto found=modelResources_.find(resolved);
        if(found==modelResources_.end()) {
            const auto file=projectRoot_/fromUtf8(resolved),root=projectRoot_; const auto resolvers=modelResolvers();
            const auto forceCold=modelCacheRebuilds_.erase(resolved)!=0;
            const auto policy=references_;
            ResourceRequestOptions options; options.dependencies={file,file.parent_path()/"model.gltf",root/"assets"/"references.json"};
            // Missing optional policy files are not source dependencies.
            std::erase_if(options.dependencies,[](const auto& dependency){return !std::filesystem::is_regular_file(dependency);});
            std::ifstream manifestInput(file); if(manifestInput) try {
                const auto manifest=nlohmann::json::parse(manifestInput);
                for(const auto& dependency:manifest.at("dependencies")) if(dependency.at("kind")=="buffer") options.dependencies.push_back(file.parent_path()/fromUtf8(dependency.at("path").get<std::string>()));
            } catch(const std::exception&) { /* Loader supplies the diagnostic. */ }
            const auto packageDependencies=options.dependencies;
            auto handle=resources_->request<ModelAsset>(AssetRef<ModelAsset>{resolved},[file,root,resolvers,forceCold,policy,packageDependencies](std::stop_token token) {
                if(token.stop_requested()) throw std::runtime_error("Model load canceled.");
                auto data=forceCold?std::shared_ptr<ModelAsset>{}:streaming::loadModelMetadataCache(file,root);
                if(data) {
                    for(size_t index=0;index<data->materials.size();++index) if(resolvers.material) {
                        const auto materialId=index<data->materialIds.size()?data->materialIds[index]:importedMaterialId(data->id,index);
                        if(auto surface=resolvers.material(materialId)) data->materials[index]=*surface;
                    }
                    bool incompatible=false;
                    for(const auto& primitive:data->primitives) if(primitive.material<data->materials.size()) {
                        const auto& material=data->materials[primitive.material];
                        if(!material.triplanar && !material.normalTexture.image.empty() && primitive.tangentUv!=material.normalTexture.texCoord) incompatible=true;
                    }
                    if(incompatible) data.reset();
                }
                if(!data) {
                    data=loadModelPackage(file,root,resolvers);
                    data->stream=std::make_shared<streaming::ModelStream>(streaming::buildModelCache(*data,root));
                    for(auto& primitive:data->primitives) { primitive.sourceVertexCount=primitive.vertices.size();primitive.sourceIndexCount=primitive.indices.size();primitive.vertices=decltype(primitive.vertices){};primitive.indices=decltype(primitive.indices){}; }
                } else {
                    for(size_t index=0;index<data->materials.size();++index) if(resolvers.material) {
                        const auto materialId=index<data->materialIds.size()?data->materialIds[index]:importedMaterialId(data->id,index);
                        if(auto surface=resolvers.material(materialId)) data->materials[index]=*surface;
                    }
                }
                if(token.stop_requested()) throw std::runtime_error("Model load canceled.");
                std::size_t bytes=sizeof(ModelAsset)+data->primitives.size()*sizeof(ModelPrimitive)+data->nodes.size()*sizeof(ModelNode)+data->draws.size()*sizeof(ModelDraw)+data->materials.size()*sizeof(ModelMaterial);
                auto dependencies=packageDependencies;
                for(const auto& materialId:data->materialIds) {
                    const auto target=policy.resolve(materialId);
                    if(!target.empty() && !target.starts_with("builtin:") && std::filesystem::is_regular_file(root/fromUtf8(target))) dependencies.push_back(root/fromUtf8(target));
                }
                return ResourceLoadResult<ModelAsset>{data,bytes,0,std::move(dependencies)};
            },std::move(options));
            found=modelResources_.emplace(resolved,std::move(handle)).first;
        }
        return found->second.snapshot().shared();
    }
    const auto found = std::find_if(models_.begin(), models_.end(), [&](const auto& model) { return model->id == resolved; });
    return found == models_.end() ? nullptr : *found;
}
std::shared_ptr<const MaterialAsset> AssetLibrary::findMaterial(const std::string& id)const {
    const auto resolved=references_.resolve(id);if(resolved==DefaultMaterialId)return std::make_shared<MaterialAsset>(defaultMaterialAsset());
    if(resolved==DefaultParticleMaterialId)return std::make_shared<MaterialAsset>(defaultParticleMaterialAsset());
    materialInterest_[resolved]=std::chrono::steady_clock::now();
    if(const auto authored=authoredMaterials_.find(resolved);authored!=authoredMaterials_.end()&&authored->second.definition)return authored->second.definition;
    if(resources_ && !resolved.empty()) {
        materialInterest_[resolved]=std::chrono::steady_clock::now();
        auto found=materialResources_.find(resolved);
        if(found==materialResources_.end() && std::filesystem::is_regular_file(projectRoot_/fromUtf8(resolved))) {
            const auto root=projectRoot_; ResourceRequestOptions options;options.dependencies={root/fromUtf8(resolved)};
            auto handle=resources_->request<MaterialAsset>(AssetRef<MaterialAsset>{resolved},[root,resolved](std::stop_token token){
                if(token.stop_requested())throw std::runtime_error("Material load canceled.");
                auto data=std::make_shared<MaterialAsset>(loadMaterialAsset(root,resolved));return ResourceLoadResult<MaterialAsset>{data,sizeof(MaterialAsset),0,{}};
            },std::move(options));
            found=materialResources_.emplace(resolved,std::move(handle)).first;
        }
        if(found!=materialResources_.end()) if(auto snapshot=found->second.snapshot()) return snapshot.shared();
    }
    const auto found=std::find_if(materials_.begin(),materials_.end(),[&](const auto& m){return m->id==resolved;});return found==materials_.end()?nullptr:*found;
}
std::uint64_t AssetLibrary::materialRevision(const std::string& id) const {
    const auto resolved=references_.resolve(id);if(resolved==DefaultMaterialId||resolved==DefaultParticleMaterialId)return 1;
    if(const auto found=authoredMaterials_.find(resolved);found!=authoredMaterials_.end())return found->second.revision;
    const auto found=materialResources_.find(resolved);return found==materialResources_.end()?0:found->second.snapshot().revision();
}
std::string AssetLibrary::materialDiagnostic(const std::string& id) const {
    if(id.empty())return "No material assigned.";const auto resolved=references_.resolve(id);
    if(resolved==DefaultMaterialId||resolved==DefaultParticleMaterialId)return {};
    if(const auto found=authoredMaterials_.find(resolved);found!=authoredMaterials_.end())return found->second.diagnostic;
    if(const auto found=materialResources_.find(resolved);found!=materialResources_.end()&&!found->second.diagnostic().empty())return found->second.diagnostic();
    return "Unresolved material: "+id;
}
void AssetLibrary::publishMaterial(std::shared_ptr<const MaterialAsset> material) {
    if(!material)return;validateMaterialAsset(*material);
    if(material->id.starts_with("builtin:")||references_.deleted(material->id))throw std::runtime_error("Protected or deleted material cannot be published.");
    (void)materialAssetPath(projectRoot_,material->id);auto& version=authoredMaterials_[material->id];
    if(version.definition&&serializeMaterialAsset(*version.definition)==serializeMaterialAsset(*material)){version.diagnostic.clear();return;}
    version.definition=material;version.revision=nextMaterialRevision_++;version.diagnostic.clear();
    const auto old=std::find_if(materials_.begin(),materials_.end(),[&](const auto& value){return value->id==material->id;});
    if(old==materials_.end())materials_.push_back(material);else *old=material;
    const auto entry=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& value){return value.id==material->id;});
    if(entry==catalog_.end())catalog_.push_back({material->id,material->name,AssetKind::Material});else entry->name=material->name;
    ++catalogRevision_;
}
std::shared_ptr<const MaterialAsset> AssetLibrary::createMaterial(const std::string& name) {
    try{auto material=std::make_shared<MaterialAsset>(createMaterialAsset(projectRoot_,name));publishMaterial(material);noteAssetWrite(material->id);select(material->id);status_="Created "+material->name;return material;}
    catch(const std::exception& error){status_=error.what();return {};}
}
std::shared_ptr<const MaterialAsset> AssetLibrary::copyMaterial(const std::string& id,const std::string& name) {
    try{const auto source=findMaterial(id);if(!source)throw std::runtime_error("Material is unavailable: "+id);
        auto material=std::make_shared<MaterialAsset>(copyMaterialAsset(projectRoot_,*source,name));publishMaterial(material);noteAssetWrite(material->id);select(material->id);status_="Copied "+material->name;return material;}
    catch(const std::exception& error){status_=error.what();return {};}
}
ModelResolvers AssetLibrary::modelResolvers()const {
    // Capture an immutable policy snapshot; loaders may execute on a worker.
    const auto policy=references_;const auto root=projectRoot_;
    return {[policy](const auto& id){return policy.resolveTexture(id);},[policy,root](const auto& id)->std::optional<ModelMaterial>{
        const auto resolved=policy.resolveMaterial(id);if(resolved==DefaultMaterialId)return defaultMaterialAsset().surface;
        if(resolved==DefaultParticleMaterialId)throw std::runtime_error("Particle material is unsupported by the model renderer.");
        if(!resolved.empty()&&std::filesystem::exists(root/fromUtf8(resolved))){const auto material=loadMaterialAsset(root,resolved);if(!modelMaterialCompatible(material))throw std::runtime_error("Model material requires Standard PBR with Opaque or Mask blending: "+material.name);return material.surface;}return {};
    }};
}
std::vector<ModelMaterial> AssetLibrary::resolvedModelMaterials(const ModelAsset& model,const std::string& materialOverride)const {
    auto result=model.materials;
    if(!materialOverride.empty() && !isBuiltinModel(model.id))throw std::runtime_error("Instance materials are supported only on built-in Box models.");
    for(size_t i=0;i<result.size();++i) {
        const auto id=!materialOverride.empty()?materialOverride:i<model.materialIds.size()?model.materialIds[i]:importedMaterialId(model.id,i);
        if(auto material=findMaterial(id)){if(!modelMaterialCompatible(*material))throw std::runtime_error("Model material requires Standard PBR with Opaque or Mask blending: "+material->name);result[i]=material->surface;}
    }
    for(const auto& primitive:model.primitives) {
        if(primitive.material>=result.size()) throw std::runtime_error("Model has an invalid material reference.");
        const auto& material=result[primitive.material];
        if(material.triplanar)continue; // Local projection does not consume mesh UVs or tangents.
        for(const auto* slot:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture})
            if(!slot->image.empty() && (slot->texCoord>1 || !(primitive.uvMask&(1u<<slot->texCoord))))
                throw std::runtime_error("Material replacement requires an unavailable UV set: "+material.name);
        // The Box provides identical UV0/UV1, hence the same tangent basis.
        if(!material.normalTexture.image.empty() && primitive.tangentUv!=material.normalTexture.texCoord && !isBuiltinModel(model.id))
            throw std::runtime_error("Material replacement requires a different tangent UV basis: "+material.name);
    }
    return result;
}
std::uint64_t AssetLibrary::modelRevision(const std::string& id) const {
    if(id==BuiltinBoxModelId)return 1;
    const auto resolved=references_.resolve(id); const auto found=modelResources_.find(resolved);
    return found==modelResources_.end() ? 0 : found->second.snapshot().revision();
}
std::uint64_t AssetLibrary::modelFingerprint(const std::string& id) const {
    if(id==BuiltinBoxModelId)return 0x424f5801;
    const auto found=modelResources_.find(references_.resolve(id));return found==modelResources_.end()?0:found->second.snapshot().fingerprint();
}
bool AssetLibrary::modelLoading(const std::string& id) const {
    const auto found=modelResources_.find(references_.resolve(id));
    return found!=modelResources_.end() && found->second.state()!=ResourceState::Ready && found->second.state()!=ResourceState::Failed;
}
std::string AssetLibrary::modelDiagnostic(const std::string& id) const {
    const auto found=modelResources_.find(references_.resolve(id)); return found==modelResources_.end() ? std::string{} : found->second.diagnostic();
}
void AssetLibrary::updateResources(std::uint32_t bgfxFrame) {
    (void)bgfxFrame;
    if(!resources_) return;
    if(resourcePolicyRevision_ && resourcePolicyRevision_!=references_.revision()) {
        for(const auto& [id,handle]:modelResources_) { (void)handle;if(references_.deleted(id)) resources_->forget(id);else resources_->invalidate(id); }
        modelResources_.clear(); materialResources_.clear(); registeredModelRevisions_.clear();
    }
    resourcePolicyRevision_=references_.revision();
    const auto now=std::chrono::steady_clock::now();
    const auto expired=[&](auto& interest,auto& handles) {
        std::erase_if(interest,[&](const auto& pair) {
            if(now-pair.second<=resources_->config().idleGrace)return false;
            handles.erase(pair.first);return true;
        });
    };
    expired(modelInterest_,modelResources_);expired(materialInterest_,materialResources_);
    std::erase_if(preparedParticleMaterials_,[&](const auto& entry){return !materialInterest_.contains(entry.first);});
    for(auto iterator=materialCatalogResources_.begin();iterator!=materialCatalogResources_.end();) {
        if(auto snapshot=iterator->second.snapshot()) {
            try { promoteModelMaterials(projectRoot_,*snapshot,[&](const auto& id){return references_.deleted(id);});refreshMaterials(); }
            catch(const std::exception& error){status_=error.what();}
            iterator=materialCatalogResources_.erase(iterator);
        } else if(iterator->second.state()==ResourceState::Failed) { status_=iterator->second.diagnostic();iterator=materialCatalogResources_.erase(iterator); }
        else ++iterator;
    }
    for(const auto& [id,handle]:modelResources_) if(auto snapshot=handle.snapshot()) {
        if(registeredModelRevisions_[id]==snapshot.revision()) continue;
        try {
            std::erase_if(models_,[&](const auto& model){return model->id==id;});
            registerModel(snapshot.shared()); registeredModelRevisions_[id]=snapshot.revision();
        } catch(const std::exception& error) { status_=error.what(); }
    }
    textureStreamer_->advance(); textureStreamer_->beginFrame();
    const auto message=textureStreamer_->diagnostic(); if(!message.empty()) status_=message;
}
void AssetLibrary::onFileChanged(const std::filesystem::path& changed) {
    if(resources_)resources_->invalidatePath(changed);
    const auto id=utf8(changed.lexically_relative(projectRoot_));
    if(textureFile(changed)) {
        if(textureStreamer_)textureStreamer_->reload(id);
        if(!references_.deleted(id) && !findMutable(id) && std::filesystem::is_regular_file(changed)) addTexture(makeAsset(changed));
    }
    if(changed.extension()==".crimson-scene") refreshScenes();
    if(changed.extension()==".crimson-material") refreshMaterials();
    if(changed.extension()==".crimson-particle") refreshParticles();
}
void AssetLibrary::setTextureDemand(std::uint32_t edge,bool nativeQuality) { textureDemand_=std::max(1u,edge);nativeTextures_=nativeQuality; }
std::size_t AssetLibrary::textureResidentBytes() const { return textureStreamer_ ? textureStreamer_->residentBytes() : 0; }
void AssetLibrary::trimTextureCache(std::size_t bytes) { if(textureStreamer_) textureStreamer_->trim(bytes); }
bool AssetLibrary::modelBindingsReady(const std::vector<ModelMaterialTextures>& bindings) const {
    for(const auto& material:bindings) for(size_t slot=0;slot<material.handles.size();++slot)
        if(material.flags[slot]!=UINT32_MAX && !bgfx::isValid(material.handles[slot])) return false;
    return true;
}
bool AssetLibrary::modelMaterialsReady(const ModelAsset& model,const std::string& materialOverride) const {
    if(!materialOverride.empty() && !isBuiltinModel(model.id))return false;
    const auto ids=materialOverride.empty()?std::span<const std::string>(model.materialIds):std::span<const std::string>(&materialOverride,1);
    for(const auto& id:ids) {
        const auto target=references_.resolveMaterial(id);
        if(target==DefaultMaterialId || target.empty()) continue;
        const auto material=findMaterial(target);
        if(!material||!modelMaterialCompatible(*material)||!materialDiagnostic(target).empty())return false;
    }
    return true;
}
std::string AssetLibrary::modelMaterialsDiagnostic(const ModelAsset& model,const std::string& materialOverride) const {
    if(!materialOverride.empty() && !isBuiltinModel(model.id))return "Instance materials are supported only on built-in Box models.";
    const auto ids=materialOverride.empty()?std::span<const std::string>(model.materialIds):std::span<const std::string>(&materialOverride,1);
    for(const auto& id:ids) {
        const auto target=references_.resolveMaterial(id);
        if(target.empty() || target==DefaultMaterialId) continue;
        if(const auto material=findMaterial(target);material&&!modelMaterialCompatible(*material))return "Model material requires Standard PBR with Opaque or Mask blending: "+material->name;
        const auto diagnostic=materialDiagnostic(target);if(!diagnostic.empty())return diagnostic;
    }
    return {};
}
void AssetLibrary::rebuildModelCache(const std::string& id) {
    if(!resources_) return;
    const auto target=references_.resolve(id); if(target.empty())return;
    resources_->invalidate(target); modelResources_.erase(target);modelCacheRebuilds_.insert(target);
}
void AssetLibrary::refreshMaterials() {
    std::unordered_map<std::string,std::string> previous;
    for(const auto& entry:catalog_)if(entry.kind==AssetKind::Material)previous.emplace(entry.id,entry.name);
    std::erase_if(catalog_,[](const auto& e){return e.kind==AssetKind::Material;});materials_.clear();const auto directory=projectRoot_/"assets/materials";
    std::unordered_set<std::string> present;
    if(std::filesystem::exists(directory))for(const auto& file:std::filesystem::directory_iterator(directory))if(file.is_regular_file()&&file.path().extension()==".crimson-material") {
        const auto id=utf8(std::filesystem::relative(file.path(),projectRoot_));if(references_.deleted(id))continue;
        present.insert(id);std::string name=previous.contains(id)?previous.at(id):utf8(file.path().stem());
        const auto working=authoredMaterials_.find(id);
        if(id==materialDocumentId_&&working!=authoredMaterials_.end()&&working->second.definition){name=working->second.definition->name;materials_.push_back(working->second.definition);}
        else try{auto asset=std::make_shared<MaterialAsset>(loadMaterialAsset(projectRoot_,id));name=asset->name;
            auto& version=authoredMaterials_[id];if(!version.definition||serializeMaterialAsset(*version.definition)!=serializeMaterialAsset(*asset)){version.definition=asset;version.revision=nextMaterialRevision_++;}
            version.diagnostic.clear();materials_.push_back(version.definition);}
        catch(const std::exception& e){auto& version=authoredMaterials_[id];version.diagnostic=e.what();status_+=std::string(e.what())+'\n';if(version.definition)materials_.push_back(version.definition);}
        catalog_.push_back({id,std::move(name),AssetKind::Material});
    }
    std::erase_if(authoredMaterials_,[&](const auto& entry){return !present.contains(entry.first)&&entry.first!=materialDocumentId_;});
    ++catalogRevision_;
}

void AssetLibrary::addTexture(TextureAsset asset) {
    AssetEntry entry;
    try {
        entry = {asset.id, asset.name, asset.hdr ? AssetKind::Environment : AssetKind::Texture};
        assets_.reserve(assets_.size() + 1);
        catalog_.reserve(catalog_.size() + 1);
    } catch (...) {
        destroyTexture(asset.thumbnail); destroyTexture(asset.colorTexture); destroyTexture(asset.dataTexture);
        throw;
    }
    catalog_.push_back(std::move(entry));
    ++catalogRevision_;
    assets_.push_back(std::move(asset));
}

void AssetLibrary::registerModel(std::shared_ptr<const ModelAsset> model) {
    if (!model || std::any_of(models_.begin(),models_.end(),[&](const auto& existing){return existing->id==model->id;})) return;
    // Model-package image paths stay in their package. They share the ordinary
    // texture owner and remain usable in the editor's compatible image pickers.
    std::set<std::string> imageIds;
    std::vector<std::string> newMaterialFiles;
    for(const auto& id:model->materialIds)if(!references_.deleted(id)&&!std::filesystem::exists(projectRoot_/fromUtf8(id)))newMaterialFiles.push_back(id);
    promoteModelMaterials(projectRoot_,*model,[&](const auto& id){return references_.deleted(id);});
    for(const auto& id:newMaterialFiles)noteAssetWrite(id);
    const auto hasImage=[&](const auto& id){return std::any_of(assets_.begin(),assets_.end(),[&](const auto& image){return image.id==id;});};
    for (const auto& image : model->images) if (!image.empty() && !hasImage(image)&&!references_.deleted(image)) imageIds.insert(image);
    for (const auto& material : model->materials) {
        for (const auto* texture : {&material.baseColorTexture, &material.metallicRoughnessTexture,
                &material.normalTexture, &material.occlusionTexture, &material.emissiveTexture}) {
            if (!texture->image.empty() && !hasImage(texture->image.id)&&!references_.deleted(texture->image.id)&&!texture->image.id.starts_with("builtin:")) imageIds.insert(texture->image.id);
        }
    }
    std::vector<TextureAsset> pending;
    try {
        pending.reserve(imageIds.size());
        for (const auto& id : imageIds) {
            auto image = makeAsset(projectRoot_ / fromUtf8(id));
            image.name = model->name + " / " + image.name;
            pending.push_back(std::move(image));
        }
        assets_.reserve(assets_.size() + pending.size());
        catalog_.reserve(catalog_.size() + pending.size() + 1);
        models_.reserve(models_.size() + 1);
    } catch (...) {
        for (const auto& image : pending) destroyTexture(image.thumbnail);
        throw;
    }
    for (auto& image : pending) addTexture(std::move(image));
    if(std::none_of(catalog_.begin(),catalog_.end(),[&](const auto& existing){return existing.id==model->id;})) catalog_.push_back({model->id, model->name, AssetKind::Model});
    ++catalogRevision_;
    if(resources_ && !modelResources_.contains(model->id)) {
        auto compact=std::make_shared<ModelAsset>();
        compact->id=model->id;compact->name=model->name;compact->format=model->format;compact->meshes=model->meshes;compact->nodes=model->nodes;
        compact->roots=model->roots;compact->draws=model->draws;compact->materials=model->materials;compact->materialIds=model->materialIds;
        compact->images=model->images;compact->bounds=model->bounds;compact->warnings=model->warnings;compact->stream=model->stream;
        for(const auto& primitive:model->primitives) {
            ModelPrimitive metadata;metadata.material=primitive.material;metadata.uvMask=primitive.uvMask;metadata.tangentUv=primitive.tangentUv;
            metadata.sourceVertexCount=std::max(primitive.sourceVertexCount,primitive.vertices.size());metadata.sourceIndexCount=std::max(primitive.sourceIndexCount,primitive.indices.size());compact->primitives.push_back(std::move(metadata));
        }
        if(model->stream) {
            ResourceRequestOptions options;options.dependencies={projectRoot_/fromUtf8(model->id),projectRoot_/fromUtf8(model->id).parent_path()/"model.gltf"};
            resources_->seed<ModelAsset>(AssetRef<ModelAsset>{model->id},{compact,sizeof(ModelAsset)+compact->primitives.size()*sizeof(ModelPrimitive),0,{}},std::move(options));
            const auto root=projectRoot_;ResourceRequestOptions indexOptions;indexOptions.variant="metadata-index";indexOptions.priority=ResourcePriority::Background;indexOptions.retainLoader=false;
            (void)resources_->request<streaming::ModelStream>(AssetRef<streaming::ModelStream>{model->id},[compact,root](std::stop_token token){
                if(token.stop_requested())throw std::runtime_error("Model metadata cache canceled.");
                streaming::writeModelMetadataCache(*compact,*compact->stream,root);
                return ResourceLoadResult<streaming::ModelStream>{compact->stream,sizeof(streaming::ModelStream),compact->stream->key,{}};
            },std::move(indexOptions));
        }
    } else if(!resources_) models_.push_back(std::move(model));
    refreshMaterials();
}

TextureAsset AssetLibrary::makeAsset(const std::filesystem::path& path) {
    TextureAsset asset;
    asset.id = utf8(std::filesystem::relative(path, projectRoot_));
    asset.name = utf8(path.filename());
    asset.hdr = extension(path) == ".hdr";
    if(resources_) return asset;
    const auto pixels = decode(path);
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
            if (!find(id)) addTexture(makeAsset(destination));
            select(id);
            status_ = "Imported " + utf8(destination.filename());
            return find(id);
        }
        for (uint32_t suffix = 2; std::filesystem::exists(destination)||references_.deleted(utf8(destination.lexically_relative(projectRoot_))); ++suffix) {
            destination = folder / (source.stem().native() + L"-" + std::to_wstring(suffix) + source.extension().native());
        }
        TextureAsset asset;
        asset.id = utf8(std::filesystem::relative(destination, projectRoot_));
        asset.name = utf8(destination.filename());
        asset.width = pixels.width; asset.height = pixels.height;
        asset.hdr = extension(destination) == ".hdr";
        asset.thumbnail = upload(thumbnail(pixels), false);
        bgfx::setName(asset.thumbnail, asset.name.c_str());
        try { std::filesystem::copy_file(source, destination); }
        catch (...) { destroyTexture(asset.thumbnail); throw; }
        addTexture(std::move(asset));
        select(assets_.back().id);
        status_ = "Imported " + assets_.back().name;
        return &assets_.back();
    } catch (const std::exception& error) { status_ = error.what(); return nullptr; }
}

std::shared_ptr<const ModelAsset> AssetLibrary::importModel(const std::filesystem::path& source) {
    try {
        const auto stage=stageModelImport(source,projectRoot_);
        auto model=publishModelImport(stage,[&](const auto& id){return references_.deleted(id);});
        const bool registered=std::any_of(catalog_.begin(),catalog_.end(),[&](const auto& asset){return asset.id==model->id;});
        if (!stage->reused || !registered) registerModel(model);
        select(model->id);
        status_ = std::string(stage->reused ? "Reused " : "Imported ") + model->name;
        return model;
    } catch (const std::exception& error) { status_ = error.what(); return nullptr; }
}

size_t AssetLibrary::importFiles(const std::vector<std::filesystem::path>& sources, const std::string& destinationFolder) {
    std::set<std::string> previous;
    if (!destinationFolder.empty()) for (const auto& asset:catalog_) previous.insert(asset.id);
    size_t succeeded = 0;
    std::string failures;
    for (const auto& source : sources) {
        const auto type = extension(source);
        bool imported = false;
        if (type == ".gltf" || type == ".glb") imported = static_cast<bool>(importModel(source));
        else if (textureFile(source)) imported = importTexture(source) != nullptr;
        else status_ = "Unsupported asset format. Choose PNG, JPEG, TGA, BMP, HDR, glTF or GLB.";
        if (imported) ++succeeded;
        else failures += "\n" + utf8(source.filename()) + ": " + status_;
    }
    status_ = "Imported " + std::to_string(succeeded) + " of " + std::to_string(sources.size()) + " assets." + failures;
    if (!destinationFolder.empty()) {
        std::vector<std::string> added;
        for (const auto& asset:catalog_) if (!previous.contains(asset.id)) added.push_back(asset.id);
        if (!added.empty()) try { browserCatalog().move(added,destinationFolder); }
        catch (const std::exception& error) { status_ += "\nAssets imported into the browser root: " + std::string(error.what()); }
    }
    return succeeded;
}

void AssetLibrary::requestImport(SDL_Window* window, const std::string& destinationFolder) {
    static const SDL_DialogFileFilter filters[]{{"Supported assets", "png;jpg;jpeg;tga;bmp;hdr;gltf;glb"}};
    struct Request { std::shared_ptr<ImportQueue> queue; std::string destination; };
    auto* callbackOwner = new Request{imports_,destinationFolder};
    SDL_ShowOpenFileDialog([](void* data, const char* const* files, int) {
        std::unique_ptr<Request> owner(static_cast<Request*>(data));
        std::lock_guard lock(owner->queue->mutex);
        if (!files) { owner->queue->error = SDL_GetError(); return; }
        ImportQueue::Batch batch;
        batch.destination=owner->destination;
        for (size_t index = 0; files[index]; ++index) batch.paths.emplace_back(files[index]);
        if (!batch.paths.empty()) owner->queue->batches.push_back(std::move(batch));
    }, callbackOwner, window, filters, 1, nullptr, true);
}

void AssetLibrary::processImports() {
    if(!textureStreamer_) for(const auto& asset:retiredTextures_){destroyTexture(asset.thumbnail);destroyTexture(asset.colorTexture);destroyTexture(asset.dataTexture);}
    retiredTextures_.clear();
    std::vector<ImportQueue::Batch> batches;
    {
        std::lock_guard lock(imports_->mutex);
        batches.swap(imports_->batches);
        if (!imports_->error.empty()) { status_ = std::move(imports_->error); imports_->error.clear(); }
    }
    for (const auto& batch:batches) {
        std::vector<std::filesystem::path> sources;
        sources.reserve(batch.paths.size());
        for (const auto& path : batch.paths) sources.push_back(fromUtf8(path));
        if(asyncImports_)asyncImports_->enqueue(sources,batch.destination);
        else importFiles(sources,batch.destination);
    }
    if(asyncImports_)asyncImports_->pump(*this);
}

std::string AssetLibrary::loadPreview() {
    const auto file = projectRoot_ / "assets" / "preview.json";
    if (!std::filesystem::exists(file)) return {};
    try {
        std::ifstream input(file);
        const auto json = nlohmann::json::parse(input);
        if (json.at("version").get<int>() != 1) throw std::runtime_error("Unsupported preview version.");
        return json.at("model").get<std::string>();
    } catch (const std::exception& error) {
        status_ = std::string("Preview load failed: ") + error.what();
        return {};
    }
}

bool AssetLibrary::savePreview(const std::string& modelId) {
    try {
        const nlohmann::json json{{"version", 1}, {"model", modelId}};
        std::ofstream output(projectRoot_ / "assets" / "preview.json");
        output << json.dump(2) << '\n';
        if (!output) throw std::runtime_error("Could not write preview.json.");
        return true;
    } catch (const std::exception& error) { status_ = error.what(); return false; }
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

bgfx::TextureHandle AssetLibrary::fullTexture(TextureAsset& asset, bool srgb) const {
    if (asset.hdr) throw std::runtime_error("HDR images are supported in the environment slot only.");
    if(textureStreamer_) return textureStreamer_->request(asset.id,projectRoot_/fromUtf8(asset.id),srgb?streaming::TextureRole::Color:streaming::TextureRole::Data,
        false,nativeTextures_?UINT32_MAX:textureDemand_).handle;
    auto& handle = srgb ? asset.colorTexture : asset.dataTexture;
    auto& attempted = srgb ? asset.colorAttempted : asset.dataAttempted;
    if (!bgfx::isValid(handle) && !attempted) {
        attempted = true;
        if(asset.id==GrayTextureId||asset.id==NeutralNormalTextureId){Pixels pixels{1,1,{128,128,asset.id==NeutralNormalTextureId?uint8_t{255}:uint8_t{128},255}};handle=upload(pixels,srgb);}
        else handle = upload(decode(projectRoot_ / fromUtf8(asset.id)), srgb);
        bgfx::setName(handle, asset.name.c_str());
    }
    return handle;
}
MaterialTextures AssetLibrary::bindings(const Material& material) {
    MaterialTextures textures;
    try {
        if (auto* asset = findMutable(references_.resolveTexture(material.albedoMap))) textures.albedo = fullTexture(*asset, true);
        if (auto* asset = findMutable(references_.resolveTexture(material.roughnessMap))) textures.roughness = fullTexture(*asset, false);
        if (auto* asset = findMutable(references_.resolveTexture(material.metallicMap))) textures.metallic = fullTexture(*asset, false);
        if (auto* asset = findMutable(references_.resolveTexture(material.normalMap,true))) textures.normal = fullTexture(*asset, false);
    } catch (const std::exception& error) { status_ = error.what(); }
    return textures;
}

std::vector<ModelMaterialTextures> AssetLibrary::modelBindings(const ModelAsset& model,std::span<const ModelMaterial> materials) {
    if(materials.empty())materials=model.materials;
    std::vector<ModelMaterialTextures> result;result.reserve(materials.size());
    for(const auto& material:materials)result.push_back(materialBindings(material,false));return result;
}
ModelMaterialTextures AssetLibrary::materialBindings(const ModelMaterial& material,bool particle) const {
        ModelMaterialTextures target;
        const std::array<const ModelTexture*, 5> slots{&material.baseColorTexture, &material.metallicRoughnessTexture,
            &material.normalTexture, &material.occlusionTexture, &material.emissiveTexture};
        for (size_t slot = 0; slot < slots.size(); ++slot) {
            const auto& source = *slots[slot];
            const auto sampler=materialSampler(material,source);
            if (source.image.empty()) continue;
            const auto resolved=particle?references_.resolveParticleTexture(source.image.id):references_.resolveTexture(source.image.id,slot==2);
            if(resolved.empty())continue;
            auto* image = findMutable(resolved);
            if (!image) throw std::runtime_error("Material image is unavailable: " + source.image.id);
            if(image->hdr)throw std::runtime_error("HDR images are unsupported in material slots: "+source.image.id);
            if(!textureStreamer_&&!image->id.starts_with("builtin:")&&!std::filesystem::is_regular_file(projectRoot_/fromUtf8(image->id)))throw std::runtime_error("Material image is missing: "+source.image.id);
            if(textureStreamer_) {
                const auto role=slot==2?streaming::TextureRole::Normal:(slot==0||slot==4?streaming::TextureRole::Color:streaming::TextureRole::Data);
                const bool mips=sampler.minFilter!=ModelFilter::Linear && sampler.minFilter!=ModelFilter::Nearest;
                const auto view=textureStreamer_->request(image->id,projectRoot_/fromUtf8(image->id),role,mips,nativeTextures_?UINT32_MAX:textureDemand_,
                    nativeTextures_?ResourcePriority::Selected:ResourcePriority::Visible);
                target.handles[slot]=view.handle;target.leases[slot]=view.lease;
                target.sourceFingerprints[slot]=view.sourceFingerprint;target.signatures[slot]=view.signature;
                target.complete &=view.complete;target.sourceReady &=view.sourceReady;
                if(particle&&!view.diagnostic.empty())throw std::runtime_error("Particle color texture "+source.image.id+": "+view.diagnostic);
                if(!bgfx::isValid(view.handle) && !view.diagnostic.empty()) throw std::runtime_error(view.diagnostic);
            } else target.handles[slot] = fullTexture(*image, slot == 0 || slot == 4);
            if (!textureStreamer_ && !bgfx::isValid(target.handles[slot]))
                throw std::runtime_error("Model image GPU upload failed: " + source.image.id);
            uint32_t flags = 0;
            if (sampler.wrapU == ModelWrap::Clamp) flags |= BGFX_SAMPLER_U_CLAMP;
            else if (sampler.wrapU == ModelWrap::Mirror) flags |= BGFX_SAMPLER_U_MIRROR;
            if (sampler.wrapV == ModelWrap::Clamp) flags |= BGFX_SAMPLER_V_CLAMP;
            else if (sampler.wrapV == ModelWrap::Mirror) flags |= BGFX_SAMPLER_V_MIRROR;
            if (sampler.minFilter == ModelFilter::Nearest||sampler.minFilter==ModelFilter::NearestMipmapNearest||sampler.minFilter==ModelFilter::NearestMipmapLinear) flags |= BGFX_SAMPLER_MIN_POINT;
            if(sampler.minFilter==ModelFilter::NearestMipmapNearest||sampler.minFilter==ModelFilter::LinearMipmapNearest)flags|=BGFX_SAMPLER_MIP_POINT;
            if (sampler.magFilter == ModelFilter::Nearest) flags |= BGFX_SAMPLER_MAG_POINT;
            target.flags[slot] = flags;
        }
    return target;
}
std::shared_ptr<const PreparedParticleMaterial> AssetLibrary::preparedParticleMaterial(const std::string& id) const {
    std::string resolved;
    try{resolved=references_.resolveParticleMaterial(id);}catch(const std::exception& error){auto failed=std::make_shared<PreparedParticleMaterial>();failed->diagnostic=error.what();return failed;}
    const auto now=std::chrono::steady_clock::now();materialInterest_[resolved]=now;
    const auto grace=resources_?resources_->config().idleGrace:std::chrono::seconds(5);
    std::erase_if(preparedParticleMaterials_,[&](const auto& entry){const auto interest=materialInterest_.find(entry.first);return entry.first!=resolved&&(interest==materialInterest_.end()||now-interest->second>grace);});
    // Only catalog materials plus the two built-in/default and open-document
    // identities are retained. Unresolved scene IDs do not create cache entries.
    const bool cacheable=resolved==DefaultParticleMaterialId||resolved==materialDocumentId_||
        std::any_of(catalog_.begin(),catalog_.end(),[&](const auto& entry){return entry.kind==AssetKind::Material&&entry.id==resolved;});
    auto result=std::make_shared<PreparedParticleMaterial>();
    const auto previous=preparedParticleMaterials_.find(resolved);
    try{
        const auto material=findMaterial(resolved);if(!material)throw std::runtime_error("Unresolved particle material: "+id);
        if(!particleMaterialCompatible(*material))throw std::runtime_error("Particle material requires Unlit Alpha/Additive, base color at UV0, and no unsupported texture slots: "+material->name);
        const auto diagnostic=materialDiagnostic(resolved);if(!diagnostic.empty())throw std::runtime_error(diagnostic);
        auto textures=materialBindings(material->surface,true);
        if(!textures.sourceReady||(textures.flags[0]!=UINT32_MAX&&!bgfx::isValid(textures.handles[0])))throw std::runtime_error("Preparing particle color texture: "+material->surface.baseColorTexture.image.id);
        struct Pins {std::shared_ptr<const MaterialAsset> material;ModelMaterialTextures textures;};
        const auto pins=std::make_shared<Pins>(Pins{material,std::move(textures)});
        result->render.baseColor=material->surface.baseColor;result->render.blend=material->blend==MaterialBlend::Alpha?ParticleBlendMode::Alpha:ParticleBlendMode::Additive;
        result->render.uvScale=material->surface.uvScale;result->render.uvOffset=material->surface.uvOffset;result->render.uvRotation=material->surface.uvRotation;result->render.triplanar=material->surface.triplanar;
        result->render.colorTexture=pins->textures.handles[0];result->render.samplerFlags=pins->textures.flags[0];result->render.pinned=pins;
        result->ready=true;result->current=true;result->sourceFingerprint=pins->textures.sourceFingerprints[0];
        result->signature=1469598103934665603ull;for(const auto value:serializeMaterialAsset(*material))result->signature=(result->signature^static_cast<uint8_t>(value))*1099511628211ull;
        result->signature=(result->signature^pins->textures.signatures[0])*1099511628211ull;
        if(!material->surface.baseColorTexture.image.empty()&&references_.resolveParticleTexture(material->surface.baseColorTexture.image.id).empty())
            result->diagnostic="Deleted particle color texture uses the procedural fallback: "+material->surface.baseColorTexture.image.id;
        if(cacheable)preparedParticleMaterials_[resolved]=result;
    }catch(const std::exception& error){
        if(previous!=preparedParticleMaterials_.end()&&previous->second->ready)*result=*previous->second;
        result->current=false;
        result->diagnostic=error.what();
    }
    return result;
}
ModelMaterialTextures AssetLibrary::materialPreviewBindings(const MaterialAsset& material) const {
    validateMaterialAsset(material);
    return materialBindings(material.surface,particleMaterialCompatible(material));
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

bool AssetLibrary::assignEnvironment(EnvironmentSettings& environment, const std::string& id,bool persistLegacy) {
    if (!find(id)) return false;
    environment.map = id;
    if(persistLegacy)saveEnvironment(environment);
    return true;
}

void AssetLibrary::clearEnvironment(EnvironmentSettings& environment,bool persistLegacy) {
    environment.map.clear();
    if(persistLegacy)saveEnvironment(environment);
}

void AssetLibrary::loadEnvironment(EnvironmentSettings& environment) {
    const auto file = projectRoot_ / "assets" / "environment.json";
    if (!std::filesystem::exists(file)) return;
    try {
        std::ifstream input(file);
        const auto json = nlohmann::json::parse(input);
        auto compatible=json;
        // Retain the legacy file's bounded intensity/rotation recovery behavior.
        const auto intensity=json.value("intensity",1.f),rotation=json.value("rotation",0.f);
        compatible["intensity"]=std::isfinite(intensity)?std::clamp(intensity,0.f,10.f):1.f;
        compatible["rotation"]=std::isfinite(rotation)?std::clamp(rotation,-180.f,180.f):0.f;
        auto restored=readEnvironmentSettings(compatible);
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
        auto json=environmentSettingsJson(environment);json["version"]=4;
        writeFileAtomically(projectRoot_ / "assets" / "environment.json",json.dump(2)+'\n');
    } catch (const std::exception& error) { status_ = error.what(); }
}

namespace {
std::vector<SceneSnapshot> savedScenes(const std::filesystem::path& root) {
    std::vector<SceneSnapshot> result;const auto directory=root/"assets/scenes";if(!std::filesystem::exists(directory))return result;
    for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.is_regular_file()&&entry.path().extension()==".crimson-scene"){
        const auto id="assets/scenes/"+utf8(entry.path().filename());try{result.push_back(readSceneSnapshot(root,id));}
        catch(const std::exception& error){throw std::runtime_error("Cannot inspect scene "+id+": "+error.what());}
    }return result;
}
uint64_t mix(uint64_t value,uint64_t input){return (value^input)*1099511628211ull;}
uint64_t deletionRevision(const std::filesystem::path& root,const AssetReferenceManager& references,uint64_t catalogRevision,const SceneDocument* active){
    auto result=mix(1469598103934665603ull,references.revision());result=mix(result,catalogRevision);
    if(active){result=mix(result,active->scene().revision());result=mix(result,active->scene().generation());result=mix(result,active->history().size());}
    for(const auto& directory:{root/"assets/scenes",root/"assets/materials",root/"assets/models",root/"assets/textures",root/"assets/particles"})if(std::filesystem::exists(directory))
        for(const auto& item:std::filesystem::recursive_directory_iterator(directory))if(item.is_regular_file()){
            for(const auto c:utf8(item.path().lexically_relative(root)))result=mix(result,static_cast<uint8_t>(c));
            result=mix(result,item.file_size());result=mix(result,static_cast<uint64_t>(item.last_write_time().time_since_epoch().count()));}
    const auto browser=root/"assets/browser.json";if(std::filesystem::exists(browser)){std::ifstream input(browser,std::ios::binary);for(char c;input.get(c);)result=mix(result,static_cast<uint8_t>(c));}
    return result;
}
size_t removeDeletedModels(SceneSnapshot& snapshot,const AssetReferenceManager& previous,const AssetReferenceManager& next,const std::unordered_set<std::string>& deletedModels){
    Scene source;source.apply(snapshot.records,snapshot.properties);std::unordered_set<EntityId> removed;
    std::unordered_map<EntityId,std::size_t> ranks;const auto preorder=source.preorder();for(size_t index=0;index<preorder.size();++index)ranks.emplace(preorder[index],index);
    std::unordered_set<EntityId> changedParents;
    for(const auto& record:snapshot.records)if(record.model&&deletedModels.contains(previous.resolve(record.model->id))&&next.resolve(record.model->id).empty())removed.insert(record.id);
    if(removed.empty())return 0;
    for(const auto& record:snapshot.records)if(removed.contains(record.id))changedParents.insert(record.parent);
    for(auto& record:snapshot.records)if(!removed.contains(record.id)&&removed.contains(record.parent)){
        const auto oldWorld=source.worldTransform(record.id);if(!oldWorld)throw std::runtime_error("Cannot preserve child placement while deleting its model parent.");
        auto parent=record.parent;while(parent&&removed.contains(parent)){const auto ancestor=source.find(parent);parent=ancestor?ancestor->parent:NoEntity;}
        record.parent=parent;record.local=*oldWorld;
        if(parent){const auto world=source.worldTransform(parent);const auto inverse=world?math::inverse(*world):std::nullopt;
            if(inverse)record.local=*inverse**oldWorld;else record.parent=NoEntity;}
        changedParents.insert(record.parent);
    }
    std::erase_if(snapshot.records,[&](const auto& record){return removed.contains(record.id);});
    if(removed.contains(snapshot.properties.activeEnvironment))snapshot.properties.activeEnvironment=NoEntity;
    if(removed.contains(snapshot.properties.activeCamera))snapshot.properties.activeCamera=NoEntity;
    if(removed.contains(snapshot.properties.playerCharacter))snapshot.properties.playerCharacter=NoEntity;
    for(auto& record:snapshot.records)if(record.camera && removed.contains(record.camera->followTarget)) {
        record.camera->followTarget=NoEntity;record.camera->followOffset={};
    }
    // Stable ordering among surviving siblings; newly promoted children append in original preorder.
    for(const auto parent:changedParents) {
        std::vector<EntityRecord*> siblings;for(auto& record:snapshot.records)if(record.parent==parent)siblings.push_back(&record);
        std::sort(siblings.begin(),siblings.end(),[&](const auto* left,const auto* right){return ranks.at(left->id)<ranks.at(right->id);});
        for(size_t index=0;index<siblings.size();++index)siblings[index]->order=static_cast<uint32_t>(index);
    }
    Scene::validate(snapshot.records,snapshot.properties);return removed.size();
}
size_t replaceDeletedParticles(SceneSnapshot& snapshot,const AssetReferenceManager& previous,const AssetReferenceManager& next,const std::unordered_set<std::string>& deletedParticles){
    size_t changed{};
    for(auto& record:snapshot.records)if(record.particle&&!record.particle->empty()&&deletedParticles.contains(previous.resolveParticle(record.particle->id))){
        const auto replacement=next.resolveParticle(record.particle->id);
        if(record.particle->id!=replacement){record.particle=AssetRef<ParticleEffectAsset>{replacement};++changed;}
    }
    Scene::validate(snapshot.records,snapshot.properties);return changed;
}
size_t replaceDeletedModelMaterials(SceneSnapshot& snapshot,const AssetReferenceManager& previous,const AssetReferenceManager& next,
    const std::unordered_set<std::string>& deletedMaterials,const std::filesystem::path& root){
    size_t changed{};
    for(auto& record:snapshot.records)if(!record.modelMaterial.empty() && deletedMaterials.contains(previous.resolveMaterial(record.modelMaterial.id))) {
        const auto replacement=next.resolveModelMaterial(record.modelMaterial.id);
        const auto material=loadMaterialAsset(root,replacement);
        if(!modelMaterialCompatible(material))throw std::runtime_error("Box material replacement requires Standard PBR with Opaque or Mask blending: "+material.name);
        if(record.modelMaterial.id!=replacement){record.modelMaterial=AssetRef<MaterialAsset>{replacement};++changed;}
    }
    Scene::validate(snapshot.records,snapshot.properties);return changed;
}
}
void AssetLibrary::refreshReferenceIndex(const SceneDocument* active){
    std::vector<AssetUsage> usages;
    std::unordered_set<std::string> usableMaterials;
    for(const auto& entry:catalog_)if(entry.kind==AssetKind::Material&&std::filesystem::is_regular_file(projectRoot_/fromUtf8(entry.id)))usableMaterials.insert(entry.id);
    for(const auto& stored:materials_){const auto material=stored->id==materialDocumentId_?std::make_shared<MaterialAsset>(loadMaterialAsset(projectRoot_,stored->id)):stored;
        const auto& m=material->surface;const std::array<std::pair<const char*,const ModelTexture*>,5> slots{{
        {"Base color",&m.baseColorTexture},{"Metallic / roughness",&m.metallicRoughnessTexture},{"Normal",&m.normalTexture},{"Occlusion",&m.occlusionTexture},{"Emissive",&m.emissiveTexture}}};
        for(const auto& [slot,texture]:slots)if(!texture->image.empty())usages.push_back({texture->image.id,material->id,material->name,slot,false,false,!usableMaterials.contains(material->id)});
    }
    for(const auto& entry:catalog_)if(entry.kind==AssetKind::Particle){const auto effect=loadParticleAsset(projectRoot_,entry.id);
        if(!effect.emitter.material.empty()&&!effect.emitter.material.id.starts_with("builtin:"))usages.push_back({effect.emitter.material.id,effect.id,effect.name,"Particle material"});}
    for(const auto& id:particleDocumentReferences_)if(!id.empty()&&!id.starts_with("builtin:"))usages.push_back({id,particleDocumentId_,"Open particle effect","Particle material",true,false});
    for(const auto& id:particleDocumentHistory_)if(!id.empty()&&!id.starts_with("builtin:"))usages.push_back({id,particleDocumentId_,"Open particle effect","Particle material undo / redo history",true,true});
    for(const auto& id:materialDocumentReferences_)if(!id.empty()&&!id.starts_with("builtin:"))usages.push_back({id,materialDocumentId_,"Open material","Material texture",true,false});
    for(const auto& id:materialDocumentHistory_)if(!id.empty()&&!id.starts_with("builtin:"))usages.push_back({id,materialDocumentId_,"Open material","Material texture undo / redo history",true,true});
    // Inspect lightweight material metadata for every package, including models
    // never opened in the viewport. Authored usage cannot depend on residency.
    for(const auto& entry:catalog_) if(entry.kind==AssetKind::Model) {
        const auto model=loadModelMaterialMetadata(projectRoot_/fromUtf8(entry.id),projectRoot_,modelResolvers());
        for(size_t index=0;index<model->materials.size();++index) {
            const auto materialId=importedMaterialId(model->id,index);
            usages.push_back({materialId,model->id,model->name,"Material "+std::to_string(index+1)});
            const auto& material=model->materials[index];
            // Before promotion, or after a missing sidecar, the usable material
            // definition belongs to the model package. Do not invent an owner
            // asset that the browser cannot select and delete with its model.
            const bool native=usableMaterials.contains(references_.resolve(materialId));
            for(const auto* texture:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture})
                if(!texture->image.empty()) usages.push_back({texture->image.id,native?materialId:model->id,native?material.name:model->name,
                    native?"Texture":material.name+" / Texture"});
        }
    }
    const auto addScene=[&](const std::vector<EntityRecord>& records,const std::string& id,const std::string& name,bool isActive){
        for(const auto& record:records){if(record.model)usages.push_back({record.model->id,id,name,record.name,isActive,false});
            if(!record.modelMaterial.empty())usages.push_back({record.modelMaterial.id,id,name,record.name+" / Box material",isActive,false});
            if(record.particle&&!record.particle->empty())usages.push_back({record.particle->id,id,name,record.name+" / Particle effect",isActive,false});
            if(record.environment&&!record.environment->environment.map.empty())usages.push_back({record.environment->environment.map,id,name,record.name+" / Environment",isActive,false});}
    };
    for(const auto& scene:savedScenes(projectRoot_))addScene(scene.records,scene.id,scene.name,false);
    if(active){addScene(active->scene().records(),active->assetId(),active->displayName(),true);
        for(const auto& id:active->historicalModelReferences())usages.push_back({id,active->assetId(),active->displayName(),"Undo / redo history",true,true});
        for(const auto& id:active->historicalMaterialReferences())usages.push_back({id,active->assetId(),active->displayName(),"Undo / redo history / Box material",true,true});
        for(const auto& id:active->historicalParticleReferences())usages.push_back({id,active->assetId(),active->displayName(),"Particle undo / redo history",true,true});}
    for(const auto& file:{"material.json","environment.json","preview.json","editor-session.json"}){
        const auto source=projectRoot_/"assets"/file;if(!std::filesystem::exists(source))continue;
        try{std::ifstream input(source);const auto j=nlohmann::json::parse(input);
            for(const auto* slot:{"albedoMap","roughnessMap","metallicMap","normalMap","map","model","scene"})if(j.contains(slot)&&j[slot].is_string()){
                // These preserved files belong to the retired sphere/preview
                // workflow or editor session. They remain resolvable history,
                // rather than surviving scene-editor resource owners.
                const auto id=j[slot].get<std::string>();if(!id.empty())usages.push_back({id,"assets/"+std::string(file),file,slot,false,false,true});}}
        catch(const std::exception& e){throw std::runtime_error("Cannot inspect legacy resource references in "+std::string(file)+": "+e.what());}
    }
    references_.setUsages(std::move(usages));
}
AssetDeletionPlan AssetLibrary::planDeletion(const std::vector<std::string>& ids,const SceneDocument* active){
    AssetDeletionPlan plan;plan.selectedIds=ids;
    try{auto& browser=browserCatalog();std::unordered_set<std::string> expanded;
        for(const auto& id:ids){if(!browser.contains(id))throw std::runtime_error("A selected asset no longer exists.");expanded.insert(id);
            if(browser.isFolder(id))for(const auto& child:browser.descendants(id))expanded.insert(child);}
        std::unordered_set<std::string> models,particles,materials;
        for(const auto& id:expanded){if(browser.isFolder(id)){plan.folderIds.push_back(id);continue;}
            const auto entry=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& a){return a.id==id;});if(entry==catalog_.end())throw std::runtime_error("Asset deletion target is unavailable.");
            plan.assetIds.push_back(id);if(entry->kind==AssetKind::Model)models.insert(id);if(entry->kind==AssetKind::Particle)particles.insert(id);if(entry->kind==AssetKind::Material)materials.insert(id);}
        refreshReferenceIndex(active);
        std::unordered_set<std::string> usableOwners;
        for(const auto& entry:catalog_)if(std::filesystem::is_regular_file(projectRoot_/fromUtf8(entry.id)))usableOwners.insert(entry.id);
        // A valid saved scene can arrive from disk before the browser's watcher
        // refresh. Its actual authored dependencies must still be protected.
        const auto scenes=savedScenes(projectRoot_);for(const auto& scene:scenes)usableOwners.insert(scene.id);
        for(const auto& id:plan.assetIds){const auto users=references_.users(id);plan.usages.insert(plan.usages.end(),users.begin(),users.end());
            const auto authored=references_.authoredUsers(id,plan.assetIds);for(const auto& usage:authored)if(usableOwners.contains(usage.ownerId))plan.warningUsages.push_back(usage);}
        for(const auto& scene:scenes){size_t count=0;for(const auto& entity:scene.records)
            if((entity.model&&models.contains(references_.resolve(entity.model->id)))||(entity.particle&&!entity.particle->empty()&&particles.contains(references_.resolveParticle(entity.particle->id)))||
                (!entity.modelMaterial.empty()&&materials.contains(references_.resolveMaterial(entity.modelMaterial.id))))++count;
            if(count)plan.scenes.push_back({scene.id,scene.name,count});}
        if(active){for(const auto& entity:active->scene().records())if(entity.model&&models.contains(references_.resolve(entity.model->id)))plan.clearsActiveHistory=true;
            for(const auto& id:active->historicalModelReferences())if(models.contains(references_.resolve(id)))plan.clearsActiveHistory=true;
            for(const auto& entity:active->scene().records())if(!entity.modelMaterial.empty()&&materials.contains(references_.resolveMaterial(entity.modelMaterial.id)))plan.clearsActiveHistory=true;
            for(const auto& id:active->historicalMaterialReferences())if(materials.contains(references_.resolveMaterial(id)))plan.clearsActiveHistory=true;
            for(const auto& entity:active->scene().records())if(entity.particle&&!entity.particle->empty()&&particles.contains(references_.resolveParticle(entity.particle->id)))plan.clearsActiveHistory=true;
            for(const auto& id:active->historicalParticleReferences())if(particles.contains(references_.resolveParticle(id)))plan.clearsActiveHistory=true;}
        std::sort(plan.assetIds.begin(),plan.assetIds.end());std::sort(plan.folderIds.begin(),plan.folderIds.end());
        plan.revision=deletionRevision(projectRoot_,references_,catalogRevision_,active);
    }catch(const std::exception& e){plan.error=e.what();}return plan;
}
std::vector<AssetEntry> AssetLibrary::fallbackCandidates(const std::string& id)const{
    std::vector<AssetEntry> result;const auto source=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& e){return e.id==id;});if(source==catalog_.end())return result;
    if(source->kind==AssetKind::Scene)return result;
    bool sprite=false,model=false;
    if(source->kind==AssetKind::Material)for(const auto& usage:references_.users(id))if(usage.targetId==id||references_.resolve(usage.targetId)==id){
        sprite|=usage.ownerId.starts_with("assets/particles/");model|=usage.ownerId.starts_with("assets/models/")||usage.slot.ends_with(" / Box material");}
    for(const auto& entry:catalog_)if(entry.id!=id&&(entry.kind==source->kind||(source->kind==AssetKind::Environment&&entry.kind==AssetKind::Texture))){
        if(source->kind==AssetKind::Material){const auto material=findMaterial(entry.id);if(!material||(sprite&&!particleMaterialCompatible(*material))||(model&&!modelMaterialCompatible(*material)))continue;}
        result.push_back(entry);}
    return result;
}
struct AssetLibrary::PendingDeletion {
    AssetDeletionPlan plan;
    std::unordered_map<std::string,std::string> replacements;
    SceneDocument* active{};
    uint64_t documentGeneration{};
    std::string key;
    ResourceHandle<DeletionValidation> validation;
    std::shared_ptr<DeletionProgress> progress;
};
bool AssetLibrary::deletionPending()const{return bool(pendingDeletion_);}
void AssetLibrary::cancelDeletion(){if(pendingDeletion_&&resources_)resources_->forget(pendingDeletion_->key);pendingDeletion_.reset();}
bool AssetLibrary::pumpDeletion(SceneDocument* active){
    const auto pending=pendingDeletion_;if(!pending)return false;
    return deleteAssets(pending->plan,pending->replacements,active);
}
bool AssetLibrary::deleteAssets(const AssetDeletionPlan& plan,const std::unordered_map<std::string,std::string>& replacements,SceneDocument* active){
    static_assert(std::is_nothrow_move_assignable_v<AssetReferenceManager>);
    static_assert(std::is_nothrow_move_assignable_v<BrowserCatalog>);
    static_assert(std::is_nothrow_move_assignable_v<TextureAsset>);
    bool durable=false;
    try{
        if(!plan.error.empty())throw std::runtime_error(plan.error);
        if(plan.revision!=deletionRevision(projectRoot_,references_,catalogRevision_,active))throw std::runtime_error("Assets or scenes changed while deletion was pending. Review the updated deletion list.");
        if(active&&active->transactionActive())throw std::runtime_error("Finish the active scene edit before deleting assets.");
        if(pendingDeletion_&&(pendingDeletion_->active!=active||(active&&pendingDeletion_->documentGeneration!=active->documentGeneration())||
            pendingDeletion_->plan.revision!=plan.revision||pendingDeletion_->plan.assetIds!=plan.assetIds||pendingDeletion_->replacements!=replacements))
            throw std::runtime_error("The document or deletion choices changed while validation was pending.");
        std::vector<AssetEntry> deleted;std::unordered_set<std::string> deletedModels,deletedParticles,deletedMaterials,deletedIds(plan.assetIds.begin(),plan.assetIds.end());
        if(!particleDocumentId_.empty()&&deletedIds.contains(particleDocumentId_))throw std::runtime_error("Close the particle document before deleting its asset.");
        if(!materialDocumentId_.empty()&&deletedIds.contains(materialDocumentId_))throw std::runtime_error("Close the material document before deleting its asset.");
        for(const auto& id:plan.assetIds){const auto entry=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& a){return a.id==id;});if(entry==catalog_.end())throw std::runtime_error("Deletion target disappeared.");
            deleted.push_back(*entry);if(entry->kind==AssetKind::Model)deletedModels.insert(id);if(entry->kind==AssetKind::Particle)deletedParticles.insert(id);if(entry->kind==AssetKind::Material)deletedMaterials.insert(id);
            if(const auto chosen=replacements.find(id);chosen!=replacements.end()&&!chosen->second.empty()){
                const auto options=fallbackCandidates(id);if(std::none_of(options.begin(),options.end(),[&](const auto& option){return option.id==chosen->second;}))throw std::runtime_error("Fallback has an incompatible asset type.");}}
        auto nextReferences=references_;nextReferences.replace(deleted,replacements);
        std::vector<std::shared_ptr<const ModelAsset>> nextModels;
        std::vector<AssetEntry> surviving;for(const auto& entry:catalog_)if(entry.kind==AssetKind::Model&&!deletedModels.contains(entry.id))surviving.push_back(entry);
        std::vector<AssetEntry> survivingParticles;for(const auto& entry:catalog_)if(entry.kind==AssetKind::Particle&&!deletedParticles.contains(entry.id))survivingParticles.push_back(entry);
        if(resources_){
            if(!pendingDeletion_){
                static std::atomic_uint64_t sequence{};auto pending=std::make_shared<PendingDeletion>();pending->plan=plan;pending->replacements=replacements;
                pending->active=active;pending->documentGeneration=active?active->documentGeneration():0;pending->key="import:delete/"+std::to_string(sequence.fetch_add(1));
                pending->progress=std::make_shared<DeletionProgress>();pending->progress->total=surviving.size()+survivingParticles.size();
                ResourceRequestOptions options;options.priority=ResourcePriority::Selected;options.retainLoader=false;
                const auto root=projectRoot_;const auto policy=nextReferences;const auto progress=pending->progress;
                pending->validation=resources_->request<DeletionValidation>(AssetRef<DeletionValidation>{pending->key},[root,surviving,survivingParticles,policy,progress](std::stop_token stop){
                    auto result=validateAssetDeletion(root,surviving,policy,stop,progress,survivingParticles);return ResourceLoadResult<DeletionValidation>{result,result->bytes,result->fileStateFingerprint,result->dependencies};
                },options);
                pendingDeletion_=std::move(pending);status_="Preparing asset deletion...";return false;
            }
            if(pendingDeletion_->validation.state()==ResourceState::Failed)throw std::runtime_error(pendingDeletion_->validation.diagnostic());
            const auto validation=pendingDeletion_->validation.snapshot();
            if(!validation){status_="Preparing asset deletion: "+std::to_string(pendingDeletion_->progress->completed.load())+" / "+std::to_string(pendingDeletion_->progress->total)+" assets validated.";return false;}
            if(!deletionValidationCurrent(*validation))throw std::runtime_error("Resource files changed while deletion was validating. Review the deletion again.");
            nextModels=validation->models;
        }else nextModels=validateAssetDeletion(projectRoot_,surviving,nextReferences,{}, {},survivingParticles)->models;
        std::vector<AssetFileChange> changes;
        auto preparedMaterials=materials_;auto preparedAuthoredMaterials=authoredMaterials_;auto preparedParticles=particles_;
        auto preparedMaterialRevision=nextMaterialRevision_,preparedParticleRevision=nextParticleRevision_;
        for(const auto& entry:catalog_)if(entry.kind==AssetKind::Material&&!deletedIds.contains(entry.id)){
            auto material=loadMaterialAsset(projectRoot_,entry.id);bool changed=false;const bool sprite=particleMaterialCompatible(material);
            const std::array<ModelTexture*,5> slots{{&material.surface.baseColorTexture,&material.surface.metallicRoughnessTexture,&material.surface.normalTexture,&material.surface.occlusionTexture,&material.surface.emissiveTexture}};
            for(size_t slot=0;slot<slots.size();++slot)if(!slots[slot]->image.empty()&&deletedIds.contains(references_.resolve(slots[slot]->image.id))){
                const auto replacement=sprite?nextReferences.resolveParticleTexture(slots[slot]->image.id):nextReferences.resolveTexture(slots[slot]->image.id,slot==2);
                if(replacement!=slots[slot]->image.id){slots[slot]->image=AssetRef<DecodedImage>{replacement};changed=true;}}
            if(changed){changes.push_back({entry.id,serializeMaterialAsset(material)});
                if(entry.id!=materialDocumentId_){auto definition=std::make_shared<MaterialAsset>(std::move(material));preparedAuthoredMaterials[entry.id]={definition,preparedMaterialRevision++,{}};
                    for(auto& cached:preparedMaterials)if(cached->id==entry.id)cached=definition;}}
        }
        for(const auto& entry:survivingParticles){auto effect=loadParticleAsset(projectRoot_,entry.id);
            if(!effect.emitter.material.empty()&&deletedIds.contains(references_.resolve(effect.emitter.material.id))){const auto replacement=nextReferences.resolveParticleMaterial(effect.emitter.material.id);
                if(replacement!=effect.emitter.material.id){effect.emitter.material=AssetRef<MaterialAsset>{replacement};changes.push_back({entry.id,serializeParticleAsset(effect)});
                    if(entry.id!=particleDocumentId_)preparedParticles[entry.id]={std::make_shared<ParticleEffectAsset>(std::move(effect)),preparedParticleRevision++,{}};}}
        }
        for(auto scene:savedScenes(projectRoot_))if(!deletedIds.contains(scene.id)){
            const auto removed=removeDeletedModels(scene,references_,nextReferences,deletedModels);
            const auto replaced=replaceDeletedParticles(scene,references_,nextReferences,deletedParticles);
            const auto materials=replaceDeletedModelMaterials(scene,references_,nextReferences,deletedMaterials,projectRoot_);
            if(removed||replaced||materials)changes.push_back({scene.id,serializeSceneSnapshot(scene)});
        }
        SceneSnapshot activeCandidate;bool activeChanged=false,clearHistory=false,activeDeleted=active&&deletedIds.contains(active->assetId());const bool wasDirty=active&&active->dirty();
        if(active&&!activeDeleted){activeCandidate.records=active->scene().records();activeCandidate.properties=active->scene().properties();
            activeChanged=removeDeletedModels(activeCandidate,references_,nextReferences,deletedModels)!=0;
            activeChanged=replaceDeletedParticles(activeCandidate,references_,nextReferences,deletedParticles)!=0||activeChanged;
            activeChanged=replaceDeletedModelMaterials(activeCandidate,references_,nextReferences,deletedMaterials,projectRoot_)!=0||activeChanged;
            for(const auto& id:active->historicalMaterialReferences())if(deletedMaterials.contains(references_.resolveMaterial(id)))clearHistory=true;
            for(const auto& id:active->historicalModelReferences())if(deletedModels.contains(references_.resolve(id))&&nextReferences.resolve(id).empty())clearHistory=true;
            for(const auto& id:active->historicalParticleReferences())if(deletedParticles.contains(references_.resolveParticle(id)))clearHistory=true;}
        for(const auto& entry:deleted){if(entry.kind==AssetKind::Model){const auto package=checkedManagedAssetPath(projectRoot_,entry.id).parent_path();
                for(const auto& file:std::filesystem::recursive_directory_iterator(package))if(file.is_regular_file()){
                    const auto relative=file.path().lexically_relative(package);if(relative.begin()!=relative.end()&&*relative.begin()=="images")continue;
                    changes.push_back({utf8(file.path().lexically_relative(projectRoot_)),std::nullopt});}}
            else changes.push_back({entry.id,std::nullopt});}
        // Delete may explicitly include an image also found in a model's package list; unique file targets only.
        std::vector<AssetFileChange> unique;std::unordered_set<std::string> files;for(auto& change:changes)if(files.insert(change.id).second)unique.push_back(std::move(change));
        unique.push_back({"assets/references.json",nextReferences.serialize()});auto browserIds=plan.assetIds;browserIds.insert(browserIds.end(),plan.folderIds.begin(),plan.folderIds.end());
        unique.push_back({"assets/browser.json",browserCatalog().serializeWithout(browserIds)});
        auto preparedBrowser=browserCatalog().prepareErase(browserIds);auto preparedCatalog=catalog_;
        std::erase_if(preparedCatalog,[&](const auto& entry){return deletedIds.contains(entry.id);});
        std::erase_if(preparedMaterials,[&](const auto& entry){return deletedIds.contains(entry->id);});
        std::erase_if(preparedParticles,[&](const auto& entry){return deletedIds.contains(entry.first);});
        std::erase_if(preparedAuthoredMaterials,[&](const auto& entry){return deletedIds.contains(entry.first);});
        const auto retiredCount=std::count_if(assets_.begin(),assets_.end(),[&](const auto& asset){return deletedIds.contains(asset.id);});
        retiredTextures_.reserve(retiredTextures_.size()+static_cast<size_t>(retiredCount));
        std::optional<SceneDocument::PreparedExternalChange> preparedScene;
        if(activeDeleted) preparedScene.emplace(active->prepareNewSceneAfterAssetDeletion());
        else if(active && (activeChanged||clearHistory)) {
            auto savedFingerprint=active->loadedFingerprint();
            for(const auto& change:unique) if(change.id==active->assetId() && change.contents) savedFingerprint=sceneTextFingerprint(*change.contents);
            preparedScene.emplace(active->prepareExternalAssetChanges(activeCandidate.records,activeCandidate.properties,"Delete project assets",wasDirty,savedFingerprint));
        }
        if(preparedScene && !active->canCommitExternalAssetChanges(*preparedScene)) throw std::runtime_error("The active scene changed while deletion was preparing.");
        commitAssetTransaction(projectRoot_,unique);
        durable=true;
        // All data/registry/catalog allocations were prepared above. Install the
        // authored document immediately; cache maintenance cannot roll it back.
        if(preparedScene) { const auto committed=active->commitExternalAssetChanges(std::move(*preparedScene));if(!committed)std::terminate(); }
        references_=std::move(nextReferences);models_=std::move(nextModels);
        browserCatalog_=std::move(preparedBrowser);catalog_=std::move(preparedCatalog);materials_=std::move(preparedMaterials);particles_=std::move(preparedParticles);
        authoredMaterials_=std::move(preparedAuthoredMaterials);nextMaterialRevision_=preparedMaterialRevision;nextParticleRevision_=preparedParticleRevision;
        std::erase_if(preparedParticleMaterials_,[&](const auto& entry){return deletedIds.contains(entry.first);});++catalogRevision_;browserRevision_=catalogRevision_;
        for(auto it=assets_.begin();it!=assets_.end();)if(deletedIds.contains(it->id)){retiredTextures_.push_back(std::move(*it));it=assets_.erase(it);}else ++it;
        browserSelection_.clear();
        if(resources_) {
            for(const auto& id:deletedIds) { resources_->forget(id);textureStreamer_->invalidate(id);modelResources_.erase(id);materialResources_.erase(id); }
            // Runtime model records hold compact metadata; validation arrays above are temporary.
            models_.clear();registeredModelRevisions_.clear();
        }
        for(const auto& change:unique) noteAssetWrite(change.id);
        cancelDeletion();status_="Deleted "+std::to_string(plan.assetIds.size())+" asset(s).";return true;
    }catch(const std::exception& e){cancelDeletion();status_=(durable?"Assets were deleted; resource cache refresh needs attention: ":"Could not delete assets: ")+std::string(e.what());return durable;}
}

} // namespace crimson
