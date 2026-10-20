#include "kiln/bvh.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace kiln {

namespace {

constexpr int kSAHBins = 16;

// SAH can in theory make a lopsided tree (one primitive peeled off per level). Below this depth
// the builder switches to median splits, which halve the count every level, so the tree is never
// deeper than kMaxSAHDepth + log2(n) and the traversal stack can be a fixed-size array.
constexpr int kMaxSAHDepth = 48;
constexpr int kStackSize = 96;

Point3 centroid(const AABB& box) {
    return {0.5 * (box.x.min + box.x.max), 0.5 * (box.y.min + box.y.max),
            0.5 * (box.z.min + box.z.max)};
}

// Slab test with 1 / direction worked out once per ray instead of once per box.
// Same comparisons as AABB::hit, so a box is hit here exactly when it is hit there.
bool hit_box(const AABB& box, const Point3& origin, const Vec3& inv_dir, Interval ray_t) {
    for (int a = 0; a < 3; ++a) {
        const Interval& slab = box.axis(a);
        double t0 = (slab.min - origin[a]) * inv_dir[a];
        double t1 = (slab.max - origin[a]) * inv_dir[a];
        if (t0 > t1) std::swap(t0, t1);

        if (t0 > ray_t.min) ray_t.min = t0;
        if (t1 < ray_t.max) ray_t.max = t1;
        if (ray_t.max <= ray_t.min) return false;
    }
    return true;
}

}  // namespace

BVH::BVH(Objects objects, BVHSplit split) {
    if (objects.empty()) return;
    // A binary tree with leaves of 1+ primitives has fewer than 2n nodes.
    nodes_.reserve(2 * objects.size());
    build(objects, 0, objects.size(), split, 0);
}

std::uint32_t BVH::build(Objects& objects, std::size_t start, std::size_t end, BVHSplit split,
                         int depth) {
    const auto index = static_cast<std::uint32_t>(nodes_.size());
    nodes_.emplace_back();  // filled in below; children are appended after it

    AABB box;
    for (std::size_t i = start; i < end; ++i) box = AABB(box, objects[i]->bounding_box());
    nodes_[index].box = box;

    const std::size_t count = end - start;
    if (count <= kMaxLeafSize) {
        Node& leaf = nodes_[index];
        leaf.index = static_cast<std::uint32_t>(triangles_.size());
        leaf.other_index = static_cast<std::uint32_t>(others_.size());
        for (std::size_t i = start; i < end; ++i) {
            if (const auto* tri = dynamic_cast<const Triangle*>(objects[i].get())) {
                triangles_.push_back(*tri);
            } else {
                others_.push_back(objects[i]);
            }
        }
        leaf.tri_count = static_cast<std::uint16_t>(triangles_.size() - leaf.index);
        leaf.other_count = static_cast<std::uint16_t>(others_.size() - leaf.other_index);
        return index;
    }

    // Choose splits by where primitives sit (centroids), not how far they reach (boxes).
    // A big floor sphere would otherwise make every axis look equally long.
    Point3 cmin{kInfinity, kInfinity, kInfinity};
    Point3 cmax{-kInfinity, -kInfinity, -kInfinity};
    for (std::size_t i = start; i < end; ++i) {
        const Point3 c = centroid(objects[i]->bounding_box());
        for (int a = 0; a < 3; ++a) {
            cmin[a] = std::min(cmin[a], c[a]);
            cmax[a] = std::max(cmax[a], c[a]);
        }
    }

    auto first = objects.begin() + static_cast<std::ptrdiff_t>(start);
    auto last = objects.begin() + static_cast<std::ptrdiff_t>(end);
    auto mid = first + static_cast<std::ptrdiff_t>(count / 2);
    int axis = 0;

    // Median split on the longest centroid axis. nth_element only partially sorts: O(n)
    // per level instead of O(n log n), with the same result as a full sort for a halving split.
    auto median_split = [&](int a) {
        axis = a;
        std::nth_element(first, mid, last, [a](const auto& l, const auto& r) {
            return centroid(l->bounding_box())[a] < centroid(r->bounding_box())[a];
        });
    };

    int longest = 0;
    for (int a = 1; a < 3; ++a) {
        if (cmax[a] - cmin[a] > cmax[longest] - cmin[longest]) longest = a;
    }

    if (split == BVHSplit::kMidpoint || depth >= kMaxSAHDepth) {
        median_split(longest);
    } else {
        // Binned SAH. Expected cost of a split ~ area(L) * count(L) + area(R) * count(R):
        // a ray reaches a child with probability proportional to its surface area, then pays
        // for every primitive inside. Try kSAHBins - 1 split planes per axis, keep the cheapest.
        double best_cost = std::numeric_limits<double>::infinity();
        int best_axis = -1;
        int best_bin = 0;

        for (int a = 0; a < 3; ++a) {
            const double extent = cmax[a] - cmin[a];
            if (extent <= 0.0) continue;  // all centroids level on this axis: can't split here

            const double scale = kSAHBins / extent;
            auto bin_of = [&](const Point3& c) {
                return std::min(kSAHBins - 1, static_cast<int>((c[a] - cmin[a]) * scale));
            };

            std::array<AABB, kSAHBins> bin_box{};
            std::array<std::size_t, kSAHBins> bin_count{};
            for (auto it = first; it != last; ++it) {
                const AABB b = (*it)->bounding_box();
                const int bin = bin_of(centroid(b));
                bin_box[bin] = AABB(bin_box[bin], b);
                ++bin_count[bin];
            }

            // Sweep from the right so each candidate plane knows its right-hand area and count.
            std::array<double, kSAHBins> right_area{};
            std::array<std::size_t, kSAHBins> right_count{};
            AABB acc;
            std::size_t n = 0;
            for (int b = kSAHBins - 1; b > 0; --b) {
                acc = AABB(acc, bin_box[b]);
                n += bin_count[b];
                right_area[b] = acc.surface_area();
                right_count[b] = n;
            }

            // Sweep from the left: plane after bin b puts bins [0, b] left, [b + 1, end) right.
            acc = AABB();
            n = 0;
            for (int b = 0; b < kSAHBins - 1; ++b) {
                acc = AABB(acc, bin_box[b]);
                n += bin_count[b];
                const std::size_t rn = right_count[b + 1];
                if (n == 0 || rn == 0) continue;  // one side empty: not a real split

                const double cost = acc.surface_area() * static_cast<double>(n) +
                                    right_area[b + 1] * static_cast<double>(rn);
                if (cost < best_cost) {
                    best_cost = cost;
                    best_axis = a;
                    best_bin = b;
                }
            }
        }

        if (best_axis < 0) {
            median_split(longest);  // every centroid in one spot: any halving is as good as SAH
        } else {
            axis = best_axis;
            const double scale = kSAHBins / (cmax[axis] - cmin[axis]);
            const double lo = cmin[axis];
            mid = std::partition(first, last, [&](const auto& obj) {
                const Point3 c = centroid(obj->bounding_box());
                return std::min(kSAHBins - 1, static_cast<int>((c[axis] - lo) * scale)) <= best_bin;
            });
        }
    }

    const auto split_index = static_cast<std::size_t>(mid - objects.begin());
    build(objects, start, split_index, split, depth + 1);  // left child lands at index + 1
    const std::uint32_t right = build(objects, split_index, end, split, depth + 1);
    nodes_[index].index = right;  // nodes_ may have reallocated: index, never a reference
    nodes_[index].axis = static_cast<std::uint8_t>(axis);
    return index;
}

bool BVH::hit(const Ray& r, Interval ray_t, HitRecord& rec) const {
    if (nodes_.empty()) return false;

    const Point3& origin = r.origin();
    const Vec3& dir = r.direction();
    // +-inf for axis-parallel rays, which the slab test handles.
    const Vec3 inv_dir{1.0 / dir.x, 1.0 / dir.y, 1.0 / dir.z};

    std::array<std::uint32_t, kStackSize> stack;
    int top = 0;
    std::uint32_t current = 0;
    bool hit_anything = false;

    while (true) {
        const Node& node = nodes_[current];
        // Miss the box, miss everything inside it. ray_t.max is the closest hit so far, so
        // boxes behind it are rejected immediately.
        if (hit_box(node.box, origin, inv_dir, ray_t)) {
            if (node.is_leaf()) {
                // Leaf: test each primitive, keeping only hits closer than the best so far.
                for (std::uint32_t i = node.index; i < node.index + node.tri_count; ++i) {
                    if (triangles_[i].hit(r, ray_t, rec)) {
                        hit_anything = true;
                        ray_t.max = rec.t;
                    }
                }
                for (std::uint32_t i = node.other_index; i < node.other_index + node.other_count;
                     ++i) {
                    if (others_[i]->hit(r, ray_t, rec)) {
                        hit_anything = true;
                        ray_t.max = rec.t;
                    }
                }
            } else {
                // Visit the child on the ray's near side first and save the far one for later.
                // A near hit shrinks ray_t.max, so the far box usually fails straight away.
                const std::uint32_t left = current + 1;
                const std::uint32_t right = node.index;
                const bool reversed = dir[node.axis] < 0.0;
                stack[top++] = reversed ? left : right;
                current = reversed ? right : left;
                continue;
            }
        }
        if (top == 0) break;
        current = stack[--top];
    }
    return hit_anything;
}

}  // namespace kiln
