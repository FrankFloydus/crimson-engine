#pragma once

#include "RenderTypes.h"
#include "ParticleRenderTypes.h"
#include "assets/ModelAsset.h"
#include "assets/MaterialAsset.h"
#include "assets/MaterialPreviewGeometry.h"
#include "resources/StreamingModel.h"
#include "resources/TextureMipCache.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

struct SDL_Window;
namespace bgfx { struct CallbackI; }

namespace crimson {
struct PreparedEnvironment;

struct MaterialTextures {
    bgfx::TextureHandle albedo{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle roughness{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle metallic{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle normal{BGFX_INVALID_HANDLE};
};

// Base color, packed metallic/roughness, normal, occlusion, emissive.
// Textures are borrowed from AssetLibrary; sampler overrides belong to each slot.
struct ModelMaterialTextures {
    std::array<bgfx::TextureHandle, 5> handles{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<std::uint32_t, 5> flags{{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}};
    std::array<std::shared_ptr<const void>,5> leases;
    std::array<std::uint64_t,5> sourceFingerprints{},signatures{};
    bool complete{true};
    bool sourceReady{true};
};

// Prepared renderer values only. The color texture uses TextureStreamer's Color
// role (hardware sRGB RGB decode, linear alpha); an absent texture is procedural.
struct RenderParticleMaterial {
    Vec4 baseColor{1,1,1,1}; // Linear factors, including opacity.
    ParticleBlendMode blend{ParticleBlendMode::Additive};
    bgfx::TextureHandle colorTexture{BGFX_INVALID_HANDLE};
    std::uint32_t samplerFlags{UINT32_MAX};
    std::shared_ptr<const void> pinned; // Material definition and texture leases.
    Vec3 uvScale{1,1,1},uvOffset{},uvRotation{};
    bool triplanar{false}; // Sprite rendering intentionally remains two-dimensional.
};

inline constexpr std::size_t MaxDirectionalLights = 4;
inline constexpr std::size_t MaxPointLights = 16;

struct RenderModelInstance {
    std::uint64_t entityId{};
    ModelInstance model;
    const std::vector<ModelMaterialTextures>* textures{nullptr};
    std::uint64_t resourceRevision{};
    bool forceFullDetail{false},pickable{true};
    const std::vector<ModelMaterial>* materials{nullptr};
    MaterialShading shading{MaterialShading::StandardPbr};
    MaterialBlend blend{MaterialBlend::Opaque};
    bool castShadows{true},receiveShadows{true};
};

struct PickRequest {
    std::uint64_t generation{}, revision{}, serial{};
    std::uint32_t x{}, y{}; // Top-left viewport texture pixel, independent of desktop DPI.
    std::uint64_t resourceSignature{};
};
struct PickResult {
    std::uint64_t generation{}, revision{}, serial{}, entityId{}; // Zero means background.
    std::uint64_t resourceSignature{};
};

struct ThumbnailResult {
    std::string id;
    std::uint64_t signature{};
    DecodedImage image;
};
enum class RendererProgram { Sphere, Model, Picking, Present, Grid, Encode, Particle, Sky, PointShadow, GlowExtract, GlowBlur };
struct RendererShaderPair { RendererProgram program; std::vector<std::uint8_t> vertex,fragment; };

struct RenderScene {
    const SceneSettings& settings;
    MaterialTextures sphereTextures;
    const ModelInstance* model{nullptr};
    const std::vector<ModelMaterialTextures>* modelTextures{nullptr};
    std::span<const RenderModelInstance> instances;
    std::span<const DirectionalLight> directionalLights;
    std::span<const PointLight> pointLights;
    bool sceneMode{false}; // A scene with no instances is empty; legacy mode previews a sphere.
    std::shared_ptr<const void> pinned;
    std::span<const RenderParticle> particles;
    std::span<const RenderParticleMaterial> particleMaterials; // Empty uses procedural additive material 0.
    // Complete light-range caster set, independent of Scene/Game camera culling.
    // A zero world ID explicitly disables shadows for isolated material previews.
    std::span<const RenderModelInstance> shadowInstances;
    std::uint64_t shadowWorldId{};
    std::uint16_t shadowResolution{1024};
};

// Owns the bgfx lifetime. Destroy ImGui's GPU resources before this object.
// The application calls bgfx::frame exactly once after render and the tools UI.
class Renderer final {
public:
    explicit Renderer(SDL_Window* window, bgfx::CallbackI* callback = nullptr);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize(std::uint32_t width, std::uint32_t height);
    void resizeViewport(std::uint32_t width, std::uint32_t height);
    // Independent camera view. It shares scene resources and the one bgfx
    // frame, but never changes editor picking, overlays or viewport targets.
    void resizeGameViewport(std::uint32_t width,std::uint32_t height);
    void renderGame(const RenderScene&);
    bgfx::TextureHandle gameViewportTexture() const noexcept;
    void releaseGameViewport();
    const ParticleRenderStats& gameParticleStats() const noexcept;
    // Independent reusable preview; submission shares the application's one
    // bgfx frame and never modifies the main viewport targets or picking.
    void resizeParticlePreview(std::uint32_t width, std::uint32_t height);
    void resizeMaterialPreview(std::uint32_t width,std::uint32_t height);
    void renderMaterialPreview(const SceneSettings&,const MaterialAsset&,const ModelMaterialTextures&,
        MaterialPreviewMesh,const Mat4& objectTransform,bool showEnvironment,std::shared_ptr<const void> pins={});
    bgfx::TextureHandle materialPreviewTexture() const noexcept;
    void releaseMaterialPreview();
    bool publishMaterialPreviewEnvironment(const PreparedEnvironment&);
    void renderParticlePreview(const SceneSettings&, std::span<const RenderParticle>,
                               std::span<const RenderParticleMaterial> = {});
    bgfx::TextureHandle particlePreviewTexture() const noexcept;
    void releaseParticlePreview();
    const ParticleRenderStats& particleStats() const noexcept;
    const ParticleRenderStats& particlePreviewStats() const noexcept;
    const std::string& particleStatus() const;
    const std::string& glowStatus() const;
    std::size_t glowResidentBytes() const noexcept;
    std::size_t shadowResidentBytes() const;
    const PointShadowStats& shadowStats() const;
    const std::string& shadowStatus() const;
    // Called at startup or on source assignment, outside ordinary frame submission.
    bool prepareEnvironment(const std::filesystem::path& projectRoot, const std::string& sourceId);
    bool publishEnvironment(const PreparedEnvironment&);
    bool publishThumbnailEnvironment(const PreparedEnvironment&);
    const std::string& environmentStatus() const;
    std::size_t environmentResidentBytes() const;
    // Transactional GPU preparation outside frame submission. Failed preparation
    // preserves cached assets. Instances with the same ID share one GPU bundle.
    bool prepareModel(const ModelAsset& model);
    void configureModelStream(const std::string&,std::uint64_t revision,const streaming::ModelStream&);
    bool queueModelLod(const std::string&,std::uint64_t revision,std::shared_ptr<const ModelGeometryLevel>);
    // Once per owner frame, before submission; also rotates submitted material
    // leases so GPU inputs survive completion of their queued rendering frame.
    void advanceStreaming(std::size_t uploadBytes = 4*1024*1024,double milliseconds = 2);
    bool modelStreamReady(const std::string&,std::uint64_t revision) const;
    bool modelLodReady(const std::string&,std::uint64_t revision,std::uint32_t primitive,std::uint32_t level) const;
    void evictModel(const std::string&);
    std::uint64_t streamingSignature() const;
    void setExternalResourceSignature(std::uint64_t);
    std::size_t modelResidentBytes() const;
    void trimModelCache(std::size_t targetBytes,std::span<const std::string> liveIds);
    void clearModel();
    const std::string& modelStatus() const;
    // ID picking is rendered only on demand, with the same transforms and alpha
    // cutouts as the visible model pass. Results are asynchronous and carry the
    // caller's scene/interaction tags; the caller must reject stale tags too.
    bool requestPick(const PickRequest& request);
    std::optional<PickResult> pollPick(std::uint32_t bgfxFrame);
    void cancelPicking();
    std::uint32_t drainPending(std::uint32_t bgfxFrame,std::uint32_t maximumFrames = 16);
    const std::string& pickingStatus() const;
    void render(const RenderScene& scene);
    void render(const SceneSettings& settings, const MaterialTextures& textures = {});
    void setAppearance(const ViewportAppearance&);
    void setGridVisible(bool visible);
    // Consumes the scene immediately; the pinned owner survives asynchronous readback.
    bool renderThumbnail(const std::string& id,std::uint64_t signature,const RenderScene&,
        std::uint16_t size = 512,std::shared_ptr<const void> pinned = {});
    bool thumbnailBusy() const;
    std::optional<ThumbnailResult> pollThumbnail(std::uint32_t bgfxFrame);
    bool setThumbnail(const std::string& id,std::uint64_t signature,const DecodedImage&);
    bgfx::TextureHandle thumbnailTexture(const std::string& id,std::uint64_t signature = 0) const;
    void evictThumbnail(const std::string&);
    std::size_t thumbnailResidentBytes() const;
    void trimThumbnailCache(std::size_t targetBytes);
    const std::string& thumbnailStatus() const;
    // Every candidate is created before any live program is replaced.
    bool reloadPrograms(std::span<const RendererShaderPair>,std::span<const std::uint8_t> proceduralCompute={});
    const std::string& shaderStatus() const;
    std::uint64_t shaderSignature() const;
    bgfx::TextureHandle viewportTexture() const noexcept;
    std::uint32_t width() const noexcept;
    std::uint32_t height() const noexcept;
    bool drawable() const noexcept;
    const char* backendName() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace crimson
