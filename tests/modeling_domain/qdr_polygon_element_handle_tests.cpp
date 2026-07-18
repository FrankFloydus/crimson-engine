////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <quader/modeling/neutral/polygon/element_handles.hpp>

#include <set>
#include <utility>

namespace {

using quader_poly::as_edge_key;
using quader_poly::as_face_id;
using quader_poly::as_vertex_id;
using quader_poly::Edge;
using quader_poly::EdgeKey;
using quader_poly::ElementId;
using quader_poly::FaceId;
using quader_poly::is_valid;
using quader_poly::kInvalidEdgeKey;
using quader_poly::kInvalidElementId;
using quader_poly::kInvalidFaceId;
using quader_poly::kInvalidVertexId;
using quader_poly::to_edge;
using quader_poly::to_element_id;
using quader_poly::to_element_ids;
using quader_poly::VertexId;

static_assert(to_element_id(as_vertex_id(42)) == 42);
static_assert(to_element_id(as_face_id(77)) == 77);
static_assert(as_vertex_id(2) < as_vertex_id(5));
static_assert(as_face_id(2) < as_face_id(5));
static_assert(as_edge_key(9, 3) == EdgeKey { as_vertex_id(3), as_vertex_id(9) });
static_assert(to_edge(as_edge_key(9, 3)) == Edge { 3, 9 });
static_assert(to_element_ids(as_edge_key(9, 3)) == std::pair<ElementId, ElementId> { 3, 9 });
static_assert(!is_valid(kInvalidVertexId));
static_assert(!is_valid(kInvalidFaceId));
static_assert(!is_valid(kInvalidEdgeKey));
static_assert(!is_valid(as_edge_key(kInvalidElementId, 5)));
static_assert(!is_valid(as_edge_key(5, 5)));

} // namespace

TEST(QdrPolygonElementHandleTests, VertexAndFaceIdsPreserveElementIdAdapters)
{
    const VertexId vertex = as_vertex_id(11);
    const FaceId face = as_face_id(11);

    EXPECT_TRUE(is_valid(vertex));
    EXPECT_TRUE(is_valid(face));
    EXPECT_EQ(to_element_id(vertex), 11U);
    EXPECT_EQ(to_element_id(face), 11U);
    EXPECT_EQ(to_element_id(kInvalidVertexId), kInvalidElementId);
    EXPECT_EQ(to_element_id(kInvalidFaceId), kInvalidElementId);
}

TEST(QdrPolygonElementHandleTests, StrongHandlesAreComparableAndOrderedByElementValue)
{
    const std::set<VertexId> vertices {
        as_vertex_id(9),
        as_vertex_id(3),
        as_vertex_id(9),
        as_vertex_id(1),
    };

    ASSERT_EQ(vertices.size(), 3U);
    auto cursor = vertices.begin();
    EXPECT_EQ(*cursor++, as_vertex_id(1));
    EXPECT_EQ(*cursor++, as_vertex_id(3));
    EXPECT_EQ(*cursor, as_vertex_id(9));

    const std::set<FaceId> faces {
        as_face_id(5),
        as_face_id(2),
        as_face_id(5),
    };

    ASSERT_EQ(faces.size(), 2U);
    auto face_cursor = faces.begin();
    EXPECT_EQ(*face_cursor++, as_face_id(2));
    EXPECT_EQ(*face_cursor, as_face_id(5));
}

TEST(QdrPolygonElementHandleTests, EdgeKeysCanonicalizeCurrentEdgeApiValues)
{
    const EdgeKey from_ids = as_edge_key(14, 2);
    const EdgeKey from_vertices = as_edge_key(as_vertex_id(2), as_vertex_id(14));
    const EdgeKey from_edge = as_edge_key(Edge { 14, 2 });

    EXPECT_TRUE(is_valid(from_ids));
    EXPECT_EQ(from_ids, from_vertices);
    EXPECT_EQ(from_ids, from_edge);
    EXPECT_EQ(to_edge(from_ids), (Edge { 2, 14 }));
    EXPECT_EQ(to_element_ids(from_ids), (std::pair<ElementId, ElementId> { 2, 14 }));
}

TEST(QdrPolygonElementHandleTests, InvalidHandlesRemainExplicitAndNonLive)
{
    EXPECT_FALSE(is_valid(kInvalidVertexId));
    EXPECT_FALSE(is_valid(kInvalidFaceId));
    EXPECT_FALSE(is_valid(kInvalidEdgeKey));
    EXPECT_FALSE(is_valid(as_edge_key(kInvalidElementId, 8)));
    EXPECT_FALSE(is_valid(as_edge_key(8, kInvalidElementId)));
    EXPECT_FALSE(is_valid(as_edge_key(8, 8)));
}
