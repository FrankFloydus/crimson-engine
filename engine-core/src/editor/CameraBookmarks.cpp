#include "CameraBookmarks.h"
#include "assets/AtomicFile.h"
#include "resources/AssetId.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>

namespace crimson {
namespace {
using Json=nlohmann::json;
std::string cleanName(std::string name) {
    const auto first=name.find_first_not_of(" \t\r\n"),last=name.find_last_not_of(" \t\r\n");
    return first==std::string::npos?std::string{}:name.substr(first,last-first+1);
}
std::string folded(std::string name) {
    for(auto& c:name)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return name;
}
bool nameValid(const std::string& name) {
    return !name.empty()&&name.size()<=CameraBookmarks::MaximumNameLength&&
        std::none_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;});
}
Json cameraJson(const Camera& camera) {
    return {{"position",camera.position},{"target",camera.target},{"up",camera.up},
        {"fov",camera.fov},{"near",camera.nearPlane},{"far",camera.farPlane},
        {"fovAxis",camera.fovAxis==CameraFovAxis::Vertical?"vertical":"shorter"}};
}
Camera readCamera(const Json& value) {
    Camera camera;
    camera.position=value.at("position").get<Vec3>();camera.target=value.at("target").get<Vec3>();camera.up=value.at("up").get<Vec3>();
    camera.fov=value.at("fov").get<float>();camera.nearPlane=value.at("near").get<float>();camera.farPlane=value.at("far").get<float>();
    const auto axis=value.value("fovAxis",std::string{"shorter"});
    if(axis!="shorter"&&axis!="vertical")throw std::runtime_error("Invalid bookmark field of view axis.");
    camera.fovAxis=axis=="vertical"?CameraFovAxis::Vertical:CameraFovAxis::ShorterDimension;
    if(!CameraBookmarks::validCamera(camera))throw std::runtime_error("Invalid bookmark camera.");
    return camera;
}
}
CameraBookmarks::CameraBookmarks(const std::filesystem::path& projectRoot):path_(projectRoot/".crimson"/"camera-bookmarks.json") {reload();}
bool CameraBookmarks::validCamera(const Camera& camera) {
    return math::finite(camera.position)&&math::finite(camera.target)&&math::finite(camera.up)&&
        std::isfinite(camera.fov)&&camera.fov>=1&&camera.fov<=179&&
        std::isfinite(camera.nearPlane)&&std::isfinite(camera.farPlane)&&camera.nearPlane>0&&camera.farPlane>camera.nearPlane&&
        math::length(camera.target-camera.position)>1e-6&&math::length(math::cross(camera.target-camera.position,camera.up))>1e-6&&
        (camera.fovAxis==CameraFovAxis::ShorterDimension||camera.fovAxis==CameraFovAxis::Vertical);
}
bool CameraBookmarks::reload() {
    try {
        Scenes loaded;std::uint64_t next=1;std::optional<std::string> text;
        if(std::filesystem::exists(path_)) {
            std::ifstream input(path_,std::ios::binary);
            if(!input)throw std::runtime_error("Cannot read camera bookmarks.");
            text=std::string(std::istreambuf_iterator<char>{input},{});const auto document=Json::parse(*text);
            if(document.at("version")!=1||!document.at("scenes").is_object())throw std::runtime_error("Unsupported camera bookmarks file.");
            std::set<std::uint64_t> ids;
            for(const auto& [scene,entries]:document.at("scenes").items()) {
                if(!isAssetUuid(scene)||!entries.is_array()||entries.size()>MaximumPerScene)throw std::runtime_error("Invalid camera bookmark scene.");
                auto& views=loaded[scene];std::set<std::string> names;
                for(const auto& entry:entries) {
                    const auto& rawId=entry.at("id");
                    if(!rawId.is_number_unsigned())throw std::runtime_error("Invalid camera bookmark identity.");
                    const auto id=rawId.get<std::uint64_t>();const auto name=entry.at("name").get<std::string>();
                    if(!id||id==std::numeric_limits<std::uint64_t>::max()||!ids.insert(id).second||!nameValid(name)||
                       cleanName(name)!=name||!names.insert(folded(name)).second)throw std::runtime_error("Invalid or duplicate camera bookmark.");
                    next=std::max(next,id+1);views.push_back({id,name,readCamera(entry.at("camera"))});
                }
            }
            if(document.contains("nextId")) {
                if(!document.at("nextId").is_number_unsigned()||document.at("nextId").get<std::uint64_t>()<next)
                    throw std::runtime_error("Invalid camera bookmark identity counter.");
                next=document.at("nextId").get<std::uint64_t>();
            }
        }
        scenes_=std::move(loaded);nextId_=next;publishedText_=std::move(text);status_.clear();writable_=true;return true;
    } catch(const std::exception& error) {status_=error.what();writable_=false;return false;}
}
std::span<const CameraBookmark> CameraBookmarks::views(const std::string& scene) const {
    const auto found=scenes_.find(scene);return found==scenes_.end()?std::span<const CameraBookmark>{}:found->second;
}
std::optional<Camera> CameraBookmarks::recall(const std::string& scene,std::uint64_t id) const {
    for(const auto& view:views(scene))if(view.id==id)return view.camera;
    return std::nullopt;
}
bool CameraBookmarks::checkScene(const std::string& scene) {
    if(!writable_)return false;
    if(!isAssetUuid(scene)){status_="Save the scene before adding bookmarks.";return false;}
    return true;
}
bool CameraBookmarks::checkName(const std::vector<CameraBookmark>& views,std::uint64_t id,std::string& name) {
    name=cleanName(std::move(name));
    if(!nameValid(name)){status_="Use a name from 1 to 96 bytes without control characters.";return false;}
    const auto key=folded(name);
    if(std::any_of(views.begin(),views.end(),[&](const auto& entry){return entry.id!=id&&folded(entry.name)==key;})) {
        status_="A bookmark already has this name.";return false;
    }
    return true;
}
bool CameraBookmarks::publish(Scenes candidate,std::uint64_t next) {
    try {
        std::optional<std::string> currentText;
        if(std::filesystem::exists(path_)) {
            std::ifstream input(path_,std::ios::binary);
            if(!input)throw std::runtime_error("Cannot read camera bookmarks.");
            currentText=std::string(std::istreambuf_iterator<char>{input},{});
        }
        if(currentText!=publishedText_) {
            writable_=false;throw std::runtime_error("Camera bookmarks changed outside this editor. Reload before editing.");
        }
        Json scenes=Json::object();
        for(const auto& [scene,views]:candidate) {
            auto entries=Json::array();for(const auto& view:views)entries.push_back({{"id",view.id},{"name",view.name},{"camera",cameraJson(view.camera)}});
            if(!entries.empty())scenes[scene]=std::move(entries);
        }
        const auto text=Json{{"version",1},{"nextId",next},{"scenes",std::move(scenes)}}.dump(2);
        writeFileAtomically(path_,text);
        scenes_=std::move(candidate);nextId_=next;publishedText_=text;status_.clear();return true;
    } catch(const std::exception& error){status_=error.what();return false;}
}
bool CameraBookmarks::add(const std::string& scene,std::string name,const Camera& camera) {
    if(!checkScene(scene))return false;
    if(!validCamera(camera)){status_="The current camera cannot be bookmarked.";return false;}
    auto candidate=scenes_;auto& entries=candidate[scene];
    if(entries.size()>=MaximumPerScene||nextId_==std::numeric_limits<std::uint64_t>::max()){status_="This scene has reached its bookmark limit.";return false;}
    if(!checkName(entries,0,name))return false;
    entries.push_back({nextId_,std::move(name),camera});return publish(std::move(candidate),nextId_+1);
}
bool CameraBookmarks::update(const std::string& scene,std::uint64_t id,const Camera& camera) {
    if(!checkScene(scene))return false;
    if(!validCamera(camera)){status_="The current camera cannot be bookmarked.";return false;}
    auto candidate=scenes_;const auto found=candidate.find(scene);
    if(found!=candidate.end())for(auto& entry:found->second)if(entry.id==id){entry.camera=camera;return publish(std::move(candidate),nextId_);}
    status_="Bookmark unavailable.";return false;
}
bool CameraBookmarks::rename(const std::string& scene,std::uint64_t id,std::string name) {
    if(!checkScene(scene))return false;
    auto candidate=scenes_;const auto found=candidate.find(scene);
    if(found!=candidate.end()) {
        if(!checkName(found->second,id,name))return false;
        for(auto& entry:found->second)if(entry.id==id){entry.name=std::move(name);return publish(std::move(candidate),nextId_);}
    }
    status_="Bookmark unavailable.";return false;
}
bool CameraBookmarks::remove(const std::string& scene,std::uint64_t id) {
    if(!checkScene(scene))return false;
    auto candidate=scenes_;const auto found=candidate.find(scene);
    if(found!=candidate.end()&&std::erase_if(found->second,[&](const auto& entry){return entry.id==id;}))return publish(std::move(candidate),nextId_);
    status_="Bookmark unavailable.";return false;
}
}
