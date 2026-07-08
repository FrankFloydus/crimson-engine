////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/qdr/qdr_diagnostic.hpp"
#include "io/qdr/qdr_format.hpp"

#include <mesh/polygon/document.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace quader_io {

inline constexpr std::string_view kQdrFormatName = quader::kQdrFormatName;
inline constexpr std::string_view kQdrDocumentPayloadKey =
    quader::kQdrDocumentPayloadKey;
inline constexpr std::string_view kQdrLegacySessionPayloadKey =
    quader::kQdrLegacySessionPayloadKey;
inline constexpr int kQdrMinSupportedFormatVersion =
    quader::kQdrMinSupportedFormatVersion;
inline constexpr int kQdrFormatVersion = quader::kQdrLegacySessionFormatVersion;

/**
 * Enumerates QdrSelectionMode values used by the io layer.
 */
enum class QdrSelectionMode {
  Object,
  Vertex,
  Edge,
  Face,
};

/**
 * Enumerates QdrLightType values used by the io layer.
 */
enum class QdrLightType {
  Directional,
  Point,
  Spot,
};

/**
 * Enumerates QdrLightShadowMode values used by the io layer.
 */
enum class QdrLightShadowMode {
  None,
  Hard,
  Soft,
};

/**
 * Represents a QDR Object value used by the QDR document persistence layer.
 */
struct QdrObject {
	int id = 0;
	int material_id = 0;
	bool selected = false;
	quader_poly::Document document;
	quader_poly::Selection selection;
};

/**
 * Represents a QDR Light value used by the QDR document persistence layer.
 */
struct QdrLight {
	int id = 0;
	std::string name;
	bool enabled = true;
        QdrLightType type = QdrLightType::Point;
        bool selected = false;
	quader::QVec3 position { 0.0F, 3.0F, 0.0F };
	quader::QVec3 direction { 0.0F, -1.0F, 0.0F };
	std::uint32_t color_abgr = 0xFFFFFFFFU;
	float intensity = 1.0F;
        QdrLightShadowMode shadow_mode = QdrLightShadowMode::Soft;
        float shadow_strength = 1.0F;
	float shadow_bias = 0.05F;
	float shadow_normal_bias = 0.4F;
	float shadow_near_plane = 0.2F;
	float range = 10.0F;
	float spot_angle = 30.0F;
	float inner_spot_angle = 0.0F;
};

/**
 * Represents a QDR Document value used by the QDR document persistence layer.
 */
struct QdrDocument {
	std::vector<QdrObject> objects;
	std::vector<QdrLight> lights;
	int next_object_id = 1;
	int next_light_id = 1;
	int active_object_id = -1;
	int active_light_id = -1;
        QdrSelectionMode selection_mode = QdrSelectionMode::Object;
        std::uint64_t content_revision = 0;
	std::uint64_t selection_revision = 0;
};

/**
 * Stores the QDR Read Result data contract used by the QDR document persistence layer.
 */
struct QdrReadResult {
	bool ok = false;
	QdrDocument document;
	std::string message;
	std::vector<QdrDiagnostic> diagnostics;
};

/**
 * Stores the QDR Write Result data contract used by the QDR document persistence layer.
 */
struct QdrWriteResult {
	bool ok = false;
	std::string message;
};

// Stable DTO persistence API. Compatibility helpers that convert to editor-core
// SessionState live in qdr_runtime_io_compat.hpp so pure IO callers do not depend on
// editor runtime headers.
[[nodiscard]] bool is_supported_document_version(int version);
[[nodiscard]] std::string serialize_document(const QdrDocument& document);
[[nodiscard]] QdrReadResult deserialize_document(std::string_view text);
[[nodiscard]] QdrWriteResult write_document_atomic(const std::filesystem::path& path, const QdrDocument& document);

} // namespace quader_io
