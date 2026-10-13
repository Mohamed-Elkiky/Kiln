#include "kiln/bvh.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace kiln {

namespace {

constexpr int kSAHBins = 16;

Point3 centroid(const AABB& box) {
    return {0.5 * (box.x.min + box.x.max), 0.5 * (box.y.min + box.y.max),
            0.5 * (box.z.min + box.z.max)};
}

}  // namespace

BVHNode::BVHNode(Objects objects, BVHSplit split) {
    build(objects, 0, objects.size(), split);
}

void BVHNode::build(Objects& objects, std::size_t start, std::size_t end, BVHSplit split) {
    for (std::size_t i = start; i < end; ++i) bbox_ = AABB(bbox_, objects[i]->bounding_box());

    const std::size_t count = end - start;
    if (count <= kMaxLeafSize) {
        leaf_.assign(objects.begin() + start, objects.begin() + end);
        return;
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

    // Median split on the longest centroid axis. nth_element only partially sorts: O(n)
    // per level instead of O(n log n), with the same result as a full sort for a halving split.
    auto median_split = [&](int axis) {
        axis_ = axis;
        std::nth_element(first, mid, last, [axis](const auto& a, const auto& b) {
            return centroid(a->bounding_box())[axis] < centroid(b->bounding_box())[axis];
        });
    };

    int longest = 0;
    for (int a = 1; a < 3; ++a) {
        if (cmax[a] - cmin[a] > cmax[longest] - cmin[longest]) longest = a;
    }

    if (split == BVHSplit::kMidpoint) {
        median_split(longest);
    } else {
        // Binned SAH. Expected cost of a split ~ area(L) * count(L) + area(R) * count(R):
        // a ray reaches a child with probability proportional to its surface area, then pays
        // for every primitive inside. Try kSAHBins - 1 split planes per axis, keep the cheapest.
        double best_cost = std::numeric_limits<double>::infinity();
        int best_axis = -1;
        int best_bin = 0;

        for (int axis = 0; axis < 3; ++axis) {
            const double extent = cmax[axis] - cmin[axis];
            if (extent <= 0.0) continue;  // all centroids level on this axis: can't split here

            const double scale = kSAHBins / extent;
            auto bin_of = [&](const Point3& c) {
                return std::min(kSAHBins - 1, static_cast<int>((c[axis] - cmin[axis]) * scale));
            };

            std::array<AABB, kSAHBins> bin_box{};
            std::array<std::size_t, kSAHBins> bin_count{};
            for (auto it = first; it != last; ++it) {
                const AABB box = (*it)->bounding_box();
                const int b = bin_of(centroid(box));
                bin_box[b] = AABB(bin_box[b], box);
                ++bin_count[b];
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
                    best_axis = axis;
                    best_bin = b;
                }
            }
        }

        if (best_axis < 0) {
            median_split(longest);  // every centroid in one spot: any halving is as good as SAH
        } else {
            axis_ = best_axis;
            const double extent = cmax[best_axis] - cmin[best_axis];
            const double scale = kSAHBins / extent;
            const int axis = best_axis;
            const double lo = cmin[axis];
            mid = std::partition(first, last, [&](const auto& obj) {
                const Point3 c = centroid(obj->bounding_box());
                return std::min(kSAHBins - 1, static_cast<int>((c[axis] - lo) * scale)) <= best_bin;
            });
        }
    }

    const auto split_index = static_cast<std::size_t>(mid - objects.begin());
    left_ = std::shared_ptr<BVHNode>(new BVHNode());
    right_ = std::shared_ptr<BVHNode>(new BVHNode());
    left_->build(objects, start, split_index, split);
    right_->build(objects, split_index, end, split);
}

bool BVHNode::hit(const Ray& r, Interval ray_t, HitRecord& rec) const {
    // Miss the box, miss everything inside it.
    if (!bbox_.hit(r, ray_t)) return false;

    if (!left_) {
        // Leaf: test each primitive, keeping only hits closer than the best so far.
        bool hit_anything = false;
        for (const auto& object : leaf_) {
            if (object->hit(r, ray_t, rec)) {
                hit_anything = true;
                ray_t.max = rec.t;
            }
        }
        return hit_anything;
    }

    // Visit the child on the ray's near side first. If it hits, t_max shrinks to that hit,
    // so the far child's box test usually fails straight away and its subtree is skipped.
    const bool reversed = r.direction()[axis_] < 0.0;
    const BVHNode& near_child = reversed ? *right_ : *left_;
    const BVHNode& far_child = reversed ? *left_ : *right_;

    const bool hit_near = near_child.hit(r, ray_t, rec);
    const bool hit_far = far_child.hit(r, Interval(ray_t.min, hit_near ? rec.t : ray_t.max), rec);
    return hit_near || hit_far;
}

}  // namespace kiln
