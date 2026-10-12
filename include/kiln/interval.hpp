#pragma once

#include <limits>

namespace kiln {

inline constexpr double kInfinity = std::numeric_limits<double>::infinity();

// A range of t values [min, max]. Default is empty.
struct Interval {
    double min = +kInfinity;
    double max = -kInfinity;

    constexpr Interval() = default;
    constexpr Interval(double min_, double max_) : min(min_), max(max_) {}

    constexpr double size() const { return max - min; }

    // Inclusive: min <= x <= max
    constexpr bool contains(double x) const { return min <= x && x <= max; }

    // Exclusive: min < x < max
    constexpr bool surrounds(double x) const { return min < x && x < max; }
};

inline constexpr Interval kEmptyInterval{+kInfinity, -kInfinity};
inline constexpr Interval kUniverseInterval{-kInfinity, +kInfinity};

}  // namespace kiln