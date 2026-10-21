#include <cmath>

#include "doctest/doctest.h"
#include "kiln/vec3.hpp"

using namespace kiln;

namespace {

bool near(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

const Vec3 a{1, 2, 3};
const Vec3 b{4, 5, 6};

}  // namespace

TEST_CASE("arithmetic") {
    CHECK(near(a + b, Vec3{5, 7, 9}));
    CHECK(near(b - a, Vec3{3, 3, 3}));
    CHECK(near(2.0 * a, Vec3{2, 4, 6}));
    CHECK(near(a * 2.0, Vec3{2, 4, 6}));
    CHECK(near(a / 2.0, Vec3{0.5, 1, 1.5}));
    CHECK(near(-a, Vec3{-1, -2, -3}));
    CHECK(near(a * b, Vec3{4, 10, 18}));  // component-wise, for tinting colours

    Vec3 v = a;
    v += b;
    v -= Vec3{1, 1, 1};
    v *= 2.0;
    CHECK(near(v, Vec3{8, 12, 16}));
}

TEST_CASE("indexing") {
    CHECK(a[0] == 1.0);
    CHECK(a[1] == 2.0);
    CHECK(a[2] == 3.0);
    Vec3 v;
    v[1] = 7.0;
    CHECK(near(v, Vec3{0, 7, 0}));
}

TEST_CASE("dot and cross") {
    CHECK(dot(a, b) == doctest::Approx(32.0));
    CHECK(dot(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == doctest::Approx(0.0));

    CHECK(near(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}), Vec3{0, 0, 1}));
    const Vec3 c = cross(a, b);
    CHECK(dot(c, a) == doctest::Approx(0.0));
    CHECK(dot(c, b) == doctest::Approx(0.0));
}

TEST_CASE("length and unit vectors") {
    CHECK(Vec3{3, 4, 0}.length() == doctest::Approx(5.0));
    CHECK(Vec3{3, 4, 0}.length_squared() == doctest::Approx(25.0));
    CHECK(near(unit_vector(Vec3{10, 0, 0}), Vec3{1, 0, 0}));
    CHECK(unit_vector(b).length() == doctest::Approx(1.0));
    CHECK(Vec3{1e-9, -1e-9, 0}.near_zero());
    CHECK_FALSE(Vec3{1e-3, 0, 0}.near_zero());
}

TEST_CASE("reflect and refract") {
    // A ray going down-right bounces up-right off a floor.
    CHECK(near(reflect(Vec3{1, -1, 0}, Vec3{0, 1, 0}), Vec3{1, 1, 0}));

    // Equal refractive indices: the ray passes straight through.
    const Vec3 in = unit_vector(Vec3{1, -1, 0});
    CHECK(near(refract(in, Vec3{0, 1, 0}, 1.0), in));

    // Air into glass bends towards the normal: Snell's law, sin(out) = sin(in) / 1.5.
    const Vec3 out = refract(in, Vec3{0, 1, 0}, 1.0 / 1.5);
    CHECK(out.length() == doctest::Approx(1.0));
    CHECK(out.x == doctest::Approx(std::sin(std::acos(-in.y)) / 1.5));
    CHECK(out.y < 0.0);
}
