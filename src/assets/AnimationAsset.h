#pragma once

#include "ModelAsset.h"
#include "resources/AssetRef.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace crimson {

enum class AnimationRootLockReference { FirstFrame, ReferencePose, Zero };

// Authored clip settings are independent of the immutable imported tracks and
// of every scene/preview playback instance. Time ranges are offsets from the
// imported clip's first key; they never rewrite the source animation.
struct AnimationAsset {
    std::string id,name;
    AssetRef<ModelAsset> sourceModel;
    std::uint32_t sourceClip{};
    std::string sourceClipIdentity,bindingSignature;
    float startTime{},endTime{},playbackRate{1};
    bool loop{true},forceRootLock{};
    std::int32_t rootNode{-1};
    AnimationRootLockReference rootLockReference{AnimationRootLockReference::FirstFrame};
    bool operator==(const AnimationAsset&) const = default;
};

// Source packages have immutable UUID identities. The index is therefore only
// a selector inside that exact source; its fingerprint verifies the tracks.
std::string importedAnimationId(const std::string& modelId,std::uint32_t clip);
std::string animationBindingSignature(const ModelAsset&);
AnimationAsset importedAnimationAsset(const ModelAsset&,std::size_t clip);
void validateAnimationAsset(const AnimationAsset&);
void validateAnimationSource(const AnimationAsset&,const ModelAsset&);
std::string serializeAnimationAsset(const AnimationAsset&);
AnimationAsset deserializeAnimationAsset(std::string_view,const std::string& expectedId={});
std::filesystem::path animationAssetPath(const std::filesystem::path& root,const std::string& id);
AnimationAsset loadAnimationAsset(const std::filesystem::path& root,const std::string& id);
void saveAnimationAsset(const std::filesystem::path& root,const AnimationAsset&);
AnimationAsset copyAnimationAsset(const std::filesystem::path& root,const AnimationAsset&,const std::string& name={});

} // namespace crimson
