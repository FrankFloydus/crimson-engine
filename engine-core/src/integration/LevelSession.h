#pragma once

#include "crimson-renderer/RenderFrame.h"
#include "scene/SceneDocument.h"
#include "assets/MaterialPreviewGeometry.h"
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
class QuaderBridge;
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

// Engine level ownership composed inside Quader's existing application. Quader
// retains its modeling document; a level stores the engine's schema-25 actors,
// physics, lights and asset references and links accepted modeling publications.
// This owner creates no application, window, native input pump or GPU device.
class LevelSession final {
public:
    explicit LevelSession(std::filesystem::path projectRoot,std::filesystem::path runtimeRoot={});
    ~LevelSession();
    LevelSession(const LevelSession&)=delete;
    LevelSession& operator=(const LevelSession&)=delete;
    AssetLibrary& assets();
    ResourceManager& resources();
    QuaderBridge& modelingBridge();
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

    // Binding is read-only until an explicit save. Dirty levels reject a
    // document switch so the native shell can run its usual unsaved prompt.
    bool bindDocument(std::string stableDocumentKey,bool preserveInitialLevel=false);
    std::optional<Camera> editorCamera()const;
    void rememberEditorCamera(const Camera&);
    bool saveEditorState();
    std::vector<std::filesystem::path> takeFileChanges();
    bool newLevel(bool discardDirty=false);
    bool openLevel(const std::string& assetId,bool discardDirty=false);
    bool save(const std::string& name={},bool saveAs=false);
    bool undo();
    bool redo();
    EntityId linkModelingScene(const std::filesystem::path& manifest,
        const std::string& projectId,const std::string& sceneId);
    void synchronizeModelingScene(EntityId owner);
    bool dropAsset(const std::string& assetId,Vec2 texturePixel,const Camera&,Vec2 textureSize);
    bool dropAssets(std::span<const std::string>,Vec2 texturePixel,const Camera&,Vec2 textureSize);
    bool selectViewport(Vec2 texturePixel,const Camera&,Vec2 textureSize,bool toggle=false,bool append=false);
    bool requestViewportContext(Vec2 texturePixel,const Camera&,Vec2 textureSize);
    std::optional<LevelViewportContext> takeViewportContext();
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
