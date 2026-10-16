#pragma once

#include <cmath>

#include "kiln/hittable.hpp"
#include "kiln/random.hpp"
#include "kiln/ray.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

// Decides what happens to light when it hits a surface.
class Material {
public:
    virtual ~Material() = default;

    // Returns true if the ray scatters. attenuation = how much of each colour survives.
    virtual bool scatter(const Ray& r_in, const HitRecord& rec, Color& attenuation,
                         Ray& scattered) const = 0;

    // Light given off by the surface. Black for everything except lights.
    virtual Color emitted() const { return Color{0.0, 0.0, 0.0}; }

    // Surface colour in [0, 1], ignoring lighting. The denoiser uses it as a guide image.
    virtual Color albedo() const = 0;

    // Perfect mirrors and glass: they show what's behind them, so the denoiser's guides follow
    // the ray through them instead of stopping here.
    virtual bool is_specular() const { return false; }
};

// Matte: scatters light in random directions around the normal.
class Lambertian : public Material {
public:
    explicit Lambertian(const Color& albedo) : albedo_(albedo) {}

    bool scatter(const Ray& /*r_in*/, const HitRecord& rec, Color& attenuation,
                 Ray& scattered) const override {
        Vec3 direction = rec.normal + random_unit_vector();

        // Random vector almost exactly opposite the normal: direction ~ 0, which breaks later
        // maths.
        if (direction.near_zero()) direction = rec.normal;

        scattered = Ray(rec.p, direction);
        attenuation = albedo_;
        return true;
    }

    Color albedo() const override { return albedo_; }

private:
    Color albedo_;
};

// Shiny: reflects like a mirror, fuzz blurs the reflection.
class Metal : public Material {
public:
    Metal(const Color& albedo, double fuzz) : albedo_(albedo), fuzz_(fuzz < 1.0 ? fuzz : 1.0) {}

    bool scatter(const Ray& r_in, const HitRecord& rec, Color& attenuation,
                 Ray& scattered) const override {
        Vec3 reflected = reflect(r_in.direction(), rec.normal);
        reflected = unit_vector(reflected) + fuzz_ * random_unit_vector();

        scattered = Ray(rec.p, reflected);
        attenuation = albedo_;

        // Fuzz can push the ray below the surface: absorb it instead.
        return dot(scattered.direction(), rec.normal) > 0.0;
    }

    Color albedo() const override { return albedo_; }
    bool is_specular() const override { return fuzz_ == 0.0; }

private:
    Color albedo_;
    double fuzz_;  // 0 = perfect mirror, 1 = very blurry
};

// Glass / water / diamond: refracts through the surface, reflects at grazing angles.
class Dielectric : public Material {
public:
    explicit Dielectric(double refraction_index) : refraction_index_(refraction_index) {}

    bool scatter(const Ray& r_in, const HitRecord& rec, Color& attenuation,
                 Ray& scattered) const override {
        attenuation = Color{1.0, 1.0, 1.0};  // glass absorbs nothing

        // Entering the glass: air (1.0) -> glass. Leaving: glass -> air.
        const double ri = rec.front_face ? (1.0 / refraction_index_) : refraction_index_;

        const Vec3 unit_direction = unit_vector(r_in.direction());
        const double cos_theta = std::fmin(dot(-unit_direction, rec.normal), 1.0);
        const double sin_theta = std::sqrt(1.0 - cos_theta * cos_theta);

        // Snell's law has no solution: total internal reflection.
        const bool cannot_refract = ri * sin_theta > 1.0;

        Vec3 direction;
        if (cannot_refract || reflectance(cos_theta, ri) > random_double()) {
            direction = reflect(unit_direction, rec.normal);
        } else {
            direction = refract(unit_direction, rec.normal, ri);
        }

        scattered = Ray(rec.p, direction);
        return true;
    }

    Color albedo() const override { return Color{1.0, 1.0, 1.0}; }
    bool is_specular() const override { return true; }

private:
    // Schlick's approximation: how much light reflects instead of refracting at this angle.
    static double reflectance(double cosine, double ri) {
        double r0 = (1.0 - ri) / (1.0 + ri);
        r0 = r0 * r0;
        return r0 + (1.0 - r0) * std::pow(1.0 - cosine, 5);
    }

    double refraction_index_;  // glass 1.5, water 1.33, diamond 2.4
};

// Light source: glows with a colour, never scatters. Values above 1 = brighter.
class DiffuseLight : public Material {
public:
    explicit DiffuseLight(const Color& emit) : emit_(emit) {}

    bool scatter(const Ray& /*r_in*/, const HitRecord& /*rec*/, Color& /*attenuation*/,
                 Ray& /*scattered*/) const override {
        return false;
    }

    Color emitted() const override { return emit_; }

    // Lights are brighter than 1; the guide only needs the colour, clamped to [0, 1].
    Color albedo() const override {
        return {std::fmin(emit_.x, 1.0), std::fmin(emit_.y, 1.0), std::fmin(emit_.z, 1.0)};
    }
private:
    Color emit_;
};

}  // namespace kiln
