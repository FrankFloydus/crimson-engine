#include "quader/geometry/UVSphere.h"

#include "quader/math/Types.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace quader {
namespace {

std::uint32_t packRgba(float r, float g, float b, float a) {
    auto toByte = [](float value) -> std::uint32_t {
        return static_cast<std::uint32_t>(clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (toByte(a) << 24u) | (toByte(b) << 16u) | (toByte(g) << 8u) | toByte(r);
}

} // namespace

MeshAsset generateUVSphere(const UVSphereOptions& options) {
    if (options.segments < 3 || options.rings < 2 || options.radius <= 0.0f) {
        throw std::invalid_argument("Invalid UV sphere options.");
    }

    MeshAsset mesh;
    mesh.name = "UV Sphere";
    mesh.vertices.reserve((options.rings + 1) * (options.segments + 1));
    mesh.indices.reserve(options.rings * options.segments * 6);

    const Vec3 lightDir = normalize({-0.35f, 0.75f, 0.55f});

    for (std::uint32_t y = 0; y <= options.rings; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(options.rings);
        const float theta = v * std::numbers::pi_v<float>;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for (std::uint32_t x = 0; x <= options.segments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(options.segments);
            const float phi = u * std::numbers::pi_v<float> * 2.0f;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            Vec3 normal = normalize({sinTheta * cosPhi, cosTheta, sinTheta * sinPhi});
            const float diffuse = std::max(0.0f, dot(normal, lightDir));
            const float shade = 0.22f + diffuse * 0.78f;

            MeshVertex vertex;
            vertex.position = normal * options.radius;
            vertex.normal = normal;
            vertex.uv = {u, v};
            vertex.rgba = packRgba(shade, 0.04f * shade, 0.03f * shade, 1.0f);
            mesh.vertices.push_back(vertex);
        }
    }

    const std::uint32_t stride = options.segments + 1;
    for (std::uint32_t y = 0; y < options.rings; ++y) {
        for (std::uint32_t x = 0; x < options.segments; ++x) {
            const std::uint32_t a = y * stride + x;
            const std::uint32_t b = a + stride;
            const std::uint32_t c = b + 1;
            const std::uint32_t d = a + 1;

            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(d);
            mesh.indices.push_back(d);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
        }
    }
    mesh.primitives.push_back(MeshPrimitiveSpan{
            .indexOffset = 0,
            .indexCount = static_cast<std::uint32_t>(mesh.indices.size()),
            .materialSlot = 0,
    });

    return mesh;
}

} // namespace quader
