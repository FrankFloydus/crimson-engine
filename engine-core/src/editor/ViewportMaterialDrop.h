#pragma once
#include "scene/MaterialSlotAssignment.h"
#include <optional>

namespace crimson {
// Owns one asynchronous gesture without touching the scene or renderer. A
// moving hover waits for its outstanding readback; release always requests its
// exact pixel, rather than assigning the last completed hover target.
class ViewportMaterialDrop {
public:
    struct Pixel {
        std::uint32_t x{},y{};
        bool operator==(const Pixel&) const = default;
    };
    struct Pick { std::uint64_t serial{}; Pixel pixel; };
    bool active() const { return !material_.empty(); }
    bool delivered() const { return delivered_; }
    const std::optional<MaterialSlotAssignment>& preview() const { return preview_; }
    void cancel() { material_={};preview_.reset();request_.reset();delivered_=false; }
    // True means an old renderer request was superseded.
    bool update(const AssetRef<MaterialAsset>& material,Pixel pixel,bool delivered) {
        const bool replaced=material_!=material;
        if(replaced)cancel();
        material_=material;pixel_=pixel;
        if(delivered && !delivered_)request_.reset();
        const bool superseded=replaced || (delivered && !delivered_);
        delivered_=delivered_ || delivered;
        return superseded;
    }
    // Refresh even a stationary cursor: animated or newly resident geometry
    // can change the target without a new mouse event.
    bool needsPick() const { return active() && !request_; }
    Pick request(std::uint64_t serial) { request_=Pick{serial,pixel_};return *request_; }
    const std::optional<Pick>& pending() const { return request_; }
    std::optional<MaterialSlotAssignment> resolve(std::uint64_t serial,std::uint64_t entity,std::uint32_t slot) {
        if(!request_ || request_->serial!=serial)return {};
        request_.reset();
        if(entity && slot!=UINT32_MAX)preview_=MaterialSlotAssignment{entity,slot,material_};
        else preview_.reset();
        if(!delivered_)return {};
        auto assignment=preview_;cancel();return assignment;
    }
private:
    AssetRef<MaterialAsset> material_;
    Pixel pixel_{};
    std::optional<Pick> request_;
    std::optional<MaterialSlotAssignment> preview_;
    bool delivered_{};
};
}
