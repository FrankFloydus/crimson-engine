#pragma once
#include <crimson/renderer/Scene.hpp>
#include <crimson/renderer/forward/StreamingModel.h>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace quader {
// Worker-prepared CPU geometry. The base is ordinary stripped Engine metadata;
// immutable original-LOD uploads and source slots remain pinned by this owner.
struct CrimsonRichGeometry final : crimson::ModelAsset {
    std::vector<std::shared_ptr<const crimson::ModelGeometryLevel>> uploads;
    std::vector<std::uint32_t> source_slots;
    std::size_t byte_size{};
};

std::shared_ptr<const CrimsonRichGeometry> build_crimson_rich_geometry(
    const crimson::shared::MeshUpdate&,const crimson::shared::Matrix4& sampling,
    std::string identity,std::stop_token={});
}
