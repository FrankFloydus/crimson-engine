#pragma once

#include "editor/ViewportTransformGesture.h"
#include <QWidget>
#include <functional>
#include <memory>

class QPainter;
namespace crimson {
class AssetLibrary;
namespace integration {
// A transparent Qt sibling paints editor handles above the existing QRhi
// composition. The native viewport remains the only focus and pointer owner.
class LevelViewportTools final : public QWidget {
public:
    explicit LevelViewportTools(QWidget& viewport,std::function<void()> requestFrame={});
    ~LevelViewportTools() override;
    void bind(SceneDocument*,AssetLibrary*,Camera*);
    void synchronize(bool blocked=false);
    void setOperation(ViewportTransformOperation);
    bool active() const;
    void finish();
    void cancel();
    std::function<bool(QEvent&)> additionalEvent;
    std::function<void(QPainter&)> additionalPaint;
    std::function<void()> beforeInteraction;
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void paintEvent(QPaintEvent*) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
}
