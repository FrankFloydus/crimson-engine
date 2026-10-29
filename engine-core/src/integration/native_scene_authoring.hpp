#pragma once

#include <memory>
#include <functional>
#include <string>

namespace quader { class SceneSession; class ProjectSession; class SceneDocument; }
namespace crimson { struct SceneSnapshot; }
namespace crimson::integration {
class LevelSession;

// Component editors borrow a typed preview. Accepted changes enter the native
// scene's one history; this adapter owns no document or persistent entity data.
class NativeSceneAuthoring final {
public:
    NativeSceneAuthoring(LevelSession&,quader::ProjectSession&);
    ~NativeSceneAuthoring();
    void bind(quader::SceneSession*,std::string scene_uuid);
    void synchronize();
    quader::SceneSession* session() const;
    void set_retention_observer(std::function<void(quader::SceneDocument&)>);
    bool finish();
private:
    bool commit(const SceneSnapshot&,const std::string& label);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
