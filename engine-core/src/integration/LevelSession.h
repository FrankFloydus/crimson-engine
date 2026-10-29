#pragma once

#include "crimson-renderer/RenderFrame.h"
#include "scene/SceneDocument.h"
#include "assets/MaterialPreviewGeometry.h"
#include "ModelingWorldSnapshot.h"
#include "native_scene_projection.hpp"
#include "native_scene_render_bindings.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <functional>
#include <span>

namespace crimson {
class AssetLibrary;
class GameInput;
class PlaySession;
class ResourceManager;
struct VertexPaintTargets;
struct VertexPaintSurfaceSnapshot;
struct MaterialAsset;
struct DecodedImage;
struct ModelPose;
struct ParticlePresentation;
struct DecalAsset;
namespace shared { class Renderer; }
namespace integration {

struct LevelViewRequest {
    Camera sceneCamera;
    std::uint32_t sceneWidth{},sceneHeight{},gameWidth{},gameHeight{};
    bool sceneVisible{},gameVisible{},gridVisible{true};
    AntiAliasing antiAliasing{AntiAliasing::Smaa};
    ViewportAppearance appearance;
    std::uint16_t shadowResolution{1024};
};
struct LevelViewportContext {
    Camera camera;
    std::optional<Vec3> position;
    EntityId entity{};
    std::uint64_t document{},generation{},revision{};
};
enum class LevelModelPreviewKind {Ragdoll,Animation};

// Runtime/resources composed inside the existing native application. The native
// scene is the only authored world; this owner keeps a disposable typed projection
// and an isolated Play clone. It has no scene persistence or authored history.
// This owner creates no application, window, native input pump or GPU device.
class LevelSession final {
public:
    explicit LevelSession(std::filesystem::path projectRoot,std::filesystem::path runtimeRoot={});
    ~LevelSession();
    LevelSession(const LevelSession&)=delete;
    LevelSession& operator=(const LevelSession&)=delete;
    AssetLibrary& assets();
    ResourceManager& resources();
    void setModelingWorld(std::shared_ptr<const ModelingWorldSnapshot>,bool current=true);
    void setSceneLighting(bool);
    SceneDocument& document();
    const SceneDocument& document() const;
    SceneDocument& editingDocument();
    const Scene& presentationScene() const;
    GameInput& gameInput();
    PlaySession& runtime();
    const PlaySession& runtime() const;
    const std::filesystem::path& projectRoot() const;
    const std::string& status() const;
    std::uint64_t revision() const;

    bool publishNativeScene(const quader::SceneDocument&,std::string sceneUuid,std::uint64_t sourceRevision);
    const NativeSceneProjection& nativeProjection()const;
    std::shared_ptr<const NativeSceneRenderBindings> nativeRenderBindings();
    std::vector<std::filesystem::path> takeFileChanges();
    bool dropAsset(const std::string& assetId,Vec2 texturePixel,const Camera&,Vec2 textureSize);
    bool dropAssets(std::span<const std::string>,Vec2 texturePixel,const Camera&,Vec2 textureSize);
    bool selectViewport(Vec2 texturePixel,const Camera&,Vec2 textureSize,bool toggle=false,bool append=false);
    bool requestViewportContext(Vec2 texturePixel,const Camera&,Vec2 textureSize);
    std::optional<LevelViewportContext> takeViewportContext();
    void setViewportPickRequester(std::function<bool(const PickRequest&)>);
    bool viewportRequestPending()const;
    void cancelViewportRequest();
    void armDecalPlacement(EntityId);
    void replayParticle(EntityId);
    VertexPaintTargets vertexPaintTargets(const SceneDocument&,EntityId);
    VertexPaintSurfaceSnapshot vertexPaintSurfaces(const SceneDocument&);
    bool meshPaintSubdivision(const EntityRecord&,std::uint32_t level,SceneMeshPaint&,std::string& diagnostic);
    void setVertexPaintState(bool enabled,bool surfaceHeld,bool showSubdivisions);
    std::string& navigationProfile();
    std::string navigationStatus(const std::string&)const;
    void setNavigationVisible(bool);
    void focusSelection(Camera&,float aspect,bool preserveLens=false);
    void setAssetPreviewPublisher(std::function<void(const std::string&,const DecodedImage&)>,std::function<bool(const std::string&)> imageAvailable={});
    void pumpAssetPreviews(std::uint32_t rendererFrame,std::span<const std::string> visibleIds,std::uint32_t pixelEdge=128);
    bool recordModelDocumentPreview(LevelModelPreviewKind,const std::string& model,const std::shared_ptr<const ModelPose>&,
        const Mat4& world,const SceneSettings&,std::uint32_t width,std::uint32_t height);
    bool recordParticleDocumentPreview(const std::string& material,std::span<const ParticlePresentation>,const SceneSettings&,
        double acceptedSeconds,std::uint32_t width,std::uint32_t height);
    bool recordDecalDocumentPreview(const DecalAsset&,std::uint64_t previewRevision,MaterialPreviewMesh,
        const SceneSettings&,std::uint32_t width,std::uint32_t height);
    void clearDecalDocumentPreview();

    bool play();
    bool playFromHere(const Vec3&,const Camera&);
    void stop();
    bool setPaused(bool);
    bool step();
    bool playing() const;
    bool playPending() const;
    bool paused() const;
    void cancelInput();
    void update(double seconds,bool active,std::uint32_t rendererFrame=0);

    // Borrow the single renderer supplied by Quader's RenderHost. Frozen frame
    // leases remain valid through submission; detach before that host retires.
    void attachRenderer(shared::Renderer&);
    void detachRenderer();
    std::shared_ptr<const PreparedRenderFrame> prepareRender(const LevelViewRequest&);
    const std::shared_ptr<const PreparedRenderFrame>& lastPreparedFrame()const;
    bool materialPreparationReady()const;
    bool recordMaterialPreview(const MaterialAsset&,const SceneSettings&,MaterialPreviewMesh,
        const Mat4& objectTransform,std::uint32_t width,std::uint32_t height,bool showEnvironment,
        bool environmentPublishedExternally=false,std::optional<MaterialGraphPreviewTarget> nodePreview={});
    void clearMaterialPreview();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace integration
} // namespace crimson
