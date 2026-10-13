#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "kiln/aabb.hpp"
#include "kiln/hittable.hpp"

namespace kiln {

enum class BVHSplit {
    kMidpoint,  // split at the median centroid on the longest axis (fast build)
    kSAH,       // Surface Area Heuristic: pick the cheapest split (faster renders)
};

// Bounding Volume Hierarchy: a binary tree of boxes. A ray that misses a node's box skips
// everything inside it, so finding the closest hit is ~O(log n) instead of O(n).
class BVHNode : public Hittable {
public:
    // Leaves hold at most this many primitives.
    static constexpr std::size_t kMaxLeafSize = 4;

    // Builds the tree over objects. The vector is taken by value and reordered internally.
    explicit BVHNode(std::vector<std::shared_ptr<Hittable>> objects,
                     BVHSplit split = BVHSplit::kSAH);

    bool hit(const Ray& r, Interval ray_t, HitRecord& rec) const override;
    AABB bounding_box() const override { return bbox_; }

private:
    using Objects = std::vector<std::shared_ptr<Hittable>>;

    BVHNode() = default;
    void build(Objects& objects, std::size_t start, std::size_t end, BVHSplit split);

    AABB bbox_;
    std::shared_ptr<BVHNode> left_, right_;  // interior node
    Objects leaf_;                           // leaf node: 1 to kMaxLeafSize primitives
    int axis_ = 0;                           // split axis, picks which child to visit first
};

}  // namespace kiln
