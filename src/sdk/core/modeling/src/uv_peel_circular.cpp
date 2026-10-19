////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "uv_peel_circular.hpp"

#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <utility>

namespace quader::modeling::detail {
namespace {

using Point3 = std::array<double, 3>;
using Point2 = std::array<double, 2>;
constexpr double kFitTolerance = 64.0 * std::numeric_limits<float>::epsilon();

[[nodiscard]] Point3 point(quader::QVec3 value) {
  return {value.x, value.y, value.z};
}
[[nodiscard]] Point3 difference(Point3 a, Point3 b) {
  return {a[0]-b[0], a[1]-b[1], a[2]-b[2]};
}
[[nodiscard]] Point3 scaled(Point3 a, double scale) {
  return {a[0]*scale, a[1]*scale, a[2]*scale};
}
[[nodiscard]] Point3 sum(Point3 a, Point3 b) {
  return {a[0]+b[0], a[1]+b[1], a[2]+b[2]};
}
[[nodiscard]] double dot(Point3 a, Point3 b) {
  return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
[[nodiscard]] Point3 cross(Point3 a, Point3 b) {
  return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
[[nodiscard]] double length(Point3 a) { return std::sqrt(dot(a,a)); }
[[nodiscard]] quader::QVec3 stored(Point3 a) {
  return {static_cast<float>(a[0]),static_cast<float>(a[1]),static_cast<float>(a[2])};
}

/// Records one finite convex quad and its common-plane acceptance scale.
struct PlanarFace {
  std::array<Point3,4> corners;
  Point3 normal;
  double scale{};
};

[[nodiscard]] std::optional<PlanarFace> planar_face(
    const quader_poly::Face &face,
    const quader_poly::document_internal::VertexLookupIndex &vertices) {
  if (face.vertices.size()!=4U) { return std::nullopt; }
  PlanarFace result;
  for (std::size_t i=0U;i<4U;++i) {
    const auto *vertex=vertices.find(face.vertices[i]);
    if (vertex==nullptr) { return std::nullopt; }
    result.corners[i]=point(vertex->position);
  }
  const auto first=difference(result.corners[1],result.corners[0]);
  const auto second=difference(result.corners[3],result.corners[0]);
  result.scale=std::max(length(first),length(second));
  result.normal=cross(first,second);
  const double normal_length=length(result.normal);
  if (!(result.scale>0.0) || !(normal_length>1e-10*result.scale*result.scale)) {
    return std::nullopt;
  }
  result.normal=scaled(result.normal,1.0/normal_length);
  for (std::size_t i=0U;i<4U;++i) {
    if (std::abs(dot(difference(result.corners[i],result.corners[0]),result.normal))>
        kFitTolerance*result.scale) { return std::nullopt; }
    const auto a=difference(result.corners[(i+1U)%4U],result.corners[i]);
    const auto b=difference(result.corners[(i+2U)%4U],result.corners[(i+1U)%4U]);
    if (!(dot(cross(a,b),result.normal)>1e-10*result.scale*result.scale)) {
      return std::nullopt;
    }
  }
  return result;
}

/// Keeps the fitted guide circle in a stable local orthogonal plane.
struct CircleFrame {
  Point3 center;
  Point3 axis_u;
  Point3 axis_v;
  double radius{};
};

/** Fit the circle identity |q-c|²=R² after centering and scale normalization. */
[[nodiscard]] std::optional<CircleFrame> fit_circle(
    const PlanarFace &plane, std::span<const quader_poly::ElementId> guide,
    const quader_poly::document_internal::VertexLookupIndex &vertices) {
  const auto origin=plane.corners[0];
  const auto axis_u=scaled(difference(plane.corners[1],origin),
      1.0/length(difference(plane.corners[1],origin)));
  const auto axis_v=cross(plane.normal,axis_u);
  std::vector<Point2> samples;
  samples.reserve(guide.size());
  Point2 mean{};
  for (const auto id:guide) {
    const auto *vertex=vertices.find(id);
    if (vertex==nullptr) { return std::nullopt; }
    const auto delta=difference(point(vertex->position),origin);
    const Point2 q{dot(delta,axis_u),dot(delta,axis_v)};
    // Consecutive projected duplicates are the same station, e.g. a riser.
    if (!samples.empty() && std::hypot(q[0]-samples.back()[0],q[1]-samples.back()[1])<
        kFitTolerance*plane.scale) { continue; }
    samples.push_back(q);
    mean[0]+=q[0]; mean[1]+=q[1];
  }
  if (samples.size()<3U) { return std::nullopt; }
  mean[0]/=static_cast<double>(samples.size());
  mean[1]/=static_cast<double>(samples.size());
  double scale=0.0;
  for (auto q:samples) { scale=std::max(scale,std::hypot(q[0]-mean[0],q[1]-mean[1])); }
  if (!(scale>0.0)) { return std::nullopt; }
  double xx=0.0,xy=0.0,yy=0.0,bx=0.0,by=0.0;
  for (auto q:samples) {
    const double x=(q[0]-mean[0])/scale,y=(q[1]-mean[1])/scale;
    const double rhs=.5*(x*x+y*y);
    xx+=x*x; xy+=x*y; yy+=y*y; bx+=x*rhs; by+=y*rhs;
  }
  const double determinant=xx*yy-xy*xy;
  if (!(determinant>1e-8*xx*yy)) { return std::nullopt; }
  const Point2 center{mean[0]+scale*(bx*yy-by*xy)/determinant,
                      mean[1]+scale*(by*xx-bx*xy)/determinant};
  double radius=0.0;
  for (auto q:samples) { radius+=std::hypot(q[0]-center[0],q[1]-center[1]); }
  radius/=static_cast<double>(samples.size());
  if (!(radius>1e-6*scale) || radius>1e4*scale) { return std::nullopt; }
  for (auto q:samples) {
    if (std::abs(std::hypot(q[0]-center[0],q[1]-center[1])-radius)>
        kFitTolerance*radius) { return std::nullopt; }
  }
  return CircleFrame{sum(origin,sum(scaled(axis_u,center[0]),scaled(axis_v,center[1]))),
                     axis_u,axis_v,radius};
}

[[nodiscard]] std::optional<std::vector<CircularPeelFace>> fit_domain(
    const quader_poly::Document &document,
    const quader_poly::document_internal::VertexLookupIndex &vertices,
    std::span<const quader_poly::ElementId> guide,
    std::span<const StructuredPeelFaceCoordinates> faces,
    const std::vector<std::size_t> &domain, const PlanarFace &plane) {
  auto frame=fit_circle(plane,guide,vertices);
  if (!frame.has_value()) { return std::nullopt; }
  Point3 centroid{};
  for (auto slot:domain) {
    for (auto id:document.faces[faces[slot].face_index].vertices) {
      centroid=sum(centroid,point(vertices.find(id)->position));
    }
  }
  centroid=scaled(centroid,1.0/(4.0*static_cast<double>(domain.size())));
  auto radial=difference(centroid,frame->center);
  radial=difference(radial,scaled(plane.normal,dot(radial,plane.normal)));
  if (!(length(radial)>kFitTolerance*frame->radius)) { return std::nullopt; }
  frame->axis_u=scaled(radial,1.0/length(radial));
  frame->axis_v=cross(plane.normal,frame->axis_u);
  // Fit against exactly the rounded frame that CPU/GPU consumers will retain.
  frame->center=point(stored(frame->center));
  frame->axis_u=point(stored(frame->axis_u));
  frame->axis_v=point(stored(frame->axis_v));
  const auto polar=[&](Point3 p)->std::optional<Point2> {
    const auto delta=difference(p,frame->center);
    const double x=dot(delta,frame->axis_u),y=dot(delta,frame->axis_v);
    if (!(x>kFitTolerance*frame->radius)) { return std::nullopt; }
    return Point2{std::hypot(x,y),std::atan2(y,x)};
  };
  std::vector<Point2> features;
  std::vector<quader::QVec2> target;
  for (auto slot:domain) {
    const auto &face=document.faces[faces[slot].face_index];
    for (std::size_t i=0U;i<4U;++i) {
      const auto q=polar(point(vertices.find(face.vertices[i])->position));
      if (!q.has_value()) { return std::nullopt; }
      features.push_back(*q);
      target.push_back({static_cast<float>(faces[slot].corners[i].u),
                        static_cast<float>(faces[slot].corners[i].v)});
    }
  }
  const auto fit=quader_poly::fit_texture_affine_chart(features,target);
  if (!fit.has_value()) { return std::nullopt; }
  std::array<double,2> low{target[0].x,target[0].y},high=low;
  for (auto uv:target) {
    low[0]=std::min(low[0],static_cast<double>(uv.x));
    low[1]=std::min(low[1],static_cast<double>(uv.y));
    high[0]=std::max(high[0],static_cast<double>(uv.x));
    high[1]=std::max(high[1],static_cast<double>(uv.y));
  }
  std::vector<CircularPeelFace> result;
  result.reserve(domain.size());
  for (std::size_t slot=0U;slot<domain.size();++slot) {
    const auto &face=document.faces[faces[domain[slot]].face_index];
    CircularPeelFace output{.coordinates=faces[domain[slot]]};
    auto &descriptor=output.interpolation;
    descriptor.center=stored(frame->center);
    descriptor.axis_u=stored(frame->axis_u);
    descriptor.axis_v=stored(frame->axis_v);
    // A branch-safe frame can cover this whole domain (less than a half-turn).
    // Keeping it shared also keeps derivatives identical at internal face edges.
    for (std::size_t i=0U;i<4U;++i) {
      const auto q=features[slot*4U+i];
      const std::array<double,2> uv{
          fit->carrier[0]*q[0]+fit->carrier[1]*q[1]+fit->offset[0],
          fit->carrier[2]*q[0]+fit->carrier[3]*q[1]+fit->offset[1]};
      const auto wanted=target[slot*4U+i];
      if (std::abs(uv[0]-wanted.x)>kFitTolerance*(high[0]-low[0]) ||
          std::abs(uv[1]-wanted.y)>kFitTolerance*(high[1]-low[1])) {
        return std::nullopt;
      }
      output.coordinates.corners[i]={uv[0],uv[1]};
    }
    auto linear=face.texture_transform;
    linear.shift_uv={};
    const auto r=quader_poly::unapply_face_texture_transform(
        {static_cast<float>(fit->carrier[0]),static_cast<float>(fit->carrier[2])},linear);
    const auto a=quader_poly::unapply_face_texture_transform(
        {static_cast<float>(fit->carrier[1]),static_cast<float>(fit->carrier[3])},linear);
    descriptor.uv_from_polar={r.x,a.x,r.y,a.y};
    if (!quader_poly::valid_circular_uv_interpolation(descriptor)) { return std::nullopt; }
    result.push_back(output);
  }
  return result;
}

} // namespace

std::vector<CircularPeelFace> fit_circular_peel(
    const quader_poly::Document &document,
    std::span<const quader_poly::ElementId> guide,
    std::span<const StructuredPeelFaceCoordinates> faces) {
  const quader_poly::document_internal::VertexLookupIndex vertices(document);
  std::vector<std::optional<PlanarFace>> geometry;
  geometry.reserve(faces.size());
  std::map<std::pair<quader_poly::ElementId,quader_poly::ElementId>,
           std::vector<std::size_t>> edge_faces;
  for (std::size_t slot=0U;slot<faces.size();++slot) {
    const auto &face=document.faces[faces[slot].face_index];
    geometry.push_back(planar_face(face,vertices));
    if (!geometry.back().has_value()) { continue; }
    for (std::size_t i=0U;i<4U;++i) {
      edge_faces[std::minmax(face.vertices[i],face.vertices[(i+1U)%4U])].push_back(slot);
    }
  }
  std::vector<bool> visited(faces.size(),false);
  std::vector<CircularPeelFace> result;
  for (std::size_t seed=0U;seed<faces.size();++seed) {
    if (visited[seed] || !geometry[seed].has_value()) { continue; }
    const auto &plane=*geometry[seed];
    std::vector<std::size_t> domain{seed};
    visited[seed]=true;
    for (std::size_t cursor=0U;cursor<domain.size();++cursor) {
      const auto &face=document.faces[faces[domain[cursor]].face_index];
      for (std::size_t edge=0U;edge<4U;++edge) {
        const auto key=std::minmax(face.vertices[edge],face.vertices[(edge+1U)%4U]);
        for (auto next:edge_faces.at(key)) {
          if (visited[next] || !geometry[next].has_value()) { continue; }
          const auto &candidate=*geometry[next];
          if (dot(candidate.normal,plane.normal)<1.0-1e-8) { continue; }
          bool coplanar=true;
          for (auto p:candidate.corners) {
            coplanar=coplanar && std::abs(dot(difference(p,plane.corners[0]),plane.normal))<=
                kFitTolerance*std::max(plane.scale,candidate.scale);
          }
          if (coplanar) { visited[next]=true; domain.push_back(next); }
        }
      }
    }
    auto fitted=fit_domain(document,vertices,guide,faces,domain,plane);
    if (fitted.has_value()) {
      result.insert(result.end(),fitted->begin(),fitted->end());
    }
  }
  return result;
}

} // namespace quader::modeling::detail
