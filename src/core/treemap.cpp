#include "disktool/core/treemap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace disktool::core {
namespace {

struct WorkItem {
    double area = 0;
    std::size_t index = 0;
    std::uintmax_t size = 0;
};

// Worst aspect ratio of a row with the given total area, smallest item area,
// largest item area, and row length `side`. Lower is better.
double worstAspect(double rowSum, double minArea, double maxArea, double side)
{
    if (rowSum <= 0 || minArea <= 0 || side <= 0) {
        return std::numeric_limits<double>::infinity();
    }

    const double sumSquared = rowSum * rowSum;
    const double sideSquared = side * side;
    return std::max(sideSquared * maxArea / sumSquared,
                    sumSquared / (sideSquared * minArea));
}

}  // namespace

std::vector<TreemapItem> squarifiedTreemap(
    Rect bounds, const std::vector<std::uintmax_t>& sizes)
{
    std::vector<TreemapItem> result;

    const double boundsArea = bounds.w * bounds.h;
    if (!(boundsArea > 0) || sizes.empty()) {
        return result;
    }

    // Accumulate as double so that huge uintmax_t values cannot overflow.
    double totalSize = 0;
    for (const auto size : sizes) {
        totalSize += static_cast<double>(size);
    }
    if (!(totalSize > 0)) {
        return result;
    }

    const double scale = boundsArea / totalSize;

    std::vector<WorkItem> items;
    items.reserve(sizes.size());
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        if (sizes[index] == 0) {
            continue;
        }
        items.push_back(WorkItem{static_cast<double>(sizes[index]) * scale,
                                 index, sizes[index]});
    }

    if (items.empty()) {
        return result;
    }

    result.reserve(items.size());

    Rect free = bounds;
    std::size_t position = 0;

    while (position < items.size()) {
        const double side = std::min(free.w, free.h);
        if (!(side > 0)) {
            break;
        }

        // Grow the row while its worst aspect ratio improves.
        const std::size_t rowStart = position;
        double rowSum = 0;
        double rowMin = std::numeric_limits<double>::infinity();
        double rowMax = 0;
        double bestWorst = std::numeric_limits<double>::infinity();
        std::size_t rowEnd = position;

        while (position < items.size()) {
            const double candidateSum = rowSum + items[position].area;
            const double candidateMin = std::min(rowMin, items[position].area);
            const double candidateMax = std::max(rowMax, items[position].area);
            const double candidateWorst =
                worstAspect(candidateSum, candidateMin, candidateMax, side);

            if (rowEnd > rowStart && candidateWorst > bestWorst) {
                break;
            }

            rowSum = candidateSum;
            rowMin = candidateMin;
            rowMax = candidateMax;
            bestWorst = candidateWorst;
            ++position;
            rowEnd = position;
        }

        if (rowEnd == rowStart) {
            break;  // defensive: no progress
        }

        // Lay the row out against the shorter side of the free rectangle.
        if (free.w >= free.h) {
            const double rowWidth = rowSum / free.h;
            double y = free.y;
            for (std::size_t index = rowStart; index < rowEnd; ++index) {
                const double height = items[index].area / rowWidth;
                result.push_back(TreemapItem{
                    Rect{free.x, y, rowWidth, height},
                    items[index].index,
                    items[index].size});
                y += height;
            }
            free.x += rowWidth;
            free.w = std::max(0.0, free.w - rowWidth);
        } else {
            const double rowHeight = rowSum / free.w;
            double x = free.x;
            for (std::size_t index = rowStart; index < rowEnd; ++index) {
                const double width = items[index].area / rowHeight;
                result.push_back(TreemapItem{
                    Rect{x, free.y, width, rowHeight},
                    items[index].index,
                    items[index].size});
                x += width;
            }
            free.y += rowHeight;
            free.h = std::max(0.0, free.h - rowHeight);
        }
    }

    return result;
}

}  // namespace disktool::core
