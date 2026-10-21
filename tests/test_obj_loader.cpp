#include <cmath>
#include <stdexcept>
#include <string>

#include "doctest/doctest.h"
#include "kiln/interval.hpp"
#include "kiln/obj_loader.hpp"
#include "kiln/ray.hpp"

using namespace kiln;

namespace {

const Interval kAll(0.001, kInfinity);
const std::string kData = KILN_TEST_DATA_DIR;
const Ray kDown(Point3{0.3, 0.3, 1.0}, Vec3{0, 0, -1});

bool near(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

}  // namespace

TEST_CASE("file with vertex normals") {
    const auto quad = load_obj(kData + "/quad_with_normals.obj", nullptr);
    CHECK(quad.size() == 2);  // the quad face is triangulated

    // Face normal is (0,0,1); the file says (0,0.6,0.8). Smooth shading must use the file's.
    HitRecord rec;
    bool hit = false;
    for (const auto& tri : quad) hit = hit || tri->hit(kDown, kAll, rec);
    REQUIRE(hit);
    CHECK(near(rec.normal, Vec3{0, 0.6, 0.8}, 1e-6));  // tinyobj stores floats
}

TEST_CASE("zero-area faces are skipped") {
    CHECK(load_obj(kData + "/flat_with_degenerate.obj", nullptr).size() == 1);
}

TEST_CASE("transform: scale, then rotate about Y, then move") {
    // The z = 0 triangle ends up in the x = 10 plane.
    Transform t;
    t.scale = 2.0;
    t.rotate_y_degrees = 90.0;
    t.translate = Vec3{10.0, 0.0, 0.0};
    CHECK(near(t.apply_point(Point3{1, 0, 0}), Point3{10, 0, -2}));

    const auto moved = load_obj(kData + "/flat_with_degenerate.obj", nullptr, t);
    REQUIRE_FALSE(moved.empty());
    HitRecord rec;
    REQUIRE(moved[0]->hit(Ray(Point3{20.0, 0.5, -0.5}, Vec3{-1, 0, 0}), kAll, rec));
    CHECK(rec.t == doctest::Approx(10.0));
    CHECK(rec.front_face);
    CHECK(near(rec.normal, Vec3{1, 0, 0}));  // the normal rotates with the mesh
}

TEST_CASE("generated normals are unit length and outward") {
    ObjLoadOptions gen;
    gen.generate_normals_if_missing = true;
    const auto smooth = load_obj(kData + "/flat_with_degenerate.obj", nullptr, {}, gen);
    REQUIRE_FALSE(smooth.empty());
    HitRecord rec;
    REQUIRE(smooth[0]->hit(kDown, kAll, rec));
    CHECK(near(rec.normal, Vec3{0, 0, 1}));
}

TEST_CASE("a missing file throws") {
    CHECK_THROWS_AS(load_obj(kData + "/does_not_exist.obj", nullptr), std::runtime_error);
}
