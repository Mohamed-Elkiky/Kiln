#include "kiln/grass.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kiln {

namespace {

constexpr double kPi = 3.1415926535897932385;

// splitmix64: tiny, fast and fully specified, so every compiler gives the same field.
// (std::uniform_real_distribution is allowed to differ between standard libraries.)
class FieldRng {
public:
    explicit FieldRng(std::uint32_t seed) : state_(0xD1B54A32D192ED03ull ^ seed) {}

    double next() {  // uniform in [0, 1)
        state_ += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) * 0x1.0p-53;
    }

    double range(double lo, double hi) { return lo + (hi - lo) * next(); }

private:
    std::uint64_t state_;
};

bool in_clearing(const GrassSettings& s, double x, double z) {
    for (const GrassClearing& c : s.clearings) {
        const double dx = x - c.x;
        const double dz = z - c.z;
        if (dx * dx + dz * dz < c.radius * c.radius) return true;
    }
    return false;
}

}  // namespace

GrassField make_grass(const GrassSettings& s,
                      const std::vector<std::shared_ptr<Material>>& materials) {
    if (materials.empty()) throw std::invalid_argument("make_grass: no materials");

    GrassField field;
    field.triangles.reserve(static_cast<std::size_t>(s.count) * kTrianglesPerBlade);
    field.roots.reserve(static_cast<std::size_t>(s.count));
    FieldRng rng(s.seed);

    // Heights along the blade of the two rows of vertices below the tip.
    constexpr double kLevels[] = {0.0, 0.4, 0.75};

    for (int blade = 0; blade < s.count; ++blade) {
        // Uniform random roots (no grid, so no visible pattern), skipping clearings.
        double x = 0.0, z = 0.0;
        int tries = 0;
        do {
            if (++tries > 1000) {
                throw std::invalid_argument("make_grass: the clearings cover the whole area");
            }
            x = rng.range(s.x_min, s.x_max);
            z = rng.range(s.z_min, s.z_max);
        } while (in_clearing(s, x, z));

        const double height = s.height * rng.range(0.7, 1.3);
        const double width = s.width * rng.range(0.8, 1.2);
        const double facing = rng.range(0.0, 2.0 * kPi);    // which way the flat side faces
        const double lean_dir = rng.range(0.0, 2.0 * kPi);  // which way the tip falls over
        const double lean = s.bend * rng.range(0.3, 1.0) * height;
        const auto& material = materials[std::min(
            materials.size() - 1,
            static_cast<std::size_t>(rng.next() * static_cast<double>(materials.size())))];

        const Point3 root{x, s.ground, z};
        const Vec3 across{std::cos(facing), 0.0, std::sin(facing)};
        const Vec3 over{std::cos(lean_dir), 0.0, std::sin(lean_dir)};

        // The spine curves over as it rises (lean grows with t^2), and the blade narrows to a tip.
        const auto spine = [&](double t) {
            return root + Vec3{0.0, height * t, 0.0} + (lean * t * t) * over;
        };
        Point3 left[3], right[3];
        for (int i = 0; i < 3; ++i) {
            const double half = 0.5 * width * (1.0 - kLevels[i]);
            left[i] = spine(kLevels[i]) - half * across;
            right[i] = spine(kLevels[i]) + half * across;
        }
        const Point3 tip = spine(1.0);

        auto add = [&](const Point3& a, const Point3& b, const Point3& c) {
            field.triangles.push_back(std::make_shared<Triangle>(a, b, c, material));
        };
        for (int i = 0; i < 2; ++i) {
            add(left[i], right[i], right[i + 1]);
            add(left[i], right[i + 1], left[i + 1]);
        }
        add(left[2], right[2], tip);
        field.roots.push_back(root);
    }
    return field;
}

}  // namespace kiln
