#include "kiln/obj_loader.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

namespace kiln {

namespace {

constexpr double kPi = 3.1415926535897932385;

// Below this, a face's cross product is treated as zero area (its normal would be NaN).
constexpr double kDegenerateArea = 1e-12;

Vec3 rotate_y(const Vec3& v, double degrees) {
    const double rad = degrees * kPi / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}

}  // namespace

Point3 Transform::apply_point(const Point3& p) const {
    return rotate_y(scale * p, rotate_y_degrees) + translate;
}

Vec3 Transform::apply_normal(const Vec3& n) const {
    // A negative scale mirrors the model, which flips which side the normal is on.
    const Vec3 r = rotate_y(n, rotate_y_degrees);
    return scale < 0.0 ? -r : r;
}

std::vector<std::shared_ptr<Triangle>> load_obj(const std::string& path,
                                                std::shared_ptr<Material> material,
                                                const Transform& transform,
                                                const ObjLoadOptions& options) {
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;  // quads and n-gons become triangles
    config.vertex_color = false;
    config.mtl_search_path = "";  // materials come from the scene, not the .mtl

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config)) {
        throw std::runtime_error("load_obj: " + path + ": " + reader.Error());
    }
    if (!reader.Warning().empty()) {
        std::clog << "load_obj warning (" << path << "): " << reader.Warning();
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();

    // Transform every position once up front, rather than once per face that uses it.
    const std::size_t vertex_count = attrib.vertices.size() / 3;
    std::vector<Point3> positions(vertex_count);
    for (std::size_t i = 0; i < vertex_count; ++i) {
        positions[i] = transform.apply_point(
            {attrib.vertices[3 * i], attrib.vertices[3 * i + 1], attrib.vertices[3 * i + 2]});
    }

    const std::size_t normal_count = attrib.normals.size() / 3;
    std::vector<Vec3> normals(normal_count);
    for (std::size_t i = 0; i < normal_count; ++i) {
        const Vec3 n{attrib.normals[3 * i], attrib.normals[3 * i + 1], attrib.normals[3 * i + 2]};
        normals[i] = unit_vector(transform.apply_normal(n));
    }

    // Generated normals: sum each face's un-normalised cross product (length = 2x area) into
    // its corners, so big faces pull harder than slivers. Indexed by position, not normal.
    const bool generate = options.generate_normals_if_missing && normal_count == 0;
    std::vector<Vec3> generated;
    if (generate) {
        generated.assign(vertex_count, Vec3{0.0, 0.0, 0.0});
        for (const auto& shape : shapes) {
            const auto& idx = shape.mesh.indices;
            for (std::size_t f = 0; f + 2 < idx.size(); f += 3) {
                const int a = idx[f].vertex_index;
                const int b = idx[f + 1].vertex_index;
                const int c = idx[f + 2].vertex_index;
                const Vec3 n = cross(positions[b] - positions[a], positions[c] - positions[a]);
                generated[a] += n;
                generated[b] += n;
                generated[c] += n;
            }
        }
        for (auto& n : generated) {
            if (n.length_squared() > 0.0) n = unit_vector(n);
        }
    }

    std::vector<std::shared_ptr<Triangle>> triangles;
    std::size_t skipped = 0;

    for (const auto& shape : shapes) {
        const auto& idx = shape.mesh.indices;
        triangles.reserve(triangles.size() + idx.size() / 3);

        for (std::size_t f = 0; f + 2 < idx.size(); f += 3) {
            const std::array<tinyobj::index_t, 3> corner{idx[f], idx[f + 1], idx[f + 2]};
            const Point3& p0 = positions[corner[0].vertex_index];
            const Point3& p1 = positions[corner[1].vertex_index];
            const Point3& p2 = positions[corner[2].vertex_index];

            if (cross(p1 - p0, p2 - p0).length_squared() < kDegenerateArea * kDegenerateArea) {
                ++skipped;
                continue;
            }

            const bool file_normals = corner[0].normal_index >= 0 && corner[1].normal_index >= 0 &&
                                      corner[2].normal_index >= 0;
            if (file_normals) {
                triangles.push_back(std::make_shared<Triangle>(
                    p0, p1, p2, normals[corner[0].normal_index], normals[corner[1].normal_index],
                    normals[corner[2].normal_index], material));
            } else if (generate) {
                triangles.push_back(
                    std::make_shared<Triangle>(p0, p1, p2, generated[corner[0].vertex_index],
                                               generated[corner[1].vertex_index],
                                               generated[corner[2].vertex_index], material));
            } else {
                triangles.push_back(std::make_shared<Triangle>(p0, p1, p2, material));
            }
        }
    }

    if (skipped > 0) {
        std::clog << "load_obj: skipped " << skipped << " degenerate face(s) in " << path << '\n';
    }
    return triangles;
}

}  // namespace kiln
