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
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>

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
    : root_(root),document_(root),resources_(*assets.resources()),prepared_(assets,renderer,resources_,root) {
    restoreSession(assets);
    documentEpoch_=document_.documentGeneration();
    previews_=std::make_unique<AssetPreviews>(root,assets,resources_,renderer,prepared_);
    preparedEnvironment_="<unprepared>";
}
EditorSession::~EditorSession() { saveSession(); }
void EditorSession::restoreSession(AssetLibrary& assets) {
    const auto path=root_/"assets"/"editor-session.json";
    try {
        if (std::filesystem::exists(path)) {
            std::ifstream input(path); const auto saved=Json::parse(input);
            if (saved.at("version") != 1) throw std::runtime_error("Unsupported editor session version.");
            recoveredLegacy_=saved.value("recoveredLegacyPreview",false);
            if(saved.contains("panels")) { const auto& panels=saved.at("panels");editor_.setPanels({panels.value("outliner",true),panels.value("inspector",true),panels.value("assetBrowser",true)}); }
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
                    light->point=ScenePointLight{source.color,source.intensity,source.enabled};
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
        if (!document_.assetId().empty()) cameras_[document_.assetId()]=settings_.camera;
        Json cameras=Json::object();
        for (const auto& [id,camera] : cameras_) cameras[id]={{"position",camera.position},{"target",camera.target},{"up",camera.up},
            {"fov",camera.fov},{"near",camera.nearPlane},{"far",camera.farPlane}};
        writeFileAtomically(root_/"assets"/"editor-session.json",Json{{"version",1},{"scene",unresolvedScene_.empty() ? document_.assetId() : unresolvedScene_},
            {"recoveredLegacyPreview",recoveredLegacy_},{"cameras",std::move(cameras)},
            {"panels",{{"outliner",editor_.panels().outliner},{"inspector",editor_.panels().inspector},{"assetBrowser",editor_.panels().assetBrowser}}}}.dump(2));
    } catch (...) { /* Session state is expendable; authored scenes were saved explicitly. */ }
}
void EditorSession::filesChanged(std::span<const std::filesystem::path> paths) {
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
    if(externalScene_.state()==ResourceState::Ready) if(auto snapshot=externalScene_.snapshot()) {
        if(snapshot->id==document_.assetId() && snapshot->fingerprint!=notifiedSceneFingerprint_ && document_.externalFileChanged()) {
            notifiedSceneFingerprint_=snapshot->fingerprint;editor_.notifyExternalSceneChange(*snapshot);
        }
    }
    const auto environment=resolveEnvironment(document_.scene());
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
void EditorSession::cancelInteraction(Renderer& renderer) { editor_.cancelInteraction(document_,renderer); }
void EditorSession::frame(AssetLibrary& assets, Renderer& renderer, Navigation& navigation, SDL_Window* window,
                          uint32_t bgfxFrame, bool quitRequested) {
    const auto previousAsset=document_.assetId();
    const auto previousCamera=settings_.camera;
    document_.setPreviewCamera(settings_.camera);
    const auto frame=editor_.draw(document_,settings_,assets,renderer,window,navigation.active(),bgfxFrame,quitRequested);
    status_=startupStatus_;
    if (frame.documentReplaced || document_.assetId() != cameraScene_ || document_.documentGeneration()!=documentEpoch_) {
        if (!previousAsset.empty()) cameras_[previousAsset]=previousCamera;
        cameraScene_=document_.assetId(); navigation.cancel(); renderer.cancelPicking();
        unresolvedScene_.clear(); startupStatus_.clear(); status_.clear();
        if(document_.documentGeneration()!=documentEpoch_) { documentEpoch_=document_.documentGeneration();pendingDrops_.clear(); }
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
    navigation.update(settings_.camera,frame.viewport,ImGui::GetIO().DeltaTime,false);
    const auto environment=resolveEnvironment(document_.scene());settings_.environment=environment.environment;settings_.exposure=environment.exposure;
    settings_.environment.map=assets.references().resolveEnvironment(settings_.environment.map);
    for(auto iterator=pendingDrops_.begin();iterator!=pendingDrops_.end();) {
        if(iterator->documentEpoch!=documentEpoch_) { iterator=pendingDrops_.erase(iterator);continue; }
        std::vector<ModelPlacement> placements;bool waiting=false;
        for(const auto& id:iterator->ids) {
            if(assets.references().resolve(id).empty()) { status_+="Dropped model was deleted: "+id+"\n";continue; }
            const auto prepared=prepared_.prepare(id,math::translation(iterator->position),settings_.camera,static_cast<std::uint32_t>(std::max(1.0f,frame.viewport.height)),false,ResourcePriority::Placement);
            if(prepared && prepared->ready) placements.push_back({id,assets.displayName(id),math::translation(iterator->position)});
            else if(!prepared_.failed(id)) waiting=true;
        }
        if(waiting) { ++iterator;continue; }
        document_.createModels(placements);renderer.cancelPicking();iterator=pendingDrops_.erase(iterator);
    }
    const auto extracted=extractScene(document_.scene());
    std::vector<RenderModelInstance> instances;
    struct FramePins { std::vector<std::shared_ptr<const PreparedEditorModel>> models; };
    auto pins=std::make_shared<FramePins>();std::uint64_t signature=14695981039346656037ull;
    for(const auto& model:extracted.models) {
        const bool selected=std::find(document_.selection().ids.begin(),document_.selection().ids.end(),model.id)!=document_.selection().ids.end();
        if(!selected) if(const auto metadata=assets.findModel(model.assetId))
            if(!visibleBounds(metadata->bounds,model.world,settings_.camera,std::max(1.0f,frame.viewport.width)/std::max(1.0f,frame.viewport.height)))continue;
        if(auto prepared=prepared_.prepare(model.assetId,model.world,settings_.camera,static_cast<std::uint32_t>(std::max(1.0f,frame.viewport.height)),selected)) {
            instances.push_back({model.id,{prepared->asset,model.world},&prepared->textures,prepared->revision,selected,true,&prepared->materials});
            pins->models.push_back(prepared);signature=(signature^prepared->signature)*1099511628211ull;
        }
    }
    for(const auto& id:frame.placementPreviewIds) if(auto prepared=prepared_.prepare(id,math::translation(placement(frame.placementPreviewPosition)),settings_.camera,
        static_cast<std::uint32_t>(std::max(1.0f,frame.viewport.height)),false,ResourcePriority::Placement)) {
        instances.push_back({0,{prepared->asset,math::translation(placement(frame.placementPreviewPosition))},&prepared->textures,prepared->revision,false,false,&prepared->materials});pins->models.push_back(prepared);
    }
    renderer.setExternalResourceSignature(signature);
    renderer.setAppearance(editor_.appearance());
    if (frame.viewport.width > 0 && frame.viewport.height > 0)
        renderer.render(RenderScene{settings_,{},nullptr,nullptr,instances,extracted.directional,extracted.points,true,pins});
    visiblePreviewIds_=frame.visiblePreviewIds;viewportRegion_=frame.viewport;
    if(!document_.assetId().empty() && !document_.dirty()) try {
        const auto fingerprint=sceneFileFingerprint(root_,document_.assetId());
        if(fingerprint!=savedPreviewFingerprint_) { savedPreviewFingerprint_=fingerprint;previews_->requestSceneSave(document_.assetId()); }
    } catch(const std::exception& error){status_=error.what();}
    prepared_.endFrame();
    const auto retained=renderer.modelResidentBytes()+renderer.thumbnailResidentBytes();
    assets.trimTextureCache(retained>=resources_.config().gpuRetentionBytes?0:resources_.config().gpuRetentionBytes-retained);
    if(!prepared_.status().empty())status_+=prepared_.status();
    if(!previews_->status().empty())status_+=previews_->status();
    if(environment_.state()==ResourceState::Failed)status_+=environment_.diagnostic();
    if(!resourceStatus_.empty()) { if(!status_.empty())status_+='\n';status_+=resourceStatus_; }
    drawSceneDiagnostics(status_,renderer.pickingStatus(),renderer.modelStatus());
}
}
