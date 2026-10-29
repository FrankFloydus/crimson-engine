#pragma once
#include "editor/EditorActions.h"
#include "scene/SceneDocument.h"
#include "editor/CollisionLayersDocument.h"
#include <QWidget>
#include <memory>

namespace crimson {
class QuaderBridge;
class QtInspector final : public QWidget {
public:
    explicit QtInspector(QWidget* parent=nullptr);
    ~QtInspector() override;
    void synchronize(SceneDocument&,AssetLibrary&,Renderer&,EntityId target=NoEntity);
    void setQuaderBridge(QuaderBridge*);
    void setCollisionLayerNames(const CollisionLayerNames&);
    void setNavigationPreview(std::string*,std::function<std::string(const std::string&)>);
    EditorFrame takeActions();
    EntityId takeDecalPlacement();
    bool finish();
    void cancel();
    // Release document callbacks before its LevelSession is replaced/destroyed.
    void detach();
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
