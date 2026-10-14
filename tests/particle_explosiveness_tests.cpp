#include "particles/CpuParticleSystem.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message,double tolerance=1e-9){require(std::abs(actual-expected)<tolerance,message);}
ParticleEffectAsset fixture(float explosiveness=0) {
    auto asset=defaultParticleEffect();auto& e=asset.emitter;
    e.rate=8;e.explosiveness=explosiveness;e.duration=1;e.lifetime=10;e.capacity=4096;
    e.gravityBehavior.enabled=false;e.initialVelocity={2,0,0};return asset;
}
void ticks(CpuParticleSystem& system,unsigned count){for(unsigned i=0;i<count;++i)system.tick();}
void same(const CpuParticleSystem& a,const CpuParticleSystem& b) {
    require(a.elapsed()==b.elapsed()&&a.particles().size()==b.particles().size(),"Replay changed the fixed clock or population.");
    for(std::size_t i=0;i<a.particles().size();++i) {
        const auto& x=a.particles()[i];const auto& y=b.particles()[i];
        require(x.spawnOrdinal==y.spawnOrdinal&&x.birthTime==y.birthTime&&x.age==y.age&&x.lifetime==y.lifetime&&
            x.position==y.position&&x.previousPosition==y.previousPosition&&x.velocity==y.velocity,
            "Replay changed seeded identity, exact birth substep or motion.");
    }
    require(a.stats().spawned==b.stats().spawned&&a.stats().droppedSpawns==b.stats().droppedSpawns&&
        a.stats().burstsFired==b.stats().burstsFired&&a.stats().droppedScheduleSegments==b.stats().droppedScheduleSegments,
        "Replay changed schedule or bounded-work accounting.");
}
void zeroAndCompression() {
    auto continuous=fixture();continuous.emitter.rate=7.5f;continuous.emitter.duration=.125f;
    CpuParticleSystem baseline;baseline.restart(continuous,31);ticks(baseline,120);
    require(baseline.stats().spawned==15&&baseline.stats().droppedScheduleSegments==0,"Zero explosiveness changed continuous count or introduced duration cycles.");
    for(const auto& p:baseline.particles())near(p.birthTime,static_cast<double>(p.spawnOrdinal+1)/7.5,"Zero changed legacy continuous threshold timing.");
    for(const float value:{0.f,.5f,1.f}) {
        auto asset=fixture(value);auto& e=asset.emitter;e.delay=.25f;e.finiteDuration=true;
        CpuParticleSystem system;system.restart(asset,31);ticks(system,15);
        require(system.particles().empty(),"Delay-end birth was emitted in the preceding interval.");
        ticks(system,60);
        require(system.stats().spawned==8&&system.emissionFinished()&&system.draining(),"Compression changed a one-shot cycle budget or completion boundary.");
        for(const auto& p:system.particles()) {
            const auto birth=.25+(static_cast<double>(p.spawnOrdinal+1)/8)*(1-value);
            near(p.birthTime,birth,"A compressed rate birth has the wrong time.");
            near(p.age,system.elapsed()-birth,"Newborn age did not use its exact remaining tick.");
            near(p.position[0],2*(system.elapsed()-birth),"Newborn motion integrated a whole tick instead of its actual substep.",2e-6);
        }
        const auto count=system.stats().spawned;ticks(system,60);
        require(system.stats().spawned==count,"Finite one-shot restarted after its duration.");
    }
}
void fractionalCyclesAndContinuousPeriod() {
    for(const float value:{.5f,1.f})for(const bool finite:{false,true}) {
        auto asset=fixture(value);auto& e=asset.emitter;e.rate=2.5f;e.finiteDuration=finite;e.loop=finite;
        CpuParticleSystem system;system.restart(asset,9);
        for(unsigned cycle=1;cycle<=4;++cycle) {
            ticks(system,60);require(system.stats().spawned==static_cast<std::uint64_t>(std::floor(2.5*cycle)),
                "Fractional rate mass was rounded away at a cycle boundary.");
        }
        double carry{};std::size_t index{};
        for(unsigned cycle=0;cycle<4;++cycle) {
            const auto count=static_cast<unsigned>(std::floor(carry+2.5));
            for(unsigned i=0;i<count;++i)near(system.particles()[index++].birthTime,
                cycle+((1-carry+i)/2.5)*(1-value),"Cycle compression lost fractional phase or birth ordering.");
            carry+=2.5-count;
        }
        require(!system.emissionFinished(),"Repeating explosive emission unexpectedly finished.");
    }
}
void seededFramesAndPreprocess() {
    for(const float value:{0.f,.5f,1.f}) {
        auto asset=fixture(value);auto& e=asset.emitter;e.rate=37.5f;e.duration=.125f;e.delay=.03125f;
        e.finiteDuration=true;e.loop=true;e.shape=ParticleShape::Sphere;e.radius=.7f;
        e.lifetimeRange={true,.3f,1.25f};e.velocityMode=ParticleVelocityMode::DirectionSpeed;
        e.directionMode=ParticleDirectionMode::RandomCone;e.directionSpread=35;e.speedRange={true,.2f,3};
        CpuParticleSystem a,b,c;a.restart(asset,71);b.restart(asset,71);c.restart(asset,71);
        for(unsigned i=0;i<120;++i)a.advance(1.0/30);
        for(unsigned i=0;i<240;++i)b.advance(1.0/60);
        for(unsigned i=0;i<480;++i)c.advance(1.0/120);
        same(a,b);same(a,c);
        auto warmed=asset;warmed.preprocessSeconds=1;CpuParticleSystem warm,ordinary;
        warm.restart(warmed,71);ordinary.restart(asset,71);ticks(ordinary,60);same(warm,ordinary);
        require(warm.stats().droppedCatchUpTicks==0,"Preprocessing was treated as wall-time overload.");
    }
}
void playbackEditsAndDisabledWindows() {
    auto asset=fixture(1);asset.emitter.delay=.25f;asset.emitter.duration=.25f;
    CpuParticleSystem system;system.restart(asset,17);ticks(system,16);
    require(system.stats().spawned==2,"Delayed explosive cycle did not fire at its start.");
    system.pause();const auto elapsed=system.elapsed();system.advance(5);system.tick();
    require(system.elapsed()==elapsed&&system.stats().spawned==2,"Pause advanced or replayed an explosive cycle.");
    system.play();system.tick();require(system.stats().spawned==2,"Resume duplicated a cycle-start batch.");
    system.pause();asset.emitter.explosiveness=.5f;system.updateDefinition(asset);
    require(system.state()==ParticlePlaybackState::Paused&&system.elapsed()==0&&system.particles().empty()&&system.seed()==17,
        "Explosiveness edit did not restart the schedule while preserving Pause and seed.");
    asset.preprocessSeconds=.5f;system.updateDefinition(asset);
    require(system.state()==ParticlePlaybackState::Paused&&system.stats().ticks==30,"Paused explosive pre-process edit did not finish ordinary warm-up.");
    system.stop();asset.emitter.explosiveness=1;system.updateDefinition(asset);
    require(system.state()==ParticlePlaybackState::Stopped&&system.stats().ticks==0,"Stopped schedule edit performed unwanted warm-up.");
    for(const float value:{.5f,1.f}) {
        asset=fixture(value);asset.emitter.duration=.25f;asset.emitter.enabled=false;system.restart(asset,17);ticks(system,15);
        asset.emitter.enabled=true;system.updateDefinition(asset);ticks(system,15);
        require(system.stats().spawned==2&&system.particles().front().spawnOrdinal==0,"Re-enabling replayed a skipped cycle or changed accepted birth identity.");
        asset.emitter.enabled=false;system.updateDefinition(asset);ticks(system,15);
        require(system.stats().spawned==2,"Disabled emitter produced explosive births.");
    }
    asset=fixture(1);asset.emitter.rate=0;system.restart(asset);ticks(system,120);
    require(system.particles().empty()&&system.stats().spawned==0,"Explosiveness invented births for zero Rate.");
    asset=fixture(.5f);asset.emitter.rate=4;system.restart(asset);ticks(system,15);
    asset.emitter.enabled=false;system.updateDefinition(asset);ticks(system,15);
    asset.emitter.enabled=true;system.updateDefinition(asset);ticks(system,45);
    require(system.stats().spawned==4,"Re-enabling a partially emitted cycle replayed the disabled rate window.");
    asset=fixture(.5f);asset.emitter.rate=4;system.restart(asset);ticks(system,15);
    asset.emitter.rate=8;system.updateDefinition(asset);ticks(system,15);
    require(system.stats().spawned==6&&system.stats().ticks==30,"Live rate edit restarted playback or retroactively changed preceding compressed births.");
}
void boundedSchedulesAndOrdinals() {
    for(const float value:{0.f,.5f,1.f})for(const bool finite:{false,true}) {
        auto asset=fixture(value);auto& e=asset.emitter;e.rate=768;e.duration=1.f/512;e.finiteDuration=finite;e.loop=finite;
        CpuParticleSystem full({8,4096,32}),limited({8,4096,2});full.restart(asset,57);limited.restart(asset,57);
        for(unsigned i=0;i<90;++i) {
            full.tick();limited.tick();
            if(limited.stats().spawned+limited.stats().droppedSpawns!=full.stats().spawned)std::cerr<<"case "<<value<<" finite "<<finite<<" tick "<<i<<" full "<<full.stats().spawned<<" accepted "<<limited.stats().spawned<<" dropped "<<limited.stats().droppedSpawns<<" elapsed "<<full.elapsed()<<'\n';
            require(limited.stats().spawned+limited.stats().droppedSpawns==full.stats().spawned,
                "Dropping tiny-cycle schedule segments lost fractional emission mass or counted it twice.");
        }
        require((value>0||finite)==(limited.stats().droppedScheduleSegments>0),"Schedule work cap did not match active cycle segmentation.");
        for(const auto& p:limited.particles()) {
            const auto found=std::find_if(full.particles().begin(),full.particles().end(),[&](const auto& q){return p.spawnOrdinal==q.spawnOrdinal;});
            require(found!=full.particles().end(),"Dropped schedules changed subsequent accepted spawn ordinals.");
            near(p.birthTime,found->birthTime,"Dropped schedule changed a later accepted birth substep.",1e-8);
        }
    }
    auto asset=fixture(1);asset.emitter.rate=100000;asset.emitter.capacity=5;
    CpuParticleSystem capped({8,4,32});capped.restart(asset,31);capped.tick();
    require(capped.stats().spawned==4&&capped.stats().droppedSpawns==99996,"Instantaneous rate budget bypassed the per-tick birth cap.");
    ticks(capped,60);
    require(capped.stats().spawned==5&&capped.stats().droppedSpawns==199995&&capped.particles().back().spawnOrdinal==100000,
        "Capacity/spawn caps retained a backlog or reused dropped birth identities.");
    auto slow=fixture(1);slow.emitter.duration=.25f;CpuParticleSystem overload;overload.restart(slow);overload.advance(1);
    require(overload.stats().ticks==8&&overload.stats().droppedCatchUpTicks==52&&overload.stats().spawned==2,
        "Wall-time catch-up overflow advanced explosive schedule time beyond executed ticks.");
}
void legacyBurstOrdering() {
    auto asset=fixture(1);asset.emitter.rate=2;asset.emitter.bursts={{1,0,1},{2,1,1}};
    CpuParticleSystem system;system.restart(asset,1);ticks(system,121);
    require(system.stats().spawned==8&&system.stats().burstsFired==2,"Legacy continuous bursts repeated with the new rate period or were discarded.");
    const double expected[]{0,0,0,1,1,1,2,2};
    for(std::size_t i=0;i<system.particles().size();++i)near(system.particles()[i].birthTime,expected[i],"Legacy and compressed births were not merged chronologically.");
    asset.emitter.duration=.25f;asset.emitter.finiteDuration=true;asset.emitter.loop=true;asset.emitter.rate=8;
    asset.emitter.bursts={{1,0,1}};system.restart(asset,1);ticks(system,60);
    require(system.stats().spawned==12&&system.stats().burstsFired==4,"Explicitly looping historical bursts lost their authored repetition.");
}
void nearlyInstantaneousPrecision() {
    for(const auto duration:{.01f,.013f}) {
        auto asset=fixture(.99999994f);auto& e=asset.emitter;e.duration=duration;
        e.rate=duration==.01f?100.f:1000.f;e.lifetime=60;e.capacity=MaximumParticleCapacity;
        auto instantaneous=asset;instantaneous.emitter.explosiveness=1;
        CpuParticleSystem full({8,4096,32}),limited({8,4096,1}),reference({8,4096,32});
        full.restart(asset,41);limited.restart(asset,41);reference.restart(instantaneous,41);
        for(unsigned tick=0;tick<1200;++tick) {
            full.tick();limited.tick();reference.tick();
            require(full.stats().spawned==reference.stats().spawned&&
                limited.stats().spawned+limited.stats().droppedSpawns==full.stats().spawned,
                "Nearly instantaneous nonbinary windows lost rate mass through floating-point cancellation or schedule drops.");
        }
        require(limited.stats().droppedScheduleSegments>0,"Precision fixture did not exercise dropped cycle segments.");
        // These float durations deliberately straddle nominal integer budgets:
        // do not round .01f*100 to one and lose the fractional carry contract.
        const double cycles=std::ceil(full.elapsed()/static_cast<double>(duration)-1e-10);
        const auto expected=static_cast<std::uint64_t>(std::floor(cycles*static_cast<double>(duration)*e.rate+1e-12));
        require(full.stats().spawned==expected,"Cycle totals ignored the actual authored float rate/duration budget.");
        for(const auto& p:limited.particles()) {
            require(p.spawnOrdinal<full.particles().size()&&full.particles()[p.spawnOrdinal].spawnOrdinal==p.spawnOrdinal,
                "Precision-related schedule drops changed later birth ordinals.");
            near(p.birthTime,full.particles()[p.spawnOrdinal].birthTime,"Precision-related schedule drops changed later birth timing.",1e-8);
        }
    }
}
}
int main(){try{zeroAndCompression();fractionalCyclesAndContinuousPeriod();seededFramesAndPreprocess();playbackEditsAndDisabledWindows();boundedSchedulesAndOrdinals();legacyBurstOrdering();nearlyInstantaneousPrecision();
    std::cout<<"Particle explosiveness timing, fractional cycles, replay, lifecycle and work-cap checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
