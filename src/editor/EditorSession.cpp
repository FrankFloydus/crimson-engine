#include "EditorSession.h"
#include "input/Navigation.h"
#include "scene/SceneExtraction.h"
#include "assets/AtomicFile.h"
#include "math/Json.h"
#include "AssetPreviews.h"
#include "assets/MaterialAsset.h"
#include "crimson-renderer/Environment.h"
#include "resources/Paths.h"
#include "tools/SceneDiagnostics.h"
#include <imgui.h>
#include <SDL3/SDL_keyboard.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <exception>

namespace crimson {
using Json=nlohmann::json;
namespace {
bool visibleBounds(const Aabb& local,const Mat4& world,const Camera& camera,float aspect) {
    const auto bounds=math::transformBounds(local,world);if(!bounds.valid)return true;
    const auto depth=bgfx::getCaps()->homogeneousDepth?ClipDepth::NegativeOneToOne:ClipDepth::ZeroToOne;
    const auto projection=camera.projectionMatrix(aspect,depth)*camera.viewMatrix();
    std::array<bool,6> outside{true,true,true,true,true,true};
    for(unsigned corner=0;corner<8;++corner) {
        const Vec4 point{corner&1?bounds.maximum[0]:bounds.minimum[0],corner&2?bounds.maximum[1]:bounds.minimum[1],corner&4?bounds.maximum[2]:bounds.minimum[2],1};
        const auto clip=math::transformHomogeneous(projection,point);if(!math::finite(clip))return true;
        outside[0]&=clip[0]<-clip[3];outside[1]&=clip[0]>clip[3];outside[2]&=clip[1]<-clip[3];outside[3]&=clip[1]>clip[3];
        outside[4]&=clip[2]<(depth==ClipDepth::ZeroToOne?0:-clip[3]);outside[5]&=clip[2]>clip[3];
    }
    return std::none_of(outside.begin(),outside.end(),[](bool value){return value;});
}
}
EditorSession::EditorSession(const std::filesystem::path& root, AssetLibrary& assets, Renderer& renderer)
    : root_(root),document_(root),play_(root),particleEditor_(root),materialEditor_(root),resources_(*assets.resources()),prepared_(assets,renderer,resources_,root) {
    restoreSession(assets);
    documentEpoch_=document_.documentGeneration();
    previews_=std::make_unique<AssetPreviews>(root,assets,resources_,renderer,prepared_);
    preparedEnvironment_="<unprepared>";
    editor_.setParticleBoundsQuery([this](EntityId id){return play_.playing()?play_.particles().bounds(id):particles_.bounds(id);});
    gameInput_.setDesktopCoordinates(true);
    play_.setParticleDefinitions([&assets](const std::string& id) {
        return ParticleDefinitionVersion{assets.findParticle(id),assets.particleRevision(id),assets.particleDiagnostic(id)};
    });
}
EditorSession::~EditorSession() { saveSession(); }
void EditorSession::restoreSession(AssetLibrary& assets) {
    const auto path=root_/"assets"/"editor-session.json";
    try {
        if (std::filesystem::exists(path)) {
            std::ifstream input(path); const auto saved=Json::parse(input);
            if (saved.at("version") != 1) throw std::runtime_error("Unsupported editor session version.");
            recoveredLegacy_=saved.value("recoveredLegacyPreview",false);
            if(saved.contains("panels")) { const auto& panels=saved.at("panels");editor_.setPanels({panels.value("outliner",true),panels.value("inspector",true),panels.value("assetBrowser",true),panels.value("game",false)}); }
            const auto active=saved.value("scene",std::string{});
            if (!active.empty() && !document_.load(active)) { status_=document_.status(); unresolvedScene_=active; }
            if (saved.contains("cameras")) for (auto it=saved["cameras"].begin();it!=saved["cameras"].end();++it) {
                Camera camera;
                camera.position=it.value().at("position").get<Vec3>(); camera.target=it.value().at("target").get<Vec3>();
                camera.up=it.value().at("up").get<Vec3>(); camera.fov=it.value().value("fov",45.0f);
                camera.nearPlane=it.value().value("near",.01f); camera.farPlane=it.value().value("far",2048.0f);
                if (math::finite(camera.position) && math::finite(camera.target) && math::finite(camera.up) &&
                    math::finite(camera.fov) && math::finite(camera.nearPlane) && math::finite(camera.farPlane) &&
                    camera.fov > 0 && camera.fov < 180 && camera.nearPlane > 0 && camera.farPlane > camera.nearPlane)
                    cameras_.emplace(it.key(),camera);
            }
            cameraScene_=document_.assetId();
            if (const auto found=cameras_.find(cameraScene_);found!=cameras_.end()) settings_.camera=found->second;
        }
    } catch (const std::exception& error) { status_=error.what(); }
    startupStatus_=status_;
    if (!recoveredLegacy_ && document_.assetId().empty() && unresolvedScene_.empty() && !std::filesystem::exists(path)) {
        recoveredLegacy_=true;
        const auto preview=assets.loadPreview();
        if (!preview.empty()) {
            const auto model=assets.findModel(preview);
            document_.createModel(preview,model ? model->name : "Recovered model");
            const auto environment=document_.createEnvironment("Recovered environment");
            if(auto record=document_.scene().find(environment)) { assets.loadEnvironment(record->environment->environment);document_.updateEntity(environment,*record,"Recover preview environment"); }
            const SceneSettings defaults;
            const auto directional=document_.createDirectionalLight("Recovered directional light");
            if (auto light=document_.scene().find(directional)) {
                light->directional=SceneDirectionalLight{defaults.directional.color,defaults.directional.intensity,defaults.directional.enabled};
                light->local=math::inverse(math::lookAt({0,0,0},defaults.directional.direction)).value_or(Mat4{});
                document_.updateEntity(directional,*light,"Recover light");
            }
            for (const auto& source : defaults.points) {
                const auto id=document_.createPointLight("Recovered point light");
                if (auto light=document_.scene().find(id)) {
                    light->point=ScenePointLight{source.color,source.intensity,source.enabled,source.radius};
                    light->local=math::translation(source.position); document_.updateEntity(id,*light,"Recover light");
                }
            }
            document_.select({});
            if (model && model->bounds.valid) settings_.camera.frameBounds(model->bounds.minimum,model->bounds.maximum,1.5f);
        }
    }
}
void EditorSession::saveSession() noexcept {
    try {
        if (!document_.assetId().empty()) cameras_[document_.assetId()]=play_.playing()?editCameraBeforePlay_:settings_.camera;
        Json cameras=Json::object();
        for (const auto& [id,camera] : cameras_) cameras[id]={{"position",camera.position},{"target",camera.target},{"up",camera.up},
            {"fov",camera.fov},{"near",camera.nearPlane},{"far",camera.farPlane}};
        writeFileAtomically(root_/"assets"/"editor-session.json",Json{{"version",1},{"scene",unresolvedScene_.empty() ? document_.assetId() : unresolvedScene_},
            {"recoveredLegacyPreview",recoveredLegacy_},{"cameras",std::move(cameras)},
            {"panels",{{"outliner",editor_.panels().outliner},{"inspector",editor_.panels().inspector},{"assetBrowser",editor_.panels().assetBrowser},{"game",editor_.panels().game}}}}.dump(2));
    } catch (...) { /* Session state is expendable; authored scenes were saved explicitly. */ }
}
void EditorSession::filesChanged(std::span<const std::filesystem::path> paths) {
    particleEditor_.filesChanged(paths);
    materialEditor_.filesChanged(paths);
    changedFiles_.insert(changedFiles_.end(),paths.begin(),paths.end());
    for(const auto& path:paths) {
        const auto relative=path.lexically_relative(root_).generic_u8string();
        previews_->invalidate({reinterpret_cast<const char*>(relative.data()),relative.size()});
    }
}
void EditorSession::prepareFrame(AssetLibrary& assets,Renderer& renderer,std::uint32_t bgfxFrame) {
    prepared_.beginFrame(); renderer.advanceStreaming();renderer.setAppearance(editor_.appearance());
    for(const auto& file:changedFiles_) {
        const auto relative=file.lexically_relative(root_).generic_u8string(); const std::string id(reinterpret_cast<const char*>(relative.data()),relative.size());
        if(id==document_.assetId() && std::filesystem::is_regular_file(file)) {
            if(play_.playing()) { deferredSceneReload_=true;continue; }
            ResourceRequestOptions options;options.dependencies={file};options.forceReload=true;
            const auto root=root_;
            externalScene_=resources_.request<SceneSnapshot>(AssetRef<SceneSnapshot>{id},[root,id](std::stop_token token){
                if(token.stop_requested())throw std::runtime_error("Scene reload canceled.");
                auto snapshot=std::make_shared<SceneSnapshot>(readSceneSnapshot(root,id));
                return ResourceLoadResult<SceneSnapshot>{snapshot,sizeof(SceneSnapshot)+snapshot->records.size()*sizeof(EntityRecord),snapshot->fingerprint,{}};
            },std::move(options));
        }
    }
    changedFiles_.clear();
    if(!play_.playing() && externalScene_.state()==ResourceState::Ready) if(auto snapshot=externalScene_.snapshot()) {
        if(snapshot->id==document_.assetId() && snapshot->fingerprint!=notifiedSceneFingerprint_ && document_.externalFileChanged()) {
            notifiedSceneFingerprint_=snapshot->fingerprint;editor_.notifyExternalSceneChange(*snapshot);
        }
    }
    const auto environment=resolveEnvironment(activeDocument().scene());
    const auto map=assets.references().resolveEnvironment(environment.environment.map);
    if(preparedEnvironment_!=map || !environment_) {
        preparedEnvironment_=map; environmentRevision_=0;
        ResourceRequestOptions options;options.variant="ibl";
        if(!map.empty())options.dependencies={root_/pathFromUtf8(map)};
        const auto root=root_;
        environment_=resources_.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{map.empty()?StudioEnvironmentId:map},[root,map](std::stop_token token){
            if(token.stop_requested())throw std::runtime_error("Environment preparation canceled.");
            auto data=prepareEnvironmentCpu(root,map);
            std::size_t bytes=data->data.diffuse.rgba.size()*sizeof(float)+data->data.brdf.size()*sizeof(float);
            for(const auto& level:data->data.specular)bytes+=level.rgba.size()*sizeof(float);
            return ResourceLoadResult<PreparedEnvironment>{data,bytes,data->key,{}};
        },std::move(options));
    }
    if(auto snapshot=environment_.snapshot()) if(snapshot.revision()!=environmentRevision_ && renderer.publishEnvironment(*snapshot)) environmentRevision_=snapshot.revision();
    previews_->pump(bgfxFrame,visiblePreviewIds_);
}
void EditorSession::cancelInteraction(Renderer& renderer) {
    editor_.cancelInteraction(activeDocument(),renderer);particleEditor_.cancelInteraction();materialEditor_.cancelInteraction();gameInput_.cancel();focusGame_=false;skipSimulationTime_=true;
}
void EditorSession::transitionPlay(Renderer& renderer,Navigation& navigation,bool stop) {
    editor_.cancelInteraction(activeDocument(),renderer);navigation.cancel();gameInput_.cancel();
    pendingDrops_.clear();renderer.cancelPicking();skipSimulationTime_=true;playStatus_.clear();
    if(play_.playing()) {
        play_.stop();settings_.camera=editCameraBeforePlay_;
        if(deferredSceneReload_ && !document_.assetId().empty())changedFiles_.push_back(root_/pathFromUtf8(document_.assetId()));
        deferredSceneReload_=false;
    } else if(!stop) {
        editCameraBeforePlay_=settings_.camera;
        if(play_.start(document_)) { editor_.showGame();focusGame_=true; }
        else playStatus_=play_.status();
    }
    documentEpoch_=activeDocument().documentGeneration();
}
void EditorSession::frame(AssetLibrary& assets, Renderer& renderer, Navigation& navigation, SDL_Window* window,
                          uint32_t bgfxFrame, bool quitRequested) {
    const bool forceStop=editor_.needsAuthoringDocument() || quitRequested || (play_.playing() && !editor_.panels().game);
    if(togglePlayPending_ || (forceStop && play_.playing()))transitionPlay(renderer,navigation,forceStop);
    togglePlayPending_=false;
    auto& document=activeDocument();
    const auto previousAsset=document.assetId();
    const float simulationSeconds=simulationSuspended_ || skipSimulationTime_?0.f:ImGui::GetIO().DeltaTime;
    skipSimulationTime_=false;
    const auto previousCamera=settings_.camera;
    document.setPreviewCamera(settings_.camera);
    const auto frame=editor_.draw(document,settings_,assets,renderer,window,navigation.active(),bgfxFrame,quitRequested,particleEditor_,materialEditor_);
    togglePlayPending_=frame.togglePlay;
    if(frame.createParticle) particleEditor_.requestCreate(frame.particleFolder,assets);
    if(!frame.openParticle.empty()) particleEditor_.requestOpen(frame.openParticle,assets);
    if(frame.createMaterial)materialEditor_.requestCreate(frame.materialFolder,assets);
    if(!frame.openMaterial.empty())materialEditor_.requestOpen(frame.openMaterial,assets);
    particleEditor_.draw(assets,renderer,simulationSeconds,materialEditor_);
    materialEditor_.draw(assets,renderer,resources_,simulationSeconds);
    status_=startupStatus_+playStatus_;
    if (!play_.playing() && (frame.documentReplaced || document.assetId() != cameraScene_ || document.documentGeneration()!=documentEpoch_)) {
        if (!previousAsset.empty()) cameras_[previousAsset]=previousCamera;
        cameraScene_=document.assetId(); navigation.cancel(); renderer.cancelPicking();
        unresolvedScene_.clear(); startupStatus_.clear(); status_.clear();
        if(document.documentGeneration()!=documentEpoch_) { documentEpoch_=document.documentGeneration();pendingDrops_.clear(); }
        externalScene_={};savedPreviewFingerprint_=0;
        if (frame.documentReplaced) {
            settings_.camera=Camera{};
            if (const auto found=cameras_.find(cameraScene_);found!=cameras_.end()) settings_.camera=found->second;
        }
        saveSession();
    }
    auto dropped=frame.modelDropIds;
    if (!frame.modelDropId.empty()) dropped.push_back(frame.modelDropId);
    const auto placement=[&](const Vec2& mouse) {
        auto position=settings_.camera.target;position[1]=0;
        if(const auto ray=settings_.camera.screenRay(mouse[0],mouse[1],frame.viewport))
            if(const auto hit=math::intersectPlane(*ray,{0,0,0},{0,1,0}))position=*hit;
        return position;
    };
    if(!dropped.empty()) pendingDrops_.push_back({std::move(dropped),placement(frame.modelDropPosition),documentEpoch_});
    if(!frame.particleDropId.empty() && assets.findParticle(frame.particleDropId))
        document.createParticle(frame.particleDropId,assets.displayName(frame.particleDropId),math::translation(placement(frame.particleDropPosition)));
    bool gameOpen=editor_.panels().game;
    const auto game=gameView_.draw(document,renderer,gameOpen,play_.playing(),focusGame_);
    if(!gameOpen) { editor_.hideGame();renderer.releaseGameViewport();if(play_.playing())togglePlayPending_=true; }
    auto gameCamera=resolveGameCamera(document.scene());
    auto* gameWindow=game.viewport.windowId?SDL_GetWindowFromID(game.viewport.windowId):nullptr;
    // ImGui focus selects the dock/tab; a detached Game also needs native
    // keyboard focus. Its platform window may only exist on the next frame.
    if(focusGame_ && gameWindow && SDL_GetKeyboardFocus()!=gameWindow)SDL_RaiseWindow(gameWindow);
    gameInput_.setViewport(game.viewport,play_.playing() && gameWindow && game.pixelWidth>0 && gameCamera.has_value(),game.viewport.inputBlocked || simulationSuspended_);
    if(!game.focused)gameInput_.cancel();
    else if(game.clicked || focusGame_)gameInput_.acquire();
    if(game.pointerPosition)gameInput_.submitUiPointer(*game.pointerPosition,game.clicked,game.leftHeld);
    const bool focusComplete=game.focused && gameWindow && SDL_GetKeyboardFocus()==gameWindow &&
        (!gameCamera || gameInput_.ownsKeyboard());
    focusGame_=focusGame_ && gameOpen && play_.playing() && !focusComplete;
    if(gameInput_.ownsKeyboard())navigation.cancel();
    auto sceneRegion=frame.viewport;sceneRegion.inputBlocked|=gameInput_.ownsKeyboard();
    navigation.update(settings_.camera,sceneRegion,ImGui::GetIO().DeltaTime,false);
    const auto environment=resolveEnvironment(document.scene());settings_.environment=environment.environment;settings_.exposure=environment.exposure;
    settings_.environment.map=assets.references().resolveEnvironment(settings_.environment.map);
    for(auto iterator=pendingDrops_.begin();iterator!=pendingDrops_.end();) {
        if(iterator->documentEpoch!=documentEpoch_) { iterator=pendingDrops_.erase(iterator);continue; }
        std::vector<ModelPlacement> placements;bool waiting=false;
        for(const auto& id:iterator->ids) {
            if(assets.references().resolve(id).empty()) { status_+="Dropped model was deleted: "+id+"\n";continue; }
            const auto prepared=prepared_.prepare(id,math::translation(iterator->position),settings_.camera,renderer.height(),false,ResourcePriority::Placement,
                std::max(1.f,frame.viewport.width)/std::max(1.f,frame.viewport.height));
            if(prepared && prepared->ready) placements.push_back({id,assets.displayName(id),math::translation(iterator->position)});
            else if(!prepared_.failed(id)) waiting=true;
        }
        if(waiting) { ++iterator;continue; }
        document.createModels(placements);renderer.cancelPicking();iterator=pendingDrops_.erase(iterator);
    }
    if(play_.playing()) {
        // Mouse coordinates refer to the Game image already presented to the
        // user, including its interpolated follow camera.
        const auto inputCamera=resolveGameCamera(play_.presentationScene());
        play_.advance(simulationSeconds,gameInput_,inputCamera.value_or(gameCamera.value_or(Camera{})),!simulationSuspended_);
    }
    const auto& renderScene=play_.playing()?play_.presentationScene():document.scene();
    const auto extracted=extractScene(renderScene);
    gameCamera=resolveGameCamera(renderScene);
    auto& sceneParticles=play_.playing()?play_.particles():particles_;
    if(!play_.playing() || frame.playParticleEntity)sceneParticles.reconcile(document.documentGeneration(),extracted.particles,[&](const std::string& id) {
        return ParticleDefinitionVersion{assets.findParticle(id),assets.particleRevision(id),assets.particleDiagnostic(id)};
    },play_.playing()?0.f:simulationSeconds,frame.playParticleEntity);
    particlePackets_.clear();particlePackets_.reserve(sceneParticles.presentation().size());
    particleMaterials_.clear();particleMaterials_.push_back({});
    std::unordered_map<std::string,std::uint32_t> materialIndices;
    std::vector<std::shared_ptr<const PreparedParticleMaterial>> particlePins;
    const auto presentation=sceneParticles.presentation();
    for(const auto& group:sceneParticles.groups()) {
        try {
            const auto id=assets.references().resolveParticleMaterial(group.materialId);
            auto found=materialIndices.find(id);
            if(found==materialIndices.end()) {
                const auto material=assets.preparedParticleMaterial(group.materialId);
                if(!material || !material->ready) {
                    status_+="Particle material "+group.materialId+": "+(material?material->diagnostic:"unavailable")+"\n";
                    materialIndices.emplace(id,UINT32_MAX);continue;
                }
                if(!material->diagnostic.empty())status_+=material->diagnostic+"\n";
                const auto index=static_cast<std::uint32_t>(particleMaterials_.size());
                particleMaterials_.push_back(material->render);particlePins.push_back(material);
                found=materialIndices.emplace(id,index).first;
            }
            if(found->second==UINT32_MAX)continue;
            for(std::size_t offset=group.first;offset<group.first+group.count;++offset) {
                const auto& p=presentation[offset];particlePackets_.push_back({p.position,p.size,p.color,found->second});
            }
        } catch(const std::exception& error) {
            status_+="Particle material "+group.materialId+": "+error.what()+"\n";
        }
    }
    std::vector<RenderModelInstance> instances;
    struct FramePins {
        std::vector<std::shared_ptr<const PreparedEditorModel>> models;
        std::vector<std::shared_ptr<const PreparedParticleMaterial>> particles;
        std::shared_ptr<const PreparedEditorShadows> shadows;
    };
    auto pins=std::make_shared<FramePins>();pins->particles=std::move(particlePins);
    // Prepare from the full interpolated world before either camera culls its
    // receivers. Scene and Game submit the same pinned caster set this frame.
    pins->shadows=prepared_.prepareShadows(extracted,editor_.pointShadowResolution());
    std::uint64_t signature=14695981039346656037ull;
    for(const auto& material:pins->particles)signature=(signature^material->signature)*1099511628211ull;
    for(const auto& model:extracted.models) {
        const bool selected=std::find(document.selection().ids.begin(),document.selection().ids.end(),model.id)!=document.selection().ids.end();
        if(!selected) if(const auto metadata=assets.findModel(model.assetId))
            if(!visibleBounds(metadata->bounds,model.world,settings_.camera,std::max(1.0f,frame.viewport.width)/std::max(1.0f,frame.viewport.height)))continue;
        if(auto prepared=prepared_.prepare(model.assetId,model.world,settings_.camera,renderer.height(),selected,ResourcePriority::Visible,
            std::max(1.f,frame.viewport.width)/std::max(1.f,frame.viewport.height),model.materialId)) {
            instances.push_back({model.id,{prepared->asset,model.world},&prepared->textures,prepared->revision,selected,true,&prepared->materials});
            instances.back().castShadows=model.castShadows;instances.back().receiveShadows=model.receiveShadows;
            pins->models.push_back(prepared);signature=(signature^prepared->signature)*1099511628211ull;
        }
    }
    for(const auto& id:frame.placementPreviewIds) if(auto prepared=prepared_.prepare(id,math::translation(placement(frame.placementPreviewPosition)),settings_.camera,
        renderer.height(),false,ResourcePriority::Placement,std::max(1.f,frame.viewport.width)/std::max(1.f,frame.viewport.height))) {
        instances.push_back({0,{prepared->asset,math::translation(placement(frame.placementPreviewPosition))},&prepared->textures,prepared->revision,false,false,&prepared->materials});
        instances.back().castShadows=false;instances.back().receiveShadows=false;pins->models.push_back(prepared);
    }
    renderer.setExternalResourceSignature(signature);
    renderer.setAppearance(editor_.appearance());
    if (frame.viewport.width > 0 && frame.viewport.height > 0)
        renderer.render(RenderScene{settings_,{},nullptr,nullptr,instances,extracted.directional,extracted.points,true,pins,particlePackets_,particleMaterials_,
            pins->shadows->instances,document.documentGeneration(),editor_.pointShadowResolution()});
    if(gameOpen && game.pixelWidth && game.pixelHeight && gameCamera) {
        SceneSettings gameSettings=settings_;gameSettings.camera=*gameCamera;
        const auto aspect=static_cast<float>(game.pixelWidth)/game.pixelHeight;
        std::vector<RenderModelInstance> gameInstances;
        auto gamePins=std::make_shared<FramePins>();gamePins->particles=pins->particles;gamePins->shadows=pins->shadows;
        for(const auto& model:extracted.models) {
            if(const auto metadata=assets.findModel(model.assetId))if(!visibleBounds(metadata->bounds,model.world,*gameCamera,aspect))continue;
            if(auto prepared=prepared_.prepare(model.assetId,model.world,*gameCamera,game.pixelHeight,false,ResourcePriority::Visible,aspect,model.materialId)) {
                gameInstances.push_back({model.id,{prepared->asset,model.world},&prepared->textures,prepared->revision,false,true,&prepared->materials});
                gameInstances.back().castShadows=model.castShadows;gameInstances.back().receiveShadows=model.receiveShadows;
                gamePins->models.push_back(prepared);
            }
        }
        renderer.renderGame(RenderScene{gameSettings,{},nullptr,nullptr,gameInstances,extracted.directional,extracted.points,true,gamePins,particlePackets_,particleMaterials_,
            pins->shadows->instances,document.documentGeneration(),editor_.pointShadowResolution()});
        gameInput_.setProjection(*gameCamera,game.pixelWidth,game.pixelHeight);
        if(play_.playing() && play_.lastClickedGround())gameView_.drawClickMarker(*gameCamera,*play_.lastClickedGround());
    }
    visiblePreviewIds_=frame.visiblePreviewIds;viewportRegion_=frame.viewport;
    if(!play_.playing() && !document.assetId().empty() && !document.dirty()) try {
        const auto fingerprint=sceneFileFingerprint(root_,document.assetId());
        if(fingerprint!=savedPreviewFingerprint_) { savedPreviewFingerprint_=fingerprint;previews_->requestSceneSave(document.assetId()); }
    } catch(const std::exception& error){status_=error.what();}
    prepared_.endFrame();
    const auto retained=renderer.modelResidentBytes()+renderer.thumbnailResidentBytes()+renderer.shadowResidentBytes();
    assets.trimTextureCache(retained>=resources_.config().gpuRetentionBytes?0:resources_.config().gpuRetentionBytes-retained);
    if(!prepared_.status().empty())status_+=prepared_.status();
    if(!sceneParticles.status().empty())status_+=sceneParticles.status();
    if(play_.playing()) {
        status_+=play_.status();
        if(!document.scene().properties().playerCharacter)status_+="No Player Character. Select a Character and choose Make Player.\n";
    }
    if(!renderer.particleStatus().empty())status_+=renderer.particleStatus();
    if(!renderer.shadowStatus().empty())status_+=renderer.shadowStatus();
    const auto& particleDraw=renderer.particleStats();
    if(frame.viewport.width>0 && (particleDraw.dropped || particleDraw.invalid))
        status_+="Particle rendering omitted "+std::to_string(particleDraw.dropped)+" packets; "+std::to_string(particleDraw.invalid)+" were invalid.\n";
    if(!previews_->status().empty())status_+=previews_->status();
    if(environment_.state()==ResourceState::Failed)status_+=environment_.diagnostic();
    if(!resourceStatus_.empty()) { if(!status_.empty())status_+='\n';status_+=resourceStatus_; }
    drawSceneDiagnostics(status_,renderer.pickingStatus(),renderer.modelStatus(),&renderer.shadowStats());
}
}
