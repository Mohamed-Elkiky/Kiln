// Ray intersection tests for every primitive: sphere, triangle, quad, and the AABB slab test.

#include <cmath>

#include "doctest/doctest.h"
#include "kiln/aabb.hpp"
#include "kiln/interval.hpp"
#include "kiln/quad.hpp"
#include "kiln/ray.hpp"
#include "kiln/sphere.hpp"
#include "kiln/triangle.hpp"

using namespace kiln;

namespace {

const Interval kAll(0.001, kInfinity);

bool near(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

}  // namespace

TEST_CASE("ray/sphere") {
    const Sphere sphere(Point3{0, 0, -5}, 1.0, nullptr);
    HitRecord rec;

    SUBCASE("head-on hit takes the near side") {
        REQUIRE(sphere.hit(Ray(Point3{0, 0, 0}, Vec3{0, 0, -1}), kAll, rec));
        CHECK(rec.t == doctest::Approx(4.0));
        CHECK(near(rec.p, Point3{0, 0, -4}));
        CHECK(near(rec.normal, Vec3{0, 0, 1}));
        CHECK(rec.front_face);
        CHECK(rec.object == &sphere);
    }
    SUBCASE("direction length doesn't change the hit point") {
        REQUIRE(sphere.hit(Ray(Point3{0, 0, 0}, Vec3{0, 0, -2}), kAll, rec));
        CHECK(rec.t == doctest::Approx(2.0));
        CHECK(near(rec.p, Point3{0, 0, -4}));
    }
    SUBCASE("miss beside the sphere") {
        CHECK_FALSE(sphere.hit(Ray(Point3{0, 2, 0}, Vec3{0, 0, -1}), kAll, rec));
    }
    SUBCASE("sphere behind the ray") {
        CHECK_FALSE(sphere.hit(Ray(Point3{0, 0, 0}, Vec3{0, 0, 1}), kAll, rec));
    }
    SUBCASE("from inside: far side, normal flipped to face the ray") {
        REQUIRE(sphere.hit(Ray(Point3{0, 0, -5}, Vec3{0, 0, -1}), kAll, rec));
        CHECK(rec.t == doctest::Approx(1.0));
        CHECK_FALSE(rec.front_face);
        CHECK(near(rec.normal, Vec3{0, 0, 1}));
    }
    SUBCASE("hit beyond t_max is ignored") {
        CHECK_FALSE(sphere.hit(Ray(Point3{0, 0, 0}, Vec3{0, 0, -1}), Interval(0.001, 3.0), rec));
    }
    SUBCASE("grazing ray touches the edge") {
        REQUIRE(sphere.hit(Ray(Point3{0, 1, 0}, Vec3{0, 0, -1}), kAll, rec));
        CHECK(rec.t == doctest::Approx(5.0));
    }
}

TEST_CASE("ray/triangle") {
    // In the z = 0 plane: (0,0,0), (1,0,0), (0,1,0)
    const Triangle tri(Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{0, 1, 0}, nullptr);
    HitRecord rec;

    SUBCASE("ray through the middle") {
        REQUIRE(tri.hit(Ray(Point3{0.25, 0.25, 1.0}, Vec3{0, 0, -1}), kAll, rec));
        CHECK(rec.t == doctest::Approx(1.0));
        CHECK(near(rec.normal, Vec3{0, 0, 1}));
        CHECK(rec.object == &tri);
    }
    SUBCASE("from behind: normal flipped to face the ray") {
        REQUIRE(tri.hit(Ray(Point3{0.25, 0.25, -1.0}, Vec3{0, 0, 1}), kAll, rec));
        CHECK_FALSE(rec.front_face);
        CHECK(near(rec.normal, Vec3{0, 0, -1}));
    }
    SUBCASE("misses") {
        CHECK_FALSE(tri.hit(Ray(Point3{2.0, 2.0, 1.0}, Vec3{0, 0, -1}), kAll, rec));
        CHECK_FALSE(tri.hit(Ray(Point3{0.6, 0.6, 1.0}, Vec3{0, 0, -1}), kAll, rec));   // u + v > 1
        CHECK_FALSE(tri.hit(Ray(Point3{0.25, 0.25, 1.0}, Vec3{1, 0, 0}), kAll, rec));  // parallel
        CHECK_FALSE(tri.hit(Ray(Point3{0.25, 0.25, 1.0}, Vec3{0, 0, 1}), kAll, rec));  // behind
    }
    SUBCASE("smooth shading blends the vertex normals") {
        const Vec3 up{0, 0, 1};
        const Vec3 tilted = unit_vector(Vec3{1, 0, 1});
        const Triangle smooth(Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{0, 1, 0}, up, tilted, up,
                              nullptr);
        REQUIRE(smooth.hit(Ray(Point3{0.99, 0.005, 1.0}, Vec3{0, 0, -1}), kAll, rec));
        CHECK(rec.normal.x > 0.6);  // close to the tilted corner
        CHECK(rec.normal.length() == doctest::Approx(1.0));
    }
}

TEST_CASE("ray/quad") {
    // 2 x 3 panel in the z = 0 plane, corner at the origin.
    const Quad quad(Point3{0, 0, 0}, Vec3{2, 0, 0}, Vec3{0, 3, 0}, nullptr);
    HitRecord rec;

    REQUIRE(quad.hit(Ray(Point3{1, 1, 4}, Vec3{0, 0, -1}), kAll, rec));
    CHECK(rec.t == doctest::Approx(4.0));
    CHECK(near(rec.normal, Vec3{0, 0, 1}));
    CHECK(rec.object == &quad);

    CHECK_FALSE(quad.hit(Ray(Point3{2.5, 1, 4}, Vec3{0, 0, -1}), kAll, rec));  // past u
    CHECK_FALSE(quad.hit(Ray(Point3{1, 3.5, 4}, Vec3{0, 0, -1}), kAll, rec));  // past v
    CHECK_FALSE(quad.hit(Ray(Point3{1, 1, 4}, Vec3{1, 0, 0}), kAll, rec));     // parallel
    CHECK(quad.area() == doctest::Approx(6.0));
}

TEST_CASE("ray/AABB") {
    const AABB box(Point3{0, 0, 0}, Point3{1, 2, 3});

    CHECK(box.hit(Ray(Point3{0.5, 1, -5}, Vec3{0, 0, 1}), Interval(0, kInfinity)));
    CHECK_FALSE(box.hit(Ray(Point3{5, 1, -5}, Vec3{0, 0, 1}), Interval(0, kInfinity)));
    CHECK_FALSE(box.hit(Ray(Point3{0.5, 1, -5}, Vec3{0, 0, -1}), Interval(0, kInfinity)));
    CHECK_FALSE(box.hit(Ray(Point3{0.5, 1, -5}, Vec3{0, 0, 1}), Interval(0, 2)));    // beyond t_max
    CHECK(box.hit(Ray(Point3{0.5, 1, 1}, Vec3{1, 1, 1}), Interval(0, kInfinity)));   // from inside
    CHECK(box.hit(Ray(Point3{-1, -1, -1}, Vec3{1, 1, 1}), Interval(0, kInfinity)));  // diagonal

    CHECK(box.surface_area() == doctest::Approx(22.0));
    CHECK(box.longest_axis() == 2);

    // Flat shapes get padded so the slab test never divides a zero-width range.
    const AABB flat(Point3{0, 0, 0}, Point3{1, 1, 0});
    CHECK(flat.z.size() > 0.0);
    CHECK(flat.hit(Ray(Point3{0.5, 0.5, 1}, Vec3{0, 0, -1}), Interval(0, kInfinity)));
}
