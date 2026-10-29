#include "integration/native_asset_retention.hpp"
#include "assets/AssetLibrary.h"
#include "assets/AssetReferenceReport.h"
#include "editor/session/project_session.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <algorithm>
#include <fstream>

namespace {
using crimson::integration::NativeAssetRetention;
struct Fixture final {
    QTemporaryDir temporary;
    std::filesystem::path root{std::filesystem::path(temporary.path().toStdWString()) / "Project"};
    quader::ProjectSession project{std::filesystem::path(temporary.path().toStdWString()) / "recents.json"};
    std::unique_ptr<crimson::AssetLibrary> assets;
    std::shared_ptr<NativeAssetRetention> retention;
    std::string material;
    Fixture() {
        std::filesystem::create_directory(root);
        project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) / "resources/modeling_assets");
        const auto created = project.create(root);
        if (!created.ok) throw std::runtime_error(created.message);
        assets = std::make_unique<crimson::AssetLibrary>(root);
        const auto source = assets->createMaterial("Native reference");
        if (!source) throw std::runtime_error(assets->status());
        material = source->id;
        retention = std::make_shared<NativeAssetRetention>(project);
        const std::weak_ptr<NativeAssetRetention> weak = retention;
        assets->setExternalAssetRetentionProbe([weak] {
            const auto owner = weak.lock();
            if (!owner) throw std::runtime_error("Native retention owner is unavailable.");
            return owner->snapshot();
        }, [weak] { const auto owner = weak.lock(); return owner ? owner->revision() : 0u; });
    }
    quader::SceneSession& native() { return *project.active_scene()->session; }
    quader::EntityId reference() {
        const auto entity = native().scene().create_empty("Native material owner");
        quader::EngineComponentsComponent components{1u, "{\"materialOwner\":true}"};
        components.asset_references = {material};
        components.asset_references_complete = true;
        if (!native().scene().set_engine_components(entity, std::move(components)))
            throw std::runtime_error("Could not publish native reference.");
        return entity;
    }
};

TEST(NativeAssetRetention, LiveAndInactiveOpenNativeScenesProtectTypedAssets) {
    Fixture fixture;
    fixture.reference();
    EXPECT_FALSE(fixture.assets->planDeletion({fixture.material}).error.empty());
    const auto initial_session = fixture.project.active_scene_id();
    ASSERT_TRUE(fixture.project.new_scene().ok);
    ASSERT_NE(fixture.project.active_scene_id(), initial_session);
    const auto rows = fixture.retention->snapshot();
    EXPECT_TRUE(std::ranges::any_of(rows, [&](const auto& row) {
        return row.targetId == fixture.material && row.active && !row.historical;
    }));
    EXPECT_FALSE(fixture.assets->planDeletion({fixture.material}).error.empty());
}

TEST(NativeAssetRetention, SameFrameRemovalRetainsOnlyNativeHistoryPayloadLifetime) {
    Fixture fixture;
    const auto entity = fixture.reference();
    ASSERT_TRUE(fixture.native().document_commands().destroy_entity(entity).ok);
    EXPECT_EQ(fixture.native().scene().engine_components(entity), nullptr);
    EXPECT_EQ(fixture.native().history().undo_count(), 1u);
    const auto rows = fixture.retention->snapshot();
    EXPECT_TRUE(std::ranges::any_of(rows, [&](const auto& row) {
        return row.targetId == fixture.material && row.historical;
    }));
    EXPECT_FALSE(fixture.assets->planDeletion({fixture.material}).error.empty());
    ASSERT_TRUE(fixture.native().history().undo().ok);
    ASSERT_NE(fixture.native().scene().engine_components(entity), nullptr);
    ASSERT_TRUE(fixture.native().history().redo().ok);
    fixture.native().history().clear();
    const auto no_roots = fixture.retention->snapshot();
    EXPECT_TRUE(std::ranges::none_of(no_roots, [&](const auto& row) { return row.targetId == fixture.material; }));
    EXPECT_TRUE(fixture.assets->planDeletion({fixture.material}).error.empty());
}

TEST(NativeAssetRetention, UncataloguedSavedNativeQdrProtectsAfterLiveAndHistoryRelease) {
    Fixture fixture;
    const auto entity = fixture.reference();
    const auto bytes = quader::QdrDocumentWriter::serialize_project_document_bytes(fixture.native().document());
    const auto path = fixture.root / "Copied level.qdr";
    { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
    ASSERT_TRUE(fixture.native().scene().clear_engine_components(entity));
    fixture.native().history().clear();
    const auto rows = fixture.retention->snapshot();
    EXPECT_TRUE(std::ranges::any_of(rows, [&](const auto& row) {
        return row.targetId == fixture.material && !row.active && !row.historical;
    }));
    EXPECT_FALSE(fixture.assets->planDeletion({fixture.material}).error.empty());
    std::filesystem::remove(path);
    EXPECT_TRUE(fixture.assets->planDeletion({fixture.material}).error.empty());
}

TEST(NativeAssetRetention, NewNativeReferenceRejectsPreviouslyPreparedPhysicalDeletion) {
    Fixture fixture;
    const auto plan = fixture.assets->planDeletion({fixture.material});
    ASSERT_TRUE(plan.error.empty()) << plan.error;
    const auto path = crimson::resolveAssetPath(fixture.root, fixture.material);
    ASSERT_TRUE(std::filesystem::is_regular_file(path));
    fixture.reference();
    EXPECT_FALSE(fixture.assets->deleteAssets(plan));
    EXPECT_FALSE(fixture.assets->deletionPending());
    EXPECT_TRUE(std::filesystem::is_regular_file(path));
    EXPECT_NE(fixture.assets->status().find("native scene"), std::string::npos);
}

TEST(NativeAssetRetention, UnknownInventoriesAndUnreadableSavedNativeScenesFailClosed) {
    Fixture fixture;
    const auto entity = fixture.native().scene().create_empty("Opaque");
    ASSERT_TRUE(fixture.native().scene().set_engine_components(entity, {1u, "{}"}));
    EXPECT_THROW(fixture.retention->snapshot(), std::runtime_error);
    EXPECT_FALSE(fixture.assets->planDeletion({fixture.material}).error.empty());
    ASSERT_TRUE(fixture.native().scene().clear_engine_components(entity));
    fixture.native().history().clear();
    { std::ofstream file(fixture.root / "Broken.qdr", std::ios::binary); file << "invalid"; }
    EXPECT_THROW(fixture.retention->snapshot(), std::runtime_error);
    EXPECT_FALSE(fixture.assets->planDeletion({fixture.material}).error.empty());
}
}
