#include "CameraTemplates.h"
#include "assets/AtomicFile.h"
#include "math/Json.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace crimson {
namespace {
constexpr size_t MaximumTemplates=256;
constexpr std::uintmax_t MaximumTemplateFileBytes=1024*1024;
constexpr float DegreesToRadians=3.14159265358979323846f/180.f;
using Json=nlohmann::json;

std::string trimmedName(std::string_view name) {
    const auto first=name.find_first_not_of(" \t\r\n");
    if(first==std::string_view::npos)return {};
    return std::string(name.substr(first,name.find_last_not_of(" \t\r\n")-first+1));
}
std::string nameKey(std::string_view name) {
    auto key=trimmedName(name);
    for(auto& value:key)if(value>='A'&&value<='Z')value=char(value-'A'+'a');
    return key;
}
bool validName(std::string_view name) {
    const auto trimmed=trimmedName(name);
    return !trimmed.empty()&&trimmed.size()<=128&&
        std::none_of(trimmed.begin(),trimmed.end(),[](unsigned char value){return value<32||value==127;});
}
void validateTemplates(const std::vector<CameraTemplate>& templates) {
    if(templates.size()>MaximumTemplates)throw std::runtime_error("Too many camera templates.");
    std::vector<std::string> names;names.reserve(templates.size());
    for(const auto& value:templates) {
        if(!validName(value.name)||!validCameraTemplate(value))throw std::runtime_error("Camera template has an invalid name, placement or lens settings.");
        const auto key=nameKey(value.name);
        if(std::find(names.begin(),names.end(),key)!=names.end())throw std::runtime_error("Camera template names must be unique.");
        names.push_back(key);
    }
}
Json serializeTemplates(const std::vector<CameraTemplate>& templates) {
    Json items=Json::array();
    for(const auto& value:templates)items.push_back({{"name",value.name},{"offset",value.offset},
        {"worldBasis",value.worldBasis},{"verticalFov",value.lens.verticalFov},
        {"near",value.lens.nearPlane},{"far",value.lens.farPlane}});
    return {{"version",1},{"templates",std::move(items)}};
}
}

CameraTemplate defaultPlayerCameraTemplate() {
    CameraTemplate value;value.name="First Person";
    return value;
}
bool validCameraTemplate(const CameraTemplate& value) {
    const auto& lens=value.lens;
    return math::finite(value.offset)&&math::finite(value.worldBasis)&&
        math::isAffine(value.worldBasis)&&math::transformPoint(value.worldBasis,{})==Vec3{}&&
        math::inverse(value.worldBasis).has_value()&&
        std::isfinite(lens.verticalFov)&&lens.verticalFov>=1&&lens.verticalFov<=179&&
        std::isfinite(lens.nearPlane)&&lens.nearPlane>0&&std::isfinite(lens.farPlane)&&lens.farPlane>lens.nearPlane;
}
bool sameCameraTemplateName(std::string_view a,std::string_view b) {
    return nameKey(a)==nameKey(b);
}
Vec3 cameraTemplateRotationDegrees(const CameraTemplate& value) {
    const auto fields=math::affineTransformFields(value.worldBasis);
    return fields?fields->rotationRadians/DegreesToRadians:Vec3{};
}
bool setCameraTemplateRotationDegrees(CameraTemplate& value,const Vec3& degrees) {
    if(!math::finite(degrees))return false;
    const auto fields=math::affineTransformFields(value.worldBasis);if(!fields)return false;
    const auto basis=math::withAffineRotation(value.worldBasis,*fields,degrees*DegreesToRadians);if(!basis)return false;
    auto candidate=value;candidate.worldBasis=*basis;
    if(!validCameraTemplate(candidate))return false;
    value=std::move(candidate);return true;
}
std::optional<CameraTemplate> captureCameraTemplate(const Scene& scene,EntityId camera,EntityId target,std::string name) {
    const auto record=scene.find(camera);
    if(record&&record->camera&&record->cameraFollow&&target&&record->cameraFollow->target==target) {
        // Follow owns these exact authored world values. Reconstructing through
        // an affine parent would add roundoff and break template identification.
        CameraTemplate value{trimmedName(name),record->cameraFollow->offset,record->cameraFollow->worldBasis,*record->camera};
        return validCameraTemplate(value)?std::optional<CameraTemplate>{std::move(value)}:std::nullopt;
    }
    const auto world=scene.worldTransform(camera);
    const auto targetWorld=target?scene.worldTransform(target):std::optional<Mat4>{Mat4{}};
    if(!record||!record->camera||camera==target||!world||!targetWorld)return std::nullopt;
    const auto basis=math::withAffinePosition(*world,{});if(!basis)return std::nullopt;
    CameraTemplate value{trimmedName(name),math::transformPoint(*world,{})-math::transformPoint(*targetWorld,{}),*basis,*record->camera};
    return validCameraTemplate(value)?std::optional<CameraTemplate>{std::move(value)}:std::nullopt;
}
std::filesystem::path cameraTemplatesPath(const std::filesystem::path& projectRoot) {
    return projectRoot/"Content"/"camera-templates.json";
}
std::vector<CameraTemplate> loadCameraTemplates(const std::filesystem::path& projectRoot) {
    const auto path=cameraTemplatesPath(projectRoot);
    if(!std::filesystem::exists(path))return {};
    if(!std::filesystem::is_regular_file(path))
        throw std::runtime_error("Camera template file is invalid or too large.");
    const auto bytes=std::filesystem::file_size(path);
    if(bytes>MaximumTemplateFileBytes)throw std::runtime_error("Camera template file is invalid or too large.");
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Could not read camera templates.");
    std::string contents(static_cast<size_t>(bytes),'\0');
    input.read(contents.data(),static_cast<std::streamsize>(contents.size()));
    if(!input||input.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Camera templates changed while reading.");
    const auto json=Json::parse(contents);
    if(!json.is_object()||!json.contains("version")||!json.at("version").is_number_integer()||json.at("version")!=1||
       !json.contains("templates")||!json.at("templates").is_array()||
       json.at("templates").size()>MaximumTemplates)throw std::runtime_error("Unsupported camera template file.");
    std::vector<CameraTemplate> result;result.reserve(json.at("templates").size());
    for(const auto& item:json.at("templates"))result.push_back({trimmedName(item.at("name").get<std::string>()),
        item.at("offset").get<Vec3>(),item.at("worldBasis").get<Mat4>(),
        {item.at("verticalFov").get<float>(),item.at("near").get<float>(),item.at("far").get<float>()}});
    validateTemplates(result);return result;
}
void saveCameraTemplate(const std::filesystem::path& projectRoot,std::vector<CameraTemplate>& templates,CameraTemplate value) {
    value.name=trimmedName(value.name);
    if(!validName(value.name)||!validCameraTemplate(value))throw std::runtime_error("Camera template has an invalid name, placement or lens settings.");
    auto candidate=templates;
    const auto found=std::find_if(candidate.begin(),candidate.end(),[&](const auto& item){return sameCameraTemplateName(item.name,value.name);});
    if(found==candidate.end()&&sameCameraTemplateName(value.name,"First Person"))
        throw std::runtime_error("First Person is the built-in camera template; choose another name.");
    if(found==candidate.end())candidate.push_back(std::move(value));else *found=std::move(value);
    validateTemplates(candidate);
    const auto serialized=serializeTemplates(candidate).dump(2)+"\n";
    if(serialized.size()>MaximumTemplateFileBytes)throw std::runtime_error("Camera template file is too large.");
    writeFileAtomically(cameraTemplatesPath(projectRoot),serialized);
    templates.swap(candidate);
}
}
