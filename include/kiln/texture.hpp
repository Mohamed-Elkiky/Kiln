#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

#include "kiln/hittable.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

// A colour that varies over a surface. Every texture here is a solid (3D) texture: it is looked
// up at the hit point in world space, so it needs no UV coordinates and works the same on
// spheres, boxes and meshes. Textures never touch the render's random numbers, so a textured
// scene is exactly as deterministic as a flat one.
class Texture {
public:
    virtual ~Texture() = default;
    virtual Color value(const HitRecord& rec) const = 0;
};

namespace texture_detail {

inline Color lerp(const Color& a, const Color& b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return (1.0 - t) * a + t * b;
}

// Two unit vectors at right angles to `axis`, so a point can be split into "along the axis"
// and "across it".
inline std::pair<Vec3, Vec3> basis(const Vec3& axis) {
    const Vec3 helper = std::fabs(axis.x) > 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 a = unit_vector(cross(axis, helper));
    return {a, cross(axis, a)};
}

}  // namespace texture_detail

// 3D checkerboard: cubes of size `scale` alternating between two colours. Handy for testing.
class CheckerTexture final : public Texture {
public:
    CheckerTexture(const Color& even, const Color& odd, double scale)
        : even_(even), odd_(odd), inv_scale_(1.0 / scale) {}

    Color value(const HitRecord& rec) const override {
        const auto cell = [&](double v) {
            return static_cast<long long>(std::floor(v * inv_scale_));
        };
        const long long sum = cell(rec.p.x) + cell(rec.p.y) + cell(rec.p.z);
        return (sum % 2 == 0) ? even_ : odd_;
    }

private:
    Color even_, odd_;
    double inv_scale_;
};

// Ken Perlin's improved noise: smooth pseudo-random values in about [-1, 1], the same at the
// same point every time. The permutation table comes from its own seed, never the render's RNG.
class Perlin {
public:
    explicit Perlin(std::uint32_t seed = 0) {
        for (int i = 0; i < 256; ++i) perm_[i] = i;
        std::uint64_t state = 0x9E3779B97F4A7C15ull ^ seed;
        for (int i = 255; i > 0; --i) {  // Fisher-Yates with splitmix64: same table everywhere
            state += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z ^= z >> 31;
            std::swap(perm_[i], perm_[static_cast<int>(z % static_cast<std::uint64_t>(i + 1))]);
        }
        for (int i = 0; i < 256; ++i) perm_[256 + i] = perm_[i];
    }

    double noise(const Point3& p) const {
        const double fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
        const int xi = static_cast<int>(static_cast<long long>(fx) & 255);
        const int yi = static_cast<int>(static_cast<long long>(fy) & 255);
        const int zi = static_cast<int>(static_cast<long long>(fz) & 255);
        const double x = p.x - fx, y = p.y - fy, z = p.z - fz;
        const double u = fade(x), v = fade(y), w = fade(z);

        const int a = perm_[xi] + yi, aa = perm_[a] + zi, ab = perm_[a + 1] + zi;
        const int b = perm_[xi + 1] + yi, ba = perm_[b] + zi, bb = perm_[b + 1] + zi;

        return mix(
            w,
            mix(v, mix(u, grad(perm_[aa], x, y, z), grad(perm_[ba], x - 1, y, z)),
                mix(u, grad(perm_[ab], x, y - 1, z), grad(perm_[bb], x - 1, y - 1, z))),
            mix(v, mix(u, grad(perm_[aa + 1], x, y, z - 1), grad(perm_[ba + 1], x - 1, y, z - 1)),
                mix(u, grad(perm_[ab + 1], x, y - 1, z - 1),
                    grad(perm_[bb + 1], x - 1, y - 1, z - 1))));
    }

    // Sum of |noise| at doubling frequencies: the classic turbulence, roughly in [0, 1].
    double turbulence(Point3 p, int octaves = 7) const {
        double sum = 0.0, weight = 1.0;
        for (int i = 0; i < octaves; ++i) {
            sum += weight * std::fabs(noise(p));
            weight *= 0.5;
            p = 2.0 * p;
        }
        return sum;
    }

private:
    static double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static double mix(double t, double a, double b) { return a + t * (b - a); }
    static double grad(int hash, double x, double y, double z) {
        const int h = hash & 15;
        const double u = h < 8 ? x : y;
        const double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }

    std::array<int, 512> perm_{};
};

// Perlin turbulence blended between two colours: stone, clouds, smoke.
class NoiseTexture final : public Texture {
public:
    NoiseTexture(const Color& low, const Color& high, double scale, std::uint32_t seed)
        : low_(low), high_(high), scale_(scale), perlin_(seed) {}

    Color value(const HitRecord& rec) const override {
        return texture_detail::lerp(low_, high_, perlin_.turbulence(scale_ * rec.p));
    }

private:
    Color low_, high_;
    double scale_;
    Perlin perlin_;
};

// Wood: growth rings around a line through `center` along `axis` (the grain direction), wobbled
// by noise. Noise is stretched along the axis, so streaks run with the grain, not across it.
class WoodTexture final : public Texture {
public:
    WoodTexture(const Color& light, const Color& dark, const Vec3& axis, const Point3& center,
                double ring_scale, double noise, std::uint32_t seed)
        : light_(light),
          dark_(dark),
          axis_(unit_vector(axis)),
          center_(center),
          ring_scale_(ring_scale),
          noise_(noise),
          perlin_(seed) {
        std::tie(across_a_, across_b_) = texture_detail::basis(axis_);
    }

    Color value(const HitRecord& rec) const override {
        const Vec3 d = rec.p - center_;
        const double along = dot(d, axis_);
        const double a = dot(d, across_a_);
        const double b = dot(d, across_b_);

        // Grain-aligned noise: 4x more variation across the grain than along it.
        const Point3 q{a * ring_scale_ * 2.0, b * ring_scale_ * 2.0, along * ring_scale_ * 0.5};
        const double radius = std::sqrt(a * a + b * b);
        const double rings = radius * ring_scale_ + noise_ * perlin_.turbulence(q, 5);

        // Sharp dark latewood bands on a light background, plus fine streaks.
        const double ring = std::pow(0.5 + 0.5 * std::sin(2.0 * 3.14159265358979 * rings), 3.0);
        const double streak = 0.15 * perlin_.noise(Point3{q.x * 8.0, q.y * 8.0, q.z * 0.5});
        return texture_detail::lerp(light_, dark_, ring + streak);
    }

private:
    Color light_, dark_;
    Vec3 axis_, across_a_, across_b_;
    Point3 center_;
    double ring_scale_;
    double noise_;
    Perlin perlin_;
};

// Colour from the direction the surface faces: x, y, z mapped from [-1, 1] to [0, 1].
class NormalTexture final : public Texture {
public:
    Color value(const HitRecord& rec) const override {
        const Vec3 n = rec.front_face ? rec.normal : -rec.normal;  // outward, whatever side was hit
        return 0.5 * (n + Vec3{1.0, 1.0, 1.0});
    }
};

// Blend through two or more evenly spaced colours along an axis, from height `from` to `to`.
class GradientTexture final : public Texture {
public:
    GradientTexture(std::vector<Color> colors, const Vec3& axis, double from, double to)
        : colors_(std::move(colors)), axis_(unit_vector(axis)), from_(from), to_(to) {}

    Color value(const HitRecord& rec) const override {
        const double t = std::clamp((dot(rec.p, axis_) - from_) / (to_ - from_), 0.0, 1.0);
        const double scaled = t * static_cast<double>(colors_.size() - 1);
        const std::size_t i = std::min(static_cast<std::size_t>(scaled), colors_.size() - 2);
        return texture_detail::lerp(colors_[i], colors_[i + 1], scaled - static_cast<double>(i));
    }

private:
    std::vector<Color> colors_;
    Vec3 axis_;
    double from_, to_;
};

}  // namespace kiln