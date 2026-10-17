#include "PlaySession.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>

namespace crimson {
namespace {
constexpr double ChaseInterval=.2,RetryInterval=.5,ChaseStallInterval=.5;
constexpr float ChaseRetargetDistance=.25f,ArrivalDistance=.025f;
float randomUnit(std::uint64_t& state) {
    auto value=(state+=0x9e3779b97f4a7c15ull);
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ull;
    value=(value^(value>>27))*0x94d049bb133111ebull;
    value^=value>>31;
    return static_cast<float>(value>>40)*(1.f/16777216.f);
}
float horizontalDistance(const Vec3& a,const Vec3& b) {return std::hypot(a[0]-b[0],a[2]-b[2]);}
bool capsulesWithinReach(const Vec3& a,const AgentTraversalSettings& first,const Vec3& b,const AgentTraversalSettings& second) {
    // Upright capsule centers form vertical segments. Their separation, rather
    // than the distance between feet, also represents contact on ramps/steps.
    const double vertical=std::max({0.0,double(a[1])+first.radius-(double(b[1])+second.height-second.radius),
        double(b[1])+second.radius-(double(a[1])+first.height-first.radius)});
    const double dx=double(a[0])-b[0],dz=double(a[2])-b[2];
    return std::sqrt(dx*dx+dz*dz+vertical*vertical)<=first.radius+second.radius+.05;
}
}
void PlaySession::advanceEnemies(double seconds) {
    if(!document_||!navigation_||!physics_)return;
    const auto& scene=document_->scene();
    const auto records=scene.records();std::unordered_set<EntityId> kept;
    for(const auto& record:records) {
        if(!record.enemy)continue;kept.insert(record.id);
        const auto pose=scene.worldTransform(record.id);if(!pose)continue;
        const auto position=math::transformPoint(*pose,{});
        const auto found=agents_.find(record.id);
        auto [entry,created]=enemies_.try_emplace(record.id);auto& state=entry->second;
        const auto profile=record.controller?record.controller->profile.id:std::string{};
        const auto definition=frameProfiles_.find(profile);
        const auto traversal=definition==frameProfiles_.end()?std::optional<AgentTraversalSettings>{}:definition->second->traversal;
        const bool moved=!created&&state.pose&&math::transformPoint(*state.pose,{})!=position;
        const bool newBehaviour=created||state.definition.behaviour!=record.enemy->behaviour;
        const bool definitionChanged=!created&&state.definition!=*record.enemy;
        const bool changed=created||definitionChanged||state.profile!=profile||state.traversal!=traversal||moved;
        if(changed) {
            if(newBehaviour||moved) {
                state.origin=position;state.random=record.id^0xd1b54a32d192ed03ull;
            }
            state.definition=*record.enemy;state.profile=profile;state.traversal=traversal;
            state.target.reset();state.wait=state.retry=state.retarget=0;state.player=NoEntity;
            state.chaseProgress.reset();state.chaseStalled=state.chaseRetry=0;
            if(found!=agents_.end() && (found->second.source==CommandSource::Enemy||
                (definitionChanged&&found->second.source==CommandSource::External)))clearRoute(found->second);
        }
        state.pose=pose;
        if(found==agents_.end())continue;
        auto& agent=found->second;
        const auto resetProgress=[&]{state.chaseProgress.reset();state.chaseStalled=0;};
        const auto stop=[&]{if(agent.source==CommandSource::Enemy)clearRoute(agent);resetProgress();};
        if(record.id==player() || state.definition.behaviour==EnemyBehaviour::Idle) {stop();continue;}
        // Explicit commands own the actor until released. AI shares the motor
        // and route pipeline, but must not overwrite a script's live command.
        if(agent.source!=CommandSource::None&&agent.source!=CommandSource::Enemy){resetProgress();continue;}
        if(!record.motor||!record.motor->enabled||!record.navigation||!record.navigation->enabled||
            !agent.profileAccepted||physics_->characterLocked(record.id)) {stop();continue;}
        if(seconds<=0)continue;
        const auto request=[&](const Vec3& target) {
            clearRoute(agent);agent.source=CommandSource::Enemy;agent.requestedDestination=target;
            return planRoute(agent,physics_->characterPosition(record.id).value_or(position));
        };
        if(state.definition.behaviour==EnemyBehaviour::ChasePlayer) {
            const auto targetId=player();const auto target=scene.worldTransform(targetId);
            const auto ownCapsule=physics_->acceptedCharacterTraversal(record.id),targetCapsule=physics_->acceptedCharacterTraversal(targetId);
            if(!target||!ownCapsule||!targetCapsule||targetId==record.id) {stop();state.target.reset();continue;}
            const auto current=physics_->characterPosition(record.id).value_or(position);
            const auto destination=physics_->characterPosition(targetId).value_or(math::transformPoint(*target,{}));
            if(capsulesWithinReach(current,*ownCapsule,destination,*targetCapsule)) {stop();state.target.reset();state.retarget=0;continue;}
            const auto speed=attributes_.value(record.id,stats::MovementSpeed);
            if(!speed||speed->current<=0){resetProgress();continue;}
            if(agent.queryStatus==NavigationStatus::Preparing||navigation_->readiness(agent.profile)==NavigationStatus::Preparing) {
                // Track the latest intent without polling a dataset that cannot
                // accept it yet. Publication resumes the ordinary route path.
                state.target=destination;state.player=targetId;agent.requestedDestination=destination;
                agent.source=CommandSource::Enemy;agent.queryStatus=NavigationStatus::Preparing;
                resetProgress();continue;
            }
            state.retarget=std::max(0.0,state.retarget-seconds);
            state.chaseRetry=std::max(0.0,state.chaseRetry-seconds);
            if(agent.source==CommandSource::Enemy&&agent.queryStatus==NavigationStatus::Ready&&agent.routeActive) {
                const double progressDistance=std::min(.01,speed->current*.1);
                if(!state.chaseProgress||horizontalDistance(current,*state.chaseProgress)>=progressDistance) {
                    state.chaseProgress=current;state.chaseStalled=0;
                } else state.chaseStalled+=seconds;
            } else resetProgress();
            if(state.player!=targetId){stop();state.target.reset();state.retarget=0;state.player=targetId;}
            if(state.retarget>1e-9)continue;
            state.retarget=ChaseInterval;
            const bool targetChanged=!state.target||math::length(destination-*state.target)>=ChaseRetargetDistance;
            const bool failed=agent.queryStatus==NavigationStatus::Unreachable||agent.queryStatus==NavigationStatus::Unavailable||agent.queryStatus==NavigationStatus::Failed;
            const bool exhausted=agent.queryStatus==NavigationStatus::Ready&&!agent.routeActive;
            const bool retry=state.chaseRetry<=1e-9&&(failed||exhausted||state.chaseStalled>=ChaseStallInterval);
            if(targetChanged||agent.source==CommandSource::None||retry) {
                state.target=destination;request(destination);
                state.chaseProgress=current;state.chaseStalled=0;state.chaseRetry=RetryInterval;
                if(agent.queryStatus!=NavigationStatus::Ready&&agent.queryStatus!=NavigationStatus::Preparing)state.retarget=RetryInterval;
            }
            continue;
        }
        if(state.wait>0) {state.wait=std::max(0.0,state.wait-seconds);if(state.wait>1e-9)continue;}
        if(state.target) {
            if(agent.destination) {
                if(horizontalDistance(*agent.destination,state.origin)>state.definition.radius+1e-4f) {
                    stop();state.target.reset();state.retry=RetryInterval;
                } else state.target=agent.destination;
            }
            if(state.target) {
                const auto capsule=physics_->acceptedCharacterTraversal(record.id);
                if(agent.queryStatus==NavigationStatus::Ready&&!agent.routeActive&&
                    horizontalDistance(position,*state.target)<=ArrivalDistance&&capsule&&
                    std::abs(position[1]-(*state.target)[1])<=capsule->maxStep+.2f) {
                    stop();state.target.reset();
                    state.wait=state.definition.minWait+(state.definition.maxWait-state.definition.minWait)*randomUnit(state.random);
                    continue;
                }
                if(agent.source==CommandSource::Enemy&&agent.queryStatus==NavigationStatus::Ready&&!agent.routeActive) {
                    stop();state.target.reset();state.retry=RetryInterval;continue;
                }
                if(agent.source==CommandSource::Enemy&&!agent.routeActive&&
                    agent.queryStatus!=NavigationStatus::Ready&&agent.queryStatus!=NavigationStatus::Preparing) {
                    stop();state.target.reset();state.retry=RetryInterval;continue;
                }
                // A paused motor/transform or external command may have
                // released our route; resume the same point when usable again.
                if(agent.source==CommandSource::None)request(*state.target);
                continue;
            }
        }
        state.retry=std::max(0.0,state.retry-seconds);
        if(state.retry>1e-9)continue;
        const auto readiness=navigation_->readiness(agent.profile);
        if(readiness!=NavigationStatus::Ready) {
            agent.status=navigation_->summary(agent.profile).diagnostic+"\n";state.retry=RetryInterval;continue;
        }
        if(state.definition.radius<=ArrivalDistance) {state.retry=RetryInterval;continue;}
        bool chosen=false;
        for(unsigned attempt=0;attempt<8;++attempt) {
            const float distance=state.definition.radius*std::sqrt(randomUnit(state.random));
            const float angle=2*std::numbers::pi_v<float>*randomUnit(state.random);
            const Vec3 candidate{state.origin[0]+distance*std::cos(angle),position[1],state.origin[2]+distance*std::sin(angle)};
            const auto projection=navigation_->project(agent.profile,candidate);
            if(projection.status!=NavigationStatus::Ready||!projection.point||
                horizontalDistance(*projection.point,state.origin)>state.definition.radius+1e-4f||
                math::length(*projection.point-position)<.1f)continue;
            if(request(*projection.point)&&agent.destination&&
                horizontalDistance(*agent.destination,state.origin)<=state.definition.radius+1e-4f&&
                math::length(*agent.destination-position)>=.1f) {
                state.target=agent.destination;chosen=true;break;
            }
            stop();
        }
        if(!chosen)state.retry=RetryInterval;
    }
    std::erase_if(enemies_,[&](const auto& item) {
        if(kept.contains(item.first))return false;
        if(const auto actor=agents_.find(item.first);actor!=agents_.end()&&actor->second.source==CommandSource::Enemy)clearRoute(actor->second);
        return true;
    });
}
}
