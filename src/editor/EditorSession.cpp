#include "EditorSession.h"
#include "EditorRenderFrame.h"
#include "input/Navigation.h"
#include "scene/SceneExtraction.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "math/Json.h"
#include "AssetPreviews.h"
#include "assets/MaterialAsset.h"
#include "assets/ModelMaterialOverrides.h"
#include "crimson-renderer/Environment.h"
#include "resources/Paths.h"
#include "tools/SceneDiagnostics.h"
#include "QuaderSceneIntegration.h"
#include <imgui.h>
#include <SDL3/SDL_keyboard.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <exception>

namespace crimson {
using Json=nlohmann::json;
EditorSession::EditorSession(const std::filesystem::path& root, AssetLibrary& assets, Renderer& renderer)
    : root_(root),document_(root),play_(root),particleEditor_(root),materialEditor_(root),navigationProfileEditor_(root),collisionLayersEditor_(root),gameplayEffectEditor_(root),resources_(*assets.resources()),prepared_(assets,renderer,resources_,root),collisions_(assets,resources_),navigationPreview_(resources_,root),collisionOverlays_(resources_),quader_(root) {
    restoreSession(assets);
    documentEpoch_=document_.documentGeneration();
    previews_=std::make_unique<AssetPreviews>(root,assets,resources_,renderer,prepared_);
    preparedEnvironment_="<unprepared>";
    editor_.setParticleBoundsQuery([this](EntityId id){return play_.playing()?play_.particles().bounds(id):particles_.bounds(id);});
    gameInput_.setDesktopCoordinates(true);
    play_.setParticleDefinitions([&assets](const std::string& id) {
        return ParticleDefinitionVersion{assets.findParticle(id),assets.particleRevision(id),assets.particleDiagnostic(id)};
    });
    play_.setCollisionMeshDefinitions([this](const std::string& id){return collisions_.query(id);});
    editor_.setCollisionMeshQuery([this](const std::string& id){return collisions_.query(id);});
    editor_.setQuaderBridge(&quader_);
    const NavigationAgentProfileQuery agentProfiles=[this,&assets](const std::string& id) {
        try {
            if(auto managed=assets.findNavigationAgentProfile(id))return managed;
            return activeDocument().navigationAgentProfile(id);
        } catch(...) {return std::shared_ptr<const NavigationAgentProfile>{};}
    };
    editor_.setAgentProfileQuery([this,agentProfiles](const std::string& id) {
        return play_.playing()?play_.acceptedAgentProfile(id):agentProfiles(id);
    });
    navigationProfileEditor_.setStagedProfiles([this](const std::string& id){return activeDocument().navigationAgentProfile(id);});
    const GameplayEffectQuery gameplayEffects=[this,&assets](const std::string& id) {
        try {
            if(auto managed=assets.findGameplayEffect(id))return EffectDefinitionSnapshot{std::move(managed),assets.gameplayEffectRevision(id),{}};
            if(auto staged=activeDocument().gameplayEffect(id))return EffectDefinitionSnapshot{std::move(staged),1,{}};
            const auto diagnostic=assets.gameplayEffectDiagnostic(id);
            if(!diagnostic.empty())return EffectDefinitionSnapshot{{},0,diagnostic};
            return EffectDefinitionSnapshot{{},0,"Effect unavailable: "+id};
        } catch(const std::exception& error) {return EffectDefinitionSnapshot{{},0,error.what()};}
    };
    gameplayEffectEditor_.setStagedEffects(gameplayEffects);
    play_.setGameplayEffects(gameplayEffects);
    play_.setAgentProfiles(agentProfiles);play_.setResourceManager(resources_);
    editor_.setNavigationBuildStatus([this](const std::string& profile) {
        return navigationPreview_.status(play_.playing()?play_.navigationWorld():&navigationPreview_.world(),profile);
    });
}
EditorSession::~EditorSession() { gameInput_.cancel();saveSession(); }
void EditorSession::restoreSession(AssetLibrary& assets) {
    const auto path=resolveAssetPath(root_,"assets/editor-session.json");
    try {
        if (std::filesystem::exists(path)) {
            std::ifstream input(path); const auto saved=Json::parse(input);
            if (saved.at("version") != 1) throw std::runtime_error("Unsupported editor session version.");
            recoveredLegacy_=saved.value("recoveredLegacyPreview",false);
            if(saved.contains("panels")) { const auto& panels=saved.at("panels");editor_.setPanels({panels.value("outliner",true),panels.value("inspector",true),panels.value("assetBrowser",true),panels.value("game",false)}); }
            if(saved.contains("overlays")) { const auto& overlays=saved.at("overlays");editor_.setOverlays({overlays.value("collisions",true),overlays.value("grid",true),overlays.value("transformGizmo",true),overlays.value("navigationMesh",true)}); }
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
        if (!document_.assetId().empty()) cameras_[document_.assetId()]=play_.playing()?editCameraBeforePlay_:cameraPilot_.freeCamera().value_or(settings_.camera);
        Json cameras=Json::object();
        for (const auto& [id,camera] : cameras_) cameras[id]={{"position",camera.position},{"target",camera.target},{"up",camera.up},
            {"fov",camera.fov},{"near",camera.nearPlane},{"far",camera.farPlane}};
        writeFileAtomically(resolveAssetPath(root_,"assets/editor-session.json"),Json{{"version",1},{"scene",unresolvedScene_.empty() ? document_.assetId() : unresolvedScene_},
            {"recoveredLegacyPreview",recoveredLegacy_},{"cameras",std::move(cameras)},
            {"panels",{{"outliner",editor_.panels().outliner},{"inspector",editor_.panels().inspector},{"assetBrowser",editor_.panels().assetBrowser},{"game",editor_.panels().game}}},
            {"overlays",{{"collisions",editor_.overlays().collisions},{"grid",editor_.overlays().grid},{"transformGizmo",editor_.overlays().transformGizmo},{"navigationMesh",editor_.overlays().navigationMesh}}}}.dump(2));
    } catch (...) { /* Session state is expendable; authored scenes were saved explicitly. */ }
}
void EditorSession::filesChanged(std::span<const std::filesystem::path> paths) {
    particleEditor_.filesChanged(paths);
    materialEditor_.filesChanged(paths);
    navigationProfileEditor_.filesChanged(paths);
    gameplayEffectEditor_.filesChanged(paths);
    collisionLayersEditor_.filesChanged(paths);
    changedFiles_.insert(changedFiles_.end(),paths.begin(),paths.end());
    for(const auto& path:paths) {
        previews_->invalidateFile(path);
        if(!preparedEnvironment_.empty() && assetIdAtPath(root_,path)==preparedEnvironment_) {
            environment_={};environmentRevision_=0;
        }
    }
}
void EditorSession::prepareFrame(AssetLibrary& assets,Renderer& renderer,std::uint32_t bgfxFrame) {
    collisions_.collect();
    std::vector<QuaderTarget> targets;
    const auto records=document_.scene().records();
    for(const auto& record:records) {
        if(record.collider && record.collider->shape==ColliderShape::Trimesh && !record.collider->mesh && !record.collider->source.empty())(void)collisions_.query(record.collider->source.id);
        if(record.quader) {
            const bool imported=std::any_of(records.begin(),records.end(),[&](const auto& child){return child.quaderGenerated && child.quaderGenerated->owner==record.id;});
            targets.push_back({record.id,*record.quader,imported,record.quader->saveKey});
        }
    }
    QuaderBridgeHooks hooks;
    hooks.dirtyMaterial=[this](const std::string& id){return materialEditor_.blocksExternalRefresh(id);};
    hooks.resolveMaterial=[&assets](const std::string& id){return assets.references().resolveMaterial(id);};
    hooks.resolveTexture=[&assets](const std::string& id,bool normal){return assets.references().resolveTexture(id,normal);};
    hooks.reservedAsset=[&assets](const std::string& id){return assets.references().deleted(id);};
    hooks.assetPolicyRevision=[&assets]{return assets.references().revision();};
    hooks.applyImport=[this,&assets](EntityId owner,const QuaderLink& link,const QuaderImportResult& result){
        if(!applyQuaderSceneImport(document_,owner,link,result,[&assets](const std::string& id)->std::optional<std::vector<std::string>> {
            if(const auto model=assets.findModel(id))return model->materialIds;
            return std::nullopt;
        }))throw std::runtime_error(document_.status());
        return true;
    };
    hooks.publishAssets=[this,&assets](const QuaderImportResult& result) {
        assets.publishManagedModels(result.preparedModels);
        std::vector<std::filesystem::path> changed;
        for(const auto& file:result.files) {
            const auto path=checkedManagedAssetPath(root_,file.id);
            const auto relative=path.lexically_relative(root_).generic_string();
            // Prepared models already publish the complete bundle generation.
            // Its expanded cache is private and must not invalidate that seed.
            if(relative.starts_with(".crimson/cache/"))continue;
            assets.noteAssetWrite(assetIdAtPath(root_,path));
            if(path.extension()!=".crimson-quader")assets.onFileChanged(path);
            changed.push_back(path);
        }
        materialEditor_.filesChanged(changed);
    };
    quader_.update(targets,document_.documentGeneration(),play_.playing() || document_.transactionActive() || editor_.playTransitionBlocked(),hooks);
    if(!play_.playing() && !document_.transactionActive())assets.collectUnusedQuaderModels(document_);
    prepared_.beginFrame(); renderer.advanceStreaming();renderer.setAppearance(editor_.appearance());
    for(const auto& file:changedFiles_) {
        const auto id=assetIdAtPath(root_,file);
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
    const auto map=environment.environment.source==EnvironmentSource::Hdri?
        assets.references().resolveEnvironment(environment.environment.map):std::string{};
    if(preparedEnvironment_!=map || !environment_) {
        preparedEnvironment_=map; environmentRevision_=0;
        ResourceRequestOptions options;options.variant="ibl";
        if(!map.empty())options.dependencies={resolveAssetPath(root_,map)};
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
    previews_->pump(bgfxFrame,visiblePreviewIds_,editor_.thumbnailDemand());
}
void EditorSession::cancelInteraction(Renderer& renderer) {
    cameraPilot_.cancelGesture(activeDocument(),settings_.camera);cameraGestureCancel_=false;
    editor_.cancelInteraction(activeDocument(),renderer);particleEditor_.cancelInteraction();materialEditor_.cancelInteraction();navigationProfileEditor_.cancelInteraction();collisionLayersEditor_.cancelInteraction();gameplayEffectEditor_.cancelInteraction();gameInput_.cancel();focusGame_=false;skipSimulationTime_=true;
    playShortcuts_.cancel();playActionPending_=PlayAction::None;
}
void EditorSession::transitionPlay(Renderer& renderer,Navigation& navigation,bool stop) {
    attributeDebug_.reset();
    cameraPilot_.useEditorCamera(activeDocument(),settings_.camera);cameraGestureCancel_=false;
    playAnimations_.clear();animationPlayTick_=0;
    waitingForCollision_=false;
    editor_.cancelInteraction(activeDocument(),renderer);navigation.cancel();gameInput_.cancel();
    pendingDrops_.clear();renderer.cancelPicking();skipSimulationTime_=true;playStatus_.clear();
    if(play_.playing()) {
        play_.stop();settings_.camera=editCameraBeforePlay_;
        if(deferredSceneReload_ && !document_.assetId().empty())changedFiles_.push_back(resolveAssetPath(root_,document_.assetId()));
        deferredSceneReload_=false;
    } else if(!stop) {
        editCameraBeforePlay_=settings_.camera;
        if(play_.start(document_)) { editor_.showGame();focusGame_=true; }
        else {
            playStatus_=play_.status();
            if(playStatus_.starts_with("Preparing mesh collision")) {
                waitingForCollision_=true;waitingSceneGeneration_=document_.documentGeneration();waitingSceneRevision_=document_.scene().revision();
            }
        }
    }
    documentEpoch_=activeDocument().documentGeneration();
}
void EditorSession::frame(AssetLibrary& assets, Renderer& renderer, Navigation& navigation, SDL_Window* window,
                          uint32_t bgfxFrame, bool quitRequested) {
    const bool forceStop=editor_.needsAuthoringDocument() || quitRequested || (play_.playing() && !editor_.panels().game);
    if(waitingForCollision_) {
        if(forceStop || document_.documentGeneration()!=waitingSceneGeneration_ || document_.scene().revision()!=waitingSceneRevision_ || playActionPending_==PlayAction::Stop)waitingForCollision_=false;
        else {
            bool ready=true;
            for(const auto& record:document_.scene().records())if(record.collider && record.collider->shape==ColliderShape::Trimesh && (record.collider->mesh || !record.collider->source.empty())) {
                const auto source=colliderMeshResult(*record.collider,[this](const auto& id){return collisions_.query(id);});
                if(!source.mesh || !source.current) {
                    ready=false;playStatus_=source.diagnostic;
                    if(!playStatus_.starts_with("Preparing mesh collision"))waitingForCollision_=false;
                    break;
                }
            }
            if(ready){waitingForCollision_=false;playActionPending_=PlayAction::Start;}
        }
    }
    if(play_.playing() && (forceStop || playActionPending_==PlayAction::Stop))transitionPlay(renderer,navigation,true);
    else if(!play_.playing() && !forceStop && playActionPending_==PlayAction::Start)transitionPlay(renderer,navigation,false);
    playActionPending_=PlayAction::None;
    auto& document=activeDocument();
    if(cameraGestureCancel_) {
        cameraPilot_.cancelGesture(document,settings_.camera);navigation.cancel();cameraGestureCancel_=false;
    }
    if(quitRequested) {cameraPilot_.useEditorCamera(document,settings_.camera);navigation.cancel();}
    cameraPilot_.sync(document,settings_.camera);
    editor_.setCameraViewState(cameraPilot_.selectedCamera(),cameraPilot_.piloting(),cameraPilot_.gestureActive());
    const auto previousAsset=document.assetId();
    const float simulationSeconds=simulationSuspended_ || skipSimulationTime_?0.f:ImGui::GetIO().DeltaTime;
    materialRealSeconds_=std::chrono::duration<double>(std::chrono::steady_clock::now()-materialClockStart_).count();
    if(!play_.playing())materialGameSeconds_+=simulationSeconds;
    skipSimulationTime_=false;
    const auto previousCamera=cameraPilot_.freeCamera().value_or(settings_.camera);
    document.setPreviewCamera(settings_.camera);
    const auto interactionDocument=document.documentGeneration();
    GameplayInspectorContext gameplay;
    // Reserve the bottom viewport inset before the scene builds its dockspace.
    // Console state is editor-only; commands operate on the transient Play world.
    std::vector<ConsoleEffect> consoleEffects;
    for(const auto& asset:assets.catalog())if(asset.kind==AssetKind::GameplayEffect)consoleEffects.push_back({asset.id,asset.name});
    for(const auto& effect:document.stagedGameplayEffects())if(effect&&std::none_of(consoleEffects.begin(),consoleEffects.end(),[&](const auto& value){return value.id==effect->id;}))
        consoleEffects.push_back({effect->id,effect->name});
    commandBar_.setEffects(consoleEffects);
    const auto command=commandBar_.draw();
    attributeDebug_.update(play_);
    if(command.command==ConsoleCommand::ShowDebugAttributes) {
        if(play_.playing()) {
            attributeDebug_.toggle(play_);commandBar_.releaseFocus();focusGame_=true;
        } else commandBar_.setStatus("Start Play to use ShowDebug Attributes.");
    }
    else if(command.command) {
        if(!play_.playing())commandBar_.setStatus("Start Play to apply or remove effects.");
        else if(!attributeDebug_.enabled()||attributeDebug_.target()==NoEntity)commandBar_.setStatus("Run ShowDebug Attributes to select a target.");
        else {
            std::string diagnostic;const auto id=resolveConsoleEffect(command.argument,consoleEffects,diagnostic);
            if(!id)commandBar_.setStatus(std::move(diagnostic));
            else {
                const auto target=attributeDebug_.target();
                if(command.command==ConsoleCommand::ApplyEffect) {
                    auto source=play_.document().scene().properties().playerCharacter;
                    if(!source||!play_.document().scene().contains(source))source=target;
                    const auto result=play_.applyEffect(source,target,*id);
                    commandBar_.setStatus(result?"Effect applied.":result.diagnostic);
                } else {
                    const auto removed=play_.removeEffects(target,*id);
                    commandBar_.setStatus(removed?"Active effect removed.":"No active instances; instant changes remain applied.");
                }
                commandBar_.releaseFocus();focusGame_=true;
            }
        }
    }
    const auto frame=editor_.draw(document,settings_,assets,renderer,window,navigation.active(),bgfxFrame,quitRequested,particleEditor_,materialEditor_,navigationProfileEditor_,collisionLayersEditor_,gameplayEffectEditor_,gameplay);
    const auto sceneCamera=frame.viewportCamera.value_or(settings_.camera);
    if(frame.togglePlay)playActionPending_=play_.playing()?PlayAction::Stop:PlayAction::Start;
    if(frame.createParticle) particleEditor_.requestCreate(frame.particleFolder,assets);
    if(!frame.openParticle.empty()) particleEditor_.requestOpen(frame.openParticle,assets);
    if(frame.createMaterial)materialEditor_.requestCreate(frame.materialFolder,assets);
    if(!frame.openMaterial.empty())materialEditor_.requestOpen(frame.openMaterial,assets);
    particleEditor_.setAntiAliasing(editor_.antiAliasing());
    materialEditor_.setAntiAliasing(editor_.antiAliasing());
    particleEditor_.draw(assets,renderer,simulationSeconds,materialEditor_);
    materialEditor_.draw(assets,renderer,resources_,simulationSeconds);
    if(frame.createNavigationAgentProfile)navigationProfileEditor_.requestCreate(frame.navigationAgentProfileFolder,assets);
    if(!frame.openNavigationAgentProfile.empty())navigationProfileEditor_.requestOpen(frame.openNavigationAgentProfile,assets);
    navigationProfileEditor_.draw(assets);
    collisionLayersEditor_.draw();
    if(frame.createGameplayEffect)gameplayEffectEditor_.requestCreate(frame.gameplayEffectFolder,assets);
    if(!frame.openGameplayEffect.empty())gameplayEffectEditor_.requestOpen(frame.openGameplayEffect,assets);
    gameplayEffectEditor_.draw(assets);
    status_=startupStatus_+playStatus_;
    if (!play_.playing() && (frame.documentReplaced || document.assetId() != cameraScene_ || document.documentGeneration()!=documentEpoch_)) {
        cameraPilot_.sync(document,settings_.camera);
        if (!previousAsset.empty()) cameras_[previousAsset]=previousCamera;
        cameraScene_=document.assetId(); navigation.cancel(); renderer.cancelPicking();
        unresolvedScene_.clear(); startupStatus_.clear(); status_.clear();materialDropStatus_.clear();
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
        auto position=sceneCamera.target;position[1]=0;
        if(const auto ray=sceneCamera.screenRay(mouse[0],mouse[1],frame.viewport))
            if(const auto hit=math::intersectPlane(*ray,{0,0,0},{0,1,0}))position=*hit;
        return position;
    };
    if(!dropped.empty()) pendingDrops_.push_back({std::move(dropped),placement(frame.modelDropPosition),documentEpoch_});
    if(!frame.particleDropId.empty() && assets.findParticle(frame.particleDropId))
        document.createParticle(frame.particleDropId,assets.displayName(frame.particleDropId),math::translation(placement(frame.particleDropPosition)));
    if(!frame.materialDragId.empty())materialDropStatus_.clear();
    const bool materialDocumentCurrent=interactionDocument==document.documentGeneration() && !frame.documentReplaced;
    if(frame.materialAssignment && materialDocumentCurrent) {
        try {
            const auto& assignment=*frame.materialAssignment;
            const auto record=document.scene().find(assignment.entityId);
            if(!record || !record->model)throw std::runtime_error("The target mesh is no longer available.");
            const auto model=assets.findModel(record->model->id);
            if(!model || assignment.slot>=model->materials.size())throw std::runtime_error("The target material slot is no longer available.");
            const auto material=assets.findMaterial(assignment.materialId.id);
            if(!material || !modelMaterialCompatible(*material))throw std::runtime_error("Choose a Surface material for a mesh slot.");
            auto overrides=record->modelMaterials;
            setModelMaterialOverride(overrides,assignment.slot,assignment.materialId);
            // Check all slot UV/tangent/domain requirements before history is
            // changed. Preview preparation may still be waiting for GPU data.
            (void)assets.resolvedModelMaterials(*model,overrides);
            if(!document.edit("Assign material slot",[&](auto& records,auto&) {
                const auto target=std::find_if(records.begin(),records.end(),[&](const auto& item){return item.id==assignment.entityId;});
                if(target!=records.end())target->modelMaterials=overrides;
            }))throw std::runtime_error(document.status().empty()?"The material slot could not be assigned.":document.status());
            materialDropStatus_.clear();renderer.cancelPicking();
        } catch(const std::exception& error) { materialDropStatus_="Material assignment: "+std::string(error.what())+'\n'; }
    }
    bool gameOpen=editor_.panels().game;
    const auto game=gameView_.draw(document,renderer,gameOpen,play_.playing(),focusGame_,assets.textureResidentBytes(),attributeDebug_.enabled());
    if(!gameOpen) { editor_.hideGame();renderer.releaseGameViewport();if(play_.playing())playActionPending_=PlayAction::Stop; }
    const auto playShortcut=playShortcuts_.consume({play_.playing(),quitRequested || editor_.playTransitionBlocked(),
        navigation.active() || document.transactionActive() || particleEditor_.transactionActive() || materialEditor_.transactionActive() || navigationProfileEditor_.transactionActive() || collisionLayersEditor_.transactionActive() || gameplayEffectEditor_.transactionActive()});
    if(playShortcut!=PlayAction::None)playActionPending_=playShortcut;
    auto gameCamera=resolveGameCamera(document.scene());
    auto* gameWindow=game.viewport.windowId?SDL_GetWindowFromID(game.viewport.windowId):nullptr;
    // ImGui focus selects the dock/tab; a detached Game also needs native
    // keyboard focus. Its platform window may only exist on the next frame.
    if(focusGame_ && gameWindow && SDL_GetKeyboardFocus()!=gameWindow)SDL_RaiseWindow(gameWindow);
    gameInput_.setViewport(game.viewport,play_.playing() && gameWindow && game.pixelWidth>0 && gameCamera.has_value(),game.viewport.inputBlocked || simulationSuspended_ || command.inputActive);
    if(!game.focused || !gameWindow || SDL_GetKeyboardFocus()!=gameWindow)gameInput_.cancel();
    else if(game.clicked)gameInput_.acquireFromImageClick();
    else if(focusGame_)gameInput_.acquire();
    if(game.attributeDebugCycle && game.focused && !command.inputActive && !simulationSuspended_)
        attributeDebug_.cycle(play_,game.attributeDebugCycle<0);
    if(gameInput_.ownsKeyboard())navigation.cancel();
    gameInput_.syncMouseCapture();
    const bool focusComplete=game.focused && gameWindow && SDL_GetKeyboardFocus()==gameWindow &&
        (!gameCamera || gameInput_.ownsKeyboard());
    focusGame_=focusGame_ && gameOpen && play_.playing() && !focusComplete;
    if(frame.cameraAction!=EditorCameraAction::None) {
        navigation.cancel();renderer.cancelPicking();
        switch(frame.cameraAction) {
        case EditorCameraAction::EditorCamera:cameraPilot_.useEditorCamera(document,settings_.camera);break;
        case EditorCameraAction::SelectCamera:cameraPilot_.selectCamera(document,frame.cameraEntity,settings_.camera);break;
        case EditorCameraAction::TogglePilot:cameraPilot_.setPiloting(document,settings_.camera,!cameraPilot_.piloting());break;
        case EditorCameraAction::CreateCamera: {
            const auto aspect=frame.viewport.height>0?frame.viewport.width/frame.viewport.height:1.f;
            if(const auto id=document.createCamera(sceneCamera,aspect))cameraPilot_.selectCamera(document,id,settings_.camera);
            break;
        }
        case EditorCameraAction::None:break;
        }
    }
    const bool readOnlyCamera=cameraPilot_.selectedCamera()!=NoEntity && !cameraPilot_.piloting();
    auto sceneRegion=frame.viewport;sceneRegion.inputBlocked|=gameInput_.ownsKeyboard() || readOnlyCamera ||
        frame.cameraAction!=EditorCameraAction::None || interactionDocument!=document.documentGeneration();
    // Input still uses current-frame panel ownership. Its new pose becomes
    // visible next frame, keeping this image aligned with its overlays/picks.
    navigation.update(settings_.camera,sceneRegion,ImGui::GetIO().DeltaTime,false,cameraPilot_.piloting());
    cameraPilot_.apply(document,settings_.camera,navigation.active());
    for(auto iterator=pendingDrops_.begin();iterator!=pendingDrops_.end();) {
        if(iterator->documentEpoch!=documentEpoch_) { iterator=pendingDrops_.erase(iterator);continue; }
        std::vector<ModelPlacement> placements;bool waiting=false;
        for(const auto& id:iterator->ids) {
            if(assets.references().resolve(id).empty()) { status_+="Dropped model was deleted: "+id+"\n";continue; }
            const auto prepared=prepared_.prepare(id,math::translation(iterator->position),sceneCamera,renderer.height(),false,ResourcePriority::Placement,
                std::max(1.f,frame.viewport.width)/std::max(1.f,frame.viewport.height));
            if(prepared && prepared->ready) placements.push_back({id,assets.displayName(id),math::translation(iterator->position)});
            else if(!prepared_.failed(id)) waiting=true;
        }
        if(waiting) { ++iterator;continue; }
        document.createModels(placements);renderer.cancelPicking();iterator=pendingDrops_.erase(iterator);
    }
    if(play_.playing()) {
        const auto inputCamera=resolveGameCamera(play_.presentationScene());
        play_.advance(simulationSeconds,gameInput_,inputCamera.value_or(gameCamera.value_or(Camera{})),!simulationSuspended_);
    }
    attributeDebug_.update(play_);
    const NavigationAgentProfileQuery agentProfiles=[&](const std::string& id) {
        if(play_.playing())return play_.acceptedAgentProfile(id);
        try {
            if(auto managed=assets.findNavigationAgentProfile(id))return managed;
            return document.navigationAgentProfile(id);
        } catch(...) {return std::shared_ptr<const NavigationAgentProfile>{};}
    };
    if(!play_.playing())navigationPreview_.update(document,[this](const std::string& id){return collisions_.query(id);},agentProfiles);
    const auto& renderScene=play_.playing()?play_.presentationScene():document.scene();
    const MaterialEvaluationContext materialContext{play_.playing()?play_.tickCount()*PlaySession::FixedStep:materialGameSeconds_,materialRealSeconds_};
    prepared_.setMaterialContext(materialContext);
    auto extracted=extractScene(renderScene,agentProfiles);
    if(play_.playing()) {
        const auto ticks=play_.tickCount();
        const auto elapsed=ticks>=animationPlayTick_?(ticks-animationPlayTick_)*PlaySession::FixedStep:0.;
        playAnimations_.advance(extracted,document.documentGeneration(),elapsed);animationPlayTick_=ticks;
    } else editAnimations_.advance(extracted,document.documentGeneration(),simulationSeconds);
    settings_.environment=extracted.environment.environment;settings_.exposure=extracted.environment.exposure;
    settings_.proceduralSun=extracted.proceduralSun;
    settings_.environment.map=assets.references().resolveEnvironment(settings_.environment.map);
    gameCamera=resolveGameCamera(renderScene);
    auto& sceneParticles=play_.playing()?play_.particles():particles_;
    if(!play_.playing() || frame.playParticleEntity)sceneParticles.reconcile(document.documentGeneration(),extracted.particles,[&](const std::string& id) {
        return ParticleDefinitionVersion{assets.findParticle(id),assets.particleRevision(id),assets.particleDiagnostic(id)};
    },play_.playing()?0.f:simulationSeconds,frame.playParticleEntity);
    std::vector<RenderParticle> particlePackets;particlePackets.reserve(sceneParticles.presentation().size());
    std::vector<RenderParticleMaterial> particleMaterials;particleMaterials.push_back({});
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
                const auto index=static_cast<std::uint32_t>(particleMaterials.size());
                particleMaterials.push_back(material->render);particlePins.push_back(material);
                found=materialIndices.emplace(id,index).first;
            }
            if(found->second==UINT32_MAX)continue;
            for(std::size_t offset=group.first;offset<group.first+group.count;++offset) {
                const auto& p=presentation[offset];particlePackets.push_back({p.position,p.size,p.color,found->second,p.rotation,p.billboardMode,p.right,p.up});
            }
        } catch(const std::exception& error) {
            status_+="Particle material "+group.materialId+": "+error.what()+"\n";
        }
    }
    EditorRenderFrameInput renderInput;
    renderInput.extracted=std::move(extracted);renderInput.settings=settings_;
    renderInput.settings.antiAliasing=editor_.antiAliasing();
    renderInput.sceneCamera=sceneCamera;renderInput.gameCamera=gameCamera;
    renderInput.sceneWidth=renderer.width();renderInput.sceneHeight=renderer.height();
    renderInput.gameWidth=game.pixelWidth;renderInput.gameHeight=game.pixelHeight;
    renderInput.sceneAspect=std::max(1.f,frame.viewport.width)/std::max(1.f,frame.viewport.height);
    renderInput.sceneVisible=frame.viewport.width>0 && frame.viewport.height>0;renderInput.gameVisible=gameOpen;
    renderInput.selection=document.selection().ids;renderInput.placementIds=frame.placementPreviewIds;
    if(materialDocumentCurrent)renderInput.materialPreview=frame.materialPreview;
    renderInput.placementPosition=placement(frame.placementPreviewPosition);
    renderInput.particles=std::move(particlePackets);renderInput.particleMaterials=std::move(particleMaterials);
    renderInput.particlePins=std::move(particlePins);renderInput.worldId=document.documentGeneration();
    renderInput.giWorldId=(document.documentGeneration()<<1)|static_cast<std::uint64_t>(play_.playing());
    renderInput.gameCameraId=renderScene.properties().activeCamera;renderInput.environmentId=renderScene.properties().activeEnvironment;
    renderInput.shadowResolution=editor_.pointShadowResolution();renderInput.materialContext=materialContext;
    renderInput.appearance=editor_.appearance();renderInput.gridVisible=editor_.overlays().grid;
    auto collisionOverlays=collisionOverlays_.prepare(renderScene,document.selection().ids,editor_.overlays().collisions,
        [this](const std::string& id){return collisions_.query(id);},agentProfiles);
    if(editor_.overlays().navigationMesh) {
        auto navigationOverlays=navigationPreview_.overlays(play_.playing()?play_.navigationWorld():&navigationPreview_.world(),editor_.navigationPreviewProfile());
        collisionOverlays.scene.insert(collisionOverlays.scene.end(),navigationOverlays.begin(),navigationOverlays.end());
    }
    renderInput.sceneCollisionOverlays=std::move(collisionOverlays.scene);
    renderInput.gameCollisionOverlays=std::move(collisionOverlays.game);
    renderer.renderFrame(prepareEditorRenderFrame(std::move(renderInput),assets,prepared_));
    if(gameOpen && game.pixelWidth && game.pixelHeight && gameCamera) {
        if(play_.playing()) {
            for(const auto& actor:renderScene.records())if(actor.navigation && actor.navigation->enabled && actor.navigation->showRoute && renderScene.isVisible(actor.id)) {
                std::optional<Vec3> position;
                if(const auto world=renderScene.worldTransform(actor.id))position=math::transformPoint(*world,{});
                gameView_.drawRoute(*gameCamera,play_.actorRemainingPath(actor.id),position);
            }
            if(attributeDebug_.enabled())gameView_.drawAttributeDebug(*gameCamera,renderScene,attributeDebug_.target(),attributeDebug_.rows(play_),agentProfiles,
                [&assets](const std::string& id)->std::optional<Aabb> {
                    if(const auto model=assets.peekModel(id))return model->bounds;
                    return std::nullopt;
                });
        }
    }
    visiblePreviewIds_=frame.visiblePreviewIds;viewportRegion_=frame.viewport;
    if(!play_.playing() && !document.assetId().empty() && !document.dirty()) try {
        const auto fingerprint=sceneFileFingerprint(root_,document.assetId());
        if(fingerprint!=savedPreviewFingerprint_) { savedPreviewFingerprint_=fingerprint;previews_->requestSceneSave(document.assetId()); }
    } catch(const std::exception& error){status_=error.what();}
    prepared_.endFrame();
    // This target is shared by evictable asset caches. Fixed render targets remain
    // in total GPU accounting, but cannot be reclaimed by evicting material mips.
    const auto retained=renderer.modelResidentBytes()+renderer.thumbnailResidentBytes();
    assets.trimTextureCache(retained>=resources_.config().gpuRetentionBytes?0:resources_.config().gpuRetentionBytes-retained);
    if(!prepared_.status().empty())status_+=prepared_.status();
    if(!renderer.clusteredLightingStatus().empty())status_+=renderer.clusteredLightingStatus();
    if(!renderer.ssgiStatus().empty())status_+=renderer.ssgiStatus();
    if(!renderer.antiAliasingStatus().empty())status_+=renderer.antiAliasingStatus()+'\n';
    if(!renderer.refractionStatus().empty())status_+=renderer.refractionStatus()+'\n';
    if(!sceneParticles.status().empty())status_+=sceneParticles.status();
    if(play_.playing()) {
        status_+=play_.status();
        if(!document.scene().properties().playerCharacter)status_+="No controlled player. Add a Player or select an entity with Player Input and choose Make Player.\n";
    }
    if(!renderer.particleStatus().empty())status_+=renderer.particleStatus();
    if(!renderer.shadowStatus().empty())status_+=renderer.shadowStatus();
    const auto& particleDraw=renderer.particleStats();
    if(frame.viewport.width>0 && (particleDraw.dropped || particleDraw.invalid))
        status_+="Particle rendering omitted "+std::to_string(particleDraw.dropped)+" packets; "+std::to_string(particleDraw.invalid)+" were invalid.\n";
    if(!previews_->status().empty())status_+=previews_->status();
    if(environment_.state()==ResourceState::Failed)status_+=environment_.diagnostic();
    const auto& environmentStatus=renderer.environmentStatus();
    if(environmentStatus.starts_with("Procedural sky") || environmentStatus.starts_with("Environment:"))
        status_+=environmentStatus+'\n';
    if(!resourceStatus_.empty()) { if(!status_.empty())status_+='\n';status_+=resourceStatus_; }
    status_+=materialDropStatus_;
    if(!cameraPilot_.status().empty())status_+=cameraPilot_.status()+'\n';
    drawSceneDiagnostics(status_,renderer.pickingStatus(),renderer.modelStatus(),&renderer.shadowStats());
}
}
