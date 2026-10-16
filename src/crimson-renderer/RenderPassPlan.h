#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace crimson {

// Logical identity survives target resizing and is independent of bgfx's
// physical execution slots. instanceId distinguishes isolated thumbnail jobs.
enum class RenderViewPurpose : std::uint8_t { Scene, Game, MaterialPreview, ParticlePreview, Thumbnail };
struct RenderViewIdentity {
    RenderViewPurpose purpose{RenderViewPurpose::Scene};
    std::uint64_t instanceId{};
    bool operator==(const RenderViewIdentity&) const = default;
};

inline constexpr std::uint16_t InvalidRenderView = UINT16_MAX;
struct RenderViewPassSlots {
    std::uint16_t scene{},present{},grid{},encode{},glowExtract{},glowHorizontal{},glowVertical{};
    std::uint16_t pointShadow{},directionalShadow{};
    std::uint16_t depthPrepass{InvalidRenderView},ambient{InvalidRenderView},characterShadow{InvalidRenderView};
    std::uint16_t collisions{InvalidRenderView};
};
// The current backend requires identity view order. This is the one table of
// existing slots; the plan does not introduce remapping or allocate view IDs.
RenderViewPassSlots renderViewPassSlots(RenderViewPurpose purpose) noexcept;

// Only implemented work appears here. Ambient work is optional; the disabled
// profile retains the existing single-pass color/depth behavior.
enum class RenderPassKind : std::uint8_t {
    EnvironmentPreparation, ModelDeformation, LightAssignment, PointShadowUpdates, DirectionalShadowUpdates, CharacterShadowUpdates,
    DepthPrepass, AmbientVisibility, AmbientPublish,
    SceneColor, GlowExtract, GlowHorizontal, GlowVertical, ToneMap, Grid, CollisionOverlays, Encode,
    Picking, PickReadback, ThumbnailReadback, Count
};
enum class RenderResource : std::uint8_t {
    PreparedScene, EnvironmentLighting, ShadowAtlas, ClusteredLighting, SceneHdr,
    SceneDepth, GlowA, GlowB, Composite, Display, PickColor, PickDepth,
    PickReadback, ThumbnailReadback, ReadableSceneDepth, ReceiverScopes,
    AmbientScratch, AmbientVisibility, DeformedGeometry, Count
};
enum class RenderResourceOwner : std::uint8_t {
    PreparedFrame, Environment, ModelGeometry, ActiveWorldShadows, ThumbnailWorldShadows, ViewTargets, Readback
};
// SceneDepth names attachment use. ReadableSceneDepth explicitly identifies
// shader reads after the depth attachment has been detached. AmbientVisibility
// is a logical published region of ClusteredLighting's packed atlas, not an
// extra sampled texture. Eval writes scratch before Publish reads/writes atlas.
enum class RenderResourceAccess : std::uint8_t { Read, Write, ReadWrite, DepthRead, DepthWrite };
struct RenderResourceUse {
    RenderResource resource{};
    RenderResourceAccess access{};
    bool operator==(const RenderResourceUse&) const = default;
};
struct RenderResourceBinding {
    RenderResource resource{};
    RenderResourceOwner owner{};
    RenderViewIdentity view{}; // Meaningful for ViewTargets and Readback owners.
    bool initiallyAvailable{}; // Imported coherent data, not merely allocated storage.
};

struct RenderPassPlanConfig {
    RenderViewIdentity view{};
    bool glow{},grid{},picking{},thumbnailReadback{},worldShadows{},prepareEnvironment{};
    bool ambientVisibility{};
    bool collisionOverlays{};
};
struct RenderPass {
    static constexpr std::size_t MaximumResourceUses = 8;
    RenderPassKind kind{};
    std::uint16_t physicalView{};
    std::uint16_t viewCount{1}; // Shadow updates reserve their existing face/cascade range.
    std::array<RenderResourceUse,MaximumResourceUses> resources{};
    std::uint8_t resourceCount{};
    std::span<const RenderResourceUse> uses() const noexcept { return {resources.data(),resourceCount}; }
    std::uint16_t lastPhysicalView() const noexcept { return static_cast<std::uint16_t>(physicalView+viewCount-1); }
};
struct RenderPassPlan {
    static constexpr std::size_t MaximumPasses = 19;
    static constexpr std::size_t ResourceCount = static_cast<std::size_t>(RenderResource::Count);
    RenderPassPlanConfig config{};
    std::array<RenderResourceBinding,ResourceCount> bindings{};
    std::array<RenderPass,MaximumPasses> entries{};
    std::uint8_t count{};
    std::span<const RenderPass> passes() const noexcept { return {entries.data(),count}; }
    const RenderPass* find(RenderPassKind kind) const noexcept;
    bool has(RenderPassKind kind) const noexcept { return find(kind)!=nullptr; }
};

enum class RenderPassPlanError : std::uint8_t {
    None, InvalidViewPurpose, UnsupportedFeature, CapacityExceeded, InvalidResource,
    InvalidOwner, InvalidExternalState, InvalidPass, InvalidPhysicalView, PhysicalOrder,
    DuplicatePass, InvalidAccess, DuplicateResourceUse, MissingProducer, MissingPass, UnexpectedPass
};
struct RenderPassPlanValidation {
    RenderPassPlanError error{RenderPassPlanError::None};
    std::uint8_t passIndex{};
    RenderResource resource{RenderResource::Count};
    explicit operator bool() const noexcept { return error==RenderPassPlanError::None; }
};

// Fixed-size, allocation-free plan construction/validation. Optional features
// describe effective work (e.g. Glow only after scratch allocation succeeds).
RenderPassPlan buildRenderPassPlan(const RenderPassPlanConfig& config) noexcept;
RenderPassPlanValidation validateRenderPassPlan(const RenderPassPlan& plan) noexcept;
const char* renderPassPlanErrorText(RenderPassPlanError error) noexcept;

} // namespace crimson
