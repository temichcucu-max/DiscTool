#include "disktool/core/treemap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using Catch::Approx;
using disktool::core::Rect;
using disktool::core::TreemapItem;
using disktool::core::squarifiedTreemap;

namespace {

constexpr double kEpsilon = 1e-9;

double area(const Rect& rect)
{
    return rect.w * rect.h;
}

bool isInside(const Rect& rect, const Rect& bounds)
{
    return rect.x >= bounds.x - kEpsilon && rect.y >= bounds.y - kEpsilon
        && rect.x + rect.w <= bounds.x + bounds.w + kEpsilon
        && rect.y + rect.h <= bounds.y + bounds.h + kEpsilon;
}

double overlapArea(const Rect& left, const Rect& right)
{
    const double width = std::min(left.x + left.w, right.x + right.w)
        - std::max(left.x, right.x);
    const double height = std::min(left.y + left.h, right.y + right.h)
        - std::max(left.y, right.y);
    if (width <= 0 || height <= 0) {
        return 0;
    }
    return width * height;
}

void checkLayout(const std::vector<TreemapItem>& items, const Rect& bounds)
{
    double totalArea = 0;
    for (const auto& item : items) {
        CHECK(isInside(item.rect, bounds));
        CHECK(item.rect.w > 0);
        CHECK(item.rect.h > 0);
        totalArea += area(item.rect);
    }

    CHECK(totalArea == Approx(bounds.w * bounds.h).margin(1e-6));

    for (std::size_t left = 0; left < items.size(); ++left) {
        for (std::size_t right = left + 1; right < items.size(); ++right) {
            CHECK(overlapArea(items[left].rect, items[right].rect) <= kEpsilon);
        }
    }
}

}  // namespace

TEST_CASE("squarifiedTreemap returns bounds for a single item", "[treemap]")
{
    const Rect bounds{10, 20, 300, 200};

    const auto items = squarifiedTreemap(bounds, {42});

    REQUIRE(items.size() == 1);
    CHECK(items[0].index == 0);
    CHECK(items[0].size == 42);
    CHECK(items[0].rect.x == Approx(bounds.x));
    CHECK(items[0].rect.y == Approx(bounds.y));
    CHECK(items[0].rect.w == Approx(bounds.w));
    CHECK(items[0].rect.h == Approx(bounds.h));
}

TEST_CASE("squarifiedTreemap tiles bounds without overlaps", "[treemap]")
{
    const Rect bounds{0, 0, 800, 500};

    SECTION("two items")
    {
        const auto items = squarifiedTreemap(bounds, {5, 3});
        REQUIRE(items.size() == 2);
        checkLayout(items, bounds);
    }

    SECTION("three items")
    {
        const auto items = squarifiedTreemap(bounds, {6, 3, 1});
        REQUIRE(items.size() == 3);
        checkLayout(items, bounds);
    }

    SECTION("ten items")
    {
        const std::vector<std::uintmax_t> sizes{20, 18, 15, 12, 10, 8, 6, 4, 2, 1};
        const auto items = squarifiedTreemap(bounds, sizes);
        REQUIRE(items.size() == sizes.size());
        checkLayout(items, bounds);
    }
}

TEST_CASE("squarifiedTreemap keeps areas proportional to sizes", "[treemap]")
{
    const Rect bounds{0, 0, 700, 100};
    const std::vector<std::uintmax_t> sizes{4, 2, 1};

    const auto items = squarifiedTreemap(bounds, sizes);
    REQUIRE(items.size() == 3);

    const double boundsArea = bounds.w * bounds.h;
    CHECK(area(items[0].rect) / boundsArea == Approx(4.0 / 7.0).margin(1e-9));
    CHECK(area(items[1].rect) / boundsArea == Approx(2.0 / 7.0).margin(1e-9));
    CHECK(area(items[2].rect) / boundsArea == Approx(1.0 / 7.0).margin(1e-9));
}

TEST_CASE("squarifiedTreemap skips zero-sized items", "[treemap]")
{
    const Rect bounds{0, 0, 640, 480};
    const std::vector<std::uintmax_t> sizes{9, 0, 4, 0, 1};

    const auto items = squarifiedTreemap(bounds, sizes);

    REQUIRE(items.size() == 3);
    CHECK(items[0].index == 0);
    CHECK(items[0].size == 9);
    CHECK(items[1].index == 2);
    CHECK(items[1].size == 4);
    CHECK(items[2].index == 4);
    CHECK(items[2].size == 1);
    checkLayout(items, bounds);

    SECTION("all zero sizes produce no rectangles")
    {
        const auto empty = squarifiedTreemap(bounds, {0, 0, 0});
        CHECK(empty.empty());
    }

    SECTION("an empty size list produces no rectangles")
    {
        const auto empty = squarifiedTreemap(bounds, {});
        CHECK(empty.empty());
    }
}

TEST_CASE("squarifiedTreemap keeps elongated bounds non-degenerate", "[treemap]")
{
    const Rect bounds{0, 0, 1000, 10};
    const std::vector<std::uintmax_t> sizes{50, 30, 10, 5, 3, 1};

    const auto items = squarifiedTreemap(bounds, sizes);

    REQUIRE(items.size() == sizes.size());
    checkLayout(items, bounds);

    for (const auto& item : items) {
        CHECK(item.rect.w > kEpsilon);
        CHECK(item.rect.h > kEpsilon);
    }
}
