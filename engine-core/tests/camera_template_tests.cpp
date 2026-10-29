#include "scene/CameraTemplates.h"
#include "assets/AtomicFile.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action> void rejects(Action&& action,const char* message) {
    bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}
    require(rejected,message);
}
struct TempProject {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("crimson-camera-template-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TempProject() {
        std::error_code error;
        const auto target=std::filesystem::weakly_canonical(root,error);
        const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-camera-template-tests-"))
            std::filesystem::remove_all(target,error);
    }
};
std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void defaultsAndValidation() {
    auto value=defaultPlayerCameraTemplate();
    require(value.name=="First Person"&&value.offset==Vec3{0,1.65f,0}&&value.lens==SceneCamera{75,.05f,2048},
        "Built-in first-person camera did not retain eye height, clipping and field of view.");
    const auto rotation=cameraTemplateRotationDegrees(value);
    require(rotation==Vec3{}&&value.worldBasis==Mat4{},"Built-in first-person camera did not look straight along -Z.");
    require(validCameraTemplate(value),"Built-in first-person camera failed validation.");
    value.name.clear();require(validCameraTemplate(value),"Unnamed live camera snapshots were rejected.");
    const auto unchanged=value;
    require(!setCameraTemplateRotationDegrees(value,{std::numeric_limits<float>::infinity(),0,0})&&value==unchanged,
        "Invalid rotation mutated the template.");
    require(setCameraTemplateRotationDegrees(value,{-40,23,19})&&validCameraTemplate(value),"Finite rotation could not be authored.");
    const auto edited=cameraTemplateRotationDegrees(value);
    require(std::abs(edited[0]+40)<.001f&&std::abs(edited[1]-23)<.001f&&std::abs(edited[2]-19)<.001f,
        "Template rotation authoring lost the roll component.");
    auto invalid=value;invalid.offset[0]=std::numeric_limits<float>::quiet_NaN();require(!validCameraTemplate(invalid),"Nonfinite offset was accepted.");
    invalid=value;invalid.worldBasis.at(0,3)=1;require(!validCameraTemplate(invalid),"Template basis retained a second translation.");
    invalid=value;invalid.worldBasis=math::scaling({0,1,1});require(!validCameraTemplate(invalid),"Singular camera basis was accepted.");
    invalid=value;invalid.worldBasis.at(3,0)=.1f;require(!validCameraTemplate(invalid),"Non-affine camera basis was accepted.");
    invalid=value;invalid.lens.verticalFov=180;require(!validCameraTemplate(invalid),"Invalid FOV was accepted.");
    invalid=value;invalid.lens.nearPlane=0;require(!validCameraTemplate(invalid),"Nonpositive near clipping was accepted.");
    invalid=value;invalid.lens.farPlane=invalid.lens.nearPlane;require(!validCameraTemplate(invalid),"Reversed clipping interval was accepted.");
}
void captureFullWorldPlacement() {
    Scene scene;EntityRecord actor;actor.id=1;actor.name="Actor";actor.actor.emplace();
    actor.local=math::translation({11,3,-7})*math::eulerRotationXYZ({.2f,.7f,-.1f})*math::scaling({-1.5f,2.f,.75f});
    EntityRecord camera;camera.id=2;camera.parent=actor.id;camera.name="Camera";camera.camera=SceneCamera{48,.03f,6000};
    camera.local=math::translation({4,9,2})*math::eulerRotationXYZ({-.8f,.5f,.25f});camera.local.at(0,1)+=.125f;
    scene.apply({actor,camera},{});
    const auto world=*scene.worldTransform(camera.id),target=*scene.worldTransform(actor.id);
    const auto generation=scene.generation(),revision=scene.revision();
    const auto captured=captureCameraTemplate(scene,camera.id,actor.id,"  Dramatic Roll  ");
    require(captured&&captured->name=="Dramatic Roll"&&captured->lens==*camera.camera,
        "Capture lost the authored name or lens settings.");
    require(captured->offset==math::transformPoint(world,{})-math::transformPoint(target,{})&&
        captured->worldBasis==*math::withAffinePosition(world,{}),"Capture approximated a parented camera instead of preserving the full world pose.");
    require(math::handedness(captured->worldBasis)<0,"Capture discarded the signed affine camera basis.");
    const auto absolute=captureCameraTemplate(scene,camera.id);
    require(absolute&&absolute->offset==math::transformPoint(world,{})&&absolute->name.empty(),
        "Unfollowed camera capture did not retain absolute world placement.");
    require(!captureCameraTemplate(scene,actor.id,actor.id)&&!captureCameraTemplate(scene,camera.id,999),
        "Capture accepted an absent target or a non-camera entity.");
    require(scene.generation()==generation&&scene.revision()==revision,"Capturing a template modified the scene.");
    TempProject project;std::vector<CameraTemplate> templates;
    saveCameraTemplate(project.root,templates,*captured);
    require(loadCameraTemplates(project.root)==templates&&templates.front()==*captured,
        "Persistence lost a camera's full affine world basis, clipping interval or roll.");
    auto followed=camera;followed.cameraFollow=SceneCameraFollow{actor.id,captured->offset,captured->worldBasis};
    scene.apply({actor,followed},{});
    const auto canonical=captureCameraTemplate(scene,camera.id,actor.id,captured->name);
    require(canonical&&*canonical==*captured,"Following-camera capture introduced parent roundoff into authored template identification.");
}
void persistenceAndFailurePreservation() {
    TempProject project;
    require(loadCameraTemplates(project.root).empty()&&!std::filesystem::exists(cameraTemplatesPath(project.root)),
        "Reading missing camera templates created a project file.");
    auto value=defaultPlayerCameraTemplate();value.name="  Close Camera  ";
    std::vector<CameraTemplate> templates;saveCameraTemplate(project.root,templates,value);
    require(templates.size()==1&&templates.front().name=="Close Camera", "Template saving did not normalize its name.");
    require(cameraTemplatesPath(project.root)==project.root/"Content"/"camera-templates.json"&&loadCameraTemplates(project.root)==templates,
        "Named templates were not persisted under Content.");
    value.name="CLOSE camera";value.offset={4,8,4};value.lens={32,.05f,4096};
    saveCameraTemplate(project.root,templates,value);
    require(templates.size()==1&&templates.front()==value&&loadCameraTemplates(project.root)==templates,
        "Case-insensitive save created duplicate named templates or failed to replace settings.");
    auto second=value;second.name="Wide Camera";second.offset={8,16,8};saveCameraTemplate(project.root,templates,second);
    require(templates.size()==2&&loadCameraTemplates(project.root)==templates,"Saving a new template replaced an unrelated one.");
    const auto before=templates;const auto text=readText(cameraTemplatesPath(project.root));
    auto invalid=value;invalid.name=" \t ";rejects([&]{saveCameraTemplate(project.root,templates,invalid);},"An empty template name was saved.");
    invalid=value;invalid.name="first person";rejects([&]{saveCameraTemplate(project.root,templates,invalid);},"A user template duplicated the built-in First Person name.");
    invalid=value;invalid.lens.verticalFov=std::numeric_limits<float>::infinity();
    rejects([&]{saveCameraTemplate(project.root,templates,invalid);},"Nonfinite template settings were saved.");
    require(templates==before&&readText(cameraTemplatesPath(project.root))==text,"Rejected save changed memory or the previous template file.");
#ifdef _WIN32
    const auto file=CreateFileW(cameraTemplatesPath(project.root).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(file!=INVALID_HANDLE_VALUE,"Could not lock the template publication fixture.");
    bool rejected=false;
    try{auto replacement=value;replacement.offset={1,2,3};saveCameraTemplate(project.root,templates,replacement);}catch(const std::exception&){rejected=true;}
    CloseHandle(file);
    require(rejected&&templates==before&&readText(cameraTemplatesPath(project.root))==text,
        "Failed atomic publication changed memory or destroyed the last saved camera templates.");
#endif
    auto json=nlohmann::json::parse(text);json["templates"].push_back(json.at("templates").at(0));
    writeFileAtomically(cameraTemplatesPath(project.root),json.dump());
    rejects([&]{templates=loadCameraTemplates(project.root);},"Duplicate persisted template names were accepted.");
    require(templates==before,"Failed loading replaced the current in-memory camera templates.");
    json=nlohmann::json::parse(text);json["version"]=2;writeFileAtomically(cameraTemplatesPath(project.root),json.dump());
    rejects([&]{templates=loadCameraTemplates(project.root);},"An unsupported template version was accepted.");
    require(templates==before,"Unsupported-version load changed in-memory camera templates.");
    writeFileAtomically(cameraTemplatesPath(project.root),std::string(1024*1024+1,'x'));
    rejects([&]{templates=loadCameraTemplates(project.root);},"An unbounded camera template file was read.");
    require(templates==before,"Oversized-file load changed in-memory camera templates.");
    // First Person was a valid custom name before this built-in was introduced.
    // Keep those authored settings, including their ability to be updated.
    json=nlohmann::json::parse(text);json["templates"][0]["name"]="First Person";
    writeFileAtomically(cameraTemplatesPath(project.root),json.dump());templates=loadCameraTemplates(project.root);
    require(templates.front().name=="First Person"&&templates.front().offset==before.front().offset&&templates.front().lens==before.front().lens,
        "Replacing the built-in camera template discarded an existing user template.");
    auto existing=templates.front();existing.lens.verticalFov=63;saveCameraTemplate(project.root,templates,existing);
    require(loadCameraTemplates(project.root)==templates&&templates.front()==existing,
        "A preexisting user template could not be edited after the built-in template changed.");
}
}
int main() {
    try {
        defaultsAndValidation();captureFullWorldPlacement();persistenceAndFailurePreservation();
        std::cout<<"First-person camera defaults, full-world capture, named templates and atomic failure preservation passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
