#pragma once

#include "RenderTypes.h"
#include "CollisionOverlayTypes.h"
#include "ParticleRenderTypes.h"
#include "MaterialGraphGpu.h"
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
struct PreparedRenderFrame;

struct MaterialTextures {
    bgfx::TextureHandle albedo{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle roughness{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle metallic{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle normal{BGFX_INVALID_HANDLE};
};

// Base color, packed metallic/roughness, normal, occlusion, emissive.
// Textures are borrowed from AssetLibrary; sampler overrides belong to each slot.
struct ModelMaterialTextures {
    std::shared_ptr<const PreparedGraphMaterial> graph;
    std::array<bgfx::TextureHandle, 5> handles{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<std::uint32_t, 5> flags{{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}};
    std::array<std::shared_ptr<const void>,5> leases;
    std::array<std::uint64_t,5> sourceFingerprints{},signatures{};
    bool complete{true};
    bool sourceReady{true};
    bool graphPending{};
    std::string diagnostic;
    // A coherent coarse mip can be drawn before the requested detail is complete.
    bool drawable() const noexcept {
        if(graph)for(const auto& source:graph->sources)if(!bgfx::isValid(graph->textures[source.slot]))return false;
        for(std::size_t slot=0;slot<handles.size();++slot)
            if(flags[slot]!=UINT32_MAX&&!bgfx::isValid(handles[slot]))return false;
        return true;
    }
};

// Geometry is a transient preview choice, independent of material authorship.
inline bool usesParticleMaterialPreview(MaterialPreviewMesh mesh,const ModelMaterialTextures& bindings) {
    return mesh==MaterialPreviewMesh::Sprite && !(bindings.graph && bindings.graph->nodePreview);
}

// Prepared renderer values. Fallback coverage is explicit; an RGB texture never
// supplies opacity implicitly. Complete graph packets own all authored behavior.
struct RenderParticleMaterial {
    std::shared_ptr<const PreparedGraphMaterial> graph;
    Vec4 baseColor{1,1,1,1}; // Linear factors, including opacity.
    MaterialBlend blend{MaterialBlend::Additive};
    MaterialShading shading{MaterialShading::Unlit};
    float roughness{1},metallic{},specular{.5f},occlusion{1},alphaCutoff{.5f};
    Vec3 emission{};
    bool doubleSided{};
    bgfx::TextureHandle colorTexture{BGFX_INVALID_HANDLE};
    std::uint32_t samplerFlags{UINT32_MAX};
    std::shared_ptr<const void> pinned; // Material definition and texture leases.
    Vec3 uvScale{1,1,1},uvOffset{},uvRotation{};
    bool triplanar{false},triplanarWorld{false};
    bool receiveFog{true};
};

inline constexpr std::size_t MaxDirectionalLights = 4;
inline constexpr std::size_t MaxPointShadowLights = 16;

struct ModelPose;
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
    SurfaceLightingScopes lightingScopes{};
    std::uint32_t lightingCategory{lighting_scope::World};
    std::shared_ptr<const ModelPose> pose;
    float opacity{1.f}; // Camera-local presentation only; shared shadow casters retain full opacity.
};

enum class PickMode { Selection, MaterialSlot };
struct PickRequest {
    std::uint64_t generation{}, revision{}, serial{};
    std::uint32_t x{}, y{}; // Top-left viewport texture pixel, independent of desktop DPI.
    std::uint64_t resourceSignature{};
    PickMode mode{PickMode::Selection};
};
struct PickResult {
    std::uint64_t generation{}, revision{}, serial{}, entityId{}; // Zero means background.
    std::uint64_t resourceSignature{};
    std::uint32_t materialSlot{UINT32_MAX};
    PickMode mode{PickMode::Selection};
};

struct ThumbnailResult {
    std::string id;
    std::uint64_t signature{};
    DecodedImage image;
};
enum class RendererProgram { Sphere, Model, Picking, Present, Grid, Encode, Particle, Sky, PointShadow, GlowExtract, GlowBlur, DirectionalShadow, CollisionOverlay=17, OccluderComposite=18 };
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
    std::span<const RenderParticleMaterial> particleMaterials; // Empty uses a neutral additive fallback.
    // Complete light-range caster set, independent of Scene/Game camera culling.
    // A zero world ID explicitly disables shadows for isolated material previews.
    std::span<const RenderModelInstance> shadowInstances;
    std::uint64_t shadowWorldId{};
    std::uint16_t shadowResolution{1024};
    MaterialEvaluationContext materialContext{};
    // The reference path evaluates every local light from the same GPU records.
    // Used by hidden correctness/performance checks; ordinary views use clusters.
    LocalLightMode localLightMode{LocalLightMode::Clustered};
    // The designated player's model subtree; copied by the prepared world.
    // Only Game separates these casters into its close directional map.
    std::span<const std::uint64_t> shadowFocusEntities;
    Aabb shadowFocusBounds{};
    std::optional<Vec3> shadowPriorityOrigin;
    // Frozen world metadata, including non-casting receivers. Empty direct
    // submissions derive bounds from their camera-local instances instead.
    std::span<const Aabb> shadowReceiverBounds;
    PlayerRevealSource playerReveal{};
    std::array<CharacterLight,2> characterLights{};
    CharacterShadowSource characterShadow{};
    std::span<const RenderCollisionOverlay> collisionOverlays;
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
    // Consumes one frozen active-world frame. Retains its prepared versions and
    // value storage through the same queued-frame retirement as material leases.
    void renderFrame(std::shared_ptr<const PreparedRenderFrame>);
    bgfx::TextureHandle gameViewportTexture() const noexcept;
    void releaseGameViewport();
    const ParticleRenderStats& gameParticleStats() const noexcept;
    // Independent reusable preview; submission shares the application's one
    // bgfx frame and never modifies the main viewport targets or picking.
    void resizeParticlePreview(std::uint32_t width, std::uint32_t height);
    void resizeMaterialPreview(std::uint32_t width,std::uint32_t height);
    void renderMaterialPreview(const SceneSettings&,const MaterialAsset&,const ModelMaterialTextures&,
        MaterialPreviewMesh,const Mat4& objectTransform,bool showEnvironment,std::shared_ptr<const void> pins={},MaterialEvaluationContext context={});
    bgfx::TextureHandle materialPreviewTexture() const noexcept;
    void releaseMaterialPreview();
    bool publishMaterialPreviewEnvironment(const PreparedEnvironment&);
    void renderParticlePreview(const SceneSettings&, std::span<const RenderParticle>,
                               std::span<const RenderParticleMaterial> = {},MaterialEvaluationContext context={});
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
    const ClusteredLightingStats& clusteredLightingStats() const;
    const std::string& clusteredLightingStatus() const;
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
    std::uint64_t pickResourceSignature(PickMode) const;
    void setExternalResourceSignature(std::uint64_t);
    std::size_t modelResidentBytes() const;
    std::uint64_t collisionOverlayUploads() const;
    std::size_t occluderLayerResidentBytes() const;
    void trimModelCache(std::size_t targetBytes,std::span<const std::string> liveIds);
    void clearModel();
    const std::string& modelStatus() const;
    // Selection shares visible alpha coverage. Material-slot targeting uses
    // neutral geometry coverage so a temporary material preview cannot change
    // its target. Both modes retain geometry and scene/interaction stale guards.
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
    bool reloadPrograms(std::span<const RendererShaderPair>,std::span<const std::uint8_t> proceduralCompute={},
        std::span<const std::uint8_t> clusterCompute={},std::span<const std::uint8_t> ambientEvaluate={},
        std::span<const std::uint8_t> ambientPublish={},std::span<const std::uint8_t> deformationCompute={},
        std::span<const std::uint8_t> occluderDepthCompute={});
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
