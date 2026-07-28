// Pure polygon geometry: convex hull, minimum-area rectangle, unclip.
#include "geometry.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using naina::internal::convex_hull;
using naina::internal::min_area_quad;
using naina::internal::offset_convex_polygon;
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

static void test_offset_grows_a_square_by_distance_on_every_side() {
    std::vector<naina_point> sq = {{10, 10}, {20, 10}, {20, 20}, {10, 20}};
    const auto out = offset_convex_polygon(sq, 2.0F);
    EXPECT(out.size() == 4);
    // A square offset by d becomes a square with each side 2d longer.
    EXPECT(std::fabs(polygon_area(out) - 196.0F) < 0.5F);  // 14 * 14
    float minx = 1e9F, maxx = -1e9F, miny = 1e9F, maxy = -1e9F;
    for (const auto& p : out) {
        minx = p.x < minx ? p.x : minx;
        maxx = p.x > maxx ? p.x : maxx;
        miny = p.y < miny ? p.y : miny;
        maxy = p.y > maxy ? p.y : maxy;
    }
    EXPECT(std::fabs(minx - 8.0F) < 0.1F);
    EXPECT(std::fabs(maxx - 22.0F) < 0.1F);
    EXPECT(std::fabs(miny - 8.0F) < 0.1F);
    EXPECT(std::fabs(maxy - 22.0F) < 0.1F);
}

static void test_offset_is_winding_order_independent() {
    std::vector<naina_point> ccw = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    std::vector<naina_point> cw = {{0, 10}, {10, 10}, {10, 0}, {0, 0}};
    const float a = polygon_area(offset_convex_polygon(ccw, 3.0F));
    const float b = polygon_area(offset_convex_polygon(cw, 3.0F));
    // Both must GROW, not one grow and one shrink.
    EXPECT(a > 100.0F);
    EXPECT(b > 100.0F);
    EXPECT(std::fabs(a - b) < 0.5F);
}

static void test_offset_zero_distance_is_identity() {
    std::vector<naina_point> sq = {{1, 1}, {5, 1}, {5, 5}, {1, 5}};
    const auto out = offset_convex_polygon(sq, 0.0F);
    EXPECT(std::fabs(polygon_area(out) - 16.0F) < 1e-3F);
}

static void test_offset_degenerate_input_is_safe() {
    EXPECT(offset_convex_polygon({}, 5.0F).empty());
    const auto two = offset_convex_polygon({{0, 0}, {1, 1}}, 5.0F);
    EXPECT(two.size() == 2);  // returned unchanged, not corrupted
}

static void test_db_unclip_distance_formula() {
    // A 10x4 box: area 40, perimeter 28, ratio 1.4
    //   distance = 40 * 1.4 / 28 = 2.0
    std::vector<naina_point> box = {{0, 0}, {10, 0}, {10, 4}, {0, 4}};
    const float area = polygon_area(box);
    const float per = polygon_perimeter(box);
    EXPECT(std::fabs(area - 40.0F) < 1e-3F);
    EXPECT(std::fabs(per - 28.0F) < 1e-3F);
    const float d = area * 1.4F / per;
    EXPECT(std::fabs(d - 2.0F) < 1e-3F);
    const auto grown = offset_convex_polygon(box, d);
    // 14 x 8 = 112
    EXPECT(std::fabs(polygon_area(grown) - 112.0F) < 1.0F);
}

int main() {
    test_hull_of_a_square_with_interior_points();
    test_hull_handles_collinear_and_duplicate_points();
    test_hull_degenerate_inputs();
    test_min_area_quad_of_axis_aligned_box();
    test_min_area_quad_beats_bounding_box_when_rotated();
    test_min_area_quad_rejects_too_few_points();
    test_perimeter_and_area();
    test_offset_grows_a_square_by_distance_on_every_side();
    test_offset_is_winding_order_independent();
    test_offset_zero_distance_is_identity();
    test_offset_degenerate_input_is_safe();
    test_db_unclip_distance_formula();
    if (failures == 0) {
        std::printf("test_geometry: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
