#include "editor/CameraBookmarks.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
const std::string SceneA="4a31790d-fb43-4d51-9a20-a8f5a2f7a91a",SceneB="923a2d16-39e8-4575-9993-44c9707fa0c8";
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-bookmarks-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&path.parent_path()==temp&&path.filename().string().starts_with("crimson-bookmarks-"))std::filesystem::remove_all(path,error);}
    std::filesystem::path file()const{return root/".crimson"/"camera-bookmarks.json";}
};
std::string text(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>{input},{}};}
void write(const std::filesystem::path& path,const std::string& value){std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file<<value;}
bool same(const Camera& a,const Camera& b){return a.position==b.position&&a.target==b.target&&a.up==b.up&&a.fov==b.fov&&a.nearPlane==b.nearPlane&&a.farPlane==b.farPlane&&a.fovAxis==b.fovAxis;}
Camera view(){Camera camera;camera.position={12,4,-7};camera.target={2,6,3};camera.up={.1f,1,.2f};camera.fov=73;camera.nearPlane=.02f;camera.farPlane=4096;camera.fovAxis=CameraFovAxis::Vertical;return camera;}
void persistenceAndIdentity() {
    Project project;const auto authored=project.root/"Content"/"untouched.crimson-scene";write(authored,"authored scene sentinel");
    CameraBookmarks bookmarks(project.root);check(bookmarks.writable()&&bookmarks.views(SceneA).empty()&&!std::filesystem::exists(project.file()),"Loading empty bookmarks wrote metadata.");
    const auto camera=view();check(!bookmarks.add({},"Unsaved",camera)&&!bookmarks.add(SceneA,"  ",camera),"Invalid scene or empty name was accepted.");
    check(bookmarks.add(SceneA,"  Room Entrance  ",camera),bookmarks.status());
    const auto id=bookmarks.views(SceneA).front().id;check(id&&bookmarks.views(SceneA).front().name=="Room Entrance","Bookmark name or identity was lost.");
    const auto accepted=text(project.file());
    check(!bookmarks.add(SceneA,"ROOM ENTRANCE",camera)&&text(project.file())==accepted,"Duplicate name changed the stored file.");
    check(bookmarks.add(SceneB,"Room Entrance",camera),bookmarks.status());
    CameraBookmarks loaded(project.root);check(same(*loaded.recall(SceneA,id),camera),"Reload lost the precise editor camera and lens.");
    check(!loaded.recall(SceneB,id),"Bookmark identity crossed scene ownership.");
    auto moved=camera;moved.position=moved.position+Vec3{3,2,1};moved.target=moved.target+Vec3{3,2,1};
    check(loaded.update(SceneA,id,moved)&&loaded.rename(SceneA,id,"Upper Hall"),loaded.status());
    check(loaded.views(SceneA).front().id==id&&same(*loaded.recall(SceneA,id),moved),"Updating or renaming changed identity or pose.");
    check(loaded.remove(SceneA,id)&&!loaded.recall(SceneA,id)&&loaded.views(SceneB).size()==1,"Delete crossed scene scope or retained the view.");
    CameraBookmarks reopened(project.root);check(reopened.add(SceneA,"Replacement",camera),reopened.status());
    check(reopened.views(SceneA).front().id>id,"A deleted bookmark identity was reused after reopening.");
    check(text(authored)=="authored scene sentinel","Bookmark editing changed an authored scene.");
}
void invalidAndConflictingData() {
    Project project;CameraBookmarks bookmarks(project.root);auto camera=view();
    camera.up=camera.target-camera.position;check(!bookmarks.add(SceneA,"Parallel",camera),"Degenerate camera basis was accepted.");
    camera=view();camera.fov=std::numeric_limits<float>::quiet_NaN();check(!bookmarks.add(SceneA,"NaN",camera),"Nonfinite camera was accepted.");
    camera=view();check(bookmarks.add(SceneA,"First",camera),bookmarks.status());
    const auto id=bookmarks.views(SceneA).front().id;CameraBookmarks other(project.root);
    check(other.add(SceneA,"Second",camera),other.status());const auto external=text(project.file());
    check(!bookmarks.rename(SceneA,id,"Clobber")&&!bookmarks.writable()&&text(project.file())==external,"A stale owner overwrote external bookmarks.");
    check(bookmarks.views(SceneA).size()==1&&bookmarks.views(SceneA).front().name=="First","Rejected write changed the in-memory view.");
    check(bookmarks.reload()&&bookmarks.views(SceneA).size()==2&&bookmarks.rename(SceneA,id,"Renamed"),bookmarks.status());
    auto bad=nlohmann::json::parse(text(project.file()));bad["scenes"][SceneA][1]["id"]=bad["scenes"][SceneA][0]["id"];write(project.file(),bad.dump());
    const auto malformed=text(project.file());check(!bookmarks.reload()&&!bookmarks.writable(),"Malformed duplicate identity was accepted.");
    check(!bookmarks.add(SceneA,"Lost",camera)&&text(project.file())==malformed&&bookmarks.views(SceneA).size()==2,"Malformed metadata was overwritten or erased retained views.");
}
void capacityAndWriteFailure() {
    Project project;CameraBookmarks bookmarks(project.root);const auto camera=view();
    for(unsigned i=0;i<CameraBookmarks::MaximumPerScene;++i)check(bookmarks.add(SceneA,"View "+std::to_string(i),camera),bookmarks.status());
    const auto accepted=text(project.file());check(!bookmarks.add(SceneA,"Overflow",camera)&&text(project.file())==accepted,"Scene bookmark capacity was not bounded atomically.");
    Project blocked;CameraBookmarks failed(blocked.root);std::filesystem::create_directories(blocked.file());
    check(!failed.add(SceneA,"Failure",camera)&&failed.views(SceneA).empty()&&std::filesystem::is_directory(blocked.file()),"A failed write published a partial view.");
}
}
int main(){try{persistenceAndIdentity();invalidAndConflictingData();capacityAndWriteFailure();std::cout<<"Camera bookmarks: exact views, scene ownership, persistence, atomic failures, external conflicts and bounds passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
