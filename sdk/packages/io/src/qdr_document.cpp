#include <quader_io/qdr_document.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace quader_io {
namespace {

using json = nlohmann::ordered_json;

std::string string_from_view(std::string_view value)
{
	return std::string(value.data(), value.size());
}

std::string qdr_format_name_string()
{
	return string_from_view(qdr_format_name);
}

void require_root_object(const json& root)
{
	if (!root.is_object()) {
		throw std::runtime_error("QDR document root must be an object.");
	}
}

std::string read_format_header(const json& root)
{
	const auto iterator = root.find("format");
	if (iterator == root.end()) {
		throw std::runtime_error("QDR document is missing its format header.");
	}
	if (!iterator->is_string()) {
		throw std::runtime_error("QDR document format header must be a string.");
	}
	return iterator->get<std::string>();
}

int read_version_header(const json& root)
{
	const auto iterator = root.find("version");
	if (iterator == root.end()) {
		throw std::runtime_error("QDR document is missing its version header.");
	}
	if (!iterator->is_number_integer()) {
		throw std::runtime_error("QDR document version header must be an integer.");
	}
	if (iterator->is_number_unsigned()) {
		const auto value = iterator->get<unsigned long long>();
		if (value > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
			throw std::runtime_error("QDR document version header is outside the supported integer range.");
		}
		return static_cast<int>(value);
	}
	const auto value = iterator->get<long long>();
	if (value < static_cast<long long>(std::numeric_limits<int>::min()) ||
		value > static_cast<long long>(std::numeric_limits<int>::max())) {
		throw std::runtime_error("QDR document version header is outside the supported integer range.");
	}
	return static_cast<int>(value);
}

std::string selection_mode_name(QdrSelectionMode mode)
{
	switch (mode) {
	case QdrSelectionMode::vertex:
		return "vertex";
	case QdrSelectionMode::edge:
		return "edge";
	case QdrSelectionMode::face:
		return "face";
	case QdrSelectionMode::object:
		return "object";
	}
	return "object";
}

std::string light_type_name(QdrLightType type)
{
	switch (type) {
	case QdrLightType::directional:
		return "directional";
	case QdrLightType::point:
		return "point";
	case QdrLightType::spot:
		return "spot";
	}
	return "point";
}

std::string light_shadow_mode_name(QdrLightShadowMode mode)
{
	switch (mode) {
	case QdrLightShadowMode::none:
		return "none";
	case QdrLightShadowMode::hard:
		return "hard";
	case QdrLightShadowMode::soft:
		return "soft";
	}
	return "soft";
}

std::string component_selection_mode_name(quader_poly::SelectionMode mode)
{
	switch (mode) {
	case quader_poly::SelectionMode::vertex:
		return "vertex";
	case quader_poly::SelectionMode::edge:
		return "edge";
	case quader_poly::SelectionMode::face:
		return "face";
	}
	return "vertex";
}

std::string element_kind_name(quader_poly::ElementKind kind)
{
	switch (kind) {
	case quader_poly::ElementKind::vertex:
		return "vertex";
	case quader_poly::ElementKind::edge:
		return "edge";
	case quader_poly::ElementKind::face:
		return "face";
	}
	return "vertex";
}

std::string surface_shading_name(quader_poly::SurfaceShading shading)
{
	switch (shading) {
	case quader_poly::SurfaceShading::flat:
		return "flat";
	case quader_poly::SurfaceShading::smooth:
		return "smooth";
	case quader_poly::SurfaceShading::authored:
		break;
	}
	return "flat";
}

QdrSelectionMode parse_selection_mode(const std::string& value)
{
	if (value == "vertex") {
		return QdrSelectionMode::vertex;
	}
	if (value == "edge") {
		return QdrSelectionMode::edge;
	}
	if (value == "face") {
		return QdrSelectionMode::face;
	}
	if (value == "object") {
		return QdrSelectionMode::object;
	}
	throw std::runtime_error("Unknown selection mode: " + value);
}

QdrLightType parse_light_type(const std::string& value)
{
	if (value == "directional") {
		return QdrLightType::directional;
	}
	if (value == "point") {
		return QdrLightType::point;
	}
	if (value == "spot") {
		return QdrLightType::spot;
	}
	throw std::runtime_error("Unknown light type: " + value);
}

QdrLightShadowMode parse_light_shadow_mode(const std::string& value)
{
	if (value == "none") {
		return QdrLightShadowMode::none;
	}
	if (value == "hard") {
		return QdrLightShadowMode::hard;
	}
	if (value == "soft") {
		return QdrLightShadowMode::soft;
	}
	throw std::runtime_error("Unknown light shadow mode: " + value);
}

quader_poly::SelectionMode parse_component_selection_mode(const std::string& value)
{
	if (value == "vertex") {
		return quader_poly::SelectionMode::vertex;
	}
	if (value == "edge") {
		return quader_poly::SelectionMode::edge;
	}
	if (value == "face") {
		return quader_poly::SelectionMode::face;
	}
	throw std::runtime_error("Unknown component selection mode: " + value);
}

quader_poly::ElementKind parse_element_kind(const std::string& value)
{
	if (value == "vertex") {
		return quader_poly::ElementKind::vertex;
	}
	if (value == "edge") {
		return quader_poly::ElementKind::edge;
	}
	if (value == "face") {
		return quader_poly::ElementKind::face;
	}
	throw std::runtime_error("Unknown active element kind: " + value);
}

quader_poly::SurfaceShading parse_surface_shading(const std::string& value)
{
	if (value == "flat") {
		return quader_poly::SurfaceShading::flat;
	}
	if (value == "smooth") {
		return quader_poly::SurfaceShading::smooth;
	}
	throw std::runtime_error("Unknown face normal shading: " + value);
}

json to_json(quader_poly::Vec2 value)
{
	return json {
		{ "x", value.x },
		{ "y", value.y },
	};
}

json to_json(quader_poly::Vec3 value)
{
	return json {
		{ "x", value.x },
		{ "y", value.y },
		{ "z", value.z },
	};
}

float color_channel_to_unit(std::uint32_t color_abgr, int shift)
{
	const std::uint32_t value = (color_abgr >> static_cast<std::uint32_t>(shift)) & 0xFFU;
	return static_cast<float>(value) / 255.0F;
}

std::uint32_t color_channel_from_unit(float value)
{
	if (!std::isfinite(value)) {
		value = 1.0F;
	}
	value = std::clamp(value, 0.0F, 1.0F);
	return static_cast<std::uint32_t>(std::round(value * 255.0F));
}

json color_to_json(std::uint32_t color_abgr)
{
	return json {
		{ "r", color_channel_to_unit(color_abgr, 0) },
		{ "g", color_channel_to_unit(color_abgr, 8) },
		{ "b", color_channel_to_unit(color_abgr, 16) },
		{ "a", color_channel_to_unit(color_abgr, 24) },
	};
}

json to_json(quader_poly::Edge edge)
{
	return json {
		{ "a", edge.a },
		{ "b", edge.b },
	};
}

quader_poly::Vec2 vec2_from_json(const json& value)
{
	return {
		value.at("x").get<float>(),
		value.at("y").get<float>(),
	};
}

quader_poly::Vec3 vec3_from_json(const json& value)
{
	return {
		value.at("x").get<float>(),
		value.at("y").get<float>(),
		value.at("z").get<float>(),
	};
}

std::uint32_t color_from_json(const json& value)
{
	return
		color_channel_from_unit(value.at("r").get<float>()) |
		(color_channel_from_unit(value.at("g").get<float>()) << 8U) |
		(color_channel_from_unit(value.at("b").get<float>()) << 16U) |
		(color_channel_from_unit(value.value("a", 1.0F)) << 24U);
}

quader_poly::Edge edge_from_json(const json& value)
{
	return quader_poly::make_edge(
		value.at("a").get<quader_poly::ElementId>(),
		value.at("b").get<quader_poly::ElementId>());
}

bool contains_duplicate_ids(std::vector<quader_poly::ElementId> ids)
{
	std::sort(ids.begin(), ids.end());
	return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

json document_to_json(const quader_poly::Document& document)
{
	json vertices = json::array();
	for (const quader_poly::Vertex& vertex : document.vertices) {
		vertices.push_back({
			{ "id", vertex.id },
			{ "position", to_json(vertex.position) },
		});
	}

	json faces = json::array();
	for (const quader_poly::Face& face : document.faces) {
		json uvs = json::array();
		for (quader_poly::Vec2 uv : face.uvs) {
			uvs.push_back(to_json(uv));
		}

		faces.push_back({
			{ "id", face.id },
			{ "vertices", face.vertices },
			{ "uvs", uvs },
			{ "materialSlot", face.material_slot },
			{ "normalShading", surface_shading_name(face.normal_shading) },
		});
	}

	const std::vector<quader_poly::Edge> document_edges = quader_poly::document_edges(document);
	json hard_normal_edges = json::array();
	std::set<std::pair<quader_poly::ElementId, quader_poly::ElementId>> serialized_hard_edges;
	for (quader_poly::Edge edge : document.hard_normal_edges) {
		edge = quader_poly::make_edge(edge.a, edge.b);
		if (std::find(document_edges.begin(), document_edges.end(), edge) == document_edges.end()) {
			continue;
		}
		if (!serialized_hard_edges.insert({ edge.a, edge.b }).second) {
			continue;
		}
		hard_normal_edges.push_back(to_json(edge));
	}

	return {
		{ "nextVertexId", document.next_vertex_id },
		{ "nextFaceId", document.next_face_id },
		{ "vertices", vertices },
		{ "faces", faces },
		{ "hardNormalEdges", hard_normal_edges },
	};
}

json selection_to_json(const quader_poly::Selection& selection)
{
	json edges = json::array();
	for (quader_poly::Edge edge : selection.edges) {
		edges.push_back(to_json(edge));
	}

	return {
		{ "mode", component_selection_mode_name(selection.mode) },
		{ "vertices", selection.vertices },
		{ "edges", edges },
		{ "faces", selection.faces },
		{ "active", {
			{ "hasActive", selection.has_active },
			{ "kind", element_kind_name(selection.active_kind) },
			{ "vertex", selection.active_vertex },
			{ "edge", to_json(selection.active_edge) },
			{ "face", selection.active_face },
		} },
	};
}

json object_to_json(const QdrObject& object)
{
	return {
		{ "id", object.id },
		{ "materialId", object.material_id },
		{ "selected", object.selected },
		{ "document", document_to_json(object.document) },
		{ "selection", selection_to_json(object.selection) },
	};
}

json light_to_json(const QdrLight& light)
{
	return {
		{ "id", light.id },
		{ "name", light.name },
		{ "enabled", light.enabled },
		{ "type", light_type_name(light.type) },
		{ "selected", light.selected },
		{ "position", to_json(light.position) },
		{ "direction", to_json(light.direction) },
		{ "color", color_to_json(light.color_abgr) },
		{ "intensity", light.intensity },
		{ "shadowMode", light_shadow_mode_name(light.shadow_mode) },
		{ "shadowStrength", light.shadow_strength },
		{ "shadowBias", light.shadow_bias },
		{ "shadowNormalBias", light.shadow_normal_bias },
		{ "shadowNearPlane", light.shadow_near_plane },
		{ "range", light.range },
		{ "spotAngle", light.spot_angle },
		{ "innerSpotAngle", light.inner_spot_angle },
	};
}

bool face_references_existing_vertices(const quader_poly::Document& document, const quader_poly::Face& face)
{
	return std::all_of(face.vertices.begin(), face.vertices.end(), [&document](quader_poly::ElementId id) {
		return quader_poly::find_vertex(document, id) != nullptr;
	});
}

quader_poly::Document document_from_json(const json& value)
{
	quader_poly::Document document;
	document.next_vertex_id = value.at("nextVertexId").get<quader_poly::ElementId>();
	document.next_face_id = value.at("nextFaceId").get<quader_poly::ElementId>();

	std::set<quader_poly::ElementId> vertex_ids;
	quader_poly::ElementId max_vertex_id = 0;
	for (const json& vertex_json : value.at("vertices")) {
		quader_poly::Vertex vertex;
		vertex.id = vertex_json.at("id").get<quader_poly::ElementId>();
		vertex.position = vec3_from_json(vertex_json.at("position"));
		if (vertex.id == quader_poly::invalid_element_id || !vertex_ids.insert(vertex.id).second) {
			throw std::runtime_error("Document contains an invalid or duplicate vertex id.");
		}
		max_vertex_id = std::max(max_vertex_id, vertex.id);
		document.vertices.push_back(vertex);
	}

	std::set<quader_poly::ElementId> face_ids;
	quader_poly::ElementId max_face_id = 0;
	for (const json& face_json : value.at("faces")) {
		quader_poly::Face face;
		face.id = face_json.at("id").get<quader_poly::ElementId>();
		face.vertices = face_json.at("vertices").get<std::vector<quader_poly::ElementId>>();
		face.material_slot = face_json.value("materialSlot", 0U);
		face.normal_shading = parse_surface_shading(face_json.value("normalShading", std::string("flat")));
		for (const json& uv_json : face_json.at("uvs")) {
			face.uvs.push_back(vec2_from_json(uv_json));
		}
		if (face.id == quader_poly::invalid_element_id || !face_ids.insert(face.id).second) {
			throw std::runtime_error("Document contains an invalid or duplicate face id.");
		}
		if (face.vertices.size() < 3U) {
			throw std::runtime_error("Document contains a face with fewer than three vertices.");
		}
		if (contains_duplicate_ids(face.vertices)) {
			throw std::runtime_error("Document contains a face with duplicate vertex ids.");
		}
		if (!face.uvs.empty() && face.uvs.size() != face.vertices.size()) {
			throw std::runtime_error("Document contains a face whose UV count does not match its vertex count.");
		}
		document.faces.push_back(std::move(face));
		max_face_id = std::max(max_face_id, document.faces.back().id);
	}

	for (const quader_poly::Face& face : document.faces) {
		if (!face_references_existing_vertices(document, face)) {
			throw std::runtime_error("Document contains a face that references a missing vertex.");
		}
	}

	std::set<std::pair<quader_poly::ElementId, quader_poly::ElementId>> hard_normal_edges;
	if (const auto hard_edges_json = value.find("hardNormalEdges"); hard_edges_json != value.end()) {
		const std::vector<quader_poly::Edge> valid_edges = quader_poly::document_edges(document);
		for (const json& edge_json : *hard_edges_json) {
			quader_poly::Edge edge = edge_from_json(edge_json);
			if (std::find(valid_edges.begin(), valid_edges.end(), edge) == valid_edges.end()) {
				throw std::runtime_error("Document contains a hard normal edge that is not part of the mesh.");
			}
			if (!hard_normal_edges.insert({ edge.a, edge.b }).second) {
				throw std::runtime_error("Document contains duplicate hard normal edges.");
			}
			document.hard_normal_edges.push_back(edge);
		}
	}

	document.next_vertex_id = std::max(document.next_vertex_id, max_vertex_id + 1U);
	document.next_face_id = std::max(document.next_face_id, max_face_id + 1U);
	return document;
}

quader_poly::Selection selection_from_json(const json& value)
{
	quader_poly::Selection selection;
	selection.mode = parse_component_selection_mode(value.at("mode").get<std::string>());
	selection.vertices = value.at("vertices").get<std::vector<quader_poly::ElementId>>();
	for (const json& edge_json : value.at("edges")) {
		selection.edges.push_back(edge_from_json(edge_json));
	}
	selection.faces = value.at("faces").get<std::vector<quader_poly::ElementId>>();

	const json& active = value.at("active");
	selection.has_active = active.at("hasActive").get<bool>();
	selection.active_kind = parse_element_kind(active.at("kind").get<std::string>());
	selection.active_vertex = active.at("vertex").get<quader_poly::ElementId>();
	selection.active_edge = edge_from_json(active.at("edge"));
	selection.active_face = active.at("face").get<quader_poly::ElementId>();
	return selection;
}

void validate_selection(const quader_poly::Document& document, const quader_poly::Selection& selection)
{
	if (contains_duplicate_ids(selection.vertices)) {
		throw std::runtime_error("Selection contains duplicate vertex ids.");
	}
	if (contains_duplicate_ids(selection.faces)) {
		throw std::runtime_error("Selection contains duplicate face ids.");
	}

	for (quader_poly::ElementId vertex_id : selection.vertices) {
		if (quader_poly::find_vertex(document, vertex_id) == nullptr) {
			throw std::runtime_error("Selection contains a missing vertex id.");
		}
	}

	std::set<std::pair<quader_poly::ElementId, quader_poly::ElementId>> edge_ids;
	for (quader_poly::Edge edge : selection.edges) {
		if (edge.a == quader_poly::invalid_element_id || edge.b == quader_poly::invalid_element_id || edge.a == edge.b) {
			throw std::runtime_error("Selection contains an invalid edge id.");
		}
		if (!edge_ids.insert({ edge.a, edge.b }).second) {
			throw std::runtime_error("Selection contains duplicate edge ids.");
		}
		if (quader_poly::find_vertex(document, edge.a) == nullptr || quader_poly::find_vertex(document, edge.b) == nullptr) {
			throw std::runtime_error("Selection contains an edge with a missing vertex id.");
		}
	}
	for (quader_poly::ElementId face_id : selection.faces) {
		if (quader_poly::find_face(document, face_id) == nullptr) {
			throw std::runtime_error("Selection contains a missing face id.");
		}
	}
	if (!selection.has_active) {
		return;
	}
	switch (selection.active_kind) {
	case quader_poly::ElementKind::vertex:
		if (quader_poly::find_vertex(document, selection.active_vertex) == nullptr) {
			throw std::runtime_error("Selection active vertex is missing.");
		}
		break;
	case quader_poly::ElementKind::edge:
		if (selection.active_edge.a == quader_poly::invalid_element_id ||
			selection.active_edge.b == quader_poly::invalid_element_id ||
			selection.active_edge.a == selection.active_edge.b ||
			quader_poly::find_vertex(document, selection.active_edge.a) == nullptr ||
			quader_poly::find_vertex(document, selection.active_edge.b) == nullptr) {
			throw std::runtime_error("Selection active edge contains a missing or invalid vertex id.");
		}
		break;
	case quader_poly::ElementKind::face:
		if (quader_poly::find_face(document, selection.active_face) == nullptr) {
			throw std::runtime_error("Selection active face is missing.");
		}
		break;
	}
}

QdrObject object_from_json(const json& value)
{
	QdrObject object;
	object.id = value.at("id").get<int>();
	object.material_id = value.at("materialId").get<int>();
	object.selected = value.at("selected").get<bool>();
	object.document = document_from_json(value.at("document"));
	object.selection = selection_from_json(value.at("selection"));
	if (object.id <= 0) {
		throw std::runtime_error("QDR document contains an invalid object id.");
	}
	validate_selection(object.document, object.selection);
	return object;
}

QdrLight light_from_json(const json& value)
{
	QdrLight light;
	light.id = value.at("id").get<int>();
	light.name = value.value("name", std::string());
	light.enabled = value.at("enabled").get<bool>();
	light.type = parse_light_type(value.at("type").get<std::string>());
	light.selected = value.value("selected", false);
	light.position = vec3_from_json(value.at("position"));
	light.direction = vec3_from_json(value.at("direction"));
	light.color_abgr = color_from_json(value.at("color"));
	light.intensity = value.at("intensity").get<float>();
	light.shadow_mode = parse_light_shadow_mode(value.at("shadowMode").get<std::string>());
	light.shadow_strength = value.at("shadowStrength").get<float>();
	light.shadow_bias = value.at("shadowBias").get<float>();
	light.shadow_normal_bias = value.at("shadowNormalBias").get<float>();
	light.shadow_near_plane = value.at("shadowNearPlane").get<float>();
	light.range = value.at("range").get<float>();
	light.spot_angle = value.at("spotAngle").get<float>();
	light.inner_spot_angle = value.at("innerSpotAngle").get<float>();
	if (light.id <= 0) {
		throw std::runtime_error("QDR document contains an invalid light id.");
	}
	if (!std::isfinite(light.position.x) || !std::isfinite(light.position.y) || !std::isfinite(light.position.z) ||
		!std::isfinite(light.direction.x) || !std::isfinite(light.direction.y) || !std::isfinite(light.direction.z)) {
		throw std::runtime_error("QDR document contains a light with non-finite transform data.");
	}
	if (!std::isfinite(light.intensity) || !std::isfinite(light.shadow_strength) ||
		!std::isfinite(light.shadow_bias) || !std::isfinite(light.shadow_normal_bias) ||
		!std::isfinite(light.shadow_near_plane) || !std::isfinite(light.range) ||
		!std::isfinite(light.spot_angle) || !std::isfinite(light.inner_spot_angle)) {
		throw std::runtime_error("QDR document contains a light with non-finite scalar data.");
	}
	return light;
}

QdrDocument qdr_document_from_json(const json& value)
{
	QdrDocument document;
	document.next_object_id = value.at("nextObjectId").get<int>();
	document.next_light_id = value.value("nextLightId", 1);
	document.active_object_id = value.at("activeObjectId").get<int>();
	document.active_light_id = value.value("activeLightId", -1);
	document.selection_mode = parse_selection_mode(value.at("selectionMode").get<std::string>());
	document.content_revision = value.value("contentRevision", static_cast<std::uint64_t>(1));
	document.selection_revision = value.value("selectionRevision", static_cast<std::uint64_t>(0));

	if (document.next_object_id <= 0) {
		throw std::runtime_error("QDR document contains an invalid next object id.");
	}
	if (document.next_light_id <= 0) {
		throw std::runtime_error("QDR document contains an invalid next light id.");
	}
	if (document.active_object_id == 0 || document.active_object_id < -1) {
		throw std::runtime_error("QDR document contains an invalid active object id.");
	}
	if (document.active_light_id == 0 || document.active_light_id < -1) {
		throw std::runtime_error("QDR document contains an invalid active light id.");
	}

	std::set<int> object_ids;
	int max_object_id = 0;
	for (const json& object_json : value.at("objects")) {
		QdrObject object = object_from_json(object_json);
		if (!object_ids.insert(object.id).second) {
			throw std::runtime_error("QDR document contains a duplicate object id.");
		}
		max_object_id = std::max(max_object_id, object.id);
		document.objects.push_back(std::move(object));
	}

	if (document.objects.empty()) {
		throw std::runtime_error("QDR document must contain at least one object.");
	}
	if (document.active_object_id > 0 && object_ids.find(document.active_object_id) == object_ids.end()) {
		throw std::runtime_error("QDR document active object id is missing.");
	}
	document.next_object_id = std::max(document.next_object_id, max_object_id + 1);

	std::set<int> light_ids;
	int max_light_id = 0;
	if (const auto lights_json = value.find("lights"); lights_json != value.end()) {
		for (const json& light_json : *lights_json) {
			QdrLight light = light_from_json(light_json);
			if (!light_ids.insert(light.id).second) {
				throw std::runtime_error("QDR document contains a duplicate light id.");
			}
			max_light_id = std::max(max_light_id, light.id);
			document.lights.push_back(std::move(light));
		}
	}
	if (document.active_light_id > 0 && light_ids.find(document.active_light_id) == light_ids.end()) {
		throw std::runtime_error("QDR document active light id is missing.");
	}
	document.next_light_id = std::max(document.next_light_id, max_light_id + 1);
	return document;
}

const json& document_payload_from_root(const json& root)
{
	if (root.contains(string_from_view(qdr_document_payload_key))) {
		return root.at(string_from_view(qdr_document_payload_key));
	}
	if (root.contains(string_from_view(qdr_legacy_session_payload_key))) {
		return root.at(string_from_view(qdr_legacy_session_payload_key));
	}
	throw std::runtime_error("QDR document is missing its document payload.");
}

const json& migrate_document_payload_to_current(const json& root, int version)
{
	switch (version) {
	case 1:
	case 2:
	case 3:
		return document_payload_from_root(root);
	default:
		throw std::runtime_error("Unsupported Quader file version: " + std::to_string(version));
	}
}

std::string system_error_message(const std::string& action, const std::error_code& error)
{
	if (!error) {
		return action;
	}
	return action + ": " + error.message();
}

std::string windows_error_message(const std::string& action, unsigned long error_code)
{
	std::error_code error(static_cast<int>(error_code), std::system_category());
	return system_error_message(action, error);
}

std::string unique_temp_suffix(std::uint64_t attempt)
{
	static std::atomic_uint64_t counter { 0 };
	const std::uint64_t tick =
		static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	const std::uint64_t value = counter.fetch_add(1, std::memory_order_relaxed) ^ tick;
	std::ostringstream stream;
	stream << std::hex << value << "-" << attempt;
	return stream.str();
}

std::filesystem::path target_directory_for_atomic_write(const std::filesystem::path& target)
{
	const std::filesystem::path directory = target.parent_path();
	return directory.empty() ? std::filesystem::path(".") : directory;
}

std::filesystem::path make_temp_path_for_atomic_write(
	const std::filesystem::path& target,
	const std::filesystem::path& directory,
	std::uint64_t attempt)
{
	const std::string file_name = target.filename().string();
	return directory / ("." + file_name + "." + unique_temp_suffix(attempt) + ".tmp");
}

QdrWriteResult write_text_file(const std::filesystem::path& temp_path, std::string_view text)
{
	std::ofstream stream(temp_path, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!stream.is_open()) {
		return { false, "Could not create temporary QDR file: " + temp_path.string() };
	}

	stream.write(text.data(), static_cast<std::streamsize>(text.size()));
	if (!stream.good()) {
		return { false, "Could not write temporary QDR file: " + temp_path.string() };
	}

	stream.flush();
	if (!stream.good()) {
		return { false, "Could not flush temporary QDR file: " + temp_path.string() };
	}

	stream.close();
	if (!stream.good()) {
		return { false, "Could not close temporary QDR file: " + temp_path.string() };
	}

	return { true, {} };
}

QdrWriteResult replace_with_temp_file(const std::filesystem::path& temp_path, const std::filesystem::path& target)
{
#ifdef _WIN32
	if (::MoveFileExW(
			temp_path.c_str(),
			target.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
		return { false, windows_error_message("Could not replace QDR file", ::GetLastError()) };
	}
	return { true, {} };
#else
	std::error_code error;
	std::filesystem::rename(temp_path, target, error);
	if (error) {
		return { false, system_error_message("Could not replace QDR file", error) };
	}
	return { true, {} };
#endif
}

void delete_temp_file_best_effort(const std::filesystem::path& temp_path)
{
	std::error_code ignored;
	std::filesystem::remove(temp_path, ignored);
}

QdrWriteResult write_text_atomic(const std::filesystem::path& target, std::string_view text)
{
	if (target.empty() || target.filename().empty()) {
		return { false, "QDR file path is empty." };
	}

	const std::filesystem::path directory = target_directory_for_atomic_write(target);
	std::error_code error;
	if (!std::filesystem::is_directory(directory, error)) {
		return { false, system_error_message("QDR output directory does not exist", error) };
	}

	constexpr std::uint64_t max_attempts = 32;
	for (std::uint64_t attempt = 0; attempt < max_attempts; ++attempt) {
		const std::filesystem::path temp_path = make_temp_path_for_atomic_write(target, directory, attempt);
		if (std::filesystem::exists(temp_path, error)) {
			continue;
		}
		if (error) {
			return { false, system_error_message("Could not inspect temporary QDR file path", error) };
		}

		QdrWriteResult write_result = write_text_file(temp_path, text);
		if (!write_result.ok) {
			delete_temp_file_best_effort(temp_path);
			return write_result;
		}

		QdrWriteResult replace_result = replace_with_temp_file(temp_path, target);
		if (!replace_result.ok) {
			delete_temp_file_best_effort(temp_path);
			return replace_result;
		}

		return { true, {} };
	}

	return { false, "Could not allocate a unique temporary QDR file path." };
}

} // namespace

bool is_supported_document_version(int version)
{
	return version >= qdr_min_supported_format_version && version <= qdr_format_version;
}

std::string serialize_document(const QdrDocument& document)
{
	json objects = json::array();
	for (const QdrObject& object : document.objects) {
		objects.push_back(object_to_json(object));
	}

	json lights = json::array();
	for (const QdrLight& light : document.lights) {
		lights.push_back(light_to_json(light));
	}

	json root {
		{ "format", qdr_format_name_string() },
		{ "version", qdr_format_version },
		{ string_from_view(qdr_document_payload_key), {
			{ "nextObjectId", document.next_object_id },
			{ "nextLightId", document.next_light_id },
			{ "activeObjectId", document.active_object_id },
			{ "activeLightId", document.active_light_id },
			{ "selectionMode", selection_mode_name(document.selection_mode) },
			{ "contentRevision", document.content_revision },
			{ "selectionRevision", document.selection_revision },
			{ "objects", objects },
			{ "lights", lights },
		} },
	};
	return root.dump(2);
}

QdrReadResult deserialize_document(std::string_view text)
{
	try {
		const json root = json::parse(text.begin(), text.end());
		require_root_object(root);
		const std::string format = read_format_header(root);
		if (format != qdr_format_name_string()) {
			return { false, {}, "Unsupported Quader file format: " + format };
		}

		const int version = read_version_header(root);
		if (!is_supported_document_version(version)) {
			return { false, {}, "Unsupported Quader file version: " + std::to_string(version) };
		}

		QdrDocument document = qdr_document_from_json(migrate_document_payload_to_current(root, version));
		return { true, std::move(document), {} };
	} catch (const std::exception& exception) {
		return { false, {}, exception.what() };
	} catch (...) {
		return { false, {}, "Unknown error reading Quader file." };
	}
}

QdrWriteResult write_document_atomic(const std::filesystem::path& path, const QdrDocument& document)
{
	const std::string text = serialize_document(document);
	const QdrReadResult validation = deserialize_document(text);
	if (!validation.ok) {
		return { false, validation.message.empty() ? "Serialized QDR document failed validation." : validation.message };
	}
	return write_text_atomic(path, serialize_document(validation.document));
}

} // namespace quader_io
