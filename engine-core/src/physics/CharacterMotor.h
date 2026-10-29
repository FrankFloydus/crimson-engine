#pragma once
#include "PhysicsWorld.h"

namespace crimson {
// A non-owning C++ binding to a controlled actor. Its PhysicsWorld must outlive
// it. Call move once per fixed tick; the last velocity wins for that actor and
// is consumed by the next step. It is independent of keyboard/controller code.
class CharacterMotor {
public:
    CharacterMotor(PhysicsWorld& world,EntityId entity):world_(&world),entity_(entity){}
    void move(const Vec3& desiredVelocity){world_->requestMovement(entity_,desiredVelocity);}
    // Persistent momentum comes from resolved contacts. Call every fixed tick,
    // including with zero input to brake on the ground or coast in the air.
    // Accelerations use m/s^2; friction uses 1/s and air control is [0,1].
    void moveFirstPerson(const Vec3& desiredHorizontalVelocity,float groundAcceleration,float groundFriction,float airAcceleration,float airControl){
        world_->requestFirstPersonMovement(entity_,desiredHorizontalVelocity,groundAcceleration,groundFriction,airAcceleration,airControl);
    }
    // Queue one press edge. A short landing buffer and coyote window are owned
    // by physics; callers must not repeat this while the jump key is held.
    void jump(float jumpHeight){world_->requestJump(entity_,jumpHeight);}
    // Overrides movement-facing for the next tick, including while idle. A zero
    // ground-plane direction retains yaw and suppresses movement-facing.
    void face(const Vec3& direction){world_->requestFacing(entity_,direction);}
    std::optional<Vec3> position() const{return world_->characterPosition(entity_);}
    std::optional<Vec3> velocity() const{return world_->characterVelocity(entity_);}
    bool grounded() const{return world_->grounded(entity_);}
private:
    PhysicsWorld* world_;
    EntityId entity_;
};
}
