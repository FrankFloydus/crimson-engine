#pragma once
#include "crimson-renderer/RenderTypes.h"
#include <optional>
#include <span>
#include <vector>

namespace crimson {
struct AgentMotion { Vec3 velocity{},facing{0,0,-1}; };

// Follows Detour's corner path in world space. Facing eases toward the current
// movement waypoint; an idle controller preserves its last orientation.
class AgentSteering {
public:
    static constexpr double FacingResponse=14;
    static constexpr float ArrivalDistance=.01f;
    void reset(){destination_.reset();path_.clear();corner_=0;}
    void setPath(std::vector<Vec3> corners);
    const std::optional<Vec3>& destination() const{return destination_;}
    std::span<const Vec3> path() const{return path_;}
    std::size_t cornerIndex() const{return corner_;}
    AgentMotion update(const Vec3& position,const Vec3& forward,float speed,double seconds);
private:
    std::optional<Vec3> destination_;
    std::vector<Vec3> path_;
    std::size_t corner_{};
};

// General C++ directional control, independent of the Game view input scheme.
Vec3 cameraRelativeMovement(const Vec2& axes,const Camera&,float speed);
}
