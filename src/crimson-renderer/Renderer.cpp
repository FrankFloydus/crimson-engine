#include "Renderer.h"
#include "RenderFrame.h"
#include "ThumbnailCache.h"
#include "ModelTransform.h"
#include "Mesh.h"
#include "Environment.h"
#include "ProceduralSkyGpu.h"
#include "EnvironmentPresentation.h"
#include "Fog.h"
#include "Picking.h"
#include "ModelGpuCache.h"
#include "ModelDeformationGpu.h"
#include "assets/ModelAnimation.h"
#include "ShaderPreflight.h"
#include "PointShadowRenderer.h"
#include "ShadowLightSelection.h"
#include "ClusteredLightingGpu.h"
#include "SsgiGpu.h"
#include "SsaoGpu.h"
#include "CollisionOverlayGpu.h"

#include <SDL3/SDL.h>
#include <bgfx/platform.h>
#include <d3d11.h>
#include <d3d12.h>

#include "vs_pbr.h"
#include "fs_pbr.h"
#include "vs_model.h"
#include "fs_model.h"
#include "cs_ssgi_trace.h"
#include "cs_ssgi_temporal.h"
#include "cs_ssgi_filter.h"
#include "cs_ssgi_publish.h"
#include "cs_ssao_evaluate.h"
#include "cs_ssao_publish.h"
#include "vs_pick.h"
#include "fs_pick.h"
#include "vs_present.h"
#include "fs_present.h"
#include "fs_glow_extract.h"
#include "fs_glow_blur.h"
#include "vs_grid.h"
#include "fs_grid.h"
#include "vs_collision_overlay.h"
#include "fs_collision_overlay.h"
#include "fs_encode.h"
#include "vs_particle.h"
#include "fs_particle.h"
#include "fs_sky.h"
#include "cs_procedural_filter.h"
#include "cs_cluster_lights.h"
#include "cs_model_deform.h"
#include "vs_point_shadow.h"
#include "fs_point_shadow.h"
#include "vs_directional_shadow.h"
#include "fs_directional_shadow.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace crimson {
namespace {
struct TransparentMeshDraw { double depth{}; std::function<void()> submit; };
using RenderViews = RenderViewPassSlots;
const auto MainViews=renderViewPassSlots(RenderViewPurpose::Scene);
const auto ParticlePreviewViews=renderViewPassSlots(RenderViewPurpose::ParticlePreview);
const auto ThumbnailViews=renderViewPassSlots(RenderViewPurpose::Thumbnail);
const auto MaterialPreviewViews=renderViewPassSlots(RenderViewPurpose::MaterialPreview);
const auto GameViews=renderViewPassSlots(RenderViewPurpose::Game);
std::uint64_t shaderContentHash(std::span<const std::uint8_t> vertex,std::span<const std::uint8_t> fragment) {
    std::uint64_t hash=14695981039346656037ull;
    for (const auto bytes:{vertex,fragment}) for (const auto byte:bytes) {hash^=byte;hash*=1099511628211ull;} return hash;
}
float finiteOr(float value, float fallback) { return std::isfinite(value) ? value : fallback; }
float bounded(float value, float fallback, float lo, float hi) { return std::clamp(finiteOr(value, fallback), lo, hi); }
MaterialBlend authoredModelBlend(const RenderModelInstance& instance,const ModelMaterial& material,const ModelMaterialTextures& bindings) {
    return bindings.graph?bindings.graph->blend:material.graph?materialOutputNode(*material.graph).output.blend:
        material.alphaBlend?MaterialBlend::Alpha:instance.blend;
}
float toLinear(float value) {
    const float c = bounded(value, 0.0f, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
template<class Handle> void destroyHandle(Handle& handle) noexcept {
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
    handle = BGFX_INVALID_HANDLE;
}

// GPU ownership stays out of the CPU mesh generator.
struct GpuMesh {
    bgfx::VertexBufferHandle vertices{BGFX_INVALID_HANDLE};
    bgfx::IndexBufferHandle indices{BGFX_INVALID_HANDLE};
    std::shared_ptr<ModelDeformationSource> deformation;
    std::size_t bytes{};
    void upload(const MeshData& data) {
        if (data.vertices.empty() || data.indices.empty()) throw std::invalid_argument("Cannot upload an empty mesh.");
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float).end();
        vertices = bgfx::createVertexBuffer(bgfx::copy(data.vertices.data(), static_cast<std::uint32_t>(data.vertices.size() * sizeof(Vertex))), layout);
        indices = bgfx::createIndexBuffer(bgfx::copy(data.indices.data(), static_cast<std::uint32_t>(data.indices.size() * sizeof(std::uint16_t))));
        if (!bgfx::isValid(vertices) || !bgfx::isValid(indices)) {
            reset(); throw std::runtime_error("GPU mesh buffer creation failed.");
        }
    }
    void uploadModel(const ModelPrimitive& data) {
        if (data.vertices.empty() || data.indices.empty()) throw std::invalid_argument("Cannot upload an empty model primitive.");
        if (data.vertices.size() > UINT32_MAX / sizeof(ModelVertex) || data.indices.size() > UINT32_MAX / sizeof(std::uint32_t))
            throw std::length_error("Model primitive exceeds the GPU buffer size limit.");
        const auto largest = *std::max_element(data.indices.begin(), data.indices.end());
        if (largest >= data.vertices.size()) throw std::invalid_argument("Model primitive references a missing vertex.");
        const bool wide = largest > UINT16_MAX;
        if (wide && !(bgfx::getCaps()->supported & BGFX_CAPS_INDEX32))
            throw std::runtime_error("This GPU does not support the model's 32-bit indices.");
        static_assert(sizeof(ModelVertex) == 27 * sizeof(float));
        static_assert(offsetof(ModelVertex, normal) == 3 * sizeof(float));
        static_assert(offsetof(ModelVertex, uv0) == 6 * sizeof(float));
        static_assert(offsetof(ModelVertex, uv1) == 8 * sizeof(float));
        static_assert(offsetof(ModelVertex, tangent) == 10 * sizeof(float));
        static_assert(offsetof(ModelVertex, color) == 14 * sizeof(float));
        static_assert(offsetof(ModelVertex, uvInterpolationCoordinates) == 18 * sizeof(float));
        static_assert(offsetof(ModelVertex, uvInterpolationCorrection) == 22 * sizeof(float));
        static_assert(offsetof(ModelVertex, uvInterpolationMode) == 26 * sizeof(float));
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord4,1,bgfx::AttribType::Float).end();
        deformation=createModelDeformationSource(data);
        while(deformation && !deformation->ready())uploadModelDeformationSource(*deformation,data,SIZE_MAX);
        const auto flags=static_cast<std::uint16_t>(deformation ? BGFX_BUFFER_COMPUTE_READ|BGFX_BUFFER_COMPUTE_FORMAT_32X1|BGFX_BUFFER_COMPUTE_TYPE_FLOAT:BGFX_BUFFER_NONE);
        vertices = bgfx::createVertexBuffer(bgfx::copy(data.vertices.data(), static_cast<std::uint32_t>(data.vertices.size() * sizeof(ModelVertex))), layout,flags);
        bytes=data.vertices.size()*sizeof(ModelVertex)+data.indices.size()*(wide?4u:2u)+(deformation?deformation->bytes:0);
        if (wide) {
            indices = bgfx::createIndexBuffer(bgfx::copy(data.indices.data(), static_cast<std::uint32_t>(data.indices.size() * sizeof(std::uint32_t))), BGFX_BUFFER_INDEX32);
        } else {
            std::vector<std::uint16_t> compact(data.indices.begin(), data.indices.end());
            indices = bgfx::createIndexBuffer(bgfx::copy(compact.data(), static_cast<std::uint32_t>(compact.size() * sizeof(std::uint16_t))));
        }
        if (!bgfx::isValid(vertices) || !bgfx::isValid(indices)) {
            reset(); throw std::runtime_error("Model GPU mesh buffer creation failed.");
        }
    }
    void bind() const { bgfx::setVertexBuffer(0, vertices); bgfx::setIndexBuffer(indices); }
    void reset() noexcept { destroyHandle(vertices); destroyHandle(indices);deformation.reset();bytes=0; }
};
struct GpuModel {
    std::string id;
    std::vector<GpuMesh> primitives;
    std::vector<Aabb> bounds;
    ~GpuModel() { for (auto& primitive : primitives) primitive.reset(); }
};

template<std::size_t V, std::size_t F>
bgfx::ProgramHandle program(const std::uint8_t (&vs)[V], const std::uint8_t (&fs)[F], const char* name) {
    auto vertex = bgfx::createShader(bgfx::copy(vs, static_cast<std::uint32_t>(V)));
    auto fragment = bgfx::createShader(bgfx::copy(fs, static_cast<std::uint32_t>(F)));
    if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
        destroyHandle(vertex); destroyHandle(fragment);
        throw std::runtime_error(std::string("Failed to create shader: ") + name);
    }
    bgfx::setName(vertex, name); bgfx::setName(fragment, name);
    const auto result = bgfx::createProgram(vertex, fragment, true);
    if (!bgfx::isValid(result)) throw std::runtime_error(std::string("Failed to link shader: ") + name);
    return result;
}
bgfx::UniformHandle uniform(const char* name, bgfx::UniformType::Enum type = bgfx::UniformType::Vec4, std::uint16_t count = 1) {
    const auto handle = bgfx::createUniform(name, type, count);
    if (!bgfx::isValid(handle)) throw std::runtime_error(std::string("Failed to create uniform: ") + name);
    return handle;
}

// Two half-resolution FP32 images avoid overflow from supported HDR particle colors.
// Framebuffers borrow the textures, and every owner is destroyed before bgfx shutdown.
struct GlowTargets {
    std::array<bgfx::TextureHandle,2> textures{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    std::array<bgfx::FrameBufferHandle,2> buffers{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    std::uint16_t width{},height{};
    ~GlowTargets() { for(auto& buffer:buffers)destroyHandle(buffer);for(auto& texture:textures)destroyHandle(texture); }
    void create(std::uint16_t w,std::uint16_t h) {
        width=static_cast<std::uint16_t>((w+1u)/2u);height=static_cast<std::uint16_t>((h+1u)/2u);
        constexpr auto flags=BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
            BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
        for(std::size_t i=0;i<textures.size();++i) {
            textures[i]=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA32F,flags);
            if(!bgfx::isValid(textures[i]))throw std::runtime_error("Glow color allocation failed; the view is rendered without Glow.");
            buffers[i]=bgfx::createFrameBuffer(1,&textures[i],false);
            if(!bgfx::isValid(buffers[i]))throw std::runtime_error("Glow framebuffer allocation failed; the view is rendered without Glow.");
            bgfx::setName(textures[i],i==0?"Glow: exposed bright color / vertical blur":"Glow: horizontal blur");
        }
    }
    std::size_t bytes() const noexcept {return std::size_t(width)*height*16*textures.size();}
};

// Framebuffers borrow textures: HDR and the grid composite share one depth target.
struct ViewportTargets {
    std::unique_ptr<ClusteredLightingGpu> clustered;
    std::unique_ptr<SsgiGpu> ssgi;
    std::unique_ptr<SsaoGpu> ssao;
    std::string ssaoFailure;
    bgfx::FrameBufferHandle ssgiFramebuffer{BGFX_INVALID_HANDLE};
    struct RetiredSsgiFramebuffer {bgfx::FrameBufferHandle handle;std::uint64_t after;};
    std::vector<RetiredSsgiFramebuffer> retiredSsgiFramebuffers;
    std::string ssgiFailure;
    void retireSsgiFramebuffer(std::uint64_t frame) {
        if(!bgfx::isValid(ssgiFramebuffer))return;
        retiredSsgiFramebuffers.push_back({ssgiFramebuffer,frame+2});ssgiFramebuffer=BGFX_INVALID_HANDLE;
    }
    void advanceSsgi(std::uint64_t frame) {
        std::erase_if(retiredSsgiFramebuffers,[&](auto& item){if(item.after>frame)return false;destroyHandle(item.handle);return true;});
        if(ssgi)ssgi->advance(frame);
        if(ssao)ssao->advance(frame);
    }
    bgfx::TextureHandle readableDepth{BGFX_INVALID_HANDLE},receiverScope{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle depthPrepass{BGFX_INVALID_HANDLE};
    std::size_t readableDepthBytes{};
    bgfx::TextureHandle hdr{BGFX_INVALID_HANDLE}, composite{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle display{BGFX_INVALID_HANDLE}, depth{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle hdrBuffer{BGFX_INVALID_HANDLE}, compositeBuffer{BGFX_INVALID_HANDLE}, displayBuffer{BGFX_INVALID_HANDLE};
    std::unique_ptr<GlowTargets> glow;
    std::string glowFailure; // Retry only after target recreation, avoiding allocation churn every frame.
    ~ViewportTargets() {
        destroyHandle(ssgiFramebuffer);for(auto& item:retiredSsgiFramebuffers)destroyHandle(item.handle);ssgi.reset();ssao.reset();
        destroyHandle(depthPrepass);destroyHandle(readableDepth);destroyHandle(receiverScope);
        clustered.reset();
        glow.reset();
        destroyHandle(hdrBuffer); destroyHandle(compositeBuffer); destroyHandle(displayBuffer);
        destroyHandle(hdr); destroyHandle(composite); destroyHandle(display); destroyHandle(depth);
    }
    void create(std::uint32_t w, std::uint32_t h) {
        const auto maxSize = std::min<std::uint32_t>(bgfx::getCaps()->limits.maxTextureSize, std::numeric_limits<std::uint16_t>::max());
        if (!w || !h || w > maxSize || h > maxSize) throw std::runtime_error("Viewport exceeds the GPU texture size limit.");
        const auto ww = static_cast<std::uint16_t>(w), hh = static_cast<std::uint16_t>(h);
        constexpr std::uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        hdr = bgfx::createTexture2D(ww, hh, false, 1, bgfx::TextureFormat::RGBA32F, flags);
        composite = bgfx::createTexture2D(ww, hh, false, 1, bgfx::TextureFormat::RGBA16F, flags);
        // Encoded bytes in ordinary RGBA8; ImGui must not decode them again.
        display = bgfx::createTexture2D(ww, hh, false, 1, bgfx::TextureFormat::RGBA8, flags);
        depth = bgfx::createTexture2D(ww, hh, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
        if (!bgfx::isValid(hdr) || !bgfx::isValid(composite) || !bgfx::isValid(display) || !bgfx::isValid(depth))
            throw std::runtime_error("Failed to create viewport color/depth targets.");
        const bgfx::TextureHandle hdrAttachments[]{hdr, depth};
        const bgfx::TextureHandle compositeAttachments[]{composite, depth};
        hdrBuffer = bgfx::createFrameBuffer(2, hdrAttachments, false);
        compositeBuffer = bgfx::createFrameBuffer(2, compositeAttachments, false);
        displayBuffer = bgfx::createFrameBuffer(1, &display, false);
        if (!bgfx::isValid(hdrBuffer) || !bgfx::isValid(compositeBuffer) || !bgfx::isValid(displayBuffer))
            throw std::runtime_error("Failed to create viewport framebuffers.");
        bgfx::setName(hdr, "Viewport HDR scene");
        bgfx::setName(composite, "Linear tone map and grid");
        bgfx::setName(display, "Encoded viewport image");
        bgfx::setName(depth, "Shared scene and grid depth");
    }
    void ensureReadableDepth(std::uint16_t width,std::uint16_t height) {
        if(bgfx::isValid(depthPrepass))return;
        constexpr auto flags=BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
            BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
        if(!bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::D24S8,flags) ||
           !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA8,flags))
            throw std::runtime_error("Ambient visibility requires sampleable D24S8 depth and an RGBA8 receiver target.");
        readableDepth=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::D24S8,flags);
        receiverScope=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,flags);
        if(!bgfx::isValid(readableDepth)||!bgfx::isValid(receiverScope)) {
            destroyHandle(readableDepth);destroyHandle(receiverScope);
            readableDepth=receiverScope=BGFX_INVALID_HANDLE;
            throw std::runtime_error("Readable scene-depth allocation failed.");
        }
        const bgfx::TextureHandle attachments[]{receiverScope,readableDepth};
        depthPrepass=bgfx::createFrameBuffer(2,attachments,false);
        if(!bgfx::isValid(depthPrepass)) {
            destroyHandle(readableDepth);destroyHandle(receiverScope);
            readableDepth=receiverScope=BGFX_INVALID_HANDLE;
            throw std::runtime_error("Readable scene-depth framebuffer allocation failed.");
        }
        readableDepthBytes=std::size_t(width)*height*8;
        bgfx::setName(readableDepth,"Opaque/masked readable scene depth");bgfx::setName(receiverScope,"Ambient receiver scopes and geometric normal");
    }
};

// Dedicated non-sRGB, non-MSAA picking color/depth, and a single-pixel staging
// texture. The CPU bytes live in Impl until readback completion, even if a
// resize, cancellation, or scene replacement invalidates their selection.
struct PickTargets {
    bgfx::TextureHandle color{BGFX_INVALID_HANDLE}, depth{BGFX_INVALID_HANDLE}, readback{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle framebuffer{BGFX_INVALID_HANDLE};
    ~PickTargets() { destroyHandle(framebuffer); destroyHandle(color); destroyHandle(depth); destroyHandle(readback); }
    void create(std::uint16_t w, std::uint16_t h) {
        constexpr auto sampling = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        color = bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT | sampling);
        depth = bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::D24S8,BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
        readback = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | sampling);
        if (!bgfx::isValid(color) || !bgfx::isValid(depth) || !bgfx::isValid(readback))
            throw std::runtime_error("Unable to allocate model picking targets.");
        const bgfx::TextureHandle attachments[]{color,depth};
        framebuffer = bgfx::createFrameBuffer(2,attachments,false);
        if (!bgfx::isValid(framebuffer)) throw std::runtime_error("Unable to allocate model picking framebuffer.");
    }
};

struct PendingPick {
    std::vector<std::shared_ptr<const PreparedGraphMaterial>> graphMaterials;
    PickRequest request;
    MaterialEvaluationContext materialContext;
    std::uint64_t epoch{};
    std::uint32_t readyFrame{};
    picking::Pixel pixel{};
    // Dense draw IDs keep full 64-bit persistent entity IDs out of shader floats.
    picking::EntityIds entities;
    std::shared_ptr<PickTargets> targets;
    std::vector<std::shared_ptr<const ResidentModelLod>> resources;
    std::vector<std::shared_ptr<const DeformedModelVertices>> deformations;
    std::shared_ptr<const void> pinned;
};
struct PendingThumbnail {
    std::string id; std::uint64_t signature{}; std::uint32_t readyFrame{};
    DecodedImage image;
    std::unique_ptr<ViewportTargets> targets;
    bgfx::TextureHandle readback{BGFX_INVALID_HANDLE};
    std::shared_ptr<const void> pinned;
    std::vector<std::shared_ptr<const ResidentModelLod>> resources;
    ~PendingThumbnail() { destroyHandle(readback); }
};
} // namespace

struct Renderer::Impl {
    bool initialized{false}, viewportDrawable{false}, particlePreviewDrawable{false};
    std::uint32_t windowWidth{1}, windowHeight{1}, width{1}, height{1};
    GpuMesh sphere, fullscreen, grid, particleQuad;
    std::uint32_t particlePreviewWidth{},particlePreviewHeight{};
    std::unique_ptr<ViewportTargets> particlePreviewTargets;
    std::unique_ptr<ViewportTargets> materialPreviewTargets;
    std::uint32_t materialPreviewWidth{},materialPreviewHeight{};
    bool materialPreviewDrawable{};
    std::unique_ptr<ViewportTargets> gameTargets;
    std::uint32_t gameWidth{},gameHeight{};
    bool gameDrawable{};
    ParticleRenderStats particles,previewParticles,gameParticles;
    std::string particleStatus;
    std::vector<particle_rendering::TransparentSortKey> particleScratch;
    struct ParticleBatch {
        bgfx::InstanceDataBuffer instances{};
        std::size_t index{};
        bool mesh{};
    };
    std::vector<ParticleBatch> particleBatches;
    std::vector<std::shared_ptr<const void>> submittedPins,previousSubmittedPins;
    std::unordered_map<std::string,std::shared_ptr<GpuModel>> models;
    std::unique_ptr<ModelDeformationGpu> deformation;
    bgfx::ProgramHandle deformationProgram{BGFX_INVALID_HANDLE};
    struct BindPose {std::weak_ptr<const ModelAsset> asset;std::shared_ptr<const ModelPose> pose;};
    std::unordered_map<const ModelAsset*,BindPose> bindPoses;
    std::unique_ptr<ModelGpuCache> streamModels{std::make_unique<ModelGpuCache>()};
    std::uint64_t externalSignature{},geometrySceneSignature{},programRevision{1},environmentRevision{1};
    ViewportAppearance appearance;
    bool gridVisible{true};
    std::string modelStatus;
    std::shared_ptr<PickTargets> pickTargets;
    std::optional<PickRequest> requestedPick;
    MaterialEvaluationContext requestedPickContext{},displayedMaterialContext{};
    std::unique_ptr<PendingPick> pendingPick;
    std::uint64_t pickEpoch{}, latestPickSerial{};
    std::string pickingStatus;
    std::unique_ptr<ViewportTargets> targets;
    std::unique_ptr<EnvironmentLighting> environment;
    std::unique_ptr<EnvironmentLighting> thumbnailEnvironment;
    std::unique_ptr<EnvironmentLighting> materialPreviewEnvironment;
    std::unique_ptr<ProceduralSkyGpu> proceduralEnvironment,thumbnailProceduralEnvironment;
    std::uint64_t proceduralFrame{UINT64_MAX};
    bool proceduralActive{};
    std::unique_ptr<PendingThumbnail> pendingThumbnail;
    ThumbnailCache thumbnails;
    std::string shaderStatus,thumbnailStatus;
    std::string shadowStatus;
    std::unique_ptr<PointShadowRenderer> shadows,thumbnailShadows;
    std::array<std::uint64_t,26> programHashes{};
    std::unique_ptr<CollisionOverlayGpu> collisionOverlays;
    bgfx::ProgramHandle collisionOverlayProgram{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle clusterComputeProgram{BGFX_INVALID_HANDLE};
    std::array<bgfx::ProgramHandle,4> ssgiPrograms{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    std::array<bgfx::ProgramHandle,2> ssaoPrograms{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    bgfx::UniformHandle ssgiAtlas{BGFX_INVALID_HANDLE},ssgiSettings{BGFX_INVALID_HANDLE},ssgiDraw{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle ssaoAtlas{BGFX_INVALID_HANDLE},ssaoSettings{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle debugViewUniform{BGFX_INVALID_HANDLE};
    std::array<RenderDebugView,2> debugViews{RenderDebugView::Lit,RenderDebugView::Lit};
    mutable std::string ssgiStatusCache;
    mutable std::string ssaoStatusCache;
    struct SsgiDrawHistory {Mat4 transform;std::uint64_t material{},frame{};};
    std::array<std::unordered_map<std::uint64_t,SsgiDrawHistory>,2> ssgiDrawHistory;
    bgfx::UniformHandle surfaceLighting{BGFX_INVALID_HANDLE},surfaceCategory{BGFX_INVALID_HANDLE},directionLightingRoles{BGFX_INVALID_HANDLE};
    ClusteredLightingStats clusterStats;
    std::string clusterStatus;
    std::string glowProgramFailure;
    mutable std::string glowStatusCache;
    struct LodSelection { std::uint32_t level{}; std::uint64_t frame{}; };
    // Scene and Game may show the same entity at different projected sizes.
    std::array<std::unordered_map<std::uint64_t,LodSelection>,2> lodSelections;
    std::uint64_t renderFrame{};
    bgfx::TextureHandle white{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle pbr{BGFX_INVALID_HANDLE}, present{BGFX_INVALID_HANDLE}, gridProgram{BGFX_INVALID_HANDLE}, encode{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle modelProgram{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle pickProgram{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle particleProgram{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle skyProgram{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle shadowProgram{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle directionalShadowProgram{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle directionalCasterViewProjection{BGFX_INVALID_HANDLE},directionalCasterTile{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle glowExtractProgram{BGFX_INVALID_HANDLE},glowBlurProgram{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle glowSource{BGFX_INVALID_HANDLE},glowColor{BGFX_INVALID_HANDLE},glowParams{BGFX_INVALID_HANDLE},glowTexel{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle skyRight{BGFX_INVALID_HANDLE},skyUp{BGFX_INVALID_HANDLE},skyForward{BGFX_INVALID_HANDLE},skySampler{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle particleParameters{BGFX_INVALID_HANDLE},particleParams{BGFX_INVALID_HANDLE},particleSampler{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle pickId{BGFX_INVALID_HANDLE}, lightCounts{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle modelBase{BGFX_INVALID_HANDLE}, modelSpecular{BGFX_INVALID_HANDLE},modelSurface{BGFX_INVALID_HANDLE}, modelEmissive{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle modelUv{BGFX_INVALID_HANDLE}, modelOptions{BGFX_INVALID_HANDLE}, modelMaps{BGFX_INVALID_HANDLE}, modelEmissiveMap{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle modelNormalMatrix{BGFX_INVALID_HANDLE},instanceOpacity{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle materialUvScale{BGFX_INVALID_HANDLE},materialUvOffset{BGFX_INVALID_HANDLE},materialUvRotation{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle metallicRoughnessSampler{BGFX_INVALID_HANDLE}, occlusionSampler{BGFX_INVALID_HANDLE}, emissiveSampler{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle baseColorMetallic{BGFX_INVALID_HANDLE}, roughness{BGFX_INVALID_HANDLE}, textureFlags{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle cameraPos{BGFX_INVALID_HANDLE}, direction{BGFX_INVALID_HANDLE}, directionColor{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle presentParams{BGFX_INVALID_HANDLE}, hdrSampler{BGFX_INVALID_HANDLE}, compositeSampler{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle toneCurve{BGFX_INVALID_HANDLE},toneShoulder{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle debandingParams{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle background{BGFX_INVALID_HANDLE},gridWidths{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle fogColorDensity{BGFX_INVALID_HANDLE},fogDepth{BGFX_INVALID_HANDLE},fogHeight{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle fogSky{BGFX_INVALID_HANDLE},fogSunDirection{BGFX_INVALID_HANDLE},fogSunColor{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle albedoSampler{BGFX_INVALID_HANDLE}, roughnessSampler{BGFX_INVALID_HANDLE}, metallicSampler{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle normalSampler{BGFX_INVALID_HANDLE}, irradianceSampler{BGFX_INVALID_HANDLE}, prefilteredSampler{BGFX_INVALID_HANDLE}, brdfSampler{BGFX_INVALID_HANDLE}, environmentParams{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle ambientLight{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle gridMinor{BGFX_INVALID_HANDLE}, gridMajor{BGFX_INVALID_HANDLE}, gridAxisU{BGFX_INVALID_HANDLE}, gridAxisV{BGFX_INVALID_HANDLE}, gridParams{BGFX_INVALID_HANDLE};
    void renderParticles(std::span<const RenderParticle>,std::span<const RenderParticleMaterial>,const Camera&,const fog::Parameters&,bgfx::ViewId,ParticleRenderStats*,std::span<const TransparentMeshDraw>,bool,const std::function<void()>&,MaterialEvaluationContext,bgfx::ViewId depthView=InvalidRenderView);
    std::shared_ptr<const ModelPose> poseFor(const RenderModelInstance&);
    void renderShadows(const RenderScene&,PointShadowRenderer&,bgfx::ViewId,bgfx::ViewId,unsigned,unsigned,float);
    bool prepareGlow(const SceneSettings&,ViewportTargets&,std::uint16_t,std::uint16_t,RenderViewPurpose);
    void executePost(const RenderPassPlan&,const SceneSettings&,ViewportTargets&,std::uint16_t,std::uint16_t,
        const EnvironmentPresentation&,const Mat4&,const Mat4&,std::span<const RenderCollisionOverlay>);
    void bindFog(const fog::Parameters& viewValues,bool receiveFog=true) {
        const auto values=fog::forMaterial(viewValues,receiveFog);
        bgfx::setUniform(fogColorDensity,values.colorDensity.data());
        bgfx::setUniform(fogDepth,values.depth.data());
        bgfx::setUniform(fogHeight,values.height.data());
        bgfx::setUniform(fogSky,values.sky.data());
        bgfx::setUniform(fogSunDirection,values.sunDirections.data(),4);
        bgfx::setUniform(fogSunColor,values.sunColors.data(),4);
    }
    void bindMaterialMapping(const Vec3& scale,const Vec3& offset,const Vec3& rotation,bool triplanar,bool world=false) {
        const Vec4 scaleValue{scale[0],scale[1],scale[2],triplanar?1.f:0.f};
        const Vec4 offsetValue{offset[0],offset[1],offset[2],triplanar && world?1.f:0.f};
        constexpr float radians=std::numbers::pi_v<float>/180;
        const Vec4 rotationValue{rotation[0]*radians,rotation[1]*radians,rotation[2]*radians,0};
        bgfx::setUniform(materialUvScale,scaleValue.data());bgfx::setUniform(materialUvOffset,offsetValue.data());bgfx::setUniform(materialUvRotation,rotationValue.data());
    }
    std::uint16_t renderTo(const RenderScene&,ViewportTargets&,EnvironmentLighting&,std::uint16_t,std::uint16_t,RenderViewIdentity,bool,bool,std::vector<std::shared_ptr<const ResidentModelLod>>* = nullptr,ParticleRenderStats* = nullptr,bool drawEnvironment=false);
    ~Impl() {
        if (!initialized) return;
        shadows.reset();thumbnailShadows.reset();destroyHandle(shadowProgram);
        collisionOverlays.reset();destroyHandle(collisionOverlayProgram);
        deformation.reset();bindPoses.clear();destroyHandle(deformationProgram);
        destroyHandle(directionalShadowProgram);destroyHandle(directionalCasterViewProjection);destroyHandle(directionalCasterTile);
        sphere.reset(); fullscreen.reset(); grid.reset(); particleQuad.reset(); models.clear(); streamModels.reset(); targets.reset(); particlePreviewTargets.reset(); gameTargets.reset(); pickTargets.reset(); thumbnails.clear();
        // Keep readback destination memory alive through the final bgfx frame,
        // but release every GPU handle before bgfx shutdown.
        if (pendingPick) pendingPick->targets.reset();
        if (pendingPick) { pendingPick->resources.clear(); pendingPick->deformations.clear(); pendingPick->graphMaterials.clear(); pendingPick->pinned.reset(); }
        if (pendingThumbnail) { pendingThumbnail->targets.reset(); destroyHandle(pendingThumbnail->readback); pendingThumbnail->pinned.reset(); pendingThumbnail->resources.clear(); }
        environment.reset();
        thumbnailEnvironment.reset();
        proceduralEnvironment.reset();thumbnailProceduralEnvironment.reset();
        materialPreviewTargets.reset();materialPreviewEnvironment.reset();
        destroyHandle(clusterComputeProgram);
        destroyHandle(surfaceLighting);destroyHandle(surfaceCategory);destroyHandle(directionLightingRoles);
        for(auto& item:ssgiPrograms)destroyHandle(item);
        for(auto& item:ssaoPrograms)destroyHandle(item);
        destroyHandle(ssgiAtlas);destroyHandle(ssgiSettings);destroyHandle(ssgiDraw);
        destroyHandle(debugViewUniform);destroyHandle(ssaoAtlas);destroyHandle(ssaoSettings);
        destroyHandle(skyProgram);destroyHandle(skyRight);destroyHandle(skyUp);destroyHandle(skyForward);destroyHandle(skySampler);
        destroyHandle(white);
        destroyHandle(pbr); destroyHandle(present); destroyHandle(gridProgram); destroyHandle(encode);
        destroyHandle(glowExtractProgram);destroyHandle(glowBlurProgram);
        destroyHandle(glowSource);destroyHandle(glowColor);destroyHandle(glowParams);destroyHandle(glowTexel);
        destroyHandle(particleProgram);
        destroyHandle(particleParameters); destroyHandle(particleParams); destroyHandle(particleSampler);
        submittedPins.clear(); previousSubmittedPins.clear();
        destroyHandle(modelProgram); destroyHandle(modelBase); destroyHandle(modelSpecular); destroyHandle(modelSurface); destroyHandle(modelEmissive);
        destroyHandle(pickProgram); destroyHandle(pickId); destroyHandle(lightCounts);
        destroyHandle(modelUv); destroyHandle(modelOptions); destroyHandle(modelMaps); destroyHandle(modelEmissiveMap); destroyHandle(modelNormalMatrix);destroyHandle(instanceOpacity);
        destroyHandle(materialUvScale);destroyHandle(materialUvOffset);destroyHandle(materialUvRotation);
        destroyHandle(metallicRoughnessSampler); destroyHandle(occlusionSampler); destroyHandle(emissiveSampler);
        destroyHandle(baseColorMetallic); destroyHandle(roughness); destroyHandle(textureFlags);
        destroyHandle(cameraPos); destroyHandle(direction); destroyHandle(directionColor);
        destroyHandle(presentParams); destroyHandle(hdrSampler); destroyHandle(compositeSampler);
        destroyHandle(toneCurve);destroyHandle(toneShoulder);
        destroyHandle(debandingParams);
        destroyHandle(background); destroyHandle(gridWidths);
        destroyHandle(fogColorDensity);destroyHandle(fogDepth);destroyHandle(fogHeight);
        destroyHandle(fogSky);destroyHandle(fogSunDirection);destroyHandle(fogSunColor);
        destroyHandle(albedoSampler); destroyHandle(roughnessSampler); destroyHandle(metallicSampler);
        destroyHandle(normalSampler); destroyHandle(irradianceSampler); destroyHandle(prefilteredSampler); destroyHandle(brdfSampler); destroyHandle(environmentParams);
        destroyHandle(ambientLight);
        destroyHandle(gridMinor); destroyHandle(gridMajor); destroyHandle(gridAxisU); destroyHandle(gridAxisV); destroyHandle(gridParams);
        bgfx::shutdown();
    }
};

Renderer::Renderer(SDL_Window* window, bgfx::CallbackI* callback, GraphicsBackend backend) : impl_(std::make_unique<Impl>()) {
    if (!window) throw std::invalid_argument("Renderer requires an SDL window.");
    if (!validGraphicsBackend(backend)) throw std::invalid_argument("Unsupported graphics backend.");
#if defined(_WIN32)
    void* hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd) throw std::runtime_error(std::string("SDL did not provide a native HWND: ") + SDL_GetError());
#else
    throw std::runtime_error("This renderer requires Windows and Direct3D 11 or 12.");
#endif
    int w = 1, h = 1;
    if (!SDL_GetWindowSizeInPixels(window, &w, &h)) throw std::runtime_error(std::string("Unable to query window pixels: ") + SDL_GetError());
    impl_->windowWidth = static_cast<std::uint32_t>(std::max(1,w));
    impl_->windowHeight = static_cast<std::uint32_t>(std::max(1,h));
    bgfx::renderFrame(); // Single-thread rendering on the initialization thread.
    bgfx::Init init;
    init.type = backend == GraphicsBackend::Direct3D12 ? bgfx::RendererType::Direct3D12 : bgfx::RendererType::Direct3D11;
    init.platformData.nwh = hwnd;
    init.callback = callback;
    init.resolution.width = impl_->windowWidth;
    init.resolution.height = impl_->windowHeight;
    init.resolution.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) throw std::runtime_error(std::string("Could not initialize ") + graphicsBackendName(backend) + ". Try --renderer dx11 to select the compatibility backend.");
    impl_->initialized = true;
    // Driver worker callbacks can finish after bgfx releases its device and
    // dynamically unloads D3D11. Keep their runtime code mapped until process
    // exit; all GPU resources and COM references still follow normal teardown.
    if (backend == GraphicsBackend::Direct3D11) {
        HMODULE d3d11Runtime{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,L"d3d11.dll",&d3d11Runtime))
            throw std::runtime_error("Could not retain the Direct3D 11 runtime through process shutdown.");
    }
    if (bgfx::getRendererType() != init.type) throw std::runtime_error(std::string(graphicsBackendName(backend)) + " was requested but another backend was selected.");
    if(bgfx::getCaps()->limits.maxViews<FirstPlatformView)throw std::runtime_error("This renderer requires at least 248 view slots for lighting, shadows, Glow and editor previews.");
    if (!bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_RT) ||
        !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT) ||
        !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT) ||
        !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::D24S8,BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY))
        throw std::runtime_error("This adapter cannot create the required viewport targets.");
    // bgfx exposes render-target support, but not a per-format blending bit.
    // The pre-init renderFrame() above makes this initialization thread the
    // render thread, as required by getInternalData(). Query the selected native
    // device synchronously; its borrowed pointer is never retained or released.
    const auto* internal=bgfx::getInternalData();
    bool blendableHdr{};
    if (backend == GraphicsBackend::Direct3D11) {
        auto* device=internal ? static_cast<ID3D11Device*>(internal->context) : nullptr;
        UINT formatSupport{};
        constexpr UINT required=D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
            D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_BLENDABLE;
        blendableHdr=device && SUCCEEDED(device->CheckFormatSupport(DXGI_FORMAT_R32G32B32A32_FLOAT,&formatSupport)) &&
            (formatSupport & required)==required;
    } else {
        auto* device=internal ? static_cast<ID3D12Device*>(internal->context) : nullptr;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{DXGI_FORMAT_R32G32B32A32_FLOAT};
        constexpr auto required=D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
            D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_BLENDABLE;
        blendableHdr=device && SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))) &&
            (support.Support1 & required)==required;
    }
    if (!blendableHdr)
        throw std::runtime_error("This adapter does not support blendable float32 HDR particle targets.");

    impl_->pbr = program(cc_vs_pbr,cc_fs_pbr,"Normal mapped GGX PBR with split-sum IBL");
    impl_->modelProgram = program(cc_vs_model,cc_fs_model,"glTF metallic roughness PBR with split-sum IBL");
    impl_->pickProgram = program(cc_vs_pick,cc_fs_pick,"Model entity ID picking with material alpha");
    impl_->pickId = uniform("u_pickId");
    impl_->modelBase = uniform("u_modelBase");
    impl_->modelSpecular = uniform("u_modelSpecular");
    impl_->modelSurface = uniform("u_modelSurface");
    impl_->modelEmissive = uniform("u_modelEmissive");
    impl_->modelUv = uniform("u_modelUv");
    impl_->modelOptions = uniform("u_modelOptions");
    impl_->modelMaps = uniform("u_modelMaps");
    impl_->modelEmissiveMap = uniform("u_modelEmissiveMap");
    impl_->modelNormalMatrix = uniform("u_modelNormalMatrix",bgfx::UniformType::Mat4);
    impl_->instanceOpacity = uniform("u_instanceOpacity");
    impl_->surfaceLighting=uniform("u_surfaceLighting");
    impl_->surfaceCategory=uniform("u_surfaceCategory");
    impl_->directionLightingRoles=uniform("u_directionLightingRoles",bgfx::UniformType::Vec4,4);
    impl_->materialUvScale=uniform("u_materialUvScale");impl_->materialUvOffset=uniform("u_materialUvOffset");impl_->materialUvRotation=uniform("u_materialUvRotation");
    impl_->metallicRoughnessSampler = uniform("s_metallicRoughness",bgfx::UniformType::Sampler);
    impl_->occlusionSampler = uniform("s_occlusion",bgfx::UniformType::Sampler);
    impl_->emissiveSampler = uniform("s_emissive",bgfx::UniformType::Sampler);
    impl_->present = program(cc_vs_present,cc_fs_present,"Linear Reinhard tone map");
    impl_->glowSource=uniform("s_glowSource",bgfx::UniformType::Sampler);
    impl_->glowParams=uniform("u_glowParams");impl_->glowTexel=uniform("u_glowTexel");
    // Glow is optional: failed GPU preparation preserves the ordinary HDR image.
    try {
        impl_->glowExtractProgram=program(cc_vs_present,cc_fs_glow_extract,"Glow exposed HDR extraction");
        impl_->glowBlurProgram=program(cc_vs_present,cc_fs_glow_blur,"Glow separable FP32 blur");
    } catch(const std::exception& error) {
        impl_->glowProgramFailure=error.what();destroyHandle(impl_->glowExtractProgram);destroyHandle(impl_->glowBlurProgram);
    }
    impl_->glowColor=uniform("s_glowColor",bgfx::UniformType::Sampler);
    impl_->gridProgram = program(cc_vs_grid,cc_fs_grid,"Quader analytic construction grid");
    impl_->collisionOverlayProgram=program(cc_vs_collision_overlay,cc_fs_collision_overlay,"Depth-tested collision overlays");
    impl_->collisionOverlays=std::make_unique<CollisionOverlayGpu>();
    impl_->encode = program(cc_vs_present,cc_fs_encode,"Viewport sRGB encoding");
    impl_->particleProgram = program(cc_vs_particle,cc_fs_particle,"Unified material sprite fallback");
    impl_->particleParameters=uniform(MaterialGraphParameterUniform,bgfx::UniformType::Vec4,MaterialGraphMaxParameterRows);
    impl_->particleParams=uniform("u_particleParams");
    impl_->particleSampler=uniform("s_particleColor",bgfx::UniformType::Sampler);
    impl_->skyProgram=program(cc_vs_present,cc_fs_sky,"Material preview HDRI background");
    impl_->shadowProgram=program(cc_vs_point_shadow,cc_fs_point_shadow,"Point-light masked depth caster");
    impl_->directionalShadowProgram=program(cc_vs_directional_shadow,cc_fs_directional_shadow,"Directional masked depth caster");
    impl_->directionalCasterViewProjection=uniform("u_directionalCasterViewProjection",bgfx::UniformType::Mat4);
    impl_->directionalCasterTile=uniform("u_directionalCasterTile");
    impl_->shadows=std::make_unique<PointShadowRenderer>();impl_->thumbnailShadows=std::make_unique<PointShadowRenderer>(false);
    const auto clusterShader=bgfx::createShader(bgfx::copy(cc_cs_cluster_lights,sizeof(cc_cs_cluster_lights)));
    if(bgfx::isValid(clusterShader))impl_->clusterComputeProgram=bgfx::createProgram(clusterShader,true);
    if(!bgfx::isValid(impl_->clusterComputeProgram))throw std::runtime_error("Forward+ compute program creation failed.");
    const auto computeProgram=[](const auto& bytes) {
        const auto shader=bgfx::createShader(bgfx::copy(bytes,sizeof(bytes)));
        return bgfx::isValid(shader)?bgfx::createProgram(shader,true):bgfx::ProgramHandle{BGFX_INVALID_HANDLE};
    };
    impl_->deformationProgram=computeProgram(cc_cs_model_deform);
    if(!bgfx::isValid(impl_->deformationProgram))throw std::runtime_error("Model GPU deformation program creation failed.");
    impl_->deformation=std::make_unique<ModelDeformationGpu>(impl_->deformationProgram);
    impl_->ssgiPrograms={computeProgram(cc_cs_ssgi_trace),computeProgram(cc_cs_ssgi_temporal),computeProgram(cc_cs_ssgi_filter),computeProgram(cc_cs_ssgi_publish)};
    impl_->ssaoPrograms={computeProgram(cc_cs_ssao_evaluate),computeProgram(cc_cs_ssao_publish)};
    impl_->ssgiAtlas=uniform("u_ssgiAtlas");impl_->ssgiSettings=uniform("u_ssgiSettings");impl_->ssgiDraw=uniform("u_ssgiDraw");
    impl_->debugViewUniform=uniform("u_debugView");
    impl_->ssaoAtlas=uniform("u_ssaoAtlas");impl_->ssaoSettings=uniform("u_ssaoSettings");
    impl_->skyRight=uniform("u_skyRight");impl_->skyUp=uniform("u_skyUp");impl_->skyForward=uniform("u_skyForward");
    impl_->skySampler=uniform("s_skyEnvironment",bgfx::UniformType::Sampler);
    impl_->programHashes={shaderContentHash(cc_vs_pbr,cc_fs_pbr),shaderContentHash(cc_vs_model,cc_fs_model),shaderContentHash(cc_vs_pick,cc_fs_pick),
        shaderContentHash(cc_vs_present,cc_fs_present),shaderContentHash(cc_vs_grid,cc_fs_grid),shaderContentHash(cc_vs_present,cc_fs_encode),
        shaderContentHash(cc_vs_particle,cc_fs_particle),shaderContentHash(cc_vs_present,cc_fs_sky),shaderContentHash(cc_vs_point_shadow,cc_fs_point_shadow),
        shaderContentHash(cc_vs_present,cc_fs_glow_extract),shaderContentHash(cc_vs_present,cc_fs_glow_blur),
        shaderContentHash(cc_vs_directional_shadow,cc_fs_directional_shadow),shaderContentHash({},cc_cs_cluster_lights),shaderContentHash({},cc_cs_procedural_filter),
        0,0,shaderContentHash({},cc_cs_model_deform),
        shaderContentHash(cc_vs_collision_overlay,cc_fs_collision_overlay),0,0,
        shaderContentHash({},cc_cs_ssgi_trace),shaderContentHash({},cc_cs_ssgi_temporal),shaderContentHash({},cc_cs_ssgi_filter),shaderContentHash({},cc_cs_ssgi_publish),
        shaderContentHash({},cc_cs_ssao_evaluate),shaderContentHash({},cc_cs_ssao_publish)};
    impl_->baseColorMetallic = uniform("u_baseColorMetallic");
    impl_->roughness = uniform("u_roughness");
    impl_->textureFlags = uniform("u_textureFlags");
    impl_->cameraPos = uniform("u_cameraPos");
    impl_->direction = uniform("u_direction",bgfx::UniformType::Vec4,static_cast<std::uint16_t>(MaxDirectionalLights));
    impl_->directionColor = uniform("u_directionColor",bgfx::UniformType::Vec4,static_cast<std::uint16_t>(MaxDirectionalLights));
    impl_->lightCounts = uniform("u_lightCounts");
    impl_->presentParams = uniform("u_presentParams");
    impl_->toneCurve=uniform("u_toneCurve");impl_->toneShoulder=uniform("u_toneShoulder");
    impl_->debandingParams = uniform("u_debandingParams");
    impl_->background=uniform("u_background");
    impl_->gridWidths=uniform("u_gridWidths");
    impl_->hdrSampler = uniform("s_hdrColor",bgfx::UniformType::Sampler);
    impl_->compositeSampler = uniform("s_compositeColor",bgfx::UniformType::Sampler);
    impl_->albedoSampler = uniform("s_albedo",bgfx::UniformType::Sampler);
    impl_->roughnessSampler = uniform("s_roughness",bgfx::UniformType::Sampler);
    impl_->metallicSampler = uniform("s_metallic",bgfx::UniformType::Sampler);
    impl_->normalSampler = uniform("s_normal",bgfx::UniformType::Sampler);
    impl_->irradianceSampler = uniform("s_irradiance",bgfx::UniformType::Sampler);
    impl_->prefilteredSampler = uniform("s_prefiltered",bgfx::UniformType::Sampler);
    impl_->brdfSampler = uniform("s_brdfLut",bgfx::UniformType::Sampler);
    impl_->environmentParams = uniform("u_environment");
    impl_->ambientLight = uniform("u_ambientLight");
    impl_->fogColorDensity = uniform("u_fogColorDensity");
    impl_->fogDepth = uniform("u_fogDepth");
    impl_->fogHeight = uniform("u_fogHeight");
    impl_->fogSky = uniform("u_fogSky");
    impl_->fogSunDirection = uniform("u_fogSunDirection",bgfx::UniformType::Vec4,4);
    impl_->fogSunColor = uniform("u_fogSunColor",bgfx::UniformType::Vec4,4);
    impl_->environment = std::make_unique<EnvironmentLighting>();
    impl_->thumbnailEnvironment=std::make_unique<EnvironmentLighting>();
    impl_->materialPreviewEnvironment=std::make_unique<EnvironmentLighting>();
    impl_->proceduralEnvironment=std::make_unique<ProceduralSkyGpu>();
    impl_->thumbnailProceduralEnvironment=std::make_unique<ProceduralSkyGpu>();
    impl_->gridMinor = uniform("u_gridMinor");
    impl_->gridMajor = uniform("u_gridMajor");
    impl_->gridAxisU = uniform("u_gridAxisU");
    impl_->gridAxisV = uniform("u_gridAxisV");
    impl_->gridParams = uniform("u_gridParams");
    constexpr std::uint8_t whitePixel[]{255,255,255,255};
    impl_->white = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_NONE,bgfx::copy(whitePixel,sizeof whitePixel));
    if (!bgfx::isValid(impl_->white)) throw std::runtime_error("Failed to create material fallback texture.");
    impl_->sphere.upload(makeUvSphere());
    impl_->fullscreen.upload({
        {{-1,-1,0,0,0,1,0,1}, {3,-1,0,0,0,1,2,1}, {-1,3,0,0,0,1,0,-1}}, {0,1,2}
    });
    impl_->grid.upload({
        {{-512,0,-512,0,1,0,0,0}, {512,0,-512,0,1,0,1,0},
         {512,0,512,0,1,0,1,1}, {-512,0,512,0,1,0,0,1}}, {0,2,1,0,3,2}
    });
    impl_->particleQuad.upload({
        {{-.5f,-.5f,0,0,0,1,0,0},{.5f,-.5f,0,0,0,1,1,0},
         {.5f,.5f,0,0,0,1,1,1},{-.5f,.5f,0,0,0,1,0,1}}, {0,1,2,0,2,3}
    });
    impl_->particleScratch.reserve(MaxParticleDrawCount);
    impl_->particleBatches.reserve(MaxParticleDrawCalls);
    if (!(bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING))
        impl_->particleStatus="This adapter does not support particle instancing.";
    resizeViewport(impl_->windowWidth,impl_->windowHeight);
    bgfx::setViewName(SceneView,"Scene: PBR linear HDR");
    bgfx::setViewName(PresentView,"Scene: linear exposure and Reinhard");
    bgfx::setViewName(GridView,"Scene: neutral analytic grid overlay");
    bgfx::setViewName(EncodeView,"Scene: viewport sRGB image");
    bgfx::setViewName(PickView,"Picking: on-demand model entity IDs");
    bgfx::setViewName(PickReadView,"Picking: pixel readback");
    bgfx::setViewName(ParticlePreviewViews.scene,"Particle preview HDR");
    bgfx::setViewName(ParticlePreviewViews.present,"Particle preview tone map");
    bgfx::setViewName(ParticlePreviewViews.grid,"Particle preview grid");
    bgfx::setViewName(ParticlePreviewViews.encode,"Particle preview sRGB");
    bgfx::setViewName(GameViews.scene,"Game: scene HDR");
    bgfx::setViewName(GameViews.present,"Game: tone map");
    bgfx::setViewName(GameViews.encode,"Game: sRGB");
    bgfx::setViewName(ToolsView,"Editor tools");
    for(const auto views:{MainViews,ParticlePreviewViews,MaterialPreviewViews,GameViews}) {
        bgfx::setViewName(views.glowExtract,"Glow: exposed HDR extraction");
        bgfx::setViewName(views.glowHorizontal,"Glow: horizontal blur");
        bgfx::setViewName(views.glowVertical,"Glow: vertical blur");
    }
    // Keep natural view IDs: this pinned bgfx backend compares blit order keys
    // with decoded view IDs. Moving PickReadView ahead of a lower-numbered
    // visible pass would copy the ID texture before its model draws complete.
    // Every shadow write precedes its consumers, and every image encode
    // precedes tools/native UI without remapping readback indices.
    std::array<bgfx::ViewId,FirstPlatformView> order{};
    for(bgfx::ViewId id=0;id<FirstPlatformView;++id)order[id]=id;
    bgfx::setViewOrder(0,static_cast<std::uint16_t>(order.size()),order.data());
    for (const auto id : order) bgfx::setViewMode(id,bgfx::ViewMode::Sequential);
    for(unsigned index=0;index<96;++index) {
        const auto name="World point shadow "+std::to_string(index/6)+" face "+std::to_string(index%6);
        bgfx::setViewName(static_cast<bgfx::ViewId>(PointShadowView+index),name.c_str());
        const auto preview="Thumbnail point shadow "+std::to_string(index/6)+" face "+std::to_string(index%6);
        bgfx::setViewName(static_cast<bgfx::ViewId>(ThumbnailPointShadowView+index),preview.c_str());
    }
    for(unsigned index=0;index<4;++index) {
        const auto sceneName="Scene directional shadow "+std::to_string(index);
        const auto gameName="Game directional shadow "+std::to_string(index);
        const auto thumbnailName="Thumbnail directional shadow "+std::to_string(index);
        bgfx::setViewName(static_cast<bgfx::ViewId>(DirectionalShadowView+index),sceneName.c_str());
        bgfx::setViewName(static_cast<bgfx::ViewId>(GameDirectionalShadowView+index),gameName.c_str());
        bgfx::setViewName(static_cast<bgfx::ViewId>(ThumbnailDirectionalShadowView+index),thumbnailName.c_str());
    }
}
Renderer::~Renderer() = default;

bool Renderer::prepareEnvironment(const std::filesystem::path& projectRoot, const std::string& sourceId) {
    const bool result=impl_->environment->prepare(projectRoot, sourceId); if (result) ++impl_->environmentRevision; return result;
}
bool Renderer::publishEnvironment(const PreparedEnvironment& data) { const bool result=impl_->environment->publish(data); if (result) ++impl_->environmentRevision; return result; }
bool Renderer::publishThumbnailEnvironment(const PreparedEnvironment& data) {
    const bool result=impl_->thumbnailEnvironment->publish(data);impl_->thumbnailStatus=result ? std::string{}:impl_->thumbnailEnvironment->status();return result;
}
const std::string& Renderer::environmentStatus() const {
    return impl_->proceduralActive?impl_->proceduralEnvironment->status():impl_->environment->status();
}
std::size_t Renderer::environmentResidentBytes() const {
    auto bytes=impl_->environment->residentBytes()+impl_->thumbnailEnvironment->residentBytes()+impl_->materialPreviewEnvironment->residentBytes()+
        impl_->proceduralEnvironment->residentBytes()+impl_->thumbnailProceduralEnvironment->residentBytes();
    const auto add=[&](const auto& target){if(target) {
        if(target->clustered)bytes+=target->clustered->residentBytes();
        if(target->ssgi)bytes+=target->ssgi->residentBytes();
        if(target->ssao)bytes+=target->ssao->residentBytes();
        bytes+=target->readableDepthBytes;
    }};
    add(impl_->targets);add(impl_->gameTargets);add(impl_->materialPreviewTargets);add(impl_->particlePreviewTargets);
    if(impl_->pendingThumbnail)add(impl_->pendingThumbnail->targets);
    return bytes;
}
const ClusteredLightingStats& Renderer::clusteredLightingStats() const {return impl_->clusterStats;}
const std::string& Renderer::clusteredLightingStatus() const {return impl_->clusterStatus;}
const std::string& Renderer::ssgiStatus() const {
    auto& status=impl_->ssgiStatusCache;status.clear();
    for(const auto* target:{impl_->targets.get(),impl_->gameTargets.get()})if(target) {
        const auto& value=target->ssgiFailure.empty()&&target->ssgi?target->ssgi->status():target->ssgiFailure;
        if(!value.empty()){if(!status.empty())status+='\n';status+="SSGI: "+value;}
    }
    return status;
}
std::size_t Renderer::ssgiResidentBytes() const {
    std::size_t bytes{};for(const auto* target:{impl_->targets.get(),impl_->gameTargets.get()})
        if(target&&target->ssgi)bytes+=target->ssgi->residentBytes();return bytes;
}
const std::string& Renderer::ssaoStatus() const {
    auto& status=impl_->ssaoStatusCache;status.clear();
    for(const auto* target:{impl_->targets.get(),impl_->gameTargets.get()})if(target) {
        const auto& value=target->ssaoFailure.empty()&&target->ssao?target->ssao->status():target->ssaoFailure;
        if(!value.empty()){if(!status.empty())status+='\n';status+="SSAO: "+value;}
    }
    return status;
}
std::size_t Renderer::ssaoResidentBytes() const {
    std::size_t bytes{};for(const auto* target:{impl_->targets.get(),impl_->gameTargets.get()})
        if(target&&target->ssao)bytes+=target->ssao->residentBytes();return bytes;
}

bool Renderer::prepareModel(const ModelAsset& model) {
    if (impl_->models.contains(model.id)) {
        impl_->modelStatus.clear();
        return true;
    }
    try {
        if (model.id.empty() || model.primitives.empty() || model.draws.empty() || model.materials.empty())
            throw std::invalid_argument("Model has no renderable geometry or materials.");
        auto next = std::make_shared<GpuModel>();
        next->id = model.id;
        next->primitives.resize(model.primitives.size());
        next->bounds.resize(model.primitives.size());
        for (std::size_t i = 0; i < model.primitives.size(); ++i) {
            if (model.primitives[i].material >= model.materials.size())
                throw std::invalid_argument("Model primitive references a missing material.");
            next->primitives[i].uploadModel(model.primitives[i]);
            for(const auto& vertex:model.primitives[i].vertices)math::include(next->bounds[i],vertex.position);
        }
        for (const auto& draw : model.draws) {
            if (draw.primitive >= next->primitives.size()) throw std::invalid_argument("Model draw references a missing primitive.");
        }
        impl_->models.emplace(model.id,std::move(next));
        impl_->modelStatus.clear();
        return true;
    } catch (const std::exception& error) {
        impl_->modelStatus = std::string("Model preparation failed: ") + error.what();
        return false;
    }
}
void Renderer::clearModel() { cancelPicking(); impl_->models.clear(); impl_->modelStatus.clear(); }
const std::string& Renderer::modelStatus() const { return impl_->modelStatus; }

void Renderer::configureModelStream(const std::string& id,std::uint64_t revision,const streaming::ModelStream& descriptor) { impl_->streamModels->configure(id,revision,descriptor); }
bool Renderer::queueModelLod(const std::string& id,std::uint64_t revision,std::shared_ptr<const ModelGeometryLevel> level) {
    const bool result=impl_->streamModels->queue(id,revision,std::move(level));
    if(!result) impl_->modelStatus=impl_->streamModels->status().empty() ? "Model LOD request does not match its streaming descriptor." : impl_->streamModels->status();
    return result;
}
void Renderer::advanceStreaming(std::size_t bytes,double milliseconds) {
    // This boundary runs once per application owner frame. Retain the previous
    // submitted scene/material/geometry leases for an additional frame, including test callers
    // that advance immediately before processing that frame's queued draws.
    impl_->previousSubmittedPins.clear();
    impl_->previousSubmittedPins.swap(impl_->submittedPins);
    impl_->streamModels->advance(bytes,milliseconds);
    // Advance uploads against last frame's Scene picking set, then collect the
    // new Scene set. Game and shadow residency do not affect pick signatures.
    impl_->streamModels->beginMainView();
    ++impl_->renderFrame;
    for(auto* target:{impl_->targets.get(),impl_->gameTargets.get()})if(target)target->advanceSsgi(impl_->renderFrame);
    impl_->collisionOverlays->trim(impl_->renderFrame);
    impl_->deformation->advance(impl_->renderFrame);
    std::erase_if(impl_->bindPoses,[](const auto& item){return item.second.asset.expired();});
    impl_->shadows->advance(impl_->renderFrame);impl_->thumbnailShadows->advance(impl_->renderFrame);
    if (impl_->renderFrame%120==0) for (auto& selections:impl_->lodSelections)
        std::erase_if(selections,[&](const auto& item){return impl_->renderFrame-item.second.frame>120;});
    if (!impl_->streamModels->status().empty()) impl_->modelStatus=impl_->streamModels->status();
}
bool Renderer::modelStreamReady(const std::string& id,std::uint64_t revision) const { return impl_->streamModels->ready(id,revision); }
bool Renderer::modelLodReady(const std::string& id,std::uint64_t revision,std::uint32_t primitive,std::uint32_t level) const {
    const auto resident=impl_->streamModels->resident(id,revision,primitive,level);return resident && resident->level==level;
}
void Renderer::evictModel(const std::string& id) {
    const bool legacy=impl_->models.erase(id)!=0;
    const auto prior=impl_->streamModels->signature(); impl_->streamModels->evict(id);
    if(legacy || prior!=impl_->streamModels->signature()) cancelPicking();
}
std::uint64_t Renderer::streamingSignature() const {
    std::uint64_t value=14695981039346656037ull;
    for (const auto item:{impl_->streamModels->signature(),impl_->externalSignature,impl_->programRevision,impl_->environmentRevision}) { value^=item; value*=1099511628211ull; }
    return value;
}
std::uint64_t Renderer::pickResourceSignature(PickMode mode) const {
    if(mode==PickMode::Selection)return streamingSignature();
    std::uint64_t value=14695981039346656037ull;
    for(const auto item:{impl_->streamModels->signature(),impl_->geometrySceneSignature,impl_->programRevision}) {value^=item;value*=1099511628211ull;}
    return value;
}
void Renderer::setExternalResourceSignature(std::uint64_t value) { impl_->externalSignature=value; }
std::size_t Renderer::modelResidentBytes() const {
    auto bytes=impl_->streamModels->bytes()+impl_->deformation->bytes()+impl_->collisionOverlays->residentBytes();
    for(const auto& [id,model]:impl_->models){(void)id;for(const auto& primitive:model->primitives)bytes+=primitive.bytes;}
    return bytes;
}
std::uint64_t Renderer::collisionOverlayUploads() const {return impl_->collisionOverlays->uploads();}
void Renderer::trimModelCache(std::size_t bytes,std::span<const std::string> live) {
    impl_->deformation->trim(bytes);
    auto fixed=impl_->deformation->bytes()+impl_->collisionOverlays->residentBytes();for(const auto& [id,model]:impl_->models){(void)id;for(const auto& primitive:model->primitives)fixed+=primitive.bytes;}
    impl_->streamModels->trim(bytes>fixed?bytes-fixed:0,live);
}
void Renderer::setAppearance(const ViewportAppearance& appearance) { impl_->appearance=appearance; }
void Renderer::setGridVisible(bool visible) { impl_->gridVisible=visible; }
void Renderer::setDebugView(RenderViewPurpose purpose,RenderDebugView value) {
    if(purpose!=RenderViewPurpose::Scene&&purpose!=RenderViewPurpose::Game)return;
    if(value<RenderDebugView::Lit||value>RenderDebugView::IndirectLighting)value=RenderDebugView::Lit;
    impl_->debugViews[purpose==RenderViewPurpose::Game?1:0]=value;
}
RenderDebugView Renderer::debugView(RenderViewPurpose purpose) const noexcept {
    return purpose==RenderViewPurpose::Scene?impl_->debugViews[0]:purpose==RenderViewPurpose::Game?impl_->debugViews[1]:RenderDebugView::Lit;
}
bool Renderer::thumbnailBusy() const { return !!impl_->pendingThumbnail; }
bool Renderer::renderThumbnail(const std::string& id,std::uint64_t signature,const RenderScene& scene,std::uint16_t size,std::shared_ptr<const void> pinned) {
    if (impl_->pendingThumbnail || id.empty() || !size || size>512 ||
        (bgfx::getCaps()->supported&(BGFX_CAPS_TEXTURE_BLIT|BGFX_CAPS_TEXTURE_READ_BACK))!=(BGFX_CAPS_TEXTURE_BLIT|BGFX_CAPS_TEXTURE_READ_BACK)) return false;
    try {
        auto pending=std::make_unique<PendingThumbnail>(); pending->id=id; pending->signature=signature; pending->pinned=pinned ? std::move(pinned):scene.pinned;
        pending->targets=std::make_unique<ViewportTargets>(); pending->targets->create(size,size);
        pending->readback=bgfx::createTexture2D(size,size,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
        if (!bgfx::isValid(pending->readback)) return false;
        pending->image={size,size,std::vector<std::uint8_t>(static_cast<std::size_t>(size)*size*4)};
        const bool ready=bgfx::isValid(impl_->thumbnailEnvironment->diffuse());
        struct RestoreAppearance { ViewportAppearance& destination; ViewportAppearance saved; ~RestoreAppearance(){destination=saved;} } restore{impl_->appearance,impl_->appearance};
        impl_->appearance=ViewportAppearance{};
        const auto readbackView=impl_->renderTo(scene,*pending->targets,ready ? *impl_->thumbnailEnvironment:*impl_->environment,size,size,{RenderViewPurpose::Thumbnail,signature},false,false,&pending->resources);
        if(readbackView==InvalidRenderView)throw std::logic_error("Thumbnail plan has no readback pass.");
        bgfx::blit(readbackView,pending->readback,0,0,pending->targets->display,0,0,size,size);
        pending->readyFrame=bgfx::readTexture(pending->readback,pending->image.rgba.data());
        impl_->pendingThumbnail=std::move(pending);impl_->thumbnailStatus.clear(); return true;
    } catch (const std::exception& error) { impl_->thumbnailStatus=std::string("Thumbnail: ")+error.what(); return false; }
}
std::optional<ThumbnailResult> Renderer::pollThumbnail(std::uint32_t frame) {
    if (!impl_->pendingThumbnail || !picking::frameReady(frame,impl_->pendingThumbnail->readyFrame)) return {};
    auto pending=std::move(impl_->pendingThumbnail);
    if (bgfx::getCaps()->originBottomLeft) {
        const auto pitch=static_cast<std::size_t>(pending->image.width)*4;
        for (std::uint32_t y=0;y<pending->image.height/2;++y) {
            auto top=pending->image.rgba.begin()+y*pitch,bottom=pending->image.rgba.begin()+(pending->image.height-1-y)*pitch;
            std::swap_ranges(top,top+pitch,bottom);
        }
    }
    return ThumbnailResult{std::move(pending->id),pending->signature,std::move(pending->image)};
}
bool Renderer::setThumbnail(const std::string& id,std::uint64_t signature,const DecodedImage& image) {
    return impl_->thumbnails.publish(id,signature,image);
}
bgfx::TextureHandle Renderer::thumbnailTexture(const std::string& id,std::uint64_t signature) const {
    return impl_->thumbnails.find(id,signature);
}
void Renderer::evictThumbnail(const std::string& id) { impl_->thumbnails.erase(id); }
std::size_t Renderer::thumbnailResidentBytes() const {
    auto bytes=impl_->thumbnails.bytes();
    if (impl_->pendingThumbnail) bytes+=static_cast<std::size_t>(impl_->pendingThumbnail->image.width)*impl_->pendingThumbnail->image.height*36;
    return bytes;
}
void Renderer::trimThumbnailCache(std::size_t target) {
    const auto pending=impl_->pendingThumbnail?static_cast<std::size_t>(impl_->pendingThumbnail->image.width)*impl_->pendingThumbnail->image.height*36:0;
    impl_->thumbnails.trim(target>pending?target-pending:0);
}
const std::string& Renderer::thumbnailStatus() const { return impl_->thumbnailStatus; }
bool Renderer::reloadPrograms(std::span<const RendererShaderPair> pairs,std::span<const std::uint8_t> proceduralCompute,
    std::span<const std::uint8_t> clusterCompute,std::span<const std::uint8_t> deformationCompute,
    std::span<const std::uint8_t> ssgiTrace,
    std::span<const std::uint8_t> ssgiTemporal,std::span<const std::uint8_t> ssgiFilter,std::span<const std::uint8_t> ssgiPublish,
    std::span<const std::uint8_t> ssaoEvaluate,std::span<const std::uint8_t> ssaoPublish) {
    std::vector<std::pair<bgfx::ProgramHandle*,bgfx::ProgramHandle>> candidates;
    std::vector<std::pair<std::size_t,std::uint64_t>> hashes;
    bgfx::ProgramHandle worldCompute{BGFX_INVALID_HANDLE},thumbnailCompute{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle nextClusterCompute{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle nextDeformation{BGFX_INVALID_HANDLE};
    std::array<bgfx::ProgramHandle,4> nextSsgi{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    const std::array<std::span<const std::uint8_t>,4> giBytes{ssgiTrace,ssgiTemporal,ssgiFilter,ssgiPublish};
    std::array<bgfx::ProgramHandle,2> nextSsao{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    const std::array<std::span<const std::uint8_t>,2> ssaoBytes{ssaoEvaluate,ssaoPublish};
    try {
        candidates.reserve(pairs.size());hashes.reserve(pairs.size());
        const auto prepareCompute=[&](std::span<const std::uint8_t> bytes,std::span<const std::uint8_t> contract,
            std::size_t hashIndex,bgfx::ProgramHandle& target) {
            if(bytes.empty()||shaderContentHash({},bytes)==impl_->programHashes[hashIndex])return;
            std::string diagnostic;
            if(!preflightComputeShader(bytes,contract,diagnostic))throw std::runtime_error(diagnostic);
            const auto shader=bgfx::createShader(bgfx::copy(bytes.data(),static_cast<std::uint32_t>(bytes.size())));
            if(bgfx::isValid(shader))target=bgfx::createProgram(shader,true);
            if(!bgfx::isValid(target))throw std::runtime_error("Compute shader hot-reload allocation failed.");
        };
        prepareCompute(deformationCompute,cc_cs_model_deform,16,nextDeformation);
        prepareCompute(ssgiTrace,cc_cs_ssgi_trace,20,nextSsgi[0]);prepareCompute(ssgiTemporal,cc_cs_ssgi_temporal,21,nextSsgi[1]);
        prepareCompute(ssgiFilter,cc_cs_ssgi_filter,22,nextSsgi[2]);prepareCompute(ssgiPublish,cc_cs_ssgi_publish,23,nextSsgi[3]);
        prepareCompute(ssaoEvaluate,cc_cs_ssao_evaluate,24,nextSsao[0]);
        prepareCompute(ssaoPublish,cc_cs_ssao_publish,25,nextSsao[1]);
        if(!clusterCompute.empty() && shaderContentHash({},clusterCompute)!=impl_->programHashes[12]) {
            std::string diagnostic;
            if(!preflightComputeShader(clusterCompute,cc_cs_cluster_lights,diagnostic))throw std::runtime_error(diagnostic);
            const auto shader=bgfx::createShader(bgfx::copy(clusterCompute.data(),static_cast<std::uint32_t>(clusterCompute.size())));
            if(bgfx::isValid(shader))nextClusterCompute=bgfx::createProgram(shader,true);
            if(!bgfx::isValid(nextClusterCompute))throw std::runtime_error("Forward+ compute hot-reload allocation failed.");
        }
        const auto sky=std::find_if(pairs.begin(),pairs.end(),[](const auto& pair){return pair.program==RendererProgram::Sky;});
        if(sky!=pairs.end() && proceduralCompute.empty() &&
            shaderContentHash(sky->vertex,sky->fragment)!=impl_->programHashes[static_cast<std::size_t>(RendererProgram::Sky)])
            throw std::runtime_error("Sky reload requires its matching environment compute shader.");
        if(!proceduralCompute.empty() && shaderContentHash({},proceduralCompute)!=impl_->programHashes[13]) {
            if(sky==pairs.end())throw std::runtime_error("Environment compute reload requires its matching visible sky shader.");
            std::string diagnostic;
            worldCompute=impl_->proceduralEnvironment->prepareProgramReload(proceduralCompute,diagnostic);
            if(!bgfx::isValid(worldCompute))throw std::runtime_error(diagnostic);
            thumbnailCompute=impl_->thumbnailProceduralEnvironment->prepareProgramReload(proceduralCompute,diagnostic);
            if(!bgfx::isValid(thumbnailCompute))throw std::runtime_error(diagnostic);
        }
        for (const auto& pair:pairs) {
            bgfx::ProgramHandle* target{}; std::span<const std::uint8_t> contractV,contractF;
            switch (pair.program) {
            case RendererProgram::Sphere: target=&impl_->pbr; contractV=cc_vs_pbr; contractF=cc_fs_pbr; break;
            case RendererProgram::Model: target=&impl_->modelProgram; contractV=cc_vs_model; contractF=cc_fs_model; break;
            case RendererProgram::Picking: target=&impl_->pickProgram; contractV=cc_vs_pick; contractF=cc_fs_pick; break;
            case RendererProgram::Present: target=&impl_->present; contractV=cc_vs_present; contractF=cc_fs_present; break;
            case RendererProgram::Grid: target=&impl_->gridProgram; contractV=cc_vs_grid; contractF=cc_fs_grid; break;
            case RendererProgram::Encode: target=&impl_->encode; contractV=cc_vs_present; contractF=cc_fs_encode; break;
            case RendererProgram::Particle: target=&impl_->particleProgram; contractV=cc_vs_particle; contractF=cc_fs_particle; break;
              case RendererProgram::Sky: target=&impl_->skyProgram; contractV=cc_vs_present; contractF=cc_fs_sky; break;
            case RendererProgram::PointShadow: target=&impl_->shadowProgram; contractV=cc_vs_point_shadow; contractF=cc_fs_point_shadow; break;
            case RendererProgram::DirectionalShadow: target=&impl_->directionalShadowProgram; contractV=cc_vs_directional_shadow; contractF=cc_fs_directional_shadow; break;
            case RendererProgram::GlowExtract: target=&impl_->glowExtractProgram; contractV=cc_vs_present; contractF=cc_fs_glow_extract; break;
            case RendererProgram::GlowBlur: target=&impl_->glowBlurProgram; contractV=cc_vs_present; contractF=cc_fs_glow_blur; break;
            case RendererProgram::CollisionOverlay:target=&impl_->collisionOverlayProgram;contractV=cc_vs_collision_overlay;contractF=cc_fs_collision_overlay;break;
            }
            if (!target || std::any_of(candidates.begin(),candidates.end(),[&](const auto& value){return value.first==target;})) throw std::runtime_error("Shader reload batch has an invalid or duplicated program.");
            std::string diagnostic;
            if (!preflightShaderPair(pair.vertex,pair.fragment,contractV,contractF,diagnostic)) throw std::runtime_error(diagnostic);
            const auto vertex=bgfx::createShader(bgfx::copy(pair.vertex.data(),static_cast<std::uint32_t>(pair.vertex.size())));
            const auto fragment=bgfx::createShader(bgfx::copy(pair.fragment.data(),static_cast<std::uint32_t>(pair.fragment.size())));
            if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
                auto v=vertex,f=fragment; destroyHandle(v); destroyHandle(f); throw std::runtime_error("Hot-reload shader allocation failed.");
            }
            const auto program=bgfx::createProgram(vertex,fragment,true);
            if (!bgfx::isValid(program)) throw std::runtime_error("Hot-reload program creation failed.");
            candidates.emplace_back(target,program);
            hashes.emplace_back(static_cast<std::size_t>(pair.program),shaderContentHash(pair.vertex,pair.fragment));
        }
        // Nothing above changes a live owner or its bindings. Rejecting any
        // compute or graphics candidate leaves the entire prior group active.
        const bool programsChanged=!candidates.empty()||bgfx::isValid(nextClusterCompute)||bgfx::isValid(worldCompute)||
            bgfx::isValid(nextDeformation)||
            std::any_of(nextSsgi.begin(),nextSsgi.end(),[](auto item){return bgfx::isValid(item);})||
            std::any_of(nextSsao.begin(),nextSsao.end(),[](auto item){return bgfx::isValid(item);});
        for (auto& [target,candidate]:candidates) { destroyHandle(*target); *target=candidate; }
        if(bgfx::isValid(nextClusterCompute)) {
            destroyHandle(impl_->clusterComputeProgram);impl_->clusterComputeProgram=nextClusterCompute;
            nextClusterCompute=BGFX_INVALID_HANDLE;impl_->programHashes[12]=shaderContentHash({},clusterCompute);
        }
        if(bgfx::isValid(nextDeformation)) {
            destroyHandle(impl_->deformationProgram);impl_->deformationProgram=nextDeformation;
            nextDeformation=BGFX_INVALID_HANDLE;impl_->programHashes[16]=shaderContentHash({},deformationCompute);
            impl_->deformation->setProgram(impl_->deformationProgram);
        }
        for(std::size_t i=0;i<nextSsgi.size();++i)if(bgfx::isValid(nextSsgi[i])) {
            destroyHandle(impl_->ssgiPrograms[i]);impl_->ssgiPrograms[i]=nextSsgi[i];nextSsgi[i]=BGFX_INVALID_HANDLE;
            impl_->programHashes[20+i]=shaderContentHash({},giBytes[i]);
        }
        for(std::size_t i=0;i<nextSsao.size();++i)if(bgfx::isValid(nextSsao[i])) {
            destroyHandle(impl_->ssaoPrograms[i]);impl_->ssaoPrograms[i]=nextSsao[i];nextSsao[i]=BGFX_INVALID_HANDLE;
            impl_->programHashes[24+i]=shaderContentHash({},ssaoBytes[i]);
        }
        if(bgfx::isValid(worldCompute)) {
            impl_->proceduralEnvironment->publishProgramReload(worldCompute);
            impl_->thumbnailProceduralEnvironment->publishProgramReload(thumbnailCompute);
            worldCompute=BGFX_INVALID_HANDLE;thumbnailCompute=BGFX_INVALID_HANDLE;
            impl_->programHashes[13]=shaderContentHash({},proceduralCompute);
            impl_->proceduralFrame=UINT64_MAX;
        }
        for (const auto& [index,hash]:hashes) impl_->programHashes[index]=hash;
        if(bgfx::isValid(impl_->glowExtractProgram) && bgfx::isValid(impl_->glowBlurProgram))impl_->glowProgramFailure.clear();
        if (programsChanged) {
            ++impl_->programRevision;impl_->shadows->invalidate();impl_->thumbnailShadows->invalidate();cancelPicking();
            for(auto* target:{impl_->targets.get(),impl_->gameTargets.get()})if(target) {
                target->ssgiFailure.clear();if(target->ssgi)target->ssgi->invalidate();
                target->ssaoFailure.clear();
            }
        }
        impl_->shaderStatus.clear(); return true;
    } catch (const std::exception& error) {
        for (auto& [target,candidate]:candidates) { (void)target; destroyHandle(candidate); }
        destroyHandle(worldCompute);destroyHandle(thumbnailCompute);
        destroyHandle(nextClusterCompute);
        destroyHandle(nextDeformation);
        for(auto& item:nextSsgi)destroyHandle(item);
        for(auto& item:nextSsao)destroyHandle(item);
        impl_->shaderStatus=error.what(); return false;
    }
}
const std::string& Renderer::shaderStatus() const { return impl_->shaderStatus; }
std::uint64_t Renderer::shaderSignature() const {
    std::uint64_t hash=14695981039346656037ull;
    for (const auto value:impl_->programHashes) {hash^=value;hash*=1099511628211ull;} return hash;
}

bool Renderer::requestPick(const PickRequest& request) {
    const auto required = BGFX_CAPS_TEXTURE_BLIT | BGFX_CAPS_TEXTURE_READ_BACK;
    if ((bgfx::getCaps()->supported & required) != required) {
        impl_->pickingStatus = "This adapter does not support asynchronous viewport picking.";
        return false;
    }
    if (!drawable() || request.x >= impl_->width || request.y >= impl_->height) return false;
    if (!impl_->pickTargets) {
        try {
            auto targets = std::make_shared<PickTargets>();
            targets->create(static_cast<std::uint16_t>(impl_->width),static_cast<std::uint16_t>(impl_->height));
            impl_->pickTargets = std::move(targets);
        } catch (const std::exception& error) {
            impl_->pickingStatus = error.what();
            return false;
        }
    }
    impl_->latestPickSerial = request.serial;
    impl_->requestedPick = request;
    impl_->requestedPickContext=impl_->displayedMaterialContext;
    if (!impl_->requestedPick->resourceSignature) impl_->requestedPick->resourceSignature=pickResourceSignature(request.mode);
    impl_->pickingStatus.clear();
    return true;
}

std::optional<PickResult> Renderer::pollPick(std::uint32_t bgfxFrame) {
    if (!impl_->pendingPick || !picking::frameReady(bgfxFrame,impl_->pendingPick->readyFrame)) return {};
    auto pending = std::move(impl_->pendingPick);
    if (pending->epoch != impl_->pickEpoch || pending->request.serial != impl_->latestPickSerial) return {};
    const auto draw = pending->entities.resolveDraw(pending->pixel);
    if (!draw) {
        impl_->pickingStatus = "Viewport picking returned an invalid entity ID.";
        return {};
    }
    if (pending->request.resourceSignature && pending->request.resourceSignature!=pickResourceSignature(pending->request.mode)) return {};
    return PickResult{pending->request.generation,pending->request.revision,pending->request.serial,draw->entityId,
        pending->request.resourceSignature,draw->materialSlot,pending->request.mode};
}

void Renderer::cancelPicking() { ++impl_->pickEpoch; impl_->requestedPick.reset(); }
std::uint32_t Renderer::drainPending(std::uint32_t frame,std::uint32_t maximum) {
    cancelPicking();
    for(std::uint32_t count=0;count<maximum && (impl_->pendingPick || impl_->pendingThumbnail);++count) {
        frame=bgfx::frame();(void)pollPick(frame);(void)pollThumbnail(frame);
    }
    return frame;
}
const std::string& Renderer::pickingStatus() const { return impl_->pickingStatus; }

void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    // App owns native-window visibility. A minimized main swap chain keeps its
    // last valid size while detached windows continue using offscreen targets.
    if (!width || !height) return;
    if (width != impl_->windowWidth || height != impl_->windowHeight) {
        bgfx::reset(width,height,BGFX_RESET_VSYNC);
        impl_->windowWidth = width; impl_->windowHeight = height;
    }
}
void Renderer::resizeViewport(std::uint32_t width, std::uint32_t height) {
    if (!width || !height) { cancelPicking(); impl_->viewportDrawable = false; return; }
    if (impl_->targets && width == impl_->width && height == impl_->height) { impl_->viewportDrawable = true; return; }
    auto next = std::make_unique<ViewportTargets>();
    next->create(width,height); // Old targets survive allocation failure.
    cancelPicking();
    impl_->pickTargets.reset();
    impl_->targets = std::move(next);
    impl_->width = width; impl_->height = height; impl_->viewportDrawable = true;
}

void Renderer::resizeGameViewport(std::uint32_t width,std::uint32_t height) {
    if (!width || !height) { impl_->gameDrawable=false; return; }
    if (impl_->gameTargets && width==impl_->gameWidth && height==impl_->gameHeight) {
        impl_->gameDrawable=true; return;
    }
    auto targets=std::make_unique<ViewportTargets>(); targets->create(width,height);
    impl_->gameTargets=std::move(targets);
    impl_->gameWidth=width; impl_->gameHeight=height; impl_->gameDrawable=true;
}
void Renderer::renderGame(const RenderScene& scene) {
    if (!impl_->gameDrawable || !impl_->gameTargets) {
        impl_->gameParticles={scene.particles.size(),0,0,scene.particles.size()}; return;
    }
    impl_->renderTo(scene,*impl_->gameTargets,*impl_->environment,static_cast<std::uint16_t>(impl_->gameWidth),
        static_cast<std::uint16_t>(impl_->gameHeight),{RenderViewPurpose::Game},false,false,nullptr,&impl_->gameParticles);
}
void Renderer::renderFrame(std::shared_ptr<const PreparedRenderFrame> frame) {
    if(!frame)throw std::invalid_argument("Cannot submit an empty prepared render frame.");
    const auto error=validatePreparedRenderFrame(*frame);
    if(error!=PreparedFrameError::None)throw std::invalid_argument(preparedFrameErrorText(error));
    setExternalResourceSignature(frame->resourceSignature);
    setAppearance(frame->appearance);
    setGridVisible(frame->gridVisible);
    // The complete owner includes camera-local receiver vectors, the shared
    // world values and all exact-version model/material leases.
    for(const auto& request:frame->views) {
        auto scene=preparedRenderScene(*frame,request);
        scene.pinned=frame;
        if(request.identity.purpose==RenderViewPurpose::Scene) {
            resizeViewport(request.width,request.height);
            render(scene);
        } else {
            resizeGameViewport(request.width,request.height);
            renderGame(scene);
        }
    }
}
bgfx::TextureHandle Renderer::gameViewportTexture() const noexcept {
    return impl_->gameDrawable && impl_->gameTargets ? impl_->gameTargets->display : bgfx::TextureHandle{bgfx::kInvalidHandle};
}
void Renderer::releaseGameViewport() {
    impl_->gameTargets.reset(); impl_->gameDrawable=false;
    impl_->gameWidth=impl_->gameHeight=0; impl_->gameParticles={};
}
const ParticleRenderStats& Renderer::gameParticleStats() const noexcept { return impl_->gameParticles; }

void Renderer::resizeParticlePreview(std::uint32_t width,std::uint32_t height) {
    if (!width || !height) { impl_->particlePreviewDrawable=false; return; }
    if (impl_->particlePreviewTargets && width==impl_->particlePreviewWidth && height==impl_->particlePreviewHeight) {
        impl_->particlePreviewDrawable=true; return;
    }
    auto next=std::make_unique<ViewportTargets>();
    next->create(width,height); // Preserve the last usable preview on allocation failure.
    impl_->particlePreviewTargets=std::move(next);
    impl_->particlePreviewWidth=width; impl_->particlePreviewHeight=height; impl_->particlePreviewDrawable=true;
}
void Renderer::renderParticlePreview(const SceneSettings& settings,std::span<const RenderParticle> particles,
                                    std::span<const RenderParticleMaterial> materials,MaterialEvaluationContext context) {
    if (!impl_->particlePreviewDrawable || !impl_->particlePreviewTargets) {
        impl_->previewParticles={particles.size(),0,0,particles.size()}; return;
    }
    RenderScene scene{settings,{}};
    scene.sceneMode=true; scene.particles=particles; scene.particleMaterials=materials;scene.materialContext=context;
    struct RestoreStatus { std::string& destination; std::string saved; ~RestoreStatus(){destination=std::move(saved);} } restore{impl_->modelStatus,impl_->modelStatus};
    impl_->renderTo(scene,*impl_->particlePreviewTargets,*impl_->environment,
        static_cast<std::uint16_t>(impl_->particlePreviewWidth),static_cast<std::uint16_t>(impl_->particlePreviewHeight),
        {RenderViewPurpose::ParticlePreview},true,false,nullptr,&impl_->previewParticles);
}
bgfx::TextureHandle Renderer::particlePreviewTexture() const noexcept {
    if (impl_->particlePreviewDrawable && impl_->particlePreviewTargets) return impl_->particlePreviewTargets->display;
    return BGFX_INVALID_HANDLE;
}
void Renderer::releaseParticlePreview() {
    impl_->particlePreviewTargets.reset(); impl_->particlePreviewDrawable=false;
    impl_->particlePreviewWidth=impl_->particlePreviewHeight=0; impl_->previewParticles={};
}
const ParticleRenderStats& Renderer::particleStats() const noexcept { return impl_->particles; }
const ParticleRenderStats& Renderer::particlePreviewStats() const noexcept { return impl_->previewParticles; }
const std::string& Renderer::particleStatus() const { return impl_->particleStatus; }
const std::string& Renderer::glowStatus() const {
    auto& status=impl_->glowStatusCache;status=impl_->glowProgramFailure;
    for(const auto* target:{impl_->targets.get(),impl_->particlePreviewTargets.get(),impl_->materialPreviewTargets.get(),impl_->gameTargets.get()})
        if(target && !target->glowFailure.empty()) {
            if(!status.empty())status+='\n';status+=target->glowFailure;
        }
    return status;
}
std::size_t Renderer::glowResidentBytes() const noexcept {
    std::size_t bytes{};
    for(const auto* target:{impl_->targets.get(),impl_->particlePreviewTargets.get(),impl_->materialPreviewTargets.get(),impl_->gameTargets.get()})
        if(target && target->glow)bytes+=target->glow->bytes();
    return bytes;
}
std::size_t Renderer::shadowResidentBytes() const {return impl_->shadows->residentBytes()+impl_->thumbnailShadows->residentBytes();}
const PointShadowStats& Renderer::shadowStats() const {return impl_->shadows->stats();}
const std::string& Renderer::shadowStatus() const {return impl_->shadowStatus;}

void Renderer::resizeMaterialPreview(std::uint32_t width,std::uint32_t height) {
    if(!width || !height) {impl_->materialPreviewDrawable=false;return;}
    if(impl_->materialPreviewTargets && width==impl_->materialPreviewWidth && height==impl_->materialPreviewHeight) {impl_->materialPreviewDrawable=true;return;}
    auto target=std::make_unique<ViewportTargets>();target->create(width,height);
    impl_->materialPreviewTargets=std::move(target);impl_->materialPreviewWidth=width;impl_->materialPreviewHeight=height;impl_->materialPreviewDrawable=true;
}
bool Renderer::publishMaterialPreviewEnvironment(const PreparedEnvironment& data) { return impl_->materialPreviewEnvironment->publish(data); }
void Renderer::renderMaterialPreview(const SceneSettings& settings,const MaterialAsset& material,const ModelMaterialTextures& bindings,
                                    MaterialPreviewMesh mesh,const Mat4& transform,bool showEnvironment,std::shared_ptr<const void> pins,MaterialEvaluationContext context) {
    if(!impl_->materialPreviewDrawable || !impl_->materialPreviewTargets || !bgfx::isValid(impl_->materialPreviewEnvironment->diffuse()))return;
    struct RestoreStatus {std::string& target;std::string old;~RestoreStatus(){target=std::move(old);}} restore{impl_->modelStatus,impl_->modelStatus};
    if(usesParticleMaterialPreview(mesh,bindings)) {
        RenderParticle particle;particle.position={};particle.size=2;particle.color={1,1,1,1};particle.billboardMode=ParticleBillboardMode::Enabled;
        RenderParticleMaterial sprite;
        sprite.graph=bindings.graph;
        sprite.baseColor=sprite.graph?Vec4{1,1,1,1}:material.surface.baseColor;
        sprite.blend=sprite.graph?sprite.graph->blend:material.blend;
        sprite.shading=sprite.graph?sprite.graph->shading:material.shading;
        sprite.roughness=material.surface.roughness;sprite.metallic=material.surface.metallic;sprite.specular=material.surface.specular;
        sprite.emission=material.surface.emissive;sprite.alphaCutoff=material.surface.alphaCutoff;sprite.doubleSided=material.surface.doubleSided;
        sprite.receiveFog=material.surface.receiveFog;
        sprite.colorTexture=bindings.handles[0];sprite.samplerFlags=bindings.flags[0];sprite.pinned=pins;
        sprite.uvScale=material.surface.uvScale;sprite.uvOffset=material.surface.uvOffset;sprite.uvRotation=material.surface.uvRotation;
        sprite.triplanar=material.surface.triplanar;sprite.triplanarWorld=material.surface.triplanarWorld;
        RenderScene scene{settings,{}};scene.sceneMode=true;scene.particles={&particle,1};scene.particleMaterials={&sprite,1};scene.pinned=std::move(pins);scene.materialContext=context;
        impl_->renderTo(scene,*impl_->materialPreviewTargets,*impl_->materialPreviewEnvironment,
            static_cast<std::uint16_t>(impl_->materialPreviewWidth),static_cast<std::uint16_t>(impl_->materialPreviewHeight),{RenderViewPurpose::MaterialPreview},false,false,nullptr,nullptr,showEnvironment);
        return;
    }
    const auto effectiveMesh=mesh==MaterialPreviewMesh::Sprite?MaterialPreviewMesh::Plane:mesh;
    const auto geometry=materialPreviewGeometry(effectiveMesh);
    configureModelStream(geometry->id,1,*geometry->stream);
    if(!modelLodReady(geometry->id,1,0,0))queueModelLod(geometry->id,1,materialPreviewGeometryLevel(effectiveMesh));
    std::vector<ModelMaterial> surfaces{material.surface};surfaces.front().alphaMask=material.blend==MaterialBlend::Mask;
    std::vector<ModelMaterialTextures> textures{bindings};
    RenderModelInstance instance{0,{geometry,transform},&textures,1,true,false,&surfaces,material.shading,material.blend};
    struct PreviewPins {std::shared_ptr<const void> material;std::vector<std::shared_ptr<const ResidentModelLod>> geometry;};
    auto framePins=std::make_shared<PreviewPins>();framePins->material=std::move(pins);
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances={&instance,1};scene.pinned=framePins;scene.materialContext=context;
    impl_->renderTo(scene,*impl_->materialPreviewTargets,*impl_->materialPreviewEnvironment,
        static_cast<std::uint16_t>(impl_->materialPreviewWidth),static_cast<std::uint16_t>(impl_->materialPreviewHeight),{RenderViewPurpose::MaterialPreview},false,false,&framePins->geometry,nullptr,showEnvironment);
}
bgfx::TextureHandle Renderer::materialPreviewTexture() const noexcept { return impl_->materialPreviewDrawable && impl_->materialPreviewTargets ? impl_->materialPreviewTargets->display:bgfx::TextureHandle{bgfx::kInvalidHandle}; }
void Renderer::releaseMaterialPreview() { impl_->materialPreviewTargets.reset();impl_->materialPreviewDrawable=false;impl_->materialPreviewWidth=impl_->materialPreviewHeight=0; }

void Renderer::render(const SceneSettings& settings, const MaterialTextures& textures) {
    render(RenderScene{settings, textures});
}
void Renderer::render(const RenderScene& scene) {
    if (!drawable()) { impl_->particles={scene.particles.size(),0,0,scene.particles.size()}; return; }
    // A material hit describes the visible geometry in its captured frame.
    // Animation and transient preview bindings may advance during readback;
    // source identity/LOD changes still invalidate it. Authored transform edits
    // are guarded by the caller's scene revision instead of live pose values.
    std::uint64_t geometry=14695981039346656037ull;
    const auto bytes=[&](const void* data,std::size_t count) {
        const auto* value=static_cast<const unsigned char*>(data);
        for(std::size_t i=0;i<count;++i){geometry^=value[i];geometry*=1099511628211ull;}
    };
    const auto hash=[&](const auto& value){bytes(&value,sizeof(value));};
    for(const auto& instance:scene.instances)if(instance.pickable&&instance.entityId&&instance.model.asset) {
        hash(instance.entityId);hash(instance.resourceRevision);hash(instance.forceFullDetail);
        const auto& id=instance.model.asset->id;hash(id.size());bytes(id.data(),id.size());
    }
    impl_->geometrySceneSignature=geometry;
    impl_->renderTo(scene,*impl_->targets,*impl_->environment,static_cast<std::uint16_t>(impl_->width),static_cast<std::uint16_t>(impl_->height),{RenderViewPurpose::Scene},impl_->gridVisible,true,nullptr,&impl_->particles);
    impl_->displayedMaterialContext=scene.materialContext;
}
void Renderer::Impl::renderParticles(std::span<const RenderParticle> input,std::span<const RenderParticleMaterial> materials,
                                    const Camera& camera,const fog::Parameters& fogValues,bgfx::ViewId view,ParticleRenderStats* result,std::span<const TransparentMeshDraw> meshDraws,bool transparentPhase,const std::function<void()>& bindLighting,MaterialEvaluationContext context,bgfx::ViewId depthView) {
    ParticleRenderStats stats=transparentPhase && result?*result:ParticleRenderStats{};
    stats.requested=input.size();
    const bool instancing=(bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING)!=0;
    const auto inspected=instancing?std::min(input.size(),MaxParticleDrawCount):std::size_t{};
    if(!transparentPhase)stats.dropped=input.size()-inspected;
    particleScratch.clear();
    const RenderParticleMaterial defaultMaterial;
    std::array<bool,MaxParticleMaterialCount> usable{},pinned{};
    const auto requestedMaterials=materials.empty() ? std::size_t{1}:materials.size();
    const auto materialCount=std::min(requestedMaterials,MaxParticleMaterialCount);
    const auto materialAt=[&](std::size_t index)->const RenderParticleMaterial& {return materials.empty()?defaultMaterial:materials[index];};
    for(std::size_t i=0;i<materialCount;++i) {
        const auto& material=materialAt(i);
        usable[i]=math::finite(material.baseColor) && math::finite(material.uvScale) && math::finite(material.uvOffset) && math::finite(material.uvRotation) &&
            std::all_of(material.baseColor.begin(),material.baseColor.begin()+3,[](float value){return value>=0 && value<=1e6f;}) && material.baseColor[3]>=0 && material.baseColor[3]<=1 &&
            static_cast<unsigned>(material.blend)<=static_cast<unsigned>(MaterialBlend::Additive) &&
            math::finite(material.emission) && std::isfinite(material.roughness) && std::isfinite(material.metallic) &&
            std::isfinite(material.specular) && std::isfinite(material.occlusion) && std::isfinite(material.alphaCutoff);
    }
    for(std::size_t i=0;i<meshDraws.size();++i)particleScratch.push_back({i,meshDraws[i].depth,true});
    const auto eye=camera.eye(),forward=camera.forward();
    for (std::size_t i=0;i<inspected;++i) {
        const auto& particle=input[i];
        if (!particle_rendering::valid(particle) || particle.materialIndex>=requestedMaterials) {
            if(!transparentPhase)++stats.invalid; continue;
        }
        if(particle.materialIndex>=materialCount) {if(!transparentPhase)++stats.dropped;continue;}
        if(!usable[particle.materialIndex]) {if(!transparentPhase)++stats.invalid;continue;}
        const auto& material=materialAt(particle.materialIndex);
        const auto blend=material.graph?material.graph->blend:material.blend;
        const bool transparent=blend==MaterialBlend::Alpha || blend==MaterialBlend::Additive;
        if(transparent!=transparentPhase)continue;
        const auto depth=particle_rendering::viewDepth(particle.position,eye,forward);
        if(!std::isfinite(depth)) {++stats.invalid;continue;}
        particleScratch.push_back({i,depth,false});
    }
    if(transparentPhase)particle_rendering::sortTransparentBackToFront(particleScratch);
    struct GpuInstance {Vec3 position;float size;Vec4 color;Vec4 rotation;Vec4 right;Vec4 up;};
    static_assert(sizeof(GpuInstance)==80 && offsetof(GpuInstance,color)==16 && offsetof(GpuInstance,rotation)==32 &&
        offsetof(GpuInstance,right)==48 && offsetof(GpuInstance,up)==64);
    constexpr std::uint32_t BatchSize=4096;
    constexpr auto Stride=static_cast<std::uint16_t>(sizeof(GpuInstance));
    particleBatches.clear();
    std::size_t cursor{};
    while (cursor<particleScratch.size()) {
        if(particleScratch[cursor].mesh){particleBatches.push_back({{},particleScratch[cursor++].index,true});continue;}
        if(stats.drawCalls>=MaxParticleDrawCalls){++stats.dropped;++cursor;continue;}
        const auto materialIndex=input[particleScratch[cursor].index].materialIndex;
        const auto limit=std::min(particleScratch.size(),cursor+BatchSize);
        auto runEnd=cursor+1;
        while(runEnd<limit && !particleScratch[runEnd].mesh && input[particleScratch[runEnd].index].materialIndex==materialIndex)++runEnd;
        const auto count=static_cast<std::uint32_t>(runEnd-cursor);
        const auto available=bgfx::getAvailInstanceDataBuffer(count,Stride);
        if (!available) {stats.dropped+=count;cursor=runEnd;continue;}
        bgfx::InstanceDataBuffer instances;
        bgfx::allocInstanceDataBuffer(&instances,available,Stride);
        for(std::uint32_t i=0;i<available;++i) {
            const auto& particle=input[particleScratch[cursor+i].index];
            const GpuInstance value{particle.position,particle.size,particle.color,{particle.rotation,static_cast<float>(particle.billboardMode),0,0},
                {particle.right[0],particle.right[1],particle.right[2],0},{particle.up[0],particle.up[1],particle.up[2],0}};
            std::memcpy(instances.data+i*Stride,&value,sizeof(value));
        }
        particleBatches.push_back({instances,materialIndex,false});
        stats.submitted+=available;cursor+=available;++stats.drawCalls;
    }
    for(const auto& batch:particleBatches) {
        if(batch.mesh){meshDraws[batch.index].submit();continue;}
        const auto materialIndex=batch.index;
        const auto& material=materialAt(materialIndex);
        if(!pinned[materialIndex] && material.pinned) {
            submittedPins.push_back(material.pinned);pinned[materialIndex]=true;
        }
        const bool textured=bgfx::isValid(material.colorTexture);
        const auto shading=material.graph?material.graph->shading:material.shading;
        const auto mode=material.graph?material.graph->blend:material.blend;
        const float options[]{textured?1.f:0.f,0,0,0};
        const std::array<Vec4,5> parameters{{material.baseColor,{material.roughness,material.metallic,material.occlusion,1},
            {material.emission[0],material.emission[1],material.emission[2],material.alphaCutoff},
            {material.doubleSided?1.f:0.f,0,shading==MaterialShading::Unlit?1.f:0.f,static_cast<float>(mode)}, {material.specular,0,0,0}}};
        const Mat4 identity;
        const Vec4 modelSettings{0,mode==MaterialBlend::Mask?1.f:0.f,material.doubleSided?1.f:0.f,1};
        const Vec4 emissiveSettings{0,shading==MaterialShading::Unlit?1.f:0.f,1,0},uvSettings{};
        if(material.graph)submittedPins.push_back(material.graph);
        const auto bindBatch=[&](bool prepass) {
            bgfx::setUniform(particleParameters,parameters.data(),static_cast<std::uint16_t>(parameters.size()));
            bgfx::setUniform(particleParams,options);bindLighting();
            // Sprites occlude the geometry query but never supply or
            // receive indirect lighting in the model-only GI profile.
            const Vec4 giDraw{!transparentPhase?1.f:0.f,1.f,0,0};bgfx::setUniform(ssgiDraw,giDraw.data());
            const Vec4 scopes{float(lighting_scope::World),float(lighting_scope::World),prepass?1.f:0.f,1.f};
            bgfx::setUniform(surfaceLighting,scopes.data());
            const Vec4 category{float(lighting_scope::World),0,0,0};bgfx::setUniform(surfaceCategory,category.data());
            bindFog(fogValues,material.graph?material.graph->receiveFog:material.receiveFog);
            bgfx::setUniform(modelNormalMatrix,identity.data());bgfx::setUniform(modelOptions,modelSettings.data());
            bgfx::setUniform(modelUv,uvSettings.data());bgfx::setUniform(modelEmissiveMap,emissiveSettings.data());
            bindMaterialMapping(material.uvScale,material.uvOffset,material.uvRotation,material.triplanar,material.triplanarWorld);
            bgfx::setTexture(0,particleSampler,textured?material.colorTexture:white,material.samplerFlags);
            if(material.graph)material.graph->bind(context);
            particleQuad.bind();bgfx::setInstanceDataBuffer(&batch.instances);
        };
        const auto blend=mode==MaterialBlend::Alpha ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA):
            mode==MaterialBlend::Additive ? BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ZERO,BGFX_STATE_BLEND_ONE):0;
        const bool doubleSided=material.graph?material.graph->uniforms[3][0]>.5f:material.doubleSided;
        if(depthView!=InvalidRenderView && !transparentPhase) {
            // Both passes reference this one allocation. A transient-buffer
            // shortage drops the same batch from depth and visible color.
            bindBatch(true);
            bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS|(doubleSided?0:BGFX_STATE_CULL_CW));
            bgfx::submit(depthView,material.graph?material.graph->program(MaterialGraphPass::Particle):particleProgram);
        }
        bindBatch(false);
        bgfx::setState((BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A) | (transparentPhase?0:BGFX_STATE_WRITE_Z) |
            BGFX_STATE_DEPTH_TEST_LESS | (doubleSided?0:BGFX_STATE_CULL_CW) | blend);
        bgfx::submit(view,material.graph?material.graph->program(MaterialGraphPass::Particle):particleProgram);
    }
    if (result) *result=stats;
}
std::shared_ptr<const ModelPose> Renderer::Impl::poseFor(const RenderModelInstance& instance) {
    if(instance.pose)return instance.pose;
    const auto& asset=instance.model.asset;
    if(!asset)return {};
    const bool dynamic=!asset->skins.empty() || !asset->animations.empty() || std::any_of(asset->primitives.begin(),asset->primitives.end(),[](const auto& primitive){
        return primitive.hasSkinning || !primitive.skinWeights.empty() || !primitive.morphTargets.empty();
    });
    if(!dynamic)return {};
    if(const auto found=bindPoses.find(asset.get());found!=bindPoses.end() && !found->second.asset.expired())return found->second.pose;
    auto pose=std::make_shared<ModelPose>(evaluateModelPose(*asset));bindPoses[asset.get()]={asset,pose};return pose;
}
void Renderer::Impl::renderShadows(const RenderScene& scene,PointShadowRenderer& owner,bgfx::ViewId firstView,bgfx::ViewId firstDirectionalView,unsigned cameraContext,unsigned cameraCount,float aspect) {
    struct CasterDraw {
        ModelMatrix world;
        ModelMatrix normal;
        Aabb bounds;
        const ModelMaterial* material{};
        const ModelMaterialTextures* textures{};
        std::shared_ptr<const ResidentModelLod> resident;
        const GpuMesh* legacy{};
        std::shared_ptr<const DeformedModelVertices> deformed;
        float handedness{1};
    };
    static_assert(MaxPointShadowLights==PointShadowLightBudget);
    constexpr auto MaximumShadowTargets=MaxPointShadowLights+MaxDirectionalLights*4;
    std::array<std::vector<CasterDraw>,MaximumShadowTargets> casters;
    std::array<std::uint64_t,MaximumShadowTargets> signatures;
    signatures.fill(14695981039346656037ull);
    const auto hashBytes=[](std::uint64_t& hash,const void* data,std::size_t size) {
        const auto* bytes=static_cast<const std::uint8_t*>(data);
        for(std::size_t index=0;index<size;++index)hash=(hash^bytes[index])*1099511628211ull;
    };
    const auto hashValue=[&](std::uint64_t& hash,const auto& value){hashBytes(hash,&value,sizeof(value));};
    const auto shadowSelection=selectPointShadowLights(scene.pointLights,scene.shadowPriorityOrigin);
    std::array<PointLight,PointShadowLightBudget> selectedPoints;
    for(std::uint32_t slot=0;slot<shadowSelection.count;++slot) {
        const auto source=shadowSelection.sourceIndices[slot];selectedPoints[slot]=scene.pointLights[source];
        if(!selectedPoints[slot].entityId)selectedPoints[slot].entityId=std::uint64_t(source)+1;
    }
    const auto pointLights=std::span(selectedPoints).first(shadowSelection.count);
    const auto count=pointLights.size();
    const auto directionalCount=std::min(scene.directionalLights.size(),MaxDirectionalLights);
    struct CasterInstance {const RenderModelInstance* instance;Aabb bounds;};
    std::vector<CasterInstance> instances;
    std::vector<Aabb> allBounds;
    const bool directionalShadows=std::any_of(scene.directionalLights.begin(),scene.directionalLights.begin()+directionalCount,
        [](const auto& light){return light.enabled&&light.castShadows;});
    if(scene.shadowWorldId && (count || directionalShadows)) {
        instances.reserve(scene.shadowInstances.size());
        if(directionalShadows)allBounds.reserve(scene.shadowInstances.size());
        for(const auto& instance:scene.shadowInstances)if(instance.castShadows&&instance.model.asset) {
            const auto pose=poseFor(instance);
            const auto bounds=math::transformBounds(pose?pose->bounds:instance.model.asset->bounds,instance.model.transform);
            instances.push_back({&instance,bounds});
            if(directionalShadows)allBounds.push_back(bounds);
        }
    }
    const ShadowCamera shadowCamera{scene.settings.camera,aspect};
    std::array<directional_shadow::Cascades,MaxDirectionalLights> cascadeFits;
    for(std::size_t index=0;index<directionalCount;++index)if(scene.directionalLights[index].enabled&&scene.directionalLights[index].castShadows) {
        cascadeFits[index]=directional_shadow::build(scene.directionalLights[index],shadowCamera,scene.shadowResolution,allBounds);
    }
    struct ShadowTarget {
        const PointLight* point{};
        const directional_shadow::Cascade* projection{};
        std::uint32_t casterMask{lighting_scope::All};
    };
    std::array<ShadowTarget,MaximumShadowTargets> shadowTargets{};
    const auto targetCount=count+directionalCount*4;
    static_assert(MaximumShadowTargets<=64);
    for(std::size_t index=0;index<targetCount;++index) {
        hashValue(signatures[index],programRevision);
        if(!scene.shadowWorldId)continue;
        auto& target=shadowTargets[index];
        if(index<count) {target.point=&pointLights[index];target.casterMask=pointLights[index].roles.shadowCasterMask;continue;}
        const auto directionIndex=(index-count)/4,projectionIndex=(index-count)%4;
        const auto& light=scene.directionalLights[directionIndex];const auto& fits=cascadeFits[directionIndex];
        target.casterMask=light.roles.shadowCasterMask;
        if(!light.enabled || !light.castShadows || !fits.valid || projectionIndex>=fits.projectionCount)continue;
        target.projection=&fits.values[projectionIndex];
    }
    const auto visible=[](const ShadowTarget& target,const Aabb& bounds) {
        return target.point?point_shadow::intersectsLight(bounds,target.point->position,target.point->radius):
            target.projection && directional_shadow::intersects(bounds,*target.projection);
    };
    const auto includes=[](std::uint64_t mask,std::size_t index) {return (mask&(std::uint64_t{1}<<index))!=0;};
    // Prepare geometry/material metadata once per relevant instance/primitive.
    // Projection membership, LOD and signatures remain independent so each
    // cached cascade is invalidated only by its own relevant casters.
    for(const auto& entry:instances) {
        const auto& instance=*entry.instance;
        if(instance.blend==MaterialBlend::Alpha || instance.blend==MaterialBlend::Additive)continue;
        std::uint64_t instanceTargets{};
        for(std::size_t index=0;index<targetCount;++index) {
            const auto& target=shadowTargets[index];
            if(!(target.casterMask & instance.lightingScopes.shadowCasterMask))continue;
            if(visible(target,entry.bounds))instanceTargets|=std::uint64_t{1}<<index;
        }
        if(!instanceTargets)continue;
        const auto& asset=*instance.model.asset;
        const auto found=models.find(asset.id);
        const auto* stream=streamModels->descriptor(asset.id,instance.resourceRevision);
        const auto pose=poseFor(instance);
        auto instanceSignature=UINT64_C(14695981039346656037);
        hashValue(instanceSignature,instance.entityId);hashBytes(instanceSignature,asset.id.data(),asset.id.size());
        hashValue(instanceSignature,instance.resourceRevision);hashBytes(instanceSignature,instance.model.transform.data(),sizeof(ModelMatrix));
        for(std::size_t index=0;index<targetCount;++index)if(includes(instanceTargets,index))hashValue(signatures[index],instanceSignature);
        // Shadow-only demand protects geometry without changing pick signatures.
        if(stream)streamModels->markRendered(asset.id,instance.resourceRevision,false);
        std::size_t drawIndex=0;
        for(const auto& draw:asset.draws) {
            const auto currentDraw=drawIndex++;
            if(draw.primitive>=asset.primitives.size())continue;
            CasterDraw common;
            const auto local=pose && currentDraw<pose->drawTransforms.size()?pose->drawTransforms[currentDraw]:draw.transform;
            if(prepareModelMatrices(instance.model.transform,local,common.world,common.normal,common.handedness)!=ModelTransformStatus::Ready)continue;
            Aabb localBounds;
            const auto* primitive=stream && draw.primitive<stream->primitives.size()?&stream->primitives[draw.primitive]:nullptr;
            if(primitive)localBounds=primitive->bounds;
            else for(const auto& vertex:asset.primitives[draw.primitive].vertices)math::include(localBounds,vertex.position);
            common.bounds=pose?entry.bounds:math::transformBounds(localBounds,common.world);
            std::uint64_t primitiveTargets{};
            for(std::size_t index=0;index<targetCount;++index)
                if(includes(instanceTargets,index) && visible(shadowTargets[index],common.bounds))primitiveTargets|=std::uint64_t{1}<<index;
            if(!primitiveTargets)continue;
            const auto materialIndex=asset.primitives[draw.primitive].material;
            if(instance.materials && materialIndex<instance.materials->size())common.material=&(*instance.materials)[materialIndex];
            else if(materialIndex<asset.materials.size())common.material=&asset.materials[materialIndex];
            else continue;
            if(instance.textures && materialIndex<instance.textures->size())common.textures=&(*instance.textures)[materialIndex];
            if(found!=models.end() && draw.primitive<found->second->primitives.size())common.legacy=&found->second->primitives[draw.primitive];
            if(common.material->alphaBlend)continue;
            const auto graph=common.textures?common.textures->graph:nullptr;
            if(graph && (graph->blend==MaterialBlend::Alpha || graph->blend==MaterialBlend::Additive))continue;
            auto primitiveSignature=UINT64_C(14695981039346656037);
            if(graph)hashValue(primitiveSignature,graph->coverageSignature(scene.materialContext));
            hashValue(primitiveSignature,draw.primitive);hashBytes(primitiveSignature,common.world.data(),sizeof(ModelMatrix));
            if(pose) {
                hashValue(primitiveSignature,pose->signature);
                if(!pose->signature && draw.node>=0) {
                    if(std::size_t(draw.node)<pose->skinPalettes.size())for(const auto& matrix:pose->skinPalettes[draw.node])hashBytes(primitiveSignature,matrix.data(),sizeof(Mat4));
                    if(std::size_t(draw.node)<pose->nodeWeights.size())for(const auto weight:pose->nodeWeights[draw.node])hashValue(primitiveSignature,weight);
                }
            }
            const auto& material=*common.material;
            hashValue(primitiveSignature,material.alphaMask);hashValue(primitiveSignature,material.doubleSided);
            if(material.alphaMask) {
                hashValue(primitiveSignature,material.alphaCutoff);hashValue(primitiveSignature,material.baseColor[3]);hashValue(primitiveSignature,material.baseColorTexture.texCoord);
                hashValue(primitiveSignature,material.uvScale);hashValue(primitiveSignature,material.uvOffset);hashValue(primitiveSignature,material.uvRotation);
                hashValue(primitiveSignature,material.triplanar);hashValue(primitiveSignature,material.triplanarWorld);
                if(common.textures) {
                    hashValue(primitiveSignature,common.textures->handles[0].idx);hashValue(primitiveSignature,common.textures->flags[0]);
                    hashValue(primitiveSignature,common.textures->signatures[0]);hashValue(primitiveSignature,common.textures->sourceFingerprints[0]);
                }
            }
            bool graphPinned=false;
            std::array<const ResidentModelLod*,MaximumShadowTargets> pinnedGeometry{};
            std::size_t pinnedCount{};
            for(std::size_t index=0;index<targetCount;++index)if(includes(primitiveTargets,index)) {
                const auto& target=shadowTargets[index];CasterDraw candidate=common;
                if(primitive) {
                    const auto desired=target.projection?directional_shadow::chooseLod(primitive->levels,common.world,target.projection->texelWorld):
                        streaming::chooseModelLod(primitive->levels,common.world,primitive->bounds,target.point->position,
                            std::numbers::pi_v<float>*.5f,scene.shadowResolution);
                    candidate.resident=streamModels->resident(asset.id,instance.resourceRevision,draw.primitive,desired);
                }
                if(!candidate.resident && !candidate.legacy)continue;
                const auto source=candidate.resident?candidate.resident->deformation:candidate.legacy->deformation;
                if(source && pose) {
                    std::shared_ptr<const void> lease=candidate.resident?std::static_pointer_cast<const void>(candidate.resident):std::static_pointer_cast<const void>(found->second);
                    candidate.deformed=deformation->prepare(source,candidate.resident?candidate.resident->vertices:bgfx::DynamicVertexBufferHandle{BGFX_INVALID_HANDLE},
                        candidate.resident?bgfx::VertexBufferHandle{BGFX_INVALID_HANDLE}:candidate.legacy->vertices,std::move(lease),pose,draw.node,EnvironmentComputeView);
                    if(candidate.deformed)submittedPins.push_back(candidate.deformed);
                }
                auto& signature=signatures[index];hashValue(signature,primitiveSignature);
                if(candidate.resident) {
                    hashValue(signature,candidate.resident->level);hashValue(signature,candidate.resident->vertices.idx);hashValue(signature,candidate.resident->indices.idx);
                    if(std::find(pinnedGeometry.begin(),pinnedGeometry.begin()+pinnedCount,candidate.resident.get())==pinnedGeometry.begin()+pinnedCount) {
                        pinnedGeometry[pinnedCount++]=candidate.resident.get();submittedPins.push_back(candidate.resident);
                    }
                } else {hashValue(signature,candidate.legacy->vertices.idx);hashValue(signature,candidate.legacy->indices.idx);}
                if(graph && !graphPinned){submittedPins.push_back(graph);graphPinned=true;}
                casters[index].push_back(std::move(candidate));
            }
        }
        for(std::size_t index=0;index<targetCount;++index)if(includes(instanceTargets,index))hashValue(signatures[index],casters[index].size());
    }
    // Fully translucent or not-yet-resident primitives cannot contribute a
    // depth caster. Do not retain a dedicated tile for an empty draw list.
    const auto drawCaster=[&](const CasterDraw& draw,bgfx::ViewId view,MaterialGraphPass pass,const directional_shadow::Cascade* cascade=nullptr,unsigned cascadeIndex=0,const point_shadow::TileRect* rectangle=nullptr) {
                const auto& material=*draw.material;
                const bool textured=material.alphaMask && draw.textures && bgfx::isValid(draw.textures->handles[0]);
                const Vec4 emissive{0,0,0,material.alphaCutoff};
                const Vec4 uv{static_cast<float>(material.baseColorTexture.texCoord),0,0,0};
                const Vec4 options{0,material.alphaMask?1.f:0.f,material.doubleSided?1.f:0.f,draw.handedness};
                const Vec4 maps{textured?1.f:0.f,0,0,0};
                const Vec4 shadowScopes{};bgfx::setUniform(surfaceLighting,shadowScopes.data());
                const Vec4 category{float(lighting_scope::World),0,0,0};bgfx::setUniform(surfaceCategory,category.data());
                bgfx::setTransform(draw.world.data());
                bgfx::setUniform(modelNormalMatrix,draw.normal.data());
                const Vec4 fullOpacity{1,0,0,0};bgfx::setUniform(instanceOpacity,fullOpacity.data());
                bgfx::setUniform(modelBase,material.baseColor.data());bgfx::setUniform(modelEmissive,emissive.data());
                bgfx::setUniform(modelUv,uv.data());bgfx::setUniform(modelOptions,options.data());bgfx::setUniform(modelMaps,maps.data());
                bindMaterialMapping(material.uvScale,material.uvOffset,material.uvRotation,material.triplanar,material.triplanarWorld);
                bgfx::setTexture(0,albedoSampler,textured?draw.textures->handles[0]:white,textured?draw.textures->flags[0]:UINT32_MAX);
                const auto graph=draw.textures?draw.textures->graph:nullptr;
                if(graph)graph->bind(scene.materialContext);
                if(cascade) {
                    const auto projection=cascade->projection*cascade->view;
                    const auto tile=directional_shadow::casterTile(cascadeIndex);
                    bgfx::setUniform(directionalCasterViewProjection,projection.data());bgfx::setUniform(directionalCasterTile,tile.data());
                    bgfx::setScissor(rectangle->x,rectangle->y,rectangle->width,rectangle->height);
                }
                if(draw.resident)draw.resident->bind();else draw.legacy->bind();
                if(draw.deformed)draw.deformed->bind();
                const std::uint64_t cull=material.doubleSided?BGFX_STATE_NONE:draw.handedness<0?BGFX_STATE_CULL_CCW:BGFX_STATE_CULL_CW;
                bgfx::setState(BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS|cull);
                bgfx::submit(view,graph?graph->program(pass):cascade?directionalShadowProgram:shadowProgram);
    };
    const auto focus=scene.shadowPriorityOrigin.value_or(scene.settings.camera.eye());
    owner.render(pointLights,scene.directionalLights,std::span(signatures).first(count),scene.shadowWorldId,scene.shadowResolution,renderFrame,firstView,cameraCount,focus,
        [&](std::size_t lightIndex,const Camera& camera,bgfx::ViewId view) {
            std::uint32_t draws=0;
            for(const auto& draw:casters[lightIndex]) {
                if(!point_shadow::intersectsFace(draw.bounds,camera))continue;
                drawCaster(draw,view,MaterialGraphPass::PointShadow);++draws;
            }
            return draws;
        });
    owner.renderDirectional(scene.directionalLights,cascadeFits,std::span(signatures).subspan(count,directionalCount*4),shadowCamera,cameraContext,firstDirectionalView,
        [&](std::size_t lightIndex,unsigned cascadeIndex,const directional_shadow::Cascade& cascade,const point_shadow::TileRect& rectangle,bgfx::ViewId view) {
            // A depth-only fullscreen triangle clears this tile at depth one.
            // Sibling projections keep their cached depth in the same view.
            const Mat4 identity;
            const auto clearProjection=math::translation({0,0,1});
            const auto tile=directional_shadow::casterTile(cascadeIndex);
            const Vec4 one{1,1,1,1},zero{};
            bgfx::setUniform(surfaceLighting,zero.data());
            const Vec4 category{float(lighting_scope::World),0,0,0};bgfx::setUniform(surfaceCategory,category.data());
            bgfx::setTransform(identity.data());bgfx::setUniform(modelNormalMatrix,identity.data());
            bgfx::setUniform(directionalCasterViewProjection,clearProjection.data());bgfx::setUniform(directionalCasterTile,tile.data());
            bgfx::setUniform(modelBase,one.data());bgfx::setUniform(modelEmissive,zero.data());bgfx::setUniform(modelUv,zero.data());
            bgfx::setUniform(modelOptions,zero.data());bgfx::setUniform(modelMaps,zero.data());
            bindMaterialMapping({1,1,1},{},{},false);
            bgfx::setTexture(0,albedoSampler,white);bgfx::setScissor(rectangle.x,rectangle.y,rectangle.width,rectangle.height);
            fullscreen.bind();bgfx::setState(BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_ALWAYS);bgfx::submit(view,directionalShadowProgram);
            std::uint32_t draws=1;
            for(const auto& draw:casters[count+lightIndex*4+cascadeIndex])if(directional_shadow::intersects(draw.bounds,cascade)) {
                drawCaster(draw,view,MaterialGraphPass::DirectionalShadow,&cascade,cascadeIndex,&rectangle);++draws;
            }
            return draws;
        });
    owner.setCasterCount(scene.shadowInstances.size());
    shadowStatus=shadows->status();
    if(!thumbnailShadows->status().empty()){if(!shadowStatus.empty())shadowStatus+='\n';shadowStatus+="Thumbnail shadows: "+thumbnailShadows->status();}
    if(shadowSelection.unadmittedCount) {
        if(!shadowStatus.empty())shadowStatus+='\n';
        shadowStatus+="Point shadow budget: "+std::to_string(shadowSelection.unadmittedCount)+
            " light(s) illuminate normally without a resident shadow map (16 shadow lights supported).";
    }
}
bool Renderer::Impl::prepareGlow(const SceneSettings& settings,ViewportTargets& target,std::uint16_t w,std::uint16_t h,RenderViewPurpose purpose) {
    const auto& glow=settings.environment.glow;
    if(renderViewPassSlots(purpose).glowExtract==UINT16_MAX || !glow.enabled || !validGlowSettings(glow) || glow.strength==0 || glow.radius==0 ||
        !bgfx::isValid(glowExtractProgram) || !bgfx::isValid(glowBlurProgram))return false;
    if(!target.glow && target.glowFailure.empty()) {
        try {auto scratch=std::make_unique<GlowTargets>();scratch->create(w,h);target.glow=std::move(scratch);}
        catch(const std::exception& error) {target.glowFailure=error.what();}
    }
    if(!target.glow)return false;
    return true;
}

std::uint16_t Renderer::Impl::renderTo(const RenderScene& scene,ViewportTargets& target,EnvironmentLighting& lighting,
    std::uint16_t w,std::uint16_t h,RenderViewIdentity identity,bool drawGrid,bool allowPicking,std::vector<std::shared_ptr<const ResidentModelLod>>* pinnedGeometry,ParticleRenderStats* particleStats,bool drawEnvironment) {
    const bool sceneView=identity.purpose==RenderViewPurpose::Scene || identity.purpose==RenderViewPurpose::Game;
    const bool gameView=identity.purpose==RenderViewPurpose::Game;
    const bool thumbnailView=identity.purpose==RenderViewPurpose::Thumbnail;
    const auto debugMode=sceneView?debugViews[gameView?1:0]:RenderDebugView::Lit;
    const Vec4 debugValues{static_cast<float>(debugMode),0,0,0};
    bgfx::setUniform(debugViewUniform,debugValues.data());
    const Vec4 disabledEffects{};
    bgfx::setUniform(ssgiAtlas,disabledEffects.data());bgfx::setUniform(ssgiSettings,disabledEffects.data());bgfx::setUniform(ssgiDraw,disabledEffects.data());
    bgfx::setUniform(ssaoAtlas,disabledEffects.data());bgfx::setUniform(ssaoSettings,disabledEffects.data());
    auto& viewLods=lodSelections[gameView ? 1 : 0];
    auto& giDraws=ssgiDrawHistory[gameView?1:0];
    if (scene.pinned) submittedPins.push_back(scene.pinned);
    const auto& settings = scene.settings;
    const bool savedScene=thumbnailView && scene.sceneMode;
    const bool authoredEnvironment=sceneView || savedScene;
    const auto presentation=environmentPresentation(settings.environment,appearance,authoredEnvironment,drawEnvironment);
    drawEnvironment=presentation.showBackground&&debugMode==RenderDebugView::Lit;
    const bool glowing=debugMode==RenderDebugView::Lit&&prepareGlow(settings,target,w,h,identity.purpose);
    const auto& screenSpaceSettings=settings.environment.ssgi;
    bool ssgiEnabled=sceneView&&scene.sceneMode&&effectiveSsgi(screenSpaceSettings);
    bool ssaoEnabled=sceneView&&scene.sceneMode&&settings.environment.ssao.enabled&&settings.environment.ssao.strength>0;
    target.ssaoFailure.clear();
    if(!ssaoEnabled&&target.ssao)target.ssao->disable();
    if(ssaoEnabled) {
        try {target.ensureReadableDepth(w,h);}
        catch(const std::exception& error) {
            target.ssaoFailure=error.what();ssaoEnabled=false;if(target.ssao)target.ssao->disable();
        }
    }
    if(target.ssgi)target.advanceSsgi(renderFrame);
    if(ssgiEnabled&&target.ssgiFailure.empty()) {
        try {
            if(!target.ssgi)target.ssgi=std::make_unique<SsgiGpu>(ssgiPrograms[0],ssgiPrograms[1],ssgiPrograms[2],ssgiPrograms[3]);
            target.ssgi->setPrograms(ssgiPrograms[0],ssgiPrograms[1],ssgiPrograms[2],ssgiPrograms[3]);
            target.ssgi->advance(renderFrame);
            if(!target.ssgi->ensure(w,h))throw std::runtime_error(target.ssgi->status());
            target.ensureReadableDepth(w,h);
            if(!bgfx::isValid(target.ssgiFramebuffer)) {
                const auto& capture=target.ssgi->captureTextures();
                const bgfx::TextureHandle attachments[]{target.hdr,capture[0],capture[1],capture[2],target.depth};
                target.ssgiFramebuffer=bgfx::createFrameBuffer(5,attachments,false);
                if(!bgfx::isValid(target.ssgiFramebuffer))throw std::runtime_error("GI source framebuffer allocation failed.");
            }
        } catch(const std::exception& error) {target.ssgiFailure=error.what();ssgiEnabled=false;}
    } else if(ssgiEnabled)ssgiEnabled=false;
    if(!ssgiEnabled) {
        target.retireSsgiFramebuffer(renderFrame);if(target.ssgi)target.ssgi->disable();
        giDraws.clear();
        if(!effectiveSsgi(screenSpaceSettings))target.ssgiFailure.clear();
    }
    bool readableDepthEnabled=ssgiEnabled||ssaoEnabled;
    const bool scheduledPick=allowPicking && scene.sceneMode && requestedPick && !pendingPick && pickTargets;
    auto plan=buildRenderPassPlan({identity,glowing,drawGrid,scheduledPick,thumbnailView,sceneView || thumbnailView,presentation.procedural,sceneView&&!scene.collisionOverlays.empty(),ssgiEnabled,ssaoEnabled});
    if(const auto validation=validateRenderPassPlan(plan);!validation)
        throw std::logic_error(renderPassPlanErrorText(validation.error));
    const auto passView=[&](RenderPassKind kind) {
        const auto* pass=plan.find(kind);
        if(!pass)throw std::logic_error("Renderer attempted an undeclared frame pass.");
        return pass->physicalView;
    };
    auto views=renderViewPassSlots(identity.purpose);
    views.scene=passView(RenderPassKind::SceneColor);
    PointShadowRenderer* shadowContext=nullptr;
    const unsigned shadowCameraContext=gameView?1u:0u;
    if(plan.has(RenderPassKind::PointShadowUpdates)) {
        shadowContext=sceneView?shadows.get():thumbnailShadows.get();
        renderShadows(scene,*shadowContext,passView(RenderPassKind::PointShadowUpdates),
            passView(RenderPassKind::DirectionalShadowUpdates),shadowCameraContext,sceneView?2u:1u,float(w)/h);
    }
    ProceduralSkyGpu* procedural=nullptr;
    if(sceneView)proceduralActive=settings.environment.source==EnvironmentSource::ProceduralSky;
    if(presentation.procedural) {
        auto& owner=sceneView?*proceduralEnvironment:*thumbnailProceduralEnvironment;
        // Both world views consume one frozen generation. Camera motion never
        // changes the direction-only sky key, and thumbnail ownership is separate.
        if(!sceneView || proceduralFrame!=renderFrame) {
            const bool prepared=owner.prepare(settings.environment.procedural,settings.proceduralSun,passView(RenderPassKind::EnvironmentPreparation));
            // Never cache a different saved scene's last-good lighting under
            // this thumbnail's signature when its own preparation has failed.
            if(!sceneView && !prepared)throw std::runtime_error(owner.status());
            if(sceneView)proceduralFrame=renderFrame;
        }
        if(bgfx::isValid(owner.specular()))procedural=&owner;
    }
    const auto diffuse=procedural?procedural->diffuse():lighting.diffuse();
    const auto specular=procedural?procedural->specular():lighting.specular();
    auto fogValues=fog::parameters(presentation.fog,settings.camera.farPlane);
    fogValues.sky[3]=bounded(settings.environment.intensity,1,0,10);
    const std::span<const DirectionalLight> fogSuns=scene.sceneMode?scene.directionalLights:std::span<const DirectionalLight>(&settings.directional,1);
    for(std::size_t i=0;i<std::min(fogSuns.size(),fogValues.sunDirections.size());++i) {
        const auto& sun=fogSuns[i];
        const auto toward=math::normalized(sun.direction,{-.5f,-.8f,-.6f})*-1.f;
        const float energy=sun.enabled?bounded(sun.intensity,0,0,10000):0.f;
        fogValues.sunDirections[i]={toward[0],toward[1],toward[2],energy>0?1.f:0.f};
        fogValues.sunColors[i]={toLinear(sun.color[0])*energy,toLinear(sun.color[1])*energy,toLinear(sun.color[2])*energy,0};
    }
    const auto& textures = scene.sphereTextures;
    const auto* caps = bgfx::getCaps();
    bgfx::setViewFrameBuffer(views.scene,ssgiEnabled?target.ssgiFramebuffer:target.hdrBuffer);
    bgfx::setViewMode(views.scene,bgfx::ViewMode::Sequential); // Opaque draws, then one sorted mesh/particle queue.
    bgfx::setViewRect(views.scene,0,0,w,h);
    bgfx::setViewClear(views.scene,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,0u,1.0f,0);
    bgfx::touch(views.scene); // Also clear when every model draw has a singular transform.
    ModelMatrix model;
    const auto eye = settings.camera.eye();
    const float aspect = static_cast<float>(w)/static_cast<float>(h);
    const auto view = settings.camera.viewMatrix();
    const auto projection = settings.camera.projectionMatrix(aspect,caps->homogeneousDepth ? ClipDepth::NegativeOneToOne : ClipDepth::ZeroToOne);
    bgfx::setViewTransform(views.scene,view.data(),projection.data());
    if(readableDepthEnabled) {
        target.ensureReadableDepth(w,h);
        const auto depthView=passView(RenderPassKind::DepthPrepass);
        bgfx::setViewName(depthView,gameView?"Game readable depth":"Scene readable depth");
        bgfx::setViewFrameBuffer(depthView,target.depthPrepass);
        bgfx::setViewMode(depthView,bgfx::ViewMode::Sequential);
        bgfx::setViewRect(depthView,0,0,w,h);
        bgfx::setViewClear(depthView,BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,0u,1.f,0);
        bgfx::setViewTransform(depthView,view.data(),projection.data());
        bgfx::touch(depthView);
    }
    // Empty/solid backgrounds also need a density-controlled fog layer.
    // Draw it before geometry in HDR; the editor grid and tools stay in their
    // later display-space passes, and disabled fog keeps the existing matte.
    const bool backgroundFog=presentation.fog.enabled && fogValues.sky[0]>0 && debugMode==RenderDebugView::Lit &&
        (fogValues.height[1]!=0 || (presentation.fog.mode==FogMode::Depth?fogValues.depth[3]>0:fogValues.colorDensity[3]>0));
    const bool visibleSource=drawEnvironment && bgfx::isValid(specular);
    if(backgroundFog || visibleSource) {
        const auto forward=math::normalized(settings.camera.target-eye,{0,0,-1});
        const auto right=math::normalized(math::cross(forward,settings.camera.up),{1,0,0});
        const auto up=math::normalized(math::cross(right,forward),{0,1,0});
        const auto tangent=std::tan(settings.camera.verticalFov(aspect)*std::numbers::pi_v<float>/360);
        const Vec4 rightValue{right[0]*tangent*aspect,right[1]*tangent*aspect,right[2]*tangent*aspect,eye[1]};
        const Vec4 upValue{up[0]*tangent,up[1]*tangent,up[2]*tangent,0};
        const Vec4 forwardValue{forward[0],forward[1],forward[2],bounded(settings.camera.farPlane,2048,.001f,1000000)};
        const float env[]{bounded(settings.environment.intensity,1,0,10),
            procedural?0.f:bounded(settings.environment.rotation,0,-180,180)*std::numbers::pi_v<float>/180,visibleSource?1.f:0.f,procedural?1.f:0.f};
        bgfx::setUniform(skyRight,rightValue.data());bgfx::setUniform(skyUp,upValue.data());bgfx::setUniform(skyForward,forwardValue.data());
        bgfx::setUniform(environmentParams,env);
        const auto matte=particle_rendering::backgroundLinear(presentation.matte);
        const Vec4 backgroundValue{matte[0],matte[1],matte[2],1};bgfx::setUniform(background,backgroundValue.data());
        if(bgfx::isValid(specular))bgfx::setTexture(0,skySampler,specular);
        bindFog(fogValues);
        if(procedural)procedural->bindBackground();
        fullscreen.bind();bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(views.scene,skyProgram);
    }
    const float camera[]{eye[0],eye[1],eye[2],0};
    const float environmentValues[]{settings.environment.enabled ? bounded(settings.environment.intensity,1,0,10) : 0,
        procedural?0.f:bounded(settings.environment.rotation,0,-180,180) * std::numbers::pi_v<float> / 180.0f,
        procedural?procedural->maxSpecularLod():lighting.maxSpecularLod(),0};
    const auto ambientLightValues=environmentAmbientLight(settings.environment,authoredEnvironment);
    const std::span<const DirectionalLight> directional = scene.sceneMode ? scene.directionalLights : std::span<const DirectionalLight>(&settings.directional,1);
    const std::span<const PointLight> points = scene.sceneMode ? scene.pointLights : std::span<const PointLight>(settings.points);
    const auto directionalCount = std::min(directional.size(),MaxDirectionalLights);
    if (directional.size() > MaxDirectionalLights)
        modelStatus = "Scene exceeds the renderer budget of 4 directional lights.";
    const auto shadowSelection=selectPointShadowLights(points,scene.shadowPriorityOrigin);
    std::vector<std::int32_t> shadowIndices(points.size(),-1);
    for(std::uint32_t slot=0;slot<shadowSelection.count;++slot)shadowIndices[shadowSelection.sourceIndices[slot]]=static_cast<std::int32_t>(slot);
    if(!target.clustered)target.clustered=std::make_unique<ClusteredLightingGpu>(clusterComputeProgram);
    target.clustered->setComputeProgram(clusterComputeProgram);
    target.clustered->advance(renderFrame);
    bool clusterReady=target.clustered->prepare(settings.camera,w,h,points,shadowIndices,lighting.brdfCpu(),
        passView(RenderPassKind::LightAssignment),scene.localLightMode==LocalLightMode::AllLightsReference,
        ssgiEnabled?w:0,ssgiEnabled?h:0,ssaoEnabled?w:0,ssaoEnabled?h:0);
    if(!clusterReady&&(ssgiEnabled||ssaoEnabled)) {
        if(ssgiEnabled)target.ssgiFailure=target.clustered->status();
        if(ssaoEnabled)target.ssaoFailure=target.clustered->status();
        ssgiEnabled=false;ssaoEnabled=false;readableDepthEnabled=false;
        target.retireSsgiFramebuffer(renderFrame);if(target.ssgi)target.ssgi->disable();if(target.ssao)target.ssao->disable();
        bgfx::setViewFrameBuffer(views.scene,target.hdrBuffer);
        // The already scheduled readable prepass remains harmless; the
        // validated fallback plan contains only producers actually consumed.
        auto fallback=plan.config;fallback.ssgi=false;fallback.ssao=false;plan=buildRenderPassPlan(fallback);
        clusterReady=target.clustered->prepare(settings.camera,w,h,points,shadowIndices,lighting.brdfCpu(),
            passView(RenderPassKind::LightAssignment),scene.localLightMode==LocalLightMode::AllLightsReference);
    }
    if(sceneView){clusterStats=target.clustered->stats();clusterStatus=target.clustered->status();}
    if(!clusterReady) {
        if(!sceneView)throw std::runtime_error(target.clustered->status());
        return InvalidRenderView;
    }
    std::array<float,MaxDirectionalLights*4> directions{},directionColors{},directionRoles{};
    for (std::size_t i=0;i<directionalCount;++i) {
        const auto& light = directional[i];
        const auto lightDirection = math::normalized(light.direction,{-.5f,-.8f,-.6f});
        for (std::size_t j=0;j<3;++j) { directions[i*4+j]=lightDirection[j]; directionColors[i*4+j]=toLinear(light.color[j]); }
        directions[i*4+3]=light.enabled ? bounded(light.intensity,0,0,10000) : 0;
        // The unused color W carries a delta from one, preserving default
        // uniform bytes. Only SSGI diffuse donor capture consumes this value.
        directionColors[i*4+3]=bounded(light.indirectEnergy,1,0,16)-1.f;
        directionRoles[i*4]=static_cast<float>(light.roles.receiverMask);
    }
    const float lightCountValues[]{static_cast<float>(directionalCount),static_cast<float>(points.size()),0,0};
    const auto bindDirections=[&] {
        bgfx::setUniform(direction,directions.data(),static_cast<std::uint16_t>(MaxDirectionalLights));
        bgfx::setUniform(directionColor,directionColors.data(),static_cast<std::uint16_t>(MaxDirectionalLights));
        bgfx::setUniform(directionLightingRoles,directionRoles.data(),static_cast<std::uint16_t>(MaxDirectionalLights));
        bgfx::setUniform(lightCounts,lightCountValues);
    };
    bool ssgiReady=false;
    if(ssgiEnabled) {
        const auto worldKey=(scene.giWorldId?scene.giWorldId:scene.shadowWorldId)^(programRevision*1099511628211ull);
        ssgiReady=target.ssgi->prepare(target.readableDepth,target.receiverScope,settings.camera,screenSpaceSettings,
            passView(RenderPassKind::SsgiTrace),worldKey,scene.giCameraId,scene.giEnvironmentId,
            target.clustered->texture(),target.clustered->atlasWidth(),target.clustered->ssgiBaseTexel());
        if(!ssgiReady)target.ssgiFailure=target.ssgi->status();
    }
    bool ssaoReady=false;
    if(ssaoEnabled) {
        try {
            if(!target.ssao)target.ssao=std::make_unique<SsaoGpu>(ssaoPrograms[0],ssaoPrograms[1]);
            target.ssao->setPrograms(ssaoPrograms[0],ssaoPrograms[1]);target.ssao->advance(renderFrame);
            ssaoReady=target.ssao->prepare(target.readableDepth,target.receiverScope,settings.camera,w,h,*target.clustered,
                settings.environment.ssao,passView(RenderPassKind::SsaoEvaluate));
        } catch(const std::exception& error) {target.ssaoFailure=error.what();}
        if(!ssaoReady) {
            if(target.ssaoFailure.empty()&&target.ssao)target.ssaoFailure=target.ssao->status();
            if(target.ssao)target.ssao->disable();ssaoEnabled=false;readableDepthEnabled=ssgiEnabled;
            auto fallback=plan.config;fallback.ssao=false;plan=buildRenderPassPlan(fallback);
        }
    }
    const Vec4 giAtlasValues{float(target.clustered->ssgiBaseTexel()),float(w),float(h),ssgiReady?1.f:0.f};
    const Vec4 giSettingsValues{ssgiReady?1.f:0.f,settings.environment.ssgi.strength,0,0};
    const Vec4 aoAtlasValues{float(target.clustered->ssaoBaseTexel()),float(w),float(h),ssaoReady?1.f:0.f};
    const Vec4 aoSettingsValues{ssaoReady?1.f:0.f,settings.environment.ssao.strength,0,0};
    const auto bindLighting = [&](std::uint8_t firstEnvironmentSlot) {
        bgfx::setUniform(debugViewUniform,debugValues.data());
        const std::uint8_t shadowSlot=firstEnvironmentSlot==12?15:8;
        if(shadowContext && scene.shadowWorldId)shadowContext->bind(true,shadowSlot,shadowCameraContext);else shadows->bindDisabled(shadowSlot);
        bgfx::setUniform(cameraPos,camera);bindDirections();
        bgfx::setUniform(environmentParams,environmentValues);
        bgfx::setUniform(ambientLight,ambientLightValues.data());
        bgfx::setTexture(firstEnvironmentSlot,irradianceSampler,diffuse);
        bgfx::setTexture(firstEnvironmentSlot+1,prefilteredSampler,specular);
        target.clustered->bind(firstEnvironmentSlot+2);
        bgfx::setUniform(ssgiAtlas,giAtlasValues.data());bgfx::setUniform(ssgiSettings,giSettingsValues.data());
        bgfx::setUniform(ssaoAtlas,aoAtlasValues.data());bgfx::setUniform(ssaoSettings,aoSettingsValues.data());
    };
    if (allowPicking && !scene.sceneMode && requestedPick) { ++pickEpoch; requestedPick.reset(); }
    const bool capturePick = plan.has(RenderPassKind::Picking);
    if (capturePick) {
        pendingPick = std::make_unique<PendingPick>();
        pendingPick->request = *requestedPick;
        pendingPick->materialContext=requestedPickContext;
        pendingPick->epoch = pickEpoch;
        pendingPick->targets = pickTargets;
        pendingPick->pinned=scene.pinned;
        requestedPick.reset();
        bgfx::setViewFrameBuffer(passView(RenderPassKind::Picking),pickTargets->framebuffer);
        bgfx::setViewRect(passView(RenderPassKind::Picking),0,0,w,h);
        bgfx::setViewTransform(passView(RenderPassKind::Picking),view.data(),projection.data());
        bgfx::setViewClear(passView(RenderPassKind::Picking),BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,0,1.0f,0);
        bgfx::touch(passView(RenderPassKind::Picking));
    }
    std::array<RenderModelInstance,1> legacy;
    auto instances = scene.instances;
    if (!scene.sceneMode && scene.model && scene.model->asset) {
        legacy[0] = {0,*scene.model,scene.modelTextures};
        instances = legacy;
    }
    std::vector<TransparentMeshDraw> transparentMeshes;
    std::size_t skipped = 0, collapsed = 0, missing = 0;
    for (const auto& instance : instances) {
        const float opacity=bounded(instance.opacity,1,0,1);
        if(opacity<=0)continue;
        if (!instance.model.asset) { ++missing; continue; }
        const auto& asset = *instance.model.asset;
        const auto pose=poseFor(instance);
        const auto found = models.find(asset.id);
        const auto* stream=streamModels->descriptor(asset.id,instance.resourceRevision);
        if (found == models.end() && !(stream && streamModels->ready(asset.id,instance.resourceRevision))) {
            ++missing;
        } else {
            if (sceneView && stream) streamModels->markRendered(asset.id,instance.resourceRevision,views.scene==MainViews.scene);
            const std::array<bgfx::UniformHandle, 5> samplers{albedoSampler, metallicRoughnessSampler,
                normalSampler, occlusionSampler, emissiveSampler};
            std::uint32_t drawIndex=0;
            for (const auto& draw : asset.draws) {
                const auto currentDraw=drawIndex++;
                ModelMatrix world, normal;
                float handedness = 1;
                const auto local=pose && currentDraw<pose->drawTransforms.size()?pose->drawTransforms[currentDraw]:draw.transform;
                const auto transform=prepareModelMatrices(instance.model.transform,local,world,normal,handedness);
                if (transform==ModelTransformStatus::Collapsed) { ++collapsed; continue; }
                if (transform==ModelTransformStatus::Invalid) { ++skipped; continue; }
                std::shared_ptr<const ResidentModelLod> resident;
                if (stream && draw.primitive<stream->primitives.size()) {
                    const auto& primitive=stream->primitives[draw.primitive];
                    auto desired=instance.forceFullDetail ? 0u : streaming::chooseModelLod(primitive.levels,world,primitive.bounds,eye,
                        settings.camera.verticalFov(aspect)*std::numbers::pi_v<float>/180,h);
                    if (sceneView && instance.entityId && !instance.forceFullDetail) {
                        const auto key=(instance.entityId*1099511628211ull)^currentDraw;
                        const auto previous=viewLods.find(key);
                        if (previous!=viewLods.end() && previous->second.level!=desired)
                            desired=streaming::chooseModelLod(primitive.levels,world,primitive.bounds,eye,settings.camera.verticalFov(aspect)*std::numbers::pi_v<float>/180,h,
                                desired>previous->second.level ? .8f:1.2f);
                        viewLods[key]={desired,renderFrame};
                    }
                    resident=streamModels->resident(asset.id,instance.resourceRevision,draw.primitive,desired);
                }
                if (!resident && (found==models.end() || draw.primitive>=found->second->primitives.size())) { ++missing; continue; }
                if (resident) {
                    submittedPins.push_back(resident);
                    if (pinnedGeometry) pinnedGeometry->push_back(resident);
                }
                const auto* legacyMesh=resident?nullptr:&found->second->primitives[draw.primitive];
                std::shared_ptr<const DeformedModelVertices> deformed;
                const auto source=resident?resident->deformation:legacyMesh->deformation;
                if(source && pose) {
                    std::shared_ptr<const void> lease=resident?std::static_pointer_cast<const void>(resident):std::static_pointer_cast<const void>(found->second);
                    deformed=deformation->prepare(source,resident?resident->vertices:bgfx::DynamicVertexBufferHandle{BGFX_INVALID_HANDLE},
                        resident?bgfx::VertexBufferHandle{BGFX_INVALID_HANDLE}:legacyMesh->vertices,std::move(lease),pose,draw.node,EnvironmentComputeView);
                    if(deformed)submittedPins.push_back(deformed);
                }
                const auto tangentUv=asset.primitives[draw.primitive].tangentUv;
                const auto materialIndex = asset.primitives[draw.primitive].material;
                std::array<float,4> pickColor{};
                if(capturePick&&instance.pickable&&instance.entityId)
                    pickColor=picking::color(pendingPick->entities.add(instance.entityId,materialIndex));
                const auto& material=instance.materials && materialIndex<instance.materials->size() ? (*instance.materials)[materialIndex]:asset.materials[materialIndex];
                const ModelMaterialTextures emptyBindings;
                const auto& bindings = instance.textures && materialIndex < instance.textures->size()
                    ? (*instance.textures)[materialIndex] : emptyBindings;
                const auto graph=bindings.graph;
                const auto shading=graph?graph->shading:material.graph?materialOutputNode(*material.graph).output.shading:instance.shading;
                const auto authoredBlend=authoredModelBlend(instance,material,bindings);
                const auto blend=opacity<1 && (authoredBlend==MaterialBlend::Opaque || authoredBlend==MaterialBlend::Mask)?MaterialBlend::Alpha:authoredBlend;
                bool giUnstable=false;
                if(ssgiEnabled) {
                    std::uint64_t materialKey=graph?graph->signature:instance.resourceRevision;
                    const auto hashFloat=[&](float value){materialKey=(materialKey^std::bit_cast<std::uint32_t>(value))*1099511628211ull;};
                    for(float value:material.baseColor)hashFloat(value);
                    for(float value:material.emissive)hashFloat(value);
                    for(float value:{material.metallic,material.roughness,material.specular,material.occlusionStrength,material.normalScale})hashFloat(value);
                    for(auto texture:bindings.handles)materialKey=(materialKey^texture.idx)*1099511628211ull;
                    const auto key=(instance.entityId*1099511628211ull)^currentDraw;
                    const auto previous=giDraws.find(key);
                    giUnstable=bool(pose)||(graph&&!graph->runtimeParameters.empty())||previous==giDraws.end()||
                        previous->second.frame+1!=renderFrame||previous->second.transform!=world||previous->second.material!=materialKey;
                    giDraws[key]={world,materialKey,renderFrame};
                }
                if(graph)submittedPins.push_back(graph);
                auto submitDraw=[&,world,normal,handedness,resident,legacyMesh,deformed,tangentUv,material,bindings,graph,shading,blend,authoredBlend,opacity,samplers,instance,pickColor,giUnstable]() {
                    const auto bindGeometry=[&] {if(resident)resident->bind();else legacyMesh->bind();if(deformed)deformed->bind();};
                    float mapFlags[4]{};
                    for(std::size_t slot=0;slot<4;++slot)mapFlags[slot]=bgfx::isValid(bindings.handles[slot])?1.f:0.f;
                    const Vec4 specularValue{material.specular,0,0,0};
                    const float surface[]{material.metallic,material.roughness,material.normalScale,material.occlusionStrength};
                    const float emissive[]{material.emissive[0],material.emissive[1],material.emissive[2],material.alphaCutoff};
                    const float uv[]{static_cast<float>(graph?tangentUv:material.baseColorTexture.texCoord),static_cast<float>(material.metallicRoughnessTexture.texCoord),
                        static_cast<float>(material.normalTexture.texCoord),static_cast<float>(material.occlusionTexture.texCoord)};
                    const float options[]{static_cast<float>(material.emissiveTexture.texCoord),material.alphaMask ? 1.0f : 0.0f,
                        material.doubleSided ? 1.0f : 0.0f,handedness};
                    const float emissiveMap[]{bgfx::isValid(bindings.handles[4]) ? 1.0f : 0.0f,shading==MaterialShading::Unlit?1.f:0.f,instance.receiveShadows?1.f:0.f,
                        authoredBlend==MaterialBlend::Additive?2.f:authoredBlend==MaterialBlend::Alpha?1.f:0.f};
                    const std::uint64_t cull = material.doubleSided ? BGFX_STATE_NONE : handedness < 0 ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
                    const bool transparent=blend==MaterialBlend::Alpha || blend==MaterialBlend::Additive;
                    const auto bindSurface=[&](bool prepass) {
                        bgfx::setTransform(world.data());
                        bgfx::setUniform(modelNormalMatrix,normal.data());
                        const Vec4 instanceFade{opacity,0,0,0};bgfx::setUniform(instanceOpacity,instanceFade.data());
                        bgfx::setUniform(modelBase,material.baseColor.data());
                        bgfx::setUniform(modelSpecular,specularValue.data());bgfx::setUniform(modelSurface,surface);bgfx::setUniform(modelEmissive,emissive);
                        bgfx::setUniform(modelUv,uv);bgfx::setUniform(modelOptions,options);
                        bindMaterialMapping(material.uvScale,material.uvOffset,material.uvRotation,material.triplanar,material.triplanarWorld);
                        bgfx::setUniform(modelMaps,mapFlags);bgfx::setUniform(modelEmissiveMap,emissiveMap);
                        const Vec4 scopes{float(instance.lightingScopes.directReceiverMask),0.f,
                            prepass?1.f:0.f,transparent?1.f:0.f};
                        bgfx::setUniform(surfaceLighting,scopes.data());
                        const Vec4 category{float(instance.lightingCategory),0,0,0};bgfx::setUniform(surfaceCategory,category.data());
                        const Vec4 giDraw{ssgiEnabled&&!transparent&&opacity>=1.f?1.f:0.f,giUnstable?1.f:0.f,0,0};
                        bgfx::setUniform(ssgiDraw,giDraw.data());
                        if(!prepass) {bindLighting(graph?12:5);bindFog(fogValues,graph?graph->receiveFog:material.receiveFog);}
                        for(std::uint8_t slot=0;slot<samplers.size();++slot)
                            bgfx::setTexture(slot,samplers[slot],bgfx::isValid(bindings.handles[slot])?bindings.handles[slot]:white,bindings.flags[slot]);
                        if(graph)graph->bind(scene.materialContext);
                        bindGeometry();
                    };
                    if(plan.has(RenderPassKind::DepthPrepass) && !transparent) {
                        bindSurface(true);
                        bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS|cull);
                        bgfx::submit(passView(RenderPassKind::DepthPrepass),graph?graph->program(MaterialGraphPass::PointShadow):shadowProgram);
                    }
                    bindSurface(false);
                    const auto blending=blend==MaterialBlend::Alpha?BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA):
                        blend==MaterialBlend::Additive?BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ZERO,BGFX_STATE_BLEND_ONE):0;
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | (transparent?0:BGFX_STATE_WRITE_Z) | BGFX_STATE_DEPTH_TEST_LESS | cull | blending);
                    bgfx::submit(views.scene,graph?graph->program(MaterialGraphPass::Model):modelProgram);
                    if (capturePick && instance.pickable && instance.entityId) {
                        const bool geometryOnly=pendingPick->request.mode==PickMode::MaterialSlot;
                        if(graph&&!geometryOnly)pendingPick->graphMaterials.push_back(graph);
                        if (resident) pendingPick->resources.push_back(resident);
                        if(deformed)pendingPick->deformations.push_back(deformed);
                        // Slot targeting ignores material coverage and sidedness;
                        // selection retains the visible material's exact cutout.
                        const Vec4 neutral{},opaque{1,1,1,1};
                        bgfx::setTransform(world.data());
                        bgfx::setUniform(modelNormalMatrix,normal.data());
                        const Vec4 instanceFade{geometryOnly?1.f:opacity,0,0,0};bgfx::setUniform(instanceOpacity,instanceFade.data());
                        bgfx::setUniform(modelBase,geometryOnly?opaque.data():material.baseColor.data());
                        bgfx::setUniform(modelEmissive,geometryOnly?neutral.data():emissive);
                        bgfx::setUniform(modelUv,uv);
                        bindMaterialMapping(material.uvScale,material.uvOffset,material.uvRotation,material.triplanar,material.triplanarWorld);
                        bgfx::setUniform(modelOptions,geometryOnly?neutral.data():options);
                        bgfx::setUniform(modelMaps,geometryOnly?neutral.data():mapFlags);
                        bgfx::setUniform(modelEmissiveMap,geometryOnly?neutral.data():emissiveMap);
                        bgfx::setUniform(pickId,pickColor.data());
                        bgfx::setTexture(0,albedoSampler,!geometryOnly&&bgfx::isValid(bindings.handles[0]) ? bindings.handles[0] : white,
                            geometryOnly?UINT32_MAX:bindings.flags[0]);
                        if(graph&&!geometryOnly)graph->bind(pendingPick->materialContext);
                        bindGeometry();
                        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | (geometryOnly?BGFX_STATE_NONE:cull));
                        bgfx::submit(passView(RenderPassKind::Picking),graph&&!geometryOnly?graph->program(MaterialGraphPass::Picking):pickProgram);
                    }
                };
                if(blend==MaterialBlend::Alpha || blend==MaterialBlend::Additive) {
                    const auto localBounds=stream && draw.primitive<stream->primitives.size()?stream->primitives[draw.primitive].bounds:found->second->bounds[draw.primitive];
                    const auto bounds=pose?math::transformBounds(pose->bounds,instance.model.transform):math::transformBounds(localBounds,world);
                    const Vec3 center{(bounds.minimum[0]+bounds.maximum[0])*.5f,(bounds.minimum[1]+bounds.maximum[1])*.5f,(bounds.minimum[2]+bounds.maximum[2])*.5f};
                    const auto depth=particle_rendering::viewDepth(center,eye,settings.camera.forward());
                    if(std::isfinite(depth)) {
                        transparentMeshes.push_back({depth,std::move(submitDraw)});
                    }
                    else ++skipped;
                } else submitDraw();
            }
        }
    }
    if (sceneView) {
        if (missing) modelStatus = "Scene has " + std::to_string(missing) + " model instance(s) without prepared resources.";
        else if (skipped) modelStatus = "Skipped " + std::to_string(skipped) + " draw(s) with invalid or nonfinite transforms.";
        else if (collapsed) modelStatus = "Hidden " + std::to_string(collapsed) + " draw(s) with a collapsed transform (for example, zero scale).";
        else if (modelStatus.starts_with("Skipped ") || modelStatus.starts_with("Hidden ") || modelStatus.starts_with("Scene has ")) modelStatus.clear();
    }
    if (capturePick) {
        auto& pending = *pendingPick;
        const auto x = static_cast<std::uint16_t>(pending.request.x);
        const auto y = static_cast<std::uint16_t>(caps->originBottomLeft ? height-1-pending.request.y : pending.request.y);
        bgfx::blit(passView(RenderPassKind::PickReadback),pickTargets->readback,0,0,pickTargets->color,x,y,1,1);
        pending.readyFrame = bgfx::readTexture(pickTargets->readback,pending.pixel.data());
    }
    if (!scene.sceneMode && !(scene.model && scene.model->asset)) {
        model = math::translation({0,1,0}); // Radius-one sphere rests on the ground plane.
        bgfx::setTransform(model.data());
        const auto& m = settings.material;
        const float base[]{toLinear(m.baseColor[0]),toLinear(m.baseColor[1]),toLinear(m.baseColor[2]),bounded(m.metallic,0,0,1)};
        // The shared perceptual-roughness floor is applied after map multiplication.
        const float rough[]{bounded(m.roughness,.35f,0,1),bounded(m.normalStrength,1,0,2),.5f,0};
        const float mapFlags[]{bgfx::isValid(textures.albedo)?1.0f:0.0f,bgfx::isValid(textures.roughness)?1.0f:0.0f,bgfx::isValid(textures.metallic)?1.0f:0.0f,bgfx::isValid(textures.normal)?1.0f:0.0f};
        bgfx::setUniform(baseColorMetallic,base); bgfx::setUniform(roughness,rough); bgfx::setUniform(textureFlags,mapFlags);
        bgfx::setTexture(0,albedoSampler,bgfx::isValid(textures.albedo)?textures.albedo:white);
        bgfx::setTexture(1,roughnessSampler,bgfx::isValid(textures.roughness)?textures.roughness:white);
        bgfx::setTexture(2,metallicSampler,bgfx::isValid(textures.metallic)?textures.metallic:white);
        bgfx::setTexture(3,normalSampler,bgfx::isValid(textures.normal)?textures.normal:white);
        bindLighting(4);
        const Vec4 sphereScopes{float(lighting_scope::All),float(lighting_scope::All),0,1};bgfx::setUniform(surfaceLighting,sphereScopes.data());
        const Vec4 category{float(lighting_scope::World),0,0,0};bgfx::setUniform(surfaceCategory,category.data());
        bindFog(fogValues);
        sphere.bind();
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW);
        bgfx::submit(views.scene,pbr);
    }

    ParticleRenderStats localParticleStats;
    auto* totals=particleStats?particleStats:&localParticleStats;
    const auto bindSpriteLighting=[&]{bindLighting(12);};
    renderParticles(scene.particles,scene.particleMaterials,settings.camera,fogValues,views.scene,totals,{},false,bindSpriteLighting,scene.materialContext,plan.has(RenderPassKind::DepthPrepass)?passView(RenderPassKind::DepthPrepass):InvalidRenderView);
    renderParticles(scene.particles,scene.particleMaterials,settings.camera,fogValues,views.scene,totals,transparentMeshes,true,bindSpriteLighting,scene.materialContext);
    if(ssgiEnabled&&ssgiReady)target.ssgi->finishCapture();
    if(ssgiEnabled)std::erase_if(giDraws,[&](const auto& item){return item.second.frame+2<renderFrame;});

    executePost(plan,settings,target,w,h,presentation,view,projection,scene.collisionOverlays);
    const auto* readback=plan.find(RenderPassKind::ThumbnailReadback);
    return readback?readback->physicalView:InvalidRenderView;
}


void Renderer::Impl::executePost(const RenderPassPlan& plan,const SceneSettings& settings,ViewportTargets& target,
    std::uint16_t w,std::uint16_t h,const EnvironmentPresentation& presentation,const Mat4& view,const Mat4& projection,
    std::span<const RenderCollisionOverlay> overlays) {
    const auto* caps=bgfx::getCaps();
    const bool glowing=plan.has(RenderPassKind::GlowExtract);
    ModelMatrix model;
    const auto purpose=plan.config.view.purpose;
    const auto debugMode=purpose==RenderViewPurpose::Scene?debugViews[0]:purpose==RenderViewPurpose::Game?debugViews[1]:RenderDebugView::Lit;
    const bool dataView=debugMode>=RenderDebugView::Albedo&&debugMode<=RenderDebugView::Normal;
    const float params[]{dataView?1.f:bounded(settings.exposure,1,0,100),caps->originBottomLeft?1.0f:0.0f,
        static_cast<float>(dataView?ToneMapping::Linear:settings.environment.toneMapping),glowing?settings.environment.glow.strength:0.f};
    const auto tone=toneMappingUniforms(settings.environment.toneMappingParameters);
    for(const auto& pass:plan.passes())switch(pass.kind) {
    case RenderPassKind::GlowExtract:
    case RenderPassKind::GlowHorizontal:
    case RenderPassKind::GlowVertical: {
        const auto& scratch=*target.glow;
        const float glowValues[]{bounded(settings.exposure,1,0,100),settings.environment.glow.threshold,
            caps->originBottomLeft?1.f:0.f,settings.environment.glow.radius};
        const bool extract=pass.kind==RenderPassKind::GlowExtract;
        const bool horizontal=pass.kind==RenderPassKind::GlowHorizontal;
        const auto destination=horizontal?scratch.buffers[1]:scratch.buffers[0];
        const auto source=extract?target.hdr:horizontal?scratch.textures[0]:scratch.textures[1];
        const float dx=1.f/scratch.width,dy=1.f/scratch.height;
        const Vec4 texel=extract?Vec4{1.f/w,1.f/h,0,0}:horizontal?Vec4{dx,dy,dx,0}:Vec4{dx,dy,0,dy};
        bgfx::setViewFrameBuffer(pass.physicalView,destination);bgfx::setViewRect(pass.physicalView,0,0,scratch.width,scratch.height);
        bgfx::setViewClear(pass.physicalView,BGFX_CLEAR_NONE);bgfx::setViewTransform(pass.physicalView,nullptr,nullptr);
        bgfx::setUniform(glowParams,glowValues);bgfx::setUniform(glowTexel,texel.data());bgfx::setTexture(0,glowSource,source);
        fullscreen.bind();bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
        bgfx::submit(pass.physicalView,extract?glowExtractProgram:glowBlurProgram);
        break;
    }
    case RenderPassKind::ToneMap: {
        bgfx::setViewFrameBuffer(pass.physicalView,target.compositeBuffer);
        bgfx::setViewRect(pass.physicalView,0,0,w,h);
        bgfx::setViewClear(pass.physicalView,BGFX_CLEAR_NONE);
        bgfx::setViewTransform(pass.physicalView,nullptr,nullptr);
        bgfx::setUniform(presentParams,params);
        bgfx::setUniform(toneCurve,tone.curve.data());bgfx::setUniform(toneShoulder,tone.shoulder.data());
        const auto matte=particle_rendering::backgroundLinear(presentation.matte);
        const float backgroundValue[]{matte[0],matte[1],matte[2],1};
        bgfx::setUniform(background,backgroundValue);
        bgfx::setTexture(0,hdrSampler,target.hdr);
        bgfx::setTexture(1,glowColor,glowing?target.glow->textures[0]:white);
        if(glowing) {
            const float texel[]{1.f/target.glow->width,1.f/target.glow->height,0,0};bgfx::setUniform(glowTexel,texel);
    }
        fullscreen.bind(); bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(pass.physicalView,present);

        break;
    }
    case RenderPassKind::Grid: {
        bgfx::setViewFrameBuffer(pass.physicalView,target.compositeBuffer);
        bgfx::setViewRect(pass.physicalView,0,0,w,h);
        bgfx::setViewClear(pass.physicalView,BGFX_CLEAR_NONE); // Preserve scene depth and the tone-mapped color.
        bgfx::setViewTransform(pass.physicalView,view.data(),projection.data());
        model = ModelIdentity; bgfx::setTransform(model.data());
        const float minor[]{toLinear(appearance.gridMinorSrgb[0]),toLinear(appearance.gridMinorSrgb[1]),toLinear(appearance.gridMinorSrgb[2]),bounded(appearance.gridMinorSrgb[3],1,0,1)};
        const float major[]{toLinear(appearance.gridMajorSrgb[0]),toLinear(appearance.gridMajorSrgb[1]),toLinear(appearance.gridMajorSrgb[2]),bounded(appearance.gridMajorSrgb[3],1,0,1)};
        const float axisU[]{toLinear(1),toLinear(.239f),toLinear(0),.72f};
        const float axisV[]{toLinear(.059f),toLinear(.612f),toLinear(1),.72f};
        const float gridParameterValues[]{1,1,.005f,caps->homogeneousDepth?1.0f:0.0f};
        const float gridWidthValues[]{bounded(appearance.gridMinorWidth,.325f,0,8),bounded(appearance.gridMajorWidth,.250f,0,8),1.5f,0};
        bgfx::setUniform(gridMinor,minor); bgfx::setUniform(gridMajor,major);
        bgfx::setUniform(gridAxisU,axisU); bgfx::setUniform(gridAxisV,axisV); bgfx::setUniform(gridParams,gridParameterValues);
        bgfx::setUniform(gridWidths,gridWidthValues);
        grid.bind();
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL |
            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA));
        bgfx::submit(pass.physicalView,gridProgram);
        break;
    }
    case RenderPassKind::CollisionOverlays: {
        bgfx::setViewFrameBuffer(pass.physicalView,target.compositeBuffer);
        bgfx::setViewRect(pass.physicalView,0,0,w,h);
        bgfx::setViewClear(pass.physicalView,BGFX_CLEAR_NONE);
        bgfx::setViewMode(pass.physicalView,bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(pass.physicalView,view.data(),projection.data());
        collisionOverlays->render(overlays,settings.camera,pass.physicalView,collisionOverlayProgram,renderFrame);
        if(!collisionOverlays->status().empty())modelStatus=collisionOverlays->status();
        else if(modelStatus.starts_with("Collision overlay") || modelStatus.starts_with("Visible collision overlays"))modelStatus.clear();
        break;
    }
    case RenderPassKind::Encode: {
        bgfx::setViewFrameBuffer(pass.physicalView,target.displayBuffer);
        bgfx::setViewRect(pass.physicalView,0,0,w,h);
        bgfx::setViewClear(pass.physicalView,BGFX_CLEAR_NONE);
        bgfx::setViewTransform(pass.physicalView,nullptr,nullptr);
        const float debandingValues[]{static_cast<float>(w),static_cast<float>(h),presentation.debanding?1.0f:0.0f,0};
        bgfx::setUniform(presentParams,params);
        bgfx::setUniform(debandingParams,debandingValues);
        bgfx::setTexture(0,compositeSampler,target.composite);
        fullscreen.bind(); bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(pass.physicalView,encode);
        break;
    }
    default:break; // Scene, persistent updates and readbacks are recorded by their owners.
    }
}

bgfx::TextureHandle Renderer::viewportTexture() const noexcept {
    if (impl_->targets) return impl_->targets->display;
    return BGFX_INVALID_HANDLE;
}
std::uint32_t Renderer::width() const noexcept {return impl_->width;}
std::uint32_t Renderer::height() const noexcept {return impl_->height;}
// Scene rendering and picking target offscreen buffers, independently of which
// native ImGui window presents the image or whether the main window is visible.
bool Renderer::drawable() const noexcept {return impl_->viewportDrawable && impl_->targets != nullptr;}
const char* Renderer::backendName() const noexcept {return bgfx::getRendererName(bgfx::getRendererType());}
GraphicsBackend Renderer::graphicsBackend() const noexcept {
    return bgfx::getRendererType()==bgfx::RendererType::Direct3D11 ? GraphicsBackend::Direct3D11 : GraphicsBackend::Direct3D12;
}

} // namespace crimson
