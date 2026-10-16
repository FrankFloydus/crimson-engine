#include "assets/ParticleAsset.h"
#include "resources/AssetId.h"
#include "assets/MaterialAsset.h"
#include "particles/ParticleAppearance.h"
#include "editor/ParticleDocument.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
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
        root=std::filesystem::temp_directory_path()/("crimson-particle-step4-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++sequence));
        std::filesystem::create_directories(root);
    }
    ~Project() {
        std::error_code error;
        const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        const auto target=std::filesystem::weakly_canonical(root,error);
        if (!error && target.parent_path()==parent && target.filename().string().starts_with("crimson-particle-step4-"))
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
    auto effect=defaultParticleEffect("Growing smoke");
    effect.id="assets/particles/smoke.crimson-particle";
    effect.defaultSeed=0xfedcba9876543210ull;
    auto& e=effect.emitter;
    e.id=0xffffffffffffffffull; e.size=.25f; e.intensity=2; e.color={.7f,.5f,.3f}; e.opacity=.8f; e.rotation=-30;
    e.shape=ParticleShape::Box; e.boxSize={1,2,3}; e.space=ParticleSpace::Local;
    e.material=AssetRef<MaterialAsset>{canonicalAssetId("assets/materials/smoke.crimson-material")};
    e.sizeTrack.mode=ParticleAppearanceMode::Curve; e.sizeTrack.range=ParticleAppearanceRange{.2f,.9f};
    e.sizeTrack.curve.keys={{1,0,.25f},{0xfffffffffffffffeull,.4f,.8f},{4,1,2}};
    e.intensityTrack.mode=ParticleAppearanceMode::RandomRange; e.intensityTrack.range=ParticleAppearanceRange{1,5};
    e.intensityTrack.curve.interpolation=ParticleInterpolation::Constant;
    e.intensityTrack.curve.keys={{1,0,2},{2,1,0}};
    e.rotationTrack.mode=ParticleAppearanceMode::Curve;
    e.rotationTrack.curve.interpolation=ParticleInterpolation::Constant;
    e.rotationTrack.curve.keys={{0xfffffffffffffffdull,0,-30},{2,.6f,90},{3,1,180}};
    e.colorMode=ParticleColorMode::Gradient;
    e.colorGradient.interpolation=ParticleInterpolation::Linear;
    e.colorGradient.stops={{1,0,{1,.6f,.2f,.8f}},{2,.5f,{.3f,.4f,.5f,.5f}},{0xffffffffffffffffull,1,{0,0,0,0}}};
    return effect;
}
void modesAndPersistence() {
    Project project; auto effect=authoredEffect();
    const auto encoded=serializeParticleAsset(effect);
    require(Json::parse(encoded).at("version")==9,"Appearance assets do not use native v9.");
    require(deserializeParticleAsset(encoded,effect.id)==current(effect),"Appearance modes, stable keys or RGBA stops did not round-trip.");
    saveParticleAsset(project.root,effect);
    require(loadParticleAsset(project.root,effect.id)==current(effect),"Saved appearance tracks did not reopen exactly.");
    const auto copy=saveParticleAssetAs(project.root,effect,"Independent smoke");
    require(copy.id!=effect.id && copy.emitter==effect.emitter && copy.defaultSeed==effect.defaultSeed,
        "Save As lost appearance identities, inactive data or material reference.");
    require(read(particleAssetPath(project.root,effect.id))==encoded,"Appearance Save As changed its source.");
    for (auto mode:{ParticleAppearanceMode::Constant,ParticleAppearanceMode::RandomRange,ParticleAppearanceMode::Curve})
        for (auto interpolation:{ParticleInterpolation::Linear,ParticleInterpolation::Constant}) {
            effect.emitter.sizeTrack.mode=mode;
            effect.emitter.sizeTrack.curve.interpolation=interpolation;
            effect.emitter.colorGradient.interpolation=interpolation;
            require(deserializeParticleAsset(serializeParticleAsset(effect))==current(effect),"A scalar/interpolation mode lost authored inactive settings.");
        }
    effect.emitter.colorMode=ParticleColorMode::Constant;
    require(deserializeParticleAsset(serializeParticleAsset(effect))==current(effect),"Constant color mode discarded its inactive RGBA gradient.");
    auto empty=defaultParticleEffect(); empty.id="assets/particles/empty.crimson-particle";
    require(deserializeParticleAsset(serializeParticleAsset(empty))==current(empty),"Empty inactive appearance settings did not reopen.");
    require(!empty.emitter.sizeTrack.range && empty.emitter.sizeTrack.curve.keys.empty() &&
        empty.emitter.colorGradient.stops.empty() && empty.emitter.rotation==0,"Starter requires authored appearance data before use.");
    // Scalar size zero is meaningful for a curve/range, while the old constant
    // size property retains its original positive lower bound.
    effect.emitter.sizeTrack.range=ParticleAppearanceRange{0,10000};
    effect.emitter.sizeTrack.curve.keys={{1,0,0},{2,1,10000}};
    effect.emitter.intensityTrack.range=ParticleAppearanceRange{0,100000};
    effect.emitter.intensityTrack.curve.keys={{1,0,0},{2,1,100000}};
    effect.emitter.rotationTrack.range=ParticleAppearanceRange{-MaximumParticleRotation,MaximumParticleRotation};
    effect.emitter.rotationTrack.curve.keys={{1,0,-MaximumParticleRotation},{2,1,MaximumParticleRotation}};
    effect.emitter.rotation=MaximumParticleRotation;
    require(deserializeParticleAsset(serializeParticleAsset(effect))==current(effect),"Documented appearance domain endpoints failed persistence.");
    auto legacySize=effect; legacySize.emitter.size=0;
    rejects([&]{validateParticleAsset(legacySize);},"Zero legacy constant size became valid unintentionally.");
}
void priorVersions() {
    for (int version:{1,2,3,4}) {
        Project project;
        auto expected=defaultParticleEffect("Legacy effect"); expected.id="assets/particles/legacy.crimson-particle";
        expected.defaultSeed=0xffffffffffffffffull;
        auto& e=expected.emitter;
        e.billboardMode=ParticleBillboardMode::Enabled;
        e.id=0xabcdef; e.rate=75; e.lifetime=2; e.initialVelocity={2,5,-1}; e.gravity={1,-8,2};
        e.size=.7f; e.intensity=3; e.color={.4f,.5f,.6f}; e.opacity=.3f;
        e.material=AssetRef<MaterialAsset>{"assets/materials/legacy.crimson-material"};
        if (version>=3) {
            e.shape=ParticleShape::Cone; e.space=ParticleSpace::Local; e.velocityMode=ParticleVelocityMode::DirectionSpeed;
            e.directionMode=ParticleDirectionMode::RandomCone; e.directionSpread=35; e.directionFlatness=.7f;
            e.delay=.2f; e.finiteDuration=true; e.duration=2; e.loop=true; e.bursts={{12,.5f,30}};
            e.dragBehavior={true,true}; e.drag=.6f;
        }
        auto source=Json::parse(serializeParticleAsset(expected)); source["version"]=version; legacyEmissionFields(source);
        auto& emitter=source["emitters"][0];
        emitter["colorSpace"]="srgb";
        for (auto& channel:e.color) channel=particleSrgbToLinear(channel);
        emitter.erase("rotation"); emitter.erase("appearanceTracks");
        if (version<4) { emitter.erase("directionFlatness"); e.directionFlatness=0; }
        if (version==1) {
            emitter["renderer"]="procedural-additive"; emitter.erase("material"); emitter.erase("opacity");
            e.material=AssetRef<MaterialAsset>{DefaultParticleMaterialId}; e.opacity=1;
        }
        // Inactive future-looking payloads in old versions cannot opt existing
        // effects into a curve/gradient or replace their constant appearance.
        const auto future=Json::parse(serializeParticleAsset(authoredEffect()));
        emitter["appearanceTracks"]=future["emitters"][0]["appearanceTracks"]; emitter["rotation"]=120;
        const auto encoded=source.dump(2)+"\n";
        std::filesystem::create_directories(project.root/"assets/particles");
        std::ofstream(particleAssetPath(project.root,expected.id),std::ios::binary)<<encoded;
        const auto migrated=loadParticleAsset(project.root,expected.id);
        require(migrated==current(expected),"v1-v4 migration changed decoded appearance, material or preexisting behavior.");
        require(read(particleAssetPath(project.root,expected.id))==encoded,"Loading an earlier effect rewrote its source.");
        saveParticleAsset(project.root,migrated);
        require(Json::parse(read(particleAssetPath(project.root,expected.id))).at("version")==9 && loadParticleAsset(project.root,expected.id)==current(expected),
            "Explicit v9 upgrade did not preserve earlier appearance.");
    }
}
void hdrMigrationAndPersistence() {
    // v5 includes independent appearance modes. All of them, including inactive
    // gradient stops and random intensity lanes, must retain their old result.
    for (auto colorMode:{ParticleColorMode::Constant,ParticleColorMode::Gradient})
        for (auto intensityMode:{ParticleAppearanceMode::Constant,ParticleAppearanceMode::RandomRange,ParticleAppearanceMode::Curve}) {
            Project project;auto legacy=authoredEffect();auto& e=legacy.emitter;
            e.color={.5f,.04045f,1};e.colorMode=colorMode;e.intensity=3;e.intensityTrack.mode=intensityMode;
            e.intensityTrack.range=ParticleAppearanceRange{2,5};
            e.intensityTrack.curve={ParticleInterpolation::Linear,{{1,0,2},{2,1,4}}};
            e.colorGradient={ParticleInterpolation::Linear,{{11,.25f,{.5f,0,1,.2f}},{22,.75f,{1,.5f,0,.8f}}}};
            auto json=Json::parse(serializeParticleAsset(legacy));json["version"]=5;legacyEmissionFields(json);json["emitters"][0]["colorSpace"]="srgb";
            const auto bytes=json.dump(2)+"\n";
            std::filesystem::create_directories(project.root/"assets/particles");
            std::ofstream(particleAssetPath(project.root,legacy.id),std::ios::binary)<<bytes;
            auto expected=legacy;auto& converted=expected.emitter;
            converted.billboardMode=ParticleBillboardMode::Enabled;
            converted.color={particleSrgbToLinear(.5f),particleSrgbToLinear(.04045f),1};
            for(auto& stop:converted.colorGradient.stops)
                for(std::size_t channel=0;channel<3;++channel)stop.color[channel]=particleSrgbToLinear(stop.color[channel]);
            const auto migrated=loadParticleAsset(project.root,legacy.id);
            require(migrated==current(expected),"v5 migration decoded color twice, discarded inactive HDR data or changed intensity modes.");
            require(read(particleAssetPath(project.root,legacy.id))==bytes,"Loading v5 rewrote the authored source.");
            const auto prepared=prepareParticleAppearance(migrated.emitter);const auto seed=particleAppearanceSeed(legacy.defaultSeed,e.id);
            for(double age:{0.,.25,.5,.75,1.}) {
                const auto sample=prepared.sample(age,seed,17);
                const double factor=std::clamp((age-.25)/.5,0.,1.);
                Vec4 color=colorMode==ParticleColorMode::Constant?Vec4{converted.color[0],converted.color[1],1,e.opacity}:
                    Vec4{static_cast<float>(particleSrgbToLinear(.5f)+(1-particleSrgbToLinear(.5f))*factor),
                        static_cast<float>(particleSrgbToLinear(.5f)*factor),static_cast<float>(1-factor),static_cast<float>(.2+.6*factor)};
                const double strength=intensityMode==ParticleAppearanceMode::Constant?3:
                    intensityMode==ParticleAppearanceMode::Curve?2+2*age:particleAppearanceRange(*e.intensityTrack.range,legacy.defaultSeed,e.id,17,8);
                for(std::size_t channel=0;channel<3;++channel)
                    require(std::abs(sample.color[channel]-color[channel]*strength)<1e-5,"Migrated v5 rendered a different color/intensity function.");
                require(std::abs(sample.color[3]-color[3])<1e-6,"Migration applied a color transfer or intensity to alpha.");
            }
            saveParticleAsset(project.root,migrated);
            const auto saved=Json::parse(read(particleAssetPath(project.root,legacy.id)));
            require(saved.at("version")==9 && saved["emitters"][0]["colorSpace"]=="linear-hdr" &&
                loadParticleAsset(project.root,legacy.id)==current(expected),"Explicit v9 save reinterpreted already migrated RGB.");
            auto malformed=json;malformed["emitters"][0]["color"][0]=1.1f;
            rejects([&]{deserializeParticleAsset(malformed.dump());},"Legacy RGB above one bypassed its original validation.");
            malformed=json;malformed["emitters"][0]["appearanceTracks"]["color"]["gradient"]["stops"][0]["color"][2]=2;
            rejects([&]{deserializeParticleAsset(malformed.dump());},"Invalid inactive legacy gradient became a valid HDR gradient.");
        }
    Project project;auto hdr=authoredEffect();hdr.emitter.color={.5f,12,MaximumParticleColor};
    hdr.emitter.colorGradient.stops[0].color={0,32,2,.25f};hdr.emitter.colorGradient.stops[1].color={MaximumParticleColor,0,0,.75f};
    saveParticleAsset(project.root,hdr);
    require(loadParticleAsset(project.root,hdr.id)==current(hdr),"HDR RGB or opacity was lost in native v9 round-trip.");
    ParticleEffectDocument document(project.root);require(document.load(hdr.id),"HDR document did not reopen.");
    require(document.begin("HDR stop intensity") && document.update([](auto& asset){asset.emitter.colorGradient.stops[0].color[1]=64;}) &&
        document.update([](auto& asset){asset.emitter.colorGradient.stops[0].color[1]=128;}) && document.commit() &&
        document.undo() && document.definition()==current(hdr) && document.redo(),"HDR stop drag did not form one reversible transaction.");
    const auto committed=document.definition();require(document.begin("Cancel HDR stop") && document.update([](auto& asset){asset.emitter.color={64,32,16};}),
        "HDR constant edit was rejected.");document.cancel();require(document.definition()==committed,"Cancel lost the prior HDR revision.");
    require(document.save() && loadParticleAsset(project.root,hdr.id)==committed,"HDR history result failed reopening.");
    auto malformed=Json::parse(serializeParticleAsset(hdr));malformed["emitters"][0]["colorSpace"]="srgb";
    rejects([&]{deserializeParticleAsset(malformed.dump());},"v6 accepted an ambiguous sRGB color convention.");
    const auto source=read(particleAssetPath(project.root,hdr.id));auto invalid=hdr;invalid.emitter.color[0]=MaximumParticleColor+1;
    rejects([&]{saveParticleAsset(project.root,invalid);},"HDR value above the supported bound was saved.");
    require(read(particleAssetPath(project.root,hdr.id))==source,"Failed HDR save replaced the working source.");
}
void preprocessPersistenceMigrationAndHistory() {
    require(defaultParticleEffect().preprocessSeconds==0,"A new effect unexpectedly preprocesses before playback.");
    Project project;auto effect=authoredEffect();effect.preprocessSeconds=2.5f;
    const auto encoded=serializeParticleAsset(effect);const auto json=Json::parse(encoded);
    require(json.at("version")==9 && json.at("preprocessSeconds")==2.5f &&
        json["emitters"][0]["colorSpace"]=="linear-hdr" && deserializeParticleAsset(encoded)==current(effect),
        "Native preprocessing did not retain effect-level seconds, HDR colors and authored values.");
    saveParticleAsset(project.root,effect);
    const auto copied=saveParticleAssetAs(project.root,effect,"Preprocessed fire copy");
    require(copied.preprocessSeconds==effect.preprocessSeconds && copied.emitter==effect.emitter &&
        loadParticleAsset(project.root,copied.id)==copied && read(particleAssetPath(project.root,effect.id))==encoded,
        "Preprocess Save As lost seconds or changed its source.");
    for(float seconds:{0.f,MaximumParticlePreprocessSeconds}) {
        auto boundary=effect;boundary.preprocessSeconds=seconds;
        require(deserializeParticleAsset(serializeParticleAsset(boundary))==current(boundary),"A valid preprocessing endpoint failed native round-trip.");
    }
    for(int version:{1,2,3,4,5,6}) {
        auto original=defaultParticleEffect("Old effect");original.id="assets/particles/old-preprocess.crimson-particle";
        original.preprocessSeconds=4;original.emitter.color={.5f,.25f,1};original.emitter.intensity=3;
        auto source=Json::parse(serializeParticleAsset(original));source["version"]=version;legacyEmissionFields(source);source.erase("preprocessSeconds");
        if(version<6)source["emitters"][0]["colorSpace"]="srgb";
        if(version==1) {source["emitters"][0]["renderer"]="procedural-additive";source["emitters"][0].erase("material");source["emitters"][0].erase("opacity");}
        auto expected=original;expected.preprocessSeconds=0;
        expected.emitter.billboardMode=ParticleBillboardMode::Enabled;
        if(version<6)for(auto& channel:expected.emitter.color)channel=particleSrgbToLinear(channel);
        const auto bytes=source.dump(2)+"\n";
        std::ofstream(particleAssetPath(project.root,original.id),std::ios::binary)<<bytes;
        require(loadParticleAsset(project.root,original.id)==current(expected) && read(particleAssetPath(project.root,original.id))==bytes,
            "Loading v1-v6 enabled preprocessing, changed its color convention or rewrote its source.");
        source["preprocessSeconds"]="unknown future field";
        require(deserializeParticleAsset(source.dump())==current(expected),"An unknown old-version field unexpectedly enabled preprocessing.");
        saveParticleAsset(project.root,expected);
        require(Json::parse(read(particleAssetPath(project.root,original.id))).at("version")==9 && loadParticleAsset(project.root,original.id)==current(expected),
            "Explicit v1-v6 upgrade changed disabled preprocessing or rendered appearance.");
    }
    for(float seconds:{-.001f,MaximumParticlePreprocessSeconds+1,std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto invalid=effect;invalid.preprocessSeconds=seconds;
        rejects([&]{validateParticleAsset(invalid);},"Invalid preprocess seconds entered a valid definition.");
        rejects([&]{saveParticleAsset(project.root,invalid);},"Invalid preprocess seconds replaced a saved effect.");
        require(read(particleAssetPath(project.root,effect.id))==encoded,"Failed preprocess validation changed the prior file.");
    }
    const auto invalidJson=[&](const auto& mutate) {auto invalid=json;mutate(invalid);rejects([&]{deserializeParticleAsset(invalid.dump());},"Malformed preprocessing payload was accepted.");};
    invalidJson([](auto& value){value.erase("preprocessSeconds");});
    invalidJson([](auto& value){value["preprocessSeconds"]="2.5";});
    invalidJson([](auto& value){value["preprocessSeconds"]=false;});
    invalidJson([](auto& value){value["preprocessSeconds"]=nullptr;});
    invalidJson([](auto& value){value["preprocessSeconds"]=-1;});
    invalidJson([](auto& value){value["preprocessSeconds"]=61;});
    ParticleEffectDocument document(project.root);require(document.load(effect.id),"Cannot load a preprocessed effect document.");
    const auto revision=document.revision();const auto published=document.committed();
    require(document.begin("Preprocess drag") && document.update([](auto& asset){asset.preprocessSeconds=4;}) &&
        document.update([](auto& asset){asset.preprocessSeconds=6;}),"A valid preprocessing edit was rejected.");
    require(document.revision()==revision && document.committed()==published,"A preprocess draft published before commit.");
    require(document.commit() && document.undo() && document.definition()==current(effect) && !document.canUndo() && document.redo(),
        "Preprocess drag failed its single undo/redo transaction.");
    const auto committed=document.definition();
    require(document.begin("Cancel preprocess") && document.update([](auto& asset){asset.preprocessSeconds=9;}),"Cannot begin preprocess cancel fixture.");
    document.cancel();require(document.definition()==committed,"Cancel failed to restore saved preprocessing intent.");
    require(!document.edit("Invalid preprocess",[](auto& asset){asset.preprocessSeconds=MaximumParticlePreprocessSeconds+1;}) &&
        document.definition()==committed,"Rejected preprocess edit changed the working revision.");
    require(document.save() && loadParticleAsset(project.root,effect.id)==committed,"Committed preprocessing did not survive save/reopen.");
}
void orientationPersistenceMigrationAndHistory() {
    require(ParticleEmitterDefinition{}.billboardMode==ParticleBillboardMode::Disabled &&
        defaultParticleEffect().emitter.billboardMode==ParticleBillboardMode::Disabled,"New emitters or effects enable billboarding implicitly.");
    Project project;auto effect=authoredEffect();
    effect.emitter.orientation={27,-43,91};
    for(auto mode:{ParticleBillboardMode::Disabled,ParticleBillboardMode::Enabled,ParticleBillboardMode::FixedY}) {
        effect.emitter.billboardMode=mode;
        require(deserializeParticleAsset(serializeParticleAsset(effect))==current(effect),"A billboard mode or retained orientation failed v9 round-trip.");
    }
    // Earlier versions ignored all future orientation fields. Reading one
    // must preserve its original camera-facing rendering and its exact bytes.
    for(int version=1;version<=8;++version) {
        auto expected=defaultParticleEffect("Legacy facing");expected.id="assets/particles/facing.crimson-particle";
        auto source=Json::parse(serializeParticleAsset(expected));source["version"]=version;
        auto& emitter=source["emitters"][0];
        if(version<8)legacyEmissionFields(source);
        if(version<7)source.erase("preprocessSeconds");
        if(version<6)emitter["colorSpace"]="srgb";
        if(version==1){emitter["renderer"]="procedural-additive";emitter.erase("material");emitter.erase("opacity");}
        emitter["billboardMode"]="unknown future value";emitter["orientation"]="not a legacy property";
        const auto bytes=source.dump(2)+"\n";
        std::filesystem::create_directories(project.root/"assets/particles");
        std::ofstream(particleAssetPath(project.root,expected.id),std::ios::binary)<<bytes;
        expected.emitter.billboardMode=ParticleBillboardMode::Enabled;
        require(loadParticleAsset(project.root,expected.id)==current(expected) && read(particleAssetPath(project.root,expected.id))==bytes,
            "v1-v8 facing migration changed authored properties or rewrote its source.");
        ParticleEffectDocument legacy(project.root);
        require(legacy.load(expected.id) && !legacy.dirty() && !legacy.canUndo(),"Legacy billboard migration dirtied the effect document.");
        saveParticleAsset(project.root,expected);
        require(Json::parse(read(particleAssetPath(project.root,expected.id))).at("version")==9 && loadParticleAsset(project.root,expected.id)==current(expected),
            "Explicit v9 upgrade lost migrated camera-facing behavior.");
    }
    saveParticleAsset(project.root,effect);const auto before=read(particleAssetPath(project.root,effect.id));
    const auto invalid=[&](const auto& mutate) {
        auto value=effect;mutate(value.emitter);
        rejects([&]{validateParticleAsset(value);},"Invalid orientation data entered a valid effect.");
        rejects([&]{saveParticleAsset(project.root,value);},"Invalid orientation replaced the saved effect.");
        require(read(particleAssetPath(project.root,effect.id))==before,"Failed orientation save changed its source.");
    };
    invalid([](auto& e){e.billboardMode=static_cast<ParticleBillboardMode>(99);});
    invalid([](auto& e){e.orientation[1]=MaximumParticleRotation+1;});
    for(float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()})
        invalid([&](auto& e){e.orientation[0]=value;});
    const auto json=Json::parse(serializeParticleAsset(effect));
    const auto malformed=[&](const auto& mutate){auto source=json;mutate(source["emitters"][0]);rejects([&]{deserializeParticleAsset(source.dump());},"Malformed v9 orientation was accepted.");};
    malformed([](auto& e){e["billboardMode"]="camera-ish";});
    malformed([](auto& e){e["billboardMode"]=1;});
    malformed([](auto& e){e.erase("billboardMode");});
    malformed([](auto& e){e.erase("orientation");});
    malformed([](auto& e){e["orientation"]={0,0};});
    malformed([](auto& e){e["orientation"]={0,nullptr,0};});
    ParticleEffectDocument document(project.root);require(document.load(effect.id),"Cannot load orientation history fixture.");
    require(document.edit("Facing",[](auto& a){a.emitter.billboardMode=ParticleBillboardMode::Disabled;}) && document.undo() &&
        document.definition()==current(effect) && document.redo(),"Facing changes did not retain inactive XYZ through undo/redo.");
    const auto beforeDrag=document.definition();
    require(document.begin("Orientation drag") && document.update([](auto& a){a.emitter.orientation[0]=10;}) &&
        document.update([](auto& a){a.emitter.orientation[0]=20;}) && document.commit() && document.undo() &&
        document.definition()==beforeDrag && document.redo(),"Orientation drag failed its single transaction.");
    const auto committed=document.definition();
    require(document.begin("Cancel orientation") && document.update([](auto& a){a.emitter.orientation={};}),"Cannot begin orientation cancellation.");
    document.cancel();require(document.definition()==committed,"Cancellation lost the prior XYZ orientation.");
    require(document.save() && loadParticleAsset(project.root,effect.id)==committed,"Orientation history result did not survive reopening.");
}
void validation() {
    const auto valid=authoredEffect();
    const auto invalid=[&](const auto& mutation,const char* message) { auto candidate=valid; mutation(candidate.emitter); rejects([&]{validateParticleAsset(candidate);},message); };
    invalid([](auto& e){e.sizeTrack.mode=static_cast<ParticleAppearanceMode>(99);},"Unknown scalar mode accepted.");
    invalid([](auto& e){e.colorMode=static_cast<ParticleColorMode>(99);},"Unknown color mode accepted.");
    invalid([](auto& e){e.sizeTrack.curve.interpolation=static_cast<ParticleInterpolation>(99);},"Unknown curve interpolation accepted.");
    invalid([](auto& e){e.colorGradient.interpolation=static_cast<ParticleInterpolation>(99);},"Unknown gradient interpolation accepted.");
    invalid([](auto& e){e.sizeTrack.mode=ParticleAppearanceMode::RandomRange; e.sizeTrack.range.reset();},"Active Random Range without values accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys.clear();},"Active empty curve accepted.");
    invalid([](auto& e){e.colorGradient.stops.clear();},"Active empty gradient accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys.resize(1);},"Single-key curve accepted.");
    invalid([](auto& e){e.colorGradient.stops.resize(1);},"Single-stop gradient accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys[0].age=-.001f;},"Curve first key below normalized age zero accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys.back().age=1.001f;},"Curve final key above normalized age one accepted.");
    invalid([](auto& e){e.colorGradient.stops[0].age=-.001f;},"Gradient first stop below normalized age zero accepted.");
    invalid([](auto& e){e.colorGradient.stops.back().age=1.001f;},"Gradient final stop above normalized age one accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys[1].age=0;},"Coincident curve keys accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys[1].age=.00001f;},"Insufficient curve key spacing accepted.");
    invalid([](auto& e){e.colorGradient.stops[1].age=.00001f;},"Insufficient gradient stop spacing accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys[1].age=2;},"Curve key outside normalized lifetime accepted.");
    invalid([](auto& e){e.colorGradient.stops[1].age=-.1f;},"Negative gradient lifetime age accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys[1].id=0;},"Zero curve key identity accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys[1].id=e.sizeTrack.curve.keys[0].id;},"Duplicate curve key identity accepted.");
    invalid([](auto& e){e.colorGradient.stops[1].id=0;},"Zero gradient stop identity accepted.");
    invalid([](auto& e){e.colorGradient.stops[1].id=e.colorGradient.stops[0].id;},"Duplicate gradient stop identity accepted.");
    invalid([](auto& e){e.sizeTrack.range=ParticleAppearanceRange{2,1};},"Inverted inactive range accepted.");
    invalid([](auto& e){e.sizeTrack.range=ParticleAppearanceRange{-1,1};},"Negative size range accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys[0].value=-1;},"Negative size key accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys.back().value=10001;},"Unbounded size key accepted.");
    invalid([](auto& e){e.intensityTrack.curve.keys.back().value=100001;},"Unbounded inactive intensity key accepted.");
    invalid([](auto& e){e.rotationTrack.range=ParticleAppearanceRange{-MaximumParticleRotation-1,0};},"Unbounded inactive rotation range accepted.");
    invalid([](auto& e){e.rotation=MaximumParticleRotation+1;},"Unbounded constant rotation accepted.");
    invalid([](auto& e){e.colorGradient.stops[0].color[3]=-1;},"Negative gradient opacity accepted.");
    invalid([](auto& e){e.colorGradient.stops[0].color[0]=MaximumParticleColor+1;},"Gradient RGB above the HDR limit accepted.");
    invalid([](auto& e){e.sizeTrack.mode=ParticleAppearanceMode::Constant; e.sizeTrack.curve.keys.resize(1);},"Invalid inactive curve accepted.");
    invalid([](auto& e){e.colorMode=ParticleColorMode::Constant; e.colorGradient.stops[0].color[3]=2;},"Invalid inactive gradient accepted.");
    invalid([](auto& e){e.sizeTrack.curve.keys.resize(MaximumParticleAppearanceKeys+1);},"Unbounded curve key array accepted.");
    invalid([](auto& e){e.colorGradient.stops.resize(MaximumParticleAppearanceKeys+1);},"Unbounded gradient stop array accepted.");
    for (float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
        invalid([&](auto& e){e.rotation=value;},"Nonfinite constant rotation accepted.");
        invalid([&](auto& e){e.sizeTrack.range->minimum=value;},"Nonfinite inactive range accepted.");
        invalid([&](auto& e){e.sizeTrack.curve.keys[1].age=value;},"Nonfinite curve age accepted.");
        invalid([&](auto& e){e.sizeTrack.curve.keys[1].value=value;},"Nonfinite curve value accepted.");
        invalid([&](auto& e){e.colorGradient.stops[1].age=value;},"Nonfinite gradient age accepted.");
        invalid([&](auto& e){e.colorGradient.stops[1].color[3]=value;},"Nonfinite gradient opacity accepted.");
    }
    auto bounded=valid;
    bounded.emitter.sizeTrack.curve.keys.clear(); bounded.emitter.colorGradient.stops.clear();
    for (size_t i=0;i<MaximumParticleAppearanceKeys;++i) {
        const float age=static_cast<float>(i)/static_cast<float>(MaximumParticleAppearanceKeys-1);
        bounded.emitter.sizeTrack.curve.keys.push_back({i+1,age,static_cast<float>(i)});
        bounded.emitter.colorGradient.stops.push_back({i+1,age,{age,1-age,age,1-age}});
    }
    require(deserializeParticleAsset(serializeParticleAsset(bounded))==current(bounded),"Maximum valid key/stop counts failed persistence.");
    auto precise=valid; precise.emitter.sizeTrack.curve.keys={{1,0,1},{2,.5f,1},{3,.5f+MinimumParticleKeySpacing,1},{4,1,1}};
    validateParticleAsset(precise); // Neighbor+spacing must survive float rounding.
    const auto encoded=Json::parse(serializeParticleAsset(valid));
    const auto malformed=[&](const auto& mutation,const char* message) { auto source=encoded; mutation(source); rejects([&]{deserializeParticleAsset(source.dump());},message); };
    malformed([](auto& j){j["version"]=10;},"Unknown next version accepted.");
    malformed([](auto& j){j["emitters"][0]["appearanceTracks"]["size"]["mode"]="noise";},"Unknown serialized scalar mode accepted.");
    malformed([](auto& j){j["emitters"][0]["appearanceTracks"]["size"]["curve"]["interpolation"]="cubic";},"Unimplemented curve interpolation accepted.");
    malformed([](auto& j){j["emitters"][0]["appearanceTracks"]["color"]["mode"]="curve";},"Unknown serialized color mode accepted.");
    malformed([](auto& j){j["emitters"][0]["appearanceTracks"]["size"]["range"]=Json::array();},"Malformed range structure accepted.");
    malformed([](auto& j){j["emitters"][0]["appearanceTracks"]["size"]["curve"]["keys"]=Json::object();},"Object key list accepted.");
    malformed([](auto& j){j["emitters"][0]["appearanceTracks"]["color"]["gradient"]["stops"][0]["color"]={1,1,1};},"RGB gradient missing opacity accepted.");
    malformed([](auto& j){j["emitters"][0]["appearanceTracks"]["size"]["curve"]["keys"][0]["id"]="10000000000000000";},"Overflowing key identity accepted.");
    malformed([](auto& j){j["emitters"][0]["appearanceTracks"]["unknown"]=Json::object();},"Unknown appearance track was silently discarded.");
    malformed([](auto& j){j["emitters"][0].erase("appearanceTracks");},"Incomplete v5 appearance payload accepted.");
    malformed([](auto& j){auto& keys=j["emitters"][0]["appearanceTracks"]["size"]["curve"]["keys"]; while(keys.size()<=MaximumParticleAppearanceKeys)keys.push_back(keys[0]);},"Oversized serialized key array accepted.");
    malformed([](auto& j){auto& stops=j["emitters"][0]["appearanceTracks"]["color"]["gradient"]["stops"]; while(stops.size()<=MaximumParticleAppearanceKeys)stops.push_back(stops[0]);},"Oversized serialized gradient array accepted.");
    Project project; saveParticleAsset(project.root,valid); const auto before=read(particleAssetPath(project.root,valid.id));
    auto rejected=valid; rejected.emitter.sizeTrack.curve.keys[0].id=0;
    rejects([&]{saveParticleAsset(project.root,rejected);},"Invalid appearance definition was written.");
    require(read(particleAssetPath(project.root,valid.id))==before,"Failed appearance validation replaced its working file.");
}
void documentHistory() {
    Project project; const auto original=authoredEffect(); saveParticleAsset(project.root,original);
    ParticleEffectDocument document(project.root); require(document.load(original.id),"Cannot open appearance document.");
    const auto revision=document.revision(); const auto committed=document.committed();
    require(document.begin("Size key drag") && document.update([](auto& a){a.emitter.sizeTrack.curve.keys[1].age=.5f;}) &&
        document.update([](auto& a){a.emitter.sizeTrack.curve.keys[1].age=.6f;}),"Valid curve drag failed.");
    require(document.revision()==revision && document.committed()==committed,"Curve intermediate published to scene instances.");
    require(document.commit() && document.undo() && document.definition()==current(original) && !document.canUndo() && document.redo(),
        "Curve drag did not form one undo command.");
    const auto before=document.definition();
    require(document.begin("Gradient drag") && document.update([](auto& a){a.emitter.colorGradient.stops[1].color={.2f,.3f,.4f,.7f};}),
        "Valid RGBA stop drag failed.");
    document.cancel(); require(document.definition()==before,"Gradient cancel did not restore stop RGBA/identity.");
    require(!document.edit("Invalid key",[](auto& a){a.emitter.sizeTrack.curve.keys[1].id=a.emitter.sizeTrack.curve.keys[0].id;}) &&
        document.definition()==before,"Rejected key edit changed working state.");
    require(document.edit("Constant size",[](auto& a){a.emitter.sizeTrack.mode=ParticleAppearanceMode::Constant;}) &&
        document.definition().emitter.sizeTrack.curve==before.emitter.sizeTrack.curve,"Mode change discarded curve keys.");
    require(document.undo() && document.definition()==before,"Mode undo did not restore curve mode/data.");
    require(document.edit("Constant color",[](auto& a){a.emitter.colorMode=ParticleColorMode::Constant;}) &&
        document.definition().emitter.colorGradient==before.emitter.colorGradient,"Color mode change discarded gradient.");
    const auto published=document.revision(); require(document.save() && !document.dirty() && document.revision()==published,"Appearance save republished or left dirty state.");
    ParticleEffectDocument reopened(project.root);
    require(reopened.load(original.id) && reopened.definition()==document.definition(),"Appearance history result did not reopen.");
    const auto saved=document.definition(); const auto source=read(particleAssetPath(project.root,original.id));
    require(document.save("Smoke copy",true) && document.assetId()!=original.id && document.definition().emitter==saved.emitter,
        "Document Save As lost stable appearance settings.");
    require(read(particleAssetPath(project.root,original.id))==source,"Appearance document Save As overwrote source.");
}
void movableEndpointsPersistenceAndHistory() {
    Project project;const auto original=authoredEffect();saveParticleAsset(project.root,original);
    const auto savedNative=read(particleAssetPath(project.root,original.id));
    require(Json::parse(savedNative).at("version")==9 && loadParticleAsset(project.root,original.id)==current(original),
        "Native HDR endpoint tracks no longer load unchanged.");
    require(read(particleAssetPath(project.root,original.id))==savedNative,"Loading a saved HDR track rewrote the source.");
    ParticleEffectDocument document(project.root);require(document.load(original.id),"Cannot load movable-endpoint document.");
    require(document.begin("Move first and last size keys") && document.update([](auto& a){
        a.emitter.sizeTrack.curve.keys.front().age=.1f;a.emitter.sizeTrack.curve.keys.back().age=.9f;
    }) && document.update([](auto& a){
        a.emitter.sizeTrack.curve.keys.front().age=.2f;a.emitter.sizeTrack.curve.keys.back().age=.8f;
    }) && document.commit(),"First/last scalar key movement was rejected.");
    const auto movedCurve=document.definition();
    require(movedCurve.emitter.sizeTrack.curve.keys.front().id==original.emitter.sizeTrack.curve.keys.front().id &&
        movedCurve.emitter.sizeTrack.curve.keys.back().id==original.emitter.sizeTrack.curve.keys.back().id,
        "Moving scalar edge keys replaced their stable identities.");
    require(document.undo() && document.definition()==current(original) && !document.canUndo() && document.redo() && document.definition()==movedCurve,
        "Dragging scalar edge keys did not form one reversible transaction.");
    require(document.begin("Move gradient edges") && document.update([](auto& a){
        a.emitter.colorGradient.stops.front().age=.15f;a.emitter.colorGradient.stops.back().age=.85f;
    }),"Moving first/last gradient stops was rejected.");
    document.cancel();require(document.definition()==movedCurve,"Cancel did not restore moved gradient stop ages.");
    require(document.edit("Move appearance edges",[](auto& a){
        auto& e=a.emitter;e.colorGradient.stops.front().age=.15f;e.colorGradient.stops.back().age=.85f;
        e.rotationTrack.curve.keys.front().age=.1f;e.rotationTrack.curve.keys.back().age=.9f;
        e.intensityTrack.curve.keys.front().age=.25f;e.intensityTrack.curve.keys.back().age=.75f;
    }),"Movable appearance edges did not remain editable/saveable.");
    const auto result=document.definition();
    require(result.emitter.colorGradient.stops.front().id==original.emitter.colorGradient.stops.front().id &&
        result.emitter.colorGradient.stops.back().id==original.emitter.colorGradient.stops.back().id,
        "Moving gradient edge stops replaced their stable identities.");
    require(document.save(),"Movable edge tracks could not be saved.");
    const auto encoded=read(particleAssetPath(project.root,original.id));
    require(Json::parse(encoded).at("version")==9 && deserializeParticleAsset(encoded)==result && loadParticleAsset(project.root,original.id)==result,
        "Movable edge ages failed native v9 round-trip or reopening.");
    ParticleEffectDocument reopened(project.root);require(reopened.load(original.id) && reopened.definition()==result,
        "Movable curve/gradient edges were not retained across document reopening.");
    auto edge=original;edge.emitter.sizeTrack.curve.keys={{1,.4f,0},{2,.4f+MinimumParticleKeySpacing,2}};
    edge.emitter.colorGradient.stops={{1,.7f,{1,0,0,1}},{2,.7f+MinimumParticleKeySpacing,{0,1,0,0}}};
    require(deserializeParticleAsset(serializeParticleAsset(edge))==current(edge),"Two-key interior intervals failed spacing or persistence.");
}
}

int main() {
    try {
        modesAndPersistence(); priorVersions();hdrMigrationAndPersistence();preprocessPersistenceMigrationAndHistory();orientationPersistenceMigrationAndHistory(); validation(); documentHistory();movableEndpointsPersistenceAndHistory();
        std::cout<<"Particle Step 4 appearance schema, compatibility, validation and history checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
