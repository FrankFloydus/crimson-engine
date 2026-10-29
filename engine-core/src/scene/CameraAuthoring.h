#pragma once

#include "CameraTemplates.h"
#include "SceneDocument.h"

namespace crimson {
// The camera remains a real child entity. Authoring it from an actor's Inspector
// never adds camera fields to the actor or changes the actor's transform.
EntityId resolveOwnedActorCamera(const Scene&,EntityId actor);
// Uses the current gesture when one is active; otherwise creates one command.
// Parent, identity, follow target and scene active-camera choice are preserved.
bool updateCameraAuthoring(SceneDocument&,EntityId camera,const CameraTemplate&,
                           const std::string& label="Edit camera");
}
