#include "EditorSession.h"
#include "input/Navigation.h"
#include "scene/SceneExtraction.h"
#include "assets/AtomicFile.h"
#include "math/Json.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <fstream>

namespace crimson {
using Json=nlohmann::json;
EditorSession::EditorSession(const std::filesystem::path& root, AssetLibrary& assets, Renderer& renderer)
    : root_(root),document_(root) {
    restoreSession(assets);
    if (!renderer.prepareEnvironment(root_,{})) status_=renderer.environmentStatus();
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
            SceneProperties properties; assets.loadEnvironment(properties.environment); document_.setProperties(properties,"Recover preview environment");
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
            {"recoveredLegacyPreview",recoveredLegacy_},{"cameras",std::move(cameras)}}.dump(2));
    } catch (...) { /* Session state is expendable; authored scenes were saved explicitly. */ }
}
bool EditorSession::prepare(const std::string& id, AssetLibrary& assets, Renderer& renderer) {
    if (bindings_.contains(id)) return true;
    try {
        const auto asset=assets.findModel(id);
        if (!asset) throw std::runtime_error("Missing model asset: " + id);
        auto textures=assets.modelBindings(*asset);
        if (!renderer.prepareModel(*asset)) throw std::runtime_error(renderer.modelStatus());
        bindings_.emplace(id,std::move(textures));
        return true;
    } catch (const std::exception& error) { status_=error.what(); return false; }
}
void EditorSession::cancelInteraction(Renderer& renderer) { editor_.cancelInteraction(document_,renderer); }
void EditorSession::frame(AssetLibrary& assets, Renderer& renderer, Navigation& navigation, SDL_Window* window,
                          uint32_t bgfxFrame, bool quitRequested) {
    const auto previousAsset=document_.assetId();
    const auto previousCamera=settings_.camera;
    const auto frame=editor_.draw(document_,settings_,assets,renderer,window,navigation.active(),bgfxFrame,quitRequested);
    status_=startupStatus_;
    if (frame.documentReplaced || document_.assetId() != cameraScene_) {
        if (!previousAsset.empty()) cameras_[previousAsset]=previousCamera;
        cameraScene_=document_.assetId(); navigation.cancel(); renderer.cancelPicking();
        unresolvedScene_.clear(); startupStatus_.clear(); status_.clear();
        if (frame.documentReplaced) {
            settings_.camera=Camera{};
            if (const auto found=cameras_.find(cameraScene_);found!=cameras_.end()) settings_.camera=found->second;
        }
        saveSession();
    }
    auto dropped=frame.modelDropIds;
    if (!frame.modelDropId.empty()) dropped.push_back(frame.modelDropId);
    if (!dropped.empty()) {
        auto position=settings_.camera.target; position[1]=0;
        if (const auto ray=settings_.camera.screenRay(frame.modelDropPosition[0],frame.modelDropPosition[1],frame.viewport))
            if (const auto hit=math::intersectPlane(*ray,{0,0,0},{0,1,0})) position=*hit;
        std::vector<ModelPlacement> placements;
        for (const auto& id : dropped) if (prepare(id,assets,renderer)) {
            const auto model=assets.findModel(id);
            placements.push_back({id,assets.displayName(id),math::translation(position)});
        }
        document_.createModels(placements); renderer.cancelPicking();
    }
    navigation.update(settings_.camera,frame.viewport,ImGui::GetIO().DeltaTime,false);
    const auto& properties=document_.scene().properties();
    settings_.environment=properties.environment; settings_.exposure=properties.exposure;
    if (preparedEnvironment_ != properties.environment.map) {
        renderer.prepareEnvironment(root_,properties.environment.map); preparedEnvironment_=properties.environment.map;
    }
    const auto extracted=extractScene(document_.scene());
    std::vector<RenderModelInstance> instances;
    for (const auto& model : extracted.models) if (prepare(model.assetId,assets,renderer))
        instances.push_back({model.id,{assets.findModel(model.assetId),model.world},&bindings_.at(model.assetId)});
    if (frame.viewport.width > 0 && frame.viewport.height > 0)
        renderer.render(RenderScene{settings_,{},nullptr,nullptr,instances,extracted.directional,extracted.points,true});
    if (!status_.empty()) { ImGui::Begin("Scene diagnostics"); ImGui::TextWrapped("%s",status_.c_str()); ImGui::End(); }
    if (!renderer.pickingStatus().empty() || !renderer.modelStatus().empty()) {
        ImGui::Begin("Scene diagnostics");
        if (!renderer.pickingStatus().empty()) ImGui::TextWrapped("%s",renderer.pickingStatus().c_str());
        if (!renderer.modelStatus().empty()) ImGui::TextWrapped("%s",renderer.modelStatus().c_str());
        ImGui::End();
    }
}
}
