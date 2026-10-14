#include "assets/ParticleAsset.h"
#include "editor/ParticleDocument.h"
#include "editor/SceneParticles.h"
#include "particles/CpuParticleSystem.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
using namespace crimson;
void require(bool condition,const std::string& message) { if (!condition) throw std::runtime_error(message); }
void expectNear(double actual,double expected,const std::string& message,double tolerance=1e-5) {
    require(std::isfinite(actual) && std::abs(actual-expected)<tolerance,message);
}
template<class Action> void rejects(Action action) {
    bool rejected=false;
    try { action(); } catch (const std::exception&) { rejected=true; }
    require(rejected,"Invalid particle input was accepted.");
}
struct TempProject {
    std::filesystem::path root;
    TempProject() {
        static uint64_t sequence{};
        root=std::filesystem::temp_directory_path()/("crimson-particle-test-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++sequence));
        std::filesystem::create_directories(root);
    }
    ~TempProject() {
        std::error_code error;
        const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        const auto target=std::filesystem::weakly_canonical(root,error);
        if (!error && target.parent_path()==parent && target.filename().string().starts_with("crimson-particle-test-"))
            std::filesystem::remove_all(target,error);
    }
};
std::string readText(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void sameSimulation(const CpuParticleSystem& a,const CpuParticleSystem& b) {
    require(a.particles().size()==b.particles().size(),"Particle counts differ.");
    expectNear(a.elapsed(),b.elapsed(),"Simulated time differs.",1e-10);
    for (size_t i=0;i<a.particles().size();++i) {
        const auto& x=a.particles()[i]; const auto& y=b.particles()[i];
        require(x.spawnOrdinal==y.spawnOrdinal,"Spawn identity changed.");
        expectNear(x.age,y.age,"Particle age differs.",1e-10);
        for (size_t j=0;j<3;++j) { expectNear(x.position[j],y.position[j],"Particle trajectory differs."); expectNear(x.velocity[j],y.velocity[j],"Particle velocity differs."); }
    }
}
void timingAndReplay() {
    auto effect=defaultParticleEffect();
    effect.emitter.initialVelocity={0,6,0}; // Explicit launch, independent of falling starter defaults.
    effect.emitter.rate=90;
    CpuParticleSystem system; system.restart(effect,42);
    system.tick();
    require(system.particles().size()==1,"First tick emitted the wrong count.");
    const auto& particle=system.particles().front();
    const auto age=ParticleFixedStep-1.0/90.0;
    expectNear(particle.age,age,"Newborn was advanced for a full tick.",1e-10);
    expectNear(particle.position[1],6.0*age-4.905*age*age,"Constant gravity integration is wrong.");
    for (int i=0;i<80;++i) system.tick();
    std::unordered_set<uint64_t> identities;
    for (const auto& p:system.particles()) require(identities.insert(p.spawnOrdinal).second,"Compaction duplicated an identity.");
    require(!system.particles().empty() && system.particles().front().spawnOrdinal>0,"Lifetime expiry did not compact old particles.");
    CpuParticleSystem repeated; repeated.restart(effect,42);
    for (int i=0;i<81;++i) repeated.tick();
    sameSimulation(system,repeated);
    require(repeated.seed()==42,"Restart did not retain the requested seed.");

    CpuParticleSystem slow,fast;
    slow.restart(effect,7); fast.restart(effect,7);
    for (int i=0;i<60;++i) slow.advance(1.0/60.0);
    for (int i=0;i<240;++i) fast.advance(1.0/240.0);
    sameSimulation(slow,fast);
}
void fallingStarterAndLaunch() {
    const auto effect=defaultParticleEffect();
    require(effect.emitter.initialVelocity==Vec3{0,0,0},"Starter launches particles instead of starting at rest.");
    CpuParticleSystem system;system.restart(effect);
    for(int tick=0;tick<50;++tick) {
        system.tick();
        for(const auto& particle:system.particles()) {
            require(particle.position[1]<=0 && particle.velocity[1]<=0,"Negative gravity launched a resting starter upward.");
            expectNear(particle.position[1],.5*effect.emitter.gravity[1]*particle.age*particle.age,"Falling starter trajectory is wrong.");
        }
    }
    auto launched=effect;launched.emitter.initialVelocity={0,6,0};
    system.restart(launched);
    for(int tick=0;tick<10;++tick)system.tick();
    require(system.particles().front().position[1]>0 && system.particles().front().velocity[1]>0,"Explicit upward initial velocity should rise against gravity.");
    for(int tick=0;tick<40;++tick)system.tick();
    require(system.particles().front().velocity[1]<0,"Negative gravity did not reverse an explicit upward launch.");
}
void pauseAndBounds() {
    auto effect=defaultParticleEffect(); effect.emitter.rate=120;
    CpuParticleSystem system,continuous;
    system.restart(effect,3); continuous.restart(effect,3);
    system.advance(ParticleFixedStep*2.5); continuous.advance(ParticleFixedStep*2.5);
    std::vector<ParticlePresentation> before,paused;
    system.appendPresentation(before);
    system.pause(); system.advance(1000); system.appendPresentation(paused);
    sameSimulation(system,continuous);
    require(before.size()==paused.size(),"Pause changed presentation count.");
    for (size_t i=0;i<before.size();++i) require(before[i].position==paused[i].position,"Pause moved an interpolated particle.");
    system.play(); system.advance(ParticleFixedStep*.5); continuous.advance(ParticleFixedStep*.5);
    sameSimulation(system,continuous);
    system.stop(); require(system.particles().empty() && system.elapsed()==0,"Stop retained transient particle state.");
    system.advance(1); require(system.elapsed()==0,"Stopped simulation advanced.");
    system.play(); require(system.state()==ParticlePlaybackState::Running && system.seed()==3,"Play after Stop did not retain its seed.");

    effect.emitter.capacity=2; effect.emitter.rate=600; effect.emitter.lifetime=100;
    CpuParticleSystem bounded({2,3}); bounded.restart(effect);
    bounded.advance(1);
    require(bounded.stats().ticks==2 && bounded.stats().droppedCatchUpTicks==58,"Catch-up work was not bounded.");
    require(bounded.particles().size()==2 && bounded.stats().droppedSpawns==18,"Spawn/capacity bounds were not enforced.");
    expectNear(bounded.elapsed(),ParticleFixedStep*2,"Dropped wall time advanced the effect.",1e-10);
    const auto first=bounded.particles().front().spawnOrdinal;
    bounded.tick(); require(bounded.particles().front().spawnOrdinal==first,"Overflow removed existing valid particles.");
    auto lowerCapacity=effect; lowerCapacity.emitter.capacity=1;
    bounded.restart(lowerCapacity); require(bounded.reservedCapacity()==1,"Restart retained a larger previous reservation.");
    bounded.advance(std::numeric_limits<double>::infinity()); require(bounded.elapsed()==0,"Invalid timer input advanced the effect.");
}
void transformsAndPresentation() {
    auto effect=defaultParticleEffect(); effect.emitter.rate=90; effect.emitter.initialVelocity={2,3,4};
    effect.emitter.color={.5f,.04045f,1}; effect.emitter.intensity=10;
    Mat4 world=math::translation({5,6,7})*math::scaling({-2,3,4}); world[4]=.7f;
    CpuParticleSystem system; system.restart(effect,1,world); system.tick();
    const auto first=system.particles().front();
    const auto velocity=math::transformDirection(world,effect.emitter.initialVelocity);
    const auto expected=math::transformPoint(world,{0,0,0})+velocity*first.age+effect.emitter.gravity*(.5*first.age*first.age);
    for (size_t i=0;i<3;++i) expectNear(first.position[i],expected[i],"Spawn discarded an affine transform.");
    system.setTransform(math::translation({100,0,0})); system.tick();
    const auto expectedOld=first.position+first.velocity*ParticleFixedStep+effect.emitter.gravity*(.5*ParticleFixedStep*ParticleFixedStep);
    for (size_t i=0;i<3;++i) expectNear(system.particles().front().position[i],expectedOld[i],"World-space particle followed its moved emitter.");
    require(system.particles().back().position[0]>99,"New births ignored the moved emitter.");
    system.advance(ParticleFixedStep*.75);
    std::vector<ParticlePresentation> frame; system.appendPresentation(frame);
    require(!frame.empty(),"Live particles produced no presentation.");
    expectNear(frame.front().color[0],2.1404114,"Authored sRGB did not convert to linear HDR.");
    expectNear(frame.front().color[2],10,"HDR intensity was clamped.");
    expectNear(frame.front().size,.08,"Size was not preserved in world meters.");
    require(frame.front().color[3]==1,"Presentation opacity differs from the procedural sprite contract.");
    auto invalid=world; invalid[0]=std::numeric_limits<float>::infinity();
    require(!system.setTransform(invalid),"Invalid emitter transform was installed.");
}
struct LiveParticleSnapshot {
    std::vector<ParticleState> records;
    ParticleStats stats;
    ParticlePlaybackState playback;
    double elapsed{}, interpolation{};
    uint64_t seed{};
    const ParticleState* storage{};
    size_t reserved{};
    explicit LiveParticleSnapshot(const CpuParticleSystem& system)
        :records(system.particles().begin(),system.particles().end()),stats(system.stats()),playback(system.state()),
         elapsed(system.elapsed()),interpolation(system.interpolation()),seed(system.seed()),
         storage(system.particles().data()),reserved(system.reservedCapacity()) {}
};
void preservesLiveState(const CpuParticleSystem& system,const LiveParticleSnapshot& before) {
    require(system.state()==before.playback && system.elapsed()==before.elapsed && system.interpolation()==before.interpolation &&
        system.seed()==before.seed,"Live definition update reset playback, clocks or explicit seed.");
    require(system.particles().data()==before.storage && system.reservedCapacity()==before.reserved && system.particles().size()==before.records.size(),
        "Ordinary definition update replaced live particle storage.");
    for (size_t i=0;i<before.records.size();++i) {
        const auto& current=system.particles()[i]; const auto& old=before.records[i];
        require(current.spawnOrdinal==old.spawnOrdinal && current.previousPosition==old.previousPosition && current.position==old.position &&
            current.velocity==old.velocity && current.age==old.age && current.lifetime==old.lifetime && current.birthTime==old.birthTime,
            "Ordinary definition update changed an authoritative particle record.");
    }
    const auto& current=system.stats(); const auto& old=before.stats;
    require(current.live==old.live && current.ticks==old.ticks && current.spawned==old.spawned && current.droppedSpawns==old.droppedSpawns &&
        current.droppedCatchUpTicks==old.droppedCatchUpTicks && current.droppedWallTime==old.droppedWallTime &&
        current.clearedCapacityParticles==old.clearedCapacityParticles,"Ordinary definition update reset simulation counters.");
}
void liveAppearanceAndDocumentChanges() {
    TempProject project; ParticleEffectDocument document(project.root);
    require(document.create(),"Cannot create live-update document fixture.");
    CpuParticleSystem preview; preview.restart(document.definition(),77); preview.advance(ParticleFixedStep*4.5); preview.pause();
    const LiveParticleSnapshot baseline(preview);
    std::vector<ParticlePresentation> original; preview.appendPresentation(original);
    require(!original.empty(),"Live-update fixture has no visible particles.");
    require(document.begin("Appearance drag") && document.update([](auto& asset) {
        asset.emitter.color={.5f,.2f,.1f}; asset.emitter.size=.2f; asset.emitter.intensity=2;asset.emitter.opacity=.4f;
        asset.emitter.material=AssetRef<MaterialAsset>{"assets/materials/appearance.crimson-material"};
    }),"Cannot update live appearance transaction.");
    preview.updateDefinition(document.definition());
    preservesLiveState(preview,baseline);
    std::vector<ParticlePresentation> changed; preview.appendPresentation(changed);
    require(changed.size()==original.size() && changed.front().position==original.front().position,"Paused appearance update moved or removed visible particles.");
    expectNear(changed.front().color[0],particleSrgbToLinear(.5f)*2,"Live color/intensity did not affect existing particles.");
    expectNear(changed.front().size,.2,"Live size did not affect existing particles.");
    expectNear(changed.front().color[3],.4,"Opacity did not update paused live particles.");
    require(document.commit(),"Cannot commit live appearance transaction.");
    preview.updateDefinition(*document.committed()); preservesLiveState(preview,baseline);
    require(document.undo(),"Cannot undo live appearance.");
    preview.updateDefinition(*document.committed()); preservesLiveState(preview,baseline);
    changed.clear(); preview.appendPresentation(changed);
    require(changed.front().color==original.front().color && changed.front().size==original.front().size,"Undo did not restore appearance on existing particles.");
    require(document.redo(),"Cannot redo live appearance.");
    preview.updateDefinition(*document.committed()); preservesLiveState(preview,baseline);
    require(document.begin("Canceled live appearance") && document.update([](auto& asset){asset.emitter.color={0,1,0};}),"Cannot prepare live cancel.");
    preview.updateDefinition(document.definition()); preservesLiveState(preview,baseline);
    document.cancel(); preview.updateDefinition(document.definition()); preservesLiveState(preview,baseline);
    changed.clear(); preview.appendPresentation(changed);
    expectNear(changed.front().color[0],particleSrgbToLinear(.5f)*2,"Cancel did not restore prior live appearance.");
    preview.stop(); const LiveParticleSnapshot stopped(preview);
    require(document.edit("Stopped intensity",[](auto& asset){asset.emitter.intensity=4;}),"Cannot update stopped fixture.");
    preview.updateDefinition(*document.committed()); preservesLiveState(preview,stopped);
    require(document.edit("Authored seed",[](auto& asset){asset.defaultSeed=9;}),"Cannot update authored seed.");
    preview.updateDefinition(*document.committed());
    require(preview.seed()==9 && preview.state()==ParticlePlaybackState::Stopped && preview.elapsed()==0,"Authored seed update restarted a stopped effect.");
    preview.play(); require(preview.seed()==9 && preview.definition().emitter.intensity==4,"Play did not use the current live definition/seed.");
}
void liveMotionAndEmission() {
    auto effect=defaultParticleEffect(); effect.emitter.rate=60; effect.emitter.lifetime=100;
    effect.emitter.initialVelocity={1,2,3}; effect.emitter.gravity={0,0,0};
    CpuParticleSystem system; system.restart(effect,123); system.advance(ParticleFixedStep*4.5);
    const LiveParticleSnapshot before(system);
    const auto old=system.particles().front();
    effect.emitter.gravity={0,-120,0}; effect.emitter.initialVelocity={5,0,0}; effect.emitter.lifetime=.05f;
    system.updateDefinition(effect); preservesLiveState(system,before);
    system.advance(ParticleFixedStep*.5);
    const auto& retained=system.particles().front(); const auto& newborn=system.particles().back();
    expectNear(retained.velocity[0],1,"Initial velocity update retroactively changed a live particle.");
    expectNear(retained.velocity[1],old.velocity[1]-120*ParticleFixedStep,"Gravity update did not affect the next tick.");
    expectNear(retained.position[1],old.position[1]+old.velocity[1]*ParticleFixedStep-60*ParticleFixedStep*ParticleFixedStep,
        "Live gravity update integrated the wrong trajectory.");
    require(retained.lifetime==old.lifetime && newborn.lifetime==effect.emitter.lifetime,"Lifetime update was not restricted to newborns.");
    require(newborn.spawnOrdinal>old.spawnOrdinal && newborn.velocity==effect.emitter.initialVelocity,"Newborn did not use current initial velocity.");

    auto phased=defaultParticleEffect(); phased.emitter.rate=30; phased.emitter.gravity={0,0,0};
    CpuParticleSystem emission; emission.restart(phased); emission.tick();
    require(emission.particles().empty(),"Phase fixture emitted too early.");
    phased.emitter.enabled=false; emission.updateDefinition(phased);
    for (int i=0;i<3;++i) emission.tick();
    require(emission.particles().empty() && emission.stats().ticks==4,"Disabling emission stopped the simulation clock or emitted particles.");
    phased.emitter.enabled=true; phased.emitter.rate=90; emission.updateDefinition(phased); emission.tick();
    require(emission.particles().size()==2,"Rate/enabled update lost the fractional emission phase.");
    expectNear(emission.particles().front().age,ParticleFixedStep-1.0/180.0,"New rate used an incorrect retained birth phase.",1e-10);
    const auto live=emission.particles().size(); phased.emitter.enabled=false; emission.updateDefinition(phased); emission.tick();
    require(emission.particles().size()==live,"Disabling emission cleared live particles.");
}
void liveCapacityAndRejection() {
    auto effect=defaultParticleEffect(); effect.emitter.rate=90; effect.emitter.lifetime=100; effect.emitter.capacity=8;
    CpuParticleSystem system; system.restart(effect,555); system.advance(ParticleFixedStep*3.5); system.pause();
    const LiveParticleSnapshot before(system);
    const auto lastOrdinal=system.particles().back().spawnOrdinal;
    effect.emitter.capacity=2; system.updateDefinition(effect);
    require(system.particles().empty() && system.reservedCapacity()==2 && system.stats().live==0 &&
        system.stats().clearedCapacityParticles==before.records.size(),"Capacity change did not replace/clear its exact reservation.");
    require(system.state()==before.playback && system.elapsed()==before.elapsed && system.interpolation()==before.interpolation && system.seed()==555 &&
        system.stats().spawned==before.stats.spawned && system.stats().ticks==before.stats.ticks && system.stats().droppedSpawns==before.stats.droppedSpawns,
        "Capacity change reset clocks, pause, seed or cumulative counters.");
    system.advance(20); require(system.particles().empty() && system.elapsed()==before.elapsed,"Capacity update resumed a paused simulation.");
    system.play(); system.advance(ParticleFixedStep*.5);
    require(system.particles().size()==2 && system.particles().front().spawnOrdinal==lastOrdinal+1,"Capacity change reset spawn ordinal or emission phase.");
    const auto cleared=system.stats().clearedCapacityParticles;
    const auto elapsed=system.elapsed(); const auto ticks=system.stats().ticks;
    effect.emitter.capacity=16; system.updateDefinition(effect);
    require(system.reservedCapacity()==16 && system.particles().empty() && system.stats().clearedCapacityParticles==cleared+2 &&
        system.elapsed()==elapsed && system.stats().ticks==ticks,"Growing capacity restarted playback or retained historical allocation.");
    system.tick();
    const LiveParticleSnapshot valid(system); const auto validDefinition=system.definition();
    auto invalid=validDefinition; invalid.emitter.lifetime=-1;
    rejects([&]{system.updateDefinition(invalid);}); preservesLiveState(system,valid);
    invalid=validDefinition; invalid.emitter.capacity=0;
    rejects([&]{system.updateDefinition(invalid);}); preservesLiveState(system,valid);
    invalid=validDefinition; invalid.emitter.color[0]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{system.updateDefinition(invalid);}); preservesLiveState(system,valid);
    invalid=validDefinition; invalid.id="assets/particles/other.crimson-particle";
    rejects([&]{system.updateDefinition(invalid);}); preservesLiveState(system,valid);
    invalid=validDefinition; invalid.emitter.id=2;
    rejects([&]{system.updateDefinition(invalid);}); preservesLiveState(system,valid);
    require(system.definition()==validDefinition,"Rejected update replaced the last valid runtime definition.");
}
void persistenceAndValidation() {
    TempProject project;
    auto effect=createParticleAsset(project.root);
    const auto second=createParticleAsset(project.root);
    require(effect.id!=second.id && effect.name!=second.name,"Creation reused identity or display name.");
    effect.defaultSeed=std::numeric_limits<uint64_t>::max(); effect.emitter.id=0xabcdef;
    effect.emitter.gravity={1,-20,3}; effect.emitter.color={.1f,.2f,.3f};
    saveParticleAsset(project.root,effect);
    require(loadParticleAsset(project.root,effect.id)==effect,"Native particle round-trip changed authored values.");
    const auto original=readText(particleAssetPath(project.root,effect.id));
    const auto copied=saveParticleAssetAs(project.root,effect,"Independent effect");
    require(copied.id!=effect.id && copied.emitter==effect.emitter,"Save As failed to copy the emitter under a distinct identity.");
    require(readText(particleAssetPath(project.root,effect.id))==original,"Save As changed its source file.");
    auto malformed=nlohmann::json::parse(original); malformed["emitters"][0]["capacity"]=1.5;
    rejects([&]{deserializeParticleAsset(malformed.dump(),effect.id);});
    malformed=nlohmann::json::parse(original); malformed["version"]=5;
    rejects([&]{deserializeParticleAsset(malformed.dump(),effect.id);});
    auto legacy=nlohmann::json::parse(original);legacy["version"]=1;
    legacy["emitters"][0].erase("material");legacy["emitters"][0].erase("opacity");
    legacy["emitters"][0]["renderer"]="procedural-additive";
    const auto migrated=deserializeParticleAsset(legacy.dump(),effect.id);
    require(migrated==effect,"Step 1 migration changed the saved particle definition.");
    malformed=nlohmann::json::parse(original);malformed["emitters"][0]["opacity"]=-.1f;
    rejects([&]{deserializeParticleAsset(malformed.dump(),effect.id);});
    rejects([&]{loadParticleAsset(project.root,"assets/particles/../material.json");});
    rejects([&]{deserializeParticleAsset(original,second.id);});
    std::ofstream bad(project.root/"assets/particles/broken.crimson-particle"); bad<<"{}"; bad.close();
    std::vector<std::string> diagnostics;
    require(scanParticleAssets(project.root,&diagnostics).size()==3 && diagnostics.size()==1,"Catalog scan did not isolate a malformed asset.");
    auto invalid=effect; invalid.emitter.lifetime=0;
    rejects([&]{saveParticleAsset(project.root,invalid);});
    require(readText(particleAssetPath(project.root,effect.id))==original,"Failed validation replaced a valid file.");
}
void documentPublicationAndHistory() {
    TempProject project; ParticleEffectDocument document(project.root);
    require(document.create(),"Particle document creation failed.");
    require(document.isOpen() && !document.dirty(),"New saved particle document is dirty or closed.");
    const auto original=document.definition(); const auto revision=document.revision(); const auto preview=document.previewRevision();
    const auto committed=document.committed();
    require(document.begin("Gravity drag"),"Cannot begin particle transaction.");
    require(document.update([](auto& asset){asset.emitter.gravity[1]=-20;}),"Valid intermediate value failed.");
    require(document.update([](auto& asset){asset.emitter.gravity[1]=-30;}),"Second intermediate value failed.");
    require(document.previewRevision()>preview && document.revision()==revision && document.committed()==committed,"Intermediate drag published to scene instances.");
    require(document.commit() && document.revision()>revision,"Committed edit did not publish a revision.");
    require(document.dirty() && document.undo() && document.definition()==original && !document.canUndo(),"Drag was not one undo command.");
    require(!document.dirty() && document.redo() && document.dirty(),"Undo/redo did not follow the saved state.");
    const auto saveRevision=document.revision();
    require(document.save() && !document.dirty() && document.revision()==saveRevision,"Save republished or did not mark the definition clean.");
    require(document.undo() && document.dirty() && document.redo() && !document.dirty(),"Saved-state tokens did not follow undo/redo.");
    require(document.edit("Rate",[](auto& asset){asset.emitter.rate=20;}) && document.undo(),"Cannot prepare redo test.");
    const auto beforeFailure=document.revision();
    require(!document.edit("Invalid lifetime",[](auto& asset){asset.emitter.lifetime=-1;}),"Invalid definition was committed.");
    require(document.canRedo() && document.revision()==beforeFailure,"Rejected edit cleared redo or published a revision.");
    require(document.begin("Cancel drag") && document.update([](auto& asset){asset.emitter.size=.5f;}),"Cannot prepare cancel test.");
    document.cancel(); require(document.definition().emitter.size==original.emitter.size && document.canRedo(),"Canceled drag changed authored data or redo.");
    require(document.begin("Invalid intermediate") && !document.update([](auto& asset){asset.emitter.capacity=0;}) && !document.commit(),"Invalid intermediate committed.");
    document.cancel();
    const auto prior=document.definition();
    require(!document.load("assets/particles/missing.crimson-particle") && document.definition()==prior,"Failed load replaced the active document.");

    const auto sourceId=document.assetId(); const auto sourceText=readText(particleAssetPath(project.root,sourceId));
    require(document.save("Saved copy",true) && document.assetId()!=sourceId && !document.dirty(),"Document Save As failed.");
    const auto copiedId=document.assetId();
    require(document.undo() && document.assetId()==copiedId && document.dirty(),"Save As undo resurrected the source identity.");
    require(document.redo() && document.assetId()==copiedId && !document.dirty() && document.definition().name=="Saved copy","Save As redo lost the saved name/state.");
    require(readText(particleAssetPath(project.root,sourceId))==sourceText,"Document Save As modified its original.");
    require(document.edit("Unsaved",[](auto& asset){asset.emitter.rate=70;}) && document.discard() && !document.dirty(),"Discard did not restore the last saved definition.");
    require(document.definition().emitter.rate==prior.emitter.rate,"Discard retained an unsaved value.");
    require(document.edit("Write failure",[](auto& asset){asset.emitter.rate=60;}),"Cannot prepare failed save.");
    const auto diskBefore=readText(particleAssetPath(project.root,copiedId));
#ifdef _WIN32
    const auto file=particleAssetPath(project.root,copiedId);
    require(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"Cannot prepare read-only save fixture.");
    const auto failed=!document.save();
    const auto unchanged=readText(file)==diskBefore;
    SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL);
    require(failed && unchanged && document.dirty() && document.assetId()==copiedId,"Failed atomic save changed its file, identity or dirty state.");
#endif
    require(document.save() && !document.dirty(),"Save did not recover after failure.");
    ParticleEffectDocument reopened(project.root);
    require(reopened.load(copiedId) && reopened.definition()==document.definition(),"Saved particle document did not reopen.");
}
void sceneReconciliation() {
    auto effect=std::make_shared<ParticleEffectAsset>(defaultParticleEffect()); effect->id="assets/particles/test.crimson-particle";
    effect->emitter.rate=120;
    uint64_t revision=1; bool missing=false;
    const SceneParticles::DefinitionQuery query=[&](const auto&)->ParticleDefinitionVersion {
        return {missing?nullptr:effect,revision,missing?"Missing fixture":std::string{}};
    };
    std::vector<ExtractedParticle> sources{{1,effect->id,math::translation({0,0,0})},{2,effect->id,math::translation({5,0,0})}};
    SceneParticles runtime; runtime.reconcile(7,sources,query,ParticleFixedStep*2);
    require(runtime.groups().size()==2 && runtime.groups()[0].entity==1 && runtime.groups()[1].entity==2 &&
        runtime.groups()[0].materialId==effect->emitter.material.id && runtime.groups()[0].first==0 &&
        runtime.groups()[1].first==runtime.groups()[0].count &&
        runtime.groups()[1].first+runtime.groups()[1].count==runtime.presentation().size(),
        "Scene particle material groups lost stable entity order or presentation ranges.");
    require(runtime.instance(1) && runtime.instance(2) && runtime.instance(1)!=runtime.instance(2),"Scene effect instances are not independent.");
    const auto first=runtime.instance(1)->particles().front(); const auto elapsed=runtime.instance(1)->elapsed();
    sources[0].world=math::translation({20,0,0});
    runtime.reconcile(7,sources,query,0);
    require(runtime.instance(1)->particles().front().position==first.position && runtime.instance(1)->elapsed()==elapsed,"Transform edit restarted or moved existing world-space particles.");
    runtime.reconcile(7,sources,query,0,1);
    require(runtime.instance(1)->elapsed()==0 && runtime.instance(2)->elapsed()==elapsed,"Selected replay reset unrelated instances.");
    runtime.reconcile(7,sources,query,ParticleFixedStep);
    const auto timeBeforePublication=runtime.instance(1)->elapsed();
    const auto otherTimeBeforePublication=runtime.instance(2)->elapsed();
    const auto positionBeforePublication=runtime.instance(1)->particles().front().position;
    ++revision; runtime.reconcile(7,sources,query,0);
    require(runtime.instance(1)->elapsed()==timeBeforePublication && runtime.instance(2)->elapsed()==otherTimeBeforePublication &&
        runtime.instance(1)->particles().front().position==positionBeforePublication,"Definition publication restarted its live instances.");
    effect->emitter.color={.5f,0,0}; ++revision; runtime.reconcile(7,sources,query,0);
    require(runtime.instance(1)->elapsed()==timeBeforePublication && runtime.instance(2)->elapsed()==otherTimeBeforePublication &&
        !runtime.presentation().empty(),"Live color publication cleared scene particles.");
    expectNear(runtime.presentation().front().color[0],particleSrgbToLinear(.5f),"Scene color publication did not update visible particles.");
    runtime.reconcile(8,sources,query,0);
    require(runtime.instance(1)->particles().empty(),"Document replacement retained transient state.");
    missing=true; runtime.reconcile(8,sources,query,0);
    require(!runtime.instance(1) && !runtime.status().empty(),"Unresolved effect retained a live instance without a diagnostic.");
    missing=false;
    sources[0].world=math::scaling({std::numeric_limits<float>::max(),1,1})*math::scaling({2,1,1});
    runtime.reconcile(8,sources,query,0);
    require(!runtime.instance(1) && runtime.instance(2) && runtime.status().find("invalid composed")!=std::string::npos,
        "Invalid composed transform crashed or retained a live instance without a diagnostic.");
    sources[0].world=Mat4{};
    runtime.reconcile(8,sources,query,0);
    require(runtime.instance(1) && runtime.instance(1)->elapsed()==0,"Corrected emitter transform did not resume with a fresh instance.");
    effect->emitter.capacity=MaximumParticleCapacity;
    sources.clear(); for (EntityId id=1;id<=5;++id) sources.push_back({id,effect->id,{}});
    runtime.reconcile(9,sources,query,0);
    require(runtime.instance(4) && !runtime.instance(5) && !runtime.status().empty(),"Scene aggregate capacity was not bounded.");
}
}

int main() {
    try {
        timingAndReplay(); fallingStarterAndLaunch(); pauseAndBounds(); transformsAndPresentation();
        liveAppearanceAndDocumentChanges(); liveMotionAndEmission(); liveCapacityAndRejection(); persistenceAndValidation();
        documentPublicationAndHistory(); sceneReconciliation();
        std::cout<<"Particle CPU, asset, document and scene reconciliation checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
