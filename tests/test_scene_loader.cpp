#include <cstdlib>
#include <iostream>
#include <string>

#include "kiln/scene_loader.hpp"

using namespace kiln;

namespace {

int failures = 0;

void check(bool ok, const std::string& name) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!ok) ++failures;
}

// The text must be rejected, and the message must contain `expected`.
void check_error(const std::string& text, const std::string& expected, const std::string& name) {
    std::string message;
    try {
        parse_scene(text, ".", "test.json");
    } catch (const SceneError& e) {
        message = e.what();
    }
    const bool ok = message.find(expected) != std::string::npos;
    check(ok, name);
    if (!ok) std::cout << "       got: " << (message.empty() ? "(no error)" : message) << '\n';
}

const std::string kScenes = KILN_SCENES_DIR;

// The error tests each break one thing in an otherwise valid scene.
const std::string kCamera = R"("camera": { "look_from": [0, 0, 0], "look_at": [0, 0, -1] })";
const std::string kMatte =
    R"("materials": { "matte": { "type": "lambertian", "albedo": [0.5, 0.5, 0.5] } })";

std::string with_object(const std::string& object) {
    return "{ " + kCamera + ", " + kMatte + R"(, "objects": [ )" + object + " ] }";
}

}  // namespace

int main() {
    // ---------- Showcase scenes (mesh scenes need the downloaded models, so not here) ----------
    for (const std::string name : {"cornell", "spheres", "triangles"}) {
        bool ok = true;
        try {
            load_scene(kScenes + "/" + name + ".json");
        } catch (const SceneError& e) {
            std::cout << "       " << e.what() << '\n';
            ok = false;
        }
        check(ok, "scenes/" + name + ".json loads");
    }

    const Scene cornell = load_scene(kScenes + "/cornell.json");
    check(cornell.world.objects().size() == 8, "cornell box has 8 objects");
    check(cornell.camera.image_width == 600 && cornell.camera.samples_per_pixel == 500 &&
              cornell.camera.max_depth == 50 && cornell.camera.vfov == 40.0,
          "cornell settings and camera are read");

    // ---------- Defaults and extras ----------
    const Scene minimal = parse_scene("{ " + kCamera + R"(, "objects": [] })");
    check(minimal.world.objects().empty(), "empty object list is allowed");
    check(minimal.camera.samples_per_pixel == CameraConfig{}.samples_per_pixel,
          "missing settings keep their defaults");

    const Scene boxed = parse_scene("{\n  // comments are allowed\n  " + kCamera + ", " + kMatte +
                                    R"(, "settings": { "aspect_ratio": [16, 9] },
        "objects": [ { "type": "box", "min": [0, 0, 0], "max": [1, 2, 3], "material": "matte" } ] })");
    check(boxed.world.objects().size() == 6, "box becomes 6 quads");
    check(boxed.camera.aspect_ratio == 16.0 / 9.0, "aspect ratio [16, 9] is exact");

    // ---------- Lights for next event estimation ----------
    check(cornell.lights.size() == 1, "cornell box registers its ceiling light");
    check(boxed.lights.empty(), "matte objects are not lights");
    const Scene glowing = parse_scene("{ " + kCamera + R"(,
        "materials": { "lamp": { "type": "light", "emit": [4, 4, 4] },
                       "dark": { "type": "light", "emit": [0, 0, 0] } },
        "objects": [
          { "type": "box", "min": [0, 0, 0], "max": [1, 1, 1], "material": "lamp" },
          { "type": "quad", "corner": [0, 0, 0], "u": [1, 0, 0], "v": [0, 1, 0], "material": "dark" },
          { "type": "sphere", "center": [0, 5, 0], "radius": 1, "material": "lamp" } ] })");
    check(glowing.lights.size() == 6,
          "every face of a glowing box is a light; black and non-quad emitters are not");

    // ---------- Errors: each must name the file and the field ----------
    check_error(R"({ "objects": [] })", "test.json: missing required field 'camera'",
                "missing camera");
    check_error(
        with_object(
            R"({ "type": "sphere", "center": [0, 0, -1], "radius": 0.5, "material": "glas" })"),
        "objects[0].material: unknown material 'glas' (defined: matte)",
        "unknown material lists the defined ones");
    check_error(with_object(R"({ "type": "sphere", "center": [0, 0, -1], "material": "matte" })"),
                "objects[0]: missing required field 'radius'", "missing object field");
    check_error(
        with_object(
            R"({ "type": "sphere", "center": [0, 0, -1], "raduis": 0.5, "material": "matte" })"),
        "objects[0]: unknown field 'raduis'", "typo in a field name");
    check_error(with_object(R"({ "type": "cylinder", "material": "matte" })"),
                "objects[0].type: unknown object type 'cylinder'", "unknown object type");
    check_error(
        with_object(
            R"({ "type": "sphere", "center": [0, 0], "radius": 0.5, "material": "matte" })"),
        "objects[0].center: expected an array of 3 numbers", "vector with 2 numbers");
    check_error(
        with_object(
            R"({ "type": "sphere", "center": [0, "a", 0], "radius": 0.5, "material": "matte" })"),
        "objects[0].center[1]: expected a number, got string", "non-number in a vector");
    check_error(
        with_object(
            R"({ "type": "sphere", "center": [0, 0, -1], "radius": -1, "material": "matte" })"),
        "objects[0].radius: must be greater than 0", "negative radius");
    check_error(with_object(R"({ "type": "mesh", "file": "missing.obj", "material": "matte" })"),
                "objects[0].file: could not load mesh", "missing mesh file");
    check_error(
        "{ " + kCamera + R"(, "materials": { "m": { "type": "plastic" } }, "objects": [] })",
        "materials.m.type: unknown material type 'plastic'", "unknown material type");
    check_error("{ " + kCamera + R"(, "settings": { "samples": 0 }, "objects": [] })",
                "settings.samples: expected a whole number >= 1", "zero samples");
    check_error("{ " + kCamera + R"(, "setings": {}, "objects": [] })", "unknown field 'setings'",
                "typo in a section name");
    check_error(R"({ "camera": { "look_from": [0, 0, 0], )",
                "test.json: invalid JSON: parse error at line 1", "broken JSON reports the line");

    bool threw = false;
    try {
        load_scene(kScenes + "/does_not_exist.json");
    } catch (const SceneError&) {
        threw = true;
    }
    check(threw, "missing scene file throws");

    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests FAILED\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
