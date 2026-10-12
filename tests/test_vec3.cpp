#include <cmath>
#include <cstdlib>
#include <iostream>

#include "kiln/vec3.hpp"

using kiln::Vec3;

namespace {

int failures = 0;

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

bool near(const Vec3& a, const Vec3& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

void check(bool ok, const char* name) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!ok) ++failures;
}

}  // namespace

int main() {
    const Vec3 a{1, 2, 3};
    const Vec3 b{4, 5, 6};

    check(near(a + b, Vec3{5, 7, 9}), "addition");
    check(near(b - a, Vec3{3, 3, 3}), "subtraction");
    check(near(2.0 * a, Vec3{2, 4, 6}), "scalar multiply");
    check(near(a / 2.0, Vec3{0.5, 1, 1.5}), "scalar divide");
    check(near(-a, Vec3{-1, -2, -3}), "negation");

    check(near(kiln::dot(a, b), 32.0), "dot product");
    check(near(kiln::dot(Vec3{1, 0, 0}, Vec3{0, 1, 0}), 0.0), "dot of perpendicular = 0");

    check(near(kiln::cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}), Vec3{0, 0, 1}), "cross x * y = z");
    const Vec3 c = kiln::cross(a, b);
    check(near(kiln::dot(c, a), 0.0) && near(kiln::dot(c, b), 0.0), "cross is perpendicular");

    check(near(Vec3{3, 4, 0}.length(), 5.0), "length");
    check(near(kiln::unit_vector(Vec3{10, 0, 0}), Vec3{1, 0, 0}), "unit_vector");
    check(near(kiln::unit_vector(b).length(), 1.0), "unit_vector has length 1");

    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests FAILED\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}