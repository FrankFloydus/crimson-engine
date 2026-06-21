#pragma once

#include <quader_poly/document.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace quader_io {

inline constexpr std::string_view qdr_format_name = "quader.qdr";
inline constexpr std::string_view qdr_document_payload_key = "document";
inline constexpr std::string_view qdr_legacy_session_payload_key = "session";
inline constexpr int qdr_min_supported_format_version = 1;
inline constexpr int qdr_format_version = 3;

enum class QdrSelectionMode {
	object,
	vertex,
	edge,
	face,
};

enum class QdrLightType {
	directional,
	point,
	spot,
};

enum class QdrLightShadowMode {
	none,
	hard,
	soft,
};

struct QdrObject {
	int id = 0;
	int material_id = 0;
	bool selected = false;
	quader_poly::Document document;
	quader_poly::Selection selection;
};

struct QdrLight {
	int id = 0;
	std::string name;
	bool enabled = true;
	QdrLightType type = QdrLightType::point;
	bool selected = false;
	quader_poly::Vec3 position { 0.0F, 3.0F, 0.0F };
	quader_poly::Vec3 direction { 0.0F, -1.0F, 0.0F };
	std::uint32_t color_abgr = 0xFFFFFFFFU;
	float intensity = 1.0F;
	QdrLightShadowMode shadow_mode = QdrLightShadowMode::soft;
	float shadow_strength = 1.0F;
	float shadow_bias = 0.05F;
	float shadow_normal_bias = 0.4F;
	float shadow_near_plane = 0.2F;
	float range = 10.0F;
	float spot_angle = 30.0F;
	float inner_spot_angle = 0.0F;
};

struct QdrDocument {
	std::vector<QdrObject> objects;
	std::vector<QdrLight> lights;
	int next_object_id = 1;
	int next_light_id = 1;
	int active_object_id = -1;
	int active_light_id = -1;
	QdrSelectionMode selection_mode = QdrSelectionMode::object;
	std::uint64_t content_revision = 0;
	std::uint64_t selection_revision = 0;
};

struct QdrReadResult {
	bool ok = false;
	QdrDocument document;
	std::string message;
};

struct QdrWriteResult {
	bool ok = false;
	std::string message;
};

// Stable DTO persistence API. Compatibility helpers that convert to editor-core
// SessionState live in qdr.h so pure IO callers do not depend on editor runtime
// headers.
[[nodiscard]] bool is_supported_document_version(int version);
[[nodiscard]] std::string serialize_document(const QdrDocument& document);
[[nodiscard]] QdrReadResult deserialize_document(std::string_view text);
[[nodiscard]] QdrWriteResult write_document_atomic(const std::filesystem::path& path, const QdrDocument& document);

} // namespace quader_io
