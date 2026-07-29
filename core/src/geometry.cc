#include "geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace naina::internal {

namespace {

// Cross product of (o->a) x (o->b). Positive when o,a,b turn left.
float cross(const naina_point& o, const naina_point& a, const naina_point& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

}  // namespace

float polygon_area(const std::vector<naina_point>& poly) {
    if (poly.size() < 3) {
        return 0.0F;
    }
    float acc = 0.0F;
    for (size_t i = 0, n = poly.size(); i < n; ++i) {
        const naina_point& p = poly[i];
        const naina_point& q = poly[(i + 1) % n];
        acc += p.x * q.y - q.x * p.y;
    }
    return std::fabs(acc) * 0.5F;
}

float polygon_perimeter(const std::vector<naina_point>& poly) {
    if (poly.size() < 2) {
        return 0.0F;
    }
    float acc = 0.0F;
    for (size_t i = 0, n = poly.size(); i < n; ++i) {
        const naina_point& p = poly[i];
        const naina_point& q = poly[(i + 1) % n];
        const float dx = q.x - p.x;
        const float dy = q.y - p.y;
        acc += std::sqrt(dx * dx + dy * dy);
    }
    return acc;
}

std::vector<naina_point> convex_hull(std::vector<naina_point> pts) {
    std::sort(pts.begin(), pts.end(), [](const naina_point& a, const naina_point& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    pts.erase(std::unique(pts.begin(), pts.end(),
                          [](const naina_point& a, const naina_point& b) {
                              return a.x == b.x && a.y == b.y;
                          }),
              pts.end());
    const size_t n = pts.size();
    if (n < 3) {
        return pts;
    }

    std::vector<naina_point> hull(2 * n);
    size_t k = 0;
    // Lower hull, then upper hull. `<= 0` drops collinear points.
    for (size_t i = 0; i < n; ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0.0F) {
            --k;
        }
        hull[k++] = pts[i];
    }
    for (size_t i = n - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i - 1]) <= 0.0F) {
            --k;
        }
        hull[k++] = pts[i - 1];
    }
    hull.resize(k - 1);  // last point repeats the first
    return hull;
}

bool min_area_quad(const std::vector<naina_point>& pts, naina_point out[4]) {
    if (out == nullptr) {
        return false;
    }
    const std::vector<naina_point> hull = convex_hull(pts);
    if (hull.size() < 3) {
        return false;
    }

    float best_area = -1.0F;
    float best_ux = 1.0F;
    float best_uy = 0.0F;
    float best_min_u = 0.0F;
    float best_max_u = 0.0F;
    float best_min_v = 0.0F;
    float best_max_v = 0.0F;

    // Rotating calipers: the minimum-area rectangle always has one side
    // flush with a hull edge, so testing every edge direction is exact.
    const size_t n = hull.size();
    for (size_t i = 0; i < n; ++i) {
        const naina_point& a = hull[i];
        const naina_point& b = hull[(i + 1) % n];
        float ex = b.x - a.x;
        float ey = b.y - a.y;
        const float len = std::sqrt(ex * ex + ey * ey);
        if (len < 1e-6F) {
            continue;
        }
        ex /= len;
        ey /= len;
        // Project every hull vertex onto the edge direction and its normal.
        float min_u = 1e30F;
        float max_u = -1e30F;
        float min_v = 1e30F;
        float max_v = -1e30F;
        for (const auto& p : hull) {
            const float u = p.x * ex + p.y * ey;
            const float v = -p.x * ey + p.y * ex;
            min_u = std::min(min_u, u);
            max_u = std::max(max_u, u);
            min_v = std::min(min_v, v);
            max_v = std::max(max_v, v);
        }
        const float area = (max_u - min_u) * (max_v - min_v);
        if (best_area < 0.0F || area < best_area) {
            best_area = area;
            best_ux = ex;
            best_uy = ey;
            best_min_u = min_u;
            best_max_u = max_u;
            best_min_v = min_v;
            best_max_v = max_v;
        }
    }
    if (best_area < 0.0F) {
        return false;
    }

    // Rebuild world-space corners from (u, v) extents.
    auto to_world = [&](float u, float v) {
        naina_point p{};
        p.x = u * best_ux - v * best_uy;
        p.y = u * best_uy + v * best_ux;
        return p;
    };
    out[0] = to_world(best_min_u, best_min_v);
    out[1] = to_world(best_max_u, best_min_v);
    out[2] = to_world(best_max_u, best_max_v);
    out[3] = to_world(best_min_u, best_max_v);
    return true;
}

void order_quad_clockwise(naina_point q[4]) {
    if (q == nullptr) {
        return;
    }
    // Sort by x: the first two are the left pair, the last two the right pair.
    std::array<naina_point, 4> p = {q[0], q[1], q[2], q[3]};
    std::sort(p.begin(), p.end(), [](const naina_point& a, const naina_point& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    // Within each pair the smaller y is the upper corner.
    naina_point tl = p[0];
    naina_point bl = p[1];
    if (bl.y < tl.y) {
        std::swap(tl, bl);
    }
    naina_point tr = p[2];
    naina_point br = p[3];
    if (br.y < tr.y) {
        std::swap(tr, br);
    }
    q[0] = tl;
    q[1] = tr;
    q[2] = br;
    q[3] = bl;
}

std::vector<naina_point> offset_convex_polygon(const std::vector<naina_point>& poly,
                                               float distance) {
    if (poly.size() < 3 || distance == 0.0F) {
        return poly;
    }
    const size_t n = poly.size();

    // Signed area tells us the winding, which fixes which normal points out.
    float signed2 = 0.0F;
    for (size_t i = 0; i < n; ++i) {
        const naina_point& p = poly[i];
        const naina_point& q = poly[(i + 1) % n];
        signed2 += p.x * q.y - q.x * p.y;
    }
    // For counter-clockwise winding (signed2 > 0) the outward normal of edge
    // p->q is (dy, -dx); for clockwise it is the negation.
    const float sign = (signed2 > 0.0F) ? 1.0F : -1.0F;

    // Each edge becomes a line offset outward by `distance`, stored as a
    // point plus direction. Adjacent offset lines are then intersected.
    struct Line {
        float px, py, dx, dy;
    };
    std::vector<Line> lines(n);
    for (size_t i = 0; i < n; ++i) {
        const naina_point& p = poly[i];
        const naina_point& q = poly[(i + 1) % n];
        float dx = q.x - p.x;
        float dy = q.y - p.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9F) {
            // Zero-length edge: keep the vertex, no meaningful normal.
            lines[i] = Line{p.x, p.y, 1.0F, 0.0F};
            continue;
        }
        dx /= len;
        dy /= len;
        const float nx = sign * dy;
        const float ny = -sign * dx;
        lines[i] = Line{p.x + nx * distance, p.y + ny * distance, dx, dy};
    }

    std::vector<naina_point> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const Line& a = lines[(i + n - 1) % n];
        const Line& b = lines[i];
        // Solve a.p + t*a.d == b.p + s*b.d for t.
        const float denom = a.dx * b.dy - a.dy * b.dx;
        if (std::fabs(denom) < 1e-9F) {
            // Parallel adjacent edges (straight line): use the offset vertex.
            out.push_back(naina_point{b.px, b.py});
            continue;
        }
        const float t = ((b.px - a.px) * b.dy - (b.py - a.py) * b.dx) / denom;
        out.push_back(naina_point{a.px + a.dx * t, a.py + a.dy * t});
    }
    return out;
}

}  // namespace naina::internal
