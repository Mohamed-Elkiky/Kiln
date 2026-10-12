#pragma once

#include <cstdint>
#include <random>

#include "kiln/vec3.hpp"

namespace kiln {

// One generator per thread: no locking, no data races when we go multithreaded.
inline std::mt19937& rng() {
    thread_local std::mt19937 generator{std::random_device{}()};
    return generator;
}

// Reseed this thread's generator. A fixed seed makes renders repeatable, so two builds of the
// same scene can be diffed pixel for pixel.
inline void seed_rng(std::uint32_t seed) { rng().seed(seed); }

// Uniform in [0, 1)
inline double random_double() {
    thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng());
}

// Uniform in [min, max)
inline double random_double(double min, double max) { return min + (max - min) * random_double(); }

inline Vec3 random_vec3() { return {random_double(), random_double(), random_double()}; }

inline Vec3 random_vec3(double min, double max) {
    return {random_double(min, max), random_double(min, max), random_double(min, max)};
}

// Uniformly distributed direction on the unit sphere (rejection sampling).
inline Vec3 random_unit_vector() {
    while (true) {
        const Vec3 p = random_vec3(-1.0, 1.0);
        const double len_sq = p.length_squared();
        if (1e-160 < len_sq && len_sq <= 1.0) return p / std::sqrt(len_sq);
    }
}

}  // namespace kiln