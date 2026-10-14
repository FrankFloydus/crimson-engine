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
    // Overrides movement-facing for the next tick, including while idle. A zero
    // ground-plane direction retains yaw and suppresses movement-facing.
    void face(const Vec3& direction){world_->requestFacing(entity_,direction);}
    std::optional<Vec3> position() const{return world_->characterPosition(entity_);}
    bool grounded() const{return world_->grounded(entity_);}
private:
    PhysicsWorld* world_;
    EntityId entity_;
};
}
