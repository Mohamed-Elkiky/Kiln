#pragma once

#include <cmath>
#include <memory>

#include "kiln/hittable.hpp"
#include "kiln/material.hpp"

namespace kiln {

// A single triangle. The building block of every 3D model.
class Triangle : public Hittable {
public:
    // Flat shading: one normal for the whole face.
    Triangle(const Point3& v0, const Point3& v1, const Point3& v2, std::shared_ptr<Material> mat)
        : v0_(v0), e1_(v1 - v0), e2_(v2 - v0), mat_(std::move(mat)),
          bbox_(AABB(v0, v1), AABB(v0, v2)) {            
        face_normal_ = unit_vector(cross(e1_, e2_));
    }

    // Smooth shading: normals at each vertex, blended across the face.
    Triangle(const Point3& v0, const Point3& v1, const Point3& v2, const Vec3& n0, const Vec3& n1,
             const Vec3& n2, std::shared_ptr<Material> mat)
        : Triangle(v0, v1, v2, std::move(mat)) {
        n0_ = n0;
        n1_ = n1;
        n2_ = n2;
        has_vertex_normals_ = true;
    }

    // Moller-Trumbore: solve O + tD = v0 + u*e1 + v*e2 directly, no plane equation needed.
    bool hit(const Ray& r, Interval ray_t, HitRecord& rec) const override {
        constexpr double kEpsilon = 1e-8;

        const Vec3 pvec = cross(r.direction(), e2_);
        const double det = dot(e1_, pvec);

        // det ~ 0: ray is parallel to the triangle.
        if (std::fabs(det) < kEpsilon) return false;
        const double inv_det = 1.0 / det;

        const Vec3 tvec = r.origin() - v0_;
        const double u = dot(tvec, pvec) * inv_det;
        if (u < 0.0 || u > 1.0) return false;

        const Vec3 qvec = cross(tvec, e1_);
        const double v = dot(r.direction(), qvec) * inv_det;
        if (v < 0.0 || u + v > 1.0) return false;

        const double t = dot(e2_, qvec) * inv_det;
        if (!ray_t.surrounds(t)) return false;

        rec.t = t;
        rec.p = r.at(t);
        rec.mat = mat_.get();

        // Barycentric weights (1 - u - v, u, v) blend the vertex normals for smooth shading.
        const Vec3 outward_normal =
            has_vertex_normals_ ? unit_vector((1.0 - u - v) * n0_ + u * n1_ + v * n2_)
                                : face_normal_;
        rec.set_face_normal(r, outward_normal);
        return true;
    }

    AABB bounding_box() const override { return bbox_; }

private:
    Point3 v0_;
    Vec3 e1_, e2_;  // edges from v0
    Vec3 face_normal_;
    Vec3 n0_, n1_, n2_;
    bool has_vertex_normals_ = false;
    std::shared_ptr<Material> mat_;
    AABB bbox_;
};

}  // namespace kiln