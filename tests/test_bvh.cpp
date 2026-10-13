#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "kiln/aabb.hpp"
#include "kiln/bvh.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/interval.hpp"
#include "kiln/random.hpp"
#include "kiln/ray.hpp"
#include "kiln/sphere.hpp"
#include "kiln/triangle.hpp"

using namespace kiln;

namespace {

int failures = 0;

void check(bool ok, const char* name) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!ok) ++failures;
}

// Fire many random rays at both structures. The BVH must find exactly the same closest hit
// as testing every object: same hit/miss, same t. That is what "renders identically" means.
int count_mismatches(const HittableList& list, const BVHNode& bvh, int rays) {
    int mismatches = 0;
    const Interval all(0.001, kInfinity);
    for (int i = 0; i < rays; ++i) {
        const Ray r(random_vec3(-12.0, 12.0), random_unit_vector());
        HitRecord a, b;
        const bool hit_a = list.hit(r, all, a);
        const bool hit_b = bvh.hit(r, all, b);
        if (hit_a != hit_b || (hit_a && a.t != b.t)) ++mismatches;
    }
    return mismatches;
}

}  // namespace

int main() {
    seed_rng(42);

    // ---------- AABB ----------
    const AABB box(Point3{0, 0, 0}, Point3{1, 2, 3});
    check(box.hit(Ray(Point3{0.5, 1, -5}, Vec3{0, 0, 1}), Interval(0, kInfinity)),
          "ray through box hits");
    check(!box.hit(Ray(Point3{5, 1, -5}, Vec3{0, 0, 1}), Interval(0, kInfinity)),
          "ray beside box misses");
    check(!box.hit(Ray(Point3{0.5, 1, -5}, Vec3{0, 0, -1}), Interval(0, kInfinity)),
          "box behind ray misses");
    check(!box.hit(Ray(Point3{0.5, 1, -5}, Vec3{0, 0, 1}), Interval(0, 2)),
          "box beyond t_max misses");
    check(std::fabs(box.surface_area() - 22.0) < 1e-12, "surface area 2(ab + bc + ca)");
    check(box.longest_axis() == 2, "longest axis is z");

    const AABB flat(Point3{0, 0, 0}, Point3{1, 1, 0});
    check(flat.z.size() > 0.0, "flat box is padded so slabs never have zero width");
    check(flat.hit(Ray(Point3{0.5, 0.5, 1}, Vec3{0, 0, -1}), Interval(0, kInfinity)),
          "padded flat box is hit");

    // ---------- Random scene ----------
    HittableList list;
    for (int i = 0; i < 2000; ++i) {
        const Point3 p = random_vec3(-10.0, 10.0);
        list.add(std::make_shared<Triangle>(p, p + random_vec3(-1.0, 1.0),
                                            p + random_vec3(-1.0, 1.0), nullptr));
    }
    for (int i = 0; i < 20; ++i) {
        list.add(
            std::make_shared<Sphere>(random_vec3(-10.0, 10.0), random_double(0.2, 1.5), nullptr));
    }
    // A huge sphere like the scenes' floor: its box dwarfs everything else.
    list.add(std::make_shared<Sphere>(Point3{0, -1000, 0}, 990, nullptr));

    const BVHNode sah(list.objects(), BVHSplit::kSAH);
    const BVHNode midpoint(list.objects(), BVHSplit::kMidpoint);

    check(count_mismatches(list, sah, 20000) == 0, "SAH BVH matches brute force on 20k rays");
    check(count_mismatches(list, midpoint, 20000) == 0,
          "midpoint BVH matches brute force on 20k rays");

    // ---------- Edge cases ----------
    HittableList one;
    one.add(std::make_shared<Sphere>(Point3{0, 0, -5}, 1, nullptr));
    const BVHNode single(one.objects());
    HitRecord rec;
    check(single.hit(Ray(Point3{0, 0, 0}, Vec3{0, 0, -1}), Interval(0.001, kInfinity), rec) &&
              std::fabs(rec.t - 4.0) < 1e-12,
          "single-object BVH hits at t = 4");

    // Many objects sharing one centroid: no axis can be split, must still build and work.
    HittableList stacked;
    for (int i = 0; i < 50; ++i) {
        stacked.add(std::make_shared<Sphere>(Point3{0, 0, -5}, 0.5 + 0.01 * i, nullptr));
    }
    const BVHNode stacked_bvh(stacked.objects());
    check(stacked_bvh.hit(Ray(Point3{0, 0, 0}, Vec3{0, 0, -1}), Interval(0.001, kInfinity), rec) &&
              std::fabs(rec.t - (5.0 - 0.99)) < 1e-9,
          "identical centroids: builds and finds the outermost sphere");

    const BVHNode empty(std::vector<std::shared_ptr<Hittable>>{});
    check(!empty.hit(Ray(Point3{0, 0, 0}, Vec3{0, 0, -1}), Interval(0.001, kInfinity), rec),
          "empty BVH never hits");

    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests FAILED\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
