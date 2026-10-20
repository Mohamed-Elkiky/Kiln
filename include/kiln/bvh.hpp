#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "kiln/aabb.hpp"
#include "kiln/hittable.hpp"
#include "kiln/triangle.hpp"

namespace kiln {

enum class BVHSplit {
    kMidpoint,  // split at the median centroid on the longest axis (fast build)
    kSAH,       // Surface Area Heuristic: pick the cheapest split (faster renders)
};

// Bounding Volume Hierarchy: a binary tree of boxes. A ray that misses a node's box skips
// everything inside it, so finding the closest hit is ~O(log n) instead of O(n).
//
// The whole tree lives in one flat array, in depth-first order: a node's left child is the
// very next node, so only the right child's index is stored. Traversal is a loop with a small
// stack, not recursion.
//
// Triangles are copied into the BVH by value, packed in leaf order, and tested with a direct
// (non-virtual) call: a leaf's triangles sit next to each other in memory instead of in
// separate heap allocations. Everything else (spheres, quads) is tested through Hittable.
class BVH : public Hittable {
public:
    // Leaves hold at most this many primitives.
    static constexpr std::size_t kMaxLeafSize = 4;

    // Builds the tree over objects. The vector is taken by value and reordered internally.
    explicit BVH(std::vector<std::shared_ptr<Hittable>> objects, BVHSplit split = BVHSplit::kSAH);

    bool hit(const Ray& r, Interval ray_t, HitRecord& rec) const override;
    AABB bounding_box() const override { return nodes_.empty() ? AABB() : nodes_.front().box; }

    std::size_t node_count() const { return nodes_.size(); }

private:
    using Objects = std::vector<std::shared_ptr<Hittable>>;

    // 64 bytes: one cache line.
    struct Node {
        AABB box;
        std::uint32_t index = 0;        // leaf: first triangle; interior: right child
        std::uint32_t other_index = 0;  // leaf: first non-triangle primitive
        std::uint16_t tri_count = 0;    // leaf only
        std::uint16_t other_count = 0;  // leaf only
        std::uint8_t axis = 0;          // interior: split axis, picks which child to visit first

        bool is_leaf() const { return tri_count + other_count > 0; }
    };

    std::uint32_t build(Objects& objects, std::size_t start, std::size_t end, BVHSplit split,
                        int depth);

    std::vector<Node> nodes_;
    std::vector<Triangle> triangles_;  // copies of the triangles, in leaf order
    Objects others_;                   // every other primitive, in leaf order
};

}  // namespace kiln
