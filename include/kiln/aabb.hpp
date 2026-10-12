#pragma once

#include <algorithm>
#include <utility>

#include "kiln/interval.hpp"
#include "kiln/ray.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

// Smallest interval containing both a and b.
constexpr Interval enclose(const Interval& a, const Interval& b) {
    return {a.min <= b.min ? a.min : b.min, a.max >= b.max ? a.max : b.max};
}

// Axis-aligned bounding box: one interval per axis. Default is empty.
class AABB {
public:
    Interval x, y, z;

    AABB() = default;

    AABB(const Interval& x_, const Interval& y_, const Interval& z_) : x(x_), y(y_), z(z_) {
        pad_to_minimums();
    }

    // Box spanning two corner points, given in any order.
    AABB(const Point3& a, const Point3& b)
        : x(std::min(a.x, b.x), std::max(a.x, b.x)),
          y(std::min(a.y, b.y), std::max(a.y, b.y)),
          z(std::min(a.z, b.z), std::max(a.z, b.z)) {
        pad_to_minimums();
    }

    // Box around two boxes.
    AABB(const AABB& a, const AABB& b)
        : x(enclose(a.x, b.x)), y(enclose(a.y, b.y)), z(enclose(a.z, b.z)) {}

    const Interval& axis(int n) const { return n == 0 ? x : (n == 1 ? y : z); }

    int longest_axis() const {
        if (x.size() > y.size()) return x.size() > z.size() ? 0 : 2;
        return y.size() > z.size() ? 1 : 2;
    }

    // Used by SAH: the chance a random ray hits a box is proportional to its surface area.
    double surface_area() const {
        if (x.size() < 0.0 || y.size() < 0.0 || z.size() < 0.0) return 0.0;  // empty
        return 2.0 * (x.size() * y.size() + y.size() * z.size() + z.size() * x.size());
    }

    // Slab method: each axis gives a [t_enter, t_exit] range where the ray is between that
    // axis's two planes. The ray hits the box only if all three ranges overlap.
    bool hit(const Ray& r, Interval ray_t) const {
        const Point3& origin = r.origin();
        const Vec3& dir = r.direction();

        for (int a = 0; a < 3; ++a) {
            const Interval& slab = axis(a);
            const double inv_d = 1.0 / dir[a];  // +-inf for axis-parallel rays, which still works

            double t0 = (slab.min - origin[a]) * inv_d;
            double t1 = (slab.max - origin[a]) * inv_d;
            if (t0 > t1) std::swap(t0, t1);  // ray travelling in the negative direction

            if (t0 > ray_t.min) ray_t.min = t0;
            if (t1 < ray_t.max) ray_t.max = t1;
            if (ray_t.max <= ray_t.min) return false;
        }
        return true;
    }

private:
    // Flat shapes (quads, axis-aligned triangles) have zero thickness on one axis.
    // Give it a sliver of width so the slab test never divides a zero-size range.
    void pad_to_minimums() {
        constexpr double kDelta = 1e-4;
        if (x.size() < kDelta) x = {x.min - kDelta / 2.0, x.max + kDelta / 2.0};
        if (y.size() < kDelta) y = {y.min - kDelta / 2.0, y.max + kDelta / 2.0};
        if (z.size() < kDelta) z = {z.min - kDelta / 2.0, z.max + kDelta / 2.0};
    }
};

}  // namespace kiln