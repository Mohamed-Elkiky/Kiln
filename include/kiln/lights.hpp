#pragma once

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "kiln/hittable.hpp"
#include "kiln/quad.hpp"

namespace kiln {

// The emissive quads in a scene, for next event estimation: at each diffuse bounce the renderer
// picks one of these and fires a shadow ray straight at it, instead of waiting for a random
// bounce to find it. Other emitters (a glowing sphere, say) still light the scene, just only
// when a ray happens to hit them.
class Lights {
public:
    void add(std::shared_ptr<Quad> light) { quads_.push_back(std::move(light)); }

    bool empty() const { return quads_.empty(); }
    std::size_t size() const { return quads_.size(); }
    const Quad& operator[](std::size_t i) const { return *quads_[i]; }

    // True if `object` is one of the sampled lights. Scenes have a handful of lights, so a
    // linear scan beats any lookup structure.
    bool contains(const Hittable* object) const {
        return std::any_of(quads_.begin(), quads_.end(),
                           [object](const auto& q) { return q.get() == object; });
    }

private:
    std::vector<std::shared_ptr<Quad>> quads_;
};

}  // namespace kiln
