#pragma once

#include <stdexcept>
#include <string>

#include "kiln/camera.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

// Everything a render needs: the objects, the camera and the background colour.
struct Scene {
    HittableList world;
    CameraConfig camera;
    Color background{0.0, 0.0, 0.0};
};

// Any problem with a scene file. what() names the file and the exact field, e.g.
//   scenes/cornell.json: objects[6].material: unknown material 'glas' (defined: glass, red)
class SceneError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Load a JSON scene file. Mesh paths inside it are relative to the scene file's folder,
// so a scene renders the same whichever directory kiln is run from.
Scene load_scene(const std::string& path);

// Same, from JSON text already in memory. base_dir resolves mesh paths; source names the
// text in error messages.
Scene parse_scene(const std::string& text, const std::string& base_dir = ".",
                  const std::string& source = "<scene>");

}  // namespace kiln