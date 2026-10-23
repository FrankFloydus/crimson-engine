#pragma once
#include "tools/Tools.h"
#include <optional>
namespace crimson {
// One surface-pick gesture. Release owns its exact pixel, so a delayed hover
// result can never place a decal on the surface the cursor has already left.
class ViewportDecalDrop {
public:
    struct Pixel {std::uint32_t x{},y{};bool operator==(const Pixel&) const=default;};
    struct Request {std::uint64_t serial{};Pixel pixel;};
    bool active() const {return !asset_.empty()||replace_!=0;}
    bool awaitingClick() const {return replace_&&!delivered_;}
    bool delivered() const {return delivered_;}
    const auto& pending() const {return pending_;}
    const auto& preview() const {return preview_;}
    void cancel(){asset_={};replace_=0;delivered_=false;pending_.reset();preview_.reset();}
    void beginReplace(std::uint64_t entity){cancel();replace_=entity;}
    bool update(AssetRef<DecalAsset> asset,Pixel pixel,bool delivered) {
        const bool changed=replace_||asset_!=asset;
        if(changed)cancel();
        asset_=std::move(asset);if(!delivered_)pixel_=pixel;
        const bool superseded=changed||(delivered&&!delivered_);
        if(superseded)pending_.reset();
        delivered_=delivered_||delivered;return superseded;
    }
    void click(Pixel pixel){pixel_=pixel;delivered_=true;pending_.reset();}
    void clearPick(){pending_.reset();preview_.reset();}
    bool needsPick() const {return active()&&!awaitingClick()&&!pending_;}
    void request(std::uint64_t serial){pending_=Request{serial,pixel_};}
    std::optional<DecalSurfacePlacement> resolve(const PickResult& result) {
        if(!pending_||pending_->serial!=result.serial)return {};
        pending_.reset();
        if(result.surfacePending){preview_.reset();return {};}
        if(result.entityId&&result.surfaceValid)preview_=DecalSurfacePlacement{asset_,replace_,result};
        else preview_.reset();
        if(!delivered_)return {};
        const auto placement=preview_;cancel();return placement;
    }
private:
    AssetRef<DecalAsset> asset_;
    std::uint64_t replace_{};
    Pixel pixel_{};
    bool delivered_{};
    std::optional<Request> pending_;
    std::optional<DecalSurfacePlacement> preview_;
};
}
