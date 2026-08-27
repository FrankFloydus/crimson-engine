////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

namespace {

using namespace quader_poly;

bool nearly_equal(float left, float right, float epsilon = 0.0001F)
{
    return std::abs(left - right) <= epsilon;
}

::testing::AssertionResult make_uv_quad_document(
    Document& document,
    std::uint32_t material_slot = 7)
{
    const ElementId bottom_left = add_vertex(document, { -1.0F, -1.0F, 0.0F });
    const ElementId bottom_right = add_vertex(document, { 1.0F, -1.0F, 0.0F });
    const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
    const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
    const std::array<ElementId, 4> face_vertices { bottom_left, bottom_right, top_right, top_left };
    const ElementId face_id = add_face(document, face_vertices, material_slot);
    return require_test(face_id != kInvalidElementId, "quad face should be created");
}

void compile_document_preserves_loop_uvs_material_slots_and_normals()
{
    Document document;
    ASSERT_TRUE(make_uv_quad_document(document, 9));
    Face& face = document.faces.front();
    face.uvs = {
        { 0.125F, 0.25F },
        { 0.875F, 0.25F },
        { 0.875F, 0.75F },
        { 0.125F, 0.75F },
    };

    const CompiledMesh mesh = compile_document(document, SurfaceShading::Flat);
    ASSERT_TRUE(require_test(mesh.vertices.size() == 4U, "quad compile should emit one vertex per loop"));
    ASSERT_TRUE(require_test(mesh.indices.size() == 6U, "quad compile should triangulate to two triangles"));
    ASSERT_TRUE(require_test(mesh.primitives.size() == 1U, "quad compile should emit one primitive"));
    ASSERT_TRUE(require_test(mesh.primitives.front().material_slot == 9U, "compiled primitive should preserve face material slot"));
    ASSERT_TRUE(require_test(mesh.primitives.front().index_offset == 0U, "first primitive should start at index zero"));
    ASSERT_TRUE(require_test(mesh.primitives.front().index_count == 6U, "quad primitive should own six indices"));

    for (std::size_t index = 0; index < face.uvs.size(); ++index) {
        ASSERT_TRUE(require_test(nearly_equal(mesh.vertices[index].uv0.x, face.uvs[index].x), "compiled vertex should preserve explicit loop UV x"));
        ASSERT_TRUE(require_test(nearly_equal(mesh.vertices[index].uv0.y, face.uvs[index].y), "compiled vertex should preserve explicit loop UV y"));
        ASSERT_TRUE(require_test(nearly_equal(mesh.vertices[index].normal.z, 1.0F), "flat compiled normal should follow face normal"));
    }
}

} // namespace

TEST(QdrDocumentMeshUvTests, QdrDocumentMeshUvTests) {
    compile_document_preserves_loop_uvs_material_slots_and_normals();
}

TEST(QdrDocumentMeshUvTests, FaceSettingsRotateThenDivideThenShiftBaseUvs) {
    Document document;
    ASSERT_TRUE(make_uv_quad_document(document));
    Face &face = document.faces.front();
    face.uvs = {{2.0F, 4.0F}, {4.0F, 4.0F}, {4.0F, 6.0F}, {2.0F, 6.0F}};
    const auto original_uvs = face.uvs;
    face.texture_transform = {
        .scale = {2.0F, -4.0F},
        .shift_uv = {0.25F, -0.5F},
        .rotation_degrees = 90.0F,
    };
    const auto mesh = compile_document(document);
    ASSERT_EQ(mesh.vertices.size(), face.vertices.size());
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
        const auto uv = mesh.vertices[corner].uv0;
        EXPECT_NEAR(uv.x, -original_uvs[corner].y / 2.0F + 0.25F, 1.0e-6F);
        EXPECT_NEAR(uv.y, -original_uvs[corner].x / 4.0F - 0.5F, 1.0e-6F);
        const auto base = unapply_face_texture_transform(uv, face.texture_transform);
        EXPECT_NEAR(base.x, original_uvs[corner].x, 1.0e-6F);
        EXPECT_NEAR(base.y, original_uvs[corner].y, 1.0e-6F);
        EXPECT_EQ(face.uvs[corner].x, original_uvs[corner].x);
        EXPECT_EQ(face.uvs[corner].y, original_uvs[corner].y);
    }
}

TEST(QdrDocumentMeshUvTests, MaterializingGeneratedBaseDoesNotApplySettingsTwice) {
    Document document;
    ASSERT_TRUE(make_uv_quad_document(document));
    document.faces.front().texture_transform = {
        .scale = {3.0F, 0.5F}, .shift_uv = {-1.25F, 0.75F},
        .rotation_degrees = 35.0F,
    };
    const auto procedural = compile_document(document);
    ensure_face_uvs(document);
    ASSERT_FALSE(document.faces.front().uvs.empty());
    const auto materialized = compile_document(document);
    ASSERT_EQ(procedural.vertices.size(), materialized.vertices.size());
    for (std::size_t corner = 0U; corner < procedural.vertices.size(); ++corner) {
        EXPECT_EQ(procedural.vertices[corner].uv0.x,
                  materialized.vertices[corner].uv0.x);
        EXPECT_EQ(procedural.vertices[corner].uv0.y,
                  materialized.vertices[corner].uv0.y);
        EXPECT_EQ(procedural.vertices[corner].uv_carrier,
                  materialized.vertices[corner].uv_carrier);
    }
}

TEST(QdrDocumentMeshUvTests, GeneratedProjectionPreservesSourceAxisTiesAndSigns) {
    const quader::QVec3 position{2.0F, 3.0F, 5.0F};
    const auto x_tie = generated_texture_uv(position, {1.0F, 1.0F, -1.0F});
    EXPECT_EQ(x_tie.x, -2.5F);
    EXPECT_EQ(x_tie.y, -1.5F);
    const auto y_tie = generated_texture_uv(position, {0.0F, 1.0F, -1.0F});
    EXPECT_EQ(y_tie.x, -1.0F);
    EXPECT_EQ(y_tie.y, -1.5F);
    const auto negative_x = generated_texture_uv(position, {-1.0F, 0.0F, 0.0F});
    EXPECT_EQ(negative_x.x, 2.5F);
    EXPECT_EQ(negative_x.y, -1.5F);
    const auto up = generated_texture_uv(position, {0.0F, 1.0F, 0.0F});
    EXPECT_EQ(up.x, 1.0F);
    EXPECT_EQ(up.y, 2.5F);
}

TEST(QdrDocumentMeshUvTests, GeneratedCarrierSurvivesRightAngleRotationAndReflection) {
    Document document;
    ASSERT_TRUE(make_uv_quad_document(document));
    const auto mesh = compile_document(document);
    ASSERT_EQ(mesh.vertices.size(), 4U);
    const std::array<Transform3, 2> mappings{
        Transform3{.x_axis = {0.0F, 0.0F, -1.0F},
                   .y_axis = {0.0F, 1.0F, 0.0F},
                   .z_axis = {1.0F, 0.0F, 0.0F}},
        Transform3{.x_axis = {-1.0F, 0.0F, 0.0F}},
    };
    for (const auto &mapping : mappings) {
        std::array<quader::QVec2, 4> mapped;
        for (std::size_t corner = 0U; corner < mesh.vertices.size(); ++corner) {
            const auto &vertex = mesh.vertices[corner];
            EXPECT_EQ(vertex.uv_carrier, (std::array<float, 4>{1, 0, 0, 1}));
            EXPECT_EQ(vertex.uv_geometric_normal.z, 1.0F);
            mapped[corner] = evaluate_texture_mapping(
                vertex.uv0, vertex.position, vertex.uv_geometric_normal,
                vertex.uv_carrier, mapping);
            EXPECT_NEAR(mapped[corner].x, vertex.uv0.x, 1.0e-6F);
            EXPECT_NEAR(mapped[corner].y, vertex.uv0.y, 1.0e-6F);
        }
        const auto a = mapped[1] - mapped[0];
        const auto b = mapped[2] - mapped[0];
        EXPECT_GT(std::abs(a.x * b.y - a.y * b.x), 0.5F);
    }
}

TEST(QdrDocumentMeshUvTests, AuthoredCarrierRetainsNonlinearResiduals) {
    Document document;
    ASSERT_TRUE(make_uv_quad_document(document));
    auto &face = document.faces.front();
    const auto normal = face_normal(document, face);
    constexpr std::array<float, 4> kResidual{0.2F, -0.2F, 0.2F, -0.2F};
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
        const auto q = generated_texture_uv(
            find_vertex(document, face.vertices[corner])->position, normal);
        face.uvs.push_back({2.0F * q.x + 0.5F * q.y + 0.3F + kResidual[corner],
                            -q.x + 3.0F * q.y - 0.7F});
    }
    face.texture_transform = {
        .scale = {2.0F, -0.5F}, .shift_uv = {0.25F, -0.75F},
        .rotation_degrees = 30.0F,
    };
    const auto mesh = compile_document(document);
    const Transform3 mapping{.origin = {1.5F, 0.0F, 0.0F}};
    for (std::size_t corner = 0U; corner < mesh.vertices.size(); ++corner) {
        const auto &vertex = mesh.vertices[corner];
        // Translation contributes dq=(0.75,0); residual remains unchanged.
        const auto expected = apply_face_texture_transform(
            face.uvs[corner] + quader::QVec2{1.5F, -0.75F},
            face.texture_transform);
        const auto mapped = evaluate_texture_mapping(
            vertex.uv0, vertex.position, vertex.uv_geometric_normal,
            vertex.uv_carrier, mapping);
        EXPECT_NEAR(mapped.x, expected.x, 1.0e-5F);
        EXPECT_NEAR(mapped.y, expected.y, 1.0e-5F);
        const auto identity = evaluate_texture_mapping(
            vertex.uv0, vertex.position, vertex.uv_geometric_normal,
            vertex.uv_carrier, {});
        EXPECT_EQ(std::bit_cast<std::uint32_t>(identity.x),
                  std::bit_cast<std::uint32_t>(vertex.uv0.x));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(identity.y),
                  std::bit_cast<std::uint32_t>(vertex.uv0.y));
    }
}

TEST(QdrDocumentMeshUvTests, SmallScaleCofactorsKeepTheGeometricProjection) {
    Document document;
    ASSERT_TRUE(make_uv_quad_document(document));
    const auto mesh = compile_document(document);
    for (const float scale : {0.01F, 0.0001F, 1000.0F}) {
        const Transform3 mapping{
            .x_axis = {scale, 0.0F, 0.0F},
            .y_axis = {0.0F, scale, 0.0F},
            .z_axis = {0.0F, 0.0F, scale},
        };
        for (const auto &vertex : mesh.vertices) {
            const auto mapped = evaluate_texture_mapping(
                vertex.uv0, vertex.position, vertex.uv_geometric_normal,
                vertex.uv_carrier, mapping);
            EXPECT_NEAR(mapped.x, vertex.uv0.x * scale, 1.0e-6F);
            EXPECT_NEAR(mapped.y, vertex.uv0.y * scale, 1.0e-6F);
        }
    }
}

TEST(QdrDocumentMeshUvTests, CarrierAllowsRankOneAndSafelyRejectsDegenerateFits) {
    constexpr std::array<quader::QVec3, 4> kPositions{
        quader::QVec3{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    constexpr std::array<quader::QVec2, 4> kRankOne{
        quader::QVec2{-1, 7}, {1, 7}, {1, 7}, {-1, 7}};
    EXPECT_EQ(face_texture_carrier(kPositions, kRankOne, {0, 0, 1}, {}),
              (std::array<float, 4>{2, 0, 0, 0}));
    constexpr std::array<quader::QVec3, 4> kCollinear{
        quader::QVec3{-1, 0, 0}, {0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    EXPECT_EQ(face_texture_carrier(kCollinear, kRankOne, {0, 0, 1}, {}),
              (std::array<float, 4>{}));
    auto nonfinite = kRankOne;
    nonfinite[0].x = std::numeric_limits<float>::infinity();
    EXPECT_EQ(face_texture_carrier(kPositions, nonfinite, {0, 0, 1}, {}),
              (std::array<float, 4>{}));
}

TEST(QdrDocumentMeshUvTests, BakeMappingPreservesFaceSettingsAndViewportUvs) {
    Document document;
    ASSERT_TRUE(make_uv_quad_document(document));
    auto &face = document.faces.front();
    face.texture_transform = {
        .scale = {2.0F, 0.5F}, .shift_uv = {0.75F, -0.25F},
        .rotation_degrees = -25.0F,
    };
    const auto settings = face.texture_transform;
    const auto before = compile_document(document);
    EXPECT_FALSE(bake_document_texture_mapping(document, {}));
    EXPECT_TRUE(face.uvs.empty());
    const Transform3 mapping{.origin = {1.25F, -0.5F, 0.0F}};
    ASSERT_TRUE(bake_document_texture_mapping(document, mapping));
    EXPECT_EQ(document.faces.front().texture_transform, settings);
    const auto after = compile_document(document);
    ASSERT_EQ(before.vertices.size(), after.vertices.size());
    for (std::size_t corner = 0U; corner < before.vertices.size(); ++corner) {
        const auto &vertex = before.vertices[corner];
        const auto expected = evaluate_texture_mapping(
            vertex.uv0, vertex.position, vertex.uv_geometric_normal,
            vertex.uv_carrier, mapping);
        EXPECT_NEAR(after.vertices[corner].uv0.x, expected.x, 1.0e-5F);
        EXPECT_NEAR(after.vertices[corner].uv0.y, expected.y, 1.0e-5F);
    }
}
