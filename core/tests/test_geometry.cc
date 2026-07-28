// Pure polygon geometry: convex hull, minimum-area rectangle, unclip.
#include "geometry.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using naina::internal::convex_hull;
using naina::internal::min_area_quad;
using naina::internal::polygon_area;
using naina::internal::polygon_perimeter;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void test_hull_of_a_square_with_interior_points() {
    std::vector<naina_point> pts = {
        {0, 0}, {10, 0}, {10, 10}, {0, 10},  // corners
        {5, 5}, {3, 7},  {8, 2},             // interior, must be dropped
    };
    const auto hull = convex_hull(pts);
    EXPECT(hull.size() == 4);
    // Hull area equals the square's area.
    EXPECT(std::fabs(polygon_area(hull) - 100.0F) < 1e-3F);
}

static void test_hull_handles_collinear_and_duplicate_points() {
    std::vector<naina_point> pts = {
        {0, 0}, {5, 0}, {10, 0},  // collinear along the bottom
        {10, 10}, {0, 10}, {0, 0} // duplicate of the first
    };
    const auto hull = convex_hull(pts);
    // Collinear interior points are not vertices.
    EXPECT(hull.size() == 4);
    EXPECT(std::fabs(polygon_area(hull) - 100.0F) < 1e-3F);
}

static void test_hull_degenerate_inputs() {
    EXPECT(convex_hull({}).empty());
    const auto one = convex_hull({{3, 4}});
    EXPECT(one.size() == 1);
    const auto two = convex_hull({{0, 0}, {1, 1}});
    EXPECT(two.size() == 2);
}

static void test_min_area_quad_of_axis_aligned_box() {
    std::vector<naina_point> pts = {{2, 3}, {12, 3}, {12, 9}, {2, 9}};
    naina_point out[4];
    EXPECT(min_area_quad(pts, out));
    // Area must be 10*6 = 60 regardless of corner ordering.
    std::vector<naina_point> q(out, out + 4);
    EXPECT(std::fabs(polygon_area(q) - 60.0F) < 1e-2F);
    // Every input point lies on or inside the quad's bounding extent.
    float minx = out[0].x, maxx = out[0].x, miny = out[0].y, maxy = out[0].y;
    for (int i = 1; i < 4; ++i) {
        minx = out[i].x < minx ? out[i].x : minx;
        maxx = out[i].x > maxx ? out[i].x : maxx;
        miny = out[i].y < miny ? out[i].y : miny;
        maxy = out[i].y > maxy ? out[i].y : maxy;
    }
    EXPECT(minx <= 2.01F && maxx >= 11.99F);
    EXPECT(miny <= 3.01F && maxy >= 8.99F);
}

static void test_min_area_quad_beats_bounding_box_when_rotated() {
    // A 45-degree diamond, centered at (10,10) with "radius" 10. It is
    // already a rotated square, so its own shoelace area is 200 (side^2,
    // side = sqrt(200)). Its axis-aligned bbox spans x in [0,20], y in
    // [0,20]: area 400. The true minimum-area enclosing rectangle is the
    // diamond itself, at area 200 -- half the naive bbox, not the AABB.
    //
    // NOTE: the plan this test was copied from claimed bbox=200 and
    // min-rect=100 (both off by a factor of 2 from hand-verified shoelace
    // and bbox arithmetic); fixed here per the plan's own guidance to
    // correct a wrong hand-computed assertion rather than bend the
    // implementation to match it.
    std::vector<naina_point> pts = {{10, 0}, {20, 10}, {10, 20}, {0, 10}};
    naina_point out[4];
    EXPECT(min_area_quad(pts, out));
    std::vector<naina_point> q(out, out + 4);
    const float area = polygon_area(q);
    EXPECT(area < 250.0F);              // strictly better than the 400 bbox
    EXPECT(std::fabs(area - 200.0F) < 1.0F);  // matches the true minimum
}

static void test_min_area_quad_rejects_too_few_points() {
    naina_point out[4];
    EXPECT(!min_area_quad({}, out));
    EXPECT(!min_area_quad({{0, 0}, {1, 1}}, out));
}

static void test_perimeter_and_area() {
    std::vector<naina_point> sq = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    EXPECT(std::fabs(polygon_area(sq) - 16.0F) < 1e-3F);
    EXPECT(std::fabs(polygon_perimeter(sq) - 16.0F) < 1e-3F);
    // Area is unsigned: reversing the winding must not flip the sign.
    std::vector<naina_point> rev = {{0, 4}, {4, 4}, {4, 0}, {0, 0}};
    EXPECT(std::fabs(polygon_area(rev) - 16.0F) < 1e-3F);
}

int main() {
    test_hull_of_a_square_with_interior_points();
    test_hull_handles_collinear_and_duplicate_points();
    test_hull_degenerate_inputs();
    test_min_area_quad_of_axis_aligned_box();
    test_min_area_quad_beats_bounding_box_when_rotated();
    test_min_area_quad_rejects_too_few_points();
    test_perimeter_and_area();
    if (failures == 0) {
        std::printf("test_geometry: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
