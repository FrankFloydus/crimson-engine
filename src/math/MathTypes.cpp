module;

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

module quader.math.types;

namespace quader {

Vec3 normalized_direction_from_yaw_pitch(float yaw_degrees, float pitch_degrees)
{
    const float yaw = glm::radians(yaw_degrees);
    const float pitch = glm::radians(pitch_degrees);

    const glm::vec3 direction = glm::normalize(glm::vec3 {
        glm::cos(pitch) * glm::sin(yaw),
        glm::sin(pitch),
        -glm::cos(pitch) * glm::cos(yaw),
    });

    return Vec3 { direction.x, direction.y, direction.z };
}

} // namespace quader
