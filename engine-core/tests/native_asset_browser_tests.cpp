#include "integration/browser/LevelAssetBrowser.h"
#include "integration/browser/QtAssetBrowser.h"
#include "ui/qt_asset_browser.hpp"
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
        SceneDocument saved(root);
        require(saved.edit("Prepare browser fixture",[&](auto& records,auto& properties){records=session.document().scene().records();properties=session.document().scene().properties();}),"Cannot prepare the isolated reference fixture.");
        require(saved.save("Browser level"),"Cannot save the isolated browser-domain reference fixture.");
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
    const auto folder=assets.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Grouped assets");
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
    const auto folder=assets.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Duplicate destination");
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
void unifiedFilesystemController(){
    ragdoll_test::Project project;QMainWindow window;integration::LevelSession session(project.root);
    integration::LevelAssetBrowser controller(window,[]{},false);require(controller.widget()==nullptr,"The unified controller constructed a second browser widget.");controller.bind(&session);
    const auto profile=session.assets().createNavigationAgentProfile("Shared browser profile");require(bool(profile),"Cannot create the unified browser fixture.");
    const auto file=resolveAssetPath(project.root,profile->id);const auto relative=QString::fromStdWString(file.lexically_relative(project.root).generic_wstring());
    quader::QtAssetBrowser view;auto extension=controller.extension();view.set_extension(extension);
    quader::QtAssetBrowserSnapshot snapshot;snapshot.generation=51;snapshot.revision=9;snapshot.items.push_back({.id=QStringLiteral("file:")+relative,.relative_path=relative,.name=QStringLiteral("Raw"),.kind=quader::QtAssetBrowserKind::File});extension->append_snapshot(snapshot);snapshot.dependency_revision=extension->revision();
    require(std::count_if(snapshot.items.begin(),snapshot.items.end(),[&](const auto& item){return item.relative_path==relative;})==1,"The Engine overlay duplicated a physical filesystem row.");
    const auto found=std::find_if(snapshot.items.begin(),snapshot.items.end(),[&](const auto& item){return item.extension_id==QString::fromStdString(profile->id);});require(found!=snapshot.items.end()&&found->external&&found->kind==quader::QtAssetBrowserKind::NavigationProfile,"The unified row lost its typed Engine identity.");
    const auto item=*found;require(view.set_snapshot(snapshot)&&view.reveal_item(item.id),"The shared browser could not publish/reveal an Engine row.");
    auto list=view.findChild<QListView*>(QStringLiteral("assetBrowser.grid"));require(list&&list->currentIndex().isValid(),"The unified browser has no selected visible row.");
    std::unique_ptr<QMimeData> mime(list->model()->mimeData({list->currentIndex()}));require(mime&&mime->hasFormat(QtAssetMime),"The shared browser did not append canonical Engine drag MIME.");
    const auto payload=nlohmann::json::parse(mime->data(QtAssetMime).toStdString());require(payload.size()==1&&payload.front().at("id")==profile->id&&payload.front().at("kind")==unsigned(AssetKind::NavigationAgentProfile),"Unified drag changed the canonical asset identity/kind.");
    quader::QtAssetBrowserActionRequest request;request.action=quader::QtAssetBrowserAction::Open;request.selected_items={item};request.dependency_revision=snapshot.dependency_revision;
    require(extension->handle_action(request)&&controller.takeActions().openNavigationAgentProfile==profile->id,"The shared browser Open bypassed the retained document action.");
    controller.setPrepareCommand([]{return false;});request.action=quader::QtAssetBrowserAction::CreateGraphMaterial;request.selected_items.clear();require(extension->handle_action(request)&&!controller.takeActions().createMaterial,"A shared-browser action bypassed the active document guard.");controller.setPrepareCommand([]{return true;});
    require(extension->handle_action(request)&&controller.takeActions().createMaterial,"The shared browser lost native material graph creation.");
    request.action=quader::QtAssetBrowserAction::Duplicate;request.selected_items={item};require(!extension->handle_action(request),"The shared browser intercepted physical Duplicate before the project service could clone/remap identities.");
    const auto count=snapshot.items.size();const auto copy=session.assets().copyNavigationAgentProfile(profile->id);require(bool(copy)&&controller.reveal(copy->id),"The viewless controller could not reveal the newly published physical copy.");
    snapshot.items.clear();extension->append_snapshot(snapshot);snapshot.dependency_revision=extension->revision();require(snapshot.items.size()==count+1&&view.set_snapshot(snapshot),"An Engine-only catalog revision failed to update the shared browser model.");
    const auto reveal=extension->take_reveal();require(!reveal.isEmpty()&&view.reveal_item(reveal),"Canonical Duplicate did not reveal/select the new shared-browser row.");
    const auto authored_bytes=read(file);const auto renamed=file.parent_path()/(std::filesystem::path("Physical filename").native()+file.extension().native());
    const ContentPathMove move{file,renamed};relocateContentPaths(project.root,std::span(&move,1));
    const std::array<StoredAssetPath,1> before{{{profile->id,file}}},after{{{profile->id,renamed}}};session.assets().publishPhysicalAssetMutation(before,after);
    snapshot.items.clear();extension->append_snapshot(snapshot);const auto renamed_row=std::find_if(snapshot.items.begin(),snapshot.items.end(),[&](const auto& value){return value.extension_id==QString::fromStdString(profile->id);});
    require(renamed_row!=snapshot.items.end()&&renamed_row->name==QStringLiteral("Physical filename")&&renamed_row->relative_path==QString::fromStdWString(renamed.lexically_relative(project.root).generic_wstring()),"A physical rename kept the authored display name on the filesystem card.");
    require(read(renamed)==authored_bytes,"A filesystem card rename rewrote the typed asset's authored display name.");
    controller.bind(nullptr);require(extension->revision()==0,"Unbinding retained an Engine browser publication.");
}

}
int main(int argc,char** argv){qputenv("QT_QPA_PLATFORM","minimal");QApplication app(argc,argv);try{unifiedFilesystemController();sharedSmallTextureMip();Fixture fixture;catalogAndDetachedThumbnails(fixture);duplicateRetainedKinds(fixture);explicitImmutableReferences(fixture);guardedDeletion(fixture);std::cout<<"Native asset catalog folders/search/MIME, retained asset duplication, detached/shared tiny thumbnails, explicit immutable async reports, stale cancellation and guarded replacement/deletion passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
