#pragma once

#include <cmath>
#include <memory>

#include "kiln/hittable.hpp"
#include "kiln/material.hpp"

namespace kiln {

// Flat parallelogram: corner Q, edges u and v. Walls, floors, light panels.
class Quad : public Hittable {
public:
    Quad(const Point3& Q, const Vec3& u, const Vec3& v, std::shared_ptr<Material> mat)
        : Q_(Q), u_(u), v_(v), mat_(std::move(mat)) {
        const Vec3 n = cross(u_, v_);
        normal_ = unit_vector(n);
        area_ = n.length();
        D_ = dot(normal_, Q_);
        w_ = n / dot(n, n);  // used to get planar coordinates

        // Box around all four corners (the two diagonals cover them).
        bbox_ = AABB(AABB(Q_, Q_ + u_ + v_), AABB(Q_ + u_, Q_ + v_));
    }

    AABB bounding_box() const override { return bbox_; }

    bool hit(const Ray& r, Interval ray_t, HitRecord& rec) const override {
        const double denom = dot(normal_, r.direction());

        // Ray is parallel to the plane: no hit.
        if (std::fabs(denom) < 1e-8) return false;

        // Where the ray meets the plane.
        const double t = (D_ - dot(normal_, r.origin())) / denom;
        if (!ray_t.surrounds(t)) return false;

        // Express the hit point in quad coordinates (alpha along u, beta along v).
        const Point3 intersection = r.at(t);
        const Vec3 planar_hit = intersection - Q_;
        const double alpha = dot(w_, cross(planar_hit, v_));
        const double beta = dot(w_, cross(u_, planar_hit));

        // Inside the quad only if both are in [0, 1].
        if (alpha < 0.0 || alpha > 1.0 || beta < 0.0 || beta > 1.0) return false;

        rec.t = t;
        rec.p = intersection;
        // after
        rec.mat = mat_.get();
        rec.object = this;
        rec.set_face_normal(r, normal_);
        return true;
    }

    // ---------- Light sampling (next event estimation) ----------

    // Uniformly random point on the quad.
    Point3 random_point() const { return Q_ + random_double() * u_ + random_double() * v_; }
    double area() const { return area_; }
    const Vec3& normal() const { return normal_; }
    const Material& material() const { return *mat_; }

private:
    Point3 Q_;
    Vec3 u_, v_;
    Vec3 w_;
    Vec3 normal_;
    double D_;
    double area_;
    std::shared_ptr<Material> mat_;
    AABB bbox_;
};

}  // namespace kiln
