#include "Renderer.h"
#include "ModelTransform.h"
#include "Mesh.h"
#include "Environment.h"
#include "Picking.h"
#include "ModelGpuCache.h"
#include "ShaderPreflight.h"

#include <SDL3/SDL.h>
#include <bgfx/platform.h>

#include "vs_pbr.h"
#include "fs_pbr.h"
#include "vs_model.h"
#include "fs_model.h"
#include "vs_pick.h"
#include "fs_pick.h"
#include "vs_present.h"
#include "fs_present.h"
#include "vs_grid.h"
#include "fs_grid.h"
#include "fs_encode.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <deque>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace crimson {
namespace {
constexpr bgfx::ViewId PickView = 5, PickReadView = 6;
struct RenderViews { bgfx::ViewId scene,present,grid,encode; };
inline constexpr RenderViews MainViews{SceneView,PresentView,GridView,EncodeView};
inline constexpr RenderViews ThumbnailViews{7,8,9,10};
inline constexpr bgfx::ViewId ThumbnailReadView=11;
std::uint64_t shaderContentHash(std::span<const std::uint8_t> vertex,std::span<const std::uint8_t> fragment) {
    std::uint64_t hash=14695981039346656037ull;
    for (const auto bytes:{vertex,fragment}) for (const auto byte:bytes) {hash^=byte;hash*=1099511628211ull;} return hash;
}
float finiteOr(float value, float fallback) { return std::isfinite(value) ? value : fallback; }
float bounded(float value, float fallback, float lo, float hi) { return std::clamp(finiteOr(value, fallback), lo, hi); }
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
        static_assert(sizeof(ModelVertex) == 18 * sizeof(float));
        static_assert(offsetof(ModelVertex, normal) == 3 * sizeof(float));
        static_assert(offsetof(ModelVertex, uv0) == 6 * sizeof(float));
        static_assert(offsetof(ModelVertex, uv1) == 8 * sizeof(float));
        static_assert(offsetof(ModelVertex, tangent) == 10 * sizeof(float));
        static_assert(offsetof(ModelVertex, color) == 14 * sizeof(float));
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float).end();
        vertices = bgfx::createVertexBuffer(bgfx::copy(data.vertices.data(), static_cast<std::uint32_t>(data.vertices.size() * sizeof(ModelVertex))), layout);
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
    void reset() noexcept { destroyHandle(vertices); destroyHandle(indices); }
};
struct GpuModel {
    std::string id;
    std::vector<GpuMesh> primitives;
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

// Framebuffers borrow textures: HDR and the grid composite share one depth target.
struct ViewportTargets {
    bgfx::TextureHandle hdr{BGFX_INVALID_HANDLE}, composite{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle display{BGFX_INVALID_HANDLE}, depth{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle hdrBuffer{BGFX_INVALID_HANDLE}, compositeBuffer{BGFX_INVALID_HANDLE}, displayBuffer{BGFX_INVALID_HANDLE};
    ~ViewportTargets() {
        destroyHandle(hdrBuffer); destroyHandle(compositeBuffer); destroyHandle(displayBuffer);
        destroyHandle(hdr); destroyHandle(composite); destroyHandle(display); destroyHandle(depth);
    }
    void create(std::uint32_t w, std::uint32_t h) {
        const auto maxSize = std::min<std::uint32_t>(bgfx::getCaps()->limits.maxTextureSize, std::numeric_limits<std::uint16_t>::max());
        if (!w || !h || w > maxSize || h > maxSize) throw std::runtime_error("Viewport exceeds the GPU texture size limit.");
        const auto ww = static_cast<std::uint16_t>(w), hh = static_cast<std::uint16_t>(h);
        constexpr std::uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        hdr = bgfx::createTexture2D(ww, hh, false, 1, bgfx::TextureFormat::RGBA16F, flags);
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
    PickRequest request;
    std::uint64_t epoch{};
    std::uint32_t readyFrame{};
    picking::Pixel pixel{};
    // Dense draw IDs keep full 64-bit persistent entity IDs out of shader floats.
    picking::EntityIds entities;
    std::shared_ptr<PickTargets> targets;
    std::vector<std::shared_ptr<const ResidentModelLod>> resources;
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
struct CachedThumbnail {
    std::uint64_t signature{},lastUsed{}; std::size_t bytes{}; bgfx::TextureHandle texture{BGFX_INVALID_HANDLE};
    ~CachedThumbnail() { destroyHandle(texture); }
};
} // namespace

struct Renderer::Impl {
    bool initialized{false}, windowDrawable{false}, viewportDrawable{false};
    std::uint32_t windowWidth{1}, windowHeight{1}, width{1}, height{1};
    GpuMesh sphere, fullscreen, grid;
    std::unordered_map<std::string,std::unique_ptr<GpuModel>> models;
    std::unique_ptr<ModelGpuCache> streamModels{std::make_unique<ModelGpuCache>()};
    std::uint64_t externalSignature{},programRevision{1},environmentRevision{1};
    ViewportAppearance appearance;
    std::string modelStatus;
    std::shared_ptr<PickTargets> pickTargets;
    std::optional<PickRequest> requestedPick;
    std::unique_ptr<PendingPick> pendingPick;
    std::uint64_t pickEpoch{}, latestPickSerial{};
    std::string pickingStatus;
    std::unique_ptr<ViewportTargets> targets;
    std::unique_ptr<EnvironmentLighting> environment;
    std::unique_ptr<EnvironmentLighting> thumbnailEnvironment;
    std::unique_ptr<PendingThumbnail> pendingThumbnail;
    std::unordered_map<std::string,std::unique_ptr<CachedThumbnail>> thumbnails;
    mutable std::uint64_t thumbnailClock{};
    std::string shaderStatus,thumbnailStatus;
    std::array<std::uint64_t,6> programHashes{};
    struct LodSelection { std::uint32_t level{}; std::uint64_t frame{}; };
    std::unordered_map<std::uint64_t,LodSelection> lodSelections;
    std::uint64_t renderFrame{};
    bgfx::TextureHandle white{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle pbr{BGFX_INVALID_HANDLE}, present{BGFX_INVALID_HANDLE}, gridProgram{BGFX_INVALID_HANDLE}, encode{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle modelProgram{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle pickProgram{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle pickId{BGFX_INVALID_HANDLE}, lightCounts{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle modelBase{BGFX_INVALID_HANDLE}, modelSurface{BGFX_INVALID_HANDLE}, modelEmissive{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle modelUv{BGFX_INVALID_HANDLE}, modelOptions{BGFX_INVALID_HANDLE}, modelMaps{BGFX_INVALID_HANDLE}, modelEmissiveMap{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle modelNormalMatrix{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle metallicRoughnessSampler{BGFX_INVALID_HANDLE}, occlusionSampler{BGFX_INVALID_HANDLE}, emissiveSampler{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle baseColorMetallic{BGFX_INVALID_HANDLE}, roughness{BGFX_INVALID_HANDLE}, textureFlags{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle cameraPos{BGFX_INVALID_HANDLE}, direction{BGFX_INVALID_HANDLE}, directionColor{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle pointPosition{BGFX_INVALID_HANDLE}, pointColor{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle presentParams{BGFX_INVALID_HANDLE}, hdrSampler{BGFX_INVALID_HANDLE}, compositeSampler{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle background{BGFX_INVALID_HANDLE},gridWidths{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle albedoSampler{BGFX_INVALID_HANDLE}, roughnessSampler{BGFX_INVALID_HANDLE}, metallicSampler{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle normalSampler{BGFX_INVALID_HANDLE}, irradianceSampler{BGFX_INVALID_HANDLE}, prefilteredSampler{BGFX_INVALID_HANDLE}, brdfSampler{BGFX_INVALID_HANDLE}, environmentParams{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle gridMinor{BGFX_INVALID_HANDLE}, gridMajor{BGFX_INVALID_HANDLE}, gridAxisU{BGFX_INVALID_HANDLE}, gridAxisV{BGFX_INVALID_HANDLE}, gridParams{BGFX_INVALID_HANDLE};
    void renderTo(const RenderScene&,ViewportTargets&,EnvironmentLighting&,std::uint16_t,std::uint16_t,RenderViews,bool,bool,std::vector<std::shared_ptr<const ResidentModelLod>>* = nullptr);
    ~Impl() {
        if (!initialized) return;
        sphere.reset(); fullscreen.reset(); grid.reset(); models.clear(); streamModels.reset(); targets.reset(); pickTargets.reset(); thumbnails.clear();
        // Keep readback destination memory alive through the final bgfx frame,
        // but release every GPU handle before bgfx shutdown.
        if (pendingPick) pendingPick->targets.reset();
        if (pendingPick) { pendingPick->resources.clear(); pendingPick->pinned.reset(); }
        if (pendingThumbnail) { pendingThumbnail->targets.reset(); destroyHandle(pendingThumbnail->readback); pendingThumbnail->pinned.reset(); pendingThumbnail->resources.clear(); }
        environment.reset();
        thumbnailEnvironment.reset();
        destroyHandle(white);
        destroyHandle(pbr); destroyHandle(present); destroyHandle(gridProgram); destroyHandle(encode);
        destroyHandle(modelProgram); destroyHandle(modelBase); destroyHandle(modelSurface); destroyHandle(modelEmissive);
        destroyHandle(pickProgram); destroyHandle(pickId); destroyHandle(lightCounts);
        destroyHandle(modelUv); destroyHandle(modelOptions); destroyHandle(modelMaps); destroyHandle(modelEmissiveMap); destroyHandle(modelNormalMatrix);
        destroyHandle(metallicRoughnessSampler); destroyHandle(occlusionSampler); destroyHandle(emissiveSampler);
        destroyHandle(baseColorMetallic); destroyHandle(roughness); destroyHandle(textureFlags);
        destroyHandle(cameraPos); destroyHandle(direction); destroyHandle(directionColor);
        destroyHandle(pointPosition); destroyHandle(pointColor);
        destroyHandle(presentParams); destroyHandle(hdrSampler); destroyHandle(compositeSampler);
        destroyHandle(background); destroyHandle(gridWidths);
        destroyHandle(albedoSampler); destroyHandle(roughnessSampler); destroyHandle(metallicSampler);
        destroyHandle(normalSampler); destroyHandle(irradianceSampler); destroyHandle(prefilteredSampler); destroyHandle(brdfSampler); destroyHandle(environmentParams);
        destroyHandle(gridMinor); destroyHandle(gridMajor); destroyHandle(gridAxisU); destroyHandle(gridAxisV); destroyHandle(gridParams);
        bgfx::shutdown();
    }
};

Renderer::Renderer(SDL_Window* window, bgfx::CallbackI* callback) : impl_(std::make_unique<Impl>()) {
    if (!window) throw std::invalid_argument("Renderer requires an SDL window.");
#if defined(_WIN32)
    void* hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd) throw std::runtime_error(std::string("SDL did not provide a native HWND: ") + SDL_GetError());
#else
    throw std::runtime_error("This renderer MVP requires Windows and Direct3D 11.");
#endif
    int w = 1, h = 1;
    if (!SDL_GetWindowSizeInPixels(window, &w, &h)) throw std::runtime_error(std::string("Unable to query window pixels: ") + SDL_GetError());
    impl_->windowWidth = static_cast<std::uint32_t>(std::max(1,w));
    impl_->windowHeight = static_cast<std::uint32_t>(std::max(1,h));
    bgfx::renderFrame(); // Single-thread rendering on the initialization thread.
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.platformData.nwh = hwnd;
    init.callback = callback;
    init.resolution.width = impl_->windowWidth;
    init.resolution.height = impl_->windowHeight;
    init.resolution.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) throw std::runtime_error("bgfx could not initialize Direct3D 11.");
    impl_->initialized = true;
    if (bgfx::getRendererType() != bgfx::RendererType::Direct3D11) throw std::runtime_error("Direct3D 11 was requested but another backend was selected.");
    if (!bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT) ||
        !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT) ||
        !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::D24S8,BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY))
        throw std::runtime_error("This adapter cannot create the required viewport targets.");

    impl_->pbr = program(cc_vs_pbr,cc_fs_pbr,"Normal mapped GGX PBR with split-sum IBL");
    impl_->modelProgram = program(cc_vs_model,cc_fs_model,"glTF metallic roughness PBR with split-sum IBL");
    impl_->pickProgram = program(cc_vs_pick,cc_fs_pick,"Model entity ID picking with material alpha");
    impl_->pickId = uniform("u_pickId");
    impl_->modelBase = uniform("u_modelBase");
    impl_->modelSurface = uniform("u_modelSurface");
    impl_->modelEmissive = uniform("u_modelEmissive");
    impl_->modelUv = uniform("u_modelUv");
    impl_->modelOptions = uniform("u_modelOptions");
    impl_->modelMaps = uniform("u_modelMaps");
    impl_->modelEmissiveMap = uniform("u_modelEmissiveMap");
    impl_->modelNormalMatrix = uniform("u_modelNormalMatrix",bgfx::UniformType::Mat4);
    impl_->metallicRoughnessSampler = uniform("s_metallicRoughness",bgfx::UniformType::Sampler);
    impl_->occlusionSampler = uniform("s_occlusion",bgfx::UniformType::Sampler);
    impl_->emissiveSampler = uniform("s_emissive",bgfx::UniformType::Sampler);
    impl_->present = program(cc_vs_present,cc_fs_present,"Linear Reinhard tone map");
    impl_->gridProgram = program(cc_vs_grid,cc_fs_grid,"Quader analytic construction grid");
    impl_->encode = program(cc_vs_present,cc_fs_encode,"Viewport sRGB encoding");
    impl_->programHashes={shaderContentHash(cc_vs_pbr,cc_fs_pbr),shaderContentHash(cc_vs_model,cc_fs_model),shaderContentHash(cc_vs_pick,cc_fs_pick),
        shaderContentHash(cc_vs_present,cc_fs_present),shaderContentHash(cc_vs_grid,cc_fs_grid),shaderContentHash(cc_vs_present,cc_fs_encode)};
    impl_->baseColorMetallic = uniform("u_baseColorMetallic");
    impl_->roughness = uniform("u_roughness");
    impl_->textureFlags = uniform("u_textureFlags");
    impl_->cameraPos = uniform("u_cameraPos");
    impl_->direction = uniform("u_direction",bgfx::UniformType::Vec4,static_cast<std::uint16_t>(MaxDirectionalLights));
    impl_->directionColor = uniform("u_directionColor",bgfx::UniformType::Vec4,static_cast<std::uint16_t>(MaxDirectionalLights));
    impl_->pointPosition = uniform("u_pointPosition",bgfx::UniformType::Vec4,static_cast<std::uint16_t>(MaxPointLights));
    impl_->pointColor = uniform("u_pointColor",bgfx::UniformType::Vec4,static_cast<std::uint16_t>(MaxPointLights));
    impl_->lightCounts = uniform("u_lightCounts");
    impl_->presentParams = uniform("u_presentParams");
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
    impl_->environment = std::make_unique<EnvironmentLighting>();
    impl_->thumbnailEnvironment=std::make_unique<EnvironmentLighting>();
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
    impl_->windowDrawable = w > 0 && h > 0;
    resizeViewport(impl_->windowWidth,impl_->windowHeight);
    bgfx::setViewName(SceneView,"00: PBR scene - linear HDR");
    bgfx::setViewName(PresentView,"01: Linear exposure and Reinhard");
    bgfx::setViewName(GridView,"02: Neutral analytic grid overlay");
    bgfx::setViewName(EncodeView,"03: Viewport sRGB image");
    bgfx::setViewName(PickView,"05: On-demand model entity IDs");
    bgfx::setViewName(PickReadView,"06: Picking pixel readback");
    // Keep natural view IDs: this pinned bgfx backend compares blit order keys
    // with decoded view IDs. Moving PickReadView ahead of a lower-numbered
    // visible pass would copy the ID texture before its model draws complete.
    // Picking is offscreen, so it can follow the complete visible frame.
    const bgfx::ViewId order[]{SceneView,PresentView,GridView,EncodeView,ToolsView,PickView,PickReadView,7,8,9,10,11};
    bgfx::setViewOrder(SceneView,static_cast<std::uint16_t>(std::size(order)),order);
    for (const auto id : order) bgfx::setViewMode(id,bgfx::ViewMode::Sequential);
}
Renderer::~Renderer() = default;

bool Renderer::prepareEnvironment(const std::filesystem::path& projectRoot, const std::string& sourceId) {
    const bool result=impl_->environment->prepare(projectRoot, sourceId); if (result) ++impl_->environmentRevision; return result;
}
bool Renderer::publishEnvironment(const PreparedEnvironment& data) { const bool result=impl_->environment->publish(data); if (result) ++impl_->environmentRevision; return result; }
bool Renderer::publishThumbnailEnvironment(const PreparedEnvironment& data) {
    const bool result=impl_->thumbnailEnvironment->publish(data);impl_->thumbnailStatus=result ? std::string{}:impl_->thumbnailEnvironment->status();return result;
}
const std::string& Renderer::environmentStatus() const { return impl_->environment->status(); }

bool Renderer::prepareModel(const ModelAsset& model) {
    if (impl_->models.contains(model.id)) {
        impl_->modelStatus.clear();
        return true;
    }
    try {
        if (model.id.empty() || model.primitives.empty() || model.draws.empty() || model.materials.empty())
            throw std::invalid_argument("Model has no renderable geometry or materials.");
        auto next = std::make_unique<GpuModel>();
        next->id = model.id;
        next->primitives.resize(model.primitives.size());
        for (std::size_t i = 0; i < model.primitives.size(); ++i) {
            if (model.primitives[i].material >= model.materials.size())
                throw std::invalid_argument("Model primitive references a missing material.");
            next->primitives[i].uploadModel(model.primitives[i]);
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
    impl_->streamModels->advance(bytes,milliseconds);
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
void Renderer::setExternalResourceSignature(std::uint64_t value) { impl_->externalSignature=value; }
std::size_t Renderer::modelResidentBytes() const { return impl_->streamModels->bytes(); }
void Renderer::trimModelCache(std::size_t bytes,std::span<const std::string> live) { impl_->streamModels->trim(bytes,live); }
void Renderer::setAppearance(const ViewportAppearance& appearance) { impl_->appearance=appearance; }
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
        impl_->renderTo(scene,*pending->targets,ready ? *impl_->thumbnailEnvironment:*impl_->environment,size,size,ThumbnailViews,false,false,&pending->resources);
        bgfx::blit(ThumbnailReadView,pending->readback,0,0,pending->targets->display,0,0,size,size);
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
    if (id.empty() || !image.width || !image.height || image.width>512 || image.height>512 || image.rgba.size()!=static_cast<std::size_t>(image.width)*image.height*4) return false;
    if (const auto found=impl_->thumbnails.find(id);found!=impl_->thumbnails.end() && found->second->signature==signature) return true;
    auto next=std::make_unique<CachedThumbnail>(); next->signature=signature; next->bytes=image.rgba.size(); next->lastUsed=++impl_->thumbnailClock;
    next->texture=bgfx::createTexture2D(static_cast<std::uint16_t>(image.width),static_cast<std::uint16_t>(image.height),false,1,bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP,bgfx::copy(image.rgba.data(),static_cast<std::uint32_t>(image.rgba.size())));
    if (!bgfx::isValid(next->texture)) return false;
    impl_->thumbnails.insert_or_assign(id,std::move(next));
    const auto total=[&] { std::size_t bytes=0; for (const auto& [key,value]:impl_->thumbnails) { (void)key; bytes+=value->bytes; } return bytes; };
    while (total()>64*1024*1024 && impl_->thumbnails.size()>1) {
        const auto victim=std::min_element(impl_->thumbnails.begin(),impl_->thumbnails.end(),[](const auto& a,const auto& b){return a.second->lastUsed<b.second->lastUsed;});
        impl_->thumbnails.erase(victim);
    }
    return true;
}
bgfx::TextureHandle Renderer::thumbnailTexture(const std::string& id,std::uint64_t signature) const {
    const auto found=impl_->thumbnails.find(id);
    if (found==impl_->thumbnails.end() || (signature && found->second->signature!=signature)) return BGFX_INVALID_HANDLE;
    found->second->lastUsed=++impl_->thumbnailClock; return found->second->texture;
}
void Renderer::evictThumbnail(const std::string& id) { impl_->thumbnails.erase(id); }
std::size_t Renderer::thumbnailResidentBytes() const {
    std::size_t bytes=0;
    for (const auto& [id,value]:impl_->thumbnails) { (void)id;bytes+=value->bytes; }
    if (impl_->pendingThumbnail) bytes+=static_cast<std::size_t>(impl_->pendingThumbnail->image.width)*impl_->pendingThumbnail->image.height*28;
    return bytes;
}
void Renderer::trimThumbnailCache(std::size_t target) {
    while (!impl_->thumbnails.empty() && thumbnailResidentBytes()>target) {
        const auto victim=std::min_element(impl_->thumbnails.begin(),impl_->thumbnails.end(),[](const auto& a,const auto& b){return a.second->lastUsed<b.second->lastUsed;});
        impl_->thumbnails.erase(victim);
    }
}
const std::string& Renderer::thumbnailStatus() const { return impl_->thumbnailStatus; }
bool Renderer::reloadPrograms(std::span<const RendererShaderPair> pairs) {
    std::vector<std::pair<bgfx::ProgramHandle*,bgfx::ProgramHandle>> candidates;
    std::vector<std::pair<std::size_t,std::uint64_t>> hashes;
    try {
        for (const auto& pair:pairs) {
            bgfx::ProgramHandle* target{}; std::span<const std::uint8_t> contractV,contractF;
            switch (pair.program) {
            case RendererProgram::Sphere: target=&impl_->pbr; contractV=cc_vs_pbr; contractF=cc_fs_pbr; break;
            case RendererProgram::Model: target=&impl_->modelProgram; contractV=cc_vs_model; contractF=cc_fs_model; break;
            case RendererProgram::Picking: target=&impl_->pickProgram; contractV=cc_vs_pick; contractF=cc_fs_pick; break;
            case RendererProgram::Present: target=&impl_->present; contractV=cc_vs_present; contractF=cc_fs_present; break;
            case RendererProgram::Grid: target=&impl_->gridProgram; contractV=cc_vs_grid; contractF=cc_fs_grid; break;
            case RendererProgram::Encode: target=&impl_->encode; contractV=cc_vs_present; contractF=cc_fs_encode; break;
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
        for (auto& [target,candidate]:candidates) { destroyHandle(*target); *target=candidate; }
        for (const auto& [index,hash]:hashes) impl_->programHashes[index]=hash;
        if (!candidates.empty()) { ++impl_->programRevision; cancelPicking(); }
        impl_->shaderStatus.clear(); return true;
    } catch (const std::exception& error) {
        for (auto& [target,candidate]:candidates) { (void)target; destroyHandle(candidate); }
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
    if (!impl_->requestedPick->resourceSignature) impl_->requestedPick->resourceSignature=streamingSignature();
    impl_->pickingStatus.clear();
    return true;
}

std::optional<PickResult> Renderer::pollPick(std::uint32_t bgfxFrame) {
    if (!impl_->pendingPick || !picking::frameReady(bgfxFrame,impl_->pendingPick->readyFrame)) return {};
    auto pending = std::move(impl_->pendingPick);
    if (pending->epoch != impl_->pickEpoch || pending->request.serial != impl_->latestPickSerial) return {};
    const auto entity = pending->entities.resolve(pending->pixel);
    if (!entity) {
        impl_->pickingStatus = "Viewport picking returned an invalid entity ID.";
        return {};
    }
    if (pending->request.resourceSignature && pending->request.resourceSignature!=streamingSignature()) return {};
    return PickResult{pending->request.generation,pending->request.revision,pending->request.serial,*entity,pending->request.resourceSignature};
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
    if (!width || !height) { impl_->windowDrawable = false; return; }
    if (width != impl_->windowWidth || height != impl_->windowHeight) {
        bgfx::reset(width,height,BGFX_RESET_VSYNC);
        impl_->windowWidth = width; impl_->windowHeight = height;
    }
    impl_->windowDrawable = true;
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

void Renderer::render(const SceneSettings& settings, const MaterialTextures& textures) {
    render(RenderScene{settings, textures});
}
void Renderer::render(const RenderScene& scene) {
    if (!drawable()) return;
    impl_->renderTo(scene,*impl_->targets,*impl_->environment,static_cast<std::uint16_t>(impl_->width),static_cast<std::uint16_t>(impl_->height),MainViews,true,true);
}
void Renderer::Impl::renderTo(const RenderScene& scene,ViewportTargets& target,EnvironmentLighting& lighting,
    std::uint16_t w,std::uint16_t h,RenderViews views,bool drawGrid,bool allowPicking,std::vector<std::shared_ptr<const ResidentModelLod>>* pinnedGeometry) {
    if (allowPicking) {
        ++renderFrame;
        streamModels->beginMainView();
        if (renderFrame%120==0) std::erase_if(lodSelections,[&](const auto& item){return renderFrame-item.second.frame>120;});
    }
    const auto& settings = scene.settings;
    const auto& textures = scene.sphereTextures;
    const auto* caps = bgfx::getCaps();
    bgfx::setViewFrameBuffer(views.scene,target.hdrBuffer);
    bgfx::setViewRect(views.scene,0,0,w,h);
    bgfx::setViewClear(views.scene,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,0u,1.0f,0);
    bgfx::touch(views.scene); // Also clear when every model draw has a singular transform.
    ModelMatrix model;
    const auto eye = settings.camera.eye();
    const float aspect = static_cast<float>(w)/static_cast<float>(h);
    const auto view = settings.camera.viewMatrix();
    const auto projection = settings.camera.projectionMatrix(aspect,caps->homogeneousDepth ? ClipDepth::NegativeOneToOne : ClipDepth::ZeroToOne);
    bgfx::setViewTransform(views.scene,view.data(),projection.data());
    const float camera[]{eye[0],eye[1],eye[2],0};
    const float environmentValues[]{settings.environment.enabled ? bounded(settings.environment.intensity,1,0,10) : 0,
        bounded(settings.environment.rotation,0,-180,180) * std::numbers::pi_v<float> / 180.0f,
        lighting.maxSpecularLod(),0};
    const std::span<const DirectionalLight> directional = scene.sceneMode ? scene.directionalLights : std::span<const DirectionalLight>(&settings.directional,1);
    const std::span<const PointLight> points = scene.sceneMode ? scene.pointLights : std::span<const PointLight>(settings.points);
    const auto directionalCount = std::min(directional.size(),MaxDirectionalLights);
    const auto pointCount = std::min(points.size(),MaxPointLights);
    if (directional.size() > MaxDirectionalLights || points.size() > MaxPointLights)
        modelStatus = "Scene exceeds the renderer budget of 4 directional and 16 point lights.";
    std::array<float,MaxDirectionalLights*4> directions{},directionColors{};
    std::array<float,MaxPointLights*4> pointPositions{},pointColors{};
    for (std::size_t i=0;i<directionalCount;++i) {
        const auto& light = directional[i];
        const auto lightDirection = math::normalized(light.direction,{-.5f,-.8f,-.6f});
        for (std::size_t j=0;j<3;++j) { directions[i*4+j]=lightDirection[j]; directionColors[i*4+j]=toLinear(light.color[j]); }
        directions[i*4+3]=light.enabled ? bounded(light.intensity,0,0,10000) : 0;
    }
    for (std::size_t i=0;i<pointCount;++i) {
        const auto& light = points[i];
        for (std::size_t j=0;j<3;++j) {pointPositions[i*4+j]=finiteOr(light.position[j],0);pointColors[i*4+j]=toLinear(light.color[j]);}
        pointPositions[i*4+3]=light.enabled ? bounded(light.intensity,0,0,10000) : 0;
    }
    const float lightCountValues[]{static_cast<float>(directionalCount),static_cast<float>(pointCount),0,0};
    const auto bindLighting = [&](std::uint8_t firstEnvironmentSlot) {
        bgfx::setUniform(cameraPos,camera); bgfx::setUniform(direction,directions.data(),static_cast<std::uint16_t>(MaxDirectionalLights));
        bgfx::setUniform(directionColor,directionColors.data(),static_cast<std::uint16_t>(MaxDirectionalLights));
        bgfx::setUniform(pointPosition,pointPositions.data(),static_cast<std::uint16_t>(MaxPointLights));
        bgfx::setUniform(pointColor,pointColors.data(),static_cast<std::uint16_t>(MaxPointLights));
        bgfx::setUniform(lightCounts,lightCountValues);
        bgfx::setUniform(environmentParams,environmentValues);
        bgfx::setTexture(firstEnvironmentSlot,irradianceSampler,lighting.diffuse());
        bgfx::setTexture(firstEnvironmentSlot+1,prefilteredSampler,lighting.specular());
        bgfx::setTexture(firstEnvironmentSlot+2,brdfSampler,lighting.brdf());
    };
    if (allowPicking && !scene.sceneMode && requestedPick) { ++pickEpoch; requestedPick.reset(); }
    const bool capturePick = allowPicking && scene.sceneMode && requestedPick && !pendingPick && pickTargets;
    if (capturePick) {
        pendingPick = std::make_unique<PendingPick>();
        pendingPick->request = *requestedPick;
        pendingPick->epoch = pickEpoch;
        pendingPick->targets = pickTargets;
        pendingPick->pinned=scene.pinned;
        requestedPick.reset();
        bgfx::setViewFrameBuffer(PickView,pickTargets->framebuffer);
        bgfx::setViewRect(PickView,0,0,w,h);
        bgfx::setViewTransform(PickView,view.data(),projection.data());
        bgfx::setViewClear(PickView,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,0,1.0f,0);
        bgfx::touch(PickView);
    }
    std::array<RenderModelInstance,1> legacy;
    auto instances = scene.instances;
    if (!scene.sceneMode && scene.model && scene.model->asset) {
        legacy[0] = {0,*scene.model,scene.modelTextures};
        instances = legacy;
    }
    std::size_t skipped = 0, collapsed = 0, missing = 0;
    for (const auto& instance : instances) {
        if (!instance.model.asset) { ++missing; continue; }
        const auto& asset = *instance.model.asset;
        const auto found = models.find(asset.id);
        const auto* stream=streamModels->descriptor(asset.id,instance.resourceRevision);
        if (found == models.end() && !(stream && streamModels->ready(asset.id,instance.resourceRevision))) {
            ++missing;
        } else {
            if (allowPicking && stream) streamModels->markRendered(asset.id,instance.resourceRevision);
            std::array<float,4> pickColor{};
            if (capturePick && instance.pickable && instance.entityId) {
                pickColor = picking::color(pendingPick->entities.add(instance.entityId));
            }
            const std::array<bgfx::UniformHandle, 5> samplers{albedoSampler, metallicRoughnessSampler,
                normalSampler, occlusionSampler, emissiveSampler};
            std::uint32_t drawIndex=0;
            for (const auto& draw : asset.draws) {
                const auto currentDraw=drawIndex++;
                ModelMatrix world, normal;
                float handedness = 1;
                const auto transform=prepareModelMatrices(instance.model.transform,draw.transform,world,normal,handedness);
                if (transform==ModelTransformStatus::Collapsed) { ++collapsed; continue; }
                if (transform==ModelTransformStatus::Invalid) { ++skipped; continue; }
                std::shared_ptr<const ResidentModelLod> resident;
                if (stream && draw.primitive<stream->primitives.size()) {
                    const auto& primitive=stream->primitives[draw.primitive];
                    auto desired=instance.forceFullDetail ? 0u : streaming::chooseModelLod(primitive.levels,world,primitive.bounds,eye,
                        settings.camera.verticalFov(aspect)*std::numbers::pi_v<float>/180,h);
                    if (allowPicking && instance.entityId && !instance.forceFullDetail) {
                        const auto key=(instance.entityId*1099511628211ull)^currentDraw;
                        const auto previous=lodSelections.find(key);
                        if (previous!=lodSelections.end() && previous->second.level!=desired)
                            desired=streaming::chooseModelLod(primitive.levels,world,primitive.bounds,eye,settings.camera.verticalFov(aspect)*std::numbers::pi_v<float>/180,h,
                                desired>previous->second.level ? .8f:1.2f);
                        lodSelections[key]={desired,renderFrame};
                    }
                    resident=streamModels->resident(asset.id,instance.resourceRevision,draw.primitive,desired);
                }
                if (!resident && (found==models.end() || draw.primitive>=found->second->primitives.size())) { ++missing; continue; }
                if (resident && pinnedGeometry) pinnedGeometry->push_back(resident);
                const auto bindGeometry=[&] { if (resident) resident->bind(); else found->second->primitives[draw.primitive].bind(); };
                const auto materialIndex = asset.primitives[draw.primitive].material;
                const auto& material=instance.materials && materialIndex<instance.materials->size() ? (*instance.materials)[materialIndex]:asset.materials[materialIndex];
                const ModelMaterialTextures emptyBindings;
                const auto& bindings = instance.textures && materialIndex < instance.textures->size()
                    ? (*instance.textures)[materialIndex] : emptyBindings;
                float mapFlags[4]{};
                for (std::uint8_t slot = 0; slot < samplers.size(); ++slot) {
                    const bool valid = bgfx::isValid(bindings.handles[slot]);
                    if (slot < 4) mapFlags[slot] = valid ? 1.0f : 0.0f;
                    bgfx::setTexture(slot,samplers[slot],valid ? bindings.handles[slot] : white,bindings.flags[slot]);
                }
                const float surface[]{material.metallic,material.roughness,material.normalScale,material.occlusionStrength};
                const float emissive[]{material.emissive[0],material.emissive[1],material.emissive[2],material.alphaCutoff};
                const float uv[]{static_cast<float>(material.baseColorTexture.texCoord),static_cast<float>(material.metallicRoughnessTexture.texCoord),
                    static_cast<float>(material.normalTexture.texCoord),static_cast<float>(material.occlusionTexture.texCoord)};
                const float options[]{static_cast<float>(material.emissiveTexture.texCoord),material.alphaMask ? 1.0f : 0.0f,
                    material.doubleSided ? 1.0f : 0.0f,handedness};
                const float emissiveMap[]{bgfx::isValid(bindings.handles[4]) ? 1.0f : 0.0f,0,0,0};
                bgfx::setTransform(world.data());
                bgfx::setUniform(modelNormalMatrix,normal.data());
                bgfx::setUniform(modelBase,material.baseColor.data());
                bgfx::setUniform(modelSurface,surface); bgfx::setUniform(modelEmissive,emissive);
                bgfx::setUniform(modelUv,uv); bgfx::setUniform(modelOptions,options);
                bgfx::setUniform(modelMaps,mapFlags); bgfx::setUniform(modelEmissiveMap,emissiveMap);
                bindLighting(5);
                bindGeometry();
                const std::uint64_t cull = material.doubleSided ? BGFX_STATE_NONE : handedness < 0 ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | cull);
                bgfx::submit(views.scene,modelProgram);
                if (capturePick && instance.pickable && instance.entityId) {
                    if (resident) pendingPick->resources.push_back(resident);
                    // This pass deliberately shares model transforms, winding,
                    // base-color UV/filter flags, vertex alpha and mask cutoff.
                    // It never writes to the visible scene depth or HDR targets.
                    bgfx::setTransform(world.data());
                    bgfx::setUniform(modelBase,material.baseColor.data());
                    bgfx::setUniform(modelEmissive,emissive);
                    bgfx::setUniform(modelUv,uv);
                    bgfx::setUniform(modelOptions,options);
                    bgfx::setUniform(modelMaps,mapFlags);
                    bgfx::setUniform(pickId,pickColor.data());
                    bgfx::setTexture(0,albedoSampler,bgfx::isValid(bindings.handles[0]) ? bindings.handles[0] : white,bindings.flags[0]);
                    bindGeometry();
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | cull);
                    bgfx::submit(PickView,pickProgram);
                }
            }
        }
    }
    if (allowPicking) {
        if (missing) modelStatus = "Scene has " + std::to_string(missing) + " model instance(s) without prepared resources.";
        else if (skipped) modelStatus = "Skipped " + std::to_string(skipped) + " draw(s) with invalid or nonfinite transforms.";
        else if (collapsed) modelStatus = "Hidden " + std::to_string(collapsed) + " draw(s) with a collapsed transform (for example, zero scale).";
        else if (modelStatus.starts_with("Skipped ") || modelStatus.starts_with("Hidden ") || modelStatus.starts_with("Scene has ")) modelStatus.clear();
    }
    if (capturePick) {
        auto& pending = *pendingPick;
        const auto x = static_cast<std::uint16_t>(pending.request.x);
        const auto y = static_cast<std::uint16_t>(caps->originBottomLeft ? height-1-pending.request.y : pending.request.y);
        bgfx::blit(PickReadView,pickTargets->readback,0,0,pickTargets->color,x,y,1,1);
        pending.readyFrame = bgfx::readTexture(pickTargets->readback,pending.pixel.data());
    }
    if (!scene.sceneMode && !(scene.model && scene.model->asset)) {
        model = math::translation({0,1,0}); // Radius-one sphere rests on the ground plane.
        bgfx::setTransform(model.data());
        const auto& m = settings.material;
        const float base[]{toLinear(m.baseColor[0]),toLinear(m.baseColor[1]),toLinear(m.baseColor[2]),bounded(m.metallic,0,0,1)};
        // The shared perceptual-roughness floor is applied after map multiplication.
        const float rough[]{bounded(m.roughness,.35f,0,1),bounded(m.normalStrength,1,0,2),0,0};
        const float mapFlags[]{bgfx::isValid(textures.albedo)?1.0f:0.0f,bgfx::isValid(textures.roughness)?1.0f:0.0f,bgfx::isValid(textures.metallic)?1.0f:0.0f,bgfx::isValid(textures.normal)?1.0f:0.0f};
        bgfx::setUniform(baseColorMetallic,base); bgfx::setUniform(roughness,rough); bgfx::setUniform(textureFlags,mapFlags);
        bgfx::setTexture(0,albedoSampler,bgfx::isValid(textures.albedo)?textures.albedo:white);
        bgfx::setTexture(1,roughnessSampler,bgfx::isValid(textures.roughness)?textures.roughness:white);
        bgfx::setTexture(2,metallicSampler,bgfx::isValid(textures.metallic)?textures.metallic:white);
        bgfx::setTexture(3,normalSampler,bgfx::isValid(textures.normal)?textures.normal:white);
        bindLighting(4);
        sphere.bind();
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW);
        bgfx::submit(views.scene,pbr);
    }

    const float params[]{bounded(settings.exposure,1,0,100),caps->originBottomLeft?1.0f:0.0f,0,0};
    bgfx::setViewFrameBuffer(views.present,target.compositeBuffer);
    bgfx::setViewRect(views.present,0,0,w,h);
    bgfx::setViewClear(views.present,BGFX_CLEAR_NONE);
    bgfx::setViewTransform(views.present,nullptr,nullptr);
    bgfx::setUniform(presentParams,params);
    const float backgroundValue[]{toLinear(appearance.backgroundSrgb[0]),toLinear(appearance.backgroundSrgb[1]),toLinear(appearance.backgroundSrgb[2]),1};
    bgfx::setUniform(background,backgroundValue);
    bgfx::setTexture(0,hdrSampler,target.hdr);
    fullscreen.bind(); bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(views.present,present);

    if (drawGrid) {
    bgfx::setViewFrameBuffer(views.grid,target.compositeBuffer);
    bgfx::setViewRect(views.grid,0,0,w,h);
    bgfx::setViewClear(views.grid,BGFX_CLEAR_NONE); // Preserve scene depth and the tone-mapped color.
    bgfx::setViewTransform(views.grid,view.data(),projection.data());
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
    bgfx::submit(views.grid,gridProgram);
    }

    bgfx::setViewFrameBuffer(views.encode,target.displayBuffer);
    bgfx::setViewRect(views.encode,0,0,w,h);
    bgfx::setViewClear(views.encode,BGFX_CLEAR_NONE);
    bgfx::setViewTransform(views.encode,nullptr,nullptr);
    bgfx::setUniform(presentParams,params);
    bgfx::setTexture(0,compositeSampler,target.composite);
    fullscreen.bind(); bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(views.encode,encode);
}

bgfx::TextureHandle Renderer::viewportTexture() const noexcept {
    if (impl_->targets) return impl_->targets->display;
    return BGFX_INVALID_HANDLE;
}
std::uint32_t Renderer::width() const noexcept {return impl_->width;}
std::uint32_t Renderer::height() const noexcept {return impl_->height;}
bool Renderer::drawable() const noexcept {return impl_->windowDrawable && impl_->viewportDrawable && impl_->targets != nullptr;}
const char* Renderer::backendName() const noexcept {return bgfx::getRendererName(bgfx::getRendererType());}

} // namespace crimson
