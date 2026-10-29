#include "LevelProjectState.h"
#include "assets/AtomicFile.h"
#include "math/Json.h"
#include <fstream>
#include <stdexcept>

namespace crimson::integration {
namespace {
using Json=nlohmann::json;
bool validCamera(const Camera& camera){return math::finite(camera.position)&&math::finite(camera.target)&&math::finite(camera.up)&&math::finite(camera.fov)&&math::finite(camera.nearPlane)&&math::finite(camera.farPlane)&&camera.fov>0&&camera.fov<180&&camera.nearPlane>0&&camera.farPlane>camera.nearPlane&&math::length(camera.target-camera.position)>1e-6f&&math::length(math::cross(camera.target-camera.position,camera.up))>1e-6f&&(camera.fovAxis==CameraFovAxis::Vertical||camera.fovAxis==CameraFovAxis::ShorterDimension);}
Json cameraValue(const Camera& camera){return {{"position",camera.position},{"target",camera.target},{"up",camera.up},{"fov",camera.fov},{"near",camera.nearPlane},{"far",camera.farPlane},{"fovAxis",camera.fovAxis==CameraFovAxis::Vertical?"vertical":"shorter"}};}
std::optional<Camera> cameraFrom(const Json& value){
    try{
        Camera camera;camera.position=value.at("position").get<Vec3>();camera.target=value.at("target").get<Vec3>();camera.up=value.at("up").get<Vec3>();
        camera.fov=value.value("fov",45.f);camera.nearPlane=value.value("near",.01f);camera.farPlane=value.value("far",2048.f);
        const auto axis=value.value("fovAxis",std::string{"shorter"});if(axis!="shorter"&&axis!="vertical")return {};
        camera.fovAxis=axis=="vertical"?CameraFovAxis::Vertical:CameraFovAxis::ShorterDimension;
        if(!validCamera(camera))return {};
        return camera;
    }catch(const std::exception&){return {};}
}
}
std::optional<std::filesystem::path> findLevelProjectRoot(const std::filesystem::path& start){
    std::error_code error;auto path=std::filesystem::absolute(start,error);if(error)return {};
    if(!std::filesystem::is_directory(path,error))path=path.parent_path();
    while(!path.empty()){
        if(std::filesystem::is_directory(path/"Content",error)||std::filesystem::is_regular_file(path/".crimson/editor-session.json",error)||std::filesystem::is_regular_file(path/"assets/editor-session.json",error)||std::filesystem::is_regular_file(path/"assets/browser.json",error))return path.lexically_normal();
        const auto parent=path.parent_path();if(parent==path)break;path=parent;
    }
    return {};
}
std::optional<std::filesystem::path> readLevelProjectSelection(const std::filesystem::path& file){
    try{std::ifstream input(file);if(!input)return {};const auto value=Json::parse(input);if(value.at("version")!=1)return {};const auto text=value.at("root").get<std::string>();const auto root=std::filesystem::u8path(text);if(root.empty()||!std::filesystem::is_directory(root))return {};return std::filesystem::absolute(root).lexically_normal();}catch(const std::exception&){return {};}
}
void writeLevelProjectSelection(const std::filesystem::path& file,const std::filesystem::path& root){const auto bytes=std::filesystem::absolute(root).lexically_normal().u8string();writeFileAtomically(file,Json{{"version",1},{"root",std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size())}}.dump(2));}
LevelProjectState::LevelProjectState(const std::filesystem::path& root):path_(root/".crimson/editor-session.json"){
    try{
        auto source=path_;if(!std::filesystem::exists(source))source=root/"assets/editor-session.json";
        std::ifstream input(source);if(!input)return;saved_=Json::parse(input);
        if(saved_.at("version")!=1)throw std::runtime_error("Unsupported editor session version.");
        scene_=saved_.value("scene",saved_.value("activeScene",std::string{}));
        if(saved_.contains("cameras")&&saved_["cameras"].is_object())for(const auto& [id,value]:saved_["cameras"].items())if(const auto camera=cameraFrom(value))cameras_.emplace(id,*camera);
    }catch(const std::exception& error){status_=error.what();writable_=false;}
}
std::optional<Camera> LevelProjectState::camera(const std::string& id)const{const auto found=cameras_.find(id);return found==cameras_.end()?std::nullopt:std::optional{found->second};}
void LevelProjectState::remember(const std::string& id,const Camera& camera){if(id.empty()||!validCamera(camera))return;const auto found=cameras_.find(id);if(found==cameras_.end()||found->second.position!=camera.position||found->second.target!=camera.target||found->second.up!=camera.up||found->second.fov!=camera.fov||found->second.nearPlane!=camera.nearPlane||found->second.farPlane!=camera.farPlane||found->second.fovAxis!=camera.fovAxis){cameras_[id]=camera;changed_=true;}}
bool LevelProjectState::save(const std::string& active){
    if(!writable_)return false;if(!changed_&&active==scene_)return true;
    try{auto candidate=saved_;candidate["version"]=1;candidate["scene"]=active;candidate.erase("activeScene");auto cameras=Json::object();for(const auto& [id,camera]:cameras_)cameras[id]=cameraValue(camera);candidate["cameras"]=std::move(cameras);writeFileAtomically(path_,candidate.dump(2));saved_=std::move(candidate);scene_=active;changed_=false;status_.clear();return true;}catch(const std::exception& error){status_=error.what();return false;}
}
}
