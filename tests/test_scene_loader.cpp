#include <string>

#include "doctest/doctest.h"
#include "kiln/scene_loader.hpp"

using namespace kiln;

namespace {

const std::string kScenes = KILN_SCENES_DIR;

// The error tests each break one thing in an otherwise valid scene.
const std::string kCamera = R"("camera": { "look_from": [0, 0, 0], "look_at": [0, 0, -1] })";
const std::string kMatte =
    R"("materials": { "matte": { "type": "lambertian", "albedo": [0.5, 0.5, 0.5] } })";

std::string with_object(const std::string& object) {
    return "{ " + kCamera + ", " + kMatte + R"(, "objects": [ )" + object + " ] }";
}

// The error message parse_scene() throws for this text, or "" if it parses.
std::string error_for(const std::string& text) {
    try {
        parse_scene(text, ".", "test.json");
    } catch (const SceneError& e) {
        return e.what();
    }
    return "";
}

}  // namespace

TEST_CASE("showcase scenes load") {
    // Mesh scenes need the downloaded models, so they're not checked here.
    for (const std::string name : {"cornell", "spheres", "triangles"}) {
        CAPTURE(name);
        CHECK_NOTHROW(load_scene(kScenes + "/" + name + ".json"));
    }

    const Scene cornell = load_scene(kScenes + "/cornell.json");
    CHECK(cornell.world.objects().size() == 8);
    CHECK(cornell.camera.image_width == 600);
    CHECK(cornell.camera.samples_per_pixel == 500);
    CHECK(cornell.camera.max_depth == 50);
    CHECK(cornell.camera.vfov == 40.0);
    CHECK(cornell.lights.size() == 1);  // the ceiling light, for next event estimation
}

TEST_CASE("defaults and extras") {
    const Scene minimal = parse_scene("{ " + kCamera + R"(, "objects": [] })");
    CHECK(minimal.world.objects().empty());
    CHECK(minimal.camera.samples_per_pixel == CameraConfig{}.samples_per_pixel);

    const Scene boxed = parse_scene("{\n  // comments are allowed\n  " + kCamera + ", " + kMatte +
                                    R"(, "settings": { "aspect_ratio": [16, 9] },
        "objects": [ { "type": "box", "min": [0, 0, 0], "max": [1, 2, 3], "material": "matte" } ] })");
    CHECK(boxed.world.objects().size() == 6);  // a box is 6 quads
    CHECK(boxed.camera.aspect_ratio == 16.0 / 9.0);
    CHECK(boxed.lights.empty());
}

TEST_CASE("glowing quads and boxes are registered as lights") {
    const Scene glowing = parse_scene("{ " + kCamera + R"(,
        "materials": { "lamp": { "type": "light", "emit": [4, 4, 4] },
                       "dark": { "type": "light", "emit": [0, 0, 0] } },
        "objects": [
          { "type": "box", "min": [0, 0, 0], "max": [1, 1, 1], "material": "lamp" },
          { "type": "quad", "corner": [0, 0, 0], "u": [1, 0, 0], "v": [0, 1, 0], "material": "dark" },
          { "type": "sphere", "center": [0, 5, 0], "radius": 1, "material": "lamp" } ] })");
    // Every face of the glowing box; not the black quad, not the sphere.
    CHECK(glowing.lights.size() == 6);
}

TEST_CASE("every error names the file and the exact field") {
    struct Case {
        const char* name;
        std::string text;
        const char* expected;
    };
    const Case cases[] = {
        {"missing camera", R"({ "objects": [] })", "test.json: missing required field 'camera'"},
        {"unknown material lists the defined ones",
         with_object(
             R"({ "type": "sphere", "center": [0, 0, -1], "radius": 0.5, "material": "glas" })"),
         "objects[0].material: unknown material 'glas' (defined: matte)"},
        {"missing object field",
         with_object(R"({ "type": "sphere", "center": [0, 0, -1], "material": "matte" })"),
         "objects[0]: missing required field 'radius'"},
        {"typo in a field name",
         with_object(
             R"({ "type": "sphere", "center": [0, 0, -1], "raduis": 0.5, "material": "matte" })"),
         "objects[0]: unknown field 'raduis'"},
        {"unknown object type", with_object(R"({ "type": "cylinder", "material": "matte" })"),
         "objects[0].type: unknown object type 'cylinder'"},
        {"vector with 2 numbers",
         with_object(
             R"({ "type": "sphere", "center": [0, 0], "radius": 0.5, "material": "matte" })"),
         "objects[0].center: expected an array of 3 numbers"},
        {"non-number in a vector",
         with_object(
             R"({ "type": "sphere", "center": [0, "a", 0], "radius": 0.5, "material": "matte" })"),
         "objects[0].center[1]: expected a number, got string"},
        {"negative radius",
         with_object(
             R"({ "type": "sphere", "center": [0, 0, -1], "radius": -1, "material": "matte" })"),
         "objects[0].radius: must be greater than 0"},
        {"missing mesh file",
         with_object(R"({ "type": "mesh", "file": "missing.obj", "material": "matte" })"),
         "objects[0].file: could not load mesh"},
        {"unknown material type",
         "{ " + kCamera + R"(, "materials": { "m": { "type": "plastic" } }, "objects": [] })",
         "materials.m.type: unknown material type 'plastic'"},
        {"zero samples", "{ " + kCamera + R"(, "settings": { "samples": 0 }, "objects": [] })",
         "settings.samples: expected a whole number >= 1"},
        {"typo in a section name", "{ " + kCamera + R"(, "setings": {}, "objects": [] })",
         "unknown field 'setings'"},
        {"broken JSON reports the line", R"({ "camera": { "look_from": [0, 0, 0], )",
         "test.json: invalid JSON: parse error at line 1"},
    };

    for (const Case& c : cases) {
        CAPTURE(c.name);
        const std::string message = error_for(c.text);
        CAPTURE(message);
        CHECK(message.find(c.expected) != std::string::npos);
    }
}

TEST_CASE("a missing scene file throws") {
    CHECK_THROWS_AS(load_scene(kScenes + "/does_not_exist.json"), SceneError);
}
