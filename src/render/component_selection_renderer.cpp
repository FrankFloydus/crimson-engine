////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/component_selection_renderer.hpp"

#include "diagnostics/profile.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"

#include <backend/PixelBufferDescriptor.h>
#include <filament/Box.h>
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/RenderTarget.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace quader {
namespace {

using SelectionReadbackClock = std::chrono::steady_clock;

/// Keeps driver callbacks independent of render, widget and session lifetimes.
class SelectionReadbackSource final : public ViewportSelectionDepthSource {
public:
  std::shared_ptr<const ViewportSelectionDepthSnapshot> latest() const override {
    if (!active.load(std::memory_order_acquire)) return {};
    return completed.load(std::memory_order_acquire);
  }
  std::atomic<std::shared_ptr<const ViewportSelectionDepthSnapshot>> completed;
  std::atomic<bool> active = true;
  std::atomic<std::uint64_t> submission_count = 0u;
  std::atomic<std::uint64_t> completed_readbacks = 0u;
  std::atomic<std::uint64_t> readback_bytes = 0u;
  std::atomic<std::int64_t> readback_latency_ns = 0;
};

/// Pins all four destination allocations until their independent callbacks finish.
struct PendingSelectionReadback final {
  ViewportSelectionDepthSnapshot snapshot;
  SelectionReadbackClock::time_point submitted;
  std::atomic<std::size_t> completed_layers = 0u;
};

/// Carries one descriptor's ownership through asynchronous backend completion.
struct SelectionReadbackCallback final {
  std::shared_ptr<PendingSelectionReadback> pending;
  std::shared_ptr<SelectionReadbackSource> source;
};

void finish_selection_readback(void *, std::size_t, void *user) {
  const std::unique_ptr<SelectionReadbackCallback> callback(
      static_cast<SelectionReadbackCallback *>(user));
  if (callback->pending->completed_layers.fetch_add(1u, std::memory_order_acq_rel) + 1u != kViewportSelectionDepthLayers) return;
  if (!callback->source->active.load(std::memory_order_acquire)) return;
  callback->source->readback_latency_ns.store(
      std::chrono::duration_cast<std::chrono::nanoseconds>(SelectionReadbackClock::now() - callback->pending->submitted).count(),
      std::memory_order_relaxed);
  callback->source->completed_readbacks.fetch_add(1u, std::memory_order_relaxed);
  callback->source->completed.store(
      std::shared_ptr<const ViewportSelectionDepthSnapshot>(callback->pending, &callback->pending->snapshot),
      std::memory_order_release);
}

/// Shares one camera-independent quad stream across all conservative passes.
struct SelectionTriangleVertex final {
  QVec3 a;
  QVec3 b;
  QVec3 c;
  QVec3 control;
};

float maximum_absolute(QVec3 value) {
  return std::max({std::abs(value.x), std::abs(value.y), std::abs(value.z)});
}

filament::math::float3 filament_vector(QVec3 value) {
  return {value.x, value.y, value.z};
}

} // namespace

/// Retains bounded targets and publishes only completed immutable certificates.
class ComponentSelectionRenderer::Impl final {
public:
  /// Destroys staged and published consumers before their shared buffer owners.
  struct Geometry final {
    filament::Engine *engine = nullptr;
    filament::Scene *scene = nullptr;
    filament::VertexBuffer *vertices = nullptr;
    filament::IndexBuffer *indices = nullptr;
    std::array<utils::Entity, kViewportSelectionDepthLayers> entities{};
    std::shared_ptr<const ViewportSelectionDepthGeometry> source;

    ~Geometry() {
      if (!engine) return;
      for (const auto entity : entities) {
        if (entity && scene) scene->remove(entity);
        if (entity) engine->getRenderableManager().destroy(entity);
      }
      for (const auto entity : entities) if (entity) utils::EntityManager::get().destroy(entity);
      if (vertices) engine->destroy(vertices);
      if (indices) engine->destroy(indices);
    }
  };

  /// Owns one peeled color/depth attachment without borrowing readback memory.
  struct Target final {
    filament::View *view = nullptr;
    filament::MaterialInstance *material = nullptr;
    filament::Texture *color = nullptr;
    filament::Texture *depth = nullptr;
    filament::RenderTarget *target = nullptr;
  };

  filament::Engine *engine = nullptr;
  filament::Scene *scene = nullptr;
  filament::Camera *camera = nullptr;
  filament::Texture *fallback = nullptr;
  utils::Entity camera_entity;
  std::array<Target, kViewportSelectionDepthLayers> targets;
  std::unique_ptr<Geometry> geometry;
  std::shared_ptr<SelectionReadbackSource> readbacks = std::make_shared<SelectionReadbackSource>();
  std::shared_ptr<PendingSelectionReadback> pending;
  ViewportSelectionDepthCamera observed_camera;
  ViewportSelectionDepthCamera prepared_camera;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  unsigned int settled_frames = 0u;
  double camera_depth_error = 0.0;
  bool coverage_complete = false;
  bool ready = false;

  void destroy_targets() {
    if (!engine) return;
    for (auto &target : targets) {
      if (target.view) target.view->setRenderTarget(nullptr);
      if (target.target) engine->destroy(target.target);
      if (target.color) engine->destroy(target.color);
      if (target.depth) engine->destroy(target.depth);
      target.target = nullptr;
      target.color = nullptr;
      target.depth = nullptr;
    }
    width = height = 0u;
  }

  void shutdown() {
    readbacks->active.store(false, std::memory_order_release);
    readbacks->completed.store({}, std::memory_order_release);
    ready = false;
    pending.reset();
    geometry.reset();
    if (!engine) return;
    destroy_targets();
    for (auto &target : targets) {
      if (target.view) engine->destroy(target.view);
      if (target.material) engine->destroy(target.material);
      target = {};
    }
    if (camera_entity) {
      engine->destroyCameraComponent(camera_entity);
      utils::EntityManager::get().destroy(camera_entity);
    }
    if (scene) engine->destroy(scene);
    camera_entity.clear();
    engine = nullptr;
    scene = nullptr;
    camera = nullptr;
    fallback = nullptr;
    settled_frames = 0u;
  }

  bool resize(std::uint32_t requested_width, std::uint32_t requested_height) {
    if (width == requested_width && height == requested_height && targets[0].target) return true;
    destroy_targets();
    using Texture = filament::Texture;
    for (auto &target : targets) {
      target.color = Texture::Builder().width(requested_width).height(requested_height).levels(1u)
          .format(Texture::InternalFormat::RGBA32F)
          .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE | Texture::Usage::BLIT_SRC).build(*engine);
      target.depth = Texture::Builder().width(requested_width).height(requested_height).levels(1u)
          .format(Texture::InternalFormat::DEPTH32F).usage(Texture::Usage::DEPTH_ATTACHMENT).build(*engine);
      if (!target.color || !target.depth) { destroy_targets(); return false; }
      target.target = filament::RenderTarget::Builder()
          .texture(filament::RenderTarget::AttachmentPoint::COLOR, target.color)
          .texture(filament::RenderTarget::AttachmentPoint::DEPTH, target.depth).build(*engine);
      if (!target.target) { destroy_targets(); return false; }
      target.view->setRenderTarget(target.target);
      target.view->setViewport({0, 0, requested_width, requested_height});
    }
    const filament::TextureSampler sampler(filament::TextureSampler::MinFilter::NEAREST,
        filament::TextureSampler::MagFilter::NEAREST, filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);
    for (std::size_t layer = 0u; layer < targets.size(); ++layer) {
      for (std::size_t previous = 0u; previous < 3u; ++previous) {
        static constexpr std::array<const char *, 3u> kPreviousNames{"previous0", "previous1", "previous2"};
        targets[layer].material->setParameter(kPreviousNames[previous], previous < layer ? targets[previous].color : fallback, sampler);
      }
    }
    width = requested_width;
    height = requested_height;
    return true;
  }

  bool sync_geometry(std::shared_ptr<const ViewportSelectionDepthGeometry> source) {
    if (geometry && geometry->source == source) return true;
    if (!source || source->triangles.empty() || source->triangles.size() >= (1u << 24u)) return false;
    QDR_PROFILE_SCOPE("Selection GPU prepare triangle buffers");
    auto next = std::make_unique<Geometry>();
    next->engine = engine;
    next->scene = scene;
    next->source = std::move(source);
    std::vector<SelectionTriangleVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(next->source->triangles.size() * 4u);
    indices.reserve(next->source->triangles.size() * 6u);
    constexpr std::array<QVec2, 4u> kCorners{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
    constexpr std::array<std::uint32_t, 6u> kIndices{0u, 1u, 2u, 2u, 1u, 3u};
    for (std::size_t index = 0u; index < next->source->triangles.size(); ++index) {
      const auto &triangle = next->source->triangles[index];
      for (const auto corner : kCorners) vertices.push_back({triangle.a, triangle.b, triangle.c,
          {corner.x, corner.y, static_cast<float>(index + 1u)}});
      for (const auto local : kIndices) indices.push_back(static_cast<std::uint32_t>(index * 4u) + local);
    }
    using Attribute = filament::VertexAttribute;
    using Element = filament::backend::ElementType;
    next->vertices = filament::VertexBuffer::Builder().vertexCount(static_cast<std::uint32_t>(vertices.size())).bufferCount(1u)
        .attribute(Attribute::POSITION, 0u, Element::FLOAT3, offsetof(SelectionTriangleVertex, a), sizeof(SelectionTriangleVertex))
        .attribute(Attribute::CUSTOM0, 0u, Element::FLOAT3, offsetof(SelectionTriangleVertex, b), sizeof(SelectionTriangleVertex))
        .attribute(Attribute::CUSTOM1, 0u, Element::FLOAT3, offsetof(SelectionTriangleVertex, c), sizeof(SelectionTriangleVertex))
        .attribute(Attribute::CUSTOM2, 0u, Element::FLOAT3, offsetof(SelectionTriangleVertex, control), sizeof(SelectionTriangleVertex)).build(*engine);
    next->indices = filament::IndexBuffer::Builder().indexCount(static_cast<std::uint32_t>(indices.size()))
        .bufferType(filament::IndexBuffer::IndexType::UINT).build(*engine);
    if (!next->vertices || !next->indices) return false;
    next->vertices->setBufferAt(*engine, 0u, FilamentBufferDescriptorFactory::vector(std::move(vertices)));
    next->indices->setBuffer(*engine, FilamentBufferDescriptorFactory::vector(std::move(indices)));
    for (std::size_t layer = 0u; layer < targets.size(); ++layer) {
      const auto entity = utils::EntityManager::get().create();
      next->entities[layer] = entity;
      if (filament::RenderableManager::Builder(1u)
              .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES, next->vertices, next->indices)
              .material(0u, targets[layer].material).boundingBox({{0, 0, 0}, {1, 1, 1}})
              .culling(false).castShadows(false).receiveShadows(false)
              .layerMask(0xffu, static_cast<std::uint8_t>(1u << layer)).build(*engine, entity) !=
          filament::RenderableManager::Builder::Success) return false;
    }
    geometry = std::move(next);
    for (const auto entity : geometry->entities) scene->addEntity(entity);
    readbacks->completed.store({}, std::memory_order_release);
    return true;
  }
};

ComponentSelectionRenderer::ComponentSelectionRenderer() : impl_(std::make_unique<Impl>()) {}
ComponentSelectionRenderer::~ComponentSelectionRenderer() { shutdown(); }

bool ComponentSelectionRenderer::initialize(filament::Engine &engine,
    filament::Material &material, filament::Texture &fallback) {
  shutdown();
  auto &state = *impl_;
  state.readbacks = std::make_shared<SelectionReadbackSource>();
  state.engine = &engine;
  state.fallback = &fallback;
  state.scene = engine.createScene();
  state.camera_entity = utils::EntityManager::get().create();
  state.camera = engine.createCamera(state.camera_entity);
  if (!state.scene || !state.camera) { shutdown(); return false; }
  for (std::size_t layer = 0u; layer < state.targets.size(); ++layer) {
    auto &target = state.targets[layer];
    target.material = material.createInstance();
    target.view = engine.createView();
    if (!target.material || !target.view) { shutdown(); return false; }
    target.material->setDepthFunc(filament::MaterialInstance::DepthFunc::GE);
    target.material->setCullingMode(filament::MaterialInstance::CullingMode::NONE);
    target.material->setParameter("layerIndex", static_cast<int>(layer));
    target.view->setName("Conservative component selection IDs");
    target.view->setScene(state.scene);
    target.view->setCamera(state.camera);
    target.view->setVisibleLayers(0xffu, static_cast<std::uint8_t>(1u << layer));
    target.view->setPostProcessingEnabled(false);
    target.view->setAntiAliasing(filament::AntiAliasing::NONE);
    target.view->setDithering(filament::Dithering::NONE);
    target.view->setMultiSampleAntiAliasingOptions({.enabled = false});
    target.view->setTemporalAntiAliasingOptions({.enabled = false});
    target.view->setDynamicResolutionOptions({.enabled = false});
    target.view->setShadowingEnabled(false);
    target.view->setFogOptions({.enabled = false});
    target.view->setBlendMode(filament::View::BlendMode::OPAQUE);
  }
  return true;
}

void ComponentSelectionRenderer::shutdown() { impl_->shutdown(); }

bool ComponentSelectionRenderer::wants_geometry(const ViewportSelectionDepthCamera &camera, bool eligible) {
  auto &state = *impl_;
  if (!state.engine || !eligible || !camera.supported) {
    state.settled_frames = 0u;
    state.ready = false;
    return false;
  }
  if (!(state.observed_camera == camera)) {
    state.observed_camera = camera;
    state.settled_frames = 0u;
    state.ready = false;
  }
  state.settled_frames = std::min(state.settled_frames + 1u, 3u);
  return state.settled_frames >= 2u;
}

void ComponentSelectionRenderer::prepare(std::shared_ptr<const ViewportSelectionDepthGeometry> geometry,
                                        const ViewportSelectionDepthCamera &camera) {
  auto &state = *impl_;
  if (!state.engine || !geometry || !camera.supported || !(camera.viewport_size.x > 4.0f) || !(camera.viewport_size.y > 4.0f)) return;
  if (const auto completed = state.readbacks->latest(); completed && completed->matches(geometry.get(), camera)) return;
  if (state.pending && state.pending->completed_layers.load(std::memory_order_acquire) != kViewportSelectionDepthLayers) return;
  if (state.ready && state.geometry && state.geometry->source == geometry && state.prepared_camera == camera) return;
  QDR_PROFILE_SCOPE("Selection GPU prepare settled camera");
  const double scale = std::min(1.0, std::sqrt(1048576.0 /
      (static_cast<double>(camera.viewport_size.x) * camera.viewport_size.y)));
  const auto width = static_cast<std::uint32_t>(std::max(5.0, std::floor(camera.viewport_size.x * scale)));
  const auto height = static_cast<std::uint32_t>(std::max(5.0, std::floor(camera.viewport_size.y * scale)));
  if (!state.resize(width, height) || !state.sync_geometry(std::move(geometry))) return;
  const float vertical = camera.orthographic ? 2.0f / std::max(0.0001f, camera.orthographic_size)
      : 1.0f / std::tan(std::clamp(camera.vertical_fov_degrees, 1.0f, 179.0f) * std::numbers::pi_v<float> / 360.0f);
  const float horizontal = vertical * camera.viewport_size.y / camera.viewport_size.x;
  double magnitude = std::max(static_cast<double>(maximum_absolute(camera.origin)),
                              state.geometry->source->transform_arithmetic_magnitude);
  float minimum_positive_depth = std::numeric_limits<float>::infinity();
  float maximum_discarded_depth = -std::numeric_limits<float>::infinity();
  double maximum_projection_ratio = 0.0;
  bool complete = state.geometry->source->conservative_transforms;
  for (const auto &triangle : state.geometry->source->triangles) {
    const std::array points{triangle.a, triangle.b, triangle.c};
    bool front = false;
    bool behind = false;
    float triangle_maximum_depth = -std::numeric_limits<float>::infinity();
    for (const auto point : points) {
      magnitude = std::max(magnitude, static_cast<double>(maximum_absolute(point)));
      const auto relative = point - camera.origin;
      const float depth = dot(relative, camera.forward);
      if (!std::isfinite(depth)) complete = false;
      triangle_maximum_depth = std::max(triangle_maximum_depth, depth);
      front = front || depth > 0.000001f;
      behind = behind || !(depth > 0.000001f);
      if (depth > 0.000001f) {
        minimum_positive_depth = std::min(minimum_positive_depth, depth);
        const float divisor = camera.orthographic ? 1.0f : depth;
        if (!camera.orthographic) {
          maximum_projection_ratio = std::max({maximum_projection_ratio,
              std::abs(static_cast<double>(dot(relative, camera.right)) / depth),
              std::abs(static_cast<double>(dot(relative, camera.up)) / depth)});
        }
        const double projected_x = static_cast<double>(dot(relative, camera.right) / divisor * horizontal) * width;
        const double projected_y = static_cast<double>(dot(relative, camera.up) / divisor * vertical) * height;
        if (!std::isfinite(projected_x) || !std::isfinite(projected_y) ||
            std::abs(projected_x) > 32768.0 || std::abs(projected_y) > 32768.0) complete = false;
      }
    }
    if (front && behind) complete = false;
    if (!front) maximum_discarded_depth = std::max(maximum_discarded_depth, triangle_maximum_depth);
  }
  state.camera_depth_error = std::max(0.000001, static_cast<double>(magnitude) * std::numeric_limits<float>::epsilon() * 128.0);
  // Selection rays start at the camera, and the CPU triangle kernel tolerates
  // a tiny negative distance. Shader-discarded near-plane triangles therefore
  // cannot certify empty space, even when all three vertices are discarded.
  if (maximum_discarded_depth >= -(state.camera_depth_error + 0.000001)) complete = false;
  const double projected_error = state.camera_depth_error * std::max(horizontal * width, vertical * height) /
      (camera.orthographic ? 1.0 : std::max(0.000001f, minimum_positive_depth)) *
      (1.0 + maximum_projection_ratio);
  state.coverage_complete = complete && std::isfinite(projected_error) && projected_error < 0.25;
  for (auto &target : state.targets) {
    target.material->setParameter("cameraOrigin", filament_vector(camera.origin));
    target.material->setParameter("cameraRight", filament_vector(camera.right));
    target.material->setParameter("cameraUp", filament_vector(camera.up));
    target.material->setParameter("cameraForward", filament_vector(camera.forward));
    target.material->setParameter("projectionScale", filament::math::float2{horizontal, vertical});
    target.material->setParameter("orthographicProjection", camera.orthographic ? 1.0f : 0.0f);
    target.material->setParameter("depthError", static_cast<float>(state.camera_depth_error));
  }
  state.prepared_camera = camera;
  state.ready = true;
}

void ComponentSelectionRenderer::render(filament::Renderer &renderer) {
  auto &state = *impl_;
  if (!state.ready || !state.geometry || !state.targets[0].target) return;
  QDR_PROFILE_SCOPE("Selection GPU render conservative layers");
  auto pending = std::make_shared<PendingSelectionReadback>();
  pending->snapshot.geometry = state.geometry->source;
  pending->snapshot.camera = state.prepared_camera;
  pending->snapshot.width = state.width;
  pending->snapshot.height = state.height;
  pending->snapshot.camera_depth_error = state.camera_depth_error;
  pending->snapshot.conservative_coverage = state.coverage_complete;
  pending->snapshot.readback_top_left = state.engine->getBackend() == filament::Engine::Backend::VULKAN;
  // Completion also runs on backend failures and NOOP. Only transferred clear
  // pixels may certify exhaustion; untouched storage must remain unavailable.
  const float invalid = std::numeric_limits<float>::quiet_NaN();
  for (auto &layer : pending->snapshot.layers) {
    layer.resize(static_cast<std::size_t>(state.width) * state.height, {invalid, invalid, invalid, invalid});
  }
  pending->submitted = SelectionReadbackClock::now();
  const auto clear = renderer.getClearOptions();
  renderer.setClearOptions({.clearColor = {0, 0, 0, 0}, .clear = true, .discard = false});
  for (std::size_t layer = 0u; layer < state.targets.size(); ++layer) {
    renderer.render(state.targets[layer].view);
    auto &pixels = pending->snapshot.layers[layer];
    auto *callback = new SelectionReadbackCallback{pending, state.readbacks};
    renderer.readPixels(state.targets[layer].target, 0u, 0u, state.width, state.height,
        filament::backend::PixelBufferDescriptor(pixels.data(), pixels.size() * sizeof(pixels.front()),
            filament::backend::PixelDataFormat::RGBA, filament::backend::PixelDataType::FLOAT,
            finish_selection_readback, callback));
  }
  renderer.setClearOptions(clear);
  state.readbacks->submission_count.fetch_add(1u, std::memory_order_relaxed);
  const auto bytes = static_cast<std::uint64_t>(state.width) * state.height * 16u * kViewportSelectionDepthLayers;
  state.readbacks->readback_bytes.fetch_add(bytes, std::memory_order_relaxed);
  QDR_PROFILE_PLOT("Quader selection GPU readback bytes", static_cast<std::int64_t>(bytes));
  state.pending = std::move(pending);
  state.ready = false;
}

std::shared_ptr<const ViewportSelectionDepthSource> ComponentSelectionRenderer::source() const { return impl_->readbacks; }

ComponentSelectionRendererStats ComponentSelectionRenderer::stats() const {
  const auto &source = *impl_->readbacks;
  return {.submission_count = source.submission_count.load(std::memory_order_relaxed),
          .completed_readbacks = source.completed_readbacks.load(std::memory_order_relaxed),
          .readback_bytes = source.readback_bytes.load(std::memory_order_relaxed),
          .readback_latency_ms = static_cast<double>(source.readback_latency_ns.load(std::memory_order_relaxed)) / 1000000.0};
}

} // namespace quader
