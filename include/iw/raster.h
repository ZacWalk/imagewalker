// Minimal anti-aliased 2D rasterizer over a 32bpp BGRA surface.
//
// This replaces the three things ImageWalker actually used AGG for: a
// gradient-filled rounded rectangle, a gradient-filled rectangle, and an
// additively blended anti-aliased polygon. Image scaling is not here; that has
// always been done by the app's own blitter.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace iw::raster {

struct Color {
    std::uint8_t b = 0, g = 0, r = 0, a = 255;

    static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) { return {b, g, r, 255}; }
    static constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) { return {b, g, r, a}; }

    // COLORREF is 0x00BBGGRR.
    static constexpr Color fromColorRef(std::uint32_t c, std::uint8_t alpha = 255) {
        return {static_cast<std::uint8_t>((c >> 16) & 0xFF),
                static_cast<std::uint8_t>((c >> 8) & 0xFF),
                static_cast<std::uint8_t>(c & 0xFF), alpha};
    }
};

// Non-owning view of a top-down 32bpp BGRA buffer.
struct Surface {
    std::uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;

    std::uint32_t* row(int y) { return reinterpret_cast<std::uint32_t*>(data + std::size_t(y) * stride); }
    const std::uint32_t* row(int y) const {
        return reinterpret_cast<const std::uint32_t*>(data + std::size_t(y) * stride);
    }
};

enum class Blend {
    SrcOver,
    Plus,  // agg::comp_op_plus
};

enum class FillRule {
    NonZero,
    EvenOdd,
};

struct Paint {
    Color color;
    Blend blend = Blend::SrcOver;

    // When set, colour is interpolated from `color` at (x0,y0) to `color1` at
    // (x1,y1) and clamped outside that span.
    bool gradient = false;
    Color color1;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    static Paint solid(Color c, Blend blend = Blend::SrcOver);
    static Paint linearGradient(Color from, Color to, float x0, float y0, float x1, float y1,
                                Blend blend = Blend::SrcOver);
};

class Path {
public:
    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void close() {}  // subpaths are always closed for filling

    void addRect(float x0, float y0, float x1, float y1);
    void addRoundRect(float x0, float y0, float x1, float y1, float radius);
    void addCircle(float cx, float cy, float radius);

    bool empty() const { return points_.empty(); }
    void clear();

    // Calls fn(ax, ay, bx, by) for every edge, implicitly closing each subpath.
    template <class Fn>
    void forEachEdge(Fn&& fn) const {
        for (std::size_t s = 0; s < starts_.size(); ++s) {
            const std::size_t begin = starts_[s];
            const std::size_t end = (s + 1 < starts_.size()) ? starts_[s + 1] : points_.size();
            if (end - begin < 2) continue;
            for (std::size_t i = begin; i + 1 < end; ++i)
                fn(points_[i].x, points_[i].y, points_[i + 1].x, points_[i + 1].y);
            fn(points_[end - 1].x, points_[end - 1].y, points_[begin].x, points_[begin].y);
        }
    }

private:
    struct Pt {
        float x, y;
    };
    std::vector<Pt> points_;
    std::vector<std::size_t> starts_;
};

// Expands `path` into an outline that, filled with the non-zero rule, looks like
// the original stroked with round joins and caps.
Path strokePath(const Path& path, float width);

void fillPath(Surface& surface, const Path& path, const Paint& paint, FillRule rule = FillRule::NonZero);

// --- implementation ---------------------------------------------------------

namespace detail {

inline constexpr float kPi = 3.14159265358979323846f;

// Enough segments that the curve stays smooth at UI sizes without generating
// pointless work for small radii.
inline int arcSegments(float radius) {
    const int n = static_cast<int>(std::ceil(radius * 0.75f));
    return n < 4 ? 4 : (n > 64 ? 64 : n);
}

}  // namespace detail

inline void Path::moveTo(float x, float y) {
    starts_.push_back(points_.size());
    points_.push_back({x, y});
}

inline void Path::lineTo(float x, float y) {
    if (points_.empty()) {
        moveTo(x, y);
        return;
    }
    points_.push_back({x, y});
}

inline void Path::clear() {
    points_.clear();
    starts_.clear();
}

inline void Path::addRect(float x0, float y0, float x1, float y1) {
    moveTo(x0, y0);
    lineTo(x1, y0);
    lineTo(x1, y1);
    lineTo(x0, y1);
}

inline void Path::addRoundRect(float x0, float y0, float x1, float y1, float radius) {
    const float maxRadius = 0.5f * std::fmin(x1 - x0, y1 - y0);
    const float r = radius < 0.0f ? 0.0f : (radius > maxRadius ? maxRadius : radius);
    if (r <= 0.0f) {
        addRect(x0, y0, x1, y1);
        return;
    }

    const int n = detail::arcSegments(r);
    // Corner centres, walked clockwise starting from the top-left.
    const float cx[4] = {x0 + r, x1 - r, x1 - r, x0 + r};
    const float cy[4] = {y0 + r, y0 + r, y1 - r, y1 - r};
    const float startAngle[4] = {detail::kPi, 1.5f * detail::kPi, 0.0f, 0.5f * detail::kPi};

    bool first = true;
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i <= n; ++i) {
            const float a = startAngle[corner] + (0.5f * detail::kPi) * (static_cast<float>(i) / n);
            const float px = cx[corner] + r * std::cos(a);
            const float py = cy[corner] + r * std::sin(a);
            if (first) {
                moveTo(px, py);
                first = false;
            } else {
                lineTo(px, py);
            }
        }
    }
}

inline void Path::addCircle(float cx, float cy, float radius) {
    if (radius <= 0.0f) return;
    const int n = detail::arcSegments(radius) * 4;
    for (int i = 0; i < n; ++i) {
        const float a = 2.0f * detail::kPi * (static_cast<float>(i) / n);
        const float px = cx + radius * std::cos(a);
        const float py = cy + radius * std::sin(a);
        if (i == 0)
            moveTo(px, py);
        else
            lineTo(px, py);
    }
}

namespace detail {

// Every emitted contour is wound the same way as Path::addRect, so that filling
// the union with the non-zero rule merges them instead of cancelling out.
inline void addSegmentQuad(Path& out, float ax, float ay, float bx, float by, float half) {
    const float dx = bx - ax, dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 1e-6f) return;

    const float nx = -dy / len * half;
    const float ny = dx / len * half;

    out.moveTo(ax - nx, ay - ny);
    out.lineTo(bx - nx, by - ny);
    out.lineTo(bx + nx, by + ny);
    out.lineTo(ax + nx, ay + ny);
}

}  // namespace detail

inline Path strokePath(const Path& path, float width) {
    Path out;
    const float half = (width < 1.0f ? 1.0f : width) * 0.5f;

    path.forEachEdge([&](float ax, float ay, float bx, float by) {
        detail::addSegmentQuad(out, ax, ay, bx, by, half);
        // A disc at each vertex gives round joins and caps for free.
        if (half > 0.5f) out.addCircle(ax, ay, half);
    });

    return out;
}

inline Paint Paint::solid(Color c, Blend blend) {
    Paint p;
    p.color = c;
    p.blend = blend;
    return p;
}

inline Paint Paint::linearGradient(Color from, Color to, float x0, float y0, float x1, float y1, Blend blend) {
    Paint p;
    p.color = from;
    p.color1 = to;
    p.gradient = true;
    p.x0 = x0;
    p.y0 = y0;
    p.x1 = x1;
    p.y1 = y1;
    p.blend = blend;
    return p;
}

namespace detail {

// Vertical supersampling with exact horizontal coverage. Analytic area
// accumulation would be marginally sharper, but this is far easier to get
// provably right and the shapes involved are UI chrome.
inline constexpr int kSubSamples = 16;
inline constexpr float kSubWeight = 1.0f / kSubSamples;

struct Edge {
    float ytop, ybot;  // ytop < ybot
    float x;           // x at ytop
    float dxdy;
    int dir;  // +1 if the original edge pointed down, -1 if up
};

struct Crossing {
    float x;
    int dir;
};

inline void addSpan(float* cov, int width, float xa, float xb, float weight) {
    if (xa < 0.0f) xa = 0.0f;
    if (xb > static_cast<float>(width)) xb = static_cast<float>(width);
    if (!(xa < xb)) return;  // also rejects NaN, which every < and > lets through

    const int ia = static_cast<int>(xa);
    const int ib = static_cast<int>(xb);

    if (ia == ib) {
        cov[ia] += (xb - xa) * weight;
        return;
    }
    cov[ia] += (static_cast<float>(ia + 1) - xa) * weight;
    for (int i = ia + 1; i < ib; ++i) cov[i] += weight;
    if (ib < width) cov[ib] += (xb - static_cast<float>(ib)) * weight;
}

inline int mul255(int a, int b) { return (a * b + 127) / 255; }

inline void blendPixel(std::uint32_t& dst, const Color& src, int alpha, Blend blend) {
    if (alpha <= 0) return;

    const int sb = mul255(src.b, alpha);
    const int sg = mul255(src.g, alpha);
    const int sr = mul255(src.r, alpha);

    const int db = dst & 0xFF;
    const int dg = (dst >> 8) & 0xFF;
    const int dr = (dst >> 16) & 0xFF;
    const int da = (dst >> 24) & 0xFF;

    int ob, og, orr, oa;
    if (blend == Blend::Plus) {
        // Parenthesised because windows.h defines min/max as macros in these apps.
        ob = (std::min)(255, db + sb);
        og = (std::min)(255, dg + sg);
        orr = (std::min)(255, dr + sr);
        oa = (std::min)(255, da + alpha);
    } else {
        const int inv = 255 - alpha;
        ob = sb + mul255(db, inv);
        og = sg + mul255(dg, inv);
        orr = sr + mul255(dr, inv);
        oa = alpha + mul255(da, inv);
    }

    dst = (static_cast<std::uint32_t>(oa) << 24) | (static_cast<std::uint32_t>(orr) << 16) |
          (static_cast<std::uint32_t>(og) << 8) | static_cast<std::uint32_t>(ob);
}

}  // namespace detail

inline void fillPath(Surface& surface, const Path& path, const Paint& paint, FillRule rule) {
    if (!surface.data || surface.width <= 0 || surface.height <= 0 || path.empty()) return;

    std::vector<detail::Edge> edges;
    float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;

    path.forEachEdge([&](float ax, float ay, float bx, float by) {
        minX = std::fmin(minX, std::fmin(ax, bx));
        maxX = std::fmax(maxX, std::fmax(ax, bx));
        minY = std::fmin(minY, std::fmin(ay, by));
        maxY = std::fmax(maxY, std::fmax(ay, by));

        if (ay == by) return;  // horizontal edges never produce a crossing

        detail::Edge e{};
        if (ay < by) {
            e.ytop = ay; e.ybot = by; e.x = ax; e.dxdy = (bx - ax) / (by - ay); e.dir = +1;
        } else {
            e.ytop = by; e.ybot = ay; e.x = bx; e.dxdy = (ax - bx) / (ay - by); e.dir = -1;
        }
        edges.push_back(e);
    });

    if (edges.empty()) return;

    const int y0 = (std::max)(0, static_cast<int>(std::floor(minY)));
    const int y1 = (std::min)(surface.height, static_cast<int>(std::ceil(maxY)));
    const int x0 = (std::max)(0, static_cast<int>(std::floor(minX)));
    const int x1 = (std::min)(surface.width, static_cast<int>(std::ceil(maxX)) + 1);
    if (y0 >= y1 || x0 >= x1) return;

    std::sort(edges.begin(), edges.end(),
              [](const detail::Edge& a, const detail::Edge& b) { return a.ytop < b.ytop; });

    std::vector<float> coverage(surface.width, 0.0f);
    std::vector<const detail::Edge*> active;
    std::vector<detail::Crossing> crossings;
    std::size_t next = 0;

    // Gradient setup: project the pixel centre onto (x0,y0)->(x1,y1).
    const float gdx = paint.x1 - paint.x0;
    const float gdy = paint.y1 - paint.y0;
    const float gLen2 = gdx * gdx + gdy * gdy;
    const float gInv = gLen2 > 1e-9f ? 1.0f / gLen2 : 0.0f;

    for (int y = y0; y < y1; ++y) {
        const float yTop = static_cast<float>(y);
        const float yBot = yTop + 1.0f;

        while (next < edges.size() && edges[next].ytop < yBot) active.push_back(&edges[next++]);
        active.erase(std::remove_if(active.begin(), active.end(),
                                    [&](const detail::Edge* e) { return e->ybot <= yTop; }),
                     active.end());
        if (active.empty()) continue;

        std::fill(coverage.begin() + x0, coverage.begin() + x1, 0.0f);

        for (int s = 0; s < detail::kSubSamples; ++s) {
            const float sy = yTop + (static_cast<float>(s) + 0.5f) * detail::kSubWeight;

            crossings.clear();
            for (const detail::Edge* e : active) {
                if (sy < e->ytop || sy >= e->ybot) continue;
                crossings.push_back({e->x + (sy - e->ytop) * e->dxdy, e->dir});
            }
            if (crossings.size() < 2) continue;

            std::sort(crossings.begin(), crossings.end(),
                      [](const detail::Crossing& a, const detail::Crossing& b) { return a.x < b.x; });

            int winding = 0;
            for (std::size_t i = 0; i + 1 < crossings.size(); ++i) {
                winding += crossings[i].dir;
                const bool inside = (rule == FillRule::NonZero) ? (winding != 0) : ((i & 1) == 0);
                if (inside)
                    detail::addSpan(coverage.data(), surface.width, crossings[i].x, crossings[i + 1].x,
                                    detail::kSubWeight);
            }
        }

        std::uint32_t* dstRow = surface.row(y);
        const float py = yTop + 0.5f;

        for (int x = x0; x < x1; ++x) {
            const float c = coverage[x];
            if (c <= 0.0015f) continue;

            Color src = paint.color;
            if (paint.gradient) {
                const float px = static_cast<float>(x) + 0.5f;
                float t = ((px - paint.x0) * gdx + (py - paint.y0) * gdy) * gInv;
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                src.b = static_cast<std::uint8_t>(paint.color.b + t * (paint.color1.b - paint.color.b));
                src.g = static_cast<std::uint8_t>(paint.color.g + t * (paint.color1.g - paint.color.g));
                src.r = static_cast<std::uint8_t>(paint.color.r + t * (paint.color1.r - paint.color.r));
                src.a = static_cast<std::uint8_t>(paint.color.a + t * (paint.color1.a - paint.color.a));
            }

            const float cc = c > 1.0f ? 1.0f : c;
            detail::blendPixel(dstRow[x], src, static_cast<int>(src.a * cc + 0.5f), paint.blend);
        }
    }
}

}  // namespace iw::raster
