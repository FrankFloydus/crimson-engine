#pragma once
#include <crimson/renderer/forward/ForwardTypes.hpp>
#include <optional>

namespace crimson {
// Retains one clicked image point while its material-aware surface readback
// completes. This state owns no document edits, renderer calls or UI widgets.
class ViewportContextMenu {
public:
    struct Stamp {
        std::uint64_t docGeneration{},generation{},revision{};
        Mat4 view{},projection{};
        Vec2 size{};
        bool operator==(const Stamp&) const=default;
    };
    struct Pixel {
        std::uint32_t x{},y{};
        bool operator==(const Pixel&) const=default;
    };
    void begin(const Stamp& stamp,Pixel pixel) {
        cancel();stamp_=stamp;pixel_=pixel;active_=true;
    }
    void cancel(){active_=false;completed_=false;pendingSerial_.reset();position_.reset();entity_=0;}
    bool active() const{return active_;}
    bool completed() const{return active_&&completed_;}
    bool matches(const Stamp& stamp) const{return active_&&stamp_==stamp;}
    bool needsPick() const{return active_&&!completed_&&!pendingSerial_;}
    const Stamp& stamp() const{return stamp_;}
    Pixel pixel() const{return pixel_;}
    const std::optional<Vec3>& position() const{return position_;}
    std::uint64_t entity() const{return entity_;}
    PickRequest request(std::uint64_t serial) {
        pendingSerial_=serial;
        return {stamp_.generation,stamp_.revision,serial,pixel_.x,pixel_.y,0,PickMode::Surface};
    }
    bool resolve(const PickResult& result) {
        if(!active_||!pendingSerial_||result.mode!=PickMode::Surface||result.serial!=*pendingSerial_||
           result.generation!=stamp_.generation||result.revision!=stamp_.revision)return false;
        pendingSerial_.reset();
        if(result.surfacePending)return true;
        completed_=true;
        entity_=result.entityId;
        if(result.entityId&&result.surfaceValid&&math::finite(result.worldPosition))position_=result.worldPosition;
        else position_.reset();
        return true;
    }
private:
    Stamp stamp_;
    Pixel pixel_;
    std::optional<std::uint64_t> pendingSerial_;
    std::optional<Vec3> position_;
    std::uint64_t entity_{};
    bool active_{},completed_{};
};
}
