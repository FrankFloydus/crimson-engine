#pragma once
#include "NavigationMesh.h"
#include "resources/ResourceManager.h"
#include <span>

namespace crimson {
// One Edit/Play domain; accepted static collision and supported asset snapshots
// are explicit inputs. Profile data remains resident without spawned actors.
class NavigationWorld {
public:
    explicit NavigationWorld(ResourceManager&,std::filesystem::path cacheRoot={});
    ~NavigationWorld();
    NavigationWorld(const NavigationWorld&)=delete;
    NavigationWorld& operator=(const NavigationWorld&)=delete;
    void reconcile(std::uint64_t domainGeneration,const std::optional<NavigationVolumeDescriptor>&,
        std::span<const NavigationAgentProfile>,std::span<const PhysicsBox>,std::span<const PhysicsStaticMesh>);
    void upsert(const PhysicsBox&);
    void upsert(const PhysicsStaticMesh&);
    void removeSource(EntityId);
    void setSourceReadiness(bool current,std::string diagnostic={});
    void pumpPublication();
    void clear();
    NavigationStatus readiness(const std::string& profile) const;
    void prepare(const std::string& profile);
    NavigationResult query(const std::string& profile,const Vec3& start,const Vec3& destination) const;
    NavigationProjection project(const std::string& profile,const Vec3&,const Vec3& extents={1,2,1}) const;
    bool valid(const std::string& profile,const NavigationPath&) const;
    NavigationDebugSnapshot summary(const std::string& profile) const;
    NavigationDebugSnapshot debugSnapshot(const std::string& profile) const;
    std::uint64_t revision(const std::string& profile) const;
    std::uint64_t buildCount() const;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}
