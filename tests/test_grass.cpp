#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

#include "doctest/doctest.h"
#include "kiln/grass.hpp"
#include "kiln/material.hpp"

using namespace kiln;

namespace {

GrassSettings meadow(std::uint32_t seed = 7) {
    GrassSettings s;
    s.x_min = -2.0;
    s.x_max = 3.0;
    s.z_min = -1.0;
    s.z_max = 1.5;
    s.ground = 0.25;
    s.count = 2000;
    s.height = 0.3;
    s.width = 0.02;
    s.bend = 0.4;
    s.seed = seed;
    return s;
}

std::vector<std::shared_ptr<Material>> greens() {
    return {std::make_shared<Lambertian>(Color{0.2, 0.5, 0.1}),
            std::make_shared<Lambertian>(Color{0.3, 0.6, 0.15})};
}

bool same_boxes(const GrassField& a, const GrassField& b) {
    if (a.triangles.size() != b.triangles.size()) return false;
    for (std::size_t i = 0; i < a.triangles.size(); ++i) {
        const AABB p = a.triangles[i]->bounding_box();
        const AABB q = b.triangles[i]->bounding_box();
        for (int axis = 0; axis < 3; ++axis) {
            if (p.axis(axis).min != q.axis(axis).min || p.axis(axis).max != q.axis(axis).max) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("blade count matches") {
    const GrassField field = make_grass(meadow(), greens());
    CHECK(field.roots.size() == 2000);
    CHECK(field.triangles.size() == 2000 * kTrianglesPerBlade);
}

TEST_CASE("the field stays inside the area") {
    const GrassSettings s = meadow();
    const GrassField field = make_grass(s, greens());

    for (const Point3& root : field.roots) {
        CHECK(root.x >= s.x_min);
        CHECK(root.x <= s.x_max);
        CHECK(root.z >= s.z_min);
        CHECK(root.z <= s.z_max);
        CHECK(root.y == s.ground);
    }

    // Tips can lean past the edge by at most the tallest blade's lean, plus half a width.
    const double tallest = 1.3 * s.height;
    const double reach = s.bend * tallest + s.width;
    for (const auto& tri : field.triangles) {
        const AABB box = tri->bounding_box();
        CHECK(box.axis(0).min >= s.x_min - reach);
        CHECK(box.axis(0).max <= s.x_max + reach);
        CHECK(box.axis(2).min >= s.z_min - reach);
        CHECK(box.axis(2).max <= s.z_max + reach);
        CHECK(box.axis(1).min >= s.ground - 1e-3);  // AABBs are padded slightly
        CHECK(box.axis(1).max <= s.ground + tallest + 1e-3);
    }
}

TEST_CASE("the same seed gives the same field, a new seed a new one") {
    const GrassField a = make_grass(meadow(7), greens());
    const GrassField b = make_grass(meadow(7), greens());
    const GrassField c = make_grass(meadow(8), greens());
    CHECK(same_boxes(a, b));
    CHECK_FALSE(same_boxes(a, c));
}

TEST_CASE("no grass grows in a clearing") {
    GrassSettings s = meadow();
    s.clearings.push_back({0.5, 0.25, 0.6});
    const GrassField field = make_grass(s, greens());
    CHECK(field.roots.size() == 2000);
    for (const Point3& root : field.roots) {
        CHECK(std::hypot(root.x - 0.5, root.z - 0.25) >= 0.6);
    }

    GrassSettings bare = meadow();
    bare.clearings.push_back({0.0, 0.0, 100.0});  // covers everything
    CHECK_THROWS_AS(make_grass(bare, greens()), std::invalid_argument);
}