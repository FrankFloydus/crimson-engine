////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#include "ui/qt_crimson_instancing_probe.hpp"
#include "render/crimson/crimson_render_host.hpp"
#include "render/render_layers.hpp"
#include "ui/qt_vulkan_composition_widget.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"

#include <crimson/renderer/BuildIdentity.hpp>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace quader {
namespace {

/** Uses the public application's actual host, scene projection and Qt copy path. */
class InstancingCompositionProbe final {
public:
  InstancingCompositionProbe(QApplication &application, QString output_directory,
                            std::filesystem::path resources)
      : application_(application), output_directory_(std::move(output_directory)),
        resources_(std::move(resources)), host_(create_app_render_host()) {
    timer_.setInterval(1);
    QObject::connect(&timer_, &QTimer::timeout, &application_, [this] { advance(); });
  }

  ~InstancingCompositionProbe() { retire(); }

  int run() {
    if (!host_ || !QDir().mkpath(output_directory_)) return 1;
    evidence_.open(std::filesystem::path(output_directory_.toStdWString()) / "instancing.csv");
    if (!evidence_) return 1;
    evidence_ << "phase,scene_meshes,scene_instances,position_uploads,attribute_uploads,index_uploads,image_uploads,"
                 "instance_buffer_allocations,instance_record_uploads,instance_upload_bytes,"
                 "instanced_draws,instanced_instances,fallback_draws,culled_instances,"
                 "frame_submissions,bgfx_draws,identity_hash,preparation_ms,phase_ms,lit_pixels,benchmark_frames,benchmark_ms,"
                 "measured_render_epochs,cpu_samples,cpu_host_mean_ms,cpu_host_max_ms\n";
    evidence_.flush();
    std::ofstream metadata(std::filesystem::path(output_directory_.toStdWString()) / "conditions.txt");
    metadata << "consumer=quader-0.1.exe\nrenderer=" << crimson::shared::kRendererSourceCommit
        << "\nrenderer_dirty=" << crimson::shared::kRendererWorkingTreeDirty
        << "\nbackend=borrowed Qt Vulkan\nmode=modeling shaded\nprojection=orthographic\n"
           "target=800x600\npopulation=100000\nlayout=100x100x10 cubes\n"
           "geometry=one 24-vertex 36-index cube\nbenchmark=120 presented camera frames (119 intervals), unchanged instance data\n"
           "baseline=10000 cubes per-object versus same 10000 cubes instanced; no extrapolated 100000-cube ratio\n"
           "timing=wall presentation interval including Qt scheduling and compositor; not active GPU execution\n"
           "cpu_timing=host preparation plus rendering/copy submission; first presented frame excluded from mean\n";
    metadata.close();
    progress("starting Qt composition");
    settings_ = ViewportSettings::defaults();
    (void)settings_.set_color(ViewportColorSettingId::Background, {0, 0, 0, 1});
    settings_.set_bool_value(ViewportBoolSettingId::ShowGrid, false);
    settings_.set_bool_value(ViewportBoolSettingId::ShowGizmos, false);
    root_ = std::make_unique<QWidget>();
    root_->setWindowTitle(QStringLiteral("Quader 0.1 Crimson: 100000 shared cubes"));
    root_->resize(kWidth, kHeight);
    auto *layout = new QVBoxLayout(root_.get());
    layout->setContentsMargins(0, 0, 0, 0);
    widget_ = new QtVulkanCompositionWidget(root_.get());
    widget_->setFixedColorBufferSize({kWidth, kHeight});
    layout->addWidget(widget_);
    widget_->set_render_callback([this](const RenderVulkanCompositionTarget &target) {
      if (!initialized_) return false;
      const bool new_epoch = rendered_epoch_ != prepared_epoch_;
      std::uint64_t before_submissions = 0u;
      if (new_epoch && measured_phase() &&
          !crimson_render_frame_submissions(*host_, before_submissions)) {
        fail("Cannot observe production renderer submissions"); return false;
      }
      QElapsedTimer rendering;
      rendering.start();
      if (!host_->render_composited_viewport(*view_, *surface_, target)) {
        CrimsonRenderDiagnostics diagnostics;
        (void)crimson_render_diagnostics(*host_, diagnostics);
        fail(diagnostics.error.empty() ? "Host composition failed" : diagnostics.error);
        return false;
      }
      if (new_epoch) {
        render_cpu_ms_ = static_cast<double>(rendering.nsecsElapsed()) / 1000000.0;
        if (measured_phase()) {
          std::uint64_t after_submissions = 0u;
          if (!crimson_render_frame_submissions(*host_, after_submissions) ||
              after_submissions <= before_submissions) {
            fail("A measured prepared epoch copied pixels without rendering a new production frame"); return false;
          }
          ++measured_render_epochs_;
        }
      }
      // Recomposition still copies into Qt's current destination. Only the
      // first actual production render of a prepared epoch enters measurement.
      rendered_epoch_ = prepared_epoch_;
      return true;
    });
    widget_->set_release_callback([this] {
      if (host_->initialized()) qFatal("Instancing probe released Qt before renderer shutdown");
      ++release_count_;
    });
    QObject::connect(widget_, &QRhiWidget::frameSubmitted, widget_, [this] {
      if (initialized_) host_->complete_composited_presentation_frame();
      presented_epoch_ = rendered_epoch_;
      presented_clock_.restart();
    });
    root_->show();
    phase_clock_.start();
    timer_.start();
    return application_.exec();
  }

private:
  enum class Phase { Baseline, BaselineInstanced, Initial, Unchanged, SingleEdit,
                     Camera, Culled, Restored, Benchmark, DepthFront, DepthBack,
                     AffineInstanced, AffineFallback };
  static constexpr std::uint32_t kPopulation = 100000u;
  static constexpr int kWidth = 800;
  static constexpr int kHeight = 600;
  static constexpr std::array<const char *, 13> kPhaseNames{
      "baseline_10000_separate", "baseline_10000_instanced", "initial", "unchanged",
      "single_edit", "camera", "culled", "restored", "benchmark_100000", "depth_front", "depth_back",
      "affine_instanced", "affine_separate"};
  static constexpr std::uint32_t kBenchmarkFrames = 120u;

  bool measured_phase() const noexcept {
    return phase_ == Phase::Baseline || phase_ == Phase::BaselineInstanced || phase_ == Phase::Benchmark;
  }

  void fail(std::string message) {
    if (!error_.empty()) return;
    error_ = std::move(message);
    std::ofstream failure(std::filesystem::path(output_directory_.toStdWString()) / "failure.txt");
    failure << kPhaseNames[static_cast<std::size_t>(phase_)] << ": " << error_ << '\n';
    std::cerr << "FAIL " << kPhaseNames[static_cast<std::size_t>(phase_)] << ": " << error_ << std::endl;
  }

  void progress(const char *message) const {
    std::ofstream log(std::filesystem::path(output_directory_.toStdWString()) / "progress.txt", std::ios::app);
    log << kPhaseNames[static_cast<std::size_t>(phase_)] << ": " << message
        << "; requested=" << requested_epoch_ << "; prepared=" << prepared_epoch_
        << "; rendered=" << rendered_epoch_ << "; presented=" << presented_epoch_
        << "; benchmark_frames=" << benchmark_frames_ << '\n';
  }

  void create_population() {
    geometry_ = std::make_shared<MeshGeometryPayload>();
    constexpr std::array<QVec3, 8> kCorners{{
        {-0.45f,-0.45f,-0.45f}, {0.45f,-0.45f,-0.45f},
        {0.45f,0.45f,-0.45f}, {-0.45f,0.45f,-0.45f},
        {-0.45f,-0.45f,0.45f}, {0.45f,-0.45f,0.45f},
        {0.45f,0.45f,0.45f}, {-0.45f,0.45f,0.45f}}};
    constexpr std::array<std::array<std::uint32_t, 4>, 6> kFaces{{
        {4,5,6,7}, {1,0,3,2}, {5,1,2,6}, {0,4,7,3}, {7,6,2,3}, {0,1,5,4}}};
    constexpr std::array<QVec3, 6> kNormals{{
        {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}}};
    constexpr std::array<QVec2, 4> kUvs{{{0,0}, {1,0}, {1,1}, {0,1}}};
    for (std::size_t face = 0u; face < kFaces.size(); ++face) {
      const auto first = static_cast<std::uint32_t>(geometry_->vertices.size());
      for (std::size_t vertex = 0u; vertex < 4u; ++vertex)
        geometry_->vertices.push_back({kCorners[kFaces[face][vertex]], kNormals[face], kUvs[vertex]});
      for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) geometry_->indices.push_back(first + index);
    }
    geometry_->primitives = {{0u, 36u, 0u}};
    RenderSceneInstancePrimitiveRecord primitive;
    primitive.definition_entity = 10u;
    primitive.member_entity = 11u;
    primitive.mesh.mesh_id = 27u;
    primitive.mesh.mesh_revision = 1u;
    primitive.mesh.geometry_payload = geometry_;
    primitive.mesh.vertices = geometry_->vertices;
    primitive.mesh.indices = geometry_->indices;
    primitive.mesh.primitives = geometry_->primitives;
    primitive.mesh.present = true;
    primitive.primitive = geometry_->primitives.front();
    primitive.material.present = true;
    primitive.material.revision = 1u;
    primitive.material.primary_material.present = true;
    primitive.material.primary_material.material_id = 83u;
    primitive.material.primary_material.revision = 1u;
    primitive.material.primary_material.document.base_color = {0.65f, 0.35f, 0.1f};
    primitive.occurrences.reserve(kPopulation);
    for (std::uint32_t index = 0u; index < kPopulation; ++index) {
      RenderSceneInstanceOccurrenceRecord occurrence;
      occurrence.entity_id = 100u + index;
      occurrence.occurrence = index + 1u;
      occurrence.world_transform.values[12] = (static_cast<float>(index % 100u) - 49.5f) * 1.4f;
      occurrence.world_transform.values[13] = (static_cast<float>((index / 100u) % 100u) - 49.5f) * 1.4f;
      occurrence.world_transform.values[14] = -static_cast<float>(index / 10000u) * 1.4f;
      primitive.occurrences.push_back(occurrence);
    }
    snapshot_.transform_revision = 1u;
    snapshot_.texture_mapping_revision = 1u;
    snapshot_.instance_primitives.push_back(std::move(primitive));
  }

  bool initialize() {
    const auto *context = widget_->shared_context();
    if (!context) return false;
    if (!host_->initialize(nullptr, resources_, {.backend = RendererBackend::Vulkan}, context)) {
      fail("Public host failed to borrow Qt Vulkan context"); return false;
    }
    if (!crimson_set_gpu_instancing_enabled(*host_, false)) {
      fail("Public host did not accept its diagnostic baseline control"); return false;
    }
    sink_ = host_->create_scene_sink(resources_);
    view_ = host_->create_view(viewport_grid_layer_mask(0u), ViewportRenderSurface::SrgbSwapchain);
    surface_ = host_->create_composited_surface(kWidth, kHeight);
    if (!sink_ || !view_ || !surface_ || !surface_->valid()) {
      fail("Public host did not create its sink/view/surface"); return false;
    }
    sink_->set_active_overlay_viewports(1u);
    view_->set_active(true);
    view_->resize(kWidth, kHeight);
    view_->set_presentation_bounds(kWidth, kHeight, 0u, 0u);
    view_->set_overlay_visible(false);
    view_->set_scene_visible(true);
    view_->set_scene_layer_mask(kViewportShadedSceneLayerMask);
    view_->set_shadowing_enabled(false);
    view_->set_material_presentation(ViewportMaterialDebugMode::None, false, false);
    host_->bind_view_surface(*view_, *surface_);
    widget_->set_render_system(host_.get());
    camera_.set_viewport_size(kWidth, kHeight);
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.focus_bounds({-74,-74,-16}, {74,74,1}, 1.1f);
    camera_.set_clip_planes(0.01f, 1000.0f);
    create_population();
    initialized_ = true;
    progress("host initialized");
    return true;
  }

  void prepare() {
    if (benchmark_frames_ == 0u) progress("preparing phase");
    QElapsedTimer preparation;
    preparation.start();
    host_->begin_shared_command_batch();
    host_->begin_view_preparation();
    if (phase_ == Phase::Baseline) {
      if (benchmark_frames_ == 0u) {
        auto baseline = snapshot_;
        baseline.instance_primitives.front().occurrences.resize(10000u);
        sink_->sync(std::move(baseline));
      }
    } else if (phase_ == Phase::Initial || phase_ == Phase::SingleEdit ||
               phase_ == Phase::DepthFront || phase_ == Phase::DepthBack ||
               phase_ == Phase::AffineInstanced) sink_->sync(snapshot_);
    view_->update_camera(camera_);
    view_->apply_scene_settings({}, false, settings_);
    host_->complete_view_preparation();
    host_->begin_presentation_frame();
    host_->end_shared_command_batch();
    preparation_ms_ = static_cast<double>(preparation.nsecsElapsed()) / 1000000.0;
    prepared_epoch_ = requested_epoch_;
    needs_prepare_ = false;
    widget_->update();
  }

  bool capture() {
    const auto image = widget_->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
    if (image.size() != QSize(kWidth, kHeight)) { fail("Missing actual GPU framebuffer"); return false; }
    const auto name = QString::fromLatin1(kPhaseNames[static_cast<std::size_t>(phase_)]);
    if (!image.save(QDir(output_directory_).filePath(name + QStringLiteral(".png")))) {
      fail("Cannot save GPU evidence"); return false;
    }
    CrimsonRenderDiagnostics current;
    if (!crimson_render_diagnostics(*host_, current) || !current.error.empty()) {
      fail(current.error.empty() ? "Missing public host diagnostics" : current.error); return false;
    }
    const auto &gpu = current.scene_gpu;
    const auto &previous = retained_.scene_gpu;
    std::uint64_t lit_pixels = 0u;
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x) lit_pixels += image.pixelColor(x,y).red() > 10;
    const bool geometry_retained = gpu.position_uploads == previous.position_uploads &&
        gpu.attribute_uploads == previous.attribute_uploads && gpu.index_uploads == previous.index_uploads;
    const bool instances_retained = gpu.instance_record_uploads == previous.instance_record_uploads &&
        gpu.instance_upload_bytes == previous.instance_upload_bytes;
    const bool baseline = phase_ == Phase::Baseline || phase_ == Phase::BaselineInstanced;
    const bool affine = phase_ == Phase::AffineInstanced || phase_ == Phase::AffineFallback;
    if (current.scene_meshes != 1u || current.scene_instances != (baseline ? 10000u : affine ? 8u : kPopulation)) {
      fail("100000 cube occurrences did not share one canonical mesh"); return false;
    }
    const bool measured = measured_phase();
    if (measured && (current.frame_submissions - retained_.frame_submissions < kBenchmarkFrames ||
                     measured_render_epochs_ != kBenchmarkFrames ||
                     cpu_samples_ != kBenchmarkFrames - 1u)) {
      fail("Benchmark counted a retained copy instead of a newly prepared/rendered frame"); return false;
    }
    if (phase_ == Phase::Baseline) {
      if (gpu.fallback_draws < 10000u || gpu.instanced_draws != 0u || current.bgfx_draws < 10000u || lit_pixels < 10000u) {
        fail("Separate-draw baseline did not render the actual 10000-cube scene"); return false;
      }
    } else if (phase_ == Phase::BaselineInstanced) {
      if (!geometry_retained || gpu.instanced_draws <= previous.instanced_draws ||
          gpu.fallback_draws != previous.fallback_draws || current.bgfx_draws >= 100u || image != previous_image_) {
        fail("Instanced 10000-cube baseline lost pixel parity or retained per-object submits"); return false;
      }
    } else if (phase_ == Phase::Initial) {
      if (gpu.position_uploads != 1u || gpu.attribute_uploads != 1u || gpu.index_uploads != 1u ||
          gpu.instanced_instances - previous.instanced_instances < kPopulation ||
          gpu.instanced_draws <= previous.instanced_draws ||
          gpu.instanced_draws - previous.instanced_draws >= kPopulation / 100u ||
          gpu.fallback_draws != previous.fallback_draws || current.bgfx_draws == 0u ||
          current.bgfx_draws >= 100u || lit_pixels < 10000u) {
        fail("Initial workload did not issue bounded real indexed instanced draws"); return false;
      }
      identity_hash_ = current.scene_instance_identity_hash;
    } else if (phase_ == Phase::SingleEdit) {
      const auto records = gpu.instance_record_uploads - previous.instance_record_uploads;
      const auto bytes = gpu.instance_upload_bytes - previous.instance_upload_bytes;
      const auto point = camera_.project_world_point({1.4f,0.7f,0.45f}).position;
      const auto before = previous_image_.pixelColor(qRound(point.x), qRound(point.y));
      const auto after = image.pixelColor(qRound(point.x), qRound(point.y));
      if (!geometry_retained || records != 1u || bytes != 256u || image == previous_image_ ||
          before.red() > 10 || after.red() < 20) {
        fail("One placement edit rebuilt geometry, reuploaded the population, or failed to change pixels"); return false;
      }
    } else if (phase_ != Phase::DepthFront && phase_ != Phase::DepthBack && !affine &&
               (!geometry_retained || !instances_retained)) {
      fail("Unchanged population or camera change uploaded retained geometry/instance records"); return false;
    }
    if (phase_ == Phase::Unchanged && image != previous_image_) {
      fail("Unchanged public host composition changed pixels"); return false;
    }
    if (phase_ == Phase::Culled && (gpu.culled_instances - previous.culled_instances < kPopulation || lit_pixels != 0u)) {
      fail("Offscreen population was not culled from the submitted view"); return false;
    }
    if (phase_ == Phase::Restored && lit_pixels < 10000u) {
      fail("Restoring the camera did not restore the retained cube population"); return false;
    }
    if (!baseline && !affine && current.scene_instance_identity_hash != identity_hash_) {
      fail("Placement edits, culling or material regrouping changed stable occurrence identity"); return false;
    }
    if (phase_ == Phase::DepthFront || phase_ == Phase::DepthBack) {
      const auto point = camera_.project_world_point({0.7f,0.7f,0});
      const auto color = image.pixelColor(qRound(point.position.x), qRound(point.position.y));
      const bool blue = color.blue() > color.red() + 30;
      const bool red = color.red() > color.blue() + 30;
      if (!geometry_retained || (phase_ == Phase::DepthFront ? !blue : !red)) {
        fail("Instanced material group depth ordering disagreed with actual foreground cube pixels"); return false;
      }
    }
    if (phase_ == Phase::AffineInstanced) {
      if (gpu.instanced_draws == previous.instanced_draws || gpu.fallback_draws != previous.fallback_draws || lit_pixels < 1000u) {
        fail("Affine parity fixture did not use actual instanced submissions"); return false;
      }
      if (gpu.image_uploads - previous.image_uploads != 1u) {
        fail("Affine default-image fixture did not upload exactly one checker image"); return false;
      }
    } else if (phase_ == Phase::AffineFallback) {
      std::uint64_t different = 0u;
      std::uint64_t warm_pixels = 0u, cool_pixels = 0u;
      for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        const auto left = image.pixelColor(x,y), right = previous_image_.pixelColor(x,y);
        different += std::abs(left.red()-right.red()) > 2 || std::abs(left.green()-right.green()) > 2 ||
                     std::abs(left.blue()-right.blue()) > 2;
        warm_pixels += left.red() > left.blue() + 30;
        cool_pixels += left.blue() > left.red() + 30;
      }
      if (!geometry_retained) { fail("Affine oracle reuploaded unchanged geometry"); return false; }
      if (!instances_retained) { fail("Affine oracle reuploaded unchanged instance records"); return false; }
      if (gpu.image_uploads != previous.image_uploads) { fail("Affine oracle reuploaded the unchanged default image"); return false; }
      if (gpu.fallback_draws - previous.fallback_draws < 8u) {
        fail("Affine oracle did not issue all eight separate object draws"); return false;
      }
      if (warm_pixels < 100u || cool_pixels < 100u) {
        fail("Affine default-image checker was absent: warm=" + std::to_string(warm_pixels) +
             "; cool=" + std::to_string(cool_pixels)); return false;
      }
      if (different > 8u) {
        fail("Affine instanced/separate GPU pixels disagree: differing_pixels=" + std::to_string(different)); return false;
      }
      if (current.scene_instance_identity_hash != retained_.scene_instance_identity_hash) {
        fail("Affine oracle changed accepted occurrence identity"); return false;
      }
    }
    evidence_ << kPhaseNames[static_cast<std::size_t>(phase_)] << ',' << current.scene_meshes << ','
        << current.scene_instances << ',' << gpu.position_uploads << ',' << gpu.attribute_uploads << ','
        << gpu.index_uploads << ',' << gpu.image_uploads << ',' << gpu.instance_buffer_allocations << ',' << gpu.instance_record_uploads << ','
        << gpu.instance_upload_bytes << ',' << gpu.instanced_draws << ',' << gpu.instanced_instances << ','
        << gpu.fallback_draws << ',' << gpu.culled_instances << ',' << current.frame_submissions << ','
        << current.bgfx_draws << ',' << current.scene_instance_identity_hash << ','
        << preparation_ms_ << ',' << phase_clock_.elapsed() << ',' << lit_pixels << ','
        << benchmark_frames_ << ',' << benchmark_ms_ << ',' << measured_render_epochs_ << ',' << cpu_samples_ << ','
        << (cpu_samples_ ? cpu_host_ms_ / cpu_samples_ : 0.0) << ',' << cpu_host_max_ms_ << '\n';
    evidence_.flush();
    std::cout << "PASS phase=" << kPhaseNames[static_cast<std::size_t>(phase_)]
        << "; cubes=" << current.scene_instances << "; actual_instanced_draws=" << gpu.instanced_draws
        << "; submitted_instances=" << gpu.instanced_instances
        << "; instance_upload_bytes=" << gpu.instance_upload_bytes << std::endl;
    retained_ = current;
    previous_image_ = image;
    return true;
  }

  void next(Phase phase) {
    phase_ = phase;
    ++requested_epoch_;
    needs_prepare_ = true;
    phase_clock_.restart();
    benchmark_frames_ = 0u;
    benchmark_ms_ = 0.0;
    cpu_samples_ = 0u;
    cpu_host_ms_ = 0.0;
    cpu_host_max_ms_ = 0.0;
    measured_render_epochs_ = 0u;
  }

  void create_depth_fixture() {
    auto &population = snapshot_.instance_primitives.front();
    RenderSceneInstancePrimitiveRecord foreground;
    foreground.definition_entity = population.definition_entity;
    foreground.member_entity = population.member_entity;
    foreground.mesh = population.mesh;
    foreground.primitive = population.primitive;
    foreground.material = population.material;
    foreground.material.primary_material.material_id = 84u;
    foreground.material.primary_material.document.base_color = {0.02f,0.02f,0.9f};
    auto occurrence = population.occurrences.back();
    population.occurrences.pop_back();
    occurrence.world_transform.values[12] = 0.7f;
    occurrence.world_transform.values[13] = 0.7f;
    occurrence.world_transform.values[14] = 3.0f;
    foreground.occurrences.push_back(occurrence);
    snapshot_.instance_primitives.push_back(std::move(foreground));
    ++snapshot_.transform_revision;
  }

  void create_affine_fixture() {
    snapshot_.instance_primitives.resize(1u);
    auto &primitive = snapshot_.instance_primitives.front();
    primitive.occurrences.resize(8u);
    geometry_ = std::make_shared<MeshGeometryPayload>(*geometry_);
    for (auto &vertex : geometry_->vertices) {
      vertex.uv_carrier = {1,0,0,1};
      vertex.uv_geometric_normal = vertex.normal;
    }
    primitive.mesh.geometry_payload = geometry_;
    primitive.mesh.vertices = geometry_->vertices;
    primitive.mesh.indices = geometry_->indices;
    primitive.mesh.primitives = geometry_->primitives;
    ++primitive.mesh.mesh_revision;
    auto pixels = std::make_shared<std::vector<std::byte>>(16u * 16u * 4u);
    for (std::size_t y = 0u; y < 16u; ++y) for (std::size_t x = 0u; x < 16u; ++x) {
      const bool alternate = ((x / 2u) + (y / 2u)) % 2u == 0u;
      const auto offset = (y * 16u + x) * 4u;
      (*pixels)[offset] = static_cast<std::byte>(alternate ? 240u : 20u);
      (*pixels)[offset+1u] = static_cast<std::byte>(alternate ? 40u : 200u);
      (*pixels)[offset+2u] = static_cast<std::byte>(alternate ? 20u : 240u);
      (*pixels)[offset+3u] = static_cast<std::byte>(255u);
    }
    RenderTextureAssetSnapshot texture;
    texture.slot = material_texture_slot::kBaseColor;
    texture.texture_id = 99u;
    texture.content_revision = 1u;
    texture.texture.usage = TextureUsage::BaseColor;
    texture.texture.color_space = TextureColorSpace::Srgb;
    texture.decoded_rgba8 = std::move(pixels);
    texture.width = 16u; texture.height = 16u; texture.present = true;
    primitive.material.primary_material.textures = {std::move(texture)};
    // Match the public default-image extraction contract: an embedded material
    // binds its image through the canonical slot as well as pinning its bytes.
    (void)set_material_texture_slot(primitive.material.primary_material.document,
                                   material_texture_slot::kBaseColor, 99u);
    primitive.material.primary_material.document.base_color = {1,1,1};
    ++primitive.material.primary_material.revision;
    ++primitive.material.revision;
    if (!primitive.material.primary_material.texture_for_slot(material_texture_slot::kBaseColor)) {
      fail("The default-image acceptance snapshot has no canonical albedo binding"); return;
    }
    for (std::size_t index = 0u; index < primitive.occurrences.size(); ++index) {
      auto &occurrence = primitive.occurrences[index];
      occurrence.world_transform = {};
      occurrence.world_transform.values[0] = index % 2u == 0u ? 5.0f : -5.0f;
      occurrence.world_transform.values[5] = index % 3u == 0u ? 9.0f : 5.0f;
      occurrence.world_transform.values[10] = 4.0f;
      occurrence.world_transform.values[4] = index >= 4u ? 2.0f : 0.0f;
      occurrence.world_transform.values[8] = index >= 6u ? 1.0f : 0.0f;
      occurrence.world_transform.values[12] = (static_cast<float>(index) - 3.5f) * 9.0f;
      occurrence.sampling_from_local = {};
      occurrence.sampling_from_local.values[0] = 0.8f + static_cast<float>(index) * 0.1f;
      occurrence.sampling_from_local.values[4] = index >= 4u ? 0.2f : 0.0f;
      occurrence.sampling_from_local.values[12] = static_cast<float>(index) * 0.17f;
    }
    ++snapshot_.transform_revision;
    ++snapshot_.texture_mapping_revision;
    camera_.focus_bounds({-40,-7,-4}, {40,7,4}, 1.1f);
  }

  void retire() {
    if (!root_) return;
    initialized_ = false;
    if (view_) { view_->shutdown(); view_.reset(); }
    if (surface_) { surface_->reset(); surface_.reset(); }
    if (sink_) { sink_->shutdown(); sink_.reset(); }
    host_->shutdown();
    widget_->set_render_system(nullptr);
    root_.reset(); widget_ = nullptr;
  }

  void advance() {
    if (!error_.empty() || phase_clock_.elapsed() > 60000) {
      if (error_.empty()) fail("Timed out waiting for 100000-cube host composition");
      retire(); timer_.stop(); application_.exit(1); return;
    }
    if (!initialized_ && !initialize()) return;
    if (needs_prepare_) { prepare(); return; }
    if (presented_epoch_ != requested_epoch_) return;
    if (phase_ == Phase::Baseline || phase_ == Phase::BaselineInstanced || phase_ == Phase::Benchmark) {
      if (benchmark_frames_ == 0u) benchmark_clock_.start();
      else {
        const double cpu = preparation_ms_ + render_cpu_ms_;
        cpu_host_ms_ += cpu;
        cpu_host_max_ms_ = std::max(cpu_host_max_ms_, cpu);
        ++cpu_samples_;
      }
      ++benchmark_frames_;
      if (benchmark_frames_ % 30u == 0u) progress("presented benchmark progress");
      if (benchmark_frames_ < kBenchmarkFrames) {
        camera_.fly_move({benchmark_frames_ % 2u == 0u ? 1.0f : -1.0f, 0, 0}, 0.0001f);
        ++requested_epoch_;
        needs_prepare_ = true;
        return;
      }
      benchmark_ms_ = static_cast<double>(benchmark_clock_.nsecsElapsed()) / 1000000.0;
    } else if (presented_clock_.elapsed() < 100) return;
    if (!capture()) return;
    switch (phase_) {
    case Phase::Baseline:
      (void)crimson_set_gpu_instancing_enabled(*host_, true);
      camera_.focus_bounds({-74,-74,-16}, {74,74,1}, 1.1f);
      next(Phase::BaselineInstanced); break;
    case Phase::BaselineInstanced:
      timer_.setInterval(40);
      next(Phase::Initial); break;
    case Phase::Initial: next(Phase::Unchanged); break;
    case Phase::Unchanged:
      snapshot_.instance_primitives.front().occurrences[5050u].world_transform.values[12] += 0.6f;
      ++snapshot_.transform_revision;
      next(Phase::SingleEdit); break;
    case Phase::SingleEdit:
      camera_.fly_move({0.1f, 0, 0}, 1.0f);
      next(Phase::Camera); break;
    case Phase::Camera:
      camera_.focus_bounds({19926,-74,-16}, {20074,74,1}, 1.1f);
      next(Phase::Culled); break;
    case Phase::Culled:
      camera_.focus_bounds({-74,-74,-16}, {74,74,1}, 1.1f);
      next(Phase::Restored); break;
    case Phase::Restored:
      timer_.setInterval(1);
      camera_.fly_move({0.0001f, 0, 0}, 0.1f);
      next(Phase::Benchmark); break;
    case Phase::Benchmark:
      timer_.setInterval(40);
      camera_.focus_bounds({-74,-74,-16}, {74,74,1}, 1.1f);
      create_depth_fixture();
      next(Phase::DepthFront); break;
    case Phase::DepthFront:
      snapshot_.instance_primitives.back().occurrences.front().world_transform.values[14] = -30.0f;
      ++snapshot_.transform_revision;
      next(Phase::DepthBack); break;
    case Phase::DepthBack:
      create_affine_fixture();
      next(Phase::AffineInstanced); break;
    case Phase::AffineInstanced:
      (void)crimson_set_gpu_instancing_enabled(*host_, false);
      next(Phase::AffineFallback); break;
    case Phase::AffineFallback:
      retire(); timer_.stop();
      if (release_count_ != 1u) fail("Borrowed device release was not completed exactly once");
      application_.exit(error_.empty() ? 0 : 1); break;
    }
  }

  QApplication &application_;
  QString output_directory_;
  std::filesystem::path resources_;
  std::unique_ptr<RenderHost> host_;
  std::unique_ptr<RenderSceneSink> sink_;
  std::unique_ptr<RenderView> view_;
  std::unique_ptr<RenderSurface> surface_;
  std::shared_ptr<MeshGeometryPayload> geometry_;
  RenderSceneSnapshot snapshot_;
  ViewportSettings settings_;
  ViewportCameraController camera_;
  std::unique_ptr<QWidget> root_;
  QtVulkanCompositionWidget *widget_ = nullptr;
  QTimer timer_;
  QElapsedTimer phase_clock_, presented_clock_, benchmark_clock_;
  std::ofstream evidence_;
  CrimsonRenderDiagnostics retained_;
  QImage previous_image_;
  Phase phase_ = Phase::Baseline;
  std::uint64_t identity_hash_ = 0u;
  std::uint64_t requested_epoch_ = 1u, prepared_epoch_ = 0u, rendered_epoch_ = 0u, presented_epoch_ = 0u;
  std::uint32_t release_count_ = 0u;
  std::uint32_t benchmark_frames_ = 0u;
  std::uint32_t cpu_samples_ = 0u;
  std::uint32_t measured_render_epochs_ = 0u;
  double benchmark_ms_ = 0.0;
  double preparation_ms_ = 0.0;
  double render_cpu_ms_ = 0.0, cpu_host_ms_ = 0.0, cpu_host_max_ms_ = 0.0;
  bool initialized_ = false, needs_prepare_ = true;
  std::string error_;
};

} // namespace
int run_qt_crimson_instancing_probe(QApplication &application,
    const std::filesystem::path &output_directory,
    const std::filesystem::path &resources) {
  application.setQuitOnLastWindowClosed(false);
  InstancingCompositionProbe probe(application, QString::fromStdWString(output_directory.wstring()), resources);
  return probe.run();
}

} // namespace quader
