////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <io/qdr/qdr_document.hpp>

#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_polygon_document_codec.hpp"
#include "io/qdr/qdr_root.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader_io {
namespace {

using Json = nlohmann::ordered_json;

QdrReadResult read_error_result(QdrDiagnostic diagnostic) {
  const std::string message = diagnostic.message;
  return {false, {}, message, {std::move(diagnostic)}};
}

QdrReadResult read_error_result(std::string_view code, std::string_view path,
                                std::string message) {
  return read_error_result(qdr_make_error(code, path, std::move(message)));
}

std::string qdr_format_name_string()
{
  return qdr_string_from_view(kQdrFormatName);
}

std::string selection_mode_name(QdrSelectionMode mode)
{
	switch (mode) {
        case QdrSelectionMode::Vertex:
          return "vertex";
        case QdrSelectionMode::Edge:
          return "edge";
        case QdrSelectionMode::Face:
          return "face";
        case QdrSelectionMode::Object:
          return "object";
	}
	return "object";
}

std::string light_type_name(QdrLightType type)
{
	switch (type) {
        case QdrLightType::Directional:
          return "directional";
        case QdrLightType::Point:
          return "point";
        case QdrLightType::Spot:
          return "spot";
	}
	return "point";
}

std::string light_shadow_mode_name(QdrLightShadowMode mode)
{
	switch (mode) {
        case QdrLightShadowMode::None:
          return "none";
        case QdrLightShadowMode::Hard:
          return "hard";
        case QdrLightShadowMode::Soft:
          return "soft";
	}
	return "soft";
}

std::string component_selection_mode_name(quader_poly::SelectionMode mode)
{
	switch (mode) {
        case quader_poly::SelectionMode::Vertex:
          return "vertex";
        case quader_poly::SelectionMode::Edge:
          return "edge";
        case quader_poly::SelectionMode::Face:
          return "face";
	}
	return "vertex";
}

std::string element_kind_name(quader_poly::ElementKind kind)
{
	switch (kind) {
        case quader_poly::ElementKind::Vertex:
          return "vertex";
        case quader_poly::ElementKind::Edge:
          return "edge";
        case quader_poly::ElementKind::Face:
          return "face";
	}
	return "vertex";
}

QdrSelectionMode parse_selection_mode(const std::string& value)
{
	if (value == "vertex") {
          return QdrSelectionMode::Vertex;
        }
	if (value == "edge") {
          return QdrSelectionMode::Edge;
        }
	if (value == "face") {
          return QdrSelectionMode::Face;
        }
	if (value == "object") {
          return QdrSelectionMode::Object;
        }
	throw std::runtime_error("Unknown selection mode: " + value);
}

QdrLightType parse_light_type(const std::string& value)
{
	if (value == "directional") {
          return QdrLightType::Directional;
        }
	if (value == "point") {
          return QdrLightType::Point;
        }
	if (value == "spot") {
          return QdrLightType::Spot;
        }
	throw std::runtime_error("Unknown light type: " + value);
}

QdrLightShadowMode parse_light_shadow_mode(const std::string& value)
{
	if (value == "none") {
          return QdrLightShadowMode::None;
        }
	if (value == "hard") {
          return QdrLightShadowMode::Hard;
        }
	if (value == "soft") {
          return QdrLightShadowMode::Soft;
        }
	throw std::runtime_error("Unknown light shadow mode: " + value);
}

quader_poly::SelectionMode parse_component_selection_mode(const std::string& value)
{
	if (value == "vertex") {
          return quader_poly::SelectionMode::Vertex;
        }
	if (value == "edge") {
          return quader_poly::SelectionMode::Edge;
        }
	if (value == "face") {
          return quader_poly::SelectionMode::Face;
        }
	throw std::runtime_error("Unknown component selection mode: " + value);
}

quader_poly::ElementKind parse_element_kind(const std::string& value)
{
	if (value == "vertex") {
          return quader_poly::ElementKind::Vertex;
        }
	if (value == "edge") {
          return quader_poly::ElementKind::Edge;
        }
	if (value == "face") {
          return quader_poly::ElementKind::Face;
        }
	throw std::runtime_error("Unknown active element kind: " + value);
}

Json to_json(quader::QVec3 value) {
  return Json{
      {"x", value.x},
      {"y", value.y},
      {"z", value.z},
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

Json color_to_json(std::uint32_t color_abgr) {
  return Json{
      {"r", color_channel_to_unit(color_abgr, 0)},
      {"g", color_channel_to_unit(color_abgr, 8)},
      {"b", color_channel_to_unit(color_abgr, 16)},
      {"a", color_channel_to_unit(color_abgr, 24)},
  };
}

Json to_json(quader_poly::Edge edge) {
  return Json{
      {"a", edge.a},
      {"b", edge.b},
  };
}

quader::QVec3 vec3_from_json(const Json &value) {
  return {
      value.at("x").get<float>(),
      value.at("y").get<float>(),
      value.at("z").get<float>(),
  };
}

std::uint32_t color_from_json(const Json &value) {
  return color_channel_from_unit(value.at("r").get<float>()) |
         (color_channel_from_unit(value.at("g").get<float>()) << 8U) |
         (color_channel_from_unit(value.at("b").get<float>()) << 16U) |
         (color_channel_from_unit(value.value("a", 1.0F)) << 24U);
}

quader_poly::Edge edge_from_json(const Json &value) {
  return quader_poly::make_edge(value.at("a").get<quader_poly::ElementId>(),
                                value.at("b").get<quader_poly::ElementId>());
}

bool contains_duplicate_ids(std::vector<quader_poly::ElementId> ids)
{
	std::sort(ids.begin(), ids.end());
	return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

Json selection_to_json(const quader_poly::Selection &selection) {
  Json edges = Json::array();
  for (quader_poly::Edge edge : selection.edges) {
    edges.push_back(to_json(edge));
  }

  return {
      {"mode", component_selection_mode_name(selection.mode)},
      {"vertices", selection.vertices},
      {"edges", edges},
      {"faces", selection.faces},
      {"active",
       {
           {"hasActive", selection.has_active},
           {"kind", element_kind_name(selection.active_kind)},
           {"vertex", selection.active_vertex},
           {"edge", to_json(selection.active_edge)},
           {"face", selection.active_face},
       }},
  };
}

Json object_to_json(const QdrObject &object) {
  return {
      {"id", object.id},
      {"materialId", object.material_id},
      {"selected", object.selected},
      {"document", qdr_polygon_document_to_json(object.document)},
      {"selection", selection_to_json(object.selection)},
  };
}

Json light_to_json(const QdrLight &light) {
  return {
      {"id", light.id},
      {"name", light.name},
      {"enabled", light.enabled},
      {"type", light_type_name(light.type)},
      {"selected", light.selected},
      {"position", to_json(light.position)},
      {"direction", to_json(light.direction)},
      {"color", color_to_json(light.color_abgr)},
      {"intensity", light.intensity},
      {"shadowMode", light_shadow_mode_name(light.shadow_mode)},
      {"shadowStrength", light.shadow_strength},
      {"shadowBias", light.shadow_bias},
      {"shadowNormalBias", light.shadow_normal_bias},
      {"shadowNearPlane", light.shadow_near_plane},
      {"range", light.range},
      {"spotAngle", light.spot_angle},
      {"innerSpotAngle", light.inner_spot_angle},
  };
}

quader_poly::Selection selection_from_json(const Json &value) {
  quader_poly::Selection selection;
  selection.mode =
      parse_component_selection_mode(value.at("mode").get<std::string>());
  selection.vertices =
      value.at("vertices").get<std::vector<quader_poly::ElementId>>();
  for (const Json &edge_json : value.at("edges")) {
    selection.edges.push_back(edge_from_json(edge_json));
  }
  selection.faces =
      value.at("faces").get<std::vector<quader_poly::ElementId>>();

  const Json &active = value.at("active");
  selection.has_active = active.at("hasActive").get<bool>();
  selection.active_kind =
      parse_element_kind(active.at("kind").get<std::string>());
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
          if (edge.a == quader_poly::kInvalidElementId ||
              edge.b == quader_poly::kInvalidElementId || edge.a == edge.b) {
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
        case quader_poly::ElementKind::Vertex:
          if (quader_poly::find_vertex(document, selection.active_vertex) ==
              nullptr) {
            throw std::runtime_error("Selection active vertex is missing.");
          }
          break;
        case quader_poly::ElementKind::Edge:
          if (selection.active_edge.a == quader_poly::kInvalidElementId ||
              selection.active_edge.b == quader_poly::kInvalidElementId ||
              selection.active_edge.a == selection.active_edge.b ||
              quader_poly::find_vertex(document, selection.active_edge.a) ==
                  nullptr ||
              quader_poly::find_vertex(document, selection.active_edge.b) ==
                  nullptr) {
            throw std::runtime_error("Selection active edge contains a missing "
                                     "or invalid vertex id.");
          }
          break;
        case quader_poly::ElementKind::Face:
          if (quader_poly::find_face(document, selection.active_face) ==
              nullptr) {
            throw std::runtime_error("Selection active face is missing.");
          }
          break;
	}
}

QdrObject object_from_json(const Json &value) {
  QdrObject object;
  object.id = value.at("id").get<int>();
  object.material_id = value.at("materialId").get<int>();
  object.selected = value.at("selected").get<bool>();
  object.document = qdr_polygon_document_from_json(value.at("document"));
  object.selection = selection_from_json(value.at("selection"));
  if (object.id <= 0) {
    throw std::runtime_error("QDR document contains an invalid object id.");
  }
  validate_selection(object.document, object.selection);
  return object;
}

QdrLight light_from_json(const Json &value) {
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
  light.shadow_mode =
      parse_light_shadow_mode(value.at("shadowMode").get<std::string>());
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
  if (!std::isfinite(light.position.x) || !std::isfinite(light.position.y) ||
      !std::isfinite(light.position.z) || !std::isfinite(light.direction.x) ||
      !std::isfinite(light.direction.y) || !std::isfinite(light.direction.z)) {
    throw std::runtime_error(
        "QDR document contains a light with non-finite transform data.");
  }
  if (!std::isfinite(light.intensity) ||
      !std::isfinite(light.shadow_strength) ||
      !std::isfinite(light.shadow_bias) ||
      !std::isfinite(light.shadow_normal_bias) ||
      !std::isfinite(light.shadow_near_plane) || !std::isfinite(light.range) ||
      !std::isfinite(light.spot_angle) ||
      !std::isfinite(light.inner_spot_angle)) {
    throw std::runtime_error(
        "QDR document contains a light with non-finite scalar data.");
  }
  return light;
}

QdrDocument qdr_document_from_json(const Json &value) {
  QdrDocument document;
  document.next_object_id = value.at("nextObjectId").get<int>();
  document.next_light_id = value.value("nextLightId", 1);
  document.active_object_id = value.at("activeObjectId").get<int>();
  document.active_light_id = value.value("activeLightId", -1);
  document.selection_mode =
      parse_selection_mode(value.at("selectionMode").get<std::string>());
  document.content_revision =
      value.value("contentRevision", static_cast<std::uint64_t>(1));
  document.selection_revision =
      value.value("selectionRevision", static_cast<std::uint64_t>(0));

  if (document.next_object_id <= 0) {
    throw std::runtime_error(
        "QDR document contains an invalid next object id.");
  }
  if (document.next_light_id <= 0) {
    throw std::runtime_error("QDR document contains an invalid next light id.");
  }
  if (document.active_object_id == 0 || document.active_object_id < -1) {
    throw std::runtime_error(
        "QDR document contains an invalid active object id.");
  }
  if (document.active_light_id == 0 || document.active_light_id < -1) {
    throw std::runtime_error(
        "QDR document contains an invalid active light id.");
  }

  std::set<int> object_ids;
  int max_object_id = 0;
  for (const Json &object_json : value.at("objects")) {
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
  if (document.active_object_id > 0 &&
      object_ids.find(document.active_object_id) == object_ids.end()) {
    throw std::runtime_error("QDR document active object id is missing.");
  }
  document.next_object_id =
      std::max(document.next_object_id, max_object_id + 1);

  std::set<int> light_ids;
  int max_light_id = 0;
  if (const auto lights_json = value.find("lights");
      lights_json != value.end()) {
    for (const Json &light_json : *lights_json) {
      QdrLight light = light_from_json(light_json);
      if (!light_ids.insert(light.id).second) {
        throw std::runtime_error("QDR document contains a duplicate light id.");
      }
      max_light_id = std::max(max_light_id, light.id);
      document.lights.push_back(std::move(light));
    }
  }
  if (document.active_light_id > 0 &&
      light_ids.find(document.active_light_id) == light_ids.end()) {
    throw std::runtime_error("QDR document active light id is missing.");
  }
  document.next_light_id = std::max(document.next_light_id, max_light_id + 1);
  return document;
}

const Json &document_payload_from_root(const Json &root) {
  return qdr_payload_from_root(root, kQdrDocumentPayloadKey,
                               kQdrLegacySessionPayloadKey);
}

const Json &migrate_document_payload_to_current(const Json &root, int version) {
  switch (version) {
  case 1:
  case 2:
  case 3:
    return document_payload_from_root(root);
  default:
    throw std::runtime_error("Unsupported Quader file version: " +
                             std::to_string(version));
  }
}

} // namespace

bool is_supported_document_version(int version)
{
  return version >= kQdrMinSupportedFormatVersion &&
         version <= kQdrFormatVersion;
}

std::string serialize_document(const QdrDocument& document)
{
  Json objects = Json::array();
  for (const QdrObject &object : document.objects) {
    objects.push_back(object_to_json(object));
  }

  Json lights = Json::array();
  for (const QdrLight &light : document.lights) {
    lights.push_back(light_to_json(light));
  }

  Json root{
      {"format", qdr_format_name_string()},
      {"version", kQdrFormatVersion},
      {qdr_string_from_view(kQdrDocumentPayloadKey),
       {
           {"nextObjectId", document.next_object_id},
           {"nextLightId", document.next_light_id},
           {"activeObjectId", document.active_object_id},
           {"activeLightId", document.active_light_id},
           {"selectionMode", selection_mode_name(document.selection_mode)},
           {"contentRevision", document.content_revision},
           {"selectionRevision", document.selection_revision},
           {"objects", objects},
           {"lights", lights},
       }},
  };
  return root.dump(2);
}

QdrReadResult deserialize_document(std::string_view text)
{
	try {
          const Json root = Json::parse(text.begin(), text.end());
          require_qdr_root_object(root);
          const QdrRootHeader header = read_qdr_root_header(root);
          if (header.format != qdr_format_name_string()) {
            return read_error_result(
                "qdr.unsupported_format", "/format",
                "Unsupported Quader file format: " + header.format);
          }

		const int version = header.version;
		if (!is_supported_document_version(version)) {
			return read_error_result(
            "qdr.unsupported_version", "/version",
            "Unsupported Quader file version: " +
                std::to_string(version));
		}

		QdrDocument document = qdr_document_from_json(migrate_document_payload_to_current(root, version));
		return { true, std::move(document), {} };
	} catch (const QdrDiagnosticException& exception) {
		return read_error_result(exception.diagnostic());
	} catch (const nlohmann::json::parse_error& exception) {
          const QdrTextLocation location =
              qdr_text_location_from_parse_byte(text, exception.byte);
          return read_error_result(qdr_make_error(
              "qdr.json_parse_error", "", exception.what(), location.line,
              location.column));
	} catch (const nlohmann::json::exception& exception) {
		return read_error_result(
        "qdr.json_shape_error", "", exception.what());
	} catch (const std::exception& exception) {
		return read_error_result("qdr.read_failed", "", exception.what());
	} catch (...) {
		return read_error_result("qdr.unknown_error", "",
                             "Unknown error reading Quader file.");
	}
}

QdrWriteResult write_document_atomic(const std::filesystem::path& path, const QdrDocument& document)
{
	const std::string text = serialize_document(document);
	const QdrReadResult validation = deserialize_document(text);
	if (!validation.ok) {
		return { false, validation.message.empty() ? "Serialized QDR document failed validation." : validation.message };
	}
	const QdrAtomicWriteResult result = write_qdr_text_atomic(path, serialize_document(validation.document));
	return { result.ok, result.message };
}

} // namespace quader_io
