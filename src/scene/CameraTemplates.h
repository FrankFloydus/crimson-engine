#pragma once

#include "Scene.h"
#include <filesystem>
#include <optional>
#include <string_view>

namespace crimson {
// Reusable camera placement relative to a followed actor's world origin.
// No scene/entity/asset identity or live follow target is stored in a template.
struct CameraTemplate {
    std::string name;
    Vec3 offset{6.5f,13.1f,6.5f};
    Mat4 worldBasis{};
    SceneCamera lens{35.f};
    bool operator==(const CameraTemplate&) const = default;
};

CameraTemplate defaultPlayerCameraTemplate();
bool validCameraTemplate(const CameraTemplate&);
bool sameCameraTemplateName(std::string_view,std::string_view);
Vec3 cameraTemplateRotationDegrees(const CameraTemplate&);
bool setCameraTemplateRotationDegrees(CameraTemplate&,const Vec3& degrees);
// No target captures absolute world placement; a target captures a world offset.
std::optional<CameraTemplate> captureCameraTemplate(const Scene&,EntityId camera,EntityId target=NoEntity,std::string name={});

std::filesystem::path cameraTemplatesPath(const std::filesystem::path& projectRoot);
// User-authored templates only; missing file returns an empty list. Malformed
// files throw without changing the caller's current in-memory templates.
std::vector<CameraTemplate> loadCameraTemplates(const std::filesystem::path& projectRoot);
// Case-insensitive named upsert. ARPG is reserved for the built-in template.
// Validates and atomically publishes a complete
// candidate before swapping the caller's list; failure preserves both states.
void saveCameraTemplate(const std::filesystem::path& projectRoot,
                        std::vector<CameraTemplate>& templates,CameraTemplate value);
}
