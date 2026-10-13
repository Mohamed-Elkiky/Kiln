#include <cmath>
#include <cstdlib>
#include <iostream>

#include "kiln/interval.hpp"
#include "kiln/ray.hpp"
#include "kiln/triangle.hpp"

using namespace kiln;

namespace {

int failures = 0;

void check(bool ok, const char* name) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!ok) ++failures;
}

}  // namespace

int main() {
    // Triangle in the z = 0 plane: (0,0,0), (1,0,0), (0,1,0)
    const Triangle tri(Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{0, 1, 0}, nullptr);
    const Interval all(0.001, kInfinity);
    HitRecord rec;

    const Ray through_centre(Point3{0.25, 0.25, 1.0}, Vec3{0, 0, -1});
    const bool hit = tri.hit(through_centre, all, rec);
    check(hit, "ray through centre hits");
    check(hit && std::fabs(rec.t - 1.0) < 1e-9, "hit distance t = 1");
    check(hit && std::fabs(rec.normal.z - 1.0) < 1e-9, "normal faces the ray");

    const Ray outside(Point3{2.0, 2.0, 1.0}, Vec3{0, 0, -1});
    check(!tri.hit(outside, all, rec), "ray outside misses");

    const Ray past_hypotenuse(Point3{0.6, 0.6, 1.0}, Vec3{0, 0, -1});
    check(!tri.hit(past_hypotenuse, all, rec), "ray past the long edge misses (u + v > 1)");

    const Ray parallel(Point3{0.25, 0.25, 1.0}, Vec3{1, 0, 0});
    check(!tri.hit(parallel, all, rec), "parallel ray misses");

    const Ray behind(Point3{0.25, 0.25, 1.0}, Vec3{0, 0, 1});
    check(!tri.hit(behind, all, rec), "triangle behind the ray misses");

    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests FAILED\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
