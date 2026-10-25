#pragma once
// Bounded exact polyhedral geodesics on the immutable analysis surface.
// Preserve the input metric; this owner does not weld, project or fabricate UVs.
// Propagation and interval merge adapted from pinned geometry-central
// 019669ddabda05e0f71fa3587cfb3c1dadf19cb8, exact_geodesics.cpp.
// Every capped interval retains its source-window unfolding and first heading;
// logarithmic-map queries do not run a greedy, potentially cyclic traceback.
// Copyright (C) 2008 Danil Kirsanov; geometry-central modifications under MIT.
/*
MIT License

Copyright (c) 2017-2019 Nicholas Sharp and the geometry-central contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
#include "DecalInternal.h"
#include <geometrycentral/surface/exact_geodesics.h>
#include <optional>
#include <algorithm>
#include <cmath>
#include <deque>
#include <stop_token>
#include <stdexcept>
namespace crimson::decals::detail::exact {
using namespace geometrycentral;
using namespace geometrycentral::surface;
struct Budget {size_t maximumIntervals{},maximumWork{},maximumPathSteps{};};
// The caller retains edge lengths and the source geometry's face frames.
// Evaluate a point in its actual adjacent face. A squared-length reconstruction
// can erase a positive altitude during either seeding or a later traceback.
inline std::pair<double,double> embeddedEdgeCoordinates(IntrinsicGeometryInterface& geometry,Edge edge,const SurfacePoint& point){
 const double length=geometry.edgeLengths[edge];
 if(!std::isfinite(length)||length<=0)throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic coordinate edge is invalid.");
 const auto canonical=edge.halfedge();
 if(point.type==SurfacePointType::Vertex){
  if(point.vertex==canonical.tailVertex())return {0,0};
  if(point.vertex==canonical.tipVertex())return {length,0};
 }
 if(point.type==SurfacePointType::Edge&&point.edge==edge)return {point.tEdge*length,0};
 const Face face=sharedFace(point,SurfacePoint(edge,.5));
 if(face==Face()||face.degree()!=3)throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic coordinate face is unavailable.");
 Halfedge local;
 for(Halfedge candidate:face.adjacentHalfedges())if(candidate.edge()==edge){local=candidate;break;}
 if(local==Halfedge())throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic coordinate edge lost its face.");
 const auto barycentrics=point.inFace(face).faceCoords;double along{},opposite{};int corner{};
 for(Vertex vertex:face.adjacentVertices()){
  if(vertex==local.tipVertex())along=barycentrics[corner];
  if(vertex==local.next().tipVertex())opposite=barycentrics[corner];
  ++corner;
 }
 const auto axis=geometry.halfedgeVectorsInFace[local],diagonal=-geometry.halfedgeVectorsInFace[local.next().next()];
 const Vector2 offset{std::fma(along,axis.x,opposite*diagonal.x),std::fma(along,axis.y,opposite*diagonal.y)};
 const double frameLength=std::hypot(axis.x,axis.y);
 if(!std::isfinite(frameLength)||frameLength<=0)throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic coordinate frame is invalid.");
 const double x=std::fma(axis.x,offset.x,axis.y*offset.y)/frameLength,product=axis.y*offset.x;
 const double twiceArea=std::fma(axis.x,offset.y,-product)-std::fma(axis.y,offset.x,-product);
 const std::pair<double,double> result{local.orientation()?x:length-x,std::abs(twiceArea)/frameLength};
 if(!std::isfinite(result.first)||!std::isfinite(result.second))throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic point coordinates are nonfinite.");
 return result;
}
inline void initializeVertexSourceInterval(IntrinsicGeometryInterface& geometry,Edge edge,Vertex source,IntervalWithStop& interval){
 const auto [x,y]=embeddedEdgeCoordinates(geometry,edge,SurfacePoint(source));const double length=geometry.edgeLengths[edge];
 interval.initialize(geometry,edge,length);interval.stop()=length;interval.d()=0;interval.source_index()=0;
 interval.pseudo_x()=x;interval.pseudo_y()=-y;
 interval.compute_min_distance(length);interval.direction()=Interval::DirectionType::FROM_SOURCE;
 if(!std::isfinite(interval.pseudo_x())||!std::isfinite(interval.pseudo_y())||!std::isfinite(interval.min()))
  throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic source interval is nonfinite.");
}
class BoundedGeo final:private GeodesicAlgorithmExact {
public:
 BoundedGeo(SurfaceMesh& mesh,IntrinsicGeometryInterface& geometry,Budget budget,std::stop_token stop={})
  :GeodesicAlgorithmExact(checkedMesh(mesh,budget,stop),geometry),budget_(budget),stop_(stop){
   if(!budget.maximumIntervals||!budget.maximumWork||!budget.maximumPathSteps)throw Failure(DecalPatchStatus::InvalidChart,"Invalid exact-geodesic budget.");
   // The base allocator retains a fixed O(input edges) block. It is never used
   // for propagation; all growing interval storage belongs to the capped arena.
 }
 ~BoundedGeo(){m_queue.clear();}
 size_t work()const{return work_;}size_t allocatedIntervals()const{return arena_.size();}
 void propagateBounded(Vertex source,double maximumDistance){
  ready_=false;
  if(!std::isfinite(maximumDistance)||maximumDistance<=0)throw Failure(DecalPatchStatus::InvalidChart,"Invalid exact-geodesic radius.");
  ready_=false;maximumDistance_=maximumDistance;work_=0;checkpoint();m_queue.clear();
  for(Edge e:mesh.edges()){checkpoint();m_edge_interval_lists[e].clear();}
  arena_.clear();free_.clear();m_iterations=0;m_queue_max_size=0;
  m_propagation_distance_stopped=GEODESIC_INF;set_sources({SurfacePoint(source)});
  Requirements requirements(geom);checkpoint();
  std::vector<Edge> visible;list_edges_visible_from_source(SurfacePoint(source),visible);
  for(Edge e:visible){checkpoint();IntervalWithStop candidate;initializeVertexSourceInterval(geom,e,source,candidate);const WindowHeading heading;boundedUpdate(interval_list(e),&candidate,1,&heading);}
  IntervalWithStop candidates[2];

  while (!m_queue.empty()) {
    m_queue_max_size = std::max(m_queue.size(), m_queue_max_size);

    checkpoint();
    if ((*m_queue.begin())->min() > maximumDistance) break;
    interval_pointer min_interval = *m_queue.begin();
    m_queue.erase(m_queue.begin());
    Edge edge = min_interval->edge();
    Halfedge he = edge.halfedge();

    assert(min_interval->d() < GEODESIC_INF);

    bool const first_interval = min_interval->start() == 0.0;
    // bool const last_interval = min_interval->stop() == edge->length();
    bool const last_interval = min_interval->next() == nullptr;

    auto saddleOrBoundary = [&](Vertex v) -> bool {
      bool saddle = geom.vertexGaussianCurvatures[v] < 0;
      return saddle || vertexIsBoundary[v];
    };

    bool const turn_left = saddleOrBoundary(he.tailVertex());
    bool const turn_right = saddleOrBoundary(he.tipVertex());

    auto propagateNonmanifoldVertex = [&](Vertex v, bool isTailVertex) {
      IntervalWithStop candidate;
      double Le = geom.edgeLengths[min_interval->edge()];
      double dx = isTailVertex ? min_interval->pseudo_x() : Le - min_interval->pseudo_x();
      double dy = min_interval->pseudo_y();
      double source_d = min_interval->d() + sqrt(dx * dx + dy * dy);

      for (Halfedge heSpoke : v.outgoingHalfedges()) {
        checkpoint();
        double L = geom.edgeLengths[heSpoke.edge()];
        candidate.start() = 0;
        candidate.stop() = L;
        candidate.d() = source_d;
        candidate.pseudo_x() = heSpoke.orientation() ? 0 : L;
        candidate.pseudo_y() = 0;

        candidate.next() = nullptr;
        candidate.edge() = heSpoke.edge();
        candidate.halfedge() = heSpoke;
        candidate.edge_length() = L;
        candidate.source_index() = min_interval->source_index();
        candidate.direction() = Interval::DirectionType::FROM_HALFEDGE;
        candidate.min() = candidate.d();

        auto inherited=parentFrame(min_interval,Face());inherited.depth=nextDepth(inherited.depth);
        if(!inherited.frozen&&source_d>min_interval->d()){
          inherited.first=sourceDirection(inherited,inherited.apply({(isTailVertex?0:Le)-min_interval->pseudo_x(),-min_interval->pseudo_y()}));inherited.frozen=true;
        }
        boundedUpdate(interval_list(heSpoke.edge()), &candidate, 1,&inherited);
      }
    };

    if (!vertexIsManifold[he.tailVertex()]) {
      propagateNonmanifoldVertex(he.tailVertex(), true);
    }
    if (!vertexIsManifold[he.tipVertex()]) {
      propagateNonmanifoldVertex(he.tipVertex(), false);
    }

    for (Halfedge neighboring_halfedge : edge.adjacentHalfedges()) {
      checkpoint();

      //== Check some early exit conditions
      if (!neighboring_halfedge.isInterior()) continue;

      // just in case, always propagate boundary edges
      if (!edge.isBoundary() && min_interval->halfedge() == neighboring_halfedge) continue;

      Face face = neighboring_halfedge.face();

      // if we come from 1, go to 2
      // Edge next_edge = (neighboring_halfedge.orientation() == he.orientation()) ?
      // neighboring_halfedge.next().next().edge() : neighboring_halfedge.next().edge();
      Halfedge next_halfedge = (neighboring_halfedge.orientation() == he.orientation())
                                   ? neighboring_halfedge.next().next()
                                   : neighboring_halfedge.next();
      Edge next_edge = next_halfedge.edge();

      double next_edge_length = geom.edgeLengths[next_edge];
      double corner_angle = (neighboring_halfedge.orientation() == he.orientation())
                                ? geom.cornerAngles[neighboring_halfedge.corner()]
                                : geom.cornerAngles[neighboring_halfedge.next().corner()];

      unsigned num_propagated = compute_propagated_parameters(min_interval->pseudo_x(), min_interval->pseudo_y(),
                                                              min_interval->d(), // parameters of the interval
                                                              min_interval->start(),
                                                              min_interval->stop(), // start/end of the interval
                                                              corner_angle,         // corner angle
                                                              next_edge_length,     // length of the new edge
                                                              first_interval, // if it is the first interval on the edge
                                                              last_interval,  // if it is the last interval on the edge
                                                              turn_left, turn_right, candidates);
      bool propagate_to_right = true;

      if (num_propagated) {
        if (candidates[num_propagated - 1].stop() != next_edge_length) {
          propagate_to_right = false;
        }

        // if the origins coinside, do not invert intervals
        // bool const invert = next_halfedge.edge().halfedge().tailVertex() != he.tailVertex();
        bool const invert = (next_halfedge == neighboring_halfedge.next().next()) ? next_halfedge.orientation()
                                                                                  : !next_halfedge.orientation();

        // invert: do not inverse
        construct_propagated_intervals(invert, next_halfedge, candidates, num_propagated, min_interval);

        const auto headings=propagatedHeadings(min_interval,face,corner_angle,invert,false,next_edge_length,candidates,num_propagated);
        boundedUpdate(interval_list(next_edge), candidates, num_propagated,headings.data());
      }

      if (propagate_to_right) {
        // propogation to the right edge
        double length = geom.edgeLengths[edge];

        // next_edge = (neighboring_halfedge.orientation() == he.orientation()) ? neighboring_halfedge.next().edge()
        // : neighboring_halfedge.next().next().edge();
        next_halfedge = (neighboring_halfedge.orientation() == he.orientation()) ? neighboring_halfedge.next()
                                                                                 : neighboring_halfedge.next().next();
        next_edge = next_halfedge.edge();

        next_edge_length = geom.edgeLengths[next_edge];
        corner_angle = (neighboring_halfedge.orientation() == he.orientation())
                           ? geom.cornerAngles[neighboring_halfedge.next().corner()]
                           : geom.cornerAngles[neighboring_halfedge.corner()];

        num_propagated = compute_propagated_parameters(length - min_interval->pseudo_x(), min_interval->pseudo_y(),
                                                       min_interval->d(), // parameters of the interval
                                                       length - min_interval->stop(),
                                                       length - min_interval->start(), // start/end of the interval
                                                       corner_angle,                   // corner angle
                                                       next_edge_length,               // length of the new edge
                                                       last_interval, // if it is the first interval on the edge
                                                       first_interval, turn_right, turn_left,
                                                       candidates); // if it is the last interval on the edge

        if (num_propagated) {
          // if the origins coinside, do not invert intervals
          // bool const invert = next_halfedge.edge().halfedge().tailVertex() != he.tipVertex();
          bool const invert = (next_halfedge == neighboring_halfedge.next().next()) ? next_halfedge.orientation()
                                                                                    : !next_halfedge.orientation();

          // invert: do not inverse
          construct_propagated_intervals(invert, next_halfedge, candidates, num_propagated, min_interval);

          const auto headings=propagatedHeadings(min_interval,face,corner_angle,invert,true,next_edge_length,candidates,num_propagated);
          boundedUpdate(interval_list(next_edge), candidates, num_propagated,headings.data());
        }
      }
    }
  }

  m_propagation_distance_stopped = m_queue.empty() ? GEODESIC_INF : (*m_queue.begin())->min();
  ready_=true;





 }
 std::optional<double> distanceBounded(Vertex vertex){
  if(!ready_)throw Failure(DecalPatchStatus::InvalidChart,"Exact geodesic preparation is incomplete.");
  checkpoint();const SurfacePoint point(vertex);chargeQuery(point);double distance,position;unsigned sourceIndex=std::numeric_limits<unsigned>::max();
  best_first_interval(point,distance,position,sourceIndex);
  if(!std::isfinite(distance))throw Failure(DecalPatchStatus::InvalidChart,"Exact geodesic distance is nonfinite.");
  if(distance>=GEODESIC_INF/2||distance>maximumDistance_)return std::nullopt;
  if(sourceIndex>=m_sources.size())throw Failure(DecalPatchStatus::InvalidChart,"Exact geodesic distance lost its source.");
  return distance;
 }
 std::optional<Vector2> logBounded(Vertex vertex){
  if(!ready_)throw Failure(DecalPatchStatus::InvalidChart,"Exact geodesic preparation is incomplete.");
  FaceFrames frames(geom);
  checkpoint();const SurfacePoint point(vertex);chargeQuery(point);
  double pathLength,position;unsigned sourceIndex=std::numeric_limits<unsigned>::max();
  const auto first=best_first_interval(point,pathLength,position,sourceIndex);
  if(!std::isfinite(pathLength))throw Failure(DecalPatchStatus::InvalidChart,"Exact geodesic path length is nonfinite.");
  if(pathLength>=GEODESIC_INF/2||pathLength>maximumDistance_)return std::nullopt;
  if(sourceIndex>=m_sources.size())throw Failure(DecalPatchStatus::InvalidChart,"Exact geodesic path lost its source.");
  if(pathLength==0)return Vector2::zero();
  if(const auto direct=directSourceDirection(vertex))return *direct*pathLength;
  if(!first)throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic path lost its source window.");
  const auto inherited=parentFrame(first,sharedFace(m_sources[sourceIndex],point));
  auto values=*first;
  const auto direction=inherited.frozen?inherited.first:sourceDirection(inherited,inherited.apply({position-values.pseudo_x(),-values.pseudo_y()}));
  if(!direction.isFinite()||direction.norm()==0)throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic source heading is invalid.");
  return direction*pathLength;
 }
private:
 struct WindowHeading {
  Face sourceFace{};Vector2 x{1,0},y{0,1},first{};size_t depth{};bool frozen{};
  Vector2 apply(Vector2 vector)const{return x*vector.x+y*vector.y;}
 };
 struct Window:Interval {WindowHeading heading;};
 static WindowHeading& heading(interval_pointer interval){return static_cast<Window*>(interval)->heading;}
 static const WindowHeading& heading(const_interval_pointer interval){return static_cast<const Window*>(interval)->heading;}
 size_t nextDepth(size_t depth){checkpoint();if(depth>=budget_.maximumPathSteps)throw Failure(DecalPatchStatus::LimitExceeded,"Exact-geodesic unfolding path budget exceeded.");return depth+1;}
 WindowHeading parentFrame(const_interval_pointer interval,Face face){
  auto result=heading(interval);if(result.frozen||result.sourceFace!=Face())return result;
  if(face==Face())for(Halfedge h:interval->edge().adjacentHalfedges())if(h.isInterior()){face=h.face();break;}
  if(face==Face())throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic source window lost its initial face.");
  Halfedge local;for(Halfedge h:face.adjacentHalfedges())if(h.edge()==interval->edge()){local=h;break;}
  if(local==Halfedge())throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic initial frame lost its edge.");
  const auto axis=geom.halfedgeVectorsInFace[local]*(local.orientation()?1.:-1.);const double length=std::hypot(axis.x,axis.y);
  if(!axis.isFinite()||!std::isfinite(length)||length<=0)throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic initial frame is invalid.");
  result.sourceFace=face;result.x=axis/length;result.y=Vector2{-result.x.y,result.x.x}*(local.orientation()?1.:-1.);return result;
 }
 Vector2 sourceDirection(const WindowHeading& inherited,Vector2 vector){
  checkpoint();if(inherited.sourceFace==Face()||!vector.isFinite()||vector.norm()==0)throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic incoming source heading is invalid.");
  const auto direction=exactgeodesic::transformToCoordinateSystem(geom,vector,inherited.sourceFace,m_sources[0]);
  if(!direction.isFinite()||direction.norm()==0)throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic source tangent is invalid.");
  return direction.normalize();
 }
 std::optional<Vector2> directSourceDirection(Vertex vertex){
  checkpoint();const Face face=sharedFace(m_sources[0],SurfacePoint(vertex));if(face==Face())return std::nullopt;
  const Vertex source=m_sources[0].vertex;Halfedge outgoing;
  for(Halfedge h:face.adjacentHalfedges())if(h.vertex()==source){outgoing=h;break;}
  if(outgoing==Halfedge())throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic direct source face is invalid.");
  const auto vector=outgoing.tipVertex()==vertex?geom.halfedgeVectorsInFace[outgoing]:-geom.halfedgeVectorsInFace[outgoing.next().next()];
  WindowHeading frame;frame.sourceFace=face;return sourceDirection(frame,vector);
 }
 std::array<WindowHeading,2> propagatedHeadings(interval_pointer parent,Face face,double angle,bool invert,bool right,double childLength,IntervalWithStop* candidates,unsigned count){
  const auto inherited=parentFrame(parent,face);const double cosine=std::cos(angle),sine=std::sin(angle);
  Vector2 x{invert?-cosine:cosine,invert?-sine:sine},y{-sine,cosine},origin=invert?Vector2{childLength*cosine,childLength*sine}:Vector2::zero();
  if(right){x.x=-x.x;y.x=-y.x;origin.x=geom.edgeLengths[parent->edge()]-origin.x;}
  std::array<WindowHeading,2> result;
  for(unsigned i=0;i<count;++i){checkpoint();auto& child=result[i];child=inherited;child.depth=nextDepth(inherited.depth);
   if(inherited.frozen)continue;
   child.x=inherited.apply(x);child.y=inherited.apply(y);
   if(candidates[i].d()<parent->d())throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic pseudo-source distance decreased.");
   if(candidates[i].d()>parent->d()){
    const auto pseudo=origin+x*candidates[i].pseudo_x()+y*candidates[i].pseudo_y();
    std::optional<Vector2> direct;const auto edge=parent->edge().halfedge();
    if(candidates[i].d()==parent->signal(0))direct=directSourceDirection(edge.tailVertex());
    if(!direct&&candidates[i].d()==parent->signal(geom.edgeLengths[parent->edge()]))direct=directSourceDirection(edge.tipVertex());
    child.first=direct?*direct:sourceDirection(inherited,inherited.apply(pseudo-Vector2{parent->pseudo_x(),parent->pseudo_y()}));child.frozen=true;
   }
   if(!child.x.isFinite()||!child.y.isFinite())throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic unfolding frame is nonfinite.");
  }
  return result;
 }
 struct FaceFrames {IntrinsicGeometryInterface& geometry;explicit FaceFrames(IntrinsicGeometryInterface& g):geometry(g){g.requireHalfedgeVectorsInFace();}~FaceFrames(){geometry.unrequireHalfedgeVectorsInFace();}};
 struct Requirements {IntrinsicGeometryInterface& geometry;explicit Requirements(IntrinsicGeometryInterface& g):geometry(g){g.requireCornerAngles();g.requireVertexGaussianCurvatures();g.requireHalfedgeVectorsInFace();}~Requirements(){geometry.unrequireHalfedgeVectorsInFace();geometry.unrequireVertexGaussianCurvatures();geometry.unrequireCornerAngles();}};
 static SurfaceMesh& checkedMesh(SurfaceMesh&mesh,Budget budget,std::stop_token stop){
  cancelled(stop);if(!budget.maximumIntervals||!budget.maximumWork||!budget.maximumPathSteps)throw Failure(DecalPatchStatus::InvalidChart,"Invalid exact-geodesic budget.");return mesh;
 }
 Budget budget_;std::stop_token stop_;size_t work_{};double maximumDistance_{};bool ready_{};std::deque<Window> arena_;std::vector<interval_pointer> free_;
 void checkpoint(size_t cost=1){if(stop_.stop_requested())throw Failure(DecalPatchStatus::Cancelled,"Exact geodesic preparation cancelled.");if(cost>budget_.maximumWork-work_)throw Failure(DecalPatchStatus::LimitExceeded,"Exact geodesic work budget exceeded.");work_+=cost;}
 interval_pointer allocateInterval(){checkpoint();if(!free_.empty()){const auto p=free_.back();free_.pop_back();heading(p)={};return p;}if(arena_.size()>=budget_.maximumIntervals)throw Failure(DecalPatchStatus::LimitExceeded,"Exact geodesic interval budget exceeded.");arena_.emplace_back();return &arena_.back();}
 void releaseInterval(interval_pointer p){checkpoint();heading(p)={};free_.push_back(p);}
 void chargeList(Edge edge){size_t count=0;for(auto p=interval_list(edge)->first();p;p=p->next()){checkpoint();++count;}checkpoint(count);}
 void chargeQuery(const SurfacePoint&p){switch(p.type){case SurfacePointType::Vertex:for(Halfedge h:p.vertex.outgoingHalfedges()){checkpoint();chargeList(h.edge());}break;case SurfacePointType::Edge:chargeList(p.edge);break;case SurfacePointType::Face:for(Edge e:p.face.adjacentEdges())chargeList(e);break;}}
 void boundedUpdate(list_pointer list,IntervalWithStop*candidates,unsigned num_candidates,const WindowHeading* candidateHeadings){
  checkpoint();for(unsigned i=0;i<num_candidates;++i)if(!std::isfinite(candidates[i].start())||!std::isfinite(candidates[i].stop())||!std::isfinite(candidates[i].d())||!std::isfinite(candidates[i].pseudo_x())||!std::isfinite(candidates[i].pseudo_y()))throw Failure(DecalPatchStatus::InvalidChart,"Exact-geodesic interval is nonfinite.");

  assert(num_candidates <= 2);
  // assert(list->first() != nullptr);
  Edge edge = list->edge();
  double edge_length = geom.edgeLengths[edge];
  double const local_epsilon = SMALLEST_INTERVAL_RATIO * edge_length;

  if (list->first() == nullptr) {
    interval_pointer* p = &list->first();
    IntervalWithStop* first;
    IntervalWithStop* second;

    if (num_candidates == 1) {
      first = candidates;
      second = candidates;
      first->compute_min_distance(first->stop());
    } else {
      if (candidates->start() <= (candidates + 1)->start()) {
        first = candidates;
        second = candidates + 1;
      } else {
        first = candidates + 1;
        second = candidates;
      }
      assert(first->stop() == second->start());

      first->compute_min_distance(first->stop());
      second->compute_min_distance(second->stop());
    }

    if (first->start() > 0.0) {
      *p = allocateInterval();
      (*p)->initialize(geom, edge, edge_length);
      p = &(*p)->next();
    }

    *p = allocateInterval();
    // memcpy(*p, first, sizeof(Interval));
    **p = *first;
    heading(*p)=candidateHeadings[first-candidates];
    m_queue.insert(*p);

    if (num_candidates == 2) {
      p = &(*p)->next();
      *p = allocateInterval();
      // memcpy(*p, second, sizeof(Interval));
      **p = *second;
      heading(*p)=candidateHeadings[second-candidates];
      m_queue.insert(*p);
    }

    if (second->stop() < edge_length) {
      p = &(*p)->next();
      *p = allocateInterval();
      (*p)->initialize(geom, edge, edge_length);
      (*p)->start() = second->stop();
    } else {
      (*p)->next() = nullptr;
    }
    return;
  }

  bool propagate_flag;

  // for all new intervals
  for (unsigned i = 0; i < num_candidates; ++i) {
      checkpoint();
    IntervalWithStop* q = &candidates[i];

    interval_pointer previous = nullptr;

    interval_pointer p = list->first();
    assert(p->start() == 0.0);

    while (p != nullptr && p->stop() - local_epsilon < q->start()) {
      checkpoint();
      p = p->next();
    }

    // go through all old intervals
    while (p != nullptr && p->start() < q->stop() - local_epsilon) {
      checkpoint();
      unsigned const N = intersect_intervals(p, q); // interset two intervals

      if (N == 1) {
        // if "p" is always better, we do not need to update anything
        if (map[0] == MapType::OLD) {
          if (previous) { // close previous interval and put in into the queue
            previous->next() = p;
            previous->compute_min_distance(p->start());
            m_queue.insert(previous);
            previous = nullptr;
          }

          p = p->next();

        } else if (previous) { // extend previous interval to cover everything; remove p
          previous->next() = p->next();
          erase_from_queue(p);
          releaseInterval(p);

          p = previous->next();
        } else { // p becomes "previous"
          previous = p;
          interval_pointer next = p->next();
          erase_from_queue(p);

          // memcpy(previous, q, sizeof(Interval));
          *previous = *q;
          heading(previous)=candidateHeadings[i];

          previous->start() = start[0];
          previous->next() = next;

          p = next;
        }
        continue;
      }

      // update_flag = true;

      Interval swap(*p); // used for swapping information
      const WindowHeading oldHeading=heading(p);
      propagate_flag = erase_from_queue(p);

      for (unsigned j = 1; j < N; ++j) { // no memory is needed for the first one
        // create new intervals
        i_new[j] = allocateInterval();
      }

      if (map[0] == MapType::OLD) { // finish previous, if any
        if (previous) {
          previous->next() = p;
          previous->compute_min_distance(previous->stop());
          m_queue.insert(previous);
          previous = nullptr;
        }
        i_new[0] = p;
        p->next() = i_new[1];
        p->start() = start[0];
      } else if (previous) { // extend previous interval to cover everything; remove p
        i_new[0] = previous;
        previous->next() = i_new[1];
        releaseInterval(p);
        previous = nullptr;
      } else { // p becomes "previous"
        i_new[0] = p;
        // memcpy(p, q, sizeof(Interval));
        *p = *q;
        heading(p)=candidateHeadings[i];

        p->next() = i_new[1];
        p->start() = start[0];
      }

      assert(!previous);

      for (unsigned j = 1; j < N; ++j) {
        interval_pointer current_interval = i_new[j];

        if (map[j] == MapType::OLD) {
          // memcpy(current_interval, &swap, sizeof(Interval));
          *current_interval = swap;
          heading(current_interval)=oldHeading;
        } else {
          // memcpy(current_interval, q, sizeof(Interval));
          *current_interval = *q;
          heading(current_interval)=candidateHeadings[i];
        }

        if (j == N - 1) {
          current_interval->next() = swap.next();
        } else {
          current_interval->next() = i_new[j + 1];
        }

        current_interval->start() = start[j];
      }

      for (unsigned j = 0; j < N; ++j) { // find "min" and add the intervals to the queue
        if (j == N - 1 && map[j] == MapType::NEW) {
          previous = i_new[j];
        } else {
          interval_pointer current_interval = i_new[j];

          current_interval->compute_min_distance(current_interval->stop()); // compute minimal distance

          if (map[j] == MapType::NEW || (map[j] == MapType::OLD && propagate_flag)) {
            m_queue.insert(current_interval);
          }
        }
      }

      p = swap.next();
    }

    if (previous) { // close previous interval and put in into the queue
      previous->compute_min_distance(previous->stop());
      m_queue.insert(previous);
      previous = nullptr;
    }
  }

 }
};
} // namespace crimson::decals::detail::exact
