#pragma once
#include "scene/SceneDocument.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace crimson { struct ExtractedScene; }
namespace quader { class SceneDocument; struct EngineComponentsComponent; struct EngineSceneSettings; }
namespace crimson::integration {
// A disposable typed projection of the one native authored scene. It owns no
// persistence or history; UUID references remain native in authored envelopes.
class NativeSceneProjection final {
public:
    using EntityUuidResolver=std::function<std::string(EntityId)>;
    NativeSceneProjection();
    ~NativeSceneProjection();
    NativeSceneProjection(const NativeSceneProjection&)=delete;
    NativeSceneProjection& operator=(const NativeSceneProjection&)=delete;
    const SceneSnapshot& prepare(const quader::SceneDocument&,std::string scene_uuid,
        std::uint64_t source_revision,const std::filesystem::path& project_root);
    [[nodiscard]] const SceneSnapshot& snapshot()const;
    void apply_native_light_directions(ExtractedScene&,const Scene&)const;
    [[nodiscard]] EntityId entity_id(std::string_view native_uuid)const;
    [[nodiscard]] std::string entity_uuid(EntityId)const;
    [[nodiscard]] std::uint64_t source_revision()const;
    [[nodiscard]] const std::string& scene_uuid()const;
    [[nodiscard]] quader::EngineComponentsComponent engine_components(const EntityRecord&,EntityUuidResolver={})const;
    [[nodiscard]] quader::EngineSceneSettings engine_settings(const SceneProperties&,EntityUuidResolver={})const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
