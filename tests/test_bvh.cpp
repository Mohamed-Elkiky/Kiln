#include <cmath>
#include <memory>
#include <vector>

#include "doctest/doctest.h"
#include "kiln/bvh.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/interval.hpp"
#include "kiln/random.hpp"
#include "kiln/ray.hpp"
#include "kiln/sphere.hpp"
#include "kiln/triangle.hpp"

using namespace kiln;

namespace {

const Interval kAll(0.001, kInfinity);

// Fire many random rays at both structures. The BVH must find exactly the same closest hit
// as testing every object: same hit/miss, same t. That is what "renders identically" means.
int count_mismatches(const HittableList& list, const BVH& bvh, int rays) {
    int mismatches = 0;
    for (int i = 0; i < rays; ++i) {
        const Ray r(random_vec3(-12.0, 12.0), random_unit_vector());
        HitRecord a, b;
        const bool hit_a = list.hit(r, kAll, a);
        const bool hit_b = bvh.hit(r, kAll, b);
        if (hit_a != hit_b || (hit_a && a.t != b.t)) ++mismatches;
    }
    return mismatches;
}

// 2000 random triangles, 20 spheres, and a huge floor sphere whose box dwarfs everything else.
HittableList random_scene() {
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
    list.add(std::make_shared<Sphere>(Point3{0, -1000, 0}, 990, nullptr));
    return list;
}

}  // namespace

TEST_CASE("BVH returns the same closest hit as brute force on 20k random rays") {
    seed_rng(42);
    const HittableList list = random_scene();

    SUBCASE("SAH") {
        const BVH sah(list.objects(), BVHSplit::kSAH);
        CHECK(count_mismatches(list, sah, 20000) == 0);
    }
    SUBCASE("midpoint") {
        const BVH midpoint(list.objects(), BVHSplit::kMidpoint);
        CHECK(count_mismatches(list, midpoint, 20000) == 0);
    }
}

TEST_CASE("BVH bounding box covers every object") {
    seed_rng(7);
    const HittableList list = random_scene();
    const BVH bvh(list.objects());
    const AABB box = bvh.bounding_box();
    const AABB expected = list.bounding_box();
    for (int a = 0; a < 3; ++a) {
        CHECK(box.axis(a).min == expected.axis(a).min);
        CHECK(box.axis(a).max == expected.axis(a).max);
    }
    CHECK(bvh.node_count() < 2 * list.objects().size());
}

TEST_CASE("BVH edge cases") {
    HitRecord rec;

    SUBCASE("single object") {
        HittableList one;
        one.add(std::make_shared<Sphere>(Point3{0, 0, -5}, 1, nullptr));
        const BVH single(one.objects());
        REQUIRE(single.hit(Ray(Point3{0, 0, 0}, Vec3{0, 0, -1}), kAll, rec));
        CHECK(rec.t == doctest::Approx(4.0));
    }
    SUBCASE("identical centroids: no axis can be split, must still build and work") {
        HittableList stacked;
        for (int i = 0; i < 50; ++i) {
            stacked.add(std::make_shared<Sphere>(Point3{0, 0, -5}, 0.5 + 0.01 * i, nullptr));
        }
        const BVH bvh(stacked.objects());
        REQUIRE(bvh.hit(Ray(Point3{0, 0, 0}, Vec3{0, 0, -1}), kAll, rec));
        CHECK(rec.t == doctest::Approx(5.0 - 0.99));
    }
    SUBCASE("empty") {
        const BVH empty(std::vector<std::shared_ptr<Hittable>>{});
        CHECK_FALSE(empty.hit(Ray(Point3{0, 0, 0}, Vec3{0, 0, -1}), kAll, rec));
        CHECK(empty.node_count() == 0);
    }
    SUBCASE("badly balanced input: every object is still found") {
        // Exponentially spaced spheres: most of the scene is crammed into one end of the box.
        HittableList chain;
        for (int i = 0; i < 400; ++i) {
            chain.add(std::make_shared<Sphere>(Point3{std::pow(1.05, i), 0, 0}, 0.01, nullptr));
        }
        const BVH bvh(chain.objects());
        int hits = 0;
        for (int i = 0; i < 400; ++i) {
            const Ray r(Point3{std::pow(1.05, i), 1, 0}, Vec3{0, -1, 0});
            if (bvh.hit(r, kAll, rec)) ++hits;
        }
        CHECK(hits == 400);
    }
}
