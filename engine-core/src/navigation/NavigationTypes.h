#pragma once
#include "assets/NavigationAgentProfile.h"
#include "physics/PhysicsWorld.h"
#include <compare>
#include <filesystem>
#include <stop_token>

namespace crimson {
struct NavigationTileKey { int x{},z{}; auto operator<=>(const NavigationTileKey&) const = default; };
struct NavigationTileVersion { NavigationTileKey tile; std::uint64_t version{}; bool operator==(const NavigationTileVersion&) const=default; };
struct NavigationPath { std::vector<Vec3> corners; std::vector<NavigationTileVersion> tiles; };
enum class NavigationStatus { Ready, Preparing, Unreachable, Unavailable, Failed };
struct NavigationResult { NavigationStatus status{NavigationStatus::Unavailable}; std::optional<NavigationPath> path; std::string diagnostic; };
struct NavigationProjection { NavigationStatus status{NavigationStatus::Unavailable}; std::optional<Vec3> point; std::string diagnostic; };
struct NavigationVolumeDescriptor {
    EntityId entity{}; Mat4 world{}; Vec3 size{20,10,20}; NavigationBakeSettings bake{}; bool enabled{true};
    bool operator==(const NavigationVolumeDescriptor&) const=default;
};
struct NavigationDebugSnapshot {
    std::vector<Vec3> vertices; std::vector<std::uint32_t> triangles;
    std::vector<Vec3> pendingVertices; std::vector<std::uint32_t> pendingTriangles;
    NavigationStatus status{NavigationStatus::Unavailable};
    bool pendingSources{};
    std::size_t readyTiles{},pendingTiles{},failedTiles{};
    std::uint64_t revision{}; std::string diagnostic;
};
// Detached values only. Workers never access the scene, physics or live Detour.
struct NavigationTileInput {
    NavigationTileKey tile; NavigationVolumeDescriptor volume; AgentTraversalSettings agent;
    std::vector<PhysicsBox> boxes; std::vector<PhysicsStaticMesh> meshes;
    float minimumY{},maximumY{}; std::uint64_t fingerprint{};
};
struct NavigationTileData {
    NavigationTileKey tile; std::vector<std::uint8_t> data;
    std::vector<Vec3> vertices; std::vector<std::uint32_t> triangles;
    std::uint64_t fingerprint{};
};
std::shared_ptr<const NavigationTileData> buildNavigationTile(const NavigationTileInput&,std::stop_token={},const std::filesystem::path& cacheRoot={});
std::uint64_t navigationTileFingerprint(const NavigationTileInput&);
Aabb navigationVolumeBounds(const NavigationVolumeDescriptor&);
bool navigationVolumeContains(const NavigationVolumeDescriptor&,const Vec3&);
}
