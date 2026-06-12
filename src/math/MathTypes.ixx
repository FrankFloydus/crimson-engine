export module quader.math.types;

export namespace quader {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

[[nodiscard]] Vec3 normalized_direction_from_yaw_pitch(float yaw_degrees, float pitch_degrees);

} // namespace quader
