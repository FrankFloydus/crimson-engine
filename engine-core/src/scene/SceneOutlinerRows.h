#pragma once

#include "SceneDocument.h"
#include <array>
#include <string_view>
#include <unordered_map>

namespace crimson {
// One row per authored entity, retaining its real hierarchy. Only an Actor's
// direct following camera is edited inline rather than as a duplicate row.
struct SceneOutlinerRow {
    EntityId entity{};
    EntityId parent{};
    bool operator==(const SceneOutlinerRow&) const = default;
};
bool isSceneGroup(const EntityRecord&);
// Stable hierarchy preorder. A child of an inline actor camera uses its nearest
// visible ancestor; model and particle attachments keep their real rows.
std::vector<SceneOutlinerRow> sceneOutlinerRows(const Scene&);
enum class SceneOutlinerFilter : std::uint8_t {
    All,Models,Lights,Cameras,Colliders,Particles,Actors,Decals,Environment,Navigation,Quader
};
inline constexpr std::array SceneOutlinerFilters{
    SceneOutlinerFilter::All,SceneOutlinerFilter::Models,SceneOutlinerFilter::Lights,SceneOutlinerFilter::Cameras,
    SceneOutlinerFilter::Colliders,SceneOutlinerFilter::Particles,SceneOutlinerFilter::Actors,SceneOutlinerFilter::Decals,
    SceneOutlinerFilter::Environment,SceneOutlinerFilter::Navigation,SceneOutlinerFilter::Quader};
const char* sceneOutlinerFilterName(SceneOutlinerFilter);
// Selection spans use only the currently displayed hierarchy order. A filtered
// or collapsed anchor is absent and therefore cannot add hidden rows.
std::vector<EntityId> sceneOutlinerRange(std::span<const EntityId> displayed,EntityId anchor,EntityId target);
// Owns copied authoring metadata and its projection. Stable frames do not read
// records, traverse hierarchy or rerun name/component matching. Runtime-only
// poses and selection changes do not affect Outliner names or capabilities.
class SceneOutlinerProjection {
public:
    void synchronize(const Scene&,std::string_view search={},SceneOutlinerFilter=SceneOutlinerFilter::All,EntityId keepVisible=NoEntity);
    const EntityRecord& record(EntityId id) const {return records_.at(recordIndices_.at(id));}
    const std::vector<SceneOutlinerRow>& rows() const {return rows_;}
    std::span<const EntityId> children(EntityId parent) const;
    bool contains(EntityId id) const {return parents_.contains(id);}
    bool filtered() const {return !search_.empty()||filter_!=SceneOutlinerFilter::All;}
    std::uint64_t sourceBuilds() const {return sourceBuilds_;}
    std::uint64_t filterBuilds() const {return filterBuilds_;}
private:
    const Scene* owner_{};
    std::uint64_t generation_{},revision_{},sourceBuilds_{},filterBuilds_{};
    std::vector<EntityRecord> records_;
    std::vector<std::string> names_;
    std::vector<SceneOutlinerRow> baseRows_,rows_;
    std::unordered_map<EntityId,std::size_t> recordIndices_;
    std::unordered_map<EntityId,EntityId> owners_,baseParents_,parents_;
    std::unordered_map<EntityId,std::vector<EntityId>> children_;
    std::string search_;
    SceneOutlinerFilter filter_{SceneOutlinerFilter::All};
    EntityId keepVisible_{};
};
// A direct Actor-follow camera selects its actor. Authored models, emitters and
// other entities always retain their own authoring identity.
EntityId sceneEntityOwner(const Scene&,EntityId);
// The selected entity first. For an Actor (or its inline following camera), also
// include owned visual and camera properties in authored hierarchy order.
std::vector<EntityId> sceneEntityParts(const Scene&,EntityId);

struct SceneOutlinerColorTarget {
    EntityId entity{};
    bool operator==(const SceneOutlinerColorTarget&) const = default;
};
std::optional<Vec4> sceneOutlinerColor(const EntityRecord&);
bool setSceneOutlinerColors(SceneDocument&,std::span<const SceneOutlinerColorTarget>,std::optional<Vec4> color);
}
