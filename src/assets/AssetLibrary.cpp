#include "AssetLibrary.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "scene/EnvironmentSerialization.h"
#include "BuiltinGeometry.h"
#include "scene/SceneDocument.h"
#include "math/Json.h"
#include "AsyncImports.h"
#include "DeletionPreparation.h"
#include "AtomicFile.h"
#include "resources/TextureStreamer.h"
#include "resources/ImageDecoder.h"
#include "resources/StreamingModel.h"
#include "resources/CacheIO.h"
#include "MaterialGraphReferences.h"
#include "GrungeGenerator.h"
#include <crimson/renderer/forward/MaterialGraphImageBake.h>
#include <crimson/renderer/forward/MaterialProcedural.h>
#include "ContentStorage.h"
#include "QuaderSceneBundle.h"
#include "ModelMaterialSlots.h"
#include "resources/AssetId.h"

#include <SDL3/SDL.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace crimson {
namespace {

float imagePreviewHalf(const std::uint8_t* bytes) {
    const auto bits=std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8);
    const auto exponent=(bits>>10)&31u,mantissa=bits&1023u;
    if(exponent==31)throw std::runtime_error("Node image source contains a nonfinite half value.");
    if(!exponent)return std::ldexp(float(mantissa),-24)*((bits&0x8000u)?-1.f:1.f);
    return std::bit_cast<float>(((bits&0x8000u)<<16)|((exponent+112u)<<23)|(mantissa<<13));
}
FloatImage nodePreviewMipImage(const DecodedImage& source,unsigned edge,std::stop_token stop) {
    if(source.format!=ImagePixelFormat::RGBA16F||!source.width||!source.height||
        source.rgba.size()!=std::size_t(source.width)*source.height*8)
        throw std::runtime_error("Node image source has an invalid floating mip payload.");
    // Decode only the selected mip's participating texels. Never expand a full
    // authored 4K source into another RGBA32F image just to display a thumbnail.
    using Tap=std::pair<unsigned,double>;
    const auto axis=[edge](unsigned size) {
        std::vector<std::vector<Tap>> result(edge);
        for(unsigned x=0;x<edge;++x) {
            auto& taps=result[x];
            if(size>=edge) {
                const double first=double(x)*size/edge,last=double(x+1)*size/edge;
                taps.reserve(static_cast<std::size_t>(std::ceil(last)-std::floor(first)));
                for(auto i=unsigned(std::floor(first));i<std::min(size,unsigned(std::ceil(last)));++i)
                    taps.emplace_back(i,(std::min(last,double(i+1))-std::max(first,double(i)))/(last-first));
            } else {
                const double p=std::clamp((double(x)+.5)*size/edge-.5,0.,double(size-1));
                const auto i=unsigned(p);const auto fraction=p-i;
                taps.emplace_back(i,1-fraction);
                if(fraction>0)taps.emplace_back(std::min(i+1,size-1),fraction);
            }
        }
        return result;
    };
    const auto xs=axis(source.width),ys=axis(source.height);
    FloatImage result{edge,edge,{}};result.rgba.resize(std::size_t(edge)*edge*4);
    for(unsigned y=0;y<edge;++y) {
        if(stop.stop_requested())throw std::runtime_error("Node image preview canceled.");
        for(unsigned x=0;x<edge;++x) {
            std::array<double,4> sum{};
            for(const auto& [sy,wy]:ys[y])for(const auto& [sx,wx]:xs[x]) {
                const auto* bytes=source.rgba.data()+(std::size_t(sy)*source.width+sx)*8;
                for(unsigned c=0;c<4;++c)sum[c]+=imagePreviewHalf(bytes+c*2)*wx*wy;
            }
            const auto first=(std::size_t(y)*edge+x)*4;
            for(unsigned c=0;c<4;++c)result.rgba[first+c]=float(sum[c]);
        }
    }
    return result;
}
void encodeNodeImageDisplay(FloatImage& image,MaterialValueType type,std::stop_token stop) {
    const auto encode=[](float value){value=std::clamp(value,0.f,1.f);return value<=.0031308f?12.92f*value:1.055f*std::pow(value,1.f/2.4f)-.055f;};
    // A numeric image is inspected as data: .5 is display gray .5. Color
    // outputs retain the linear-color transfer, on this display copy only.
    const bool numeric=type==MaterialValueType::Scalar||type==MaterialValueType::Vector2;
    for(unsigned y=0;y<image.height;++y) {
        if(stop.stop_requested())throw std::runtime_error("Node image preview canceled.");
        for(unsigned x=0;x<image.width;++x) {
            const auto i=(std::size_t(y)*image.width+x)*4;
            const float alpha=type==MaterialValueType::Color4?std::clamp(image.rgba[i+3],0.f,1.f):1.f;
            const float checker=((x/8+y/8)&1)?.32f:.18f;
            for(unsigned c=0;c<3;++c) {
                const float value=image.rgba[i+c]*alpha+checker*(1-alpha);
                image.rgba[i+c]=numeric?std::clamp(value,0.f,1.f):encode(value);
            }
            image.rgba[i+3]=1;
        }
    }
}

std::size_t modelCpuBytes(const ModelAsset& model) {
    const auto vectorBytes=[]<class T>(const std::vector<T>& values){return values.capacity()*sizeof(T);};
    std::size_t bytes=sizeof(ModelAsset)+model.id.size()+model.name.size()+model.format.size()+
        vectorBytes(model.primitives)+vectorBytes(model.meshes)+vectorBytes(model.nodes)+vectorBytes(model.roots)+
        vectorBytes(model.draws)+vectorBytes(model.materials)+vectorBytes(model.materialIds)+vectorBytes(model.images)+
        vectorBytes(model.warnings)+vectorBytes(model.skins)+vectorBytes(model.animations);
    for(const auto& primitive:model.primitives) {
        bytes+=vectorBytes(primitive.vertices)+vectorBytes(primitive.indices)+vectorBytes(primitive.skinWeights)+
            vectorBytes(primitive.morphTargets)+vectorBytes(primitive.skinJointBounds);
        for(const auto& target:primitive.morphTargets)
            bytes+=vectorBytes(target.positions)+vectorBytes(target.normals)+vectorBytes(target.tangents);
    }
    for(const auto& mesh:model.meshes)bytes+=mesh.name.size()+vectorBytes(mesh.primitives)+vectorBytes(mesh.weights);
    for(const auto& node:model.nodes)bytes+=node.name.size()+vectorBytes(node.children)+vectorBytes(node.weights);
    for(const auto& skin:model.skins)bytes+=skin.name.size()+vectorBytes(skin.joints)+vectorBytes(skin.inverseBindMatrices);
    for(const auto& clip:model.animations) {
        bytes+=clip.name.size()+vectorBytes(clip.channels);
        for(const auto& channel:clip.channels)bytes+=vectorBytes(channel.times)+vectorBytes(channel.values);
    }
    for(const auto& material:model.materials) {
        bytes+=material.name.size();
        for(const auto* slot:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture})
            bytes+=slot->image.id.size();
    }
    for(const auto& id:model.materialIds)bytes+=id.size();
    for(const auto& id:model.images)bytes+=id.size();
    for(const auto& warning:model.warnings)bytes+=warning.size();
    if(model.stream) {
        bytes+=sizeof(streaming::ModelStream)+model.stream->id.size()+vectorBytes(model.stream->primitives);
        for(const auto& primitive:model.stream->primitives) {
            bytes+=vectorBytes(primitive.levels);
            for(const auto& level:primitive.levels)bytes+=level.file.native().size()*sizeof(std::filesystem::path::value_type);
        }
    }
    return bytes;
}

void releaseModelGeometry(ModelPrimitive& primitive) {
    primitive.sourceVertexCount=std::max<std::uint64_t>(primitive.sourceVertexCount,primitive.vertices.size());
    primitive.sourceIndexCount=std::max<std::uint64_t>(primitive.sourceIndexCount,primitive.indices.size());
    decltype(primitive.vertices){}.swap(primitive.vertices);
    decltype(primitive.indices){}.swap(primitive.indices);
    decltype(primitive.skinWeights){}.swap(primitive.skinWeights);
    for(auto& target:primitive.morphTargets) {
        decltype(target.positions){}.swap(target.positions);
        decltype(target.normals){}.swap(target.normals);
        decltype(target.tangents){}.swap(target.tangents);
    }
}

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

std::filesystem::path initializedProjectRoot(std::filesystem::path root) {
    root=std::filesystem::absolute(std::move(root)).lexically_normal();initializeContentStorage(root);return root;
}
void discoverContentFiles(const std::filesystem::path& root) {
    std::unordered_set<std::string> indexed;
    for(const auto& entry:storedAssetPaths(root))indexed.insert(utf8(std::filesystem::weakly_canonical(entry.file)));
    const auto add=[&](const std::string& id,const std::filesystem::path& file) {
        if(indexed.contains(utf8(std::filesystem::weakly_canonical(file))))return;
        for(const auto& entry:storedAssetPaths(root))if(entry.id==canonicalAssetId(id)&&entry.file!=file)
            throw std::runtime_error("A copied content file uses an existing asset identity: "+id);
        registerAssetPath(root,id,file);indexed.insert(utf8(std::filesystem::weakly_canonical(file)));
    };
    const auto content=root/"Content";if(!std::filesystem::is_directory(content))return;
    for(auto iterator=std::filesystem::recursive_directory_iterator(content);iterator!=std::filesystem::recursive_directory_iterator{};++iterator) {
        const auto& entry=*iterator;
        if(entry.is_directory()&&std::filesystem::is_regular_file(entry.path()/"manifest.json")) {
            const auto manifest=entry.path()/"manifest.json";auto id=assetIdAtPath(root,manifest);
            if(id.empty())id=canonicalAssetId("assets/models/"+utf8(entry.path().filename())+"/manifest.json");
            add(id,manifest);
            std::ifstream input(manifest);const auto data=nlohmann::json::parse(input);
            for(const auto& dependency:data.at("dependencies"))if(dependency.at("kind")=="image") {
                const auto member=fromUtf8(dependency.at("path").get<std::string>());
                if(member.empty()||member.has_root_path())throw std::runtime_error("Model image dependency is not relative.");
                for(const auto& part:member)if(part==".."||part==".")throw std::runtime_error("Model image dependency escapes its package.");
                const auto image=checkedContentPath(root,entry.path()/member),relative=image.lexically_relative(std::filesystem::weakly_canonical(entry.path()));
                if(relative.empty()||relative.has_root_path()||*relative.begin()=="..")throw std::runtime_error("Model image dependency escapes its package.");
                if(!std::filesystem::is_regular_file(image))throw std::runtime_error("Model image dependency is missing.");
                if(!textureFile(image))continue;
                const auto imageId=derivedAssetId(id,"image:"+dependency.at("path").get<std::string>());
                bool known=false;for(const auto& stored:storedAssetPaths(root))if(stored.id==imageId){known=true;break;}
                if(!known)add(imageId,image);
            }
            iterator.disable_recursion_pending();continue;
        }
        if(!entry.is_regular_file()||indexed.contains(utf8(std::filesystem::weakly_canonical(entry.path()))))continue;
        const auto extension=entry.path().extension();
        if(extension==".crimson-material"||extension==".crimson-particle"||extension==".crimson-agent-profile"||extension==".crimson-effect"||extension==".crimson-animation"||extension==".crimson-scene") {
            std::ifstream input(entry.path());const auto data=nlohmann::json::parse(input);
            auto id=data.value("id",std::string{});
            if(id.empty())id="assets/scenes/"+utf8(entry.path().filename());
            add(id,entry.path());
        } else if(textureFile(entry.path())) {
            add(newAssetId(),entry.path());
        }
    }
}
} // namespace

struct AssetLibrary::ImportQueue {
    struct Batch { std::vector<std::string> paths; std::string destination; };
    std::mutex mutex;
    std::vector<Batch> batches;
    std::string error;
};

AssetLibrary::AssetLibrary(std::filesystem::path root,ResourceManager* resources)
    : imports_(std::make_shared<ImportQueue>()), projectRoot_(initializedProjectRoot(std::move(root))),resources_(resources), references_(projectRoot_),browserCatalog_(projectRoot_) {
    if(resources_) { textureStreamer_=std::make_unique<TextureStreamer>(*resources_,projectRoot_);asyncImports_=std::make_unique<AsyncImports>(projectRoot_,*resources_); }
    if(!references_.status().empty()) status_=references_.status();
    try{discoverContentFiles(projectRoot_);}catch(const std::exception& error){status_+=std::string(error.what())+'\n';}
    ensureEngineMaterialAssets(projectRoot_);
    ensureEngineNavigationAgentProfile(projectRoot_);
    ensureEngineGameplayEffects(projectRoot_);
    refreshQuaderModelCatalog();
    std::vector<std::filesystem::path> files;
    for(const auto& entry:storedAssetPaths(projectRoot_))if(std::filesystem::is_regular_file(entry.file)) {
        if(textureFile(entry.file)&&!references_.deleted(entry.id)) {
            try{addTexture(makeAsset(entry.file));}catch(const std::exception& error){status_+=std::string(error.what())+'\n';}
        }
        else if(entry.file.filename()=="manifest.json")files.push_back(entry.file);
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        try {
            const auto id=assetIdAtPath(projectRoot_,file);
            if(references_.deleted(id)) continue;
            if(resources_) {
                if(inactiveQuaderModels_.contains(id))continue;
                std::ifstream input(file); const auto manifest=nlohmann::json::parse(input);
                catalog_.push_back({id,manifest.value("name",utf8(file.parent_path().filename())),AssetKind::Model}); ++catalogRevision_;
                // Quader packages contain static geometry. Ordinary imported
                // packages receive one worker scan; warm native clips avoid
                // geometry decoding and the main thread only publishes files.
                if(!quaderBundleSource(projectRoot_,id)) {
                    const auto packageRoot=projectRoot_;std::unordered_set<std::string> known;
                    for(const auto& entry:storedAssetPaths(projectRoot_))if(entry.file.extension()==".crimson-animation")known.insert(entry.id);
                    const auto policy=references_;const auto resolvers=modelResolvers();
                    ResourceRequestOptions animationOptions;animationOptions.variant="animation-catalog";animationOptions.priority=ResourcePriority::Background;
                    animationOptions.dependencies={file,file.parent_path()/"model.gltf"};
                    animationCatalogResources_[id]=resources_->request<ModelAsset>(AssetRef<ModelAsset>{id},[file,packageRoot,known,policy,resolvers,id](std::stop_token token) {
                        if(token.stop_requested())throw std::runtime_error("Animation catalog scan canceled.");
                        auto empty=std::make_shared<ModelAsset>();empty->id=id;
                        std::ifstream input(file.parent_path()/"model.gltf");const auto document=nlohmann::json::parse(input);
                        const auto clips=document.find("animations");bool missing=false;
                        if(clips!=document.end()&&clips->is_array())for(std::size_t clip=0;clip<clips->size();++clip) {
                            const auto clipId=importedAnimationId(id,static_cast<std::uint32_t>(clip));
                            if(!policy.deleted(clipId)&&!known.contains(clipId)){missing=true;break;}
                        }
                        if(!missing)return ResourceLoadResult<ModelAsset>{empty,sizeof(ModelAsset),0,{}};
                        auto data=streaming::loadModelMetadataCache(file,packageRoot);
                        if(!data){data=loadModelPackage(file,packageRoot,resolvers);for(auto& primitive:data->primitives)releaseModelGeometry(primitive);}
                        if(token.stop_requested())throw std::runtime_error("Animation catalog scan canceled.");
                        return ResourceLoadResult<ModelAsset>{data,modelCpuBytes(*data),0,{}};
                    },std::move(animationOptions));
                }
                // Explicit shared-material assignments cannot create imported
                // sidecars. Native material discovery below already covers them.
                const auto explicitMaterials=manifest.find("materialAssets");
                if(explicitMaterials!=manifest.end()&&explicitMaterials->is_array()) {
                    bool needsPromotion=false;
                    for(std::size_t i=0;i<explicitMaterials->size();++i)
                        if(!explicitMaterials->at(i).is_string()||canonicalAssetId(explicitMaterials->at(i).get<std::string>())==importedMaterialId(id,i)) {needsPromotion=true;break;}
                    if(!needsPromotion)continue;
                }
                const auto packageRoot=projectRoot_;const auto resolvers=modelResolvers();
                ResourceRequestOptions metadataOptions;metadataOptions.variant="material-catalog";metadataOptions.priority=ResourcePriority::Background;
                metadataOptions.dependencies={file,file.parent_path()/"model.gltf"};
                if(const auto bundle=quaderBundleSource(projectRoot_,id))metadataOptions.dependencies.push_back(*bundle);
                materialCatalogResources_[id]=resources_->request<ModelAsset>(AssetRef<ModelAsset>{id},[file,packageRoot,resolvers](std::stop_token token){
                    if(token.stop_requested())throw std::runtime_error("Material catalog scan canceled.");
                    auto data=loadModelMaterialMetadata(file,packageRoot,resolvers);
                    return ResourceLoadResult<ModelAsset>{data,modelCpuBytes(*data),0,{}};
                },std::move(metadataOptions));
            } else registerModel(loadModelPackage(file, projectRoot_,modelResolvers()),false);
        }
        catch (const std::exception& error) {
            status_ += "Model load failed (" + utf8(file.parent_path().filename()) + "): " + error.what() + '\n';
        }
    }
    refreshMaterials();
    refreshScenes();
    refreshParticles();
    refreshNavigationAgentProfiles();
    refreshGameplayEffects();
    refreshAnimations();
    storageRevision_=contentStorageRevision(projectRoot_);
    for(const auto& entry:storedAssetPaths(projectRoot_))storageLocations_.emplace(entry.id,entry.file);
}

AssetLibrary::~AssetLibrary() {
    preparedParticleMaterials_.clear();
    asyncImports_.reset(); modelResources_.clear(); materialResources_.clear();materialCatalogResources_.clear();animationCatalogResources_.clear();
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
    std::unordered_set<std::string> present;
    for(const auto& file:storedAssetPaths(projectRoot_)) {
        if(!std::filesystem::is_regular_file(file.file) || file.file.extension()!=".crimson-particle") continue;
        const auto& id=file.id;
        if(references_.deleted(id)) continue;
        present.insert(id);
        std::string name=previous.contains(id)?previous.at(id):utf8(file.file.stem());
        if(id==particleDocumentId_ && particles_.contains(id)) name=particles_.at(id).definition->name;
        else try {
            auto effect=std::make_shared<ParticleEffectAsset>(loadParticleAsset(projectRoot_,id));name=effect->name;publishParticle(std::move(effect));
        } catch(const std::exception& error) { particles_[id].diagnostic=error.what(); }
        catalog_.push_back({id,std::move(name),AssetKind::Particle});
    }
    std::erase_if(particles_,[&](const auto& entry){return !present.contains(entry.first) && entry.first!=particleDocumentId_;});
    ++catalogRevision_;
}
void AssetLibrary::refreshNavigationAgentProfiles() {
    std::erase_if(catalog_,[](const auto& entry){return entry.kind==AssetKind::NavigationAgentProfile;});
    navigationProfiles_[DefaultNavigationAgentProfileId]={defaultNavigationAgentProfile(),1,{}};
    std::unordered_set<std::string> present{DefaultNavigationAgentProfileId};
    for(const auto& file:storedAssetPaths(projectRoot_))if(file.file.extension()==".crimson-agent-profile"&&!references_.deleted(file.id)) {
        present.insert(file.id);auto& entry=navigationProfiles_[file.id];
        if(file.id!=navigationProfileDocumentId_)try {
            auto profile=std::make_shared<const NavigationAgentProfile>(loadNavigationAgentProfile(projectRoot_,file.id));
            if(!entry.definition||*entry.definition!=*profile){entry.definition=std::move(profile);entry.revision=nextNavigationProfileRevision_++;}
            entry.diagnostic.clear();
        } catch(const std::exception& error){entry.diagnostic=error.what();}
        const auto name=entry.definition?entry.definition->name:utf8(file.file.stem());
        catalog_.push_back({file.id,name,AssetKind::NavigationAgentProfile});
    }
    if(std::none_of(catalog_.begin(),catalog_.end(),[](const auto& entry){return entry.id==DefaultNavigationAgentProfileId;}))
        catalog_.push_back({DefaultNavigationAgentProfileId,"Default Agent",AssetKind::NavigationAgentProfile});
    std::erase_if(navigationProfiles_,[&](const auto& entry){return !present.contains(entry.first)&&entry.first!=navigationProfileDocumentId_;});
    ++catalogRevision_;
}
std::shared_ptr<const NavigationAgentProfile> AssetLibrary::findNavigationAgentProfile(const std::string& id) const {
    try {const auto resolved=references_.resolveNavigationAgentProfile(id);const auto found=navigationProfiles_.find(resolved);
        return found==navigationProfiles_.end()?nullptr:found->second.definition;
    }catch(const std::exception&){return nullptr;}
}
std::uint64_t AssetLibrary::navigationAgentProfileRevision(const std::string& id) const {
    try {const auto found=navigationProfiles_.find(references_.resolveNavigationAgentProfile(id));return found==navigationProfiles_.end()?0:found->second.revision;}
    catch(const std::exception&){return 0;}
}
std::string AssetLibrary::navigationAgentProfileDiagnostic(const std::string& id) const {
    try {const auto found=navigationProfiles_.find(references_.resolveNavigationAgentProfile(id));
        return found==navigationProfiles_.end()?"Navigation Agent Profile is unavailable: "+id:found->second.diagnostic;
    }catch(const std::exception& error){return error.what();}
}
void AssetLibrary::publishNavigationAgentProfile(std::shared_ptr<const NavigationAgentProfile> profile) {
    if(!profile)return;validateNavigationAgentProfile(*profile);
    if(isEngineAssetId(profile->id)||references_.deleted(profile->id))throw std::runtime_error("Protected or deleted Navigation Agent Profile cannot be published.");
    auto& entry=navigationProfiles_[profile->id];if(!entry.definition||*entry.definition!=*profile){entry.definition=std::move(profile);entry.revision=nextNavigationProfileRevision_++;}
    entry.diagnostic.clear();++catalogRevision_;
}
std::shared_ptr<const NavigationAgentProfile> AssetLibrary::createNavigationAgentProfile(const std::string& name) {
    auto profile=std::make_shared<const NavigationAgentProfile>(crimson::createNavigationAgentProfile(projectRoot_,name));
    publishNavigationAgentProfile(profile);noteAssetWrite(profile->id);refreshNavigationAgentProfiles();select(profile->id);return profile;
}
std::shared_ptr<const NavigationAgentProfile> AssetLibrary::copyNavigationAgentProfile(const std::string& id,const std::string& name) {
    const auto original=findNavigationAgentProfile(id);if(!original)throw std::runtime_error("Navigation Agent Profile is unavailable.");
    auto profile=std::make_shared<const NavigationAgentProfile>(crimson::copyNavigationAgentProfile(projectRoot_,*original,name));
    publishNavigationAgentProfile(profile);noteAssetWrite(profile->id);refreshNavigationAgentProfiles();select(profile->id);return profile;
}
void AssetLibrary::refreshGameplayEffects() {
    std::erase_if(catalog_,[](const auto& entry){return entry.kind==AssetKind::GameplayEffect;});
    std::unordered_set<std::string> present;
    for(const auto& builtin:{playerStartingEffect(),enemyStartingEffect()}) { gameplayEffects_[builtin->id]={builtin,1,{}};present.insert(builtin->id); }
    for(const auto& file:storedAssetPaths(projectRoot_))if(file.file.extension()==".crimson-effect"&&!references_.deleted(file.id)) {
        present.insert(file.id);auto& entry=gameplayEffects_[file.id];
        if(file.id!=gameplayEffectDocumentId_)try {
            auto profile=std::make_shared<const GameplayEffectAsset>(loadGameplayEffectAsset(projectRoot_,file.id));
            if(!entry.definition||*entry.definition!=*profile){entry.definition=std::move(profile);entry.revision=nextGameplayEffectRevision_++;}
            entry.diagnostic.clear();
        } catch(const std::exception& error){entry.diagnostic=error.what();}
        const auto name=entry.definition?entry.definition->name:utf8(file.file.stem());
        catalog_.push_back({file.id,name,AssetKind::GameplayEffect});
    }
    for(const auto& builtin:{playerStartingEffect(),enemyStartingEffect()})if(std::none_of(catalog_.begin(),catalog_.end(),[&](const auto& entry){return entry.id==builtin->id;}))
        catalog_.push_back({builtin->id,builtin->name,AssetKind::GameplayEffect});
    std::erase_if(gameplayEffects_,[&](const auto& entry){return !present.contains(entry.first)&&entry.first!=gameplayEffectDocumentId_;});
    ++catalogRevision_;
}
std::shared_ptr<const GameplayEffectAsset> AssetLibrary::findGameplayEffect(const std::string& id) const {
    try {const auto resolved=references_.resolveGameplayEffect(id);const auto found=gameplayEffects_.find(resolved);
        return found==gameplayEffects_.end()||!found->second.diagnostic.empty()?nullptr:found->second.definition;
    }catch(const std::exception&){return nullptr;}
}
std::uint64_t AssetLibrary::gameplayEffectRevision(const std::string& id) const {
    try {const auto found=gameplayEffects_.find(references_.resolveGameplayEffect(id));return found==gameplayEffects_.end()?0:found->second.revision;}
    catch(const std::exception&){return 0;}
}
std::string AssetLibrary::gameplayEffectDiagnostic(const std::string& id) const {
    try {const auto found=gameplayEffects_.find(references_.resolveGameplayEffect(id));
        return found==gameplayEffects_.end()?"Gameplay Effect is unavailable: "+id:found->second.diagnostic;
    }catch(const std::exception& error){return error.what();}
}
void AssetLibrary::publishGameplayEffect(std::shared_ptr<const GameplayEffectAsset> profile) {
    if(!profile)return;validateGameplayEffectAsset(*profile);
    if(isEngineAssetId(profile->id)||references_.deleted(profile->id))throw std::runtime_error("Protected or deleted Gameplay Effect cannot be published.");
    auto& entry=gameplayEffects_[profile->id];if(!entry.definition||*entry.definition!=*profile){entry.definition=std::move(profile);entry.revision=nextGameplayEffectRevision_++;}
    entry.diagnostic.clear();++catalogRevision_;
}
std::shared_ptr<const GameplayEffectAsset> AssetLibrary::createGameplayEffect(const std::string& name) {
    auto profile=std::make_shared<const GameplayEffectAsset>(crimson::createGameplayEffectAsset(projectRoot_,name));
    publishGameplayEffect(profile);noteAssetWrite(profile->id);refreshGameplayEffects();select(profile->id);return profile;
}
std::shared_ptr<const GameplayEffectAsset> AssetLibrary::copyGameplayEffect(const std::string& id,const std::string& name) {
    const auto original=findGameplayEffect(id);if(!original)throw std::runtime_error("Gameplay Effect is unavailable.");
    auto profile=std::make_shared<const GameplayEffectAsset>(crimson::copyGameplayEffectAsset(projectRoot_,*original,name));
    publishGameplayEffect(profile);noteAssetWrite(profile->id);refreshGameplayEffects();select(profile->id);return profile;
}
void AssetLibrary::refreshAnimations() {
    std::erase_if(catalog_,[](const auto& entry){return entry.kind==AssetKind::Animation;});
    std::unordered_set<std::string> present;
    for(const auto& file:storedAssetPaths(projectRoot_))if(file.file.extension()==".crimson-animation"&&!references_.deleted(file.id)) {
        present.insert(file.id);auto& entry=animations_[file.id];
        if(file.id!=animationDocumentId_||!entry.definition)try {
            auto definition=std::make_shared<const AnimationAsset>(loadAnimationAsset(projectRoot_,file.id));
            if(!entry.definition||*entry.definition!=*definition){entry.definition=std::move(definition);entry.revision=nextAnimationRevision_++;}
            entry.diagnostic.clear();
        }catch(const std::exception& error){entry.diagnostic=error.what();}
        catalog_.push_back({file.id,entry.definition?entry.definition->name:utf8(file.file.stem()),AssetKind::Animation});
    }
    std::erase_if(animations_,[&](const auto& entry){return !present.contains(entry.first)&&entry.first!=animationDocumentId_;});
    ++catalogRevision_;
}
std::shared_ptr<const AnimationAsset> AssetLibrary::findAnimation(const std::string& id) const {
    try {const auto found=animations_.find(references_.resolveAnimation(id));return found==animations_.end()?nullptr:found->second.definition;}
    catch(...){return {};}
}
std::uint64_t AssetLibrary::animationRevision(const std::string& id) const {
    try {const auto found=animations_.find(references_.resolveAnimation(id));return found==animations_.end()?0:found->second.revision;}
    catch(...){return 0;}
}
std::string AssetLibrary::animationDiagnostic(const std::string& id) const {
    try {const auto found=animations_.find(references_.resolveAnimation(id));return found==animations_.end()?"Animation unavailable: "+id:found->second.diagnostic;}
    catch(const std::exception& error){return error.what();}
}
void AssetLibrary::publishAnimation(std::shared_ptr<const AnimationAsset> animation) {
    if(!animation)return;validateAnimationAsset(*animation);
    if(isEngineAssetId(animation->id)||references_.deleted(animation->id))throw std::runtime_error("Protected or deleted Animation cannot be published.");
    auto& entry=animations_[animation->id];
    if(!entry.definition||*entry.definition!=*animation){entry.definition=std::move(animation);entry.revision=nextAnimationRevision_++;}
    entry.diagnostic.clear();
    const auto catalog=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& value){return value.id==entry.definition->id;});
    if(catalog==catalog_.end())catalog_.push_back({entry.definition->id,entry.definition->name,AssetKind::Animation});else catalog->name=entry.definition->name;
    ++catalogRevision_;
}
std::shared_ptr<const AnimationAsset> AssetLibrary::copyAnimation(const std::string& id,const std::string& name) {
    const auto original=findAnimation(id);if(!original)throw std::runtime_error("Animation is unavailable.");
    auto definition=std::make_shared<const AnimationAsset>(crimson::copyAnimationAsset(projectRoot_,*original,name));
    publishAnimation(definition);noteAssetWrite(definition->id);refreshAnimations();select(definition->id);return definition;
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
    auto normalized=std::make_shared<ParticleEffectAsset>(*effect);normalized->id=canonicalAssetId(normalized->id);normalized->emitter.material.id=canonicalAssetId(normalized->emitter.material.id);effect=std::move(normalized);
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
    const auto canonical=canonicalAssetId(id);
    const auto found = std::find_if(assets_.begin(), assets_.end(), [&](const auto& asset) { return asset.id == canonical; });
    if(found==assets_.end()) return nullptr;
    if(textureStreamer_) {
        const auto view=textureStreamer_->request(canonical,resolveAssetPath(projectRoot_,canonical),streaming::TextureRole::Color,false,thumbnailDemand_,ResourcePriority::Preview,true);
        found->thumbnail=view.handle;
        if(view.sourceWidth && view.sourceHeight) { found->width=view.sourceWidth;found->height=view.sourceHeight; }
    }
    return &*found;
}
TextureAsset* AssetLibrary::findMutable(const std::string& id) const {
    const auto canonical=canonicalAssetId(id);
    const auto found = std::find_if(assets_.begin(), assets_.end(), [&](const auto& asset) { return asset.id == canonical; });
    if(found!=assets_.end())return &*found;
    if(canonical==GrayTextureId||canonical==NeutralNormalTextureId){TextureAsset asset;asset.id=canonical;asset.name=canonical==GrayTextureId?"Default gray":"Neutral normal";asset.width=asset.height=1;assets_.push_back(std::move(asset));return &assets_.back();}
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
            std::filesystem::path file;try{file=resolveAssetPath(projectRoot_,resolved);}catch(const std::exception&){return {};}
            const auto root=projectRoot_; const auto resolvers=modelResolvers();
            const auto forceCold=modelCacheRebuilds_.erase(resolved)!=0;
            const auto policy=references_;
            ResourceRequestOptions options; options.dependencies={file,file.parent_path()/"model.gltf",resolveAssetPath(root,"assets/references.json")};
            if(const auto bundle=quaderBundleSource(root,resolved))options.dependencies.push_back(*bundle);
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
                        if(!material.graph && !material.triplanar && !material.normalTexture.image.empty() && primitive.tangentUv!=material.normalTexture.texCoord) incompatible=true;
                    }
                    if(incompatible) data.reset();
                }
                if(!data) {
                    data=loadModelPackage(file,root,resolvers);
                    data->stream=std::make_shared<streaming::ModelStream>(streaming::buildModelCache(*data,root));
                    for(auto& primitive:data->primitives)releaseModelGeometry(primitive);
                }
                if(token.stop_requested()) throw std::runtime_error("Model load canceled.");
                const auto bytes=modelCpuBytes(*data);
                auto dependencies=packageDependencies;
                for(const auto& materialId:data->materialIds) {
                    const auto target=policy.resolve(materialId);
                    if(!target.empty()&&!isEngineAssetId(target)) {
                        // Imported sidecars may still be awaiting their catalog
                        // promotion. Embedded material data is already usable.
                        const auto materialFile=materialAssetPath(root,target);
                        if(std::filesystem::is_regular_file(materialFile))dependencies.push_back(materialFile);
                    }
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
    const auto resolved=references_.resolve(id);
    if(resolved==DefaultMaterialId) {
        static const auto value=[] {auto material=defaultMaterialAsset();canonicalizeMaterialAsset(material);return std::make_shared<const MaterialAsset>(std::move(material));}();return value;
    }
    if(resolved==DefaultParticleMaterialId) {
        static const auto value=[] {auto material=defaultParticleMaterialAsset();canonicalizeMaterialAsset(material);return std::make_shared<const MaterialAsset>(std::move(material));}();return value;
    }
    materialInterest_[resolved]=std::chrono::steady_clock::now();
    if(const auto authored=authoredMaterials_.find(resolved);authored!=authoredMaterials_.end()&&authored->second.definition)return authored->second.definition;
    if(resources_ && !resolved.empty()) {
        materialInterest_[resolved]=std::chrono::steady_clock::now();
        auto found=materialResources_.find(resolved);
        if(found==materialResources_.end() && std::filesystem::is_regular_file(materialAssetPath(projectRoot_,resolved))) {
            const auto root=projectRoot_; ResourceRequestOptions options;options.dependencies={materialAssetPath(root,resolved)};
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
    if(!material)return;
    auto canonical=std::make_shared<MaterialAsset>(*material);canonicalizeMaterialAsset(*canonical);material=std::move(canonical);
    validateMaterialAsset(*material);
    if(isEngineAssetId(material->id)||references_.deleted(material->id))throw std::runtime_error("Protected or deleted material cannot be published.");
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
        if(resolved==DefaultParticleMaterialId){auto material=defaultParticleMaterialAsset();canonicalizeMaterialAsset(material);return material.surface;}
        if(!resolved.empty()&&std::filesystem::exists(materialAssetPath(root,resolved))){const auto material=loadMaterialAsset(root,resolved);return modelMaterialCompatible(material)?material.surface:defaultMaterialAsset().surface;}return {};
    }};
}
std::vector<ModelMaterial> AssetLibrary::resolvedModelMaterials(const ModelAsset& model,std::span<const AssetRef<MaterialAsset>> materialOverrides)const {
    auto result=model.materials;
    for(size_t i=result.size();i<materialOverrides.size();++i)
        if(!materialOverrides[i].empty()&&!isLegacyUnusedMaterialSlot(model,i))throw std::runtime_error("An assigned material slot no longer exists in this model.");
    for(size_t i=0;i<result.size();++i) {
        const auto id=i<materialOverrides.size()&&!materialOverrides[i].empty()?materialOverrides[i].id:
            i<model.materialIds.size()?model.materialIds[i]:importedMaterialId(model.id,i);
        if(auto material=findMaterial(id))result[i]=materialCompatible(material)?material->surface:defaultMaterialAsset().surface;
    }
    for(const auto& primitive:model.primitives) {
        if(primitive.material>=result.size()) throw std::runtime_error("Model has an invalid material reference.");
        const auto& material=result[primitive.material];
        if(material.graph) {
            const auto required=requiredGraphUvs(material.graph);
            if((primitive.uvMask&required)!=required)throw std::runtime_error("Material texture requires an unavailable UV set: "+material.name);
            continue; // The graph normal path reconstructs a basis for a different UV set.
        }
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
std::vector<std::filesystem::path> AssetLibrary::modelDependencies(const std::string& id) const {
    const auto found=modelResources_.find(references_.resolve(id));return found==modelResources_.end()?std::vector<std::filesystem::path>{}:found->second.dependencies();
}
std::shared_ptr<const ModelAsset> AssetLibrary::peekModel(const std::string& id) const {
    const auto resolved=references_.resolve(id);if(resolved.empty())return {};
    if(resolved==BuiltinBoxModelId)return builtinBoxGeometry();
    if(const auto found=modelResources_.find(resolved);found!=modelResources_.end())if(auto snapshot=found->second.snapshot())return snapshot.shared();
    const auto found=std::find_if(models_.begin(),models_.end(),[&](const auto& model){return model->id==resolved;});return found==models_.end()?nullptr:*found;
}
std::shared_ptr<const MaterialAsset> AssetLibrary::peekMaterial(const std::string& id) const {
    const auto resolved=references_.resolve(id);
    if(resolved==DefaultMaterialId||resolved==DefaultParticleMaterialId)return findMaterial(resolved);
    if(const auto authored=authoredMaterials_.find(resolved);authored!=authoredMaterials_.end()&&authored->second.definition)return authored->second.definition;
    if(const auto found=materialResources_.find(resolved);found!=materialResources_.end())if(auto snapshot=found->second.snapshot())return snapshot.shared();
    const auto found=std::find_if(materials_.begin(),materials_.end(),[&](const auto& material){return material->id==resolved;});return found==materials_.end()?nullptr:*found;
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
    const auto locationRevision=contentStorageRevision(projectRoot_);
    if(storageRevision_!=locationRevision) {
        std::unordered_map<std::string,std::filesystem::path> next;
        for(const auto& entry:storedAssetPaths(projectRoot_))next.emplace(entry.id,entry.file);
        std::unordered_set<std::string> moved;
        std::unordered_set<std::filesystem::path> oldPaths;
        for(const auto& [id,file]:storageLocations_) {
            const auto found=next.find(id);if(found!=next.end()&&found->second==file)continue;
            moved.insert(id);oldPaths.insert(file);
        }
        // New registrations do not alter any existing loader. Only relocated
        // source/dependency paths require its captured loader to be recreated.
        for(const auto& [id,handle]:modelResources_)
            for(const auto& file:handle.dependencies())if(oldPaths.contains(file)){moved.insert(id);break;}
        for(const auto& [id,handle]:materialCatalogResources_)
            for(const auto& file:handle.dependencies())if(oldPaths.contains(file)){moved.insert(id);break;}
        for(const auto& id:moved) {
            if(resources_)resources_->invalidate(id);
            modelResources_.erase(id);materialResources_.erase(id);materialCatalogResources_.erase(id);
            registeredModelRevisions_.erase(id);preparedParticleMaterials_.erase(id);
        }
        storageLocations_=std::move(next);
        storageRevision_=locationRevision;++catalogRevision_;
    }
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
    std::erase_if(resizeImageSources_,[&](const auto& entry){return now-entry.second.used>resources_->config().idleGrace;});
    for(auto i=nodeImagePreviews_.begin();i!=nodeImagePreviews_.end();) {
        if(now-i->second.used>std::chrono::milliseconds{500}) {
            if(textureStreamer_&&!i->second.textureKey.empty())textureStreamer_->forgetGenerated(i->second.textureKey);
            i=nodeImagePreviews_.erase(i);
        } else ++i;
    }
    std::erase_if(preparedParticleMaterials_,[&](const auto& entry){return !materialInterest_.contains(entry.first);});
    for(auto iterator=materialCatalogResources_.begin();iterator!=materialCatalogResources_.end();) {
        if(auto snapshot=iterator->second.snapshot()) {
            try { ensureModelMaterials(*snapshot); }
            catch(const std::exception& error){status_=error.what();}
            iterator=materialCatalogResources_.erase(iterator);
        } else if(iterator->second.state()==ResourceState::Failed) { status_=iterator->second.diagnostic();iterator=materialCatalogResources_.erase(iterator); }
        else ++iterator;
    }
    for(auto iterator=animationCatalogResources_.begin();iterator!=animationCatalogResources_.end();) {
        if(auto snapshot=iterator->second.snapshot()) {
            try {if(!references_.deleted(snapshot->id))ensureModelAnimations(*snapshot);}
            catch(const std::exception& error){status_=error.what();}
            iterator=animationCatalogResources_.erase(iterator);
        } else if(iterator->second.state()==ResourceState::Failed){status_=iterator->second.diagnostic();iterator=animationCatalogResources_.erase(iterator);}
        else ++iterator;
    }
    for(const auto& [id,handle]:modelResources_) if(auto snapshot=handle.snapshot()) {
        if(registeredModelRevisions_[id]==snapshot.revision()) continue;
        try {
            std::erase_if(models_,[&](const auto& model){return model->id==id;});
            registerModel(snapshot.shared(),false); registeredModelRevisions_[id]=snapshot.revision();
        } catch(const std::exception& error) { status_=error.what(); }
    }
    if(materialCatalogDirty_)refreshMaterials();
    textureStreamer_->advance(); textureStreamer_->beginFrame();nodeImageStarts_=0;
    const auto message=textureStreamer_->diagnostic(); if(!message.empty()) status_=message;
}
void AssetLibrary::onFileChanged(const std::filesystem::path& changed) {
    if(changed.extension()==".crimson-quader") {
        try {
            // Rebuild derived files before any request can observe the source
            // notification. Cache writes themselves are excluded by the watcher.
            initializeQuaderSceneBundles(projectRoot_);
            refreshQuaderModelCatalog();
            const auto source=std::filesystem::weakly_canonical(changed);
            for(const auto& entry:storedAssetPaths(projectRoot_)) {
                if(entry.file.filename()!="manifest.json")continue;
                const auto bundle=quaderBundleSource(projectRoot_,entry.id);
                if(!bundle||std::filesystem::weakly_canonical(*bundle)!=source)continue;
                if(resources_)resources_->invalidate(entry.id);
                modelResources_.erase(entry.id);materialCatalogResources_.erase(entry.id);registeredModelRevisions_.erase(entry.id);
                std::erase_if(models_,[&](const auto& model){return model->id==entry.id;});
                if(references_.deleted(entry.id))continue;
                if(!resources_)registerModel(loadModelPackage(entry.file,projectRoot_,modelResolvers()),false);
                else if(!inactiveQuaderModels_.contains(entry.id)) {
                    std::ifstream input(entry.file);const auto manifest=nlohmann::json::parse(input);
                    const auto item=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& asset){return asset.id==entry.id;});
                    if(item==catalog_.end())catalog_.push_back({entry.id,manifest.at("name").get<std::string>(),AssetKind::Model});
                    else item->name=manifest.at("name").get<std::string>();
                }
            }
            if(materialCatalogDirty_)refreshMaterials();
            if(resources_)resources_->invalidatePath(changed);
            ++catalogRevision_;
        }catch(const std::exception& error){status_="Quader bundle refresh failed: "+std::string(error.what());}
        return;
    }
    const auto relative=std::filesystem::absolute(changed).lexically_normal().lexically_relative(projectRoot_);
    if(!relative.empty()&&!relative.has_root_path()&&(*relative.begin()=="Content"||*relative.begin()=="Engine")) {
        std::error_code error;const auto state=std::filesystem::status(changed,error);
        // Directory notifications have no asset identity/index mutation. Their
        // disappearance also needs one browser rescan, including empty folders.
        if(error||!std::filesystem::is_regular_file(state))++catalogRevision_;
        if(!error&&std::filesystem::is_directory(state))return;
    }
    auto id=assetIdAtPath(projectRoot_,changed);bool registered=false;
    try{registered=resolveAssetPath(projectRoot_,id)==std::filesystem::absolute(changed).lexically_normal();}catch(const std::exception&){/* A newly discovered file has no registered ID yet. */}
    if(std::filesystem::is_regular_file(changed)&&!registered) {
        try{discoverContentFiles(projectRoot_);id=assetIdAtPath(projectRoot_,changed);}catch(const std::exception& error){status_=error.what();return;}
    }
    if(resources_)resources_->invalidatePath(changed);
    if(textureFile(changed)) {
        if(textureStreamer_)textureStreamer_->reload(id);
        if(!references_.deleted(id) && !findMutable(id) && std::filesystem::is_regular_file(changed)) addTexture(makeAsset(changed));
    }
    if(changed.extension()==".crimson-scene") refreshScenes();
    if(changed.extension()==".crimson-material") refreshMaterials();
    if(changed.extension()==".crimson-particle") refreshParticles();
    if(changed.extension()==".crimson-agent-profile")refreshNavigationAgentProfiles();
    if(changed.extension()==".crimson-effect")refreshGameplayEffects();
    if(changed.extension()==".crimson-animation")refreshAnimations();
}
std::uint64_t AssetLibrary::materialShaderRevision()const {return graphPrograms_?graphPrograms_->revision():0;}
std::vector<std::filesystem::path> AssetLibrary::materialShaderDependencies() const {return graphPrograms_?graphPrograms_->sourceDependencies():std::vector<std::filesystem::path>{};}
void AssetLibrary::setTextureDemand(std::uint32_t edge,bool nativeQuality) { textureDemand_=std::max(1u,edge);nativeTextures_=nativeQuality; }
std::size_t AssetLibrary::textureResidentBytes() const { return (textureStreamer_ ? textureStreamer_->residentBytes() : 0)+(graphPrograms_?graphPrograms_->residentBytes():0); }
void AssetLibrary::trimTextureCache(std::size_t bytes) {
    const auto programs=graphPrograms_?graphPrograms_->residentBytes():0;
    if(textureStreamer_)textureStreamer_->trim(programs>=bytes?0:bytes-programs);
}
bool AssetLibrary::modelBindingsReady(const std::vector<ModelMaterialTextures>& bindings) const {
    return std::all_of(bindings.begin(),bindings.end(),[](const auto& material){return material.drawable();});
}
bool AssetLibrary::modelMaterialsReady(const ModelAsset& model,std::span<const AssetRef<MaterialAsset>> materialOverrides) const {
    const auto slots=std::max(model.materials.size(),model.materialIds.size());
    for(size_t i=slots;i<materialOverrides.size();++i)
        if(!materialOverrides[i].empty()&&!isLegacyUnusedMaterialSlot(model,i))return false;
    for(size_t i=0;i<slots;++i) {
        const auto id=i<materialOverrides.size()&&!materialOverrides[i].empty()?materialOverrides[i].id:
            i<model.materialIds.size()?model.materialIds[i]:importedMaterialId(model.id,i);
        const auto target=references_.resolveMaterial(id);
        if(target==DefaultMaterialId || target.empty()) continue;
        const auto material=findMaterial(target);
        if(!material||!materialDiagnostic(target).empty())return false;
    }
    return true;
}
std::string AssetLibrary::modelMaterialsDiagnostic(const ModelAsset& model,std::span<const AssetRef<MaterialAsset>> materialOverrides) const {
    const auto slots=std::max(model.materials.size(),model.materialIds.size());
    for(size_t i=slots;i<materialOverrides.size();++i)
        if(!materialOverrides[i].empty()&&!isLegacyUnusedMaterialSlot(model,i))return "An assigned material slot no longer exists in this model.";
    for(size_t i=0;i<slots;++i) {
        const auto id=i<materialOverrides.size()&&!materialOverrides[i].empty()?materialOverrides[i].id:
            i<model.materialIds.size()?model.materialIds[i]:importedMaterialId(model.id,i);
        const auto target=references_.resolveMaterial(id);
        if(target.empty() || target==DefaultMaterialId) continue;
        if(const auto material=findMaterial(target);material&&!materialCompatible(material))return "Mesh material is invalid: "+material->name;
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
    std::erase_if(catalog_,[](const auto& e){return e.kind==AssetKind::Material;});materials_.clear();
    std::unordered_set<std::string> present;
    for(const auto& file:storedAssetPaths(projectRoot_))if(std::filesystem::is_regular_file(file.file)&&file.file.extension()==".crimson-material") {
        const auto& id=file.id;if(references_.deleted(id))continue;
        present.insert(id);std::string name=previous.contains(id)?previous.at(id):utf8(file.file.stem());
        const auto working=authoredMaterials_.find(id);
        if(id==materialDocumentId_&&working!=authoredMaterials_.end()&&working->second.definition){name=working->second.definition->name;materials_.push_back(working->second.definition);}
        else try{auto asset=std::make_shared<MaterialAsset>(loadMaterialAsset(projectRoot_,id));name=asset->name;
            auto& version=authoredMaterials_[id];if(!version.definition||serializeMaterialAsset(*version.definition)!=serializeMaterialAsset(*asset)){version.definition=asset;version.revision=nextMaterialRevision_++;}
            version.diagnostic.clear();materials_.push_back(version.definition);}
        catch(const std::exception& e){auto& version=authoredMaterials_[id];version.diagnostic=e.what();status_+=std::string(e.what())+'\n';if(version.definition)materials_.push_back(version.definition);}
        catalog_.push_back({id,std::move(name),AssetKind::Material});
    }
    std::erase_if(authoredMaterials_,[&](const auto& entry){return !present.contains(entry.first)&&entry.first!=materialDocumentId_;});
    materialCatalogDirty_=false;
    ++catalogRevision_;
}

void AssetLibrary::addTexture(TextureAsset asset) {
    asset.id=canonicalAssetId(asset.id);
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

void AssetLibrary::ensureModelMaterials(const ModelAsset& model) {
    promoteModelMaterials(projectRoot_,model,[&](const auto& id){return references_.deleted(id);},[&](const auto& id) {
        materialCatalogDirty_=true;noteAssetWrite(id);
    });
    for(const auto& id:model.materialIds)
        if(!isEngineAssetId(id)&&!references_.deleted(id)&&!authoredMaterials_.contains(id)&&std::filesystem::is_regular_file(materialAssetPath(projectRoot_,id)))
            materialCatalogDirty_=true;
}
void AssetLibrary::ensureModelAnimations(const ModelAsset& model) {
    if(model.animations.empty()||!isAssetUuid(model.id)||isEngineAssetId(model.id))return;
    const auto locations=storedAssetPaths(projectRoot_);
    const auto source=resolveAssetPath(projectRoot_,model.id);
    std::vector<StoredAssetPath> registrations;std::vector<AssetFileChange> changes;
    std::vector<std::shared_ptr<const AnimationAsset>> created;
    for(std::size_t clip=0;clip<model.animations.size();++clip) {
        const auto id=importedAnimationId(model.id,static_cast<std::uint32_t>(clip));
        if(references_.deleted(id)||std::any_of(locations.begin(),locations.end(),[&](const auto& entry){return entry.id==id;}))continue;
        auto definition=std::make_shared<const AnimationAsset>(importedAnimationAsset(model,clip));
        const auto file=checkedContentPath(projectRoot_,source.parent_path().parent_path()/fromUtf8(id+".crimson-animation"));
        if(std::filesystem::exists(file))throw std::runtime_error("An unregistered imported Animation destination already exists.");
        registrations.push_back({id,file});changes.push_back({utf8(file.lexically_relative(projectRoot_)),serializeAnimationAsset(*definition)});created.push_back(std::move(definition));
    }
    if(changes.empty())return;
    changes.push_back({".crimson/asset-locations.json",prepareAssetPathRegistration(projectRoot_,registrations)});
    commitAssetTransaction(projectRoot_,changes);refreshContentStorage(projectRoot_);
    for(const auto& definition:created){publishAnimation(definition);noteAssetWrite(definition->id);}
}
void AssetLibrary::registerModel(std::shared_ptr<const ModelAsset> model,bool refreshMaterialCatalog) {
    if(!model)return;
    if(std::any_of(models_.begin(),models_.end(),[&](const auto& existing){return existing->id==model->id;})){
        if(!inactiveQuaderModels_.contains(model->id)&&std::none_of(catalog_.begin(),catalog_.end(),[&](const auto& entry){return entry.id==model->id;})){
            catalog_.push_back({model->id,model->name,AssetKind::Model});++catalogRevision_;
        }
        return;
    }
    if(!isAssetUuid(model->id)) {
        const auto original=model->id;const auto file=resolveAssetPath(projectRoot_,original);
        if(std::filesystem::is_regular_file(file))registerAssetPath(projectRoot_,original,file);
        auto normalized=std::make_shared<ModelAsset>(*model);normalized->id=canonicalAssetId(original);
        for(auto& id:normalized->images)id=canonicalAssetId(id);
        for(auto& id:normalized->materialIds)id=canonicalAssetId(id);
        for(auto& surface:normalized->materials) {
            for(auto* texture:{&surface.baseColorTexture,&surface.metallicRoughnessTexture,&surface.normalTexture,&surface.occlusionTexture,&surface.emissiveTexture})texture->image.id=canonicalAssetId(texture->image.id);
            if(surface.graph){auto graph=std::make_shared<MaterialGraph>(*surface.graph);for(auto& node:graph->nodes)if(node.kind==MaterialNodeKind::Texture2D||node.kind==MaterialNodeKind::ImageInput)node.texture.image.id=canonicalAssetId(node.texture.image.id);surface.graph=std::move(graph);}
        }
        if(normalized->stream){auto stream=std::make_shared<streaming::ModelStream>(*normalized->stream);stream->id=normalized->id;normalized->stream=std::move(stream);}
        model=std::move(normalized);
    }
    // Model-package image paths stay in their package. They share the ordinary
    // texture owner and remain usable in the editor's compatible image pickers.
    std::set<std::string> imageIds;
    ensureModelMaterials(*model);
    const auto hasImage=[&](const auto& id){return std::any_of(assets_.begin(),assets_.end(),[&](const auto& image){return image.id==id;});};
    for (const auto& image : model->images) if (!image.empty() && !hasImage(image)&&!references_.deleted(image)) imageIds.insert(image);
    for (const auto& material : model->materials) {
        for (const auto* texture : {&material.baseColorTexture, &material.metallicRoughnessTexture,
                &material.normalTexture, &material.occlusionTexture, &material.emissiveTexture}) {
            if (!texture->image.empty() && !hasImage(texture->image.id)&&!references_.deleted(texture->image.id)&&!isEngineAssetId(texture->image.id)) imageIds.insert(texture->image.id);
        }
    }
    std::vector<TextureAsset> pending;
    try {
        pending.reserve(imageIds.size());
        for (const auto& id : imageIds) {
            auto image = makeAsset(resolveAssetPath(projectRoot_,id));
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
    if(!inactiveQuaderModels_.contains(model->id)&&std::none_of(catalog_.begin(),catalog_.end(),[&](const auto& existing){return existing.id==model->id;})) catalog_.push_back({model->id, model->name, AssetKind::Model});
    ++catalogRevision_;
    if(resources_ && !modelResources_.contains(model->id)) {
        auto compact=std::make_shared<ModelAsset>();
        compact->id=model->id;compact->name=model->name;compact->format=model->format;compact->meshes=model->meshes;compact->nodes=model->nodes;
        compact->roots=model->roots;compact->draws=model->draws;compact->materials=model->materials;compact->materialIds=model->materialIds;
        compact->legacyUnusedMaterialSlot=model->legacyUnusedMaterialSlot;
        compact->images=model->images;compact->bounds=model->bounds;compact->warnings=model->warnings;compact->stream=model->stream;
        compact->skins=model->skins;compact->animations=model->animations;
        for(const auto& primitive:model->primitives) {
            ModelPrimitive metadata;metadata.material=primitive.material;metadata.uvMask=primitive.uvMask;metadata.tangentUv=primitive.tangentUv;
            metadata.sourceVertexCount=std::max(primitive.sourceVertexCount,primitive.vertices.size());metadata.sourceIndexCount=std::max(primitive.sourceIndexCount,primitive.indices.size());
            metadata.hasSkinning=primitive.hasSkinning;metadata.bounds=primitive.bounds;metadata.skinJointBounds=primitive.skinJointBounds;
            metadata.morphTargets.reserve(primitive.morphTargets.size());
            for(const auto& target:primitive.morphTargets) {ModelMorphTarget value;value.positionBounds=target.positionBounds;metadata.morphTargets.push_back(std::move(value));}
            compact->primitives.push_back(std::move(metadata));
        }
        if(model->stream) {
            ResourceRequestOptions options;options.dependencies={resolveAssetPath(projectRoot_,model->id),resolveAssetPath(projectRoot_,model->id).parent_path()/"model.gltf"};
            if(const auto bundle=quaderBundleSource(projectRoot_,model->id))options.dependencies.push_back(*bundle);
            resources_->seed<ModelAsset>(AssetRef<ModelAsset>{model->id},{compact,modelCpuBytes(*compact),0,{}},std::move(options));
            const auto root=projectRoot_;ResourceRequestOptions indexOptions;indexOptions.variant="metadata-index";indexOptions.priority=ResourcePriority::Background;indexOptions.retainLoader=false;
            (void)resources_->request<streaming::ModelStream>(AssetRef<streaming::ModelStream>{model->id},[compact,root](std::stop_token token){
                if(token.stop_requested())throw std::runtime_error("Model metadata cache canceled.");
                streaming::writeModelMetadataCache(*compact,*compact->stream,root);
                return ResourceLoadResult<streaming::ModelStream>{compact->stream,sizeof(streaming::ModelStream),compact->stream->key,{}};
            },std::move(indexOptions));
        }
    } else if(!resources_) models_.push_back(model);
    if(refreshMaterialCatalog&&materialCatalogDirty_)refreshMaterials();
    // Publish clip files after fallible image/resource preparation, so an
    // import preparation failure cannot leave source-dependent sidecars.
    ensureModelAnimations(*model);
}
void AssetLibrary::publishManagedModels(std::span<const std::shared_ptr<const ModelAsset>> models) {
    refreshQuaderModelCatalog();
    for(const auto& model:models)registerModel(model,false);
    if(materialCatalogDirty_)refreshMaterials();
}
std::vector<std::string> AssetLibrary::newModelImportAssets(const ModelAsset& model,bool reused) const {
    std::set<std::string> candidates;
    if(!reused) {
        candidates.insert(model.id);candidates.insert(model.images.begin(),model.images.end());
    }
    for(std::size_t i=0;i<model.materials.size();++i) {
        const auto id=importedMaterialId(model.id,i);
        if(i<model.materialIds.size()&&model.materialIds[i]!=id)continue;
        if(!references_.deleted(id)&&!std::filesystem::exists(materialAssetPath(projectRoot_,id)))candidates.insert(id);
    }
    for(std::size_t clip=0;clip<model.animations.size();++clip) {
        const auto id=importedAnimationId(model.id,static_cast<std::uint32_t>(clip));
        if(!references_.deleted(id))candidates.insert(id);
    }
    // Catalog discovery of an existing image/material is not a newly imported
    // asset. Keep every existing card's virtual location and authored alias.
    for(const auto& existing:catalog_)candidates.erase(existing.id);
    candidates.erase("");return {candidates.begin(),candidates.end()};
}
std::string AssetLibrary::placeImportedAssets(std::span<const std::string> ids,const std::string& destinationFolder) {
    if(ids.empty())return {};
    auto& browser=browserCatalog();
    std::string destination=destinationFolder.empty()?BrowserCatalog::ContentFolder:destinationFolder,warning;
    if(!browser.writableFolder(destination)) {
        destination=BrowserCatalog::ContentFolder;warning="Assets imported into Content: The destination folder no longer exists or is read-only.";
    }
    std::vector<std::string> available;available.reserve(ids.size());
    for(const auto& id:ids)if(browser.contains(id))available.push_back(id);
    if(available.empty())return {};
    try{browser.move(available,destination,true);noteAssetWrite("assets/browser.json");return warning;}
    catch(const std::exception& error){return "Assets imported into the browser root: "+std::string(error.what());}
}

TextureAsset AssetLibrary::makeAsset(const std::filesystem::path& path) {
    TextureAsset asset;
    asset.id = assetIdAtPath(projectRoot_,path);
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

const TextureAsset* AssetLibrary::importTexture(const std::filesystem::path& source,const std::string& destinationFolder) {
    try {
        if (!textureFile(source)) throw std::runtime_error("Choose a PNG, JPEG, TGA, BMP or HDR image.");
        std::string existing;try{existing=assetIdAtPath(projectRoot_,source);}catch(const std::exception&){/* External source files have no project identity. */}
        if(!existing.empty()&&find(existing)) {select(existing);status_="Reused "+displayName(existing);return find(existing);}
        // Decode before copying so a failed import does not leave a broken asset.
        const auto pixels = [&] {
            if(!resources_)return decode(source);
            auto image=decodeManagedImage(source);
            return Pixels{image.width,image.height,std::move(image.rgba)};
        }();
        const auto folder = resolveAssetPath(projectRoot_,"assets/textures");
        std::filesystem::create_directories(folder);
        auto destination = folder / source.filename();
        if (std::filesystem::exists(destination) && std::filesystem::equivalent(source, destination)) {
            const auto id = assetIdAtPath(projectRoot_,destination);
            if (!find(id)) addTexture(makeAsset(destination));
            select(id);
            status_ = "Imported " + utf8(destination.filename());
            return find(id);
        }
        const auto unavailable=[&](const auto& file){
            if(std::filesystem::exists(file))return true;
            for(const auto& entry:storedAssetPaths(projectRoot_))if(entry.file==std::filesystem::weakly_canonical(file))return true;
            return false;};
        for (uint32_t suffix = 2; unavailable(destination); ++suffix) {
            destination = folder / (source.stem().native() + L"-" + std::to_wstring(suffix) + source.extension().native());
        }
        TextureAsset asset;
        asset.id = newAssetId();
        asset.name = utf8(destination.filename());
        asset.width = pixels.width; asset.height = pixels.height;
        asset.hdr = extension(destination) == ".hdr";
        if(!resources_) {asset.thumbnail = upload(thumbnail(pixels), false);bgfx::setName(asset.thumbnail, asset.name.c_str());}
        try { std::filesystem::copy_file(source, destination); }
        catch (...) { destroyTexture(asset.thumbnail); throw; }
        registerAssetPath(projectRoot_,asset.id,destination);
        addTexture(std::move(asset));
        select(assets_.back().id);
        status_ = "Imported " + assets_.back().name;
        if(const auto warning=placeImportedAssets(std::array{assets_.back().id},destinationFolder);!warning.empty())status_+='\n'+warning;
        return &assets_.back();
    } catch (const std::exception& error) { status_ = error.what(); return nullptr; }
}

std::shared_ptr<const ModelAsset> AssetLibrary::importModel(const std::filesystem::path& source,const std::string& destinationFolder) {
    try {
        const auto stage=stageModelImport(source,projectRoot_);
        auto model=publishModelImport(stage,[&](const auto& id){return references_.deleted(id);});
        const auto newAssets=newModelImportAssets(*model,stage->reused);
        const bool registered=std::any_of(catalog_.begin(),catalog_.end(),[&](const auto& asset){return asset.id==model->id;});
        if (!stage->reused || !registered) registerModel(model);
        else {ensureModelMaterials(*model);ensureModelAnimations(*model);if(materialCatalogDirty_)refreshMaterials();}
        select(model->id);
        status_ = std::string(stage->reused ? "Reused " : "Imported ") + model->name;
        if(const auto warning=placeImportedAssets(newAssets,destinationFolder);!warning.empty())status_+='\n'+warning;
        return model;
    } catch (const std::exception& error) { status_ = error.what(); return nullptr; }
}

size_t AssetLibrary::importFiles(const std::vector<std::filesystem::path>& sources, const std::string& destinationFolder) {
    size_t succeeded = 0;
    std::string failures,notices;
    for (const auto& source : sources) {
        const auto type = extension(source);
        bool imported = false;
        if (type == ".gltf" || type == ".glb") imported = static_cast<bool>(importModel(source,destinationFolder));
        else if (textureFile(source)) imported = importTexture(source,destinationFolder) != nullptr;
        else status_ = "Unsupported asset format. Choose PNG, JPEG, TGA, BMP, HDR, glTF or GLB.";
        if (imported) {++succeeded;if(const auto newline=status_.find('\n');newline!=std::string::npos)notices+=status_.substr(newline);}
        else failures += "\n" + utf8(source.filename()) + ": " + status_;
    }
    status_ = "Imported " + std::to_string(succeeded) + " of " + std::to_string(sources.size()) + " assets." + failures + notices;
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
    const auto file = resolveAssetPath(projectRoot_,"assets/preview.json");
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
        std::ofstream output(resolveAssetPath(projectRoot_,"assets/preview.json"));
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
    if(textureStreamer_) return textureStreamer_->request(asset.id,resolveAssetPath(projectRoot_,asset.id),srgb?streaming::TextureRole::Color:streaming::TextureRole::Data,
        false,nativeTextures_?UINT32_MAX:textureDemand_).handle;
    auto& handle = srgb ? asset.colorTexture : asset.dataTexture;
    auto& attempted = srgb ? asset.colorAttempted : asset.dataAttempted;
    if (!bgfx::isValid(handle) && !attempted) {
        attempted = true;
        if(asset.id==GrayTextureId||asset.id==NeutralNormalTextureId){Pixels pixels{1,1,{128,128,asset.id==NeutralNormalTextureId?uint8_t{255}:uint8_t{128},255}};handle=upload(pixels,srgb);}
        else handle = upload(decode(resolveAssetPath(projectRoot_,asset.id)), srgb);
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
    for(std::size_t index=0;index<materials.size();++index) {
        const auto& material=materials[index];
        if(material.graph && graphPrograms_) {
            // The fixed normal shader consumes authored tangents. A graph can
            // instead reconstruct another UV basis, so certify this per mesh.
            bool compatible=true;
            if(!material.triplanar && !material.normalTexture.image.empty())for(const auto& primitive:model.primitives)
                if(primitive.material==index && primitive.tangentUv!=material.normalTexture.texCoord && !isBuiltinModel(model.id))compatible=false;
            result.push_back(graphMaterialBindings(material.graph,nativeTextures_?UINT32_MAX:textureDemand_,
                nativeTextures_?ResourcePriority::Selected:ResourcePriority::Visible,compatible));
        } else result.push_back(materialBindings(material));
    }
    return result;
}
ModelMaterialTextures AssetLibrary::materialBindings(const ModelMaterial& material) const {
    return materialBindings(material,nativeTextures_?UINT32_MAX:textureDemand_,nativeTextures_?ResourcePriority::Selected:ResourcePriority::Visible);
}
ModelMaterialTextures AssetLibrary::materialBindings(const ModelMaterial& material,std::uint32_t textureEdge,ResourcePriority priority) const {
        if(material.graph && graphPrograms_)return graphMaterialBindings(material.graph,textureEdge,priority);
        ModelMaterialTextures target;
        const std::array<const ModelTexture*, 5> slots{&material.baseColorTexture, &material.metallicRoughnessTexture,
            &material.normalTexture, &material.occlusionTexture, &material.emissiveTexture};
        for (size_t slot = 0; slot < slots.size(); ++slot) {
            const auto& source = *slots[slot];
            const auto sampler=materialSampler(material,source);
            if (source.image.empty()) continue;
            const auto resolved=references_.resolveTexture(source.image.id,slot==2);
            if(resolved.empty())continue;
            auto* image = findMutable(resolved);
            if (!image) throw std::runtime_error("Material image is unavailable: " + source.image.id);
            if(image->hdr)throw std::runtime_error("HDR images are unsupported in material slots: "+source.image.id);
            if(!textureStreamer_&&!isEngineAssetId(image->id)&&!std::filesystem::is_regular_file(resolveAssetPath(projectRoot_,image->id)))throw std::runtime_error("Material image is missing: "+source.image.id);
            if(textureStreamer_) {
                const auto role=slot==2?streaming::TextureRole::Normal:(slot==0||slot==4?streaming::TextureRole::Color:streaming::TextureRole::Data);
                const bool mips=sampler.minFilter!=ModelFilter::Linear && sampler.minFilter!=ModelFilter::Nearest;
                const auto view=textureStreamer_->request(image->id,resolveAssetPath(projectRoot_,image->id),role,mips,textureEdge,priority);
                target.handles[slot]=view.handle;target.leases[slot]=view.lease;
                target.sourceFingerprints[slot]=view.sourceFingerprint;target.signatures[slot]=view.signature;
                target.complete &=view.complete;target.sourceReady &=view.sourceReady;
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
ModelMaterialTextures AssetLibrary::graphMaterialBindings(const std::shared_ptr<const MaterialGraph>& graph,std::uint32_t textureEdge,ResourcePriority priority,bool useFixedModel) const {
    auto& cached=graphCache(graph);
    if(useFixedModel) {
        if(!cached.fixedSurfaceChecked) {
            cached.fixedSurface=materialGraphFixedSurface(*graph);cached.fixedSurfaceChecked=true;
        }
        // The embedded model program is the complete implementation for this
        // certified graph subset, including maps and material factors. Queuing
        // identical generated code would add cold startup work for no effect.
        if(cached.fixedSurface)return materialBindings(*cached.fixedSurface,textureEdge,priority);
    }
    if(!cached.shader) {
        MaterialAsset source;source.id=DefaultMaterialId;source.name="Material graph";source.graph=*graph;
        canonicalizeMaterialAsset(source);
        cached.shader=std::make_shared<MaterialGraphShader>(generateMaterialGraphShader(source));
    }
    return preparedGraphBindings(*graphPrograms_,*cached.shader,*graph,textureEdge,priority);
}
bool AssetLibrary::materialCompatible(const std::shared_ptr<const MaterialAsset>& material) const {
    auto found=materialCompatibility_.find(material.get());
    if(found!=materialCompatibility_.end()&&!found->second.source.expired())return found->second.compatible;
    if(materialCompatibility_.size()>128)std::erase_if(materialCompatibility_,[](const auto& entry){return entry.second.source.expired();});
    const bool compatible=modelMaterialCompatible(*material);
    materialCompatibility_[material.get()]={material,compatible};return compatible;
}
AssetLibrary::GraphShaderCache& AssetLibrary::graphCache(const std::shared_ptr<const MaterialGraph>& graph) const {
    auto found=graphShaderCache_.find(graph.get());
    if(found!=graphShaderCache_.end()&&!found->second.source.expired())return found->second;
    if(graphShaderCache_.size()>128)std::erase_if(graphShaderCache_,[](const auto& entry){return entry.second.source.expired();});
    auto& cached=graphShaderCache_[graph.get()];cached={};cached.source=graph;return cached;
}
std::uint8_t AssetLibrary::requiredGraphUvs(const std::shared_ptr<const MaterialGraph>& graph) const {
    auto& cached=graphCache(graph);
    if(!cached.requiredUvs)cached.requiredUvs=materialGraphRequiredUvs(*graph);
    return *cached.requiredUvs;
}

ModelMaterialTextures AssetLibrary::preparedGraphBindings(MaterialGraphPrograms& owner,const MaterialGraphShader& generated,const MaterialGraph& graph,std::uint32_t textureEdge,ResourcePriority priority) const {
    const auto programs=owner.request(generated,priority);
    if(!programs.current || !programs.programs) {
        // Unsupported graph/coverage expressions stay on the domain fallback;
        // published models retain their previous complete packet. Exact fixed
        // model graphs have already returned without requesting compilation.
        ModelMaterialTextures fallback;
        fallback.graphPending=true;
        fallback.diagnostic=programs.diagnostic.empty()?"Compiling material graph...":programs.diagnostic;
        return fallback;
    }
    ModelMaterialTextures result;
    result.diagnostic=programs.diagnostic;
    auto packet=std::make_shared<PreparedGraphMaterial>();
    packet->programs=programs.programs;packet->uniforms=generated.uniforms;packet->sources=generated.textures;
    packet->runtimeGraph=generated.runtimeGraph;packet->runtimeParameters=generated.runtimeParameters;
    packet->dynamicCoverage=generated.dynamicCoverage;
    packet->dynamicHeight=generated.dynamicHeight;
    packet->tessellation=generated.tessellation;
    const auto& output=materialOutputNode(graph).output;
    packet->shading=generated.nodePreview?MaterialShading::Unlit:output.shading;
    packet->blend=generated.nodePreview?MaterialBlend::Opaque:output.blend;
    packet->receiveFog=output.receiveFog;
    packet->nodePreview=generated.nodePreview;
    packet->refraction=generated.refraction;
    packet->signature=programs.programs->signature;
    streaming::detail::hash(packet->signature,packet->receiveFog);
    streaming::detail::hash(packet->signature,packet->dynamicHeight);
    streaming::detail::hash(packet->signature,packet->tessellation.enabled);
    streaming::detail::hash(packet->signature,packet->tessellation.amount);
    streaming::detail::hash(packet->signature,packet->tessellation.midpoint);
    streaming::detail::hash(packet->signature,packet->tessellation.maxFactor);
    streaming::detail::hash(packet->signature,packet->tessellation.heightLod);
    streaming::detail::hash(packet->signature,generated.runtimeDefinitionSignature);
    std::unordered_map<std::string,std::uint64_t> sourceVersions;
    for(auto& binding:packet->sources) {
        const auto& sampler=binding.sampler;const auto slot=binding.slot;
        std::string resolved;
        if(binding.resize) {
            const auto texture=preparedResizeTexture(*binding.resize,textureEdge,priority,sourceVersions);
            packet->textures[slot]=texture.handle;packet->leases[slot]=texture.lease;
            packet->sourceFingerprints[slot]=texture.sourceFingerprint;packet->textureSignatures[slot]=texture.signature;
            result.complete &=texture.complete;result.sourceReady &=texture.sourceReady;
            if(!texture.sourceReady||!texture.complete)result.diagnostic=texture.diagnostic.empty()?"Generating cached material image...":texture.diagnostic;
            if(!bgfx::isValid(texture.handle)&&!texture.diagnostic.empty()&&texture.diagnostic!="Loading cached material image inputs...") {
                if(texture.diagnostic!="Generating cached material image...")throw std::runtime_error(texture.diagnostic);
            }
        } else if(binding.grunge) {
            if(!textureStreamer_)throw std::runtime_error("Generated material textures require the resource owner.");
            const auto settings=*binding.grunge;
            const auto key=std::to_string(grungeTextureKey(settings,GrungeTextureEdge));
            const auto texture=textureStreamer_->requestGenerated(key,[settings]{return generateGrungeTexture(settings,GrungeTextureEdge);},GrungeTextureEdge,true,textureEdge,priority);
            packet->textures[slot]=texture.handle;packet->leases[slot]=texture.lease;
            packet->sourceFingerprints[slot]=texture.sourceFingerprint;packet->textureSignatures[slot]=texture.signature;
            result.complete &=texture.complete;result.sourceReady &=texture.sourceReady;
            if(!texture.sourceReady || !texture.complete)result.diagnostic=texture.diagnostic.empty()?"Generating grunge texture...":texture.diagnostic;
        } else {
            const bool normal=binding.role==MaterialGraphTextureRole::Normal;
            resolved=references_.resolveTexture(binding.image.id,normal);
            if(binding.image.empty() || resolved.empty())packet->uniforms[binding.settingsRow][1]=0;
            if(resolved.empty())resolved=normal?NeutralNormalTextureId:GrayTextureId;
            auto* image=findMutable(resolved);
            if(!image || image->hdr)throw std::runtime_error("Material texture is unavailable or unsupported: "+binding.image.id);
            const auto role=normal?streaming::TextureRole::Normal:binding.role==MaterialGraphTextureRole::Color?streaming::TextureRole::Color:streaming::TextureRole::Data;
            if(textureStreamer_) {
                const bool mips=sampler.minFilter!=ModelFilter::Linear && sampler.minFilter!=ModelFilter::Nearest;
                const auto texture=textureStreamer_->request(resolved,resolveAssetPath(projectRoot_,resolved),role,mips,textureEdge,priority);
                packet->textures[slot]=texture.handle;packet->leases[slot]=texture.lease;
                packet->sourceFingerprints[slot]=texture.sourceFingerprint;packet->textureSignatures[slot]=texture.signature;
                result.complete &=texture.complete;result.sourceReady &=texture.sourceReady;
                if(!bgfx::isValid(texture.handle) && !texture.diagnostic.empty())throw std::runtime_error(texture.diagnostic);
                if(texture.sourceFingerprint) {
                    const auto [entry,inserted]=sourceVersions.emplace(resolved,texture.sourceFingerprint);
                    if(!inserted && entry->second!=texture.sourceFingerprint)result.sourceReady=false;
                }
            } else {
                packet->textures[slot]=fullTexture(*image,role==streaming::TextureRole::Color);
                result.complete &=bgfx::isValid(packet->textures[slot]);result.sourceReady &=result.complete;
            }
        }
        std::uint32_t flags{};
        if(sampler.wrapU==ModelWrap::Clamp)flags|=BGFX_SAMPLER_U_CLAMP;
        else if(sampler.wrapU==ModelWrap::Mirror)flags|=BGFX_SAMPLER_U_MIRROR;
        if(sampler.wrapV==ModelWrap::Clamp)flags|=BGFX_SAMPLER_V_CLAMP;
        else if(sampler.wrapV==ModelWrap::Mirror)flags|=BGFX_SAMPLER_V_MIRROR;
        if(sampler.minFilter==ModelFilter::Nearest||sampler.minFilter==ModelFilter::NearestMipmapNearest||sampler.minFilter==ModelFilter::NearestMipmapLinear)flags|=BGFX_SAMPLER_MIN_POINT;
        if(sampler.minFilter==ModelFilter::NearestMipmapNearest||sampler.minFilter==ModelFilter::LinearMipmapNearest)flags|=BGFX_SAMPLER_MIP_POINT;
        if(sampler.magFilter==ModelFilter::Nearest)flags|=BGFX_SAMPLER_MAG_POINT;
        packet->flags[slot]=flags;if(!binding.grunge&&!binding.resize)binding.image.id=resolved;
        streaming::detail::hash(packet->signature,flags);streaming::detail::hash(packet->signature,packet->textures[slot].idx);
        streaming::detail::hash(packet->signature,packet->textureSignatures[slot]);streaming::detail::hash(packet->signature,packet->sourceFingerprints[slot]);
    }
    for(const auto& row:packet->uniforms)streaming::detail::hash(packet->signature,row);
    result.graph=std::move(packet);return result;
}
PreparedTexture AssetLibrary::preparedResizeTexture(const MaterialGraphResizeBinding& binding,std::uint32_t textureEdge,ResourcePriority priority,std::unordered_map<std::string,std::uint64_t>& versions,CachedImageRequestInfo* requestInfo,bool allowStart) const {
    if(requestInfo)*requestInfo={};
    if(!resources_||!textureStreamer_)throw std::runtime_error("Cached material images require the asynchronous resource owner.");
    if(!binding.graph)throw std::runtime_error("Cached material image has no immutable graph definition.");
    const auto* node=findMaterialNode(*binding.graph,binding.node);
    if(!node||!isMaterialImageBakeNode(node->kind))throw std::runtime_error("Cached material image references a missing image boundary.");
    const auto sources=materialResizeImageSources(*binding.graph,binding.node);
    struct Input {std::string original,resolved;ResizeImageSource* source;std::uint64_t fingerprint;};
    std::vector<Input> inputs;inputs.reserve(sources.size());bool ready=true;
    bool extentKnown=true;
    auto extent=materialCachedImageExtent(*binding.graph,binding.node,[&](const std::string& original) {
        const auto resolved=references_.resolve(original);
        if(resolved==GrayTextureId||resolved==NeutralNormalTextureId)return std::array<unsigned,2>{1,1};
        const auto* source=findMutable(resolved);
        if(!source||source->hdr)throw std::runtime_error("Cached image input is unavailable or unsupported: "+original);
        if(!source->width||!source->height){extentKnown=false;return std::array<unsigned,2>{1,1};}
        return std::array<unsigned,2>{source->width,source->height};
    });
    auto key=binding.definitionKey;
    streaming::detail::hash(key,material_procedural::algorithm_revision);
    for(const auto& original:sources) {
        const auto resolved=references_.resolve(original);
        if(resolved.empty())throw std::runtime_error("Resize input image is unavailable: "+original);
        const bool builtin=resolved==GrayTextureId||resolved==NeutralNormalTextureId;
        auto* image=findMutable(resolved);
        if(!builtin&&(!image||image->hdr))throw std::runtime_error("Resize input image is unavailable or unsupported: "+original);
        const auto file=builtin?std::filesystem::path{}:resolveAssetPath(projectRoot_,resolved);
        auto& source=resizeImageSources_[resolved];source.used=std::chrono::steady_clock::now();
        const bool moved=!source.file.empty()&&source.file!=file;
        if(source.file!=file){source.file=file;source.fingerprint={};source.pixels={};source.reloadPixels=moved;}
        if(!source.fingerprint) {
            ResourceRequestOptions config;config.variant="resize-input-fingerprint-v1";config.priority=priority;config.forceReload=moved;if(!builtin)config.dependencies={file};
            auto builtinKey=streaming::detail::HashSeed;streaming::detail::hash(builtinKey,resolved.data(),resolved.size());
            source.fingerprint=resources_->request<streaming::TextureMipCache>(AssetRef<streaming::TextureMipCache>{resolved},[builtin,builtinKey](std::stop_token token) {
                if(token.stop_requested())throw std::runtime_error("Resize input fingerprint request canceled.");
                auto data=std::make_shared<streaming::TextureMipCache>();return ResourceLoadResult<streaming::TextureMipCache>{data,sizeof(*data),builtin?builtinKey:0,{}};
            },std::move(config));
        }
        if(source.fingerprint.state()==ResourceState::Failed)throw std::runtime_error("Resize input "+original+": "+source.fingerprint.diagnostic());
        const auto descriptor=source.fingerprint.snapshot();
        if(!descriptor||source.fingerprint.state()!=ResourceState::Ready){ready=false;continue;}
        const auto [existing,inserted]=versions.emplace(resolved,descriptor.fingerprint());
        if(!inserted&&existing->second!=descriptor.fingerprint())ready=false;
        streaming::detail::hash(key,original.data(),original.size());streaming::detail::hash(key,resolved.data(),resolved.size());
        streaming::detail::hash(key,descriptor.fingerprint());streaming::detail::hash(key,descriptor.revision());
        inputs.push_back({original,resolved,&source,descriptor.fingerprint()});
    }
    if(!ready){PreparedTexture waiting;waiting.diagnostic="Loading cached material image inputs...";return waiting;}
    const auto keyForExtent=[&](const std::array<unsigned,2>& dimensions) {
        auto sizedKey=key;streaming::detail::hash(sizedKey,dimensions[0]);streaming::detail::hash(sizedKey,dimensions[1]);
        return "material-image-v3:"+std::to_string(sizedKey);
    };
    auto contentKey=keyForExtent(extent);
    if(extentKnown)if(auto cached=textureStreamer_->cachedGeneratedFloat(contentKey,extent[0],extent[1],true,textureEdge,priority)) {
        if(requestInfo)*requestInfo={contentKey,extent,true,false};
        for(auto& input:inputs)input.source->pixels={};
        return *cached;
    }
    auto images=std::make_shared<std::unordered_map<std::string,std::shared_ptr<const DecodedImage>>>();
    for(auto& input:inputs) {
        auto& source=*input.source;const bool builtin=input.resolved==GrayTextureId||input.resolved==NeutralNormalTextureId;
        if(!source.pixels) {
            ResourceRequestOptions config;config.variant="source";config.priority=priority;config.forceReload=source.reloadPixels;source.reloadPixels=false;if(!builtin)config.dependencies={source.file};
            const auto file=source.file;const auto resolved=input.resolved;const auto fingerprint=input.fingerprint;
            source.pixels=resources_->request<DecodedImage>(AssetRef<DecodedImage>{resolved},[file,resolved,builtin,fingerprint](std::stop_token token) {
                if(token.stop_requested())throw std::runtime_error("Resize input image decode canceled.");
                auto pixels=std::make_shared<DecodedImage>(builtin?DecodedImage{1,1,resolved==NeutralNormalTextureId?std::vector<std::uint8_t>{128,128,255,255}:std::vector<std::uint8_t>{128,128,128,255}}:decodeManagedImage(file));
                return ResourceLoadResult<DecodedImage>{pixels,pixels->rgba.size(),builtin?fingerprint:0,{}};
            },std::move(config));
        }
        if(source.pixels.state()==ResourceState::Failed)throw std::runtime_error("Resize input "+input.original+": "+source.pixels.diagnostic());
        const auto pixels=source.pixels.snapshot();
        if(!pixels||source.pixels.state()!=ResourceState::Ready){ready=false;continue;}
        if(pixels.fingerprint()!=input.fingerprint){source.pixels={};source.reloadPixels=true;ready=false;continue;}
        images->emplace(input.original,pixels.shared());
    }
    if(!ready){PreparedTexture waiting;waiting.diagnostic="Loading cached material image inputs...";return waiting;}
    // Catalog dimensions can lag a file watcher event until its thumbnail is
    // requested. The accepted immutable snapshots define the actual bake lattice.
    extent=materialCachedImageExtent(*binding.graph,binding.node,[&](const std::string& original) {
        const auto found=images->find(original);
        if(found==images->end())throw std::runtime_error("Cached material source snapshot is unavailable: "+original);
        return std::array<unsigned,2>{found->second->width,found->second->height};
    });
    for(const auto& input:inputs)if(auto* asset=findMutable(input.resolved)) {
        const auto& pixels=*images->at(input.original);asset->width=pixels.width;asset->height=pixels.height;
    }
    contentKey=keyForExtent(extent);
    if(auto cached=textureStreamer_->cachedGeneratedFloat(contentKey,extent[0],extent[1],true,textureEdge,priority)) {
        if(requestInfo)*requestInfo={contentKey,extent,true,false};
        for(auto& input:inputs)input.source->pixels={};
        return *cached;
    }
    if(requestInfo)*requestInfo={contentKey,extent,false,false};
    if(!allowStart){PreparedTexture waiting;waiting.diagnostic="Waiting for cached material image generation...";return waiting;}
    if(requestInfo){requestInfo->requested=true;requestInfo->started=true;}
    const auto graph=binding.graph;const auto resizeNode=binding.node;
    return textureStreamer_->requestGeneratedFloat(contentKey,[graph,resizeNode,images](std::stop_token token) {
        return bakeMaterialCachedImage(*graph,resizeNode,[images](const std::string& id) {
            const auto found=images->find(id);if(found==images->end())throw std::runtime_error("Cached material source snapshot is unavailable: "+id);return found->second;
        },token);
    },extent[0],extent[1],true,textureEdge,priority);
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
        if(!materialCompatible(material)) {
            auto fallback=preparedParticleMaterial(DefaultParticleMaterialId);
            *result=*fallback;result->current=false;
            result->diagnostic="Sprite material is invalid: "+material->name;
            return result;
        }
        const auto diagnostic=materialDiagnostic(resolved);if(!diagnostic.empty())throw std::runtime_error(diagnostic);
        auto textures=materialBindings(material->surface);
        if(textures.graphPending && previous!=preparedParticleMaterials_.end() && previous->second->ready && previous->second->render.graph)throw std::runtime_error(textures.diagnostic);
        const auto graphPending=textures.graphPending;const auto graphDiagnostic=textures.diagnostic;
        if(!textures.sourceReady||!textures.complete||(textures.flags[0]!=UINT32_MAX&&!bgfx::isValid(textures.handles[0])))throw std::runtime_error("Preparing particle material textures.");
        struct Pins {std::shared_ptr<const MaterialAsset> material;ModelMaterialTextures textures;};
        const auto pins=std::make_shared<Pins>(Pins{material,std::move(textures)});
        result->render.baseColor=material->surface.baseColor;result->render.blend=material->blend;
        result->render.shading=material->shading;result->render.roughness=material->surface.roughness;
        result->render.metallic=material->surface.metallic;result->render.specular=material->surface.specular;
        result->render.occlusion=1;result->render.alphaCutoff=material->surface.alphaCutoff;
        result->render.emission=material->surface.emissive;result->render.doubleSided=material->surface.doubleSided;
        result->render.receiveFog=material->surface.receiveFog;
        result->render.uvScale=material->surface.uvScale;result->render.uvOffset=material->surface.uvOffset;result->render.uvRotation=material->surface.uvRotation;result->render.triplanar=material->surface.triplanar;result->render.triplanarWorld=material->surface.triplanarWorld;
        result->render.colorTexture=pins->textures.handles[0];result->render.samplerFlags=pins->textures.flags[0];result->render.pinned=pins;
        result->render.graph=pins->textures.graph;
        if(result->render.graph)result->render.baseColor={1,1,1,1};
        result->ready=true;result->current=!graphPending;result->diagnostic=graphDiagnostic;result->sourceFingerprint=pins->textures.sourceFingerprints[0];
        result->signature=1469598103934665603ull;for(const auto value:serializeMaterialAsset(*material))result->signature=(result->signature^static_cast<uint8_t>(value))*1099511628211ull;
        result->signature=(result->signature^pins->textures.signatures[0])*1099511628211ull;
        if(pins->textures.graph){result->signature=pins->textures.graph->signature;result->sourceFingerprint=result->signature;}
        if(cacheable)preparedParticleMaterials_[resolved]=result;
    }catch(const std::exception& error){
        if(previous!=preparedParticleMaterials_.end()&&previous->second->ready)*result=*previous->second;
        result->current=false;
        result->diagnostic=error.what();
    }
    return result;
}
ModelMaterialTextures AssetLibrary::materialPreviewBindings(const MaterialAsset& material,std::uint32_t textureEdge) const {
    validateMaterialAsset(material);
    return materialBindings(material.surface,std::max(1u,textureEdge),ResourcePriority::Selected);
}
void AssetLibrary::releaseMaterialNodeImagePreviews(const std::string& documentId) const {
    for(auto i=nodeImagePreviews_.begin();i!=nodeImagePreviews_.end();) {
        if(i->second.document==documentId) {
            if(textureStreamer_&&!i->second.textureKey.empty())textureStreamer_->forgetGenerated(i->second.textureKey);
            i=nodeImagePreviews_.erase(i);
        } else ++i;
    }
}
PreparedTexture AssetLibrary::materialNodeImagePreview(const std::string& documentId,
    const std::shared_ptr<const MaterialGraph>& graph,MaterialGraphPreviewTarget target,unsigned edge) const {
    if(!resources_||!textureStreamer_||!graph)throw std::runtime_error("Node image previews require the asynchronous resource owner.");
    edge=std::clamp(edge,32u,256u);
    const auto priority=edge>=256?ResourcePriority::Selected:ResourcePriority::Preview;
    const auto stateId=documentId+":"+std::to_string(target.node)+":"+std::to_string(unsigned(target.pin))+":"+std::to_string(edge);
    if(!nodeImagePreviews_.contains(stateId)&&nodeImagePreviews_.size()>=256) {
        PreparedTexture waiting;waiting.diagnostic="Waiting for a node image preview slot...";return waiting;
    }
    auto& state=nodeImagePreviews_[stateId];state.document=documentId;state.used=std::chrono::steady_clock::now();
    struct ReleaseFailedInputs {
        NodeImagePreview& state;
        int exceptions{std::uncaught_exceptions()};
        ~ReleaseFailedInputs(){if(std::uncaught_exceptions()>exceptions){state.pendingInputs.clear();state.pending=false;}}
    } releaseFailedInputs{state};
    const auto waiting=[&](const std::string& message) {
        auto result=state.current?*state.current:PreparedTexture{};result.complete=false;result.sourceReady=false;result.diagnostic=message;return result;
    };
    const MaterialImagePreviewOptions options{edge,edge,64ull*1024*1024};
    if(state.definition!=graph) {
        state.definition=graph;state.diagnostic.clear();
        try {
            validateMaterialGraphImagePreview(*graph,target,options);
            const auto& output=materialOutputNode(*graph).output;
            const auto valueKey=materialGraphValueHash(*graph,output.shading,output.blend,target.node);
            if(state.valueKey!=valueKey) {
                state.pendingInputs.clear();
                // Let the bounded display job finish while accepting the newest
                // definition. Canceling it on every mouse delta starves live
                // sliders; starting another would build a backlog of old values.
                if(!state.displayPending) {
                    if(!state.textureKey.empty())textureStreamer_->forgetGenerated(state.textureKey);
                    state.textureKey.clear();state.pending=false;
                }
                state.sourceKey.clear();state.sourceExtent={};
                state.valueKey=valueKey;state.sources=materialGraphImagePreviewSources(*graph,target);
            }
        } catch(const std::exception& error){
            state.diagnostic=error.what();state.pending=false;state.displayPending=false;
            if(!state.textureKey.empty())textureStreamer_->forgetGenerated(state.textureKey);
            state.textureKey.clear();
        }
    }
    if(!state.diagnostic.empty())throw std::runtime_error(state.diagnostic);
    if(state.displayPending) {
        if(auto active=textureStreamer_->cachedGeneratedFloat(state.textureKey,edge,edge,false,edge,priority)) {
            if(active->sourceReady&&active->complete&&bgfx::isValid(active->handle)) {
                state.current=std::make_shared<const PreparedTexture>(*active);
                state.pending=false;state.displayPending=false;
            } else if(!active->failed)return waiting("Updating node image...");
        }
        state.pending=false;state.displayPending=false;
    }
    // Throttle submissions, not edits: continuous gestures still publish
    // intermediate results, with at most one display bake per visible target.
    const auto interval=std::chrono::milliseconds{edge>=256?16:33};
    if(state.used-state.requested<interval)return waiting("Updating node image...");
    auto contentHash=state.valueKey;
    streaming::detail::hash(contentHash,material_procedural::algorithm_revision);
    streaming::detail::hash(contentHash,target.pin);streaming::detail::hash(contentHash,edge);
    streaming::detail::hash(contentHash,documentId.data(),documentId.size());
    struct Input {std::string original,resolved;ResizeImageSource* source;std::uint64_t fingerprint;};
    std::vector<Input> inputs;inputs.reserve(state.sources.size());bool ready=true;
    for(const auto& original:state.sources) {
        const auto resolved=references_.resolve(original);const bool builtin=resolved==GrayTextureId||resolved==NeutralNormalTextureId;
        const auto* asset=findMutable(resolved);
        if(resolved.empty()||(!builtin&&(!asset||asset->hdr)))throw std::runtime_error("Node preview input is unavailable: "+original);
        auto& source=resizeImageSources_[resolved];source.used=state.used;
        const auto file=builtin?std::filesystem::path{}:resolveAssetPath(projectRoot_,resolved);
        const bool moved=!source.file.empty()&&source.file!=file;
        if(source.file!=file){source.file=file;source.fingerprint={};source.pixels={};source.reloadPixels=moved;}
        if(!source.fingerprint) {
            ResourceRequestOptions config;config.variant="resize-input-fingerprint-v1";config.priority=priority;
            config.forceReload=moved;if(!builtin)config.dependencies={file};
            auto builtinKey=streaming::detail::HashSeed;streaming::detail::hash(builtinKey,resolved.data(),resolved.size());
            source.fingerprint=resources_->request<streaming::TextureMipCache>(AssetRef<streaming::TextureMipCache>{resolved},[builtin,builtinKey](std::stop_token stop) {
                if(stop.stop_requested())throw std::runtime_error("Node preview source request canceled.");
                auto data=std::make_shared<streaming::TextureMipCache>();return ResourceLoadResult<streaming::TextureMipCache>{data,sizeof(*data),builtin?builtinKey:0,{}};
            },std::move(config));
        }
        if(source.fingerprint.state()==ResourceState::Failed)throw std::runtime_error(source.fingerprint.diagnostic());
        const auto version=source.fingerprint.snapshot();
        if(!version||source.fingerprint.state()!=ResourceState::Ready){ready=false;continue;}
        streaming::detail::hash(contentHash,original.data(),original.size());streaming::detail::hash(contentHash,resolved.data(),resolved.size());
        streaming::detail::hash(contentHash,version.fingerprint());streaming::detail::hash(contentHash,version.revision());
        inputs.push_back({original,resolved,&source,version.fingerprint()});
    }
    if(!ready)return waiting("Loading node image inputs...");
    const auto key="material-node-image-v4:"+std::to_string(contentHash);
    if(state.textureKey!=key) {
        state.pendingInputs.clear();
        if(!state.textureKey.empty())textureStreamer_->forgetGenerated(state.textureKey);
        state.textureKey=key;state.pending=false;
    }
    const auto publish=[&](PreparedTexture result) {
        if(result.sourceReady&&result.complete&&bgfx::isValid(result.handle)) {
            state.current=std::make_shared<const PreparedTexture>(result);state.pending=false;
        } else if(result.failed)state.pending=false;
        if((!result.sourceReady||!result.complete)&&state.current) {
            const auto diagnostic=result.diagnostic;const bool failed=result.failed;result=*state.current;result.sourceReady=false;result.complete=false;result.failed=failed;
            result.diagnostic=diagnostic.empty()?"Updating node image...":diagnostic;
        }
        return result;
    };
    if(auto cached=textureStreamer_->cachedGeneratedFloat(key,edge,edge,false,edge,priority)) {
        state.pendingInputs.clear();
        // Keep the shared authored source's CPU descriptor demanded while its
        // thumbnails are visible, allowing a second display size to reuse it.
        if(!state.sourceKey.empty())textureStreamer_->cachedGeneratedFloat(state.sourceKey,state.sourceExtent[0],state.sourceExtent[1],true,edge,priority);
        return publish(*cached);
    }
    std::shared_ptr<const DecodedImage> sourceMip;
    const auto* targetNode=findMaterialNode(*graph,target.node);
    if(target.pin==MaterialPin::Value&&targetNode&&isMaterialImageBakeNode(targetNode->kind)&&targetNode->kind!=MaterialNodeKind::ImageTransform) {
        const auto pendingOthers=std::count_if(nodeImagePreviews_.begin(),nodeImagePreviews_.end(),[&](const auto& entry){return &entry.second!=&state&&(entry.second.pending||!entry.second.pendingInputs.empty());});
        const MaterialGraphResizeBinding binding{graph,target.node,state.valueKey};
        CachedImageRequestInfo info;std::unordered_map<std::string,std::uint64_t> versions;
        const auto source=preparedResizeTexture(binding,edge,priority,versions,&info,nodeImageStarts_<2&&(priority==ResourcePriority::Selected||pendingOthers<8));
        if(info.started)++nodeImageStarts_;
        state.sourceKey=info.key;state.sourceExtent=info.extent;
        if(source.failed){state.pending=false;return publish(source);}
        const auto snapshot=info.requested?textureStreamer_->generatedFloatSnapshot(info.key,info.extent[0],info.extent[1],true):ResourceSnapshot<streaming::TextureMipCache>{};
        if(snapshot&&snapshot->format==ImagePixelFormat::RGBA16F&&!snapshot->memory.empty()&&snapshot->memory.size()==snapshot->levels.size()) {
            const auto& mips=snapshot->memory;std::size_t level{};
            const auto requiredWidth=std::min(mips.front().width,edge),requiredHeight=std::min(mips.front().height,edge);
            while(level+1<mips.size()&&mips[level+1].width>=requiredWidth&&mips[level+1].height>=requiredHeight)++level;
            // Aliasing pins the immutable descriptor without copying its full
            // source. Only this small selected mip is decoded by the UI job.
            sourceMip=std::shared_ptr<const DecodedImage>{snapshot.shared(),&mips[level]};
        } else if(!source.sourceReady||!source.complete) {
            state.pending=info.requested;
            return waiting(source.diagnostic.empty()?"Preparing node image source...":source.diagnostic);
        }
        // A ready GPU generation can outlive its CPU descriptor. In that case
        // use the canonical source-exact branch bake below, without readback.
        // Release this source-pending slot before testing the tiny-job limit:
        // otherwise eight completed sources could prevent every display job.
        state.pending=false;
    }
    const auto pending=std::count_if(nodeImagePreviews_.begin(),nodeImagePreviews_.end(),[](const auto& entry){return entry.second.pending||!entry.second.pendingInputs.empty();});
    if(nodeImageStarts_>=2||(priority!=ResourcePriority::Selected&&pending>=8&&state.pendingInputs.empty()))return waiting("Waiting for node image generation...");
    const auto type=materialOutputType(*graph,target.node,target.pin);
    if(sourceMip) {
        state.pendingInputs.clear();
        ++nodeImageStarts_;state.pending=true;state.displayPending=true;state.requested=state.used;
        return publish(textureStreamer_->requestGeneratedFloat(key,[sourceMip,edge,type](std::stop_token stop) {
            auto image=nodePreviewMipImage(*sourceMip,edge,stop);encodeNodeImageDisplay(image,type,stop);return image;
        },edge,edge,false,edge,priority));
    }
    auto images=std::make_shared<std::unordered_map<std::string,std::shared_ptr<const DecodedImage>>>();
    for(const auto& input:inputs) {
        auto& source=*input.source;const bool builtin=input.resolved==GrayTextureId||input.resolved==NeutralNormalTextureId;
        if(!source.pixels)if(const auto held=state.pendingInputs.find(input.original);held!=state.pendingInputs.end())source.pixels=held->second;
        if(!source.pixels) {
            ResourceRequestOptions config;config.variant="source";config.priority=priority;config.forceReload=source.reloadPixels;
            source.reloadPixels=false;if(!builtin)config.dependencies={source.file};
            const auto file=source.file;const auto resolved=input.resolved;const auto fingerprint=input.fingerprint;
            source.pixels=resources_->request<DecodedImage>(AssetRef<DecodedImage>{resolved},[file,resolved,builtin,fingerprint](std::stop_token stop) {
                if(stop.stop_requested())throw std::runtime_error("Node preview image decode canceled.");
                auto pixels=std::make_shared<DecodedImage>(builtin?DecodedImage{1,1,resolved==NeutralNormalTextureId?std::vector<std::uint8_t>{128,128,255,255}:std::vector<std::uint8_t>{128,128,128,255}}:decodeManagedImage(file));
                return ResourceLoadResult<DecodedImage>{pixels,pixels->rgba.size(),builtin?fingerprint:0,{}};
            },std::move(config));
        }
        state.pendingInputs[input.original]=source.pixels;
        if(source.pixels.state()==ResourceState::Failed)throw std::runtime_error(source.pixels.diagnostic());
        const auto pixels=source.pixels.snapshot();
        if(!pixels||source.pixels.state()!=ResourceState::Ready){ready=false;continue;}
        if(pixels.fingerprint()!=input.fingerprint){state.pendingInputs.erase(input.original);source.pixels={};source.reloadPixels=true;ready=false;continue;}
        images->emplace(input.original,pixels.shared());
    }
    if(!ready)return waiting("Loading node image inputs...");
    state.pendingInputs.clear(); // The worker's immutable pixel snapshots now own the inputs.
    ++nodeImageStarts_;state.pending=true;state.displayPending=true;state.requested=state.used;
    return publish(textureStreamer_->requestGeneratedFloat(key,[graph,target,images,options,type](std::stop_token stop) {
        auto image=bakeMaterialGraphImagePreview(*graph,target,[images](const std::string& id) {
            const auto found=images->find(id);if(found==images->end())throw std::runtime_error("Node preview source snapshot is unavailable: "+id);return found->second;
        },options,stop);
        encodeNodeImageDisplay(image,type,stop);
        return image;
    },edge,edge,false,edge,priority));
}
PreparedTexture AssetLibrary::grungePreview(const MaterialGrungeNode& settings,std::uint32_t edge,MaterialValueType type) const {
    if(!textureStreamer_) {PreparedTexture result;result.diagnostic="Generated texture owner is unavailable.";return result;}
    const auto key=std::to_string(grungeTextureKey(settings,GrungeTextureEdge));
    const bool color=type==MaterialValueType::Color||type==MaterialValueType::Color4;
    return textureStreamer_->requestGenerated(key,[settings]{return generateGrungeTexture(settings,GrungeTextureEdge);},GrungeTextureEdge,true,std::clamp(edge,1u,GrungeTextureEdge),ResourcePriority::Selected,color);
}
std::unique_ptr<MaterialGraphPrograms> AssetLibrary::createMaterialNodePreviewPrograms() const {
    return graphPrograms_?graphPrograms_->createSibling():nullptr;
}
ModelMaterialTextures AssetLibrary::materialNodePreviewBindings(MaterialGraphPrograms& owner,const MaterialAsset& material,MaterialGraphPreviewTarget target,std::uint32_t textureEdge) const {
    return preparedGraphBindings(owner,generateMaterialGraphPreviewShader(material,target),material.graph,std::max(1u,textureEdge),ResourcePriority::Selected);
}

void AssetLibrary::loadMaterial(Material& material) {
    const auto file = resolveAssetPath(projectRoot_,"assets/material.json");
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
        std::ofstream output(resolveAssetPath(projectRoot_,"assets/material.json"));
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
    const auto file = resolveAssetPath(projectRoot_,"assets/environment.json");
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
        writeFileAtomically(resolveAssetPath(projectRoot_,"assets/environment.json"),json.dump(2)+'\n');
    } catch (const std::exception& error) { status_ = error.what(); }
}

namespace {
std::vector<SceneSnapshot> savedScenes(const std::filesystem::path& root) {
    std::vector<SceneSnapshot> result;
    std::vector<std::string> diagnostics;const auto entries=scanSceneAssets(root,&diagnostics);
    if(!diagnostics.empty())throw std::runtime_error("Cannot inspect all saved scene references: "+diagnostics.front());
    for(const auto& entry:entries){
        const auto& id=entry.id;try{result.push_back(readSceneSnapshot(root,id));}
        catch(const std::exception& error){throw std::runtime_error("Cannot inspect scene "+id+": "+error.what());}
    }return result;
}
uint64_t mix(uint64_t value,uint64_t input){return (value^input)*1099511628211ull;}
std::optional<std::string> referencePolicyBytes(const std::filesystem::path& root){
    const auto file=resolveAssetPath(root,"assets/references.json");
    if(!std::filesystem::exists(file))return {};
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read the asset reference policy.");
    std::string bytes{std::istreambuf_iterator<char>(input),{}};
    if(!input.eof()&&input.fail())throw std::runtime_error("Cannot read the complete asset reference policy.");
    return bytes;
}
std::optional<std::string> currentReferencePolicy(const std::filesystem::path& root,const AssetReferenceManager& references){
    const auto baseline=referencePolicyBytes(root);const AssetReferenceManager persisted(root);
    if(!persisted.valid())throw std::runtime_error(persisted.status());
    if(!references.valid()||persisted.serialize()!=references.serialize()||referencePolicyBytes(root)!=baseline)
        throw std::runtime_error("The asset reference policy changed outside this editor. Reopen the project before deleting assets.");
    return baseline;
}
uint64_t deletionRevision(const std::filesystem::path& root,const AssetReferenceManager& references,uint64_t catalogRevision,const SceneDocument* active){
    auto result=mix(1469598103934665603ull,references.revision());result=mix(result,catalogRevision);
    const auto policy=referencePolicyBytes(root);result=mix(result,policy.has_value());
    if(policy)for(const auto c:*policy)result=mix(result,static_cast<uint8_t>(c));
    if(active){result=mix(result,active->scene().revision());result=mix(result,active->scene().generation());result=mix(result,active->history().size());}
    for(const auto& directory:{root/"Content",root/"Engine"})if(std::filesystem::exists(directory))
        for(const auto& item:std::filesystem::recursive_directory_iterator(directory))if(item.is_regular_file()){
            for(const auto c:utf8(item.path().lexically_relative(root)))result=mix(result,static_cast<uint8_t>(c));
            result=mix(result,item.file_size());result=mix(result,static_cast<uint64_t>(item.last_write_time().time_since_epoch().count()));}
    const auto browser=resolveAssetPath(root,"assets/browser.json");if(std::filesystem::exists(browser)){std::ifstream input(browser,std::ios::binary);for(char c;input.get(c);)result=mix(result,static_cast<uint8_t>(c));}
    return result;
}
size_t removeDeletedModels(SceneSnapshot& snapshot,const AssetReferenceManager& previous,const AssetReferenceManager& next,const std::unordered_set<std::string>& deletedModels){
    Scene source;source.apply(snapshot.records,snapshot.properties);std::unordered_set<EntityId> removed;
    std::unordered_map<EntityId,std::size_t> ranks;const auto preorder=source.preorder();for(size_t index=0;index<preorder.size();++index)ranks.emplace(preorder[index],index);
    std::unordered_set<EntityId> changedParents;
    for(const auto& record:snapshot.records)if(record.model&&deletedModels.contains(previous.resolve(record.model->id))&&next.resolve(record.model->id).empty())removed.insert(record.id);
    size_t clearedCollisions{};
    for(auto& record:snapshot.records)if(record.collider && !record.collider->source.empty() &&
        deletedModels.contains(previous.resolve(record.collider->source.id))) {
        const auto replacement=next.resolve(record.collider->source.id);
        if(record.collider->source.id!=replacement){record.collider->source=AssetRef<ModelAsset>{replacement};++clearedCollisions;}
    }
    if(removed.empty())return clearedCollisions;
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
    for(auto& record:snapshot.records)if(record.cameraFollow && removed.contains(record.cameraFollow->target))record.cameraFollow.reset();
    // Stable ordering among surviving siblings; newly promoted children append in original preorder.
    for(const auto parent:changedParents) {
        std::vector<EntityRecord*> siblings;for(auto& record:snapshot.records)if(record.parent==parent)siblings.push_back(&record);
        std::sort(siblings.begin(),siblings.end(),[&](const auto* left,const auto* right){return ranks.at(left->id)<ranks.at(right->id);});
        for(size_t index=0;index<siblings.size();++index)siblings[index]->order=static_cast<uint32_t>(index);
    }
    Scene::validate(snapshot.records,snapshot.properties);return removed.size()+clearedCollisions;
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
    for(auto& record:snapshot.records)for(auto& assigned:record.modelMaterials)if(!assigned.empty() && deletedMaterials.contains(previous.resolveMaterial(assigned.id))) {
        const auto replacement=next.resolveModelMaterial(assigned.id);
        const auto material=loadMaterialAsset(root,replacement);
        if(!modelMaterialCompatible(material))throw std::runtime_error("Mesh material is invalid: "+material.name);
        if(assigned.id!=replacement){assigned=AssetRef<MaterialAsset>{replacement};++changed;}
    }
    Scene::validate(snapshot.records,snapshot.properties);return changed;
}
size_t replaceDeletedNavigationProfiles(SceneSnapshot& snapshot,const AssetReferenceManager& previous,const AssetReferenceManager& next,
    const std::unordered_set<std::string>& deletedProfiles) {
    size_t changed{};
    const auto replace=[&](AssetRef<NavigationAgentProfile>& profile) {
        if(!deletedProfiles.contains(previous.resolveNavigationAgentProfile(profile.id)))return;
        const auto replacement=next.resolveNavigationAgentProfile(profile.id);if(profile.id!=replacement){profile.id=replacement;++changed;}
    };
    for(auto& record:snapshot.records) {
        if(record.controller)replace(record.controller->profile);
        if(record.navigationVolume) {
            std::unordered_set<std::string> kept;auto& profiles=record.navigationVolume->profiles;
            for(auto& profile:profiles)replace(profile);
            std::erase_if(profiles,[&](const auto& profile){return !kept.insert(profile.id).second;});
        }
    }
    Scene::validate(snapshot.records,snapshot.properties);return changed;
}
size_t replaceDeletedGameplayEffects(SceneSnapshot& snapshot,const AssetReferenceManager& previous,const AssetReferenceManager& next,
    const std::unordered_set<std::string>& deletedEffects) {
    size_t changed{};
    for(auto& record:snapshot.records)if(record.effects) {
        auto& starting=record.effects->starting;
        for(auto& effect:starting)if(deletedEffects.contains(previous.resolveGameplayEffect(effect.id))) {
            const auto replacement=next.resolveGameplayEffect(effect.id);
            if(effect.id!=replacement){effect.id=replacement;++changed;}
        }
        std::erase_if(starting,[](const auto& effect){return effect.empty();});
    }
    Scene::validate(snapshot.records,snapshot.properties);return changed;
}
size_t replaceDeletedAnimations(SceneSnapshot& snapshot,const AssetReferenceManager& previous,const AssetReferenceManager& next,
    const std::unordered_set<std::string>& deletedAnimations) {
    size_t changed{};
    for(auto& record:snapshot.records)if(record.model&&!record.modelAnimation.asset.empty()&&
        deletedAnimations.contains(previous.resolveAnimation(record.modelAnimation.asset.id))) {
        const auto replacement=next.resolveAnimation(record.modelAnimation.asset.id);
        if(record.modelAnimation.asset.id!=replacement){record.modelAnimation.asset.id=replacement;++changed;}
    }
    Scene::validate(snapshot.records,snapshot.properties);return changed;
}
}
void AssetLibrary::refreshReferenceIndex(const SceneDocument* active){
    std::vector<AssetUsage> usages;
    std::unordered_set<std::string> usableMaterials;
    for(const auto& entry:catalog_)if(entry.kind==AssetKind::Material&&std::filesystem::is_regular_file(resolveAssetPath(projectRoot_,entry.id)))usableMaterials.insert(entry.id);
    for(const auto& stored:materials_){const auto material=stored->id==materialDocumentId_?std::make_shared<MaterialAsset>(loadMaterialAsset(projectRoot_,stored->id)):stored;
        visitMaterialTextures(material->surface,false,[&](const auto& id,MaterialTextureRole role){
            usages.push_back({id,material->id,material->name,role==MaterialTextureRole::Normal?"Normal texture":"Graph texture",false,false,!usableMaterials.contains(material->id)});
        });
    }
    for(const auto& entry:catalog_)if(entry.kind==AssetKind::Particle){const auto effect=loadParticleAsset(projectRoot_,entry.id);
        if(!effect.emitter.material.empty()&&!isEngineAssetId(effect.emitter.material.id))usages.push_back({effect.emitter.material.id,effect.id,effect.name,"Particle material"});}
    for(const auto& entry:catalog_)if(entry.kind==AssetKind::Animation) {
        const auto animation=loadAnimationAsset(projectRoot_,entry.id);
        usages.push_back({animation.sourceModel.id,animation.id,animation.name,"Animation source model"});
    }
    for(const auto& id:animationDocumentReferences_)if(!id.empty())usages.push_back({id,animationDocumentId_,"Open animation","Animation source model",true,false});
    for(const auto& id:animationDocumentHistory_)if(!id.empty())usages.push_back({id,animationDocumentId_,"Open animation","Animation source model undo / redo history",true,true});
    for(const auto& id:particleDocumentReferences_)if(!id.empty()&&!isEngineAssetId(id))usages.push_back({id,particleDocumentId_,"Open particle effect","Particle material",true,false});
    for(const auto& id:particleDocumentHistory_)if(!id.empty()&&!isEngineAssetId(id))usages.push_back({id,particleDocumentId_,"Open particle effect","Particle material undo / redo history",true,true});
    for(const auto& id:materialDocumentReferences_)if(!id.empty()&&!isEngineAssetId(id))usages.push_back({id,materialDocumentId_,"Open material","Material texture",true,false});
    for(const auto& id:materialDocumentHistory_)if(!id.empty()&&!isEngineAssetId(id))usages.push_back({id,materialDocumentId_,"Open material","Material texture undo / redo history",true,true});
    // Inspect lightweight material metadata for every package, including models
    // never opened in the viewport. Authored usage cannot depend on residency.
    std::set<std::string> referencedModels;
    for(const auto& entry:catalog_)if(entry.kind==AssetKind::Model)referencedModels.insert(entry.id);
    // Hidden old geometry still owns material/image dependencies while saved
    // scenes, live entities or history can load that generation.
    for(const auto& id:retainedQuaderModels_)if(!references_.deleted(id))referencedModels.insert(id);
    for(const auto& id:referencedModels) {
        const auto model=loadModelMaterialMetadata(resolveAssetPath(projectRoot_,id),projectRoot_,modelResolvers());
        for(size_t index=0;index<model->materials.size();++index) {
            const auto materialId=index<model->materialIds.size()?model->materialIds[index]:importedMaterialId(model->id,index);
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
            if(record.model&&!record.modelAnimation.asset.empty())usages.push_back({record.modelAnimation.asset.id,id,name,record.name+" / Animation",isActive,false});
            if(record.effects)for(const auto& effect:record.effects->starting)usages.push_back({effect.id,id,name,record.name+" / Starting Effect",isActive,false});
            if(record.controller)usages.push_back({record.controller->profile.id,id,name,record.name+" / Navigation Agent Profile",isActive,false});
            if(record.navigationVolume)for(const auto& profile:record.navigationVolume->profiles)usages.push_back({profile.id,id,name,record.name+" / Supported Agent Profile",isActive,false});
            if(record.collider && !record.collider->source.empty())usages.push_back({record.collider->source.id,id,name,record.name+" / Collider source",isActive,false});
            for(size_t slot=0;slot<record.modelMaterials.size();++slot)if(!record.modelMaterials[slot].empty())
                usages.push_back({record.modelMaterials[slot].id,id,name,record.name+" / Material slot "+std::to_string(slot+1),isActive,false});
            if(record.particle&&!record.particle->empty())usages.push_back({record.particle->id,id,name,record.name+" / Particle effect",isActive,false});
            if(record.environment&&!record.environment->environment.map.empty())usages.push_back({record.environment->environment.map,id,name,record.name+" / Environment",isActive,false});}
    };
    for(const auto& scene:savedScenes(projectRoot_))addScene(scene.records,scene.id,scene.name,false);
    if(active){addScene(active->scene().records(),active->assetId(),active->displayName(),true);
        for(const auto& id:active->historicalModelReferences())usages.push_back({id,active->assetId(),active->displayName(),"Undo / redo history",true,true});
        for(const auto& id:active->historicalMaterialReferences())usages.push_back({id,active->assetId(),active->displayName(),"Undo / redo history / Material slot",true,true});
        for(const auto& id:active->historicalParticleReferences())usages.push_back({id,active->assetId(),active->displayName(),"Particle undo / redo history",true,true});}
    if(active)for(const auto& id:active->historicalNavigationAgentProfileReferences())usages.push_back({id,active->assetId(),active->displayName(),"Navigation profile undo / redo history",true,true});
    if(active)for(const auto& id:active->historicalGameplayEffectReferences())usages.push_back({id,active->assetId(),active->displayName(),"Starting effects undo / redo history",true,true});
    if(active)for(const auto& id:active->historicalAnimationReferences())usages.push_back({id,active->assetId(),active->displayName(),"Animation undo / redo history",true,true});
    for(const auto& file:{"material.json","environment.json","preview.json","editor-session.json"}){
        const auto source=resolveAssetPath(projectRoot_,"assets/"+std::string(file));if(!std::filesystem::exists(source))continue;
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
void AssetLibrary::refreshQuaderModelCatalog(){
    // Keep pruned IDs retired for this session as well: a completed background
    // snapshot must not republish a card after its unused source was reclaimed.
    auto inactive=inactiveQuaderModels_;std::unordered_set<std::string> retained;
    for(const auto& entry:storedAssetPaths(projectRoot_)){
        if(entry.file.extension()!=".crimson-quader"||!std::filesystem::is_regular_file(entry.file))continue;
        std::ifstream input(entry.file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read Quader bundle catalog.");
        const std::string bytes{std::istreambuf_iterator<char>(input),{}};const auto bundle=decodeQuaderSceneBundle(bytes);
        if(bundle.id!=entry.id)throw std::runtime_error("Quader bundle catalog identity mismatch.");
        for(const auto& model:bundle.models)if(model.current)inactive.erase(model.id);else {inactive.insert(model.id);retained.insert(model.id);}
    }
    const auto count=catalog_.size();std::erase_if(catalog_,[&](const auto& entry){return entry.kind==AssetKind::Model&&inactive.contains(entry.id);});
    if(inactive!=inactiveQuaderModels_||count!=catalog_.size())++catalogRevision_;
    inactiveQuaderModels_=std::move(inactive);
    retainedQuaderModels_=std::move(retained);
}
void AssetLibrary::collectUnusedQuaderModels(const SceneDocument& active){
    if(active.transactionActive())return;
    const auto state=std::array<std::uint64_t,8>{catalogRevision_,contentStorageRevision(projectRoot_),active.documentGeneration(),
        active.scene().revision(),active.scene().generation(),active.loadedFingerprint(),static_cast<std::uint64_t>(active.history().size()),references_.revision()};
    if(quaderCollectionInitialized_&&state==quaderCollectionState_)return;
    // Most scene revisions are poses/properties. Rescan saved dependencies only
    // when model references/history or an external catalog boundary changed.
    auto documentReferences=active.historicalModelReferences();
    for(const auto& record:active.scene().records()){
        if(record.model)documentReferences.push_back(record.model->id);
        if(record.collider&&!record.collider->source.empty())documentReferences.push_back(record.collider->source.id);
    }
    std::sort(documentReferences.begin(),documentReferences.end());documentReferences.erase(std::unique(documentReferences.begin(),documentReferences.end()),documentReferences.end());
    const bool unchanged=quaderCollectionInitialized_&&documentReferences==quaderDocumentModelReferences_&&state[0]==quaderCollectionState_[0]&&
        state[1]==quaderCollectionState_[1]&&state[2]==quaderCollectionState_[2]&&state[5]==quaderCollectionState_[5]&&state[7]==quaderCollectionState_[7];
    quaderCollectionInitialized_=true;quaderCollectionState_=state;
    quaderDocumentModelReferences_=std::move(documentReferences);
    if(unchanged||retainedQuaderModels_.empty())return;
    try{
        // Failure to read any scene/material/legacy reference leaves every
        // retained generation intact. Runtime residency is not authored usage.
        // Read the durable policy again at this destructive boundary. An
        // external edit cannot silently discard replacement-only owners.
        AssetReferenceManager persistedPolicy(projectRoot_);
        if(!references_.valid()||!persistedPolicy.valid())throw std::runtime_error("Asset reference policy is invalid; retained meshes were preserved.");
        refreshReferenceIndex(&active);auto referenced=references_.replacementTargets();
        const auto persistedTargets=persistedPolicy.replacementTargets();referenced.insert(referenced.end(),persistedTargets.begin(),persistedTargets.end());
        for(const auto& usage:references_.usages()){
            referenced.push_back(usage.targetId);const auto resolved=references_.resolve(usage.targetId);if(!resolved.empty())referenced.push_back(resolved);
        }
        std::unordered_map<std::string,std::filesystem::path> sources;
        for(const auto& id:retainedQuaderModels_)if(const auto source=quaderBundleSource(projectRoot_,id))sources.emplace(id,*source);
        const auto removed=pruneUnusedQuaderBundleModels(projectRoot_,referenced);
        if(removed.empty())return;
        std::set<std::filesystem::path> writtenSources;
        for(const auto& id:removed){
            inactiveQuaderModels_.insert(id);retainedQuaderModels_.erase(id);modelResources_.erase(id);materialCatalogResources_.erase(id);registeredModelRevisions_.erase(id);modelInterest_.erase(id);
            if(resources_)resources_->discard(id);
            if(const auto source=sources.find(id);source!=sources.end())writtenSources.insert(source->second);
        }
        for(const auto& source:writtenSources)ownWrites_.push_back(source);
        std::erase_if(models_,[&](const auto& model){return std::find(removed.begin(),removed.end(),model->id)!=removed.end();});
        ++catalogRevision_;
        quaderCollectionState_={catalogRevision_,contentStorageRevision(projectRoot_),active.documentGeneration(),active.scene().revision(),
            active.scene().generation(),active.loadedFingerprint(),static_cast<std::uint64_t>(active.history().size()),references_.revision()};
    }catch(const std::exception& error){status_="Quader mesh cleanup deferred: "+std::string(error.what());}
}
AssetDeletionPlan AssetLibrary::planDeletion(const std::vector<std::string>& ids,const SceneDocument* active){
    AssetDeletionPlan plan;plan.selectedIds=ids;
    try{(void)currentReferencePolicy(projectRoot_,references_);auto& browser=browserCatalog();std::unordered_set<std::string> expanded;
        for(const auto& id:ids){if(!browser.contains(id))throw std::runtime_error("A selected asset no longer exists.");expanded.insert(id);
            if(browser.isFolder(id))for(const auto& child:browser.descendants(id))expanded.insert(child);}
        std::unordered_set<std::string> models,particles,materials,profiles,effects,animations;
        for(const auto& id:expanded){if(browser.isFolder(id)){plan.folderIds.push_back(id);continue;}
            const auto entry=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& a){return a.id==id;});if(entry==catalog_.end())throw std::runtime_error("Asset deletion target is unavailable.");
            plan.assetIds.push_back(id);if(entry->kind==AssetKind::Model)models.insert(id);if(entry->kind==AssetKind::Particle)particles.insert(id);if(entry->kind==AssetKind::Material)materials.insert(id);if(entry->kind==AssetKind::NavigationAgentProfile)profiles.insert(id);if(entry->kind==AssetKind::GameplayEffect)effects.insert(id);if(entry->kind==AssetKind::Animation)animations.insert(id);}
        for(const auto& entry:catalog_)if(entry.kind==AssetKind::Animation&&!expanded.contains(entry.id)) {
            const auto animation=loadAnimationAsset(projectRoot_,entry.id);
            if(models.contains(animation.sourceModel.id))throw std::runtime_error("Delete the model's Animation assets in the same batch to preserve their imported source tracks.");
        }
        for(const auto& id:animationDocumentReferences_)if(models.contains(id))throw std::runtime_error("Close the Animation document before deleting its source model.");
        for(const auto& id:animationDocumentHistory_)if(models.contains(id))throw std::runtime_error("Close the Animation document before deleting a source model retained by its history.");
        refreshReferenceIndex(active);
        std::unordered_set<std::string> usableOwners;
        for(const auto& entry:catalog_)if(std::filesystem::is_regular_file(resolveAssetPath(projectRoot_,entry.id)))usableOwners.insert(entry.id);
        for(const auto& id:retainedQuaderModels_)if(!references_.deleted(id)&&std::filesystem::is_regular_file(resolveAssetPath(projectRoot_,id)))usableOwners.insert(id);
        // A valid saved scene can arrive from disk before the browser's watcher
        // refresh. Its actual authored dependencies must still be protected.
        const auto scenes=savedScenes(projectRoot_);for(const auto& scene:scenes)usableOwners.insert(scene.id);
        for(const auto& id:plan.assetIds){const auto users=references_.users(id);plan.usages.insert(plan.usages.end(),users.begin(),users.end());
            const auto authored=references_.authoredUsers(id,plan.assetIds);for(const auto& usage:authored)if(usableOwners.contains(usage.ownerId))plan.warningUsages.push_back(usage);}
        for(const auto& scene:scenes){size_t count=0;for(const auto& entity:scene.records)
            if((entity.model&&models.contains(references_.resolve(entity.model->id)))||(entity.model&&!entity.modelAnimation.asset.empty()&&animations.contains(references_.resolveAnimation(entity.modelAnimation.asset.id)))||(entity.collider&&!entity.collider->source.empty()&&models.contains(references_.resolve(entity.collider->source.id)))||(entity.particle&&!entity.particle->empty()&&particles.contains(references_.resolveParticle(entity.particle->id)))||
                (entity.effects&&std::any_of(entity.effects->starting.begin(),entity.effects->starting.end(),[&](const auto& effect){return effects.contains(references_.resolveGameplayEffect(effect.id));}))||
                (entity.controller&&profiles.contains(references_.resolveNavigationAgentProfile(entity.controller->profile.id)))||
                (entity.navigationVolume&&std::any_of(entity.navigationVolume->profiles.begin(),entity.navigationVolume->profiles.end(),[&](const auto& profile){return profiles.contains(references_.resolveNavigationAgentProfile(profile.id));}))||
                std::any_of(entity.modelMaterials.begin(),entity.modelMaterials.end(),[&](const auto& assigned){return !assigned.empty()&&materials.contains(references_.resolveMaterial(assigned.id));}))++count;
            if(count)plan.scenes.push_back({scene.id,scene.name,count});}
        if(active){for(const auto& entity:active->scene().records())if((entity.model&&models.contains(references_.resolve(entity.model->id))) || (entity.collider&&!entity.collider->source.empty()&&models.contains(references_.resolve(entity.collider->source.id))))plan.clearsActiveHistory=true;
            for(const auto& id:active->historicalModelReferences())if(models.contains(references_.resolve(id)))plan.clearsActiveHistory=true;
            for(const auto& entity:active->scene().records())for(const auto& assigned:entity.modelMaterials)
                if(!assigned.empty()&&materials.contains(references_.resolveMaterial(assigned.id)))plan.clearsActiveHistory=true;
            for(const auto& id:active->historicalMaterialReferences())if(materials.contains(references_.resolveMaterial(id)))plan.clearsActiveHistory=true;
            for(const auto& entity:active->scene().records())if(entity.particle&&!entity.particle->empty()&&particles.contains(references_.resolveParticle(entity.particle->id)))plan.clearsActiveHistory=true;
            for(const auto& id:active->historicalParticleReferences())if(particles.contains(references_.resolveParticle(id)))plan.clearsActiveHistory=true;}
        if(active)for(const auto& id:active->historicalNavigationAgentProfileReferences())if(profiles.contains(references_.resolveNavigationAgentProfile(id)))plan.clearsActiveHistory=true;
        if(active)for(const auto& id:active->historicalGameplayEffectReferences())if(effects.contains(references_.resolveGameplayEffect(id)))plan.clearsActiveHistory=true;
        if(active)for(const auto& id:active->historicalAnimationReferences())if(animations.contains(references_.resolveAnimation(id)))plan.clearsActiveHistory=true;
        std::sort(plan.assetIds.begin(),plan.assetIds.end());std::sort(plan.folderIds.begin(),plan.folderIds.end());
        plan.revision=deletionRevision(projectRoot_,references_,catalogRevision_,active);
    }catch(const std::exception& e){plan.error=e.what();}return plan;
}
std::vector<AssetEntry> AssetLibrary::fallbackCandidates(const std::string& id)const{
    std::vector<AssetEntry> result;const auto source=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& e){return e.id==id;});if(source==catalog_.end())return result;
    if(source->kind==AssetKind::Scene)return result;
    bool sprite=false,model=false;
    if(source->kind==AssetKind::Material)for(const auto& usage:references_.users(id))if(usage.targetId==id||references_.resolve(usage.targetId)==id){
        const auto owner=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& e){return e.id==usage.ownerId;});
        sprite|=owner!=catalog_.end()&&owner->kind==AssetKind::Particle;model|=(owner!=catalog_.end()&&owner->kind==AssetKind::Model)||retainedQuaderModels_.contains(usage.ownerId)||usage.slot.find(" / Material slot")!=std::string::npos;}
    for(const auto& entry:catalog_)if(entry.id!=id&&(entry.kind==source->kind||(source->kind==AssetKind::Environment&&entry.kind==AssetKind::Texture))){
        if(source->kind==AssetKind::Material){const auto material=findMaterial(entry.id);if(!material||(sprite&&!particleMaterialCompatible(*material))||(model&&!modelMaterialCompatible(*material)))continue;}
        if(source->kind==AssetKind::Animation){const auto original=findAnimation(id),candidate=findAnimation(entry.id);if(!original||!candidate||original->bindingSignature!=candidate->bindingSignature)continue;}
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
        const auto policyBaseline=currentReferencePolicy(projectRoot_,references_);
        if(active&&active->transactionActive())throw std::runtime_error("Finish the active scene edit before deleting assets.");
        if(pendingDeletion_&&(pendingDeletion_->active!=active||(active&&pendingDeletion_->documentGeneration!=active->documentGeneration())||
            pendingDeletion_->plan.revision!=plan.revision||pendingDeletion_->plan.assetIds!=plan.assetIds||pendingDeletion_->replacements!=replacements))
            throw std::runtime_error("The document or deletion choices changed while validation was pending.");
        std::vector<AssetEntry> deleted;std::unordered_set<std::string> deletedModels,deletedParticles,deletedMaterials,deletedProfiles,deletedEffects,deletedAnimations,deletedIds(plan.assetIds.begin(),plan.assetIds.end());
        if(!particleDocumentId_.empty()&&deletedIds.contains(particleDocumentId_))throw std::runtime_error("Close the particle document before deleting its asset.");
        if(!materialDocumentId_.empty()&&deletedIds.contains(materialDocumentId_))throw std::runtime_error("Close the material document before deleting its asset.");
        if(!navigationProfileDocumentId_.empty()&&deletedIds.contains(navigationProfileDocumentId_))throw std::runtime_error("Close the Navigation Agent Profile document before deleting its asset.");
        if(!gameplayEffectDocumentId_.empty()&&deletedIds.contains(gameplayEffectDocumentId_))throw std::runtime_error("Close the Gameplay Effect document before deleting its asset.");
        if(!animationDocumentId_.empty()&&deletedIds.contains(animationDocumentId_))throw std::runtime_error("Close the Animation document before deleting its asset.");
        for(const auto& id:plan.assetIds){const auto entry=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& a){return a.id==id;});if(entry==catalog_.end())throw std::runtime_error("Deletion target disappeared.");
            deleted.push_back(*entry);if(entry->kind==AssetKind::Model)deletedModels.insert(id);if(entry->kind==AssetKind::Particle)deletedParticles.insert(id);if(entry->kind==AssetKind::Material)deletedMaterials.insert(id);if(entry->kind==AssetKind::NavigationAgentProfile)deletedProfiles.insert(id);if(entry->kind==AssetKind::GameplayEffect)deletedEffects.insert(id);if(entry->kind==AssetKind::Animation)deletedAnimations.insert(id);
            if(const auto chosen=replacements.find(id);chosen!=replacements.end()&&!chosen->second.empty()){
                const auto options=fallbackCandidates(id);if(std::none_of(options.begin(),options.end(),[&](const auto& option){return option.id==chosen->second;}))throw std::runtime_error("Fallback has an incompatible asset type.");}}
        for(const auto& entry:catalog_)if(entry.kind==AssetKind::Animation&&!deletedIds.contains(entry.id)) {
            const auto definition=loadAnimationAsset(projectRoot_,entry.id);
            if(deletedModels.contains(definition.sourceModel.id))throw std::runtime_error("A surviving Animation still requires this immutable source model.");
        }
        for(const auto& id:animationDocumentReferences_)if(deletedModels.contains(id))throw std::runtime_error("The open Animation document still requires this source model.");
        for(const auto& id:animationDocumentHistory_)if(deletedModels.contains(id))throw std::runtime_error("Animation history still requires this source model.");
        auto nextReferences=references_;nextReferences.replace(deleted,replacements);
        for(const auto& id:deletedProfiles)(void)loadNavigationAgentProfile(projectRoot_,nextReferences.resolveNavigationAgentProfile(id));
        for(const auto& id:deletedEffects){const auto target=nextReferences.resolveGameplayEffect(id);if(!target.empty())(void)loadGameplayEffectAsset(projectRoot_,target);}
        for(const auto& id:deletedAnimations){const auto target=nextReferences.resolveAnimation(id);if(!target.empty())(void)loadAnimationAsset(projectRoot_,target);}
        std::vector<std::shared_ptr<const ModelAsset>> nextModels;
        std::vector<AssetEntry> surviving;for(const auto& entry:catalog_)if(entry.kind==AssetKind::Model&&!deletedModels.contains(entry.id))surviving.push_back(entry);
        for(const auto& id:retainedQuaderModels_)if(!references_.deleted(id)&&!deletedModels.contains(id))surviving.push_back({id,id,AssetKind::Model});
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
        const std::vector<std::string> deletedModelIds(deletedModels.begin(),deletedModels.end());
        auto bundleRemoval=prepareQuaderBundleModelRemoval(projectRoot_,deletedModelIds);
        const std::unordered_set<std::string> deletedBundleModels(bundleRemoval.modelIds.begin(),bundleRemoval.modelIds.end());
        std::vector<AssetFileChange> changes=std::move(bundleRemoval.files);
        std::vector<StoredAssetPath> profileRegistrations;
        auto preparedMaterials=materials_;auto preparedAuthoredMaterials=authoredMaterials_;auto preparedParticles=particles_;
        auto preparedMaterialRevision=nextMaterialRevision_,preparedParticleRevision=nextParticleRevision_;
        for(const auto& entry:catalog_)if(entry.kind==AssetKind::Material&&!deletedIds.contains(entry.id)){
            auto material=loadMaterialAsset(projectRoot_,entry.id);bool changed=false;
            canonicalizeMaterialAsset(material);
            rewriteMaterialGraphTextures(material.graph,[&](const auto& id,MaterialTextureRole role){
                if(!deletedIds.contains(references_.resolve(id)))return id;
                const auto replacement=nextReferences.resolveTexture(id,role==MaterialTextureRole::Normal);
                changed|=replacement!=id;return replacement;
            });
            if(changed)canonicalizeMaterialAsset(material);
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
            if(active&&scene.id==active->assetId()&&scene.fingerprint==active->loadedFingerprint())for(auto& record:scene.records)if(record.controller) {
                const auto original=active->legacyNavigationAgentProfileSeedForController(record.id);if(!original)continue;
                const auto seed=std::find_if(scene.stagedNavigationProfiles.begin(),scene.stagedNavigationProfiles.end(),[&](const auto& profile){return profile&&profile->id==record.controller->profile.id;});
                if(seed!=scene.stagedNavigationProfiles.end()&&(*seed)->traversal==original->traversal){record.controller->profile.id=original->id;*seed=original;}
            }
            if(active&&scene.id==active->assetId()&&scene.fingerprint==active->loadedFingerprint())for(auto& record:scene.records)if(record.effects) {
                const auto original=active->legacyGameplayEffectSeedForEntity(record.id);if(!original)continue;
                for(auto& assigned:record.effects->starting) {
                    const auto seed=std::find_if(scene.stagedGameplayEffects.begin(),scene.stagedGameplayEffects.end(),[&](const auto& effect){return effect&&effect->id==assigned.id;});
                    if(seed!=scene.stagedGameplayEffects.end()) {
                        auto equivalent=**seed;equivalent.id=original->id;equivalent.name=original->name;
                        if(equivalent==*original){assigned.id=original->id;*seed=original;}
                    }
                }
            }
            const auto removed=removeDeletedModels(scene,references_,nextReferences,deletedModels);
            const auto replaced=replaceDeletedParticles(scene,references_,nextReferences,deletedParticles);
            const auto materials=replaceDeletedModelMaterials(scene,references_,nextReferences,deletedMaterials,projectRoot_);
            const auto profiles=replaceDeletedNavigationProfiles(scene,references_,nextReferences,deletedProfiles);
            const auto effects=replaceDeletedGameplayEffects(scene,references_,nextReferences,deletedEffects);
            const auto animations=replaceDeletedAnimations(scene,references_,nextReferences,deletedAnimations);
            if(removed||replaced||materials||profiles||effects||animations) {
                auto persistence=prepareScenePersistence(projectRoot_,scene.id,scene);
                changes.insert(changes.end(),std::make_move_iterator(persistence.files.begin()),std::make_move_iterator(persistence.files.end()));
                profileRegistrations.insert(profileRegistrations.end(),persistence.registrations.begin(),persistence.registrations.end());
            }
        }
        SceneSnapshot activeCandidate;bool activeChanged=false,clearHistory=false,activeDeleted=active&&deletedIds.contains(active->assetId());const bool wasDirty=active&&active->dirty();
        if(active&&!activeDeleted){activeCandidate.records=active->scene().records();activeCandidate.properties=active->scene().properties();activeCandidate.stagedGameplayEffects=active->stagedGameplayEffects();
            activeChanged=removeDeletedModels(activeCandidate,references_,nextReferences,deletedModels)!=0;
            activeChanged=replaceDeletedParticles(activeCandidate,references_,nextReferences,deletedParticles)!=0||activeChanged;
            activeChanged=replaceDeletedModelMaterials(activeCandidate,references_,nextReferences,deletedMaterials,projectRoot_)!=0||activeChanged;
            activeChanged=replaceDeletedNavigationProfiles(activeCandidate,references_,nextReferences,deletedProfiles)!=0||activeChanged;
            activeChanged=replaceDeletedGameplayEffects(activeCandidate,references_,nextReferences,deletedEffects)!=0||activeChanged;
            activeChanged=replaceDeletedAnimations(activeCandidate,references_,nextReferences,deletedAnimations)!=0||activeChanged;
            for(const auto& id:active->historicalMaterialReferences())if(deletedMaterials.contains(references_.resolveMaterial(id)))clearHistory=true;
            for(const auto& id:active->historicalModelReferences())if(deletedModels.contains(references_.resolve(id))&&nextReferences.resolve(id).empty())clearHistory=true;
            for(const auto& id:active->historicalParticleReferences())if(deletedParticles.contains(references_.resolveParticle(id)))clearHistory=true;}
        if(active&&!activeDeleted)for(const auto& id:active->historicalNavigationAgentProfileReferences())if(deletedProfiles.contains(references_.resolveNavigationAgentProfile(id)))clearHistory=true;
        if(active&&!activeDeleted)for(const auto& id:active->historicalGameplayEffectReferences())if(deletedEffects.contains(references_.resolveGameplayEffect(id)))clearHistory=true;
        if(active&&!activeDeleted)for(const auto& id:active->historicalAnimationReferences())if(deletedAnimations.contains(references_.resolveAnimation(id)))clearHistory=true;
        std::vector<std::filesystem::path> removedDirectories,selectedDirectories;
        removedDirectories.insert(removedDirectories.end(),bundleRemoval.emptyDirectories.begin(),bundleRemoval.emptyDirectories.end());
        const auto contentRoot=std::filesystem::weakly_canonical(projectRoot_/"Content");
        const auto addEmptyAncestors=[&](std::filesystem::path directory){
            directory=std::filesystem::weakly_canonical(directory);const auto relative=directory.lexically_relative(contentRoot);
            if(relative.empty()||relative=="."||relative.has_root_path()||*relative.begin()=="..")return;
            for(;directory!=contentRoot;directory=directory.parent_path())removedDirectories.push_back(directory);
        };
        for(const auto& entry:deleted){
            addEmptyAncestors(resolveAssetPath(projectRoot_,entry.id).parent_path());
            const auto folder=browserCatalog().parent(entry.id);if(!folder.empty()&&browserCatalog().writableFolder(folder))addEmptyAncestors(browserCatalog().folderPath(folder));
        }
        for(const auto& id:plan.folderIds)if(browserCatalog().writableFolder(id)) {
            const auto directory=browserCatalog().folderPath(id);selectedDirectories.push_back(directory);removedDirectories.push_back(directory);
            if(std::filesystem::is_directory(directory))for(const auto& item:std::filesystem::recursive_directory_iterator(directory))if(item.is_directory())removedDirectories.push_back(item.path());
        }
        for(const auto& entry:deleted){if(deletedBundleModels.contains(entry.id))continue;
            if(entry.kind==AssetKind::Model){const auto package=checkedManagedAssetPath(projectRoot_,entry.id).parent_path();removedDirectories.push_back(package);
                for(const auto& file:std::filesystem::recursive_directory_iterator(package))if(file.is_regular_file()){
                    const auto independent=assetIdAtPath(projectRoot_,file.path());if(!independent.empty()&&independent!=entry.id&&!deletedIds.contains(independent)&&std::any_of(catalog_.begin(),catalog_.end(),[&](const auto& card){return card.id==independent;}))continue;
                    changes.push_back({utf8(file.path().lexically_relative(projectRoot_)),std::nullopt});}
                else if(file.is_directory())removedDirectories.push_back(file.path());}
            else changes.push_back({entry.id,std::nullopt});}
        // Delete may explicitly include an image also found in a model's package list; unique file targets only.
        std::vector<AssetFileChange> unique;std::set<std::filesystem::path> files;for(auto& change:changes)if(files.insert(checkedManagedAssetPath(projectRoot_,change.id)).second)unique.push_back(std::move(change));
        unique.push_back({"assets/references.json",nextReferences.serialize()});auto browserIds=plan.assetIds;browserIds.insert(browserIds.end(),plan.folderIds.begin(),plan.folderIds.end());
        unique.push_back({"assets/browser.json",browserCatalog().serializeWithout(browserIds)});
        auto removedLocations=bundleRemoval.memberIds;removedLocations.insert(removedLocations.end(),bundleRemoval.removedBundleIds.begin(),bundleRemoval.removedBundleIds.end());
        const bool locationsChanged=!profileRegistrations.empty()||!removedLocations.empty();
        if(locationsChanged)unique.push_back({".crimson/asset-locations.json",prepareAssetPathRegistration(projectRoot_,profileRegistrations,removedLocations)});
        auto preparedBrowser=browserCatalog().prepareErase(browserIds);auto preparedCatalog=catalog_;
        auto preparedInactiveQuaderModels=inactiveQuaderModels_,preparedRetainedQuaderModels=retainedQuaderModels_;
        for(const auto& id:deletedBundleModels){preparedInactiveQuaderModels.insert(id);preparedRetainedQuaderModels.erase(id);}
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
            if(!active->assetId().empty()){
                const auto savedScene=sceneAssetPath(projectRoot_,active->assetId());
                for(const auto& change:unique)if(change.contents&&checkedManagedAssetPath(projectRoot_,change.id)==savedScene)savedFingerprint=sceneTextFingerprint(*change.contents);
            }
            preparedScene.emplace(active->prepareExternalAssetChanges(activeCandidate.records,activeCandidate.properties,"Delete project assets",wasDirty,savedFingerprint));
        }
        if(preparedScene && !active->canCommitExternalAssetChanges(*preparedScene)) throw std::runtime_error("The active scene changed while deletion was preparing.");
        std::vector<std::filesystem::path> writtenPaths;writtenPaths.reserve(unique.size());
        for(const auto& change:unique)writtenPaths.push_back(checkedManagedAssetPath(projectRoot_,change.id));
        ownWrites_.reserve(ownWrites_.size()+writtenPaths.size());
        validateQuaderBundleModelRemoval(projectRoot_,bundleRemoval);
        if(referencePolicyBytes(projectRoot_)!=policyBaseline)throw std::runtime_error("The asset reference policy changed while deletion was preparing. Review the deletion again.");
        commitAssetTransaction(projectRoot_,unique);
        durable=true;
        // All data/registry/catalog allocations were prepared above. Install the
        // authored document immediately; cache maintenance cannot roll it back.
        if(preparedScene) { const auto committed=active->commitExternalAssetChanges(std::move(*preparedScene));if(!committed)std::terminate(); }
        references_=std::move(nextReferences);models_=std::move(nextModels);
        browserCatalog_=std::move(preparedBrowser);catalog_=std::move(preparedCatalog);materials_=std::move(preparedMaterials);particles_=std::move(preparedParticles);
        inactiveQuaderModels_=std::move(preparedInactiveQuaderModels);retainedQuaderModels_=std::move(preparedRetainedQuaderModels);
        authoredMaterials_=std::move(preparedAuthoredMaterials);nextMaterialRevision_=preparedMaterialRevision;nextParticleRevision_=preparedParticleRevision;
        ownWrites_.insert(ownWrites_.end(),std::make_move_iterator(writtenPaths.begin()),std::make_move_iterator(writtenPaths.end()));
        if(locationsChanged)refreshContentStorage(projectRoot_);
        std::erase_if(navigationProfiles_,[&](const auto& entry){return deletedIds.contains(entry.first);});
        std::erase_if(gameplayEffects_,[&](const auto& entry){return deletedIds.contains(entry.first);});
        std::erase_if(animations_,[&](const auto& entry){return deletedIds.contains(entry.first);});
        if(!profileRegistrations.empty()){refreshNavigationAgentProfiles();refreshGameplayEffects();}
        std::erase_if(preparedParticleMaterials_,[&](const auto& entry){return deletedIds.contains(entry.first);});++catalogRevision_;browserRevision_=0;
        for(auto it=assets_.begin();it!=assets_.end();)if(deletedIds.contains(it->id)){retiredTextures_.push_back(std::move(*it));it=assets_.erase(it);}else ++it;
        browserSelection_.clear();
        if(resources_) {
            for(const auto& id:deletedIds) { resources_->forget(id);textureStreamer_->invalidate(id);modelResources_.erase(id);materialResources_.erase(id);materialCatalogResources_.erase(id);modelInterest_.erase(id); }
            // Runtime model records hold compact metadata; validation arrays above are temporary.
            models_.clear();registeredModelRevisions_.clear();
        }
        std::sort(removedDirectories.begin(),removedDirectories.end(),[](const auto& a,const auto& b){return std::distance(a.begin(),a.end())>std::distance(b.begin(),b.end());});
        for(const auto& directory:removedDirectories){std::error_code ignored;if(std::filesystem::is_directory(directory,ignored)&&std::filesystem::is_empty(directory,ignored))std::filesystem::remove(directory,ignored);}
        std::string retained;
        for(const auto& directory:selectedDirectories)if(std::filesystem::exists(directory))retained+="\nFolder retained because it still contains files: "+utf8(directory);
        cancelDeletion();status_="Deleted "+std::to_string(plan.assetIds.size())+" asset(s)."+retained;return true;
    }catch(const std::exception& e){cancelDeletion();status_=(durable?"Assets were deleted; resource cache refresh needs attention: ":"Could not delete assets: ")+std::string(e.what());return durable;}
}

} // namespace crimson
