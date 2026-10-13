#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace crimson {

// Engine values deliberately contain only packed scalars. Backend types never
// cross this boundary, including serialization and third-party API adapters.
template<std::size_t N> struct Vector {
    std::array<float, N> values{};
    constexpr Vector() = default;
    template<class... T> requires(sizeof...(T) == N && (std::convertible_to<T, float> && ...))
    constexpr Vector(T... components) : values{static_cast<float>(components)...} {}
    constexpr Vector(std::array<float, N> components) : values(components) {}
    constexpr float* data() { return values.data(); }
    constexpr const float* data() const { return values.data(); }
    constexpr float& operator[](std::size_t i) { return values[i]; }
    constexpr float operator[](std::size_t i) const { return values[i]; }
    constexpr auto begin() { return values.begin(); }
    constexpr auto end() { return values.end(); }
    constexpr auto begin() const { return values.begin(); }
    constexpr auto end() const { return values.end(); }
    constexpr std::size_t size() const { return N; }
    constexpr bool operator==(const Vector&) const = default;
};
using Vec2 = Vector<2>;
using Vec3 = Vector<3>;
using Vec4 = Vector<4>;

// Quaternion scalar order is x,y,z,w; default construction is identity.
struct Quat {
    std::array<float, 4> values{0,0,0,1};
    constexpr Quat() = default;
    constexpr Quat(float x, float y, float z, float w) : values{x,y,z,w} {}
    constexpr float* data() { return values.data(); }
    constexpr const float* data() const { return values.data(); }
    constexpr float& operator[](std::size_t i) { return values[i]; }
    constexpr float operator[](std::size_t i) const { return values[i]; }
    constexpr auto begin() { return values.begin(); }
    constexpr auto end() { return values.end(); }
    constexpr auto begin() const { return values.begin(); }
    constexpr auto end() const { return values.end(); }
    constexpr std::size_t size() const { return 4; }
    constexpr bool operator==(const Quat&) const = default;
};

// Column-major matrices multiply column vectors. World = parent * local.
// Defaults are identity; accumulation requires the explicit zero() factory.
template<std::size_t N> struct Matrix {
    std::array<float, N*N> values{};
    constexpr Matrix() { for (std::size_t i=0; i<N; ++i) values[i*N+i] = 1; }
    template<class... T> requires(sizeof...(T) == N*N && (std::convertible_to<T, float> && ...))
    constexpr Matrix(T... components) : values{static_cast<float>(components)...} {}
    constexpr Matrix(std::array<float, N*N> components) : values(components) {}
    static constexpr Matrix identity() { return {}; }
    static constexpr Matrix zero() { return Matrix(std::array<float, N*N>{}); }
    constexpr float* data() { return values.data(); }
    constexpr const float* data() const { return values.data(); }
    constexpr float& operator[](std::size_t i) { return values[i]; }
    constexpr float operator[](std::size_t i) const { return values[i]; }
    constexpr float& at(std::size_t row, std::size_t column) { return values[column*N+row]; }
    constexpr float at(std::size_t row, std::size_t column) const { return values[column*N+row]; }
    constexpr auto begin() { return values.begin(); }
    constexpr auto end() { return values.end(); }
    constexpr auto begin() const { return values.begin(); }
    constexpr auto end() const { return values.end(); }
    constexpr std::size_t size() const { return N*N; }
    constexpr bool operator==(const Matrix&) const = default;
};
using Mat3 = Matrix<3>;
using Mat4 = Matrix<4>;

struct Ray { Vec3 origin{}, direction{0,0,-1}; };
struct Aabb { Vec3 minimum{}, maximum{}; bool valid{false}; };
enum class ClipDepth { ZeroToOne, NegativeOneToOne };

namespace math {
bool finite(float value);
bool finite(const Vec2& value);
bool finite(const Vec3& value);
bool finite(const Vec4& value);
bool finite(const Quat& value);
bool finite(const Mat3& value);
bool finite(const Mat4& value);
Vec3 add(const Vec3& a, const Vec3& b);
Vec3 subtract(const Vec3& a, const Vec3& b);
Vec3 scale(const Vec3& value, double scalar);
double dot(const Vec3& a, const Vec3& b);
float dotFloat(const Vec3& a, const Vec3& b); // Retains float accumulation in IBL kernels.
Vec3 cross(const Vec3& a, const Vec3& b);
double length(const Vec3& value);
Vec3 normalized(const Vec3& value, const Vec3& fallback = {0,1,0}, double epsilon = 1e-8);
Vec3 normalizedFloat(const Vec3& value, const Vec3& fallback = {0,1,0}, float epsilon = 1e-6f);
Vec3 rotate(const Vec3& value, const Vec3& axis, double radians);
Quat axisAngle(const Vec3& axis, double radians);
Mat4 multiply(const Mat4& a, const Mat4& b);
Mat4 translation(const Vec3& value);
Mat4 rotation(const Quat& value);
Mat4 scaling(const Vec3& value);
Vec3 transformPoint(const Mat4& matrix, const Vec3& value);
Vec4 transformHomogeneous(const Mat4& matrix,const Vec4& value);
Vec3 transformDirection(const Mat4& matrix, const Vec3& value);
bool isAffine(const Mat4& matrix, double tolerance = 1e-6);
// Returns -1/+1 for an invertible linear transform, zero for singular/invalid.
float handedness(const Mat4& matrix);
std::optional<Mat4> inverse(const Mat4& matrix);
std::optional<Mat4> normalMatrix(const Mat4& matrix);
Mat4 orthonormalFrame(const Mat4& matrix);
// Inspector presentation, never authoritative scene storage. Euler XYZ means
// Rz * Ry * Rx for column vectors; angles are radians. Scale is the signed
// length of each original linear column, so sheared columns retain their angle.
// A reference keeps equivalent Euler/sign branches stable between edits.
struct AffineTransformFields {
    Vec3 position{},rotationRadians{},scale{1,1,1};
    Mat4 rotation{};
    bool sheared{},singular{};
};
std::optional<AffineTransformFields> affineTransformFields(const Mat4& matrix,const AffineTransformFields* reference=nullptr);
Mat4 eulerRotationXYZ(const Vec3& radians);
// Compute edits from the exact matrix and fields captured at gesture start.
// Position preserves the linear block exactly; rotation applies a rigid delta;
// scale changes only edited columns. A collapsed column is restored along its
// captured rotation axis. Invalid/non-affine inputs return an explicit failure.
std::optional<Mat4> withAffinePosition(const Mat4& matrix,const Vec3& position);
std::optional<Mat4> withAffineRotation(const Mat4& matrix,const AffineTransformFields& captured,const Vec3& radians);
std::optional<Mat4> withAffineScale(const Mat4& matrix,const AffineTransformFields& captured,const Vec3& scale);
void include(Aabb& bounds, const Vec3& point);
void include(Aabb& bounds, const Aabb& other);
Aabb transformBounds(const Aabb& bounds, const Mat4& matrix);
Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up = {0,1,0});
Mat4 perspective(double verticalRadians, double aspect, double nearPlane, double farPlane, ClipDepth depth);
Mat4 orthographic(double left, double right, double bottom, double top, double nearPlane, double farPlane, ClipDepth depth);
// Screen coordinates have a top-left origin. Viewport is x,y,width,height;
// projected z is normalized [0,1] for both clip conventions.
std::optional<Vec3> project(const Vec3& world, const Mat4& viewProjection, const Vec4& viewport, ClipDepth depth);
std::optional<Vec3> unproject(const Vec3& screen, const Mat4& viewProjection, const Vec4& viewport, ClipDepth depth);
std::optional<Ray> rayFromScreen(float x, float y, const Mat4& viewProjection, const Vec4& viewport, ClipDepth depth);
std::optional<Vec3> intersectPlane(const Ray& ray, const Vec3& point, const Vec3& normal);
} // namespace math

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return math::add(a,b); }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return math::subtract(a,b); }
inline Vec3 operator-(const Vec3& value) { return math::scale(value,-1); }
inline Vec3 operator*(const Vec3& value, double scalar) { return math::scale(value,scalar); }
inline Vec3 operator*(double scalar, const Vec3& value) { return math::scale(value,scalar); }
inline Vec3 operator/(const Vec3& value, double scalar) { return math::scale(value,1/scalar); }
inline Mat4 operator*(const Mat4& a, const Mat4& b) { return math::multiply(a,b); }

static_assert(sizeof(Vec3) == 3*sizeof(float) && sizeof(Mat4) == 16*sizeof(float));
static_assert(std::is_standard_layout_v<Vec3> && std::is_trivially_copyable_v<Vec3>);
static_assert(std::is_standard_layout_v<Mat4> && std::is_trivially_copyable_v<Mat4>);
} // namespace crimson
