#pragma once

#include "kiln/aabb.hpp"
#include "kiln/interval.hpp"
#include "kiln/ray.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

class Material;  // forward declaration: material.hpp includes this file
class Hittable;

struct HitRecord {
    Point3 p;
    Vec3 normal;                       // always points against the incoming ray
    const Material* mat = nullptr;     // non-owning: the object owns its material
    const Hittable* object = nullptr;  // the primitive hit; lets the renderer spot sampled lights
    double t = 0.0;
    bool front_face = false;  // true if the ray hit the outside of the surface

    // outward_normal must be unit length.
    void set_face_normal(const Ray& r, const Vec3& outward_normal) {
        front_face = dot(r.direction(), outward_normal) < 0.0;
        normal = front_face ? outward_normal : -outward_normal;
    }
};

// Anything a ray can hit. Every shape (sphere, triangle, BVH node) implements this.
class Hittable {
public:
    virtual ~Hittable() = default;

    virtual bool hit(const Ray& r, Interval ray_t, HitRecord& rec) const = 0;

    // Box that fully contains the object. The BVH is built from these.
    virtual AABB bounding_box() const = 0;
};

}  // namespace kiln
