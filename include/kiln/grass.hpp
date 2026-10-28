#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "kiln/material.hpp"
#include "kiln/triangle.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

// A round patch where no grass grows, e.g. under a model so the grass doesn't poke through it.
struct GrassClearing {
    double x = 0.0;
    double z = 0.0;
    double radius = 0.0;
};

struct GrassSettings {
    double x_min = -1.0, z_min = -1.0;  // the rectangle the blades grow in
    double x_max = 1.0, z_max = 1.0;
    double ground = 0.0;     // height (y) of the ground the blades stand on
    int count = 1000;        // number of blades
    double height = 0.3;     // average blade height; each blade is 70% to 130% of it
    double width = 0.02;     // average blade width at the root
    double bend = 0.3;       // how far tips lean over, as a fraction of the height
    std::uint32_t seed = 0;  // same seed, same field
    std::vector<GrassClearing> clearings;
};

struct GrassField {
    std::vector<std::shared_ptr<Triangle>> triangles;
    std::vector<Point3> roots;  // where each blade stands
};

// Each blade is a tapered, curved strip of kTrianglesPerBlade triangles with its own random
// height, width, facing and lean, in one of `materials` picked at random. Uses its own random
// numbers (never the render's), so the same settings give the same field on every platform.
// Throws std::invalid_argument if the clearings leave no room to place the blades.
inline constexpr int kTrianglesPerBlade = 5;
GrassField make_grass(const GrassSettings& settings,
                      const std::vector<std::shared_ptr<Material>>& materials);

}  // namespace kiln