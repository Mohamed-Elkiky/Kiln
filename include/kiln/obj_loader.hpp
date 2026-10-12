#pragma once

#include <memory>
#include <string>
#include <vector>

#include "kiln/material.hpp"
#include "kiln/triangle.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

// Placement applied to every vertex on load, in this order: scale, rotate about Y, translate.
// Lets a model built in any unit system be dropped into a scene without editing the file.
struct Transform {
    double scale = 1.0;
    double rotate_y_degrees = 0.0;
    Vec3 translate{0.0, 0.0, 0.0};

    Point3 apply_point(const Point3& p) const;
    // Directions ignore translation. Scale is uniform, so normals only need rotating.
    Vec3 apply_normal(const Vec3& n) const;
};

struct ObjLoadOptions {
    // Most scanned models (bunny, dragon) ship without normals. When set, a file with no
    // normals gets area-weighted vertex normals computed on load so it still shades smoothly.
    bool generate_normals_if_missing = false;
};

// Read a Wavefront .obj into triangles. Polygons are triangulated, textures are ignored.
// Vertex normals in the file are used for smooth shading; faces without them are flat.
// Degenerate (zero-area) faces are skipped. Throws std::runtime_error if the file can't be read.
std::vector<std::shared_ptr<Triangle>> load_obj(const std::string& path,
                                                std::shared_ptr<Material> material,
                                                const Transform& transform = {},
                                                const ObjLoadOptions& options = {});

}  // namespace kiln