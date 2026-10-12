#pragma once

#include <cmath>
#include <ostream>

namespace kiln {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator-() const { return {-x, -y, -z}; }

    constexpr double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }

    constexpr Vec3& operator+=(const Vec3& v) {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }

    constexpr Vec3& operator-=(const Vec3& v) {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }

    constexpr Vec3& operator*=(double t) {
        x *= t;
        y *= t;
        z *= t;
        return *this;
    }

    constexpr Vec3& operator/=(double t) { return *this *= 1.0 / t; }

    constexpr double length_squared() const { return x * x + y * y + z * z; }
    double length() const { return std::sqrt(length_squared()); }

    // True if the vector is close to zero in every dimension.
    bool near_zero() const {
        constexpr double s = 1e-8;
        return std::fabs(x) < s && std::fabs(y) < s && std::fabs(z) < s;
    }
};

// Same type, different meaning. Makes signatures self-documenting.
using Point3 = Vec3;
using Color = Vec3;

// ---------- Arithmetic ----------

constexpr Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

// Component-wise multiply (used for tinting colours)
constexpr Vec3 operator*(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

constexpr Vec3 operator*(double t, const Vec3& v) { return {t * v.x, t * v.y, t * v.z}; }
constexpr Vec3 operator*(const Vec3& v, double t) { return t * v; }
constexpr Vec3 operator/(const Vec3& v, double t) { return (1.0 / t) * v; }

// ---------- Vector maths ----------

constexpr double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline Vec3 unit_vector(const Vec3& v) { return v / v.length(); }

constexpr Vec3 reflect(const Vec3& v, const Vec3& n) { return v - 2.0 * dot(v, n) * n; }

// Bend unit vector uv through a surface with normal n (Snell's law).
// eta_ratio = refractive index we're leaving / index we're entering.
inline Vec3 refract(const Vec3& uv, const Vec3& n, double eta_ratio) {
    const double cos_theta = std::fmin(dot(-uv, n), 1.0);
    const Vec3 r_out_perp = eta_ratio * (uv + cos_theta * n);
    const Vec3 r_out_parallel = -std::sqrt(std::fabs(1.0 - r_out_perp.length_squared())) * n;
    return r_out_perp + r_out_parallel;
}

inline std::ostream& operator<<(std::ostream& out, const Vec3& v) {
    return out << '(' << v.x << ", " << v.y << ", " << v.z << ')';
}

}  // namespace kiln