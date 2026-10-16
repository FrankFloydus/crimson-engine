#include "assets/ParticleAsset.h"
#include "assets/MaterialAsset.h"
#include "resources/AssetId.h"
#include "particles/ParticleAppearance.h"
#include "editor/ParticleDocument.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
using Json=nlohmann::json;
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
ParticleEffectAsset current(ParticleEffectAsset effect) {
    effect.id=canonicalAssetId(effect.id);effect.emitter.material.id=canonicalAssetId(effect.emitter.material.id);return effect;
}
template<class Action> void rejects(Action action,const char* message) {
    bool rejected=false;
    try { action(); } catch (const std::exception&) { rejected=true; }
    require(rejected,message);
}
struct Project {
    std::filesystem::path root;
    Project() {
        static uint64_t sequence{};
        root=std::filesystem::temp_directory_path()/("crimson-particle-step3-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++sequence));
        std::filesystem::create_directories(root);
    }
    ~Project() {
        std::error_code error;
        const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        const auto target=std::filesystem::weakly_canonical(root,error);
        if (!error && target.parent_path()==parent && target.filename().string().starts_with("crimson-particle-step3-"))
            std::filesystem::remove_all(target,error);
    }
};
std::string read(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void legacyEmissionFields(Json& source) {
    auto& emitter=source["emitters"][0];
    emitter["bursts"]=emitter.value("legacyBursts",Json::array());
    emitter.erase("legacyBursts"); emitter.erase("explosiveness");
}
ParticleEffectAsset authoredEffect() {
    auto effect=defaultParticleEffect("Fountain variation");
    effect.id="assets/particles/fountain.crimson-particle";
    effect.defaultSeed=std::numeric_limits<uint64_t>::max();
    auto& e=effect.emitter;
    e.id=0xabcdefedcb123456ull; e.name="Fountain";
    e.shape=ParticleShape::Cone; e.radius=.7f; e.boxSize={2,3,4}; e.space=ParticleSpace::Local;
    e.lifetime=.9f; e.lifetimeRange={true,.4f,1.2f};
    e.initialVelocity={1,2,3}; e.velocityMode=ParticleVelocityMode::DirectionSpeed;
    e.direction={.2f,2,-.1f}; e.directionMode=ParticleDirectionMode::RandomCone; e.directionSpread=35; e.directionFlatness=.65f;
    e.speed=4; e.speedRange={true,3,6}; e.delay=.25f; e.finiteDuration=true; e.duration=2; e.loop=true;
    // Authoring order and stable identities survive save, even when times are unordered.
    e.bursts={{0xffffffffffffffffull,.7f,15},{12,0,25},{25,.2f,7}};
    e.gravity={0,-5,0}; e.gravityBehavior={true,false};
    e.accelerationBehavior={true,true}; e.acceleration={2,-1,3}; e.dragBehavior={true,true}; e.drag=.8f;
    e.material=AssetRef<MaterialAsset>{canonicalAssetId("assets/materials/water.crimson-material")};
    e.opacity=.6f;
    return effect;
}
void versionFourPersistence() {
    Project project;
    const auto starter=defaultParticleEffect();
    require(starter.emitter.material.id==DefaultParticleMaterialId,"Particle starter material differs from the protected Engine UUID.");
    require(starter.emitter.directionSpread==0 && starter.emitter.directionFlatness==0,
        "New point effects do not use neutral spread/flatness defaults.");
    auto effect=authoredEffect();
    const auto encoded=serializeParticleAsset(effect);
    require(Json::parse(encoded).at("version")==9,"New particle definitions do not use schema v9.");
    require(deserializeParticleAsset(encoded,effect.id)==current(effect),"v4 lost timing, ranges, behavior settings or identities.");
    saveParticleAsset(project.root,effect);
    require(loadParticleAsset(project.root,effect.id)==current(effect),"Saved v4 effect did not reopen identically.");
    const auto copy=saveParticleAssetAs(project.root,effect,"Independent fountain");
    require(copy.id!=effect.id && copy.emitter==effect.emitter && copy.defaultSeed==effect.defaultSeed,
        "Save As lost stable burst identity, references or authored variation.");
    require(read(particleAssetPath(project.root,effect.id))==encoded,"Save As changed its source.");
    for (auto shape:{ParticleShape::Point,ParticleShape::Cone,ParticleShape::Sphere,ParticleShape::Box})
        for (auto space:{ParticleSpace::World,ParticleSpace::Local})
            for (auto mode:{ParticleDirectionMode::Fixed,ParticleDirectionMode::RandomCone,ParticleDirectionMode::Outward}) {
                effect.emitter.shape=shape; effect.emitter.space=space; effect.emitter.directionMode=mode;
                require(deserializeParticleAsset(serializeParticleAsset(effect))==current(effect),"An implemented shape, space or direction mode lost its identity.");
            }
    effect.emitter.gravityBehavior={false,false}; effect.emitter.accelerationBehavior={false,true}; effect.emitter.dragBehavior={true,false};
    effect.emitter.lifetimeRange.enabled=false; effect.emitter.speedRange.enabled=false; effect.emitter.velocityMode=ParticleVelocityMode::Vector;
    require(deserializeParticleAsset(serializeParticleAsset(effect))==current(effect),"Inactive controls lost their settings.");
}
Json oldDefinition(int version) {
    Json emitter={{"id","ae12"},{"name","Old impact"},{"enabled",false},{"rate",83.5f},{"lifetime",2.7f},
        {"initialVelocity",Vec3{1,7,-2}},{"gravity",Vec3{1,-15,4}},{"size",.21f},{"color",Vec3{.2f,.3f,.4f}},
        {"intensity",2.5f},{"capacity",151},{"space","world"},{"renderer",version==1?"procedural-additive":"sprite"},{"colorSpace","srgb"}};
    if (version==2) { emitter["material"]="assets/materials/legacy.crimson-material"; emitter["opacity"]=.35f; }
    return {{"version",version},{"type","crimson-particle"},{"id","assets/particles/old.crimson-particle"},
        {"name","Old effect"},{"defaultSeed","abcdef1234567890"},{"emitters",Json::array({emitter})}};
}
void priorVersionMigration() {
    for (int version:{1,2}) {
        Project project;
        auto json=oldDefinition(version);
        // Earlier schemas never authored these properties. Unknown fields must
        // not introduce a fan or spread into an existing vector-velocity effect.
        json["emitters"][0]["directionSpread"]=50;
        json["emitters"][0]["directionFlatness"]=.8f;
        json["emitters"][0]["explosiveness"]=1;
        const auto source=json.dump(2)+"\n";
        std::filesystem::create_directories(project.root/"assets/particles");
        std::ofstream(project.root/"assets/particles/old.crimson-particle",std::ios::binary)<<source;
        const auto migrated=loadParticleAsset(project.root,"assets/particles/old.crimson-particle");
        auto expected=defaultParticleEffect("Old effect"); expected.id=migrated.id; expected.defaultSeed=0xabcdef1234567890ull;
        auto& e=expected.emitter;
        e.billboardMode=ParticleBillboardMode::Enabled;
        e.id=0xae12; e.name="Old impact"; e.enabled=false; e.rate=83.5f; e.lifetime=2.7f; e.initialVelocity={1,7,-2};
        e.gravity={1,-15,4}; e.size=.21f; e.color={particleSrgbToLinear(.2f),particleSrgbToLinear(.3f),particleSrgbToLinear(.4f)}; e.intensity=2.5f; e.capacity=151;
        if (version==2) { e.material=AssetRef<MaterialAsset>{"assets/materials/legacy.crimson-material"}; e.opacity=.35f; }
        require(migrated==current(expected),"Legacy migration changed original values or enabled new authoring behaviors.");
        require(migrated.emitter.shape==ParticleShape::Point && migrated.emitter.space==ParticleSpace::World &&
            !migrated.emitter.finiteDuration && !migrated.emitter.loop && migrated.emitter.bursts.empty() &&
            migrated.emitter.gravityBehavior==ParticleBehavior{true,true} && !migrated.emitter.accelerationBehavior.present &&
            !migrated.emitter.dragBehavior.present,"Legacy migration changed point/world/continuous/gravity behavior.");
        require(read(particleAssetPath(project.root,migrated.id))==source,"Reading a legacy effect rewrote its file.");
        saveParticleAsset(project.root,migrated);
        require(Json::parse(read(particleAssetPath(project.root,migrated.id))).at("version")==9 && loadParticleAsset(project.root,migrated.id)==current(expected),
            "Explicit save did not upgrade legacy data without changing behavior.");
    }
}
void versionThreeLaunchMigration() {
    for (auto mode:{ParticleDirectionMode::Fixed,ParticleDirectionMode::RandomCone,ParticleDirectionMode::Outward})
        for (auto velocity:{ParticleVelocityMode::Vector,ParticleVelocityMode::DirectionSpeed}) {
            Project project;
            auto expected=authoredEffect(); expected.emitter.directionMode=mode; expected.emitter.velocityMode=velocity;
            auto source=Json::parse(serializeParticleAsset(expected)); source["version"]=3; legacyEmissionFields(source);
            source["emitters"][0]["colorSpace"]="srgb";
            source["emitters"][0].erase("directionFlatness");
            const auto encoded=source.dump(2)+"\n";
            std::filesystem::create_directories(project.root/"assets/particles");
            std::ofstream(particleAssetPath(project.root,expected.id),std::ios::binary)<<encoded;
            expected.emitter.directionFlatness=0;
            expected.emitter.billboardMode=ParticleBillboardMode::Enabled;
            if (mode==ParticleDirectionMode::Outward) expected.emitter.directionSpread=0;
            const auto migrated=loadParticleAsset(project.root,expected.id);
            require(migrated==current(expected),"v3 migration changed active cone spread or retained formerly ignored outward spread.");
            require(read(particleAssetPath(project.root,expected.id))==encoded,"Loading v3 launch settings rewrote the source file.");
            source["emitters"][0]["directionFlatness"]=.9f;
            require(deserializeParticleAsset(source.dump(),expected.id)==current(expected),"Unknown v3 flatness field changed legacy launch behavior.");
            saveParticleAsset(project.root,migrated);
            require(Json::parse(read(particleAssetPath(project.root,expected.id))).at("version")==9 && loadParticleAsset(project.root,expected.id)==current(expected),
                "Explicit v3 upgrade did not preserve migrated launch settings in v4.");
        }
}
void boundsAndMalformedInput() {
    const auto valid=authoredEffect();
    const auto invalid=[&](const auto& mutation,const char* message) { auto effect=valid; mutation(effect.emitter); rejects([&]{validateParticleAsset(effect);},message); };
    invalid([](auto& e){e.shape=static_cast<ParticleShape>(99);},"Unknown shape enum accepted.");
    invalid([](auto& e){e.space=static_cast<ParticleSpace>(99);},"Unknown space enum accepted.");
    invalid([](auto& e){e.velocityMode=static_cast<ParticleVelocityMode>(99);},"Unknown velocity mode accepted.");
    invalid([](auto& e){e.directionMode=static_cast<ParticleDirectionMode>(99);},"Unknown direction mode accepted.");
    invalid([](auto& e){e.radius=-1;},"Negative radius accepted.");
    invalid([](auto& e){e.boxSize[1]=MaximumParticleShapeSize+1;},"Unbounded box extent accepted.");
    invalid([](auto& e){e.lifetimeRange={false,2,1};},"Inverted inactive lifetime range accepted.");
    invalid([](auto& e){e.lifetimeRange.minimum=0;},"Zero lifetime range accepted.");
    invalid([](auto& e){e.speedRange={true,-1,1};},"Negative speed range accepted.");
    invalid([](auto& e){e.speedRange={false,2,1};},"Inverted inactive speed range accepted.");
    invalid([](auto& e){e.speed=MaximumParticleSpeed+1;},"Unbounded initial speed accepted.");
    invalid([](auto& e){e.direction={0,0,0};},"Zero active launch direction accepted.");
    invalid([](auto& e){e.directionSpread=181;},"Unsupported direction spread accepted.");
    invalid([](auto& e){e.directionFlatness=-.1f;},"Negative direction flatness accepted.");
    invalid([](auto& e){e.directionFlatness=1.1f;},"Direction flatness above one accepted.");
    invalid([](auto& e){e.velocityMode=ParticleVelocityMode::Vector; e.directionFlatness=-1;},"Invalid inactive vector flatness accepted.");
    invalid([](auto& e){e.directionMode=ParticleDirectionMode::Fixed; e.directionFlatness=2;},"Invalid inactive fixed-direction flatness accepted.");
    invalid([](auto& e){e.duration=0;},"Zero duration accepted.");
    invalid([](auto& e){e.delay=-.1f;},"Negative delay accepted.");
    invalid([](auto& e){e.finiteDuration=false; e.loop=true;},"Continuous loop without a bounded cycle accepted.");
    invalid([](auto& e){e.bursts[0].time=e.duration;},"Burst at excluded duration boundary accepted.");
    invalid([](auto& e){e.bursts[0].time=-.1f;},"Negative burst time accepted.");
    invalid([](auto& e){e.bursts[0].id=0;},"Zero burst identity accepted.");
    invalid([](auto& e){e.bursts[1].id=e.bursts[0].id;},"Duplicate burst identity accepted.");
    invalid([](auto& e){e.bursts[0].count=0;},"Zero burst count accepted.");
    invalid([](auto& e){e.bursts[0].count=MaximumParticleCapacity+1;},"Unbounded burst count accepted.");
    invalid([](auto& e){e.bursts.resize(MaximumParticleBursts+1);},"Unbounded burst record count accepted.");
    invalid([](auto& e){e.acceleration[0]=MaximumParticleForce+1;},"Unbounded acceleration accepted.");
    invalid([](auto& e){e.drag=-1;},"Negative drag accepted.");
    invalid([](auto& e){e.drag=MaximumParticleDrag+1;},"Unbounded drag accepted.");
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    const auto infinity=std::numeric_limits<float>::infinity();
    for (auto value:{nan,infinity,-infinity}) {
        invalid([&](auto& e){e.radius=value;},"Nonfinite radius accepted.");
        invalid([&](auto& e){e.boxSize[0]=value;},"Nonfinite box dimensions accepted.");
        invalid([&](auto& e){e.lifetimeRange.maximum=value;},"Nonfinite lifetime range accepted.");
        invalid([&](auto& e){e.speedRange.minimum=value;},"Nonfinite speed range accepted.");
        invalid([&](auto& e){e.direction[2]=value;},"Nonfinite direction accepted.");
        invalid([&](auto& e){e.directionSpread=value;},"Nonfinite spread accepted.");
        invalid([&](auto& e){e.directionFlatness=value;},"Nonfinite flatness accepted.");
        invalid([&](auto& e){e.speed=value;},"Nonfinite speed accepted.");
        invalid([&](auto& e){e.delay=value;},"Nonfinite delay accepted.");
        invalid([&](auto& e){e.duration=value;},"Nonfinite duration accepted.");
        invalid([&](auto& e){e.bursts[0].time=value;},"Nonfinite burst time accepted.");
        invalid([&](auto& e){e.acceleration[1]=value;},"Nonfinite acceleration accepted.");
        invalid([&](auto& e){e.drag=value;},"Nonfinite drag accepted.");
    }
    auto vectorMode=valid; vectorMode.emitter.velocityMode=ParticleVelocityMode::Vector; vectorMode.emitter.direction={0,0,0};
    validateParticleAsset(vectorMode); // Unused direction does not invalidate old zero-velocity effects.
    auto bounds=valid; auto& e=bounds.emitter;
    e.radius=0; e.boxSize={0,0,MaximumParticleShapeSize}; e.speedRange={true,0,MaximumParticleSpeed};
    e.lifetimeRange={true,MinimumParticleDuration,MaximumParticleTime}; e.directionSpread=180; e.directionFlatness=1; e.delay=MaximumParticleTime;
    e.duration=MaximumParticleTime; e.drag=MaximumParticleDrag; e.acceleration={MaximumParticleForce,-MaximumParticleForce,0};
    e.bursts.clear(); for (uint64_t id=1;id<=MaximumParticleBursts;++id) e.bursts.push_back({id,0,MaximumParticleCapacity});
    require(deserializeParticleAsset(serializeParticleAsset(bounds))==current(bounds),"Valid documented bounds did not save and reopen.");
    const auto encoded=Json::parse(serializeParticleAsset(valid));
    const auto malformed=[&](const auto& mutation,const char* message) { auto source=encoded; mutation(source); rejects([&]{deserializeParticleAsset(source.dump());},message); };
    malformed([](auto& j){j["version"]=10;},"Unknown format version accepted.");
    malformed([](auto& j){j["version"]=4.0;},"Fractional version type accepted.");
    malformed([](auto& j){j["emitters"][0]["shape"]="capsule";},"Unimplemented shape name accepted.");
    malformed([](auto& j){j["emitters"][0]["space"]="camera";},"Unimplemented space name accepted.");
    malformed([](auto& j){j["emitters"][0]["velocityMode"]="curve";},"Unimplemented velocity mode accepted.");
    malformed([](auto& j){j["emitters"][0]["directionMode"]="spline";},"Unimplemented direction mode accepted.");
    malformed([](auto& j){j["emitters"][0].erase("directionFlatness");},"v4 definition without flatness accepted.");
    malformed([](auto& j){j["emitters"][0]["directionFlatness"]="flat";},"Nonnumeric direction flatness accepted.");
    malformed([](auto& j){j["emitters"][0]["legacyBursts"][0]["count"]=1.5;},"Fractional burst count silently truncated.");
    malformed([](auto& j){j["emitters"][0]["legacyBursts"][0]["count"]=-1;},"Negative burst count wrapped.");
    malformed([](auto& j){j["emitters"][0]["legacyBursts"][0]["count"]=std::numeric_limits<uint64_t>::max();},"Large burst count narrowed.");
    malformed([](auto& j){j["emitters"][0]["legacyBursts"][0]["id"]="10000000000000000";},"Overflowing burst identity accepted.");
    malformed([](auto& j){j["emitters"][0]["legacyBursts"]=Json::object();},"Object burst list accepted.");
    malformed([](auto& j){j["emitters"][0]["legacyBursts"]=Json::array(); for(size_t i=0;i<=MaximumParticleBursts;++i)j["emitters"][0]["legacyBursts"].push_back({{"id",std::to_string(i+1)},{"time",0},{"count",1}});},"Oversized burst list accepted.");
    malformed([](auto& j){j["emitters"][0]["behaviors"]["collision"]={{"present",true},{"enabled",true}};},"Unknown behavior was silently discarded.");
    malformed([](auto& j){j["emitters"][0]["lifetimeRange"]["enabled"]=1;},"Nonboolean range flag accepted.");
    malformed([](auto& j){j["emitters"][0]["material"]="assets/materials/../../escaped.crimson-material";},"Escaping material dependency accepted.");
    malformed([](auto& j){j["emitters"][0].erase("timing");},"Incomplete v4 timing definition accepted.");
    Project project; saveParticleAsset(project.root,valid); const auto before=read(particleAssetPath(project.root,valid.id));
    auto rejected=valid; rejected.emitter.bursts[0].id=0;
    rejects([&]{saveParticleAsset(project.root,rejected);},"Invalid v4 asset was saved.");
    require(read(particleAssetPath(project.root,valid.id))==before,"Failed v4 save replaced a working file.");
}
void historyAndDependencies() {
    Project project; const auto original=authoredEffect(); saveParticleAsset(project.root,original);
    ParticleEffectDocument document(project.root);
    require(document.load(original.id),"Cannot open v4 particle document.");
    const auto revision=document.revision(); const auto snapshot=document.committed();
    require(document.begin("Lifetime range drag"),"Cannot begin range edit.");
    require(document.update([](auto& a){a.emitter.lifetimeRange.maximum=1.4f;}) &&
        document.update([](auto& a){a.emitter.lifetimeRange.maximum=1.6f;}),"Valid intermediate range edits failed.");
    require(document.revision()==revision && document.committed()==snapshot,"Intermediate range values leaked into scene publication.");
    require(document.commit() && document.dirty() && document.undo() && document.definition()==current(original) && !document.canUndo(),
        "Range drag did not form one undo transaction.");
    require(document.redo() && document.definition().emitter.lifetimeRange.maximum==1.6f,"Range redo failed.");
    const auto before=document.definition();
    require(document.begin("Burst timing drag") && document.update([](auto& a){a.emitter.bursts[0].time=.9f;}),"Cannot edit burst timing.");
    document.cancel();
    require(document.definition()==before,"Cancel did not restore burst time and identities.");
    require(!document.edit("Invalid burst",[](auto& a){a.emitter.bursts[1].id=a.emitter.bursts[0].id;}),"Invalid duplicate burst committed.");
    require(document.definition()==before,"Rejected burst edit changed authored data.");
    require(document.edit("Remove drag",[](auto& a){a.emitter.dragBehavior.present=false;}),"Cannot remove drag behavior.");
    require(document.definition().emitter.drag==original.emitter.drag && document.undo() && document.definition()==before,
        "Behavior removal lost values or failed undo.");
    require(document.edit("Disable acceleration",[](auto& a){a.emitter.accelerationBehavior.enabled=false;}),"Cannot disable acceleration.");
    require(document.definition().emitter.acceleration==original.emitter.acceleration && document.undo(),"Behavior disabling lost values or failed undo.");
    require(document.begin("Flatness drag") && document.update([](auto& a){a.emitter.directionFlatness=.8f;}) &&
        document.update([](auto& a){a.emitter.directionFlatness=1;}),"Cannot edit flatness as one transaction.");
    require(document.commit() && document.undo() && document.definition().emitter.directionFlatness==original.emitter.directionFlatness && document.redo(),
        "Flatness drag did not preserve undo/redo values.");
    require(document.edit("Different material",[](auto& a){a.emitter.material=AssetRef<MaterialAsset>{"assets/materials/foam.crimson-material"};}),
        "Cannot assign managed material after variation edits.");
    const auto references=document.historicalMaterialReferences();
    require(std::find(references.begin(),references.end(),original.emitter.material.id)!=references.end() &&
        std::find(references.begin(),references.end(),canonicalAssetId("assets/materials/foam.crimson-material"))!=references.end(),
        "Step 3 history lost material dependencies.");
    const auto published=document.revision();
    require(document.save() && !document.dirty() && document.revision()==published,"Saving v4 republished or lost clean state.");
    ParticleEffectDocument reopened(project.root);
    require(reopened.load(original.id) && reopened.definition()==document.definition(),"v4 range/behavior/history result did not reopen.");
    const auto saved=document.definition(); const auto source=read(particleAssetPath(project.root,original.id));
    require(document.save("Reusable fountain copy",true) && document.assetId()!=original.id && document.definition().emitter==saved.emitter,
        "Document Save As lost timing, random ranges, behavior identities or material assignment.");
    require(read(particleAssetPath(project.root,original.id))==source,"Document Save As replaced source effect.");
}
void explosivenessPersistenceAndAdoption() {
    Project project;
    auto effect=defaultParticleEffect("Explosive emission"); effect.id="assets/particles/explosive.crimson-particle";
    require(effect.emitter.explosiveness==0,"New effects do not default to continuous emission.");
    for (float value:{0.f,.4f,1.f}) {
        effect.emitter.explosiveness=value;
        const auto source=Json::parse(serializeParticleAsset(effect)); const auto& emitter=source["emitters"][0];
        require(source.at("version")==9 && emitter.at("explosiveness")==value &&
            !emitter.contains("bursts") && !emitter.contains("legacyBursts") && deserializeParticleAsset(source.dump())==current(effect),
            "Native v9 emission lost Explosiveness or serialized a new burst schedule.");
    }
    const auto encoded=Json::parse(serializeParticleAsset(effect));
    const auto malformed=[&](const auto& mutation) {
        auto source=encoded;mutation(source["emitters"][0]);
        rejects([&]{deserializeParticleAsset(source.dump());},"Malformed Explosiveness was accepted.");
    };
    malformed([](auto& e){e.erase("explosiveness");});
    malformed([](auto& e){e["explosiveness"]=false;});
    malformed([](auto& e){e["explosiveness"]=".5";});
    malformed([](auto& e){e["explosiveness"]=nullptr;});
    malformed([](auto& e){e["explosiveness"]=-.1f;});
    malformed([](auto& e){e["explosiveness"]=1.1f;});
    for (float invalid:{-.1f,1.1f,std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto candidate=effect;candidate.emitter.explosiveness=invalid;
        rejects([&]{validateParticleAsset(candidate);},"Invalid Explosiveness passed asset validation.");
        auto emitter=effect.emitter;
        rejects([&]{setParticleExplosiveness(emitter,invalid);},"Invalid Explosiveness was silently clamped by adoption.");
        require(emitter==effect.emitter,"Rejected Explosiveness changed the emitter.");
    }
    // Existing extended schemas retain their original schedules and source bytes.
    for (int version:{3,4,5,6,7}) {
        auto legacy=authoredEffect();legacy.emitter.color={1,0,1};legacy.emitter.directionFlatness=0;
        legacy.emitter.billboardMode=ParticleBillboardMode::Enabled;
        legacy.preprocessSeconds=version==7?2.f:0.f;
        auto source=Json::parse(serializeParticleAsset(legacy));source["version"]=version;legacyEmissionFields(source);
        if (version<6) source["emitters"][0]["colorSpace"]="srgb";
        if (version<7) source.erase("preprocessSeconds");
        source["emitters"][0]["explosiveness"]=1; // An unknown newer field cannot change old schedules.
        const auto bytes=source.dump(2)+"\n";
        std::filesystem::create_directories(project.root/"assets/particles");
        std::ofstream(particleAssetPath(project.root,legacy.id),std::ios::binary)<<bytes;
        const auto loaded=loadParticleAsset(project.root,legacy.id);
        require(loaded==current(legacy) && loaded.emitter.explosiveness==0 && read(particleAssetPath(project.root,legacy.id))==bytes,
            "Loading v3-v7 changed a legacy schedule or rewrote its source.");
        ParticleEffectDocument document(project.root);
        require(document.load(legacy.id) && !document.dirty() && !document.canUndo(),"Legacy emission migration dirtied a document.");
        const auto native=Json::parse(serializeParticleAsset(loaded));
        require(!native["emitters"][0].contains("bursts") && native["emitters"][0].at("legacyBursts")==source["emitters"][0]["bursts"] &&
            deserializeParticleAsset(native.dump())==current(legacy),"v8 compatibility save lost legacy burst counts, times or stable identities.");
    }
    auto legacy=authoredEffect();const auto original=legacy.emitter;
    require(setParticleExplosiveness(legacy.emitter,.5f) && legacy.emitter.bursts.empty() &&
        legacy.emitter.rate==53.5f && legacy.emitter.duration==2 && legacy.emitter.explosiveness==.5f,
        "Explosiveness adoption failed to fold rate plus all finite burst counts.");
    require(!setParticleExplosiveness(legacy.emitter,.5f) && legacy.emitter.rate==53.5f,
        "Repeated unchanged Explosiveness adoption changed rate twice.");
    auto restored=original;require(setParticleExplosiveness(restored,0) && restored.rate==53.5f && restored.bursts.empty(),
        "Explicit zero did not adopt the legacy schedule.");
    auto continuous=original;continuous.finiteDuration=false;continuous.loop=false;continuous.duration=1;
    continuous.bursts={{1,4,20},{2,0,4}};
    require(setParticleExplosiveness(continuous,1) && continuous.duration==4 && continuous.rate==36 && continuous.bursts.empty(),
        "Continuous adoption failed to extend through the last legacy burst and retain its count.");
    auto overflow=original;overflow.rate=MaximumParticleRate;const auto unchanged=overflow;
    rejects([&]{setParticleExplosiveness(overflow,.5f);},"Legacy adoption silently dropped counts above the maximum rate.");
    require(overflow==unchanged,"Failed adoption partially changed rate, duration or burst identities.");
    auto boundary=original;boundary.rate=MaximumParticleRate-1;boundary.duration=2;boundary.bursts={{1,0,2}};
    require(setParticleExplosiveness(boundary,1) && boundary.rate==MaximumParticleRate,
        "Exact maximum-rate adoption was rejected.");
    // v8 may retain imported mixed schedules; ordinary load/save never adopts them.
    auto mixed=authoredEffect();mixed.emitter.explosiveness=.75f;
    require(deserializeParticleAsset(serializeParticleAsset(mixed))==current(mixed),"A mixed imported v8 schedule was destructively rewritten.");
}
void explosivenessHistoryAndCancellation() {
    Project project;const auto original=authoredEffect();saveParticleAsset(project.root,original);
    const auto bytes=read(particleAssetPath(project.root,original.id));
    ParticleEffectDocument document(project.root);require(document.load(original.id),"Cannot load legacy schedule history fixture.");
    const auto revision=document.revision();const auto published=document.committed();
    require(document.begin("Explosiveness") && document.update([](auto& a){setParticleExplosiveness(a.emitter,.25f);}) &&
        document.update([](auto& a){setParticleExplosiveness(a.emitter,.75f);}),"Valid Explosiveness gesture failed.");
    require(document.definition().emitter.rate==53.5f && document.definition().emitter.bursts.empty() &&
        document.revision()==revision && document.committed()==published,"Explosiveness drag refolded counts or published before commit.");
    document.cancel();require(document.definition()==current(original) && !document.dirty(),"Cancel did not restore legacy burst identities and schedule.");
    require(document.begin("Explosiveness") && document.update([](auto& a){setParticleExplosiveness(a.emitter,.5f);}) &&
        document.update([](auto& a){setParticleExplosiveness(a.emitter,1);}) && document.commit(),"Cannot commit Explosiveness adoption.");
    const auto adopted=document.definition();
    require(document.undo() && document.definition()==current(original) && !document.canUndo() && document.redo() && document.definition()==adopted,
        "Explosiveness and legacy adoption did not form one reversible command.");
    require(!document.edit("Invalid Explosiveness",[](auto& a){setParticleExplosiveness(a.emitter,2);}) &&
        document.definition()==adopted && read(particleAssetPath(project.root,original.id))==bytes,"Rejected Explosiveness edit changed state or source bytes.");
    require(document.save() && loadParticleAsset(project.root,original.id)==adopted,"Adopted Explosiveness did not survive save/reopen.");
    const auto saved=read(particleAssetPath(project.root,original.id));
    require(document.save("Explosive copy",true) && document.assetId()!=original.id && document.definition().emitter==adopted.emitter &&
        read(particleAssetPath(project.root,original.id))==saved,"Explosiveness Save As changed its source or authored emitter.");
    auto overflow=original;overflow.emitter.rate=MaximumParticleRate;saveParticleAsset(project.root,overflow);
    require(document.load(overflow.id)&&document.definition()==current(overflow),"Overflow fixture did not load its canonical identity and authored schedule.");
    const auto overflowBefore=document.definition();const auto overflowPublished=document.committed();const auto overflowRevision=document.revision();
    const auto overflowBytes=read(particleAssetPath(project.root,overflow.id));
    require(!document.edit("Adopt overflowing schedule",[](auto& a){setParticleExplosiveness(a.emitter,.5f);}) &&
        document.definition()==overflowBefore && !document.dirty() && !document.canUndo() && !document.transactionActive() &&
        document.committed()==overflowPublished && document.revision()==overflowRevision && read(particleAssetPath(project.root,overflow.id))==overflowBytes,
        "Failed legacy adoption changed history, publication or source bytes.");
}
}

int main() {
    try {
        versionFourPersistence(); priorVersionMigration(); versionThreeLaunchMigration(); boundsAndMalformedInput(); historyAndDependencies();
        explosivenessPersistenceAndAdoption();explosivenessHistoryAndCancellation();
        std::cout<<"Particle Step 3 schema, migration, validation and document checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
