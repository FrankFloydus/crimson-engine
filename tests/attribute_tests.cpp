#include "gameplay/Attributes.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
bool near(double a,double b){return std::abs(a-b)<1e-9;}
template<class Action> void rejects(Action action,const char* message){try{action();}catch(const std::invalid_argument&){return;}throw std::runtime_error(message);}
void nativeDefinitionsAndSubsetClosure() {
    const auto& catalog=nativeAttributeCatalog();require(catalog.definitions().size()==9,"Native attribute definition count changed.");
    require(catalog.find(stats::Health)->initializeToMaximum&&catalog.find(stats::Mana)->initializeToMaximum&&!catalog.find(stats::MaxHealth)->initializeToMaximum,"Native full-pool initialization policy changed.");
    AttributeSeeds seeds;require(addSeed(seeds,stats::Health)&&seedBase(seeds,stats::MaxHealth)==0&&seeds.size()==2,"Health did not add only its required capacity without a flat bonus.");
    require(!seedBase(seeds,stats::Vitality)&&!removeSeed(seeds,stats::MaxHealth),"Optional Vitality was forced or a required capacity was removed.");
    const auto before=seeds;require(!setSeedBase(seeds,stats::Health,75)&&seeds==before,"A full resource accepted an authored starting amount.");
    require(setSeedBase(seeds,stats::MaxHealth,75)&&!setSeedBase(seeds,stats::MaxHealth,75),"Authored starting Base helper did not report actual changes.");
    require(seedBase(seeds,stats::Health)==0&&removeSeed(seeds,stats::Health)&&removeSeed(seeds,stats::MaxHealth),"Seed edits damaged unrelated defaults or removal order.");
    require(addSeed(seeds,stats::Mana)&&seedBase(seeds,stats::MaxMana)==0&&!seedBase(seeds,stats::Spirit),"Mana required Spirit or failed its capacity closure.");
    AttributeSet values;require(values.reconcile(seeds)&&values.value(stats::Mana)==AttributeValue{0,0},values.status());
    require(!values.value(stats::Health)&&!values.value(stats::Spirit),"Absent attributes were fabricated as zero-valued attributes.");
    rejects([]{validateAttributeSeeds({{stats::Health,100}});},"Health without its capacity passed validation.");
    rejects([]{validateAttributeSeeds({{stats::Mana,1},{GameplayTag{"stats.mana"},2},{stats::MaxMana,100}});},"Duplicate case-insensitive attribute IDs passed validation.");
}
void baseCurrentDerivationsAndCapacityRules() {
    AttributeSeeds seeds{{stats::Health,100},{stats::MaxHealth,100},{stats::Mana,100},{stats::MaxMana,100},{stats::Vitality,20},{stats::Spirit,10}};
    AttributeSet values;require(values.reconcile(seeds),values.status());
    require(values.value(stats::MaxHealth)==AttributeValue{100,140}&&values.value(stats::MaxMana)==AttributeValue{100,120},"Vitality/Spirit formulas mutated Base or used the wrong stat.");
    require(values.value(stats::Health)==AttributeValue{140,140}&&values.value(stats::Mana)==AttributeValue{120,120},"New resources did not initialize to their derived maxima.");
    require(values.addBase(stats::Health,-30)&&values.value(stats::Health)==AttributeValue{110,110},"Instant damage changed only Current rather than running Base.");
    require(values.setBase(stats::Health,-5)&&values.value(stats::Health)==AttributeValue{-5,-5},"Health was incorrectly lower-clamped at zero.");
    require(values.setBase(stats::Mana,-5)&&values.value(stats::Mana)==AttributeValue{0,0},"Mana lower bound did not clamp Base and Current.");
    require(values.setBase(stats::Health,120)&&values.setBase(stats::MaxHealth,10)&&values.value(stats::Health)==AttributeValue{50,50},"Capacity decrease failed to clamp resource Base and Current.");
    require(values.setBase(stats::MaxHealth,150)&&values.value(stats::Health)==AttributeValue{50,50},"Capacity restoration resurrected previously clamped hidden Health.");
    require(values.setBase(stats::Vitality,0)&&values.value(stats::MaxHealth)==AttributeValue{150,150},"Derived maximum did not update after its optional input changed.");
    require(values.setBase(stats::MovementSpeed,5)==false,"Mutation silently created an unselected attribute.");
}
void nativeCapacitiesComeFromStats() {
    AttributeSeeds seeds;
    for(const auto& definition:nativeAttributeCatalog().definitions())seeds.push_back({definition.tag,definition.defaultBase});
    require(seedBase(seeds,stats::Health)==0&&seedBase(seeds,stats::Mana)==0&&
        seedBase(seeds,stats::MaxHealth)==0&&seedBase(seeds,stats::MaxMana)==0,
        "Native resource defaults introduced an arbitrary flat baseline.");
    setSeedBase(seeds,stats::Vitality,80);setSeedBase(seeds,stats::Spirit,70);
    AttributeSet values;require(values.reconcile(seeds),values.status());
    require(values.value(stats::MaxHealth)==AttributeValue{0,160}&&values.value(stats::MaxMana)==AttributeValue{0,140},
        "Native capacities were not derived entirely from Vitality and Spirit.");
    require(values.value(stats::Health)==AttributeValue{160,160}&&values.value(stats::Mana)==AttributeValue{140,140},"Resources above 100 were initialized from a flat or empty starting amount.");
    for(const auto& change:values.changes())if(change.tag==stats::Health||change.tag==stats::Mana)require(!change.before&&change.after&&change.after->base==change.after->current&&change.after->current>100,"Initialization published an empty intermediate pool.");
    for(auto& seed:seeds)if(seed.tag==stats::Vitality||seed.tag==stats::Spirit)seed.startingBase=0;
    AttributeSet zero;require(zero.reconcile(seeds)&&zero.value(stats::Health)==AttributeValue{0,0}&&zero.value(stats::Mana)==AttributeValue{0,0},"Zero derived capacities introduced a hidden starting bonus.");
    setSeedBase(seeds,stats::Vitality,5);setSeedBase(seeds,stats::Spirit,5);
    require(zero.reconcile(seeds)&&zero.value(stats::MaxHealth)->current==10&&zero.value(stats::MaxMana)->current==10&&zero.value(stats::Health)==AttributeValue{0,0}&&zero.value(stats::Mana)==AttributeValue{0,0},"Existing empty pools were filled by a capacity increase.");
    AttributeSet optionalMissing;require(optionalMissing.reconcile({{stats::Health,999},{stats::MaxHealth,25},{stats::Mana,-10},{stats::MaxMana,17}})&&optionalMissing.value(stats::Health)==AttributeValue{25,25}&&optionalMissing.value(stats::Mana)==AttributeValue{17,17},"Missing optional stats or obsolete pool seeds prevented full-capacity initialization.");
}
void seedReconciliationAndSettledChanges() {
    AttributeSeeds seeds{{stats::Health,100},{stats::MaxHealth,100},{stats::Vitality,0},{stats::MovementSpeed,5}};AttributeSet values;require(values.reconcile(seeds),values.status());
    require(values.addBase(stats::Health,-60)&&values.reconcile(seeds)&&values.value(stats::Health)==AttributeValue{40,40},"Unchanged authored seeds reset depleted runtime Base.");
    require(values.changes().empty(),"Unchanged seed reconciliation repeated change notifications.");
    require(setSeedBase(seeds,stats::Vitality,10)&&values.reconcile(seeds),values.status());
    require(values.value(stats::Health)==AttributeValue{40,40}&&values.value(stats::MaxHealth)==AttributeValue{100,120},"Optional-stat seed edit reset unrelated runtime state.");
    const auto changed=std::find_if(values.changes().begin(),values.changes().end(),[](const auto& change){return change.tag==stats::MaxHealth;});
    require(changed!=values.changes().end()&&changed->before==AttributeValue{100,100}&&changed->after==AttributeValue{100,120},"Settled notification omitted a dependent attribute change.");
    for(auto& seed:seeds)if(seed.tag==stats::Health)seed.startingBase=60;
    require(values.reconcile(seeds)&&values.value(stats::Health)==AttributeValue{40,40}&&values.changes().empty(),"An obsolete authored resource amount reset the running pool.");
    require(setSeedBase(seeds,stats::MovementSpeed,7)&&values.reconcile(seeds)&&values.value(stats::MovementSpeed)==AttributeValue{7,7}&&values.value(stats::Health)==AttributeValue{40,40},"Changed non-pool seed did not reseed only its runtime attribute.");
    require(values.setBase(stats::MovementSpeed,3)&&values.reconcile(seeds)&&values.value(stats::MovementSpeed)==AttributeValue{3,3},"Changed non-pool seed was reapplied on every reconcile.");
    require(values.addBase(stats::Health,-10)&&values.reconcile(seeds)&&values.value(stats::Health)==AttributeValue{30,30},"Resource reconciliation restored depleted runtime Base.");
    require(removeSeed(seeds,stats::Vitality)&&values.reconcile(seeds)&&values.value(stats::MaxHealth)==AttributeValue{100,100},"Removing an optional formula input retained its contribution.");
    require(values.setBase(stats::MovementSpeed,0)&&values.value(stats::MovementSpeed)==AttributeValue{0,0},"Zero MovementSpeed was rejected or replaced with a default.");
    require(removeSeed(seeds,stats::Health)&&values.reconcile(seeds)&&!values.value(stats::Health),"Removing a resource retained its runtime value.");
    require(addSeed(seeds,stats::Health)&&values.reconcile(seeds)&&values.value(stats::Health)==AttributeValue{100,100},"Re-adding a resource did not initialize a new full pool.");
    require(values.reconcile({})&&values.snapshot().empty(),"Removing the authored subset retained runtime attributes.");
}
void initializationGraphOrderAndValidation() {
    const GameplayTag maximum{"Stats.ZMaximum"},pool{"Stats.Pool"},consumer{"Stats.AConsumer"};
    AttributeDefinition capacity;capacity.tag=maximum;capacity.defaultBase=125;capacity.minimum=AttributeBound::number(0);
    AttributeDefinition resource;resource.tag=pool;resource.maximum=AttributeBound::reference(maximum);resource.initializeToMaximum=true;
    AttributeDefinition dependent;dependent.tag=consumer;dependent.contributions.push_back({pool,2,false});
    const GameplayTagRegistry registry({maximum,pool,consumer});const AttributeCatalog catalog(registry,{capacity,resource,dependent});
    AttributeSeeds seeds{{consumer,0},{pool,-999},{maximum,125}};AttributeSet values(catalog);
    require(values.reconcile(seeds)&&values.value(pool)==AttributeValue{125,125}&&values.value(consumer)==AttributeValue{0,250},"A downstream formula observed an empty intermediate pool during initialization.");
    require(values.changes().size()==3,"Initial graph published multiple intermediate change batches.");
    for(const auto& change:values.changes())if(change.tag==consumer)require(!change.before&&change.after==AttributeValue{0,250},"Initial change record exposed an uninitialized dependent value.");
    require(values.setBase(pool,10)&&values.value(consumer)==AttributeValue{0,20},"Runtime Base writes were blocked by the initialization policy.");
    seeds[1].startingBase=999;require(values.reconcile(seeds)&&values.value(pool)==AttributeValue{10,10}&&values.changes().empty(),"Changed obsolete pool seed refilled an existing runtime entry.");
    resource.maximum={};rejects([&]{AttributeCatalog invalid(registry,{capacity,resource,dependent});},"Full-pool initialization without a finite maximum was accepted.");
    resource.maximum=AttributeBound::number(std::numeric_limits<double>::infinity());rejects([&]{AttributeCatalog invalid(registry,{capacity,resource,dependent});},"Full-pool initialization with an infinite maximum was accepted.");
}
void regenerationAndDeath() {
    AttributeSeeds seeds{{stats::Health,50},{stats::MaxHealth,100},{stats::Mana,20},{stats::MaxMana,100},{stats::Vitality,20},{stats::Spirit,10},{stats::HealthRegen,2},{stats::ManaRegen,1}};AttributeSet values;require(values.reconcile(seeds),values.status());
    require(values.setBase(stats::Health,50)&&values.setBase(stats::Mana,20),"Cannot deplete full resources for regeneration checks.");
    require(values.value(stats::HealthRegen)==AttributeValue{2,3}&&values.value(stats::ManaRegen)==AttributeValue{1,2},"Derived flat regeneration used incorrect coefficients.");
    require(values.regenerate(2)&&values.value(stats::Health)==AttributeValue{56,56}&&values.value(stats::Mana)==AttributeValue{24,24},"Flat units-per-second regeneration did not change running Base.");
    require(values.setBase(stats::Health,0)&&values.regenerate(1)&&values.value(stats::Health)==AttributeValue{0,0},"Regeneration resurrected zero Health.");
    require(values.setBase(stats::Health,-5)&&values.regenerate(1)&&values.value(stats::Health)==AttributeValue{-5,-5},"Regeneration resurrected negative Health.");
    require(values.setBase(stats::Health,1)&&values.regenerate(1000)&&values.value(stats::Health)==AttributeValue{140,140}&&values.value(stats::Mana)==AttributeValue{120,120},"Regeneration did not clamp capacity or honor an explicit revive.");
    AttributeSet ratesOnly;require(ratesOnly.reconcile({{stats::HealthRegen,2}})&&ratesOnly.regenerate(100)&&!ratesOnly.value(stats::Health),"Regeneration fabricated its missing target resource.");
    AttributeSet split,whole;require(split.reconcile(seeds)&&whole.reconcile(seeds)&&split.setBase(stats::Health,50)&&split.setBase(stats::Mana,20)&&whole.setBase(stats::Health,50)&&whole.setBase(stats::Mana,20),"Cannot prepare fixed-rate regeneration.");for(unsigned index=0;index<60;++index)require(split.regenerate(1.0/60),split.status());require(whole.regenerate(1),whole.status());
    require(near(split.value(stats::Health)->current,whole.value(stats::Health)->current)&&near(split.value(stats::Mana)->current,whole.value(stats::Mana)->current),"Regeneration depended on frame partitioning.");
}
void atomicBatchesUnknownAndFiniteValidation() {
    AttributeSeeds seeds{{stats::Health,100},{stats::MaxHealth,100},{stats::Vitality,0}};AttributeSet values;require(values.reconcile(seeds),values.status());
    const AttributeBaseChange batch[]{{stats::Health,180},{stats::MaxHealth,150},{stats::Vitality,20}};
    require(values.applyBaseChanges(batch)&&values.value(stats::Health)==AttributeValue{180,180},"Batch clamped against an intermediate stale capacity.");
    const auto before=values.snapshot();const GameplayTag unknown{"Stats.Future"};const AttributeBaseChange invalid[]{{stats::Health,-5},{unknown,1}};
    require(!values.applyBaseChanges(invalid)&&values.value(stats::Health)==AttributeValue{180,180}&&values.changes().empty(),"Failed batch partially changed attribute state.");
    require(values.snapshot()==before,"Failed batch modified an unrelated accepted attribute.");
    require(!values.setBase(stats::Vitality,std::numeric_limits<double>::max())&&values.value(stats::Vitality)==AttributeValue{20,20},"Derived overflow corrupted accepted state.");
    require(!values.regenerate(std::numeric_limits<double>::infinity())&&!values.regenerate(-1),"Invalid regeneration intervals were accepted.");
    rejects([&]{setSeedBase(seeds,stats::Health,std::numeric_limits<double>::quiet_NaN());},"Non-finite authored seed was accepted.");
    seeds.push_back({unknown,7});require(setSeedBase(seeds,unknown,8)&&!addSeed(seeds,GameplayTag{"Stats.OtherFuture"}),"Unknown saved seed could not be edited or unknown native definition was fabricated.");
    require(values.reconcile(seeds)&&!values.value(unknown)&&!values.status().empty(),"Unknown seed was discarded or treated as an available runtime attribute.");
    require(seedBase(seeds,unknown)==8&&values.regenerate(0)&&!values.status().empty(),"Unknown authored seed or diagnostic was lost.");
}
void catalogDependencyValidation() {
    const GameplayTag a{"Stats.TestA"},b{"Stats.TestB"};const GameplayTagRegistry registry({a,b});
    AttributeDefinition first;first.tag=a;first.displayName="A";first.defaultBase=5;first.maximum=AttributeBound::reference(b);
    AttributeDefinition second;second.tag=b;second.displayName="B";second.defaultBase=10;
    const AttributeCatalog catalog(registry,{first,second});AttributeSeeds seeds;require(addSeed(seeds,a,catalog)&&seeds.size()==2,"Custom catalog required dependencies did not initialize.");
    AttributeSet values(catalog);require(values.reconcile(seeds)&&values.setBase(b,3)&&values.value(a)==AttributeValue{3,3},"Custom attribute-valued bound did not settle.");
    second.contributions.push_back({a,1,false});rejects([&]{AttributeCatalog invalid(registry,{first,second});},"Formula/bound cycle was accepted.");
    second.contributions.clear();second.maximum=AttributeBound::number(-1);second.minimum=AttributeBound::number(1);rejects([&]{AttributeCatalog invalid(registry,{first,second});},"Inverted constant bounds were accepted.");
    rejects([&]{AttributeCatalog invalid(registry,{first,first});},"Duplicate native definitions were accepted.");
    first.tag=GameplayTag{"State.X"};first.maximum={};rejects([&]{AttributeCatalog invalid(GameplayTagRegistry({first.tag}),{first});},"Attribute definition outside Stats was accepted.");
    first.tag=GameplayTag{"Stats"};rejects([&]{AttributeCatalog invalid(GameplayTagRegistry({first.tag}),{first});},"Stats parent itself was accepted as an attribute definition.");
}
}
int main(){try{nativeDefinitionsAndSubsetClosure();baseCurrentDerivationsAndCapacityRules();nativeCapacitiesComeFromStats();seedReconciliationAndSettledChanges();initializationGraphOrderAndValidation();regenerationAndDeath();atomicBatchesUnknownAndFiniteValidation();catalogDependencyValidation();std::cout<<"Native attribute definitions, Base/Current graph, subsets, full-pool initialization, seeds, regeneration and atomic mutation checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
