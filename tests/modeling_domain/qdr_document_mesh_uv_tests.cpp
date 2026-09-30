////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"
#include "operation/operation_atomicity_test_helpers.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

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

TEST(QdrDocumentMeshUvTests, TopologyUvTransferProjectsCornersOutsideTheSourceSurface) {
    Document source;
    ASSERT_TRUE(make_uv_quad_document(source));
    source.faces.front().uvs = {
        {2.0F, 3.0F}, {6.0F, 4.0F}, {9.0F, 10.0F}, {1.0F, 8.0F}};
    const Face& source_face = source.faces.front();
    const document_internal::FaceUvSampler sampler(source, source_face);
    const auto center_uv = sampler.sample({0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(center_uv.has_value());
    for (const quader::QVec3 position : {
             quader::QVec3{0.0F, 0.0F, 0.5F},
             quader::QVec3{0.0F, 3.0F, 0.5F}}) {
        Document destination = source;
        const ElementId new_corner = add_vertex(destination, position);
        Face& face = destination.faces.front();
        face.vertices = {source_face.vertices[0], source_face.vertices[1], new_corner};
        ASSERT_FALSE(sampler.sample(position).has_value());

        ASSERT_TRUE(document_internal::preserve_face_uvs(
            source, source_face, destination, face));

        ASSERT_EQ(face.uvs.size(), face.vertices.size());
        for (std::size_t corner = 0U; corner < 2U; ++corner) {
            EXPECT_EQ(face.uvs[corner].x, source_face.uvs[corner].x);
            EXPECT_EQ(face.uvs[corner].y, source_face.uvs[corner].y);
        }
        const auto expected = position.y == 0.0F
            ? *center_uv : quader::QVec2{5.0F, 9.0F};
        EXPECT_NEAR(face.uvs.back().x, expected.x, 0.0001F);
        EXPECT_NEAR(face.uvs.back().y, expected.y, 0.0001F);
        const auto mesh = compile_document(destination);
        ASSERT_EQ(mesh.indices.size(), 3U);
        for (const auto& vertex : mesh.vertices) {
            EXPECT_TRUE(std::isfinite(vertex.uv0.x));
            EXPECT_TRUE(std::isfinite(vertex.uv0.y));
        }
    }
}

TEST(QdrDocumentMeshUvTests, TopologyUvTransferRejectsUnresolvedDestinationVertices) {
    Document source;
    ASSERT_TRUE(make_uv_quad_document(source));
    source.faces.front().uvs = {
        {2.0F, 3.0F}, {6.0F, 4.0F}, {9.0F, 10.0F}, {1.0F, 8.0F}};
    Document destination = source;
    const ElementId missing_corner = add_vertex(destination, {0.0F, 0.0F, 0.5F});
    destination.faces.front().vertices[2] = missing_corner;
    destination.vertices.pop_back();
    Face& face = destination.faces.front();
    const auto original_uvs = face.uvs;

    EXPECT_FALSE(document_internal::preserve_face_uvs(
        source, source.faces.front(), destination, face));

    ASSERT_EQ(face.uvs.size(), original_uvs.size());
    for (std::size_t corner = 0U; corner < original_uvs.size(); ++corner) {
        EXPECT_EQ(face.uvs[corner].x, original_uvs[corner].x);
        EXPECT_EQ(face.uvs[corner].y, original_uvs[corner].y);
    }
}

TEST(QdrDocumentMeshUvTests, TopologyUvTransferRejectsNonfiniteGeometryAndMalformedCharts) {
    Document original;
    ASSERT_TRUE(make_uv_quad_document(original));
    original.faces.front().uvs = {
        {2.0F, 3.0F}, {6.0F, 4.0F}, {9.0F, 10.0F}, {1.0F, 8.0F}};
    for (const int malformed_input : {0, 1, 2, 3}) {
        SCOPED_TRACE(malformed_input);
        Document source = original;
        Document destination = original;
        const ElementId new_corner = add_vertex(destination, {0.0F, 0.0F, 0.5F});
        destination.faces.front().vertices[2] = new_corner;
        if (malformed_input == 0) {
            destination.vertices.back().position.x = std::numeric_limits<float>::quiet_NaN();
        } else if (malformed_input == 1) {
            source.faces.front().uvs.front().x = std::numeric_limits<float>::quiet_NaN();
        } else if (malformed_input == 2) {
            source.faces.front().uvs.pop_back();
        } else {
            source.vertices.front().position.x = std::numeric_limits<float>::quiet_NaN();
        }

        EXPECT_FALSE(document_internal::preserve_face_uvs(
            source, source.faces.front(), destination, destination.faces.front()));

        const auto& uvs = destination.faces.front().uvs;
        ASSERT_EQ(uvs.size(), original.faces.front().uvs.size());
        for (std::size_t corner = 0U; corner < uvs.size(); ++corner) {
            EXPECT_EQ(uvs[corner].x, original.faces.front().uvs[corner].x);
            EXPECT_EQ(uvs[corner].y, original.faces.front().uvs[corner].y);
        }
    }
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

TEST(QdrDocumentMeshUvTests, EffectiveUvInverseRetainsSettingsAndNonlinearCorners) {
    constexpr std::array<quader::QVec3, 4> kPositions{
        quader::QVec3{-2, -1, 0}, {3, -1, 0}, {3, 2, 0}, {-2, 2, 0}};
    constexpr std::array<quader::QVec2, 4> kDesired{
        quader::QVec2{0.1F, 0.2F}, {1.8F, -0.1F},
        {1.1F, 1.5F}, {-0.4F, 0.9F}};
    const FaceTextureTransform settings{
        .scale = {-2.3F, 0.7F}, .shift_uv = {0.2F, -0.8F},
        .rotation_degrees = 37.0F};
    const std::array<Transform3, 5> mappings{
        Transform3{},
        Transform3{.origin = {1.4F, -0.8F, 2.0F}},
        Transform3{.x_axis = {1.7F, 0.2F, 0.0F},
                   .y_axis = {0.4F, 0.8F, 0.0F},
                   .origin = {-1.0F, 0.7F, 0.0F}},
        Transform3{.x_axis = {-1.2F, 0.0F, 0.0F},
                   .y_axis = {0.0F, 0.75F, 0.0F},
                   .origin = {0.25F, -0.5F, 0.0F}},
        Transform3{.x_axis = {0.0F, 0.0F, -1.0F},
                   .y_axis = {0.0F, 1.0F, 0.0F},
                   .z_axis = {1.0F, 0.0F, 0.0F},
                   .origin = {1.0F, 0.5F, -0.25F}},
    };
    for (const auto &mapping : mappings) {
        const auto solved = solve_face_texture_base_uvs(
            kPositions, kDesired, {0, 0, 1}, settings, mapping);
        ASSERT_TRUE(solved.has_value());
        ASSERT_EQ(solved->size(), kDesired.size());
        const auto carrier = face_texture_carrier(
            kPositions, *solved, {0, 0, 1}, settings);
        for (std::size_t corner = 0U; corner < kDesired.size(); ++corner) {
            const auto actual = evaluate_texture_mapping(
                apply_face_texture_transform((*solved)[corner], settings),
                kPositions[corner], {0, 0, 1}, carrier, mapping);
            EXPECT_NEAR(actual.x, kDesired[corner].x, 1.0e-5F);
            EXPECT_NEAR(actual.y, kDesired[corner].y, 1.0e-5F);
        }
    }
}

TEST(QdrDocumentMeshUvTests, EffectiveUvInverseAllowsRankOneAndConstantTargets) {
    constexpr std::array<quader::QVec3, 4> kPositions{
        quader::QVec3{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    const Transform3 mapping{
        .x_axis = {-2, 0, 0}, .y_axis = {0, 0.75F, 0},
        .origin = {0.5F, -1.25F, 0}};
    const std::array<std::array<quader::QVec2, 4>, 2> targets{
        std::array<quader::QVec2, 4>{quader::QVec2{1, 3}, {2, 3}, {4, 3}, {5, 3}},
        std::array<quader::QVec2, 4>{quader::QVec2{2, -1}, {2, -1}, {2, -1}, {2, -1}},
    };
    for (const auto &desired : targets) {
        const auto solved = solve_face_texture_base_uvs(
            kPositions, desired, {0, 0, 1}, {}, mapping);
        ASSERT_TRUE(solved.has_value());
        const auto carrier = face_texture_carrier(kPositions, *solved, {0, 0, 1}, {});
        for (std::size_t corner = 0U; corner < desired.size(); ++corner) {
            const auto actual = evaluate_texture_mapping(
                (*solved)[corner], kPositions[corner], {0, 0, 1}, carrier, mapping);
            EXPECT_NEAR(actual.x, desired[corner].x, 1.0e-5F);
            EXPECT_NEAR(actual.y, desired[corner].y, 1.0e-5F);
        }
    }
}

TEST(QdrDocumentMeshUvTests, EffectiveUvInverseRejectsUnrepresentableMappings) {
    constexpr std::array<quader::QVec3, 4> kPositions{
        quader::QVec3{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    const std::array<quader::QVec2, 4> desired{
        quader::QVec2{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    const Transform3 collapsed{.x_axis = {0, 0, 0}};
    EXPECT_FALSE(solve_face_texture_base_uvs(
        kPositions, desired, {0, 0, 1}, {}, collapsed).has_value());
    auto invalid = desired;
    invalid.front().x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(solve_face_texture_base_uvs(
        kPositions, invalid, {0, 0, 1}, {}, {}).has_value());
    EXPECT_FALSE(solve_face_texture_base_uvs(
        kPositions, std::span<const quader::QVec2>(desired).first(3U),
        {0, 0, 1}, {}, {}).has_value());
    EXPECT_EQ(desired.front().x, 0.0F);
    EXPECT_EQ(kPositions.front().x, -1.0F);
}

TEST(QdrDocumentMeshUvTests, EffectiveUvInverseCarriesUvsThroughGeometricReflection) {
    constexpr std::array<quader::QVec3, 4> kOriginal{
        quader::QVec3{-2, -1, 0}, {3, -1, 0}, {3, 2, 0}, {-2, 2, 0}};
    const FaceTextureTransform settings{
        .scale = {1.3F, -0.8F}, .shift_uv = {0.7F, -0.2F},
        .rotation_degrees = 51.0F};
    const Transform3 mapping{
        .x_axis = {1.6F, 0.25F, 0}, .y_axis = {0.2F, 0.9F, 0},
        .origin = {1.0F, -0.5F, 0}};
    const auto original_carrier = face_texture_carrier(
        kOriginal, {}, {0, 0, 1}, settings);
    std::array<quader::QVec3, 4> reflected;
    std::array<quader::QVec2, 4> desired;
    for (std::size_t corner = 0U; corner < kOriginal.size(); ++corner) {
        const std::size_t reversed = kOriginal.size() - corner - 1U;
        const auto position = kOriginal[reversed];
        reflected[corner] = {1.0F - position.x, position.y, position.z};
        desired[corner] = evaluate_texture_mapping(
            apply_face_texture_transform(generated_texture_uv(position, {0, 0, 1}), settings),
            position, {0, 0, 1}, original_carrier, mapping);
    }
    const auto solved = solve_face_texture_base_uvs(
        reflected, desired, {0, 0, 1}, settings, mapping);
    ASSERT_TRUE(solved.has_value());
    const auto carrier = face_texture_carrier(reflected, *solved, {0, 0, 1}, settings);
    for (std::size_t corner = 0U; corner < reflected.size(); ++corner) {
        const auto actual = evaluate_texture_mapping(
            apply_face_texture_transform((*solved)[corner], settings),
            reflected[corner], {0, 0, 1}, carrier, mapping);
        EXPECT_NEAR(actual.x, desired[corner].x, 1.0e-5F);
        EXPECT_NEAR(actual.y, desired[corner].y, 1.0e-5F);
    }
}

TEST(QdrDocumentMeshUvTests,
     SplitUvDensityStitchesOnlyAssignedFacesInDocumentOrder) {
  using namespace quader_poly::document_internal;
  Document document;
  const ElementId a = add_vertex(document, {0.0F, 0.0F, 0.0F});
  const ElementId b = add_vertex(document, {1.0F, 0.0F, 0.0F});
  const ElementId c = add_vertex(document, {0.0F, 1.0F, 1.0F});
  const ElementId d = add_vertex(document, {1.0F, 1.0F, 1.0F});
  const ElementId reference_id = add_face(document, std::array<ElementId, 3>{a, b, c});
  const ElementId unrelated_id = add_face(document, std::array<ElementId, 3>{b, d, c});
  const ElementId target_id = add_face(document, std::array<ElementId, 3>{b, d, c});
  find_face(document, reference_id)->uvs = {{0, 0}, {2, 0}, {0, 2}};
  find_face(document, unrelated_id)->uvs = {{100, 100}, {200, 200}, {300, 300}};
  find_face(document, target_id)->uvs = {{0.25F, 0.25F}, {0.5F, 0.5F}, {0, 0.5F}};
  const auto unrelated_uvs = find_face(document, unrelated_id)->uvs;
  const std::array<MergeFaceUvAssignment, 2> assignments{{
      {target_id, {.inherited_loop_basis = true}},
      {reference_id, {.inherited_loop_basis = true}}}};

  stitch_merge_split_uv_density(document, assignments);

  const Face* target = find_face(document, target_id);
  ASSERT_NE(target, nullptr);
  ASSERT_EQ(target->uvs.size(), 3U);
  EXPECT_FLOAT_EQ(target->uvs[0].x, 2.0F);
  EXPECT_FLOAT_EQ(target->uvs[0].y, 0.0F);
  EXPECT_FLOAT_EQ(target->uvs[1].x, 4.0F);
  EXPECT_FLOAT_EQ(target->uvs[1].y, 2.0F);
  EXPECT_FLOAT_EQ(target->uvs[2].x, 0.0F);
  EXPECT_FLOAT_EQ(target->uvs[2].y, 2.0F);
  for (std::size_t index = 0; index < unrelated_uvs.size(); ++index) {
    EXPECT_FLOAT_EQ(find_face(document, unrelated_id)->uvs[index].x, unrelated_uvs[index].x);
    EXPECT_FLOAT_EQ(find_face(document, unrelated_id)->uvs[index].y, unrelated_uvs[index].y);
  }
  const auto source = qdr_test::document_fingerprint(document);
  stitch_merge_split_uv_density(document, {});
  EXPECT_EQ(qdr_test::document_fingerprint(document), source);
}
