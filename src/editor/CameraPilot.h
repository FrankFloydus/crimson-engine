#pragma once

#include "scene/Scene.h"
#include <memory>
#include <optional>
#include <string>

namespace crimson {
class SceneDocument;

// Transient Scene viewport ownership. Scene-camera selection is read-only;
// piloting authors one ordinary document command per navigation gesture.
class CameraPilot {
public:
    bool selectCamera(SceneDocument&,EntityId,Camera& viewport);
    bool setPiloting(SceneDocument&,Camera& viewport,bool enabled);
    void useEditorCamera(SceneDocument&,Camera& viewport);
    bool sync(SceneDocument&,Camera& viewport);
    bool apply(SceneDocument&,Camera& viewport,bool navigationActive);
    bool finishGesture(SceneDocument&,Camera& viewport);
    void cancelGesture(SceneDocument&,Camera& viewport);
    EntityId selectedCamera()const{return camera_;}
    bool piloting()const{return piloting_;}
    bool gestureActive()const{return gesture_;}
    std::optional<Camera> freeCamera()const{return freeCamera_;}
    const std::string& status()const{return status_;}
private:
    const SceneDocument* document_{};
    EntityId camera_{};
    std::uint64_t documentGeneration_{},sceneGeneration_{},sceneRevision_{},poseRevision_{},transactionId_{};
    std::optional<Camera> freeCamera_;
    Camera lastView_;
    std::unique_ptr<Scene> gestureBasis_;
    bool piloting_{},gesture_{};
    std::string status_;
    bool sameDocument(const SceneDocument&)const;
    bool ownsGesture(const SceneDocument&)const;
    void remember(const SceneDocument&);
    void releaseGesture();
    void restoreEditorCamera(Camera&);
};
}
