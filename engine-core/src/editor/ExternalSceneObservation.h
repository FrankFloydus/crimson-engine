#pragma once
#include "scene/SceneDocument.h"

namespace crimson {
// The worker has already read and fingerprinted this observation. Consume each
// publication once; ordinary frames must never reopen the scene to check it.
class ExternalSceneObservation {
public:
    void requested(const SceneDocument& document) {
        requestGeneration_=document.documentGeneration();requestBaseline_=document.loadedFingerprint();
        generation_=revision_=0;
    }
    bool changed(const SceneDocument& document,const SceneSnapshot& snapshot,
                 std::uint64_t publicationRevision,bool playing) {
        if(playing||snapshot.id!=document.assetId())return false;
        if(generation_==document.documentGeneration()&&revision_==publicationRevision)return false;
        generation_=document.documentGeneration();revision_=publicationRevision;
        // A worker result from before an own Save/Overwrite or document switch
        // cannot replace the newer saved baseline. Its file event schedules a
        // fresh request using that baseline.
        if(requestGeneration_!=generation_||requestBaseline_!=document.loadedFingerprint())return false;
        return snapshot.fingerprint!=document.loadedFingerprint();
    }
    void reset(){generation_=revision_=requestGeneration_=requestBaseline_=0;}
private:
    std::uint64_t generation_{},revision_{},requestGeneration_{},requestBaseline_{};
};
}
