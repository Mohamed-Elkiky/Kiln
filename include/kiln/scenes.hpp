#pragma once

#include "kiln/camera.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

// Each scene fills in the world, the camera settings and the background colour.
// Mesh scenes load from assets/models/ (run from the repo root) and throw if a model is missing.
using SceneBuilder = void (*)(HittableList& world, CameraConfig& config, Color& background);

// Three spheres (glass, matte, gold) lit by a ceiling panel. Sprint 2 materials test.
void spheres_under_light(HittableList& world, CameraConfig& config, Color& background);
// Classic Cornell box: red/green walls, white room, ceiling light, glass + mirror spheres.
void cornell_box(HittableList& world, CameraConfig& config, Color& background);
// A single triangle and a tetrahedron built from 4 triangles. Sprint 3 geometry test.
void triangles_scene(HittableList& world, CameraConfig& config, Color& background);
// Utah teapot, ~6.3k triangles.
void teapot_scene(HittableList& world, CameraConfig& config, Color& background);
// Sir Hops-a-Lot, the Stanford bunny, ~69k triangles.
void bunny_scene(HittableList& world, CameraConfig& config, Color& background);
// XYZ RGB dragon, ~250k triangles.
void dragon_scene(HittableList& world, CameraConfig& config, Color& background);

}  // namespace kiln
