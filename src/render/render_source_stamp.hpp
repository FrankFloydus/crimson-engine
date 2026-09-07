////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/render_instance_edit_scope.hpp"
#include "render/render_scene_binding.hpp"

#include <cstdint>
#include <initializer_list>

namespace quader {

class AssetRegistry;
class SceneDocument;
class ViewportLocalVisibilitySnapshot;

/** Identifies one independently comparable render-source fact. */
enum class RenderSourceDependency : std::uint64_t {
  SceneBinding = 1ull << 0u,
  SceneRevision = 1ull << 1u,
  SelectionRevision = 1ull << 2u,
  HierarchyPresentationRevision = 1ull << 3u,
  VisibilityRevision = 1ull << 4u,
  LocalVisibilityRevision = 1ull << 5u,
  TransformRevision = 1ull << 6u,
  TextureMappingRevision = 1ull << 7u,
  MeshBindingRevision = 1ull << 8u,
  CurveBindingRevision = 1ull << 9u,
  MaterialBindingRevision = 1ull << 10u,
  RenderComponentRevision = 1ull << 11u,
  LayerRevision = 1ull << 12u,
  AssetRevision = 1ull << 13u,
  MeshMaterialUsageRevision = 1ull << 14u,
  MaterialCatalogRevision = 1ull << 15u,
  ProjectMaterialRevision = 1ull << 16u,
  InstanceEditScope = 1ull << 17u,
};

/** Type-safe set of render-source dependencies. */
class RenderSourceDependencyMask final {
public:
  constexpr RenderSourceDependencyMask() = default;
  constexpr RenderSourceDependencyMask(RenderSourceDependency dependency)
      : bits_(static_cast<std::uint64_t>(dependency)) {}

  [[nodiscard]] static constexpr RenderSourceDependencyMask
  from(std::initializer_list<RenderSourceDependency> dependencies) noexcept {
    std::uint64_t bits = 0u;
    for (const RenderSourceDependency dependency : dependencies) {
      bits |= static_cast<std::uint64_t>(dependency);
    }
    return RenderSourceDependencyMask(bits);
  }

  [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0u; }
  [[nodiscard]] constexpr bool
  contains(RenderSourceDependency dependency) const noexcept {
    return (bits_ & static_cast<std::uint64_t>(dependency)) != 0u;
  }
  [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return bits_; }

  friend constexpr bool operator==(const RenderSourceDependencyMask &,
                                   const RenderSourceDependencyMask &) =
      default;

  constexpr void include(RenderSourceDependency dependency) noexcept {
    bits_ |= static_cast<std::uint64_t>(dependency);
  }

private:
  constexpr explicit RenderSourceDependencyMask(std::uint64_t bits)
      : bits_(bits) {}

  std::uint64_t bits_ = 0u;
};

/**
 * Immutable-by-contract value captured at a render extraction boundary.
 *
 * Snapshot owners replace the complete value when sources advance. Public
 * fields retain source compatibility for existing render-cache consumers.
 */
struct RenderSourceStamp {
  RenderSceneBindingKey scene_binding;
  std::uint64_t scene_revision = 0u;
  std::uint64_t selection_revision = 0u;
  std::uint64_t hierarchy_presentation_revision = 0u;
  std::uint64_t visibility_revision = 0u;
  std::uint64_t local_visibility_revision = 0u;
  std::uint64_t transform_revision = 0u;
  std::uint64_t texture_mapping_revision = 0u;
  std::uint64_t mesh_binding_revision = 0u;
  std::uint64_t curve_binding_revision = 0u;
  std::uint64_t material_binding_revision = 0u;
  std::uint64_t render_component_revision = 0u;
  std::uint64_t layer_revision = 0u;
  std::uint64_t asset_revision = 0u;
  std::uint64_t mesh_material_usage_revision = 0u;
  std::uint64_t material_catalog_revision = 0u;
  std::uint64_t project_material_revision = 0u;
  RenderInstanceEditScope instance_edit_scope;

  friend bool operator==(const RenderSourceStamp &,
                         const RenderSourceStamp &) = default;
};

/** Supplies non-document facts needed to capture one source stamp. */
struct RenderSourceStampCaptureOptions final {
  RenderSceneBindingKey scene_binding;
  const ViewportLocalVisibilitySnapshot *local_visibility = nullptr;
  std::uint64_t project_material_revision = 0u;
  RenderInstanceEditScope instance_edit_scope;
};

/** Captures scalar owner revisions only; this function never allocates or hashes. */
[[nodiscard]] RenderSourceStamp capture_render_source_stamp(
    const SceneDocument &document, const AssetRegistry &assets,
    RenderSourceStampCaptureOptions options = {});

/** Returns the requested dependencies whose typed values differ. */
[[nodiscard]] RenderSourceDependencyMask render_source_changed_dependencies(
    const RenderSourceStamp &left, const RenderSourceStamp &right,
    RenderSourceDependencyMask dependencies) noexcept;

/** Tests equality only across the dependencies declared by one cache branch. */
[[nodiscard]] bool render_source_dependencies_match(
    const RenderSourceStamp &left, const RenderSourceStamp &right,
    RenderSourceDependencyMask dependencies) noexcept;

/**
 * Tests that every requested scalar revision advanced by the same nonzero
 * delta. Binding and scope dependencies are intentionally rejected.
 */
[[nodiscard]] bool render_source_dependencies_advanced_together(
    const RenderSourceStamp &before, const RenderSourceStamp &after,
    RenderSourceDependencyMask dependencies) noexcept;

inline constexpr RenderSourceDependencyMask kRenderSourceAllDependencies =
    RenderSourceDependencyMask::from({
        RenderSourceDependency::SceneBinding,
        RenderSourceDependency::SceneRevision,
        RenderSourceDependency::SelectionRevision,
        RenderSourceDependency::HierarchyPresentationRevision,
        RenderSourceDependency::VisibilityRevision,
        RenderSourceDependency::LocalVisibilityRevision,
        RenderSourceDependency::TransformRevision,
        RenderSourceDependency::TextureMappingRevision,
        RenderSourceDependency::MeshBindingRevision,
        RenderSourceDependency::CurveBindingRevision,
        RenderSourceDependency::MaterialBindingRevision,
        RenderSourceDependency::RenderComponentRevision,
        RenderSourceDependency::LayerRevision,
        RenderSourceDependency::AssetRevision,
        RenderSourceDependency::MeshMaterialUsageRevision,
        RenderSourceDependency::MaterialCatalogRevision,
        RenderSourceDependency::ProjectMaterialRevision,
        RenderSourceDependency::InstanceEditScope,
    });

/** Exact inputs that can invalidate retained main-scene records. */
inline constexpr RenderSourceDependencyMask
    kRenderSceneSnapshotSourceDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::SelectionRevision,
            RenderSourceDependency::VisibilityRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::TransformRevision,
            RenderSourceDependency::TextureMappingRevision,
            RenderSourceDependency::MeshBindingRevision,
            RenderSourceDependency::CurveBindingRevision,
            RenderSourceDependency::MaterialBindingRevision,
            RenderSourceDependency::RenderComponentRevision,
            RenderSourceDependency::MeshMaterialUsageRevision,
            RenderSourceDependency::MaterialCatalogRevision,
            RenderSourceDependency::ProjectMaterialRevision,
            RenderSourceDependency::InstanceEditScope,
        });

/** Facts that must stay fixed for a curve-only main-scene stamp refresh. */
inline constexpr RenderSourceDependencyMask
    kRenderSceneCurveRefreshStableDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::SelectionRevision,
            RenderSourceDependency::VisibilityRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::TransformRevision,
            RenderSourceDependency::TextureMappingRevision,
            RenderSourceDependency::MeshBindingRevision,
            RenderSourceDependency::MaterialBindingRevision,
            RenderSourceDependency::MeshMaterialUsageRevision,
            RenderSourceDependency::MaterialCatalogRevision,
            RenderSourceDependency::ProjectMaterialRevision,
            RenderSourceDependency::InstanceEditScope,
        });

inline constexpr RenderSourceDependencyMask
    kRenderSceneCurveRefreshAdvanceDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneRevision,
            RenderSourceDependency::CurveBindingRevision,
            RenderSourceDependency::RenderComponentRevision,
        });

/** Facts that must stay fixed for a selection-only main-scene refresh. */
inline constexpr RenderSourceDependencyMask
    kRenderSceneSelectionRefreshStableDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::AssetRevision,
            RenderSourceDependency::ProjectMaterialRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::InstanceEditScope,
            RenderSourceDependency::VisibilityRevision,
            RenderSourceDependency::TransformRevision,
            RenderSourceDependency::TextureMappingRevision,
            RenderSourceDependency::MeshBindingRevision,
            RenderSourceDependency::RenderComponentRevision,
        });

/** Facts that must stay fixed for a transform-only main-scene refresh. */
inline constexpr RenderSourceDependencyMask
    kRenderSceneTransformRefreshStableDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::AssetRevision,
            RenderSourceDependency::ProjectMaterialRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::InstanceEditScope,
            RenderSourceDependency::SelectionRevision,
            RenderSourceDependency::VisibilityRevision,
            RenderSourceDependency::MeshBindingRevision,
            RenderSourceDependency::RenderComponentRevision,
        });

/** Scene-sync owner facts that define an unchanged document publication. */
inline constexpr RenderSourceDependencyMask
    kRenderSceneSyncDocumentDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::SceneRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::InstanceEditScope,
        });

/** Asset-owner facts inspected by the scene-sync narrow fallback. */
inline constexpr RenderSourceDependencyMask kRenderSceneSyncAssetDependencies =
    RenderSourceDependencyMask::from({
        RenderSourceDependency::SceneBinding,
        RenderSourceDependency::AssetRevision,
        RenderSourceDependency::ProjectMaterialRevision,
    });

/** Stable binding/edit context required by retained scene-sync records. */
inline constexpr RenderSourceDependencyMask
    kRenderSceneSyncContextDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::InstanceEditScope,
        });

/** Persistent selected-object overlay source dependencies. */
inline constexpr RenderSourceDependencyMask
    kRenderOverlaySelectedObjectSourceDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::MeshMaterialUsageRevision,
            RenderSourceDependency::SelectionRevision,
            RenderSourceDependency::VisibilityRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::MeshBindingRevision,
        });

/** Persistent scene-wireframe geometry dependencies. */
inline constexpr RenderSourceDependencyMask
    kRenderOverlaySceneWireframeSourceDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::MeshMaterialUsageRevision,
            RenderSourceDependency::SelectionRevision,
            RenderSourceDependency::HierarchyPresentationRevision,
            RenderSourceDependency::VisibilityRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::TransformRevision,
            RenderSourceDependency::MeshBindingRevision,
            RenderSourceDependency::LayerRevision,
        });

/** Exact scalar gate for a no-work scene-wireframe GPU synchronization. */
inline constexpr RenderSourceDependencyMask
    kRenderOverlaySceneWireframeCacheNoWorkDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::SceneRevision,
            RenderSourceDependency::AssetRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::InstanceEditScope,
        });

/** Exact scalar gate for a no-work selected-instance GPU synchronization. */
inline constexpr RenderSourceDependencyMask
    kRenderOverlayInstanceSelectionCacheNoWorkDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::MeshMaterialUsageRevision,
            RenderSourceDependency::SelectionRevision,
            RenderSourceDependency::VisibilityRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::TransformRevision,
            RenderSourceDependency::MeshBindingRevision,
        });

/** Collision sources deliberately exclude camera and selection revisions. */
inline constexpr RenderSourceDependencyMask
    kRenderOverlayCollisionSourceDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::MeshMaterialUsageRevision,
            RenderSourceDependency::VisibilityRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::TransformRevision,
            RenderSourceDependency::MeshBindingRevision,
            RenderSourceDependency::RenderComponentRevision,
            RenderSourceDependency::HierarchyPresentationRevision,
            RenderSourceDependency::LayerRevision,
        });

/** Full transient selection-overlay unchanged-frame dependencies. */
inline constexpr RenderSourceDependencyMask
    kRenderOverlaySelectionSourceDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::MeshMaterialUsageRevision,
            RenderSourceDependency::SelectionRevision,
            RenderSourceDependency::HierarchyPresentationRevision,
            RenderSourceDependency::VisibilityRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::TransformRevision,
            RenderSourceDependency::MeshBindingRevision,
            RenderSourceDependency::CurveBindingRevision,
            RenderSourceDependency::RenderComponentRevision,
            RenderSourceDependency::LayerRevision,
        });

/** Facts that remain fixed during a selection-overlay transform refresh. */
inline constexpr RenderSourceDependencyMask
    kRenderOverlaySelectionTransformStableDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::AssetRevision,
            RenderSourceDependency::SelectionRevision,
            RenderSourceDependency::HierarchyPresentationRevision,
            RenderSourceDependency::VisibilityRevision,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::MeshBindingRevision,
            RenderSourceDependency::CurveBindingRevision,
            RenderSourceDependency::RenderComponentRevision,
            RenderSourceDependency::LayerRevision,
        });

/** Facts that remain fixed during a selection-overlay curve refresh. */
inline constexpr RenderSourceDependencyMask
    kRenderOverlaySelectionCurveStableDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneBinding,
            RenderSourceDependency::LocalVisibilityRevision,
            RenderSourceDependency::MeshMaterialUsageRevision,
            RenderSourceDependency::SelectionRevision,
            RenderSourceDependency::HierarchyPresentationRevision,
            RenderSourceDependency::VisibilityRevision,
            RenderSourceDependency::TransformRevision,
            RenderSourceDependency::MeshBindingRevision,
            RenderSourceDependency::LayerRevision,
        });

inline constexpr RenderSourceDependencyMask
    kRenderOverlaySelectionCurveAdvanceDependencies =
        RenderSourceDependencyMask::from({
            RenderSourceDependency::SceneRevision,
            RenderSourceDependency::CurveBindingRevision,
            RenderSourceDependency::RenderComponentRevision,
        });

} // namespace quader
