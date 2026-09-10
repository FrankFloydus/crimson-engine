////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "public_api_detail.hpp"

namespace quader::modeling {
using namespace detail;
namespace {

[[nodiscard]] Result<OperationReceipt>
checked_receipt(const std::function<OperationReceipt()> &operation) {
  try {
    OperationReceipt receipt = operation();
    if (!receipt.success) {
      return Result<OperationReceipt>::failure(receipt.error);
    }
    return Result<OperationReceipt>::success(std::move(receipt));
  } catch (const ModelingException &exception) {
    return Result<OperationReceipt>::failure(exception.error());
  }
}

template <typename Operation>
[[nodiscard]] Result<PreviewHandle>
checked_preview(ModelingApiContext &context, Operation &&operation) {
  return checked_direct_value<PreviewHandle>(
      context, std::forward<Operation>(operation),
      [](const PreviewHandle &preview) { return preview.active(); },
      unsupported_error("No selected mesh."));
}

} // namespace



MeshSyncApi::MeshSyncApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

MeshSyncSnapshot
MeshSyncApi::changes_since(std::uint64_t previous_content_revision) const {
  const SessionChangeSnapshot changes =
      context_->session.changes_since(previous_content_revision);
  MeshSyncSnapshot snapshot{
      .content_revision = changes.content_revision,
  };
  for (const SessionObjectChange &change : changes.objects) {
    if (change.deleted) {
      snapshot.objects.push_back({
          .object = change.id,
          .name = change.name,
          .dirty = change.dirty,
          .deleted = true,
      });
      continue;
    }
    MeshHandle handle(context_, change.id);
    if (!(change.dirty.topology || change.dirty.geometry ||
          change.dirty.materials)) {
      snapshot.objects.push_back({
          .object = change.id,
          .name = change.name,
          .dirty = change.dirty,
      });
      continue;
    }
    snapshot.objects.push_back({
        .object = change.id,
        .name = change.name,
        .mesh = handle.payloads().compile_mesh(),
        .dirty = change.dirty,
    });
  }
  return snapshot;
}

MeshPayload MeshSyncApi::mesh(ObjectId object,
                              MeshCompileOptions options) const {
  return MeshHandle(context_, object).payloads().compile_mesh(options);
}

AuthoredPolygonPayload MeshSyncApi::authored_polygon(ObjectId object) const {
  return MeshHandle(context_, object).payloads().authored_polygon();
}

IoApi::IoApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

std::vector<std::byte> IoApi::serialize_qdr() const {
  std::vector<ObjectId> objects;
  for (const SessionObjectSummary &summary : context_->session.objects()) {
    objects.push_back(summary.id);
  }
  return serialize_qdr(objects);
}

std::vector<std::byte> IoApi::serialize_qdr(ObjectId object) const {
  std::vector<ObjectId> objects{object};
  return serialize_qdr(objects);
}

std::vector<std::byte>
IoApi::serialize_qdr(std::span<const ObjectId> objects) const {
  QdrDocumentDto dto;
  for (const ObjectId object : objects) {
    Result<PolygonDocument> document = context_->session.document_copy(object);
    if (!document.ok()) {
      return handle_value_failure(*context_, document.error(),
                                  std::vector<std::byte>{});
    }
    const MeshSummary summary = MeshHandle(context_, object).summary();
    dto.objects.push_back({
        .id = object,
        .selected = summary.selected,
        .name = summary.name,
        .document = std::move(document).value(),
    });
  }
  dto.active_object = objects.empty() ? ObjectId{} : objects.front();
  Result<std::vector<std::byte>> bytes = serialize_qdr_document(dto);
  if (!bytes.ok()) {
    return handle_value_failure(*context_, bytes.error(),
                                std::vector<std::byte>{});
  }
  return std::move(bytes).value();
}

MeshHandle IoApi::deserialize_qdr_object(std::span<const std::byte> bytes,
                                         std::string name) {
  Result<MeshHandle> result =
      CheckedIoApi(context_).deserialize_qdr_object(bytes, name);
  if (!result.ok()) {
    return handle_value_failure(*context_, result.error(), MeshHandle{});
  }
  return std::move(result).value();
}

std::vector<MeshHandle>
IoApi::deserialize_qdr_document(std::span<const std::byte> bytes) {
  Result<QdrDocumentDto> dto =
      quader::modeling::deserialize_qdr_document(bytes);
  if (!dto.ok()) {
    return handle_value_failure(*context_, dto.error(),
                                std::vector<MeshHandle>{});
  }
  std::vector<MeshHandle> result;
  for (QdrObjectDto &object : dto.value().objects) {
    result.push_back(ModelingApi(context_).add_document(
        std::move(object.document), object.name));
  }
  return result;
}

std::string IoApi::serialize_obj(ObjectId object) const {
  std::vector<ObjectId> objects{object};
  return serialize_obj(objects);
}

std::string IoApi::serialize_obj(std::span<const ObjectId> objects) const {
  std::string output;
  for (const ObjectId object : objects) {
    MeshHandle handle(context_, object);
    Result<std::string> obj =
        serialize_obj_mesh(handle.payloads().compile_mesh(),
                           handle.summary().name);
    if (!obj.ok()) {
      return handle_value_failure(*context_, obj.error(), std::string{});
    }
    output += obj.value();
  }
  return output;
}

CheckedIoApi::CheckedIoApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

Result<std::vector<std::byte>> CheckedIoApi::serialize_qdr() const {
  return checked_direct_value<std::vector<std::byte>>(
      *context_, [&] { return IoApi(context_).serialize_qdr(); });
}

Result<std::vector<std::byte>>
CheckedIoApi::serialize_qdr(ObjectId object) const {
  return checked_direct_value<std::vector<std::byte>>(
      *context_, [&] { return IoApi(context_).serialize_qdr(object); });
}

Result<std::vector<std::byte>>
CheckedIoApi::serialize_qdr(std::span<const ObjectId> objects) const {
  return checked_direct_value<std::vector<std::byte>>(
      *context_, [&] { return IoApi(context_).serialize_qdr(objects); });
}

Result<MeshHandle>
CheckedIoApi::deserialize_qdr_object(std::span<const std::byte> bytes,
                                     std::string name) {
  Result<QdrDocumentDto> dto =
      quader::modeling::deserialize_qdr_document(bytes);
  if (!dto.ok()) {
    return Result<MeshHandle>::failure(dto.error());
  }
  if (dto.value().objects.empty()) {
    return Result<MeshHandle>::failure(
        make_error(ErrorCode::InvalidArgument,
                   "QDR payload contains no objects."));
  }
  QdrObjectDto object = std::move(dto.value().objects.front());
  return CheckedModelingApi(context_).add_document(
      std::move(object.document), name.empty() ? object.name : std::move(name));
}

Result<std::vector<MeshHandle>>
CheckedIoApi::deserialize_qdr_document(std::span<const std::byte> bytes) {
  return checked_direct_value<std::vector<MeshHandle>>(
      *context_, [&] { return IoApi(context_).deserialize_qdr_document(bytes); });
}

Result<std::string> CheckedIoApi::serialize_obj(ObjectId object) const {
  return checked_direct_value<std::string>(
      *context_, [&] { return IoApi(context_).serialize_obj(object); });
}

Result<std::string>
CheckedIoApi::serialize_obj(std::span<const ObjectId> objects) const {
  return checked_direct_value<std::string>(
      *context_, [&] { return IoApi(context_).serialize_obj(objects); });
}

CheckedOperationsApi::CheckedOperationsApi(
    std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

Result<OperationReceipt> CheckedOperationsApi::delete_selection() {
  return checked_receipt([&] { return OperationsApi(context_).delete_selection(); });
}

Result<OperationReceipt> CheckedOperationsApi::copy_selection() {
  return checked_receipt([&] { return OperationsApi(context_).copy_selection(); });
}

Result<OperationReceipt>
CheckedOperationsApi::paste_selection(PasteOptions options) {
  return checked_receipt(
      [&] { return OperationsApi(context_).paste_selection(options); });
}

Result<OperationReceipt> CheckedOperationsApi::repeat_last_action() {
  return checked_receipt(
      [&] { return OperationsApi(context_).repeat_last_action(); });
}

Result<MeshHandle>
CheckedOperationsApi::duplicate_meshes(DuplicateOptions options) {
  return checked_direct_value<MeshHandle>(
      *context_,
      [&] { return OperationsApi(context_).duplicate_meshes(options); },
      [](const MeshHandle &mesh) { return mesh.valid(); },
      unsupported_error("No selected mesh."));
}

Result<OperationReceipt> CheckedOperationsApi::combine_meshes() {
  return checked_receipt([&] { return OperationsApi(context_).combine_meshes(); });
}

Result<OperationReceipt>
CheckedOperationsApi::translate_selection(Vec3 delta) {
  return checked_receipt(
      [&] { return OperationsApi(context_).translate_selection(delta); });
}

Result<OperationReceipt>
CheckedOperationsApi::transform_selection(TransformOptions options) {
  return checked_receipt(
      [&] { return OperationsApi(context_).transform_selection(options); });
}

Result<OperationReceipt>
CheckedOperationsApi::rotate_selection(RotateOptions options) {
  return checked_receipt(
      [&] { return OperationsApi(context_).rotate_selection(options); });
}

Result<OperationReceipt>
CheckedOperationsApi::scale_selection(ScaleOptions options) {
  return checked_receipt(
      [&] { return OperationsApi(context_).scale_selection(options); });
}

Result<OperationReceipt> CheckedOperationsApi::extrude(ExtrudeOptions options) {
  return checked_receipt([&] { return OperationsApi(context_).extrude(options); });
}

Result<OperationReceipt> CheckedOperationsApi::inset(InsetOptions options) {
  return checked_receipt([&] { return OperationsApi(context_).inset(options); });
}

Result<OperationReceipt> CheckedOperationsApi::mirror_selection(Axis axis) {
  return checked_receipt(
      [&] { return OperationsApi(context_).mirror_selection(axis); });
}

Result<OperationReceipt> CheckedOperationsApi::flip_horizontal() {
  return checked_receipt([&] { return OperationsApi(context_).flip_horizontal(); });
}

Result<OperationReceipt> CheckedOperationsApi::flip_vertical() {
  return checked_receipt([&] { return OperationsApi(context_).flip_vertical(); });
}

Result<OperationReceipt> CheckedOperationsApi::invert_mesh_normals() {
  return checked_receipt(
      [&] { return OperationsApi(context_).invert_mesh_normals(); });
}

Result<OperationReceipt> CheckedOperationsApi::shade_smooth_mesh() {
  return checked_receipt(
      [&] { return OperationsApi(context_).shade_smooth_mesh(); });
}

Result<OperationReceipt> CheckedOperationsApi::shade_flat_mesh() {
  return checked_receipt(
      [&] { return OperationsApi(context_).shade_flat_mesh(); });
}

Result<PreviewHandle>
CheckedOperationsApi::create_outer_hull(ThickenOptions options) {
  return checked_preview(
      *context_,
      [&] { return OperationsApi(context_).create_outer_hull(options); });
}

Result<OperationReceipt> CheckedOperationsApi::snap_vertices_to_active() {
  return checked_receipt(
      [&] { return OperationsApi(context_).snap_vertices_to_active(); });
}

Result<OperationReceipt> CheckedOperationsApi::merge_vertices_to_active() {
  return checked_receipt(
      [&] { return OperationsApi(context_).merge_vertices_to_active(); });
}

Result<OperationReceipt> CheckedOperationsApi::merge_vertices_to_center() {
  return checked_receipt(
      [&] { return OperationsApi(context_).merge_vertices_to_center(); });
}

Result<PreviewHandle>
CheckedOperationsApi::merge_by_distance(MergeByDistanceOptions options) {
  return checked_preview(
      *context_,
      [&] { return OperationsApi(context_).merge_by_distance(options); });
}

Result<OperationReceipt>
CheckedOperationsApi::remove_doubles(MergeByDistanceOptions options) {
  return checked_receipt(
      [&] { return OperationsApi(context_).remove_doubles(options); });
}

Result<OperationReceipt>
CheckedOperationsApi::bevel_vertices(BevelVerticesOptions options) {
  return checked_receipt(
      [&] { return OperationsApi(context_).bevel_vertices(options); });
}

Result<OperationReceipt> CheckedOperationsApi::connect_vertices() {
  return checked_receipt([&] { return OperationsApi(context_).connect_vertices(); });
}

Result<OperationReceipt> CheckedOperationsApi::dissolve_vertices() {
  return checked_receipt(
      [&] { return OperationsApi(context_).dissolve_vertices(); });
}

Result<OperationReceipt> CheckedOperationsApi::radial_align_vertices() {
  return checked_receipt(
      [&] { return OperationsApi(context_).radial_align_vertices(); });
}

Result<OperationReceipt> CheckedOperationsApi::snap_edges_to_active() {
  return checked_receipt(
      [&] { return OperationsApi(context_).snap_edges_to_active(); });
}

Result<OperationReceipt> CheckedOperationsApi::connect_edges() {
  return checked_receipt([&] { return OperationsApi(context_).connect_edges(); });
}

Result<OperationReceipt> CheckedOperationsApi::split_edges() {
  return checked_receipt([&] { return OperationsApi(context_).split_edges(); });
}

Result<OperationReceipt> CheckedOperationsApi::default_edge_normals() {
  return checked_receipt(
      [&] { return OperationsApi(context_).default_edge_normals(); });
}

Result<OperationReceipt> CheckedOperationsApi::harden_edge_normals() {
  return checked_receipt(
      [&] { return OperationsApi(context_).harden_edge_normals(); });
}

Result<OperationReceipt> CheckedOperationsApi::soften_edge_normals() {
  return checked_receipt(
      [&] { return OperationsApi(context_).soften_edge_normals(); });
}

Result<PreviewHandle>
CheckedOperationsApi::bevel_edges(EdgeBevelOptions options) {
  return checked_preview(
      *context_, [&] { return OperationsApi(context_).bevel_edges(options); });
}

Result<OperationReceipt>
CheckedOperationsApi::bridge_edges(BridgeOptions options) {
  return checked_receipt(
      [&] { return OperationsApi(context_).bridge_edges(options); });
}

Result<PreviewHandle>
CheckedOperationsApi::interpolated_bridge_edges(BridgeOptions options) {
  return checked_preview(*context_, [&] {
    return OperationsApi(context_).interpolated_bridge_edges(options);
  });
}

Result<OperationReceipt> CheckedOperationsApi::dissolve_edges() {
  return checked_receipt([&] { return OperationsApi(context_).dissolve_edges(); });
}

Result<OperationReceipt> CheckedOperationsApi::merge_edges() {
  return checked_receipt([&] { return OperationsApi(context_).merge_edges(); });
}

Result<OperationReceipt> CheckedOperationsApi::collapse_edges() {
  return checked_receipt([&] { return OperationsApi(context_).collapse_edges(); });
}

Result<OperationReceipt> CheckedOperationsApi::fill_hole() {
  return checked_receipt([&] { return OperationsApi(context_).fill_hole(); });
}

Result<OperationReceipt> CheckedOperationsApi::radial_align_edges() {
  return checked_receipt(
      [&] { return OperationsApi(context_).radial_align_edges(); });
}

Result<OperationReceipt>
CheckedOperationsApi::bridge_faces(BridgeOptions options) {
  return checked_receipt(
      [&] { return OperationsApi(context_).bridge_faces(options); });
}

Result<PreviewHandle>
CheckedOperationsApi::interpolated_bridge_faces(BridgeOptions options) {
  return checked_preview(*context_, [&] {
    return OperationsApi(context_).interpolated_bridge_faces(options);
  });
}

Result<OperationReceipt> CheckedOperationsApi::invert_faces() {
  return checked_receipt([&] { return OperationsApi(context_).invert_faces(); });
}

Result<OperationReceipt> CheckedOperationsApi::combine_faces() {
  return checked_receipt([&] { return OperationsApi(context_).combine_faces(); });
}

Result<OperationReceipt> CheckedOperationsApi::collapse_faces() {
  return checked_receipt([&] { return OperationsApi(context_).collapse_faces(); });
}

Result<OperationReceipt> CheckedOperationsApi::radial_align_faces() {
  return checked_receipt(
      [&] { return OperationsApi(context_).radial_align_faces(); });
}

Result<PreviewHandle>
CheckedOperationsApi::slice_quad(SliceQuadOptions options) {
  return checked_preview(
      *context_, [&] { return OperationsApi(context_).slice_quad(options); });
}

Result<PreviewHandle>
CheckedOperationsApi::thicken_faces(ThickenOptions options) {
  return checked_preview(
      *context_, [&] { return OperationsApi(context_).thicken_faces(options); });
}

Result<OperationReceipt> CheckedOperationsApi::extract_faces() {
  return checked_receipt([&] { return OperationsApi(context_).extract_faces(); });
}

Result<OperationReceipt> CheckedOperationsApi::detach_faces() {
  return checked_receipt([&] { return OperationsApi(context_).detach_faces(); });
}

Result<OperationReceipt>
CheckedOperationsApi::assign_material_slot_to_selection(std::uint32_t slot) {
  return checked_receipt([&] {
    return OperationsApi(context_).assign_material_slot_to_selection(slot);
  });
}

Result<OperationReceipt>
CheckedOperationsApi::plane_cut(PlaneCutOptions options) {
  return checked_receipt([&] { return OperationsApi(context_).plane_cut(options); });
}

Result<OperationReceipt>
CheckedOperationsApi::knife_segment(KnifeSegmentOptions options) {
  return checked_receipt(
      [&] { return OperationsApi(context_).knife_segment(options); });
}

Result<OperationReceipt>
CheckedOperationsApi::knife_stroke(KnifeStrokeOptions options) {
  return checked_receipt(
      [&] { return OperationsApi(context_).knife_stroke(options); });
}

Result<OperationReceipt>
CheckedOperationsApi::insert_edge_loop(InsertEdgeLoopOptions options) {
  return checked_receipt(
      [&] { return OperationsApi(context_).insert_edge_loop(options); });
}

RenderApi::RenderApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

RenderSnapshotPayload
RenderApi::scene_snapshot(PayloadRequest request) const {
  RenderSnapshotPayload snapshot;
  snapshot.lifetime = SnapshotLifetime::Owned;
  if (!require_owned_snapshot_lifetime(*context_, request.lifetime,
                                       "Scene snapshot")) {
    return snapshot;
  }
  const RevisionStamp revisions = current_revisions(*context_);
  snapshot.scene_revision = revisions.scene;
  if (request.only_if_changed &&
      revisions.scene <= request.previous_revision) {
    return snapshot;
  }
  const std::vector<SessionObjectSummary> objects = context_->session.objects();
  std::erase_if(context_->render_mesh_cache,
                [&](const ModelingRenderMeshCacheEntry &entry) {
                  return std::ranges::none_of(
                      objects, [&](const SessionObjectSummary &object) {
                        return object.id == entry.object;
                      });
                });
  for (const SessionObjectSummary &summary : objects) {
    Result<std::shared_ptr<const PolygonDocument>> document_result =
        context_->session.document_snapshot(summary.id);
    if (!document_result.ok()) {
      continue;
    }
    const std::shared_ptr<const PolygonDocument> document =
        std::move(document_result).value();
    auto cached = std::ranges::find_if(
        context_->render_mesh_cache,
        [&](const ModelingRenderMeshCacheEntry &entry) {
          return entry.object == summary.id;
        });
    const PolygonContentIdentity content_identity =
        document->content_identity();
    const bool cache_matches =
        cached != context_->render_mesh_cache.end() &&
        cached->content_identity == content_identity &&
        cached->content_revision == document->content_revision();
    if (!cache_matches) {
      Result<MeshPayload> compiled = document->compile_mesh();
      if (!compiled.ok()) {
        static_cast<void>(handle_failure(*context_, compiled.error()));
        snapshot.meshes.emplace_back();
      } else if (cached == context_->render_mesh_cache.end()) {
        context_->render_mesh_cache.push_back({
            .object = summary.id,
            .content_identity = content_identity,
            .content_revision = document->content_revision(),
            .mesh = std::move(compiled).value(),
        });
        cached = std::prev(context_->render_mesh_cache.end());
        snapshot.meshes.push_back(cached->mesh);
      } else {
        cached->content_identity = content_identity;
        cached->content_revision = document->content_revision();
        cached->mesh = std::move(compiled).value();
        snapshot.meshes.push_back(cached->mesh);
      }
    } else {
      snapshot.meshes.push_back(cached->mesh);
    }
    Result<SemanticOverlayPayload> overlay =
        context_->session.semantic_overlay(summary.id);
    if (overlay.ok()) {
      snapshot.overlays.push_back(std::move(overlay).value());
    }
  }
  snapshot.materials.push_back({
      .id = make_id<MaterialTag>(1),
      .name = "Default",
  });
  return snapshot;
}

} // namespace quader::modeling
