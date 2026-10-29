#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

class QMainWindow;
namespace quader {class RenderHost;}
namespace crimson::integration {
class LevelSession;
// Engine views added to the existing Quader main window. The full modeling
// workspace, commands, project documents and native event loop remain owners.
class LevelPanels final {
public:
    LevelPanels(QMainWindow&,quader::RenderHost&,std::function<void()> requestFrame);
    ~LevelPanels();
    void bind(LevelSession*);
    void setModelingSource(std::filesystem::path manifest,std::string projectId,std::string sceneId);
    void setProjectOpenHandler(std::function<void(const std::filesystem::path&)>);
    void update(double seconds,bool active);
    bool prepareFrame();
    bool continuous()const;
    bool ownsInput()const;
    bool navigating()const;
    bool confirmClose();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
