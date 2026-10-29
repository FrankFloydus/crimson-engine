#include "integration/browser/LevelAssetBrowser.h"
#include "integration/browser/QtAssetBrowser.h"
#include "integration/LevelSession.h"
#include "assets/AssetLibrary.h"
#include "assets/AtomicFile.h"
#include "resources/ResourceManager.h"
#include "resources/ImageDecoder.h"
#include "resources/TextureStreamer.h"
#include "ragdoll_fixture.h"
#include <QApplication>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QMimeData>
#include <QTemporaryDir>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
using Clock=std::chrono::steady_clock;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
std::map<std::filesystem::path,std::string> files(const std::filesystem::path& root) {
    std::map<std::filesystem::path,std::string> result;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root))
        if(entry.is_regular_file())result.emplace(entry.path().lexically_relative(root),read(entry.path()));
    return result;
}
struct Fixture {
    QTemporaryDir temporary;
    std::filesystem::path root;
    integration::LevelSession session;
    QMainWindow main;
    integration::LevelAssetBrowser browser;
    std::string source,replacement;
    EntityId entity{};
    Fixture():root(temporary.path().toStdWString()),session(root),browser(main,[]{}) {
        require(temporary.isValid(),"Cannot create isolated browser storage.");
        source=session.assets().createMaterial("Shared source")->id;
        replacement=session.assets().createMaterial("Accepted replacement")->id;
        entity=session.document().createBox("Current occurrence");
        auto record=*session.document().scene().find(entity);record.modelMaterials={AssetRef<MaterialAsset>{source}};
        require(session.document().updateEntity(entity,record,"Assign fixture material"),"Cannot assign browser reference fixture.");
        require(session.save("Browser level"),"Cannot save the browser reference owner.");
        browser.bind(&session);browser.setPrepareCommand([this]{return !session.document().transactionActive();});
    }
    void tick(){session.resources().pump();browser.update(0,true);}
    void ready() {
        const auto deadline=Clock::now()+std::chrono::seconds{10};
        while(Clock::now()<deadline){tick();if(!browser.referencesPending())return;std::this_thread::sleep_for(std::chrono::milliseconds{1});}
        throw std::runtime_error("The immutable native reference report did not finish.");
    }
};
void catalogAndDetachedThumbnails(Fixture& fixture) {
    auto& assets=fixture.session.assets();
    const auto folder=assets.browserCatalog().createFolder(BrowserCatalog::ContentFolder,"Grouped assets");
    const std::array ids{fixture.source,fixture.replacement};assets.browserCatalog().move(ids,folder);
    require(fixture.browser.reveal(fixture.source),"Reveal did not follow a moved stable asset identity.");
    auto view=fixture.browser.widget()->findChild<QListView*>(QStringLiteral("CrimsonAssetList"));
    auto search=fixture.browser.widget()->findChild<QLineEdit*>(QStringLiteral("CrimsonAssetSearch"));
    require(view&&search,"The native browser did not reuse the retained catalog view.");
    QModelIndex source;
    for(int row=0;row<view->model()->rowCount();++row)if(view->model()->index(row,0).data(Qt::UserRole).toString().toStdString()==fixture.source)source=view->model()->index(row,0);
    require(source.isValid(),"The moved source is absent from its native folder.");
    std::unique_ptr<QMimeData> mime(view->model()->mimeData({source}));
    const auto payload=nlohmann::json::parse(mime->data(QtAssetMime).toStdString());
    require(payload.size()==1&&payload[0]["id"]==fixture.source&&payload[0]["kind"]==unsigned(AssetKind::Material),
        "Native browser drag data lost stable identity or asset type.");
    search->setText(QStringLiteral("Accepted"));fixture.tick();
    require(view->model()->rowCount()==1&&view->model()->index(0,0).data(Qt::UserRole).toString().toStdString()==fixture.replacement,
        "Native search did not query the retained folder catalog.");
    require(fixture.browser.reveal(fixture.source)&&search->text().isEmpty(),"Reveal retained an incompatible search query.");
    DecodedImage pixels{2,1,{10,20,30,255,40,50,60,255}};qtPublishThumbnail(fixture.source,pixels);
    auto image=qtThumbnail(fixture.source);pixels.rgba[0]=200;
    require(!image.isNull()&&image.pixelColor(0,0).red()==10,"The native thumbnail retained a borrowed readback/source buffer.");
    image.fill(Qt::black);
    require(qtThumbnail(fixture.source).pixelColor(0,0).red()==10,"A consumer changed the shared accepted thumbnail.");
    qtClearThumbnails();require(qtThumbnail(fixture.source).isNull(),"The bounded thumbnail cache retained a previous project copy.");
}
void sharedSmallTextureMip() {
    QTemporaryDir temporary;require(temporary.isValid(),"Cannot create the isolated texture-mip fixture.");
    const std::filesystem::path root(temporary.path().toStdWString());
    const auto source=root/"source.bin";writeFileAtomically(source,"immutable source bytes");
    const std::string id="native-ui-test-image";const auto key=fingerprintFiles({source});
    std::vector<std::uint8_t> rgba(256*128*4,64);for(std::size_t alpha=3;alpha<rgba.size();alpha+=4)rgba[alpha]=255;
    const auto levels=streaming::generateTextureMips(256,128,rgba,streaming::TextureRole::Color);
    const auto descriptor=std::make_shared<const streaming::TextureMipCache>(streaming::writeTextureMipCache(root/"tiny.mips",key,streaming::TextureRole::Color,levels));
    ResourceManager resources(ResourceConfig{.workers=1,.idleGrace=std::chrono::milliseconds{0}});
    ResourceRequestOptions options;options.variant="mips:0";options.dependencies={source};
    auto seed=resources.request<streaming::TextureMipCache>(AssetRef<streaming::TextureMipCache>{id},[descriptor,key](std::stop_token){
        return ResourceLoadResult<streaming::TextureMipCache>{descriptor,sizeof(*descriptor),key,{}};
    },options);
    const auto deadline=Clock::now()+std::chrono::seconds{5};
    while(seed.state()!=ResourceState::Ready&&Clock::now()<deadline){resources.pump();std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(seed.state()==ResourceState::Ready,"The immutable texture descriptor did not become ready.");
    TextureStreamer stream(resources,root);ResourceSnapshot<DecodedImage> image;
    while(!image&&Clock::now()<deadline){resources.pump();image=stream.thumbnailPixels(id,source,64);std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(image&&image->width==64&&image->height==32&&image->rgba.size()==64*32*4,
        "The native UI thumbnail read more than the selected small cached mip.");
    const auto pointer=image.shared();const auto loads=resources.stats().loads;
    require(stream.thumbnailPixels(id,source,64).shared()==pointer&&resources.stats().loads==loads,
        "Repeated native thumbnail requests failed to share the canonical per-mip resource.");
    image={};const auto interests=resources.stats().liveInterests;stream.beginFrame();stream.trim(0);
    require(resources.stats().liveInterests<interests,"An idle native thumbnail retained its CPU request interest indefinitely.");
}
void duplicateRetainedKinds(Fixture& fixture) {
    auto& assets=fixture.session.assets();
    const auto folder=assets.browserCatalog().createFolder(BrowserCatalog::ContentFolder,"Duplicate destination");
    const auto profile=assets.createNavigationAgentProfile("Duplicate profile");
    const auto decal=assets.createDecal("Duplicate decal");
    const auto effect=assets.createGameplayEffect("Duplicate effect");
    auto model=ragdoll_test::source();ModelAnimationChannel idle;
    idle.node=0;idle.components=3;idle.times={0,1};idle.values={0,0,0,0,0,0};
    model.animations.push_back({"Idle",0,1,{idle}});
    const auto animation=importedAnimationAsset(model,0);saveAnimationAsset(fixture.root,animation);assets.refreshAnimations();
    const auto rig=makeRagdollAsset(model);saveRagdollAsset(fixture.root,rig);assets.refreshRagdolls();
    const std::array sources{std::pair{AssetKind::NavigationAgentProfile,profile->id},std::pair{AssetKind::Decal,decal->id},
        std::pair{AssetKind::Ragdoll,rig.id},std::pair{AssetKind::Animation,animation.id},std::pair{AssetKind::GameplayEffect,effect->id}};
    const auto before=files(fixture.root);fixture.browser.setPrepareCommand([]{return false;});
    require(!fixture.browser.requestDuplicate(profile->id,folder)&&files(fixture.root)==before,"A blocked browser command duplicated an asset before finishing its document owner.");
    fixture.browser.setPrepareCommand([&]{return !fixture.session.document().transactionActive();});
    for(const auto& [kind,id]:sources) {
        const auto source_path=resolveAssetPath(fixture.root,id);const auto original=read(source_path);
        require(fixture.browser.requestDuplicate(id,folder),"The native browser did not duplicate a retained asset kind.");
        const auto actions=fixture.browser.takeActions();
        const auto copy=kind==AssetKind::NavigationAgentProfile?actions.openNavigationAgentProfile:
            kind==AssetKind::Decal?actions.openDecal:kind==AssetKind::Ragdoll?actions.openRagdoll:
            kind==AssetKind::Animation?actions.openAnimation:actions.openGameplayEffect;
        require(!copy.empty()&&copy!=id&&assets.browserCatalog().parent(copy)==folder&&assets.browserSelection().contains(copy),
            "Browser duplication lost a new identity, its folder, selected card or matching document-open action.");
        const auto entry=std::find_if(assets.catalog().begin(),assets.catalog().end(),[&](const auto& value){return value.id==copy;});
        require(entry!=assets.catalog().end()&&entry->kind==kind&&read(source_path)==original,"Browser Duplicate dispatched the wrong asset owner or rewrote its source.");
    }
    require(!fixture.browser.requestDuplicate(fixture.source,folder),"Duplicate exposed a material command beyond the retained five asset kinds.");
}
void explicitImmutableReferences(Fixture& fixture) {
    const auto before=files(fixture.root);const auto policy=fixture.session.assets().references().serialize();
    require(fixture.browser.requestReferences(fixture.source),"The browser could not queue an explicit reference report.");
    fixture.ready();const auto report=fixture.browser.referenceReport();
    require(report&&!fixture.browser.referencesStale(),"A current native reference report was not published.");
    require(std::any_of(report->uses.begin(),report->uses.end(),[&](const auto& use){return use.active&&!use.historical&&use.entityId==fixture.entity;}),
        "The browser report omitted a stable current-scene occurrence.");
    const auto loads=fixture.session.resources().stats().loads;
    for(int frame=0;frame<20;++frame)fixture.tick();
    require(fixture.session.resources().stats().loads==loads,"Stable browser frames rebuilt the authored reference graph.");
    require(files(fixture.root)==before&&fixture.session.assets().references().serialize()==policy,
        "Read-only reference reporting changed authored files or deletion policy.");
    auto record=*fixture.session.document().scene().find(fixture.entity);record.name="Unsaved occurrence";
    require(fixture.session.document().updateEntity(fixture.entity,record,"Rename occurrence"),"Cannot prepare reference freshness fixture.");
    fixture.tick();require(fixture.browser.referencesStale()&&fixture.browser.referenceReport()==report,
        "An authoring edit silently replaced the report instead of marking its accepted snapshot stale.");
    require(fixture.browser.requestReferences(fixture.replacement),"Cannot queue a cancellable native report.");
    fixture.browser.cancelReferences();fixture.ready();
    require(!fixture.browser.referenceReport(),"A cancelled native request published a late snapshot.");
}
void guardedDeletion(Fixture& fixture) {
    auto& assets=fixture.session.assets();const auto sourceFile=materialAssetPath(fixture.root,fixture.source);
    const auto original=read(sourceFile);unsigned confirmations{};
    fixture.browser.setDeletionConfirmation([&](const auto& plan,auto&){++confirmations;require(!plan.warningUsages.empty(),"Deletion omitted the saved-scene replacement warning.");return false;});
    require(!fixture.browser.requestDeletion({fixture.source})&&confirmations==1&&read(sourceFile)==original&&!assets.references().deleted(fixture.source),
        "Cancelling native deletion changed the source or reserved its identity.");
    fixture.browser.setDeletionConfirmation([&](const auto& plan,auto& replacements){++confirmations;require(std::find(plan.assetIds.begin(),plan.assetIds.end(),fixture.source)!=plan.assetIds.end(),"Native deletion lost its selected stable identity.");replacements[fixture.source]=fixture.replacement;return true;});
    require(fixture.browser.requestDeletion({fixture.source}),"The native browser did not start guarded replacement/deletion.");
    const auto deadline=Clock::now()+std::chrono::seconds{10};
    while(fixture.browser.commandPending()&&Clock::now()<deadline){fixture.tick();std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(!fixture.browser.commandPending()&&assets.references().deleted(fixture.source)&&!std::filesystem::exists(sourceFile),
        "The native deletion transaction did not complete and release command ownership.");
    require(assets.references().resolveMaterial(fixture.source)==fixture.replacement&&
        fixture.session.document().scene().find(fixture.entity)->modelMaterials.front().id==fixture.replacement,
        "The native deletion bypassed the accepted reference replacement policy.");
    fixture.browser.bind(nullptr);fixture.tick();
}
}
int main(int argc,char** argv){qputenv("QT_QPA_PLATFORM","minimal");QApplication app(argc,argv);try{sharedSmallTextureMip();Fixture fixture;catalogAndDetachedThumbnails(fixture);duplicateRetainedKinds(fixture);explicitImmutableReferences(fixture);guardedDeletion(fixture);std::cout<<"Native asset catalog folders/search/MIME, retained asset duplication, detached/shared tiny thumbnails, explicit immutable async reports, stale cancellation and guarded replacement/deletion passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
