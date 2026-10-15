#pragma once

#include "math/Math.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace crimson {

struct QuaderFingerprint {
    std::uint64_t bytes{}, hash{};
    bool operator==(const QuaderFingerprint&) const = default;
};
struct QuaderSceneEntry { std::string id,path; };
struct QuaderProjectCatalog {
    std::string id,name,defaultScene;
    std::vector<QuaderSceneEntry> scenes;
};
struct QuaderMaterialIdentity { std::string id; bool embedded{}; };
struct QuaderSourceTexture {
    bool present{},srgb{};
    std::string id,path,extension;
    std::vector<std::uint8_t> bytes;
};
struct QuaderSourceMaterial {
    std::string name,relativePath;
    QuaderMaterialIdentity identity;
    QuaderFingerprint fingerprint;
    Vec3 color{1,1,1};
    float roughness{1},metallic{},normalStrength{1};
    bool normalDirectX{},lambert{};
    Vec2 uvCenter{.5f,.5f},uvScale{1,1},uvOffset{};
    float uvRotation{};
    std::array<QuaderSourceTexture,4> textures;
};
struct QuaderSourceVertex { Vec3 position,normal; Vec2 uv; };
struct QuaderSourceSurface { std::uint32_t material{UINT32_MAX},offset{},count{}; };
enum class QuaderCollision { Disabled,StaticTriangles,Unsupported };
struct QuaderSourceMesh {
    std::string name;
    std::uint64_t displayEntity{},geometryEntity{},definitionEntity{};
    Mat4 transform;
    bool visible{true};
    std::vector<QuaderSourceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<QuaderSourceSurface> surfaces;
    QuaderCollision collision{};
    std::vector<Vec3> collisionPoints;
};
struct QuaderSourceLight {
    std::string name;
    std::uint64_t entity{};
    std::uint32_t type{}; // Native ABI: directional 0, point 1, spot 2.
    Mat4 transform;
    bool visible{true},castShadows{};
    Vec3 color{1,1,1};
    float energy{},range{},attenuation{};
};
struct QuaderSourceScene {
    QuaderProjectCatalog project;
    std::string id,relativePath;
    QuaderFingerprint fingerprint;
    std::vector<QuaderMaterialIdentity> materials;
    std::vector<QuaderSourceMesh> meshes;
    std::vector<QuaderSourceLight> lights;
    // Retains the immutable native scene/DLL. Invoke serially on a worker only;
    // reused Crimson materials never invoke this lazy source-reading operation.
    std::function<QuaderSourceMaterial(std::uint32_t)> readMaterial;
};
struct QuaderCompletedRecord {
    std::string projectId,documentId,relativePath,token;
    QuaderFingerprint fingerprint;
};
struct QuaderCompletedSave {
    std::optional<QuaderCompletedRecord> scene;
    std::unordered_map<std::string,QuaderCompletedRecord> materials;
    std::unordered_map<std::string,std::string> materialErrors;
};

QuaderProjectCatalog inspectQuaderProject(const std::filesystem::path&);
std::shared_ptr<QuaderSourceScene> readQuaderScene(const std::filesystem::path&,const std::string& sceneId);
QuaderCompletedSave readQuaderCompletedSave(const std::filesystem::path& project,const std::string& projectId,
    const std::string& sceneId,bool materials=true);
QuaderCompletedRecord parseQuaderCompletedRecord(const std::string& text,const std::string& projectId,const std::string& documentId);
std::string quaderMaterialAssetId(const std::string& projectId,const std::string& sceneId,const QuaderMaterialIdentity&);
std::string quaderSaveKey(const QuaderCompletedSave&,float scale,bool refreshMaterials,bool syncLights=true);

} // namespace crimson
