#include "CameraPilot.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"

#include <algorithm>
#include <stdexcept>

namespace crimson {
namespace {
bool samePose(const Camera& a,const Camera& b) {
    return math::length(a.position-b.position)<=1e-6&&math::length(a.forward()-b.forward())<=1e-6&&
        math::length(a.right()-b.right())<=1e-6;
}
void copyProjection(Camera& destination,const Camera& source) {
    destination.fov=source.fov;destination.nearPlane=source.nearPlane;
    destination.farPlane=source.farPlane;destination.fovAxis=source.fovAxis;
}
}
bool CameraPilot::sameDocument(const SceneDocument& document)const {
    return document_==&document&&documentGeneration_==document.documentGeneration();
}
bool CameraPilot::ownsGesture(const SceneDocument& document)const {
    return gesture_&&sameDocument(document)&&transactionId_&&document.transactionId()==transactionId_;
}
void CameraPilot::remember(const SceneDocument& document) {
    document_=&document;documentGeneration_=document.documentGeneration();
    sceneGeneration_=document.scene().generation();sceneRevision_=document.scene().revision();poseRevision_=document.scene().poseRevision();
}
void CameraPilot::releaseGesture() {gesture_=false;transactionId_=0;gestureBasis_.reset();}
void CameraPilot::restoreEditorCamera(Camera& viewport) {
    if(freeCamera_)viewport=*freeCamera_;
    freeCamera_.reset();camera_=NoEntity;piloting_=false;releaseGesture();document_=nullptr;
    documentGeneration_=sceneGeneration_=sceneRevision_=poseRevision_=0;
}
bool CameraPilot::selectCamera(SceneDocument& document,EntityId id,Camera& viewport) {
    // An invalid menu request must not finish the current gesture or replace
    // its saved editor view.
    const auto selected=resolveSceneCamera(document.scene(),id);
    if(!selected){status_="Choose an existing Camera with a usable view.";return false;}
    if(camera_)sync(document,viewport);
    if(!finishGesture(document,viewport))return false;
    if(!camera_)freeCamera_=viewport;
    camera_=id;piloting_=false;viewport=*selected;lastView_=viewport;remember(document);status_.clear();return true;
}
bool CameraPilot::setPiloting(SceneDocument& document,Camera& viewport,bool enabled) {
    if(!camera_||!sync(document,viewport)){status_="Choose a scene Camera before piloting.";return false;}
    if(!enabled) {
        if(!finishGesture(document,viewport))return false;
        piloting_=false;sync(document,viewport);lastView_=viewport;status_.clear();return true;
    }
    if(piloting_)return true;
    if(document.transactionActive()){status_="Finish the active scene edit before piloting.";return false;}
    if(!sceneCameraLocalTransform(document.scene(),camera_,viewport)) {
        status_="The selected Camera needs an invertible affine frame and parent before piloting.";return false;
    }
    piloting_=true;lastView_=viewport;remember(document);status_.clear();return true;
}
void CameraPilot::useEditorCamera(SceneDocument& document,Camera& viewport) {
    finishGesture(document,viewport);restoreEditorCamera(viewport);status_.clear();
}
bool CameraPilot::sync(SceneDocument& document,Camera& viewport) {
    if(!camera_)return false;
    if(!sameDocument(document)) {
        restoreEditorCamera(viewport);status_="Camera view ended because the scene document changed.";return false;
    }
    const bool changed=sceneGeneration_!=document.scene().generation()||sceneRevision_!=document.scene().revision()||poseRevision_!=document.scene().poseRevision();
    const bool lostGesture=gesture_&&!ownsGesture(document);
    if(changed||lostGesture) {
        // Preserve externally published data. Commit only a transaction whose
        // token we still own; never cancel or append to a replacement edit.
        const bool wasPiloting=piloting_||gesture_;
        if(ownsGesture(document))document.commit();
        releaseGesture();piloting_=false;if(wasPiloting)status_="Camera piloting ended because the scene changed.";
    }
    const auto selected=resolveSceneCamera(document.scene(),camera_);
    if(!selected) {
        if(ownsGesture(document))document.commit();
        restoreEditorCamera(viewport);status_="The selected Camera no longer has a usable scene view.";return false;
    }
    if(piloting_&&!sceneCameraLocalTransform(document.scene(),camera_,*selected)) {
        if(ownsGesture(document))document.commit();
        releaseGesture();piloting_=false;status_="Camera piloting ended because its affine frame or parent is singular.";
    }
    if(!piloting_){viewport=*selected;lastView_=viewport;}
    remember(document);return true;
}
bool CameraPilot::apply(SceneDocument& document,Camera& viewport,bool navigationActive) {
    if(!sync(document,viewport)||!piloting_)return false;
    const auto selected=resolveSceneCamera(document.scene(),camera_);
    copyProjection(viewport,*selected);
    auto local=sceneCameraLocalTransform(gestureBasis_?*gestureBasis_:document.scene(),camera_,viewport);
    if(!local) {
        cancelGesture(document,viewport);piloting_=false;status_="Camera navigation produced an invalid affine pose.";return false;
    }
    const bool moved=!samePose(viewport,lastView_);
    if(moved) {
        if(!gesture_) {
            if(document.transactionActive()) {
                piloting_=false;sync(document,viewport);status_="Camera navigation stopped for another scene edit.";return false;
            }
            try {
                auto basis=std::make_unique<Scene>();basis->apply(document.scene().records(),document.scene().properties());
                local=sceneCameraLocalTransform(*basis,camera_,viewport);
                if(!local)throw std::runtime_error("Camera gesture has no invertible affine reference.");
                if(!document.beginTransform("Pilot camera",{camera_})){status_=document.status();piloting_=false;sync(document,viewport);return false;}
                gestureBasis_=std::move(basis);gesture_=true;transactionId_=document.transactionId();
            }catch(const std::exception& error){status_=error.what();piloting_=false;sync(document,viewport);return false;}
        }
        const auto pose=*local;
        if(!document.update([&](auto& records,auto&) {
            const auto target=std::find_if(records.begin(),records.end(),[&](const auto& record){return record.id==camera_;});
            if(target==records.end()||!target->camera)throw std::runtime_error("Piloted Camera no longer exists.");
            target->local=pose;
        })) {
            const auto failure=document.status();cancelGesture(document,viewport);piloting_=false;status_=failure;return false;
        }
        remember(document);status_.clear();
    }
    // Orbit target distance is navigation state, not authored scene data. Keep
    // it through own updates rather than resolving a fresh unit-distance POV.
    lastView_=viewport;
    if(!navigationActive)return finishGesture(document,viewport);
    return true;
}
bool CameraPilot::finishGesture(SceneDocument& document,Camera& viewport) {
    if(!gesture_)return true;
    sync(document,viewport);
    if(!gesture_)return true;
    if(!ownsGesture(document)){releaseGesture();piloting_=false;return false;}
    const auto committed=document.commit();releaseGesture();remember(document);
    if(!committed)status_=document.status();return committed;
}
void CameraPilot::cancelGesture(SceneDocument& document,Camera& viewport) {
    if(ownsGesture(document))document.cancel();
    releaseGesture();
    if(camera_&&sameDocument(document)) {
        if(const auto selected=resolveSceneCamera(document.scene(),camera_)){viewport=*selected;lastView_=viewport;remember(document);return;}
    }
    if(camera_)restoreEditorCamera(viewport);
}
}
