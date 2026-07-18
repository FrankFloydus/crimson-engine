////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/interaction_api.hpp>
#include <editor/projection/mesh_projection_service.hpp>
#include <quader/modeling/modeling.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t v1_target_triangles = 5'000'000ULL;
constexpr double frame_budget_60_hz_ms = 1000.0 / 60.0;
constexpr double frame_budget_120_hz_ms = 1000.0 / 120.0;

/**
 * Stores the Benchmark Options data contract used by the modeling performance benchmark.
 */
struct BenchmarkOptions {
	std::string tier = "smoke";
	std::filesystem::path output_path;
	std::uint64_t requested_triangles = 100'000ULL;
	int object_count = 4;
	int compile_iterations = 2;
	int overlay_iterations = 1;
	int pick_iterations = 100;
	int transform_iterations = 3;
	int navigation_iterations = 5000;
	int visibility_iterations = 500;
};

/**
 * Stores the Benchmark Result data contract used by the modeling performance benchmark.
 */
struct BenchmarkResult {
	std::string name;
	std::string unit;
	int iterations = 1;
	double elapsed_ms = 0.0;
	double average_ms = 0.0;
	std::uint64_t work_items = 0;
	std::uint64_t checksum = 0;
};

/**
 * Represents a Scene Stats value used by the modeling performance benchmark.
 */
struct SceneStats {
	std::uint64_t objects = 0;
	std::uint64_t documents = 0;
	std::uint64_t faces = 0;
	std::uint64_t triangles = 0;
	std::uint64_t vertices = 0;
	std::uint64_t selected_faces = 0;
};

/**
 * Represents a Benchmark Scene value used by the modeling performance benchmark.
 */
struct BenchmarkScene {
	std::vector<quader_editor_core::PolygonObject> objects;
	SceneStats stats;
};

std::string json_escape(std::string_view value)
{
	std::string escaped;
	escaped.reserve(value.size() + 8U);
	for (const char character : value) {
		switch (character) {
		case '\\':
			escaped += "\\\\";
			break;
		case '"':
			escaped += "\\\"";
			break;
		case '\n':
			escaped += "\\n";
			break;
		case '\r':
			escaped += "\\r";
			break;
		case '\t':
			escaped += "\\t";
			break;
		default:
			escaped += character;
			break;
		}
	}
	return escaped;
}

std::uint64_t parse_u64(std::string_view text, std::string_view name)
{
	try {
		std::size_t parsed = 0;
		const std::uint64_t value = std::stoull(std::string(text), &parsed);
		if (parsed != text.size()) {
			throw std::invalid_argument("trailing characters");
		}
		return value;
	}
	catch (const std::exception&) {
		throw std::invalid_argument("Invalid " + std::string(name) + ": " + std::string(text));
	}
}

int parse_int(std::string_view text, std::string_view name)
{
	const std::uint64_t value = parse_u64(text, name);
	if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
		throw std::invalid_argument("Invalid " + std::string(name) + ": " + std::string(text));
	}
	return static_cast<int>(value);
}

void apply_tier_defaults(BenchmarkOptions& options)
{
	if (options.tier == "smoke") {
		options.requested_triangles = 10'000ULL;
		options.object_count = 2;
		options.compile_iterations = 2;
		options.overlay_iterations = 1;
		options.pick_iterations = 25;
		options.transform_iterations = 3;
		options.navigation_iterations = 2000;
		options.visibility_iterations = 200;
		return;
	}
	if (options.tier == "local") {
		options.requested_triangles = 100'000ULL;
		options.object_count = 4;
		options.compile_iterations = 1;
		options.overlay_iterations = 1;
		options.pick_iterations = 25;
		options.transform_iterations = 3;
		options.navigation_iterations = 5000;
		options.visibility_iterations = 500;
		return;
	}
	if (options.tier == "target" || options.tier == "target5m") {
		options.tier = "target5m";
		options.requested_triangles = v1_target_triangles;
		options.object_count = 24;
		options.compile_iterations = 1;
		options.overlay_iterations = 1;
		options.pick_iterations = 5;
		options.transform_iterations = 2;
		options.navigation_iterations = 10'000;
		options.visibility_iterations = 500;
		return;
	}
	throw std::invalid_argument("Unknown benchmark tier: " + options.tier);
}

BenchmarkOptions parse_args(int argc, char** argv)
{
	BenchmarkOptions options;
	std::vector<std::pair<std::string, std::string>> overrides;
	for (int index = 1; index < argc; ++index) {
		const std::string_view arg(argv[index]);
		const auto require_value = [&](std::string_view name) -> std::string_view {
			if (index + 1 >= argc) {
				throw std::invalid_argument("Missing value for " + std::string(name));
			}
			++index;
			return argv[index];
		};

		if (arg == "--help" || arg == "-h") {
			std::cout
				<< "Usage: qdr_performance_baseline [--tier smoke|local|target5m] [--output file.json]\n"
				<< "       [--triangles N] [--objects N] [--compile-iterations N]\n"
				<< "       [--overlay-iterations N] [--pick-iterations N]\n"
				<< "       [--transform-iterations N] [--navigation-iterations N]\n"
				<< "       [--visibility-iterations N]\n";
			std::exit(0);
		}
		if (arg == "--tier") {
			options.tier = std::string(require_value(arg));
		}
		else if (arg == "--output") {
			options.output_path = std::filesystem::path(std::string(require_value(arg)));
		}
		else if (arg == "--triangles" || arg == "--objects" || arg == "--compile-iterations" ||
				arg == "--overlay-iterations" || arg == "--pick-iterations" ||
				arg == "--transform-iterations" || arg == "--navigation-iterations" ||
				arg == "--visibility-iterations") {
			overrides.emplace_back(std::string(arg), std::string(require_value(arg)));
		}
		else {
			throw std::invalid_argument("Unknown argument: " + std::string(arg));
		}
	}

	apply_tier_defaults(options);
	for (const auto& [name, value] : overrides) {
		if (name == "--triangles") {
			options.requested_triangles = std::max<std::uint64_t>(2ULL, parse_u64(value, name));
		}
		else if (name == "--objects") {
			options.object_count = std::max(1, parse_int(value, name));
		}
		else if (name == "--compile-iterations") {
			options.compile_iterations = std::max(1, parse_int(value, name));
		}
		else if (name == "--overlay-iterations") {
			options.overlay_iterations = std::max(1, parse_int(value, name));
		}
		else if (name == "--pick-iterations") {
			options.pick_iterations = std::max(1, parse_int(value, name));
		}
		else if (name == "--transform-iterations") {
			options.transform_iterations = std::max(1, parse_int(value, name));
		}
		else if (name == "--navigation-iterations") {
			options.navigation_iterations = std::max(1, parse_int(value, name));
		}
		else if (name == "--visibility-iterations") {
			options.visibility_iterations = std::max(1, parse_int(value, name));
		}
	}
	return options;
}

quader_poly::Document make_dense_grid_document(int width_segments, int depth_segments, float spacing)
{
	quader_poly::Document document;
	const std::uint64_t vertex_count =
		static_cast<std::uint64_t>(width_segments + 1) * static_cast<std::uint64_t>(depth_segments + 1);
	const std::uint64_t face_count =
		static_cast<std::uint64_t>(width_segments) * static_cast<std::uint64_t>(depth_segments);
	if (vertex_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
			face_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
		throw std::runtime_error("Synthetic grid is too large for this platform.");
	}

	document.vertices.reserve(static_cast<std::size_t>(vertex_count));
	document.faces.reserve(static_cast<std::size_t>(face_count));
	std::vector<quader_poly::ElementId> grid(static_cast<std::size_t>(vertex_count));
	const auto grid_index = [depth_segments](int x, int z) {
		return static_cast<std::size_t>(x * (depth_segments + 1) + z);
	};

	const float width = static_cast<float>(width_segments) * spacing;
	const float depth = static_cast<float>(depth_segments) * spacing;
	for (int x = 0; x <= width_segments; ++x) {
		const float px = -width * 0.5F + static_cast<float>(x) * spacing;
		for (int z = 0; z <= depth_segments; ++z) {
			const float pz = -depth * 0.5F + static_cast<float>(z) * spacing;
			grid[grid_index(x, z)] = quader_poly::add_vertex(document, { px, 0.0F, pz });
		}
	}

	for (int x = 0; x < width_segments; ++x) {
		for (int z = 0; z < depth_segments; ++z) {
			const std::array vertices {
				grid[grid_index(x, z)],
				grid[grid_index(x, z + 1)],
				grid[grid_index(x + 1, z + 1)],
				grid[grid_index(x + 1, z)],
			};
			[[maybe_unused]] const quader_poly::ElementId face_id = quader_poly::add_face(document, vertices);
		}
	}
	return document;
}

std::pair<int, int> grid_segments_for_triangle_budget(std::uint64_t triangle_budget)
{
	const std::uint64_t face_budget = std::max<std::uint64_t>(1ULL, triangle_budget / 2ULL);
	int width_segments = static_cast<int>(std::max<double>(1.0, std::floor(std::sqrt(static_cast<double>(face_budget)))));
	int depth_segments = static_cast<int>(std::max<std::uint64_t>(1ULL, face_budget / static_cast<std::uint64_t>(width_segments)));
	if (static_cast<std::uint64_t>(width_segments) * static_cast<std::uint64_t>(depth_segments) * 2ULL < triangle_budget) {
		++depth_segments;
	}
	return { width_segments, depth_segments };
}

void offset_document(quader_poly::Document& document, quader::QVec3 offset)
{
	for (quader_poly::Vertex& vertex : document.vertices) {
		vertex.position.x += offset.x;
		vertex.position.y += offset.y;
		vertex.position.z += offset.z;
	}
}

BenchmarkScene make_scene(const BenchmarkOptions& options)
{
	BenchmarkScene scene;
	scene.objects.reserve(static_cast<std::size_t>(options.object_count));
	const std::uint64_t triangles_per_object =
		std::max<std::uint64_t>(2ULL, options.requested_triangles / static_cast<std::uint64_t>(options.object_count));
	const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(options.object_count)))));

	for (int index = 0; index < options.object_count; ++index) {
		auto [width_segments, depth_segments] = grid_segments_for_triangle_budget(triangles_per_object);
		quader_poly::Document document = make_dense_grid_document(width_segments, depth_segments, 1.0F);
		const int column = index % columns;
		const int row = index / columns;
		offset_document(document, {
			static_cast<float>(column) * (static_cast<float>(width_segments) + 6.0F),
			0.0F,
			static_cast<float>(row) * (static_cast<float>(depth_segments) + 6.0F),
		});

		quader_editor_core::PolygonObject object;
		object.id = index + 1;
		object.selected = true;
		object.document = std::move(document);
		object.selection.mode = quader_poly::SelectionMode::Face;
		object.selection.faces.reserve(object.document.faces.size());
		for (const quader_poly::Face& face : object.document.faces) {
			object.selection.faces.push_back(face.id);
		}
		if (!object.selection.faces.empty()) {
			object.selection.has_active = true;
			object.selection.active_kind = quader_poly::ElementKind::Face;
			object.selection.active_face = object.selection.faces.front();
		}

		scene.stats.objects += 1ULL;
		scene.stats.documents += 1ULL;
		scene.stats.vertices += object.document.vertices.size();
		scene.stats.faces += object.document.faces.size();
		scene.stats.triangles += object.document.faces.size() * 2ULL;
		scene.stats.selected_faces += object.selection.faces.size();
		scene.objects.push_back(std::move(object));
	}
	return scene;
}

template <typename Function>
BenchmarkResult measure(std::string name, std::string unit, int iterations, std::uint64_t work_items, Function&& function)
{
	const Clock::time_point start = Clock::now();
	std::uint64_t checksum = 0;
	for (int iteration = 0; iteration < iterations; ++iteration) {
		checksum += static_cast<std::uint64_t>(function(iteration));
	}
	const Clock::time_point end = Clock::now();
	const double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
	BenchmarkResult result;
	result.name = std::move(name);
	result.unit = std::move(unit);
	result.iterations = iterations;
	result.elapsed_ms = elapsed_ms;
	result.average_ms = elapsed_ms / static_cast<double>(std::max(1, iterations));
	result.work_items = work_items;
	result.checksum = checksum;
	return result;
}

std::vector<quader::QVec3> compiled_positions(const quader_poly::CompiledMesh& mesh)
{
	std::vector<quader::QVec3> positions;
	positions.reserve(mesh.vertices.size());
	for (const quader_poly::CompiledVertex& vertex : mesh.vertices) {
		positions.push_back(vertex.position);
	}
	return positions;
}

quader_editor_core::ViewportCameraState benchmark_camera()
{
	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::Perspective;
	camera.transform.origin = { 0.0F, 90.0F, 140.0F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 1.0F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.0F };
	camera.viewport_size = { 1920.0F, 1080.0F };
	camera.fov_degrees = 60.0;
	return camera;
}

std::vector<BenchmarkResult> run_benchmarks(const BenchmarkOptions& options, const BenchmarkScene& scene)
{
	std::vector<quader::modeling::PolygonDocument> sdk_documents;
	sdk_documents.reserve(scene.objects.size());
	for (const quader_editor_core::PolygonObject& object : scene.objects) {
		quader::modeling::Result<quader::modeling::PolygonDocument> converted =
			quader::MeshProjectionService::sdk_document_from_polygon_document(object.document.read());
		if (!converted.ok()) {
			throw std::runtime_error("SDK benchmark document conversion failed: " + converted.error().message);
		}
		quader::modeling::PolygonDocument document = std::move(converted).value();
		const quader::modeling::Result<quader::modeling::OperationResult> selected = document.select_all_faces();
		if (!selected.ok() || !selected.value().success) {
			throw std::runtime_error(selected.ok() ? selected.value().message : selected.error().message);
		}
		sdk_documents.push_back(std::move(document));
	}

	quader::modeling::ModelingSession transform_session;
	std::vector<quader::modeling::ObjectId> transform_object_ids;
	transform_object_ids.reserve(sdk_documents.size());
	for (const quader::modeling::PolygonDocument& document : sdk_documents) {
		quader::modeling::Result<quader::modeling::ObjectId> added =
			transform_session.add_document(document, "Benchmark Mesh");
		if (!added.ok()) {
			throw std::runtime_error("SDK benchmark session setup failed: " + added.error().message);
		}
		transform_object_ids.push_back(added.value());
	}
	if (transform_object_ids.empty()) {
		throw std::runtime_error("SDK benchmark requires at least one modeling object.");
	}
	const quader::modeling::Result<quader::modeling::OperationResult> transform_selected =
		transform_session.apply_object_selection(
			std::span<const quader::modeling::ObjectId>(transform_object_ids),
			quader::modeling::SelectionEdit::Replace);
	if (!transform_selected.ok() || !transform_selected.value().success) {
		throw std::runtime_error(
			transform_selected.ok() ? transform_selected.value().message : transform_selected.error().message);
	}

	std::vector<BenchmarkResult> results;
	results.reserve(8);

	results.push_back(measure(
		"compile_document_flat",
		"scene compile",
		options.compile_iterations,
		scene.stats.triangles,
		[&](int) {
			std::uint64_t checksum = 0;
			for (const quader_editor_core::PolygonObject& object : scene.objects) {
				const quader_poly::CompiledMesh mesh = quader_poly::compile_document(object.document, quader_poly::SurfaceShading::Flat);
				checksum += mesh.vertices.size();
				checksum += mesh.indices.size();
				checksum += mesh.primitives.size();
			}
			return checksum;
		}));

	results.push_back(measure(
		"compile_document_smooth",
		"scene compile",
		1,
		scene.stats.triangles,
		[&](int) {
			std::uint64_t checksum = 0;
			for (const quader_editor_core::PolygonObject& object : scene.objects) {
				const quader_poly::CompiledMesh mesh = quader_poly::compile_document(object.document, quader_poly::SurfaceShading::Smooth);
				checksum += mesh.vertices.size();
				checksum += mesh.indices.size();
				checksum += mesh.primitives.size();
			}
			return checksum;
		}));

	results.push_back(measure(
		"selection_overlay_object_mode",
		"overlay frame",
		options.overlay_iterations,
		scene.stats.triangles,
		[&](int) {
			const quader_editor_core::SelectionOverlayFrame frame =
				quader_editor_core::build_selection_overlay_frame(scene.objects, quader_editor_core::SelectionMode::Object, {});
			return frame.triangles.size() + frame.lines.size() + frame.points.size();
		}));

	results.push_back(measure(
		"selection_overlay_face_mode_all_faces",
		"overlay frame",
		options.overlay_iterations,
		scene.stats.selected_faces,
		[&](int) {
			const quader_editor_core::SelectionOverlayFrame frame =
				quader_editor_core::build_selection_overlay_frame(scene.objects, quader_editor_core::SelectionMode::Face, {});
			return frame.triangles.size() + frame.lines.size() + frame.points.size();
		}));

	quader_poly::PickOptions face_pick_options;
	face_pick_options.mode = quader_poly::SelectionMode::Face;
	results.push_back(measure(
		"pick_nearest_face_rays",
		"ray",
		options.pick_iterations,
		static_cast<std::uint64_t>(options.pick_iterations),
		[&](int iteration) {
			const float x = static_cast<float>((iteration % 31) - 15);
			const float z = static_cast<float>(((iteration / 31) % 31) - 15);
			quader_editor_core::PickRequest request;
			request.options = face_pick_options;
			request.ray = { { x, 100.0F, z }, { 0.0F, -1.0F, 0.0F } };
			const std::optional<quader_editor_core::PickHitRecord> hit = quader_editor_core::pick_nearest(scene.objects, request);
			return hit.has_value() ? static_cast<std::uint64_t>(hit->face_id + hit->object_id) : 0ULL;
		}));

	results.push_back(measure(
		"update_selection_hover_face",
		"hover ray",
		options.pick_iterations,
		static_cast<std::uint64_t>(options.pick_iterations),
		[&](int iteration) {
			quader_editor_core::SelectionHoverState hover;
			const float x = static_cast<float>((iteration % 37) - 18);
			const float z = static_cast<float>(((iteration / 37) % 37) - 18);
			const quader_editor_core::SelectionHoverUpdateResult result =
				quader_editor_core::update_selection_hover(
					scene.objects,
					quader_editor_core::SelectionMode::Face,
					hover,
					{ { x, 100.0F, z }, { 0.0F, -1.0F, 0.0F } },
					false,
					face_pick_options);
			return result.changed ? static_cast<std::uint64_t>(result.hover.face_id + result.hover.object_id) : 0ULL;
		}));

	results.push_back(measure(
		"transform_preview_translate_selected_objects",
		"preview update",
		1,
		static_cast<std::uint64_t>(options.transform_iterations),
		[&](int) {
			const quader::modeling::Result<quader::modeling::OperationResult> begin =
				transform_session.begin_translate_preview(transform_object_ids.front(), {});
			if (!begin.ok() || !begin.value().success) {
				throw std::runtime_error(begin.ok() ? begin.value().message : begin.error().message);
			}
			std::uint64_t checksum = 1ULL;
			for (int iteration = 0; iteration < options.transform_iterations; ++iteration) {
				const float amount = 0.05F * static_cast<float>(iteration + 1);
				const quader::modeling::Result<quader::modeling::OperationResult> update =
					transform_session.update_translate_preview({ amount, 0.0F, amount * 0.5F });
				if (!update.ok() || !update.value().success) {
					throw std::runtime_error(update.ok() ? update.value().message : update.error().message);
				}
				checksum += update.value().changed ? 17ULL : 3ULL;
				checksum += transform_session.objects().size();
			}
			const quader::modeling::Result<quader::modeling::OperationResult> cancel =
				transform_session.cancel_preview();
			if (!cancel.ok() || !cancel.value().success) {
				throw std::runtime_error(cancel.ok() ? cancel.value().message : cancel.error().message);
			}
			return checksum + 1ULL;
		}));

	results.push_back(measure(
		"viewport_navigation_math",
		"navigation step",
		options.navigation_iterations,
		static_cast<std::uint64_t>(options.navigation_iterations),
		[&](int iteration) {
			quader_editor_core::ViewportCameraState camera = benchmark_camera();
			const quader::QVec3 pivot { 0.0F, 0.0F, 0.0F };
			std::uint64_t checksum = 0;
			const quader_editor_core::ViewportNavigationResult orbit = quader_editor_core::viewport_orbit({
				camera.transform,
				pivot,
				{ static_cast<float>((iteration % 17) - 8), static_cast<float>((iteration % 13) - 6) },
				quader_editor_core::kNavigationOrbitRadiansPerPixel,
				(iteration % 97) == 0,
			});
			checksum += orbit.ok ? 1ULL : 0ULL;
			camera.transform = orbit.camera_transform;
			const quader_editor_core::ViewportNavigationResult pan = quader_editor_core::viewport_pan({
				camera,
				pivot,
				{ 2.0F, -1.0F },
			});
			checksum += pan.ok ? 3ULL : 0ULL;
			camera.transform = pan.camera_transform;
			const quader_editor_core::ViewportNavigationResult zoom = quader_editor_core::viewport_zoom({
				camera,
				pivot,
				(iteration % 2) == 0 ? 1.0 : -1.0,
			});
			checksum += zoom.ok ? 5ULL : 0ULL;
			return checksum;
		}));

	results.push_back(measure(
		"overlay_visibility_occluder",
		"visibility query",
		1,
		static_cast<std::uint64_t>(options.visibility_iterations),
		[&](int) {
			if (scene.objects.empty()) {
				return 0ULL;
			}
			const quader_poly::CompiledMesh mesh =
				quader_poly::compile_document(scene.objects.front().document, quader_poly::SurfaceShading::Flat);
			const std::vector<quader::QVec3> positions = compiled_positions(mesh);
			quader_editor_core::OverlayVisibilityOccluder occluder;
			occluder.set_mesh(positions, mesh.indices);
			const quader_editor_core::ViewportCameraState camera = benchmark_camera();
			std::uint64_t visible_count = 0;
			for (int iteration = 0; iteration < options.visibility_iterations; ++iteration) {
				const float x = static_cast<float>((iteration % 53) - 26);
				const float z = static_cast<float>(((iteration / 53) % 53) - 26);
				if (occluder.line_segment_is_visible({ x, 0.05F, z }, { x + 1.0F, 0.05F, z + 1.0F }, camera)) {
					++visible_count;
				}
			}
			return visible_count + positions.size() + mesh.indices.size();
		}));

	return results;
}

void write_json(const BenchmarkOptions& options, const BenchmarkScene& scene, const std::vector<BenchmarkResult>& results)
{
	if (options.output_path.empty()) {
		return;
	}
	const std::filesystem::path parent = options.output_path.parent_path();
	if (!parent.empty()) {
		std::filesystem::create_directories(parent);
	}
	std::ofstream stream(options.output_path);
	if (!stream) {
		throw std::runtime_error("Could not open benchmark output: " + options.output_path.string());
	}

	stream << std::fixed << std::setprecision(4);
	stream << "{\n";
	stream << "  \"schema\": \"qdr-performance-baseline-v1\",\n";
	stream << "  \"tier\": \"" << json_escape(options.tier) << "\",\n";
	stream << "  \"requestedTriangles\": " << options.requested_triangles << ",\n";
	stream << "  \"v1TargetTriangles\": " << v1_target_triangles << ",\n";
	stream << "  \"frameBudgetMs60Hz\": " << frame_budget_60_hz_ms << ",\n";
	stream << "  \"frameBudgetMs120Hz\": " << frame_budget_120_hz_ms << ",\n";
	stream << "  \"scene\": {\n";
	stream << "    \"objects\": " << scene.stats.objects << ",\n";
	stream << "    \"documents\": " << scene.stats.documents << ",\n";
	stream << "    \"vertices\": " << scene.stats.vertices << ",\n";
	stream << "    \"faces\": " << scene.stats.faces << ",\n";
	stream << "    \"triangles\": " << scene.stats.triangles << ",\n";
	stream << "    \"selectedFaces\": " << scene.stats.selected_faces << "\n";
	stream << "  },\n";
	stream << "  \"benchmarks\": [\n";
	for (std::size_t index = 0; index < results.size(); ++index) {
		const BenchmarkResult& result = results[index];
		stream << "    {\n";
		stream << "      \"name\": \"" << json_escape(result.name) << "\",\n";
		stream << "      \"unit\": \"" << json_escape(result.unit) << "\",\n";
		stream << "      \"iterations\": " << result.iterations << ",\n";
		stream << "      \"elapsedMs\": " << result.elapsed_ms << ",\n";
		stream << "      \"averageMs\": " << result.average_ms << ",\n";
		stream << "      \"workItems\": " << result.work_items << ",\n";
		stream << "      \"checksum\": " << result.checksum << "\n";
		stream << "    }" << (index + 1U == results.size() ? "\n" : ",\n");
	}
	stream << "  ]\n";
	stream << "}\n";
}

void print_results(const BenchmarkOptions& options, const BenchmarkScene& scene, const std::vector<BenchmarkResult>& results)
{
	std::cout << "Quader performance baseline\n";
	std::cout << "  tier: " << options.tier << "\n";
	std::cout << "  requested triangles: " << options.requested_triangles << "\n";
	std::cout << "  actual triangles: " << scene.stats.triangles << "\n";
	std::cout << "  objects: " << scene.stats.objects << "\n";
	std::cout << "  v1 target triangles: " << v1_target_triangles << "\n";
	std::cout << "  frame budgets: 60Hz=" << std::fixed << std::setprecision(3) << frame_budget_60_hz_ms
		<< "ms, 120Hz=" << frame_budget_120_hz_ms << "ms\n\n";
	std::cout << std::left << std::setw(44) << "benchmark"
		<< std::right << std::setw(12) << "elapsed ms"
		<< std::setw(12) << "avg ms"
		<< std::setw(14) << "iterations"
		<< std::setw(16) << "work items"
		<< "\n";
	for (const BenchmarkResult& result : results) {
		std::cout << std::left << std::setw(44) << result.name
			<< std::right << std::setw(12) << std::fixed << std::setprecision(3) << result.elapsed_ms
			<< std::setw(12) << result.average_ms
			<< std::setw(14) << result.iterations
			<< std::setw(16) << result.work_items
			<< "\n";
	}
	if (!options.output_path.empty()) {
		std::cout << "\nWrote JSON: " << options.output_path.string() << "\n";
	}
}

} // namespace

int main(int argc, char** argv)
{
	try {
		const BenchmarkOptions options = parse_args(argc, argv);
		const BenchmarkScene scene = make_scene(options);
		const std::vector<BenchmarkResult> results = run_benchmarks(options, scene);
		write_json(options, scene, results);
		print_results(options, scene, results);
		return 0;
	}
	catch (const std::exception& exception) {
		std::cerr << "qdr_performance_baseline failed: " << exception.what() << "\n";
		return 2;
	}
}
