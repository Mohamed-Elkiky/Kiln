#pragma once

#include "kiln/vec3.hpp"

namespace kiln {

// P(t) = origin + t * direction
class Ray {
public:
    constexpr Ray() = default;
    constexpr Ray(const Point3& origin, const Vec3& direction)
        : origin_(origin), direction_(direction) {}

    constexpr const Point3& origin() const { return origin_; }
    constexpr const Vec3& direction() const { return direction_; }

    constexpr Point3 at(double t) const { return origin_ + t * direction_; }

private:
    Point3 origin_;
    Vec3 direction_;
};

}  // namespace kiln
