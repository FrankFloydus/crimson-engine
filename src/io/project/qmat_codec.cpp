////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/qmat_codec.hpp"

#include "core/qstr.hpp"
#include "io/qdr/qdr_atomic_write.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace quader {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMaximumMaterialNameBytes = 256u;
constexpr std::size_t kMaximumPortablePathBytes = 32'768u;

ProjectIoResult<ProjectMaterialDocument> malformed(std::string message) {
  return ProjectIoResult<ProjectMaterialDocument>::failure(
      ProjectIoErrorCode::MaterialMalformed, std::move(message));
}

bool non_whitespace(std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](unsigned char character) {
    return !std::isspace(character);
  });
}

bool finite_unit(float value) {
  return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

bool supported_color_space(ProjectTextureColorSpace color_space) {
  switch (color_space) {
  case ProjectTextureColorSpace::Srgb:
  case ProjectTextureColorSpace::Linear:
    return true;
  }
  return false;
}

bool has_qmat_extension(const ProjectRelativePath &path) {
  return QStr::to_ascii_lower(path.native_path().extension().string()) ==
         ".qmat";
}

std::optional<ProjectTextureColorSpace>
parse_color_space(std::string_view text) {
  if (text == "srgb") {
    return ProjectTextureColorSpace::Srgb;
  }
  if (text == "linear") {
    return ProjectTextureColorSpace::Linear;
  }
  return std::nullopt;
}

std::string_view color_space_name(ProjectTextureColorSpace color_space) {
  return color_space == ProjectTextureColorSpace::Srgb ? "srgb" : "linear";
}

std::optional<NormalMapConvention>
parse_normal_convention(std::string_view text) {
  if (text == "opengl") {
    return NormalMapConvention::OpenGL;
  }
  if (text == "directx") {
    return NormalMapConvention::DirectX;
  }
  return std::nullopt;
}

std::string_view normal_convention_name(NormalMapConvention convention) {
  return convention == NormalMapConvention::OpenGL ? "opengl" : "directx";
}

ProjectIoResult<ProjectMaterialTextureSlot>
decode_texture_slot(const Json &json, std::string_view slot_name) {
  if (!json.is_object() || !json.contains("id") ||
      !json["id"].is_string() || !json.contains("path") ||
      !json["path"].is_string() || !json.contains("colorSpace") ||
      !json["colorSpace"].is_string()) {
    return ProjectIoResult<ProjectMaterialTextureSlot>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        std::string(slot_name) + " texture slot is malformed.");
  }

  const std::optional<ProjectAssetId> id =
      ProjectAssetId::parse(json["id"].get<std::string>());
  const std::string path_text = json["path"].get<std::string>();
  const std::optional<ProjectTextureColorSpace> color_space =
      parse_color_space(json["colorSpace"].get<std::string>());
  if (!id.has_value()) {
    return ProjectIoResult<ProjectMaterialTextureSlot>::failure(
        ProjectIoErrorCode::InvalidUuid,
        std::string(slot_name) + " texture UUID is invalid.");
  }
  if (path_text.size() > kMaximumPortablePathBytes) {
    return ProjectIoResult<ProjectMaterialTextureSlot>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        std::string(slot_name) + " texture path exceeds the size limit.");
  }
  ProjectIoResult<ProjectRelativePath> path =
      ProjectRelativePath::parse_portable(path_text);
  if (!path.ok()) {
    return ProjectIoResult<ProjectMaterialTextureSlot>::failure(
        path.error().code,
        std::string(slot_name) + " texture path is invalid.",
        path.error().path);
  }
  if (!color_space.has_value()) {
    return ProjectIoResult<ProjectMaterialTextureSlot>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        std::string(slot_name) + " texture color space is unsupported.");
  }
  return ProjectIoResult<ProjectMaterialTextureSlot>::success({
      .id = *id,
      .project_relative_path = path.value().portable_string(),
      .color_space = *color_space,
  });
}

ProjectIoResult<void> validate_texture_slot(
    const std::optional<ProjectMaterialTextureSlot> &slot,
    ProjectTextureColorSpace required_color_space, std::string_view name) {
  if (!slot.has_value()) {
    return ProjectIoResult<void>::success();
  }
  ProjectIoResult<ProjectRelativePath> path =
      ProjectRelativePath::parse_portable(slot->project_relative_path);
  if (!slot->id.valid() ||
      slot->project_relative_path.size() > kMaximumPortablePathBytes ||
      !path.ok() || path.value().portable_string() !=
                        slot->project_relative_path ||
      !supported_color_space(slot->color_space)) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        std::string(name) +
            " texture reference is invalid or exceeds the size limit.");
  }
  if (slot->color_space != required_color_space) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        std::string(name) + " texture must use " +
            std::string(color_space_name(required_color_space)) +
            " color space.");
  }
  return ProjectIoResult<void>::success();
}

ProjectIoResult<void> validate_standard_material(
    const ProjectStandardMaterialDefinition &material) {
  if (!std::ranges::all_of(material.base_color, finite_unit) ||
      !finite_unit(material.roughness) || !finite_unit(material.metallic) ||
      !is_valid_standard_material_normal_strength(
          material.normal_strength) ||
      !is_valid_standard_material_texture_transform(
          material.texture_transform)) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        "Standard Material parameters are outside their finite ranges.");
  }
  switch (material.normal_convention) {
  case NormalMapConvention::OpenGL:
  case NormalMapConvention::DirectX:
    break;
  default:
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        "Standard Material normal-map convention is unsupported.");
  }

  ProjectIoResult<void> result = validate_texture_slot(
      material.base_color_texture, ProjectTextureColorSpace::Srgb,
      "Albedo");
  if (!result.ok()) {
    return result;
  }
  result = validate_texture_slot(material.roughness_texture,
                                 ProjectTextureColorSpace::Linear,
                                 "Roughness");
  if (!result.ok()) {
    return result;
  }
  result = validate_texture_slot(material.metallic_texture,
                                 ProjectTextureColorSpace::Linear,
                                 "Metallic");
  if (!result.ok()) {
    return result;
  }
  return validate_texture_slot(material.normal_texture,
                               ProjectTextureColorSpace::Linear, "Normal");
}

ProjectIoResult<void>
validate_document(const ProjectMaterialDocument &material) {
  if (!material.id.valid()) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::InvalidUuid,
        "QMAT requires a valid material UUID.");
  }
  if (material.name.size() > kMaximumMaterialNameBytes ||
      !non_whitespace(material.name)) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        "QMAT material name is empty or exceeds the size limit.");
  }
  if (material.kind != ProjectMaterialKind::Standard ||
      material.material_version != kStandardMaterialVersion ||
      material.standard_material() == nullptr) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::UnsupportedFormat,
        "QMAT material type or payload version is not supported.");
  }
  return validate_standard_material(*material.standard_material());
}

ProjectIoResult<ProjectMaterialDocument> decode_standard(
    const Json &root, const Json &payload, std::uint32_t source_version) {
  if (!root.contains("id") || !root["id"].is_string() ||
      !root.contains("name") || !root["name"].is_string() ||
      !payload.is_object() || !payload.contains("model") ||
      !payload["model"].is_string() ||
      payload["model"].get<std::string>() != "metallicRoughness" ||
      !payload.contains("parameters") ||
      !payload["parameters"].is_object()) {
    return malformed("QMAT identity, model, or parameters are malformed.");
  }

  const std::optional<ProjectAssetId> id =
      ProjectAssetId::parse(root["id"].get<std::string>());
  if (!id.has_value()) {
    return ProjectIoResult<ProjectMaterialDocument>::failure(
        ProjectIoErrorCode::InvalidUuid,
        "QMAT material UUID is invalid.");
  }
  const Json &parameters = payload["parameters"];
  if (!parameters.contains("baseColor") ||
      !parameters["baseColor"].is_array() ||
      parameters["baseColor"].size() != 3u ||
      !parameters.contains("roughness") ||
      !parameters["roughness"].is_number() ||
      !parameters.contains("metallic") ||
      !parameters["metallic"].is_number()) {
    return malformed("QMAT metallic-roughness parameters are malformed.");
  }

  ProjectStandardMaterialDefinition standard;
  for (std::size_t index = 0u; index < standard.base_color.size(); ++index) {
    if (!parameters["baseColor"][index].is_number()) {
      return malformed("QMAT base color must contain three numbers.");
    }
    standard.base_color[index] =
        parameters["baseColor"][index].get<float>();
  }
  standard.roughness = parameters["roughness"].get<float>();
  standard.metallic = parameters["metallic"].get<float>();

  if (source_version == kQuaderMaterialVersion) {
    if (!parameters.contains("normalStrength") ||
        !parameters["normalStrength"].is_number() ||
        !parameters.contains("normalConvention") ||
        !parameters["normalConvention"].is_string()) {
      return malformed("QMAT Standard normal parameters are malformed.");
    }
    standard.normal_strength = parameters["normalStrength"].get<float>();
    const std::optional<NormalMapConvention> convention =
        parse_normal_convention(
            parameters["normalConvention"].get<std::string>());
    if (!convention.has_value()) {
      return malformed("QMAT normal-map convention is unsupported.");
    }
    standard.normal_convention = *convention;

    if (payload.contains("textureTransform")) {
      const Json &transform = payload["textureTransform"];
      if (!transform.is_object() || !transform.contains("center") ||
          !transform["center"].is_array() ||
          transform["center"].size() != 2u ||
          !transform.contains("scale") ||
          !transform["scale"].is_array() ||
          transform["scale"].size() != 2u ||
          !transform.contains("offset") ||
          !transform["offset"].is_array() ||
          transform["offset"].size() != 2u ||
          !transform.contains("rotationDegrees") ||
          !transform["rotationDegrees"].is_number()) {
        return malformed("QMAT Standard texture transform is malformed.");
      }
      for (std::size_t index = 0u; index < 2u; ++index) {
        if (!transform["center"][index].is_number() ||
            !transform["scale"][index].is_number() ||
            !transform["offset"][index].is_number()) {
          return malformed(
              "QMAT Standard texture transform vectors must contain numbers.");
        }
        standard.texture_transform.center[index] =
            transform["center"][index].get<float>();
        standard.texture_transform.scale[index] =
            transform["scale"][index].get<float>();
        standard.texture_transform.offset[index] =
            transform["offset"][index].get<float>();
      }
      standard.texture_transform.rotation_degrees =
          transform["rotationDegrees"].get<float>();
    }
  }

  if (payload.contains("textureSlots")) {
    const Json &slots = payload["textureSlots"];
    if (!slots.is_object()) {
      return malformed("QMAT texture slots must be an object.");
    }
    for (const auto &[name, slot_json] : slots.items()) {
      const bool legacy_slot = name == "baseColorTexture" ||
                               name == "roughnessTexture";
      const bool v2_slot = legacy_slot || name == "metallicTexture" ||
                           name == "normalTexture";
      if ((source_version == kQuaderMaterialLegacyVersion && !legacy_slot) ||
          (source_version == kQuaderMaterialVersion && !v2_slot)) {
        return malformed("QMAT contains an unsupported texture slot: " +
                         name + ".");
      }
      ProjectIoResult<ProjectMaterialTextureSlot> slot =
          decode_texture_slot(slot_json, name);
      if (!slot.ok()) {
        return ProjectIoResult<ProjectMaterialDocument>::failure(
            slot.error().code, slot.error().message, slot.error().path);
      }
      ProjectMaterialTextureSlot decoded_slot = slot.take_value();
      if (source_version == kQuaderMaterialLegacyVersion) {
        decoded_slot.color_space = name == "baseColorTexture"
                                       ? ProjectTextureColorSpace::Srgb
                                       : ProjectTextureColorSpace::Linear;
      }
      if (name == "baseColorTexture") {
        standard.base_color_texture = std::move(decoded_slot);
      } else if (name == "roughnessTexture") {
        standard.roughness_texture = std::move(decoded_slot);
      } else if (name == "metallicTexture") {
        standard.metallic_texture = std::move(decoded_slot);
      } else {
        standard.normal_texture = std::move(decoded_slot);
      }
    }
  }

  ProjectMaterialDocument material{
      .id = *id,
      .name = root["name"].get<std::string>(),
      .kind = ProjectMaterialKind::Standard,
      .material_version = kStandardMaterialVersion,
      .payload = std::move(standard),
  };
  ProjectIoResult<void> validated = validate_document(material);
  if (!validated.ok()) {
    return ProjectIoResult<ProjectMaterialDocument>::failure(
        validated.error().code, validated.error().message,
        validated.error().path);
  }
  return ProjectIoResult<ProjectMaterialDocument>::success(
      std::move(material));
}

std::uint64_t content_hash(std::string_view bytes) {
  std::uint64_t hash = 14'695'981'039'346'656'037ull;
  for (unsigned char value : bytes) {
    hash ^= value;
    hash *= 1'099'511'628'211ull;
  }
  return hash;
}

ProjectIoResult<std::string> read_text_file(const std::filesystem::path &path) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not inspect QMAT file: " + error.message(), path);
  }
  if (size > kMaximumQmatBytes) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        "QMAT exceeds the supported 4 MiB size limit.", path);
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::IoFailure, "Could not open QMAT file.", path);
  }
  std::string text(static_cast<std::size_t>(size), '\0');
  stream.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream.good() && !stream.eof()) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::IoFailure, "Could not read QMAT file.", path);
  }
  return ProjectIoResult<std::string>::success(std::move(text));
}

Json encode_texture_slot(const ProjectMaterialTextureSlot &slot) {
  return Json{
      {"id", std::string(slot.id.str())},
      {"path", slot.project_relative_path},
      {"colorSpace", std::string(color_space_name(slot.color_space))},
  };
}

} // namespace

ProjectIoResult<ProjectMaterialDocument>
QmatCodec::decode(std::string_view text) {
  if (text.empty() || text.size() > kMaximumQmatBytes) {
    return malformed("QMAT is empty or exceeds the supported size limit.");
  }

  try {
    const Json root = Json::parse(text.begin(), text.end());
    if (!root.is_object()) {
      return malformed("QMAT root must be an object.");
    }
    if (!root.contains("format") || !root["format"].is_string() ||
        root["format"].get<std::string>() != kQuaderMaterialFormat) {
      return ProjectIoResult<ProjectMaterialDocument>::failure(
          ProjectIoErrorCode::UnsupportedFormat,
          "File is not a Quader material document.");
    }
    if (!root.contains("version") ||
        !root["version"].is_number_unsigned()) {
      return ProjectIoResult<ProjectMaterialDocument>::failure(
          ProjectIoErrorCode::UnsupportedFormat,
          "QMAT version is not supported.");
    }
    const std::uint64_t source_version = root["version"].get<std::uint64_t>();
    if (source_version == kQuaderMaterialLegacyVersion) {
      return decode_standard(root, root, kQuaderMaterialLegacyVersion);
    }
    if (source_version != kQuaderMaterialVersion) {
      return ProjectIoResult<ProjectMaterialDocument>::failure(
          ProjectIoErrorCode::UnsupportedFormat,
          "QMAT version is not supported.");
    }

    if (!root.contains("materialType") ||
        !root["materialType"].is_string() ||
        root["materialType"].get<std::string>() != "standard") {
      return ProjectIoResult<ProjectMaterialDocument>::failure(
          ProjectIoErrorCode::UnsupportedFormat,
          "QMAT material type is not supported.");
    }
    if (!root.contains("materialVersion") ||
        !root["materialVersion"].is_number_unsigned() ||
        root["materialVersion"].get<std::uint64_t>() !=
            kStandardMaterialVersion ||
        !root.contains("payload")) {
      return ProjectIoResult<ProjectMaterialDocument>::failure(
          ProjectIoErrorCode::UnsupportedFormat,
          "QMAT Standard Material payload version is not supported.");
    }
    return decode_standard(root, root["payload"], kQuaderMaterialVersion);
  } catch (const std::exception &exception) {
    return malformed(std::string("Could not parse QMAT: ") +
                     exception.what());
  }
}

ProjectIoResult<std::string>
QmatCodec::encode(const ProjectMaterialDocument &material) {
  ProjectIoResult<void> validated = validate_document(material);
  if (!validated.ok()) {
    return ProjectIoResult<std::string>::failure(
        validated.error().code, validated.error().message,
        validated.error().path);
  }
  const ProjectStandardMaterialDefinition &standard =
      *material.standard_material();

  Json texture_slots = Json::object();
  if (standard.base_color_texture.has_value()) {
    texture_slots["baseColorTexture"] =
        encode_texture_slot(*standard.base_color_texture);
  }
  if (standard.roughness_texture.has_value()) {
    texture_slots["roughnessTexture"] =
        encode_texture_slot(*standard.roughness_texture);
  }
  if (standard.metallic_texture.has_value()) {
    texture_slots["metallicTexture"] =
        encode_texture_slot(*standard.metallic_texture);
  }
  if (standard.normal_texture.has_value()) {
    texture_slots["normalTexture"] =
        encode_texture_slot(*standard.normal_texture);
  }

  Json root{
      {"format", std::string(kQuaderMaterialFormat)},
      {"version", kQuaderMaterialVersion},
      {"id", std::string(material.id.str())},
      {"name", material.name},
      {"materialType", "standard"},
      {"materialVersion", kStandardMaterialVersion},
      {"payload",
       Json{
           {"model", "metallicRoughness"},
           {"parameters",
            Json{
                {"baseColor", standard.base_color},
                {"roughness", standard.roughness},
                {"metallic", standard.metallic},
                {"normalStrength", standard.normal_strength},
                {"normalConvention",
                 std::string(normal_convention_name(
                     standard.normal_convention))},
            }},
           {"textureTransform",
            Json{
                {"center", standard.texture_transform.center},
                {"scale", standard.texture_transform.scale},
                {"offset", standard.texture_transform.offset},
                {"rotationDegrees",
                 standard.texture_transform.rotation_degrees},
            }},
           {"textureSlots", std::move(texture_slots)},
       }},
  };
  std::string encoded = root.dump(2);
  if (encoded.size() > kMaximumQmatBytes) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        "Encoded QMAT exceeds the supported 4 MiB size limit.");
  }
  return ProjectIoResult<std::string>::success(std::move(encoded));
}

ProjectIoResult<QmatDecodedFile>
QmatCodec::read(const ProjectFileSystem &file_system,
                const ProjectRelativePath &path) {
  if (!path.valid() || !has_qmat_extension(path)) {
    return ProjectIoResult<QmatDecodedFile>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Material path must be a project-relative .qmat file.",
        path.native_path());
  }
  ProjectIoResult<std::filesystem::path> absolute =
      file_system.resolve_existing_file(path);
  if (!absolute.ok()) {
    return ProjectIoResult<QmatDecodedFile>::failure(
        absolute.error().code, absolute.error().message,
        absolute.error().path);
  }
  ProjectIoResult<std::string> text = read_text_file(absolute.value());
  if (!text.ok()) {
    return ProjectIoResult<QmatDecodedFile>::failure(
        text.error().code, text.error().message, text.error().path);
  }
  ProjectIoResult<ProjectMaterialDocument> decoded = decode(text.value());
  if (!decoded.ok()) {
    return ProjectIoResult<QmatDecodedFile>::failure(
        decoded.error().code, decoded.error().message, absolute.value());
  }
  const std::uintmax_t file_size = text.value().size();
  const std::uint64_t revision = content_hash(text.value());
  return ProjectIoResult<QmatDecodedFile>::success({
      .material = decoded.take_value(),
      .file_size = file_size,
      .content_revision = revision,
  });
}

ProjectIoResult<void>
QmatCodec::write_atomic(const ProjectFileSystem &file_system,
                        const ProjectRelativePath &path,
                        const ProjectMaterialDocument &material) {
  if (!path.valid() || !has_qmat_extension(path)) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Material path must be a project-relative .qmat file.",
        path.native_path());
  }
  ProjectIoResult<std::filesystem::path> absolute =
      file_system.resolve_for_write(path);
  if (!absolute.ok()) {
    return ProjectIoResult<void>::failure(
        absolute.error().code, absolute.error().message,
        absolute.error().path);
  }
  ProjectIoResult<std::string> encoded = encode(material);
  if (!encoded.ok()) {
    return ProjectIoResult<void>::failure(
        encoded.error().code, encoded.error().message,
        encoded.error().path);
  }
  const quader_io::QdrAtomicWriteResult written =
      quader_io::write_qdr_text_atomic(
          absolute.value(), encoded.value(),
          [&material](const std::filesystem::path &temporary_path) {
            ProjectIoResult<std::string> temporary_text =
                read_text_file(temporary_path);
            if (!temporary_text.ok()) {
              return quader_io::QdrAtomicWriteResult{
                  false, temporary_text.error().message};
            }
            ProjectIoResult<ProjectMaterialDocument> verified =
                QmatCodec::decode(temporary_text.value());
            const bool matches =
                verified.ok() && verified.value() == material;
            return quader_io::QdrAtomicWriteResult{
                matches,
                matches ? std::string{}
                        : verified.ok()
                              ? "Temporary QMAT content changed during write."
                              : verified.error().message};
          });
  if (!written.ok) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                          written.message,
                                          absolute.value());
  }
  return ProjectIoResult<void>::success();
}

} // namespace quader
