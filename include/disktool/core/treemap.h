#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace disktool::core {

struct Rect {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
};

struct TreemapItem {
    Rect rect;  ///< rectangle in the coordinate space of bounds
    std::size_t index = 0;  ///< index in the original sizes vector
    std::uintmax_t size = 0;
};

/// Squarified treemap (Bruls, Huizing, van Wijk 2000).
///
/// `sizes` must be sorted in descending order (the caller pre-sorts it).
/// Items with size 0 produce no rectangle, so the result contains one item per
/// positive size, preserving the original indices. The rectangles tile bounds
/// without overlaps and their total area equals area(bounds) up to floating
/// point error.
[[nodiscard]] std::vector<TreemapItem> squarifiedTreemap(
    Rect bounds,
    const std::vector<std::uintmax_t>& sizes);

}  // namespace disktool::core
