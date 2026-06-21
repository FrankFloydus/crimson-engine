#pragma once

#include "core/Types.h"
#include "modeling/ModelingSessionService.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace quader {

class SceneDocument;

class ModelingObjectRegistry {
public:
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] const std::unordered_map<int, EntityId>& entitiesByObject() const;
    [[nodiscard]] std::unordered_map<int, EntityId>& entitiesByObject();
    [[nodiscard]] const std::unordered_map<EntityId, int>& objectsByEntity() const;
    [[nodiscard]] std::unordered_map<EntityId, int>& objectsByEntity();
    [[nodiscard]] std::optional<EntityId> entityForSdkObject(int sdkObjectId) const;
    [[nodiscard]] std::optional<int> sdkObjectForEntity(EntityId entity) const;
    [[nodiscard]] std::vector<ModelingObjectRecord> records(const SceneDocument& scene) const;

    void bind(int sdkObjectId, EntityId entity);
    void unbindObject(int sdkObjectId);
    void clear();

private:
    std::unordered_map<int, EntityId> entitiesByObject_;
    std::unordered_map<EntityId, int> objectsByEntity_;
};

} // namespace quader
