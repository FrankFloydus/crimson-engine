#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

class QMainWindow;
class QWidget;
namespace quader {class RenderHost;class QuaderEditorApi;class ProjectSession;class SceneSession;class SceneDocument;struct QtAssetBrowserExtension;}
namespace crimson::integration {
class LevelSession;
// Engine views added to the existing Quader main window. The full modeling
// workspace, commands, project documents and native event loop remain owners.
class LevelPanels final {
public:
    LevelPanels(QMainWindow&,quader::RenderHost&,quader::QuaderEditorApi&,quader::ProjectSession&,std::function<void()> requestFrame);
    ~LevelPanels();
    void bind(LevelSession*);
    void bindScene(quader::SceneSession*,std::string sceneUuid);
    void setNativeAssetObserver(std::function<void(quader::SceneDocument&)>);
    void syncNativeSelection();
    void update(double seconds,bool active);
    bool prepareFrame();
    bool continuous()const;
    bool ownsInput()const;
    bool navigating()const;
    QWidget* componentExtension() const;
    std::shared_ptr<quader::QtAssetBrowserExtension> assetBrowserExtension();
    void setBeforePlay(std::function<void()>);
    bool confirmClose();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
