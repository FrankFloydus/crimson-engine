#include "QuaderReader.h"
#include "assets/ModelImportIdentity.h"
#include "resources/Paths.h"
#include <quader_bridge_reader.hpp>
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace crimson {
namespace {
using Json=nlohmann::json;
void require(bool valid,const std::string& message) { if(!valid)throw std::runtime_error(message); }
std::string string(const char* value) { require(value!=nullptr,"Quader reader returned an invalid string.");return value; }
bool uuid(const std::string& value) {
    if(value.size()!=36)return false;
    for(std::size_t i=0;i<value.size();++i)if(i==8||i==13||i==18||i==23){if(value[i]!='-')return false;}
    else if(!((value[i]>='a'&&value[i]<='f')||(value[i]>='0'&&value[i]<='9')))return false;
    return true;
}
bool relative(const std::string& value) {
    if(value.empty()||value.find_first_of("\\:\0",0,3)!=std::string::npos)return false;
    const auto p=pathFromUtf8(value);if(p.has_root_path())return false;
    for(const auto& part:p)if(part=="."||part=="..")return false;
    return true;
}
std::string pathText(const std::filesystem::path& value) {
    const auto bytes=value.generic_u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
void result(int success,const QuaderBridgeError& error) {
    if(success)return;
    const auto end=std::find(std::begin(error.message),std::end(error.message),'\0');
    throw std::runtime_error(end==std::begin(error.message)?"Quader reader failed.":std::string(std::begin(error.message),end));
}
struct Library {
    HMODULE module{};const QuaderBridgeApi* api{};
    Library() {
        std::wstring executable(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        require(length>0&&length<executable.size(),"Cannot locate Crimson's Quader reader payload.");executable.resize(length);
        auto file=std::filesystem::path(executable).parent_path()/L"quader-reader"/L"quader_bridge_reader.dll";file.make_preferred();
        module=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module){const auto code=GetLastError();throw std::runtime_error("Cannot load bundled Quader reader '"+pathText(file)+"': Win32 "+std::to_string(code)+" ("+std::system_category().message(static_cast<int>(code))+"). Rebuild or reinstall the complete Crimson payload.");}
        const auto get=reinterpret_cast<decltype(&quader_bridge_get_api)>(GetProcAddress(module,"quader_bridge_get_api"));
        api=get?get(QUADER_BRIDGE_ABI_VERSION):nullptr;
        if(!api||api->abi_version!=QUADER_BRIDGE_ABI_VERSION||api->struct_size<sizeof(QuaderBridgeApi)||
            !api->open_project||!api->release_project||!api->project_view||!api->scene_entry||!api->open_scene||!api->release_scene||
            !api->scene_view||!api->mesh_view||!api->material_identity||!api->material_view||!api->read_texture||!api->blob_view||
            !api->release_blob||!api->scene_source_fingerprint||!api->material_source_fingerprint||!api->light_view) {
            FreeLibrary(module);module=nullptr;throw std::runtime_error("Crimson's bundled Quader reader requires its matching ABI 6 header and DLL.");
        }
    }
    ~Library(){if(module)FreeLibrary(module);}
};
std::shared_ptr<Library> library() { static auto value=std::make_shared<Library>();return value; }
struct Project {
    std::shared_ptr<Library> library;QuaderBridgeProject* handle{};
    ~Project(){library->api->release_project(handle);}
};
struct Source {
    std::shared_ptr<Library> library;QuaderBridgeScene* handle{};
    ~Source(){library->api->release_scene(handle);}
};
struct Blob {
    std::shared_ptr<Library> library;QuaderBridgeBlob* handle{};
    ~Blob(){library->api->release_blob(handle);}
};
QuaderProjectCatalog catalog(const Project& project) {
    QuaderBridgeError error{};QuaderBridgeProjectView view{};const auto* api=project.library->api;
    result(api->project_view(project.handle,&view,&error),error);
    QuaderProjectCatalog out;out.id=string(view.id);out.name=string(view.name);require(uuid(out.id),"Invalid Quader project identity.");
    require(view.scene_count<=50000,"Quader project has too many scenes.");
    for(std::uint32_t i=0;i<view.scene_count;++i){QuaderBridgeSceneEntry entry{};result(api->scene_entry(project.handle,i,&entry,&error),error);
        QuaderSceneEntry value{string(entry.id),string(entry.relative_path)};require(uuid(value.id)&&relative(value.path),"Invalid Quader scene identity/path.");
        if(i==view.default_scene_index)out.defaultScene=value.id;out.scenes.push_back(std::move(value));}
    if(out.defaultScene.empty()&&!out.scenes.empty())out.defaultScene=out.scenes.front().id;return out;
}
template<std::size_t N> Vector<N> vector(const float (&values)[N]) {
    Vector<N> out;for(std::size_t i=0;i<N;++i){require(std::isfinite(values[i]),"Quader source contains a nonfinite value.");out[i]=values[i];}return out;
}
Mat4 matrix(const float (&values)[16]) {
    Mat4 out;std::copy_n(values,16,out.begin());require(math::finite(out)&&math::isAffine(out),"Quader source contains an invalid affine transform.");return out;
}
QuaderSourceMaterial material(const std::shared_ptr<Source>& source,std::uint32_t index,const QuaderMaterialIdentity& identity) {
    const auto* api=source->library->api;QuaderBridgeError error{};QuaderBridgeMaterialView view{};
    result(api->material_view(source->handle,index,&view,&error),error);
    QuaderSourceMaterial out;out.identity=identity;out.name=string(view.name);out.relativePath=string(view.source_relative_path);
    out.color=vector(view.base_color);out.roughness=view.roughness;out.metallic=view.metallic;out.normalStrength=view.normal_strength;
    out.normalDirectX=view.normal_directx!=0;require(view.diffuse_model<=1,"Unsupported Quader diffuse model.");out.lambert=view.diffuse_model==1;
    out.uvCenter=vector(view.uv_center);out.uvScale=vector(view.uv_scale);out.uvOffset=vector(view.uv_offset);out.uvRotation=view.uv_rotation_degrees;
    require(std::isfinite(out.roughness)&&std::isfinite(out.metallic)&&std::isfinite(out.normalStrength)&&std::isfinite(out.uvRotation),"Quader material has nonfinite parameters.");
    QuaderBridgeSourceFingerprint fingerprint{};result(api->material_source_fingerprint(source->handle,index,&fingerprint,&error),error);
    out.fingerprint={fingerprint.byte_count,fingerprint.content_hash};
    for(std::uint32_t i=0;i<4;++i){const auto& info=view.textures[i];if(!info.present)continue;auto& target=out.textures[i];
        target.present=true;target.srgb=info.srgb!=0;target.id=string(info.id);target.path=string(info.source_path);
        Blob blob{source->library};result(api->read_texture(source->handle,index,i,&blob.handle,&error),error);QuaderBridgeBlobView bytes{};
        result(api->blob_view(blob.handle,&bytes,&error),error);require(bytes.bytes&&bytes.byte_count>0&&bytes.byte_count<=64ull*1024*1024,"Quader texture is empty or too large.");
        target.extension=string(bytes.extension);target.bytes.assign(bytes.bytes,bytes.bytes+bytes.byte_count);}
    return out;
}
std::string readRecord(const std::filesystem::path& path) {
    const auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(file!=INVALID_HANDLE_VALUE,"Cannot read completed Quader save.");struct Close{HANDLE value;~Close(){CloseHandle(value);}} close{file};
    LARGE_INTEGER length{};require(GetFileSizeEx(file,&length)&&length.QuadPart>0&&length.QuadPart<=65536,"Invalid completed Quader save size.");
    std::string text(static_cast<std::size_t>(length.QuadPart),'\0');DWORD count{};
    require(ReadFile(file,text.data(),static_cast<DWORD>(text.size()),&count,nullptr)&&count==text.size(),"Quader save record changed while reading.");return text;
}
std::string field(const Json& record,const char* key) {
    require(record.contains(key)&&record[key].is_string(),std::string("Quader save record is missing ")+key+".");const auto value=record[key].get<std::string>();
    require(!value.empty(),"Quader save record contains an empty value.");return value;
}
std::uint64_t number(const std::string& value,int base) {
    std::uint64_t out{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),out,base);
    require(parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size(),"Invalid Quader save fingerprint.");return out;
}
}
QuaderProjectCatalog inspectQuaderProject(const std::filesystem::path& path) {
    Project project{library()};QuaderBridgeError error{};const auto utf8=pathText(path);
    result(project.library->api->open_project(utf8.c_str(),&project.handle,&error),error);return catalog(project);
}
std::shared_ptr<QuaderSourceScene> readQuaderScene(const std::filesystem::path& path,const std::string& sceneId) {
    Project project{library()};const auto* api=project.library->api;QuaderBridgeError error{};const auto utf8=pathText(path);
    result(api->open_project(utf8.c_str(),&project.handle,&error),error);auto out=std::make_shared<QuaderSourceScene>();out->project=catalog(project);
    auto source=std::make_shared<Source>();source->library=project.library;result(api->open_scene(project.handle,sceneId.c_str(),&source->handle,&error),error);
    QuaderBridgeSceneView view{};result(api->scene_view(source->handle,&view,&error),error);out->id=string(view.id);out->relativePath=string(view.relative_path);
    require(out->id==sceneId,"The selected Quader scene identity changed.");const auto selected=std::find_if(out->project.scenes.begin(),out->project.scenes.end(),[&](const auto& entry){return entry.id==sceneId;});
    require(selected!=out->project.scenes.end()&&selected->path==out->relativePath,"The selected Quader scene path changed.");
    require(view.mesh_count<=50000&&view.material_count<=50000&&view.light_count<=50000,"Quader scene exceeds bridge object limits.");
    QuaderBridgeSourceFingerprint fingerprint{};result(api->scene_source_fingerprint(source->handle,&fingerprint,&error),error);out->fingerprint={fingerprint.byte_count,fingerprint.content_hash};
    for(std::uint32_t i=0;i<view.material_count;++i){QuaderBridgeMaterialIdentity value{};result(api->material_identity(source->handle,i,&value,&error),error);
        require(value.kind==QUADER_BRIDGE_MATERIAL_PROJECT||value.kind==QUADER_BRIDGE_MATERIAL_EMBEDDED,"Unknown Quader material kind.");out->materials.push_back({string(value.id),value.kind==QUADER_BRIDGE_MATERIAL_EMBEDDED});}
    std::uint64_t totalVertices{},totalIndices{},totalCollision{};std::set<std::uint64_t> entities;
    for(std::uint32_t i=0;i<view.mesh_count;++i){QuaderBridgeMeshView value{};result(api->mesh_view(source->handle,i,&value,&error),error);
        require((!value.vertex_count||value.vertices)&&(!value.index_count||value.indices)&&(!value.surface_count||value.surfaces)&&(!value.collision_point_count||value.collision_points),"Invalid Quader mesh buffers.");
        totalVertices+=value.vertex_count;totalIndices+=value.index_count;totalCollision+=value.collision_point_count;
        require(totalVertices<=16000000&&totalIndices<=48000000&&totalCollision<=48000000,"Quader scene exceeds bridge geometry limits.");
        require(entities.insert(value.display_entity).second,"Quader scene contains duplicate mesh occurrence identities.");
        QuaderSourceMesh mesh;mesh.name=string(value.name);mesh.displayEntity=value.display_entity;mesh.geometryEntity=value.geometry_entity;mesh.definitionEntity=value.definition_entity;
        mesh.transform=matrix(value.transform);mesh.visible=value.visible!=0;require(value.collision_status<=2,"Unknown Quader collision status.");mesh.collision=static_cast<QuaderCollision>(value.collision_status);
        require(value.collision_point_count%3==0&&(mesh.collision==QuaderCollision::StaticTriangles||value.collision_point_count==0),"Invalid Quader collision triangles.");
        mesh.vertices.reserve(value.vertex_count);for(std::uint32_t v=0;v<value.vertex_count;++v)mesh.vertices.push_back({vector(value.vertices[v].position),vector(value.vertices[v].normal),vector(value.vertices[v].uv)});
        if(value.index_count)mesh.indices.assign(value.indices,value.indices+value.index_count);
        for(const auto index:mesh.indices)require(index<mesh.vertices.size(),"Quader mesh contains an invalid vertex index.");
        for(std::uint32_t s=0;s<value.surface_count;++s){const auto& surface=value.surfaces[s];require(surface.index_count%3==0&&std::uint64_t(surface.index_offset)+surface.index_count<=mesh.indices.size()&&
            (surface.material_index==UINT32_MAX||surface.material_index<out->materials.size()),"Invalid Quader mesh surface.");mesh.surfaces.push_back({surface.material_index,surface.index_offset,surface.index_count});}
        for(std::uint32_t p=0;p<value.collision_point_count;++p)mesh.collisionPoints.push_back(vector(value.collision_points[p].position));out->meshes.push_back(std::move(mesh));}
    for(std::uint32_t i=0;i<view.light_count;++i){QuaderBridgeLightView value{};result(api->light_view(source->handle,i,&value,&error),error);require(value.type<=2,"Unsupported Quader light type.");
        QuaderSourceLight light;light.name=string(value.name);light.entity=value.entity;light.type=value.type;light.transform=matrix(value.transform);light.visible=value.visible!=0;light.castShadows=value.casts_shadows!=0;
        light.color=vector(value.color);light.energy=value.preview_energy;light.range=value.range;light.attenuation=value.distance_attenuation;
        require(std::isfinite(light.energy)&&light.energy>=0&&std::isfinite(light.range)&&std::isfinite(light.attenuation),"Invalid Quader lighting values.");out->lights.push_back(std::move(light));}
    const auto identities=out->materials;out->readMaterial=[source,identities](std::uint32_t index){require(index<identities.size(),"Invalid Quader material index.");return material(source,index,identities[index]);};return out;
}
QuaderCompletedRecord parseQuaderCompletedRecord(const std::string& text,const std::string& projectId,const std::string& documentId) {
    require(text.size()<=65536,"Quader save record exceeds its size limit.");const auto value=Json::parse(text);require(value.is_object()&&value.contains("version")&&value["version"].is_number()&&value["version"]==1,"Unsupported Quader save record version.");
    QuaderCompletedRecord out;out.projectId=field(value,"project_id");out.documentId=field(value,"document_id");out.relativePath=field(value,"relative_path");out.token=field(value,"save_token");
    require(out.projectId==projectId&&out.documentId==documentId&&uuid(out.projectId)&&uuid(out.documentId)&&uuid(out.token)&&relative(out.relativePath),"Invalid Quader save record identity/path.");
    const auto hash=field(value,"content_hash");require(hash.size()==16&&hash.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid Quader completed-save hash.");
    out.fingerprint={number(field(value,"byte_count"),10),number(hash,16)};require(out.fingerprint.bytes>0,"Completed Quader save is empty.");return out;
}
QuaderCompletedSave readQuaderCompletedSave(const std::filesystem::path& project,const std::string& projectId,const std::string& sceneId,bool materials) {
    require(uuid(projectId)&&uuid(sceneId),"Choose a saved Quader project and scene.");QuaderCompletedSave out;const auto base=project.parent_path()/".quader/bridge";
    const auto scene=base/"scenes"/pathFromUtf8(sceneId+".json");if(std::filesystem::exists(scene))out.scene=parseQuaderCompletedRecord(readRecord(scene),projectId,sceneId);
    const auto folder=base/"materials";if(materials&&std::filesystem::is_directory(folder))for(const auto& entry:std::filesystem::directory_iterator(folder)){
        const auto id=pathText(entry.path().stem());if(!entry.is_regular_file()||entry.path().extension()!=".json"||!uuid(id))continue;
        try{out.materials.emplace(id,parseQuaderCompletedRecord(readRecord(entry.path()),projectId,id));}catch(const std::exception& error){out.materialErrors.emplace(id,error.what());}}
    return out;
}
std::string quaderMaterialAssetId(const std::string& projectId,const std::string& sceneId,const QuaderMaterialIdentity& identity) {
    require(uuid(projectId)&&uuid(sceneId),"Invalid Quader resource namespace.");
    require(!identity.id.empty()&&identity.id.size()<=64&&identity.id.find_first_not_of("0123456789abcdef-")==std::string::npos,"Invalid Quader material identity.");
    return "assets/materials/quader-"+projectId+"-"+(identity.embedded?sceneId+"-":"")+identity.id+".crimson-material";
}
std::string quaderSaveKey(const QuaderCompletedSave& save,float scale,bool refreshMaterials,bool syncLights) {
    if(!save.scene)return {};const auto& scene=*save.scene;std::ostringstream out;out<<scene.projectId<<'|'<<scene.documentId<<'|'<<scene.relativePath<<'|'<<scene.token<<'|'<<std::hexfloat<<scale<<'|'<<refreshMaterials;
    // Keep existing consumption keys valid for the backwards-compatible default.
    if(!syncLights)out<<"|lights:off";
    if(refreshMaterials){std::vector<std::string> ids;for(const auto& [id,record]:save.materials){(void)record;ids.push_back(id);}std::sort(ids.begin(),ids.end());for(const auto& id:ids)out<<'|'<<id<<':'<<save.materials.at(id).token;}
    const auto canonical=out.str();return "quader:v1:"+modelImportDataDigest({reinterpret_cast<const std::uint8_t*>(canonical.data()),canonical.size()});
}
} // namespace crimson
