#include "kiln/scene_loader.hpp"

#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "kiln/material.hpp"
#include "kiln/obj_loader.hpp"
#include "kiln/quad.hpp"
#include "kiln/sphere.hpp"
#include "kiln/texture.hpp"
#include "kiln/triangle.hpp"
#include "nlohmann/json.hpp"

namespace kiln {

namespace {

using json = nlohmann::json;
using Materials = std::map<std::string, std::shared_ptr<Material>>;

// ---------- Error helpers ----------
// Every reader takes `where`, a breadcrumb like "objects[2].center", so each error points
// at the exact field. parse_scene() adds the file name in front.

[[noreturn]] void fail(const std::string& where, const std::string& message) {
    throw SceneError(where.empty() ? message : where + ": " + message);
}

std::string field(const std::string& where, const std::string& key) {
    return where.empty() ? key : where + "." + key;
}

void expect_object(const json& j, const std::string& where) {
    if (!j.is_object()) fail(where, std::string("expected an object, got ") + j.type_name());
}

// Unknown keys are errors, so a typo like "raduis" is caught instead of silently ignored.
void check_keys(const json& j, const std::string& where,
                std::initializer_list<const char*> allowed) {
    for (const auto& item : j.items()) {
        bool known = false;
        for (const char* key : allowed) known = known || item.key() == key;
        if (known) continue;

        std::string list;
        for (const char* key : allowed) list += (list.empty() ? "" : ", ") + std::string(key);
        fail(where, "unknown field '" + item.key() + "' (expected one of: " + list + ")");
    }
}

const json& require(const json& j, const char* key, const std::string& where) {
    const auto it = j.find(key);
    if (it == j.end()) fail(where, std::string("missing required field '") + key + "'");
    return *it;
}

// ---------- Value readers ----------

double to_number(const json& j, const std::string& where) {
    if (!j.is_number()) fail(where, std::string("expected a number, got ") + j.type_name());
    return j.get<double>();
}

double to_positive(const json& j, const std::string& where) {
    const double v = to_number(j, where);
    if (!(v > 0.0)) fail(where, "must be greater than 0");
    return v;
}

int to_count(const json& j, const std::string& where) {
    if (!j.is_number_integer() || j.get<long long>() < 1 || j.get<long long>() > INT_MAX) {
        fail(where, "expected a whole number >= 1");
    }
    return j.get<int>();
}

std::string to_string(const json& j, const std::string& where) {
    if (!j.is_string()) fail(where, std::string("expected a string, got ") + j.type_name());
    return j.get<std::string>();
}

Vec3 to_vec3(const json& j, const std::string& where) {
    if (!j.is_array() || j.size() != 3) {
        fail(where, "expected an array of 3 numbers, e.g. [1, 2, 3]");
    }
    return {to_number(j[0], where + "[0]"), to_number(j[1], where + "[1]"),
            to_number(j[2], where + "[2]")};
}

// A plain number (1.5) or a ratio ([16, 9]), which keeps 16:9 exact without 17 digits.
double to_aspect(const json& j, const std::string& where) {
    if (!j.is_array()) return to_positive(j, where);
    if (j.size() != 2) fail(where, "expected a number or [width, height], e.g. [16, 9]");
    return to_positive(j[0], where + "[0]") / to_positive(j[1], where + "[1]");
}

// "x", "y", "z" or any non-zero vector, e.g. [1, 0, 1].
Vec3 to_axis(const json& j, const std::string& where) {
    if (j.is_string()) {
        const std::string name = j.get<std::string>();
        if (name == "x") return {1, 0, 0};
        if (name == "y") return {0, 1, 0};
        if (name == "z") return {0, 0, 1};
        fail(where, "expected \"x\", \"y\", \"z\" or a direction like [1, 0, 1]");
    }
    const Vec3 axis = to_vec3(j, where);
    if (axis.near_zero()) fail(where, "direction must not be [0, 0, 0]");
    return axis;
}

// A list of colours, e.g. [[1, 0, 0], [0, 0, 1]].
std::vector<Color> to_colors(const json& j, const std::string& where, std::size_t min_count,
                             std::size_t max_count) {
    const std::string count =
        min_count == max_count ? std::to_string(min_count) : std::to_string(min_count) + " or more";
    if (!j.is_array() || j.size() < min_count || j.size() > max_count) {
        fail(where, "expected " + count + " colours, e.g. [[1, 0, 0], [0, 0, 1]]");
    }
    std::vector<Color> colors;
    for (std::size_t i = 0; i < j.size(); ++i) {
        colors.push_back(to_vec3(j[i], where + "[" + std::to_string(i) + "]"));
    }
    return colors;
}

std::uint32_t to_seed(const json& j, const std::string& where) {
    if (!j.is_number_integer() || j.get<long long>() < 0 || j.get<long long>() > UINT32_MAX) {
        fail(where, "expected a whole number >= 0");
    }
    return static_cast<std::uint32_t>(j.get<long long>());
}

// { "texture": "wood", ... } -> a procedural texture. See the README for every field.
std::shared_ptr<Texture> read_texture(const json& j, const std::string& where) {
    const std::string type = to_string(require(j, "texture", where), field(where, "texture"));
    const auto colors = [&](std::size_t min_count, std::size_t max_count) {
        return to_colors(require(j, "colors", where), field(where, "colors"), min_count, max_count);
    };
    const auto number = [&](const char* key, double fallback) {
        return j.contains(key) ? to_positive(j.at(key), field(where, key)) : fallback;
    };
    const auto seed = [&] {
        return j.contains("seed") ? to_seed(j.at("seed"), field(where, "seed")) : 0u;
    };

    if (type == "checker") {
        check_keys(j, where, {"texture", "colors", "scale"});
        const auto c = colors(2, 2);
        return std::make_shared<CheckerTexture>(c[0], c[1], number("scale", 1.0));
    }
    if (type == "noise") {
        check_keys(j, where, {"texture", "colors", "scale", "seed"});
        const auto c = colors(2, 2);
        return std::make_shared<NoiseTexture>(c[0], c[1], number("scale", 1.0), seed());
    }
    if (type == "wood") {
        check_keys(j, where,
                   {"texture", "colors", "axis", "center", "ring_scale", "noise", "seed"});
        const auto c = colors(2, 2);
        const Vec3 axis =
            j.contains("axis") ? to_axis(j.at("axis"), field(where, "axis")) : Vec3{1, 0, 0};
        const Point3 center = j.contains("center") ? to_vec3(j.at("center"), field(where, "center"))
                                                   : Point3{0, 0, 0};
        double noise = 1.0;
        if (j.contains("noise")) {
            noise = to_number(j.at("noise"), field(where, "noise"));
            if (noise < 0.0) fail(field(where, "noise"), "must be 0 or more");
        }
        return std::make_shared<WoodTexture>(c[0], c[1], axis, center, number("ring_scale", 10.0),
                                             noise, seed());
    }
    if (type == "normal") {
        check_keys(j, where, {"texture"});
        return std::make_shared<NormalTexture>();
    }
    if (type == "gradient") {
        check_keys(j, where, {"texture", "colors", "axis", "from", "to"});
        auto c = colors(2, 64);
        const Vec3 axis =
            j.contains("axis") ? to_axis(j.at("axis"), field(where, "axis")) : Vec3{0, 1, 0};
        const double from = to_number(require(j, "from", where), field(where, "from"));
        const double to = to_number(require(j, "to", where), field(where, "to"));
        if (from == to) fail(field(where, "to"), "must be different from 'from'");
        return std::make_shared<GradientTexture>(std::move(c), axis, from, to);
    }
    fail(field(where, "texture"),
         "unknown texture '" + type + "' (expected checker, noise, wood, normal or gradient)");
}

// An "albedo" is either a flat colour [r, g, b] or a texture object.
struct Albedo {
    Color color{0.0, 0.0, 0.0};
    std::shared_ptr<Texture> texture;  // null = flat colour
};

Albedo read_albedo(const json& j, const std::string& where) {
    Albedo albedo;
    if (j.is_object()) {
        albedo.texture = read_texture(j, where);
    } else {
        albedo.color = to_vec3(j, where);
    }
    return albedo;
}

// ---------- Sections ----------

void read_camera(const json& j, const std::string& where, CameraConfig& config) {
    expect_object(j, where);
    check_keys(j, where, {"look_from", "look_at", "up", "fov"});

    config.look_from = to_vec3(require(j, "look_from", where), field(where, "look_from"));
    config.look_at = to_vec3(require(j, "look_at", where), field(where, "look_at"));
    if ((config.look_from - config.look_at).near_zero()) {
        fail(field(where, "look_at"), "must be a different point from look_from");
    }
    if (j.contains("up")) config.vup = to_vec3(j.at("up"), field(where, "up"));
    if (j.contains("fov")) {
        const double fov = to_number(j.at("fov"), field(where, "fov"));
        if (!(fov > 0.0 && fov < 180.0)) {
            fail(field(where, "fov"), "must be between 0 and 180 degrees");
        }
        config.vfov = fov;
    }
}

void read_settings(const json& j, const std::string& where, CameraConfig& config,
                   Color& background) {
    expect_object(j, where);
    check_keys(j, where, {"width", "aspect_ratio", "samples", "depth", "background"});

    if (j.contains("width")) config.image_width = to_count(j.at("width"), field(where, "width"));
    if (j.contains("aspect_ratio")) {
        config.aspect_ratio = to_aspect(j.at("aspect_ratio"), field(where, "aspect_ratio"));
    }
    if (j.contains("samples")) {
        config.samples_per_pixel = to_count(j.at("samples"), field(where, "samples"));
    }
    if (j.contains("depth")) config.max_depth = to_count(j.at("depth"), field(where, "depth"));
    if (j.contains("background")) {
        background = to_vec3(j.at("background"), field(where, "background"));
    }
}

Materials read_materials(const json& j, const std::string& where) {
    expect_object(j, where);
    Materials materials;

    for (const auto& item : j.items()) {
        const std::string w = field(where, item.key());
        const json& m = item.value();
        expect_object(m, w);
        const std::string type = to_string(require(m, "type", w), field(w, "type"));

        if (type == "lambertian") {
            check_keys(m, w, {"type", "albedo"});
            const Albedo a = read_albedo(require(m, "albedo", w), field(w, "albedo"));
            materials[item.key()] = a.texture ? std::make_shared<Lambertian>(a.texture)
                                              : std::make_shared<Lambertian>(a.color);
        } else if (type == "metal") {
            check_keys(m, w, {"type", "albedo", "fuzz"});
            double fuzz = 0.0;
            if (m.contains("fuzz")) {
                fuzz = to_number(m.at("fuzz"), field(w, "fuzz"));
                if (fuzz < 0.0 || fuzz > 1.0) fail(field(w, "fuzz"), "must be between 0 and 1");
            }
            const Albedo a = read_albedo(require(m, "albedo", w), field(w, "albedo"));
            materials[item.key()] = a.texture ? std::make_shared<Metal>(a.texture, fuzz)
                                              : std::make_shared<Metal>(a.color, fuzz);
        } else if (type == "dielectric") {
            check_keys(m, w, {"type", "ior"});
            materials[item.key()] =
                std::make_shared<Dielectric>(to_positive(require(m, "ior", w), field(w, "ior")));
        } else if (type == "light") {
            check_keys(m, w, {"type", "emit"});
            materials[item.key()] =
                std::make_shared<DiffuseLight>(to_vec3(require(m, "emit", w), field(w, "emit")));
        } else {
            fail(field(w, "type"), "unknown material type '" + type +
                                       "' (expected lambertian, metal, dielectric or light)");
        }
    }
    return materials;
}

std::shared_ptr<Material> find_material(const Materials& materials, const json& obj,
                                        const std::string& where) {
    const std::string w = field(where, "material");
    const std::string name = to_string(require(obj, "material", where), w);

    const auto it = materials.find(name);
    if (it != materials.end()) return it->second;

    std::string defined;
    for (const auto& entry : materials) defined += (defined.empty() ? "" : ", ") + entry.first;
    fail(w, "unknown material '" + name + "'" +
                (defined.empty() ? " (no materials are defined)" : " (defined: " + defined + ")"));
}

// Adds a quad to the world. Glowing quads are also registered as lights, so the renderer can
// sample them directly.
void add_quad(Scene& scene, const Point3& corner, const Vec3& u, const Vec3& v,
              const std::shared_ptr<Material>& mat) {
    auto quad = std::make_shared<Quad>(corner, u, v, mat);
    const Color e = mat->emitted();
    if (e.x > 0.0 || e.y > 0.0 || e.z > 0.0) scene.lights.add(quad);
    scene.world.add(std::move(quad));
}

// Axis-aligned box as 6 quads, added straight to the world so the BVH sees each face.
void add_box(Scene& scene, const Point3& a, const Point3& b, const std::shared_ptr<Material>& mat) {
    const Point3 lo{std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z)};
    const Point3 hi{std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z)};
    const Vec3 dx{hi.x - lo.x, 0, 0};
    const Vec3 dy{0, hi.y - lo.y, 0};
    const Vec3 dz{0, 0, hi.z - lo.z};

    add_quad(scene, Point3{lo.x, lo.y, hi.z}, dx, dy, mat);   // front
    add_quad(scene, Point3{hi.x, lo.y, hi.z}, -dz, dy, mat);  // right
    add_quad(scene, Point3{hi.x, lo.y, lo.z}, -dx, dy, mat);  // back
    add_quad(scene, Point3{lo.x, lo.y, lo.z}, dz, dy, mat);   // left
    add_quad(scene, Point3{lo.x, hi.y, hi.z}, dx, -dz, mat);  // top
    add_quad(scene, Point3{lo.x, lo.y, lo.z}, dx, dz, mat);   // bottom
}

Transform read_transform(const json& j, const std::string& where) {
    expect_object(j, where);
    check_keys(j, where, {"scale", "rotate_y", "translate"});

    Transform t;
    if (j.contains("scale")) {
        t.scale = to_number(j.at("scale"), field(where, "scale"));
        if (t.scale == 0.0) fail(field(where, "scale"), "must not be 0");
    }
    if (j.contains("rotate_y"))
        t.rotate_y_degrees = to_number(j.at("rotate_y"), field(where, "rotate_y"));
    if (j.contains("translate"))
        t.translate = to_vec3(j.at("translate"), field(where, "translate"));
    return t;
}

void add_mesh(HittableList& world, const json& obj, const std::string& where,
              const Materials& materials, const std::string& base_dir) {
    check_keys(obj, where, {"type", "file", "material", "transform", "generate_normals"});

    const std::string file = to_string(require(obj, "file", where), field(where, "file"));
    const auto material = find_material(materials, obj, where);
    const Transform transform = obj.contains("transform")
                                    ? read_transform(obj.at("transform"), field(where, "transform"))
                                    : Transform{};

    ObjLoadOptions options;
    options.generate_normals_if_missing = true;  // bunny/teapot/dragon ship without normals
    if (obj.contains("generate_normals")) {
        const json& g = obj.at("generate_normals");
        if (!g.is_boolean()) fail(field(where, "generate_normals"), "expected true or false");
        options.generate_normals_if_missing = g.get<bool>();
    }

    const std::string path =
        (std::filesystem::path(base_dir) / file).lexically_normal().generic_string();

    const auto start = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<Triangle>> triangles;
    try {
        triangles = load_obj(path, material, transform, options);
    } catch (const std::runtime_error& e) {
        fail(field(where, "file"), std::string("could not load mesh (") + e.what() + ")");
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();

    for (const auto& tri : triangles) world.add(tri);
    std::clog << "Loaded " << path << ": " << triangles.size() << " triangles in " << ms << " ms\n";
}

void read_objects(const json& j, const std::string& where, const Materials& materials,
                  const std::string& base_dir, Scene& scene) {
    HittableList& world = scene.world;
    if (!j.is_array()) fail(where, std::string("expected an array, got ") + j.type_name());

    for (std::size_t i = 0; i < j.size(); ++i) {
        const std::string w = where + "[" + std::to_string(i) + "]";
        const json& obj = j[i];
        expect_object(obj, w);
        const std::string type = to_string(require(obj, "type", w), field(w, "type"));

        if (type == "sphere") {
            check_keys(obj, w, {"type", "center", "radius", "material"});
            const Point3 center = to_vec3(require(obj, "center", w), field(w, "center"));
            const double radius = to_positive(require(obj, "radius", w), field(w, "radius"));
            world.add(std::make_shared<Sphere>(center, radius, find_material(materials, obj, w)));
        } else if (type == "quad") {
            check_keys(obj, w, {"type", "corner", "u", "v", "material"});
            const Point3 corner = to_vec3(require(obj, "corner", w), field(w, "corner"));
            const Vec3 u = to_vec3(require(obj, "u", w), field(w, "u"));
            const Vec3 v = to_vec3(require(obj, "v", w), field(w, "v"));
            if (cross(u, v).near_zero()) fail(w, "u and v must be non-zero and not parallel");
            add_quad(scene, corner, u, v, find_material(materials, obj, w));
        } else if (type == "triangle") {
            check_keys(obj, w, {"type", "v0", "v1", "v2", "material"});
            const Point3 v0 = to_vec3(require(obj, "v0", w), field(w, "v0"));
            const Point3 v1 = to_vec3(require(obj, "v1", w), field(w, "v1"));
            const Point3 v2 = to_vec3(require(obj, "v2", w), field(w, "v2"));
            if (cross(v1 - v0, v2 - v0).near_zero()) fail(w, "triangle has zero area");
            world.add(std::make_shared<Triangle>(v0, v1, v2, find_material(materials, obj, w)));
        } else if (type == "box") {
            check_keys(obj, w, {"type", "min", "max", "material"});
            const Point3 a = to_vec3(require(obj, "min", w), field(w, "min"));
            const Point3 b = to_vec3(require(obj, "max", w), field(w, "max"));
            if (a.x == b.x || a.y == b.y || a.z == b.z) {
                fail(w, "box has zero size on at least one axis (use a quad for a flat panel)");
            }
            add_box(scene, a, b, find_material(materials, obj, w));
        } else if (type == "mesh") {
            add_mesh(world, obj, w, materials, base_dir);
        } else {
            fail(field(w, "type"), "unknown object type '" + type +
                                       "' (expected sphere, quad, triangle, box or mesh)");
        }
    }
}

}  // namespace

Scene parse_scene(const std::string& text, const std::string& base_dir, const std::string& source) {
    json root;
    try {
        // Comments (// and /* */) are allowed, so scene files can be annotated.
        root = json::parse(text, nullptr, true, true);
    } catch (const json::parse_error& e) {
        // Drop nlohmann's "[json.exception.parse_error.101] " prefix, keep line and column.
        std::string message = e.what();
        const auto bracket = message.find("] ");
        if (bracket != std::string::npos) message = message.substr(bracket + 2);
        throw SceneError(source + ": invalid JSON: " + message);
    }

    Scene scene;
    try {
        expect_object(root, "");
        check_keys(root, "", {"camera", "settings", "materials", "objects"});

        read_camera(require(root, "camera", ""), "camera", scene.camera);
        if (root.contains("settings")) {
            read_settings(root.at("settings"), "settings", scene.camera, scene.background);
        }
        const Materials materials = root.contains("materials")
                                        ? read_materials(root.at("materials"), "materials")
                                        : Materials{};
        read_objects(require(root, "objects", ""), "objects", materials, base_dir, scene);
    } catch (const SceneError& e) {
        throw SceneError(source + ": " + e.what());
    }
    return scene;
}

Scene load_scene(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw SceneError(path + ": cannot open file");
    std::ostringstream text;
    text << file.rdbuf();

    std::string base_dir = std::filesystem::path(path).parent_path().string();
    if (base_dir.empty()) base_dir = ".";
    return parse_scene(text.str(), base_dir, path);
}

}  // namespace kiln
