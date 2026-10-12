#pragma once

#include <algorithm>
#include <cmath>
#include <memory>

#include "kiln/hittable.hpp"
#include "kiln/material.hpp"

namespace kiln {

class Sphere : public Hittable {
public:
    Sphere(const Point3& center, double radius, std::shared_ptr<Material> mat)
        : center_(center), radius_(std::max(0.0, radius)), mat_(std::move(mat)) {
        const Vec3 r{radius_, radius_, radius_};
        bbox_ = AABB(center_ - r, center_ + r);
    }
    bool hit(const Ray& r, Interval ray_t, HitRecord& rec) const override {
        // Solve |O + tD - C|^2 = r^2 for t, using h = b / 2 to drop the factors of 2.
        const Vec3 oc = center_ - r.origin();
        const double a = r.direction().length_squared();
        const double h = dot(r.direction(), oc);
        const double c = oc.length_squared() - radius_ * radius_;

        const double discriminant = h * h - a * c;
        if (discriminant < 0.0) return false;  // ray misses entirely

        const double sqrtd = std::sqrt(discriminant);

        // Nearest root first; fall back to the far root (ray starts inside the sphere).
        double root = (h - sqrtd) / a;
        if (!ray_t.surrounds(root)) {
            root = (h + sqrtd) / a;
            if (!ray_t.surrounds(root)) return false;
        }

        rec.t = root;
        rec.p = r.at(root);
        const Vec3 outward_normal = (rec.p - center_) / radius_;
        rec.set_face_normal(r, outward_normal);
        rec.mat = mat_.get();
        return true;
    }

    AABB bounding_box() const override { return bbox_; }

private:
    Point3 center_;
    double radius_;
    std::shared_ptr<Material> mat_;
    AABB bbox_;
};

}  // namespace kiln