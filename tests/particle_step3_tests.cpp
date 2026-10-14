#include "assets/ParticleAsset.h"
#include "particles/CpuParticleSystem.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace crimson;
void require(bool value,const std::string& message) { if (!value) throw std::runtime_error(message); }
void near(double actual,double expected,const std::string& message,double tolerance=1e-5) {
    require(std::isfinite(actual) && std::abs(actual-expected)<tolerance,message);
}
void near(const Vec3& actual,const Vec3& expected,const std::string& message,double tolerance=1e-5) {
    for (size_t axis=0;axis<3;++axis) near(actual[axis],expected[axis],message,tolerance);
}
ParticleEffectAsset pulse(uint32_t count=1) {
    auto effect=defaultParticleEffect();
    auto& e=effect.emitter;e.rate=0;e.gravityBehavior.enabled=false;e.lifetime=10;
    e.finiteDuration=true;e.duration=.1f;e.bursts={{1,0,count}};e.capacity=std::max(256u,count);
    return effect;
}
void same(const CpuParticleSystem& a,const CpuParticleSystem& b) {
    near(a.elapsed(),b.elapsed(),"Render rates changed fixed simulation time.",1e-10);
    require(a.particles().size()==b.particles().size(),"Replay particle counts differ.");
    for (size_t i=0;i<a.particles().size();++i) {
        const auto& x=a.particles()[i];const auto& y=b.particles()[i];
        require(x.spawnOrdinal==y.spawnOrdinal && x.lifetime==y.lifetime && x.birthTime==y.birthTime,
            "Replay changed birth identities, times or seeded lifetimes.");
        near(x.position,y.position,"Replay changed particle trajectories.");
        near(x.velocity,y.velocity,"Replay changed particle velocities.");
    }
}
void shapeBoundsAndSeed() {
    auto effect=pulse(10000);auto& e=effect.emitter;
    e.shape=ParticleShape::Sphere;e.radius=2;e.lifetimeRange={true,.1f,2};
    CpuParticleSystem system({8,10000,32}),replay({8,10000,32});
    system.restart(effect,72);system.tick();replay.restart(effect,72);replay.tick();same(system,replay);
    double averageCubicRadius{};
    for (const auto& p:system.particles()) {
        const auto radius=math::length(p.previousPosition);
        require(radius<=2.000001 && p.lifetime>=.1f && p.lifetime<=2,"Sphere position/lifetime escaped its range.");
        averageCubicRadius+=std::pow(radius/2,3);
    }
    near(averageCubicRadius/system.particles().size(),.5,"Sphere volume sampling is radially biased.",.02);
    const auto initial=system.particles().front().previousPosition;
    replay.restart(effect,73);replay.tick();require(replay.particles().front().previousPosition!=initial,"Distinct seeds produced identical variation.");
    effect.emitter.id=2;replay.restart(effect,72);replay.tick();
    require(replay.particles().front().previousPosition!=initial,"Stable emitter identity does not participate in the seed.");

    e.shape=ParticleShape::Box;e.boxSize={2,4,0};system.restart(effect,7);system.tick();
    for (const auto& p:system.particles()) require(std::abs(p.previousPosition[0])<=1 && std::abs(p.previousPosition[1])<=2 &&
        p.previousPosition[2]==0,"Box sampling discarded full dimensions or collapsed axes.");
    e.shape=ParticleShape::Cone;e.radius=.6f;e.velocityMode=ParticleVelocityMode::DirectionSpeed;
    e.direction={1,2,3};e.directionMode=ParticleDirectionMode::RandomCone;e.directionSpread=27;e.speedRange={true,2,7};
    system.restart(effect,8);system.tick();
    const auto axis=math::normalized(e.direction);const auto minimumCosine=std::cos(27*std::numbers::pi/180);
    for (const auto& p:system.particles()) {
        require(p.previousPosition[1]==0 && math::length(p.previousPosition)<=.600001,"Cone origins escaped the base disk.");
        const auto speed=math::length(p.velocity);
        require(speed>=1.99999 && speed<=7.00001,"Random speed escaped its range.");
        require(math::dot(math::normalized(p.velocity),axis)>=minimumCosine-1e-6,"Direction escaped the random cone half-angle.");
    }
    e.shape=ParticleShape::Sphere;e.directionMode=ParticleDirectionMode::Outward;e.directionSpread=0;e.speedRange.enabled=false;e.speed=3;
    system.restart(effect,9);system.tick();
    for (const auto& p:system.particles()) near(math::normalized(p.velocity),math::normalized(p.previousPosition),"Outward direction does not follow the sampled origin.");
    e.shape=ParticleShape::Point;system.restart(effect,9);system.tick();
    near(system.particles().front().velocity,axis*3,"Point outward fallback did not use the authored direction.");
    e.speedRange={true,0,0};system.restart(effect,9);system.tick();
    require(system.particles().front().velocity==Vec3{},"Zero-speed range unexpectedly moved a newborn.");
}
void burstTimingAndCompletion() {
    auto effect=pulse(3);effect.emitter.delay=.5f;effect.emitter.duration=.02f;effect.emitter.lifetime=.07f;
    CpuParticleSystem system;system.restart(effect,5);
    for (int i=0;i<30;++i) system.tick();
    require(system.particles().empty() && system.stats().burstsFired==0,"Delayed boundary burst fired in the preceding interval.");
    system.tick();require(system.particles().size()==3 && system.stats().burstsFired==1,"Delayed time-zero burst did not fire once.");
    near(system.particles().front().birthTime,.5,"Delayed burst had the wrong exact birth time.",1e-10);
    system.pause();const auto elapsed=system.elapsed();system.advance(10);system.tick();
    require(system.elapsed()==elapsed && system.stats().burstsFired==1,"Pause duplicated the delayed burst or advanced playback.");
    system.play();system.tick();require(system.draining() && !system.completed(),"One-shot emission did not enter draining.");
    for (int i=0;i<6;++i) system.tick();
    require(system.completed() && system.stats().burstsFired==1 && system.particles().empty(),"One-shot did not complete after its live particles died.");
    const auto completedTime=system.elapsed();system.advance(1);system.tick();
    require(system.elapsed()==completedTime,"Completed effect continued accumulating time.");
    system.play();require(system.elapsed()==0 && system.seed()==5 && !system.completed(),"Play did not replay the completed effect with its seed.");

    effect=pulse();auto& e=effect.emitter;e.delay=.007f;e.duration=.013f;e.loop=true;e.capacity=8192;e.rate=37;
    e.bursts={{9,.006f,3},{4,0,2}};
    CpuParticleSystem slow,fast;slow.restart(effect,42);fast.restart(effect,42);
    for (int i=0;i<60;++i) slow.advance(1.0/60);
    for (int i=0;i<250;++i) fast.advance(1.0/250);
    same(slow,fast);
    uint64_t expectedBursts{},expectedBirths{};
    for (const auto& burst:e.bursts) {
        const auto origin=static_cast<double>(e.delay)+burst.time;
        const auto loops=static_cast<uint64_t>(std::max(0.0,std::ceil((1-origin)/e.duration-1e-10)));
        expectedBursts+=loops;expectedBirths+=loops*burst.count;
    }
    expectedBirths+=static_cast<uint64_t>(std::floor((1-static_cast<double>(e.delay))*e.rate+1e-10));
    require(slow.stats().burstsFired==expectedBursts && slow.stats().spawned==expectedBirths && !slow.emissionFinished(),
        "Loop crossings lost/duplicated bursts or reset continuous rate fractions.");
    slow.pause();slow.advance(3);slow.play();slow.advance(ParticleFixedStep);fast.advance(ParticleFixedStep);
    same(slow,fast);
    require(slow.stats().burstsFired==fast.stats().burstsFired && slow.stats().spawned==fast.stats().spawned,
        "Loop Pause/Resume duplicated or lost a boundary burst against uninterrupted playback.");
}
void directionFlatness() {
    auto effect=pulse(2048);auto& e=effect.emitter;
    e.shape=ParticleShape::Cone;e.velocityMode=ParticleVelocityMode::DirectionSpeed;e.direction={0,1,0};
    e.directionMode=ParticleDirectionMode::RandomCone;e.directionSpread=65;e.speedRange={true,.75f,4};
    e.lifetimeRange={true,.3f,2};
    CpuParticleSystem spherical,halfFlat,flat,replay;
    spherical.restart(effect,99);spherical.tick();
    e.directionFlatness=.5f;halfFlat.restart(effect,99);halfFlat.tick();
    e.directionFlatness=1;flat.restart(effect,99);flat.tick();replay.restart(effect,99);replay.tick();same(flat,replay);
    require(flat.particles().size()==spherical.particles().size() && halfFlat.particles().size()==spherical.particles().size(),
        "Flatness changed emission counts.");
    bool hasFanWidth=false;
    for(size_t i=0;i<spherical.particles().size();++i) {
        const auto& a=spherical.particles()[i];const auto& b=halfFlat.particles()[i];const auto& c=flat.particles()[i];
        require(a.previousPosition==b.previousPosition && a.previousPosition==c.previousPosition &&
            a.lifetime==b.lifetime && a.lifetime==c.lifetime && a.spawnOrdinal==c.spawnOrdinal,
            "Flatness consumed different random lanes for origin, lifetime or identity.");
        near(math::length(b.velocity),math::length(a.velocity),"Half-flat direction changed sampled speed.",1e-6);
        near(math::length(c.velocity),math::length(a.velocity),"Flat fan changed sampled speed.",1e-6);
        require(std::abs(b.velocity[0])<=std::abs(a.velocity[0])+1e-6,"Partial flatness increased the perpendicular component.");
        near(c.velocity[0],0,"Fully flat spread escaped the axis/tangent fan plane.",1e-6);
        hasFanWidth|=std::abs(c.velocity[2])>.05f;
    }
    require(hasFanWidth,"Flatness collapsed the full fan to its axis.");
    e.directionFlatness=0;replay.restart(effect,99);replay.tick();
    for(size_t i=0;i<spherical.particles().size();++i)require(spherical.particles()[i].velocity==replay.particles()[i].velocity,
        "Explicit zero flatness changed default seeded directional records.");
    const auto previous=spherical.particles().front();const auto time=spherical.elapsed();e.directionFlatness=1;spherical.updateDefinition(effect);
    require(spherical.elapsed()==time && spherical.particles().front().velocity==previous.velocity,
        "Live flatness editing restarted or changed a previously born particle.");

    // Zero spread has no perpendicular component to flatten, including a
    // direction parallel to the normal basis fallback axis.
    e.directionSpread=0;e.direction={1,0,0};flat.restart(effect,99);flat.tick();
    for(const auto& p:flat.particles())require(p.velocity[0]>0 && p.velocity[1]==0 && p.velocity[2]==0,
        "Zero spread plus flatness did not retain the authored direction.");

    e.direction={0,1,0};e.directionSpread=120;flat.restart(effect,99);flat.tick();
    const auto wideCosine=std::cos(120*std::numbers::pi/180);
    for(size_t i=0;i<flat.particles().size();++i) {
        const auto& p=flat.particles()[i];const auto normalized=math::normalized(p.velocity);
        require(math::finite(p.velocity) && normalized[1]>=wideCosine-1e-6,"Flat wide spread escaped its authored cone half-angle.");
        near(p.velocity[0],0,"Wide flat spread escaped its fan plane.",1e-6);
        near(math::length(p.velocity),math::length(spherical.particles()[i].velocity),"Wide flat spread changed sampled speed.",1e-6);
    }
    e.shape=ParticleShape::Sphere;e.directionMode=ParticleDirectionMode::Outward;e.directionSpread=35;
    flat.restart(effect,99);flat.tick();
    const auto radialCosine=std::cos(35*std::numbers::pi/180);
    bool hasRadialSpread=false;
    for(const auto& p:flat.particles()) {
        const auto radial=math::normalized(p.previousPosition);const auto velocity=math::normalized(p.velocity);
        const auto alignment=math::dot(radial,velocity);
        require(alignment>=radialCosine-1e-6,"Outward spread escaped the cone around its own birth origin.");
        const auto fanTangent=math::normalized(math::cross(radial,std::abs(radial[1])<.99f?Vec3{0,1,0}:Vec3{1,0,0}));
        near(math::dot(velocity,math::cross(radial,fanTangent)),0,"Flat outward spread escaped its radial fan plane.",1e-6);
        hasRadialSpread|=alignment<.99;
    }
    require(hasRadialSpread,"Outward mode ignored the visible Spread control.");
    e.directionSpread=0;flat.restart(effect,99);flat.tick();
    for(const auto& p:flat.particles())near(math::normalized(p.velocity),math::normalized(p.previousPosition),
        "Zero-spread outward mode changed existing radial direction.",1e-6);
    e.directionMode=ParticleDirectionMode::RandomCone;e.directionSpread=180;flat.restart(effect,99);flat.tick();
    for(const auto& p:flat.particles())require(math::finite(p.velocity) && math::length(p.velocity)>=.74999 && math::length(p.velocity)<=4.00001,
        "Fully spherical spread with flatness produced invalid speed or direction.");
}
void chronologicalRateAndBursts() {
    auto effect=pulse();auto& e=effect.emitter;e.rate=30;e.bursts={{1,.018f,1}};e.duration=1;
    CpuParticleSystem system;system.restart(effect);system.tick();require(system.particles().empty(),"Fractional rate fixture emitted too early.");
    system.tick();require(system.particles().size()==2,"Fractional rate/burst interval emitted the wrong count.");
    near(system.particles()[0].birthTime,.018f,"Burst did not precede a later fractional-rate birth.",1e-10);
    near(system.particles()[1].birthTime,1.0/30,"Future rate birth moved before an earlier burst.",1e-10);
    require(system.particles()[0].spawnOrdinal==0 && system.particles()[1].spawnOrdinal==1,"Merged emissions have the wrong ordinal order.");

    // Exactly representable .5s is both a rate birth and a burst boundary.
    e.rate=2;e.bursts={{1,.5f,2}};system.restart(effect);
    for (int i=0;i<30;++i) system.tick();
    require(system.particles().size()==1 && system.stats().burstsFired==0,"Half-open burst fired on the previous tick's inclusive rate endpoint.");
    system.tick();require(system.particles().size()==3 && system.stats().burstsFired==1,"Boundary burst did not fire on the following tick.");
    near(system.particles()[1].birthTime,.5,"Boundary burst birth time changed.",1e-10);
    near(system.particles()[2].birthTime,.5,"Simultaneous burst particle birth time changed.",1e-10);

    e.rate=240;e.bursts={{1,.0125f,1}};system.restart(effect);system.tick();
    require(system.particles().size()==5,"Interior tied rate/burst fixture has the wrong count.");
    for (size_t i=1;i<system.particles().size();++i) require(system.particles()[i].birthTime+1e-10>=system.particles()[i-1].birthTime,
        "Rate/burst merge is not chronological.");
    near(system.particles().back().birthTime,ParticleFixedStep,"Merged rate scheduler lost its final newborn boundary.",1e-10);
}
void affineSpacesAndLiveEdits() {
    auto effect=pulse();effect.emitter.initialVelocity={1,2,3};effect.emitter.space=ParticleSpace::Local;
    effect.emitter.shape=ParticleShape::Sphere;effect.emitter.radius=.3f;
    Mat4 world=math::translation({3,4,5})*math::scaling({-2,3,4});world[4]=.7f;
    CpuParticleSystem local,worldSystem;local.restart(effect,17,world);local.tick();
    const auto localRecord=local.particles().front();
    auto worldEffect=effect;worldEffect.emitter.space=ParticleSpace::World;worldSystem.restart(worldEffect,17,world);worldSystem.tick();
    near(worldSystem.particles().front().position,math::transformPoint(world,localRecord.position),"World birth discarded a full affine origin or direction.");
    near(local.particleWorldPosition(localRecord),worldSystem.particles().front().position,"Local world-position helper has the wrong transform.");
    auto moved=world; moved[12]+=11;moved[13]-=4;
    local.setTransform(moved);worldSystem.setTransform(moved);
    require(local.elapsed()==ParticleFixedStep,"Moving local emitter restarted playback.");
    near(worldSystem.particles().front().position,math::transformPoint(world,localRecord.position),"World particle followed its emitter.");
    std::vector<ParticlePresentation> presented;local.appendPresentation(presented,1);
    require(presented.size()==1,"Local particle was not extracted for presentation.");
    near(presented.front().position,math::transformPoint(moved,localRecord.position),"Local particle did not follow the composed affine transform.");
    near(presented.front().size,effect.emitter.size,"Local particle size inherited transform scale instead of remaining world meters.");

    local.pause();const auto before=local.particles().front();const auto time=local.elapsed();const auto storage=local.particles().data();
    effect.emitter.color={.4f,.5f,.6f};effect.emitter.radius=1;effect.emitter.lifetimeRange={true,.5f,1};effect.emitter.speed=8;
    effect.emitter.accelerationBehavior={true,true};effect.emitter.acceleration={1,2,3};effect.emitter.dragBehavior={true,true};effect.emitter.drag=2;
    local.updateDefinition(effect);
    require(local.state()==ParticlePlaybackState::Paused && local.elapsed()==time && local.particles().data()==storage &&
        local.particles().front().position==before.position && local.particles().front().lifetime==before.lifetime,
        "Ordinary shape/range/behavior/appearance edit restarted or retroactively changed particles.");
    effect.emitter.space=ParticleSpace::World;local.updateDefinition(effect);
    require(local.state()==ParticlePlaybackState::Paused && local.elapsed()==0 && local.particles().empty(),"Space edit did not restart safely while retaining pause.");
    local.play();local.tick();local.pause();effect.emitter.bursts.front().count=2;local.updateDefinition(effect);
    require(local.state()==ParticlePlaybackState::Paused && local.elapsed()==0 && local.particles().empty(),"Burst edit retained an obsolete schedule.");
    local.play();local.tick();require(local.particles().size()==2,"Restarted burst did not use the new authored count.");
    effect.defaultSeed=888;local.updateDefinition(effect);
    require(local.seed()==888 && local.elapsed()==0 && local.particles().empty(),"Seed edit did not restart seeded births.");
    local.stop();effect.emitter.delay=.2f;local.updateDefinition(effect);
    require(local.state()==ParticlePlaybackState::Stopped && local.elapsed()==0,"Timing edit restarted a stopped effect.");
}
void forcesAndDrag() {
    auto effect=pulse();auto& e=effect.emitter;e.initialVelocity={6,3,-2};e.gravityBehavior={true,true};e.gravity={0,-8,0};
    e.accelerationBehavior={true,true};e.acceleration={2,1,4};e.dragBehavior={true,true};e.drag=3;
    CpuParticleSystem system;system.restart(effect);for (int i=0;i<60;++i) system.tick();
    const auto force=e.gravity+e.acceleration;const auto decay=std::exp(-3.0);const auto velocityFactor=(1-decay)/3;
    near(system.particles().front().velocity,e.initialVelocity*decay+force*velocityFactor,"Exponential drag or acceleration stage is incorrect.",2e-5);
    near(system.particles().front().position,e.initialVelocity*velocityFactor+force*((1-velocityFactor)/3),"Drag trajectory does not integrate the constant force.",2e-5);
    const auto prior=system.particles().front();const auto elapsed=system.elapsed();
    e.gravityBehavior.enabled=false;e.accelerationBehavior.present=false;e.dragBehavior.enabled=false;system.updateDefinition(effect);system.tick();
    near(system.particles().front().velocity,prior.velocity,"Disabled/removed behaviors still change velocity.");
    near(system.particles().front().position,prior.position+prior.velocity*ParticleFixedStep,"Disabled drag did not restore constant motion.");
    near(system.elapsed(),elapsed+ParticleFixedStep,"Behavior edit restarted its clock.",1e-10);
    e.dragBehavior.enabled=true;e.drag=1000;system.updateDefinition(effect);system.tick();
    require(math::length(system.particles().front().velocity)<1e-5 && math::finite(system.particles().front().position),"Strong drag reversed or destabilized velocity.");

    e.space=ParticleSpace::Local;e.dragBehavior.present=false;e.gravityBehavior.enabled=true;e.gravity={0,-6,0};e.initialVelocity={};
    Mat4 affine=math::translation({2,3,4})*math::rotation(math::axisAngle({0,0,1},std::numbers::pi/2))*math::scaling({2,3,4});
    system.restart(effect,1,affine);for (int i=0;i<30;++i) system.tick();
    near(system.particles().front().position,{0,-.75f,0},"Local gravity did not use simulation-space axes.");
    near(system.particleWorldPosition(system.particles().front()),math::transformPoint(affine,{0,-.75f,0}),"Local force path lost affine parenting.");
}
void schedulingBudgets() {
    auto completedEffect=pulse();completedEffect.emitter.duration=.05f;completedEffect.emitter.lifetime=.07f;
    CpuParticleSystem naturallyCompleted; naturallyCompleted.restart(completedEffect);naturallyCompleted.advance(10);
    require(naturallyCompleted.completed() && naturallyCompleted.stats().ticks==5 && naturallyCompleted.stats().droppedCatchUpTicks==0 &&
        naturallyCompleted.stats().droppedWallTime==0 && naturallyCompleted.interpolation()==0,
        "Idle wall time after one-shot completion was reported as simulation overload.");
    auto effect=pulse(7);auto& e=effect.emitter;e.loop=true;e.duration=.001f;
    CpuParticleSystem bounded({8,3,1});bounded.restart(effect);bounded.tick();
    const auto cycles=static_cast<uint64_t>(std::ceil(ParticleFixedStep/e.duration-1e-10));
    require(bounded.particles().size()==3 && bounded.stats().burstsFired==1 && bounded.stats().droppedScheduleSegments==cycles-1 &&
        bounded.stats().droppedSpawns==cycles*7-3,"Scheduling/spawn limits failed to account for skipped burst work.");
    const auto dropped=bounded.stats().droppedSpawns;bounded.tick();
    require(bounded.particles().size()==3 && bounded.stats().droppedSpawns>dropped,"Skipped short-loop work became a future birth backlog.");
    bounded.restart(effect);bounded.tick();
    require(bounded.particles().size()==3 && bounded.particles()[0].spawnOrdinal==0 && bounded.particles()[2].spawnOrdinal==2,
        "Bounded scheduling restart changed seeded spawn identities.");
    effect.emitter.rate=MaximumParticleRate;effect.emitter.bursts={{1,0,MaximumParticleCapacity}};
    CpuParticleSystem capacity({2,5,32});capacity.restart(effect);capacity.advance(1);
    require(capacity.stats().ticks==2 && capacity.stats().droppedCatchUpTicks==58 && capacity.particles().size()<=10 &&
        capacity.stats().droppedSpawns>100000,"Combined rate/burst/catch-up work was not bounded.");
}
}
int main() {
    try {
        shapeBoundsAndSeed();burstTimingAndCompletion();directionFlatness();chronologicalRateAndBursts();affineSpacesAndLiveEdits();forcesAndDrag();schedulingBudgets();
        std::cout<<"Particle Step 3 sampling, scheduling, lifecycle, spaces, live edits and force checks passed.\n";return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
