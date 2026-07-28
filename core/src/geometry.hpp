// Pure 2D polygon geometry for detection post-processing.
//
// PaddleOCR leans on OpenCV for these. naina has no OpenCV dependency and
// will not take one — the whole point is a small portable core — so the
// handful of primitives actually needed live here.
#ifndef NAINA_INTERNAL_GEOMETRY_HPP
#define NAINA_INTERNAL_GEOMETRY_HPP

#include "naina/naina.h"

#include <vector>

namespace naina::internal {

// Unsigned area via the shoelace formula. Winding-order independent.
float polygon_area(const std::vector<naina_point>& poly);

// Closed-path perimeter (includes the last→first edge).
float polygon_perimeter(const std::vector<naina_point>& poly);

// Convex hull, counter-clockwise, no collinear interior vertices.
// Andrew's monotone chain: O(n log n). Inputs of size < 3 are returned
// deduplicated and unchanged in spirit.
std::vector<naina_point> convex_hull(std::vector<naina_point> pts);

// Minimum-area enclosing rectangle via rotating calipers over the hull.
// Writes 4 corners to `out` in consistent winding. Returns false when
// fewer than 3 distinct points are supplied.
bool min_area_quad(const std::vector<naina_point>& pts, naina_point out[4]);

// Expand a convex polygon outward so each edge moves `distance` along its
// outward normal, then re-intersect adjacent edges. This is what DBNet's
// "unclip" step does; for a convex polygon it is exactly equivalent to a
// Clipper/pyclipper offset, without the dependency.
std::vector<naina_point> offset_convex_polygon(const std::vector<naina_point>& poly,
                                               float distance);

}  // namespace naina::internal

#endif  // NAINA_INTERNAL_GEOMETRY_HPP
