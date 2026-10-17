// ImageWalker by Zac Walker
// Inverse projective sampling with straight-alpha output and conservative document detection.

#include "EditGeometry.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <new>
#include <vector>

namespace iw::edits
{
	namespace
	{
		double cross(const Corner a, const Corner b, const Corner c)
		{
			return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
		}

		double distance(const Corner a, const Corner b)
		{
			return std::hypot(a.x - b.x, a.y - b.y);
		}

		bool valid_image(const files::DecodedImage& image)
		{
			return image.width > 1 && image.height > 1 &&
				static_cast<std::uint64_t>(image.width) * image.height <= image.pixels.size();
		}

		// Maps the destination unit square directly into the source quadrilateral.
		std::optional<std::array<double, 9>> inverse_map(const Quadrilateral& q)
		{
			if (!valid_quadrilateral(q)) return {};
			const double dx1 = q[1].x - q[2].x, dx2 = q[3].x - q[2].x;
			const double dy1 = q[1].y - q[2].y, dy2 = q[3].y - q[2].y;
			const double dx3 = q[0].x - q[1].x + q[2].x - q[3].x;
			const double dy3 = q[0].y - q[1].y + q[2].y - q[3].y;
			const double divisor = dx1 * dy2 - dx2 * dy1;
			if (std::abs(divisor) < 1e-12) return {};
			const double g = (dx3 * dy2 - dx2 * dy3) / divisor;
			const double h = (dx1 * dy3 - dx3 * dy1) / divisor;
			// A linear denominator attains its extrema at a corner. No pole may cross the image.
			if ((std::min)({1.0, 1.0 + g, 1.0 + h, 1.0 + g + h}) <= 1e-9) return {};
			return std::array<double, 9>{
				q[1].x - q[0].x + g * q[1].x, q[3].x - q[0].x + h * q[3].x, q[0].x,
				q[1].y - q[0].y + g * q[1].y, q[3].y - q[0].y + h * q[3].y, q[0].y,
				g, h, 1.0};
		}

		std::uint32_t sample(const files::DecodedImage& image, double x, double y)
		{
			if (!std::isfinite(x) || !std::isfinite(y) || x < -1e-7 || y < -1e-7 ||
				x > image.width - 1 + 1e-7 || y > image.height - 1 + 1e-7) return 0;
			x = std::clamp(x, 0.0, static_cast<double>(image.width - 1));
			y = std::clamp(y, 0.0, static_cast<double>(image.height - 1));
			const int left = static_cast<int>(x), top = static_cast<int>(y);
			const int right = (std::min)(left + 1, image.width - 1);
			const int bottom = (std::min)(top + 1, image.height - 1);
			const double fx = x - left, fy = y - top;
			const std::array weights{(1 - fx) * (1 - fy), fx * (1 - fy), fx * fy, (1 - fx) * fy};
			const std::array columns{left, right, right, left};
			const std::array rows{top, top, bottom, bottom};
			std::array<double, 3> channels{};
			double alpha = 0;
			for (size_t tap = 0; tap < 4; ++tap)
			{
				const auto pixel = image.pixels[static_cast<size_t>(rows[tap]) * image.width + columns[tap]];
				const double weight = weights[tap] * (pixel >> 24);
				alpha += weight;
				for (size_t c = 0; c < 3; ++c) channels[c] += (pixel >> (c * 8) & 255) * weight;
			}
			if (alpha < 0.5) return 0;
			auto result = static_cast<std::uint32_t>(std::clamp(std::lround(alpha), 0l, 255l)) << 24;
			for (size_t c = 0; c < 3; ++c)
				result |= static_cast<std::uint32_t>(std::clamp(std::lround(channels[c] / alpha), 0l, 255l)) << (c * 8);
			return result;
		}

		double polygon_area(const std::vector<Corner>& points)
		{
			double area = 0;
			for (size_t i = 0; i < points.size(); ++i)
			{
				const auto a = points[i], b = points[(i + 1) % points.size()];
				area += a.x * b.y - a.y * b.x;
			}
			return std::abs(area) * 0.5;
		}

		std::vector<Corner> convex_hull(std::vector<Corner> points)
		{
			std::ranges::sort(points, [](const Corner a, const Corner b)
				{ return a.x < b.x || (a.x == b.x && a.y < b.y); });
			std::vector<Corner> hull;
			for (const auto p : points)
			{
				while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 0)
					hull.pop_back();
				hull.push_back(p);
			}
			const auto lower = hull.size();
			for (auto i = points.rbegin() + 1; i != points.rend(); ++i)
			{
				while (hull.size() > lower && cross(hull[hull.size() - 2], hull.back(), *i) <= 0)
					hull.pop_back();
				hull.push_back(*i);
			}
			if (!hull.empty()) hull.pop_back();
			return hull;
		}
	}

	bool valid_quadrilateral(const Quadrilateral& corners)
	{
		for (const auto p : corners)
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.y < 0 || p.x > 1 || p.y > 1)
				return false;
		for (size_t i = 0; i < 4; ++i)
			if (cross(corners[i], corners[(i + 1) % 4], corners[(i + 2) % 4]) <= 1e-8)
				return false;
		return true;
	}

	sizei perspective_size(const sizei source, const Quadrilateral& corners)
	{
		if (source.width < 2 || source.height < 2 || !inverse_map(corners)) return {};
		auto q = corners;
		for (auto& p : q) { p.x *= source.width - 1; p.y *= source.height - 1; }
		const double width = (std::max)(distance(q[0], q[1]), distance(q[3], q[2]));
		const double height = (std::max)(distance(q[0], q[3]), distance(q[1], q[2]));
		if (width < 1 || height < 1 || width >= INT_MAX - 1.0 || height >= INT_MAX - 1.0) return {};
		const sizei size{static_cast<int>(std::lround(width)) + 1, static_cast<int>(std::lround(height)) + 1};
		const auto pixels = static_cast<std::uint64_t>(size.width) * size.height;
		const auto sourcePixels = static_cast<std::uint64_t>(source.width) * source.height;
		if (pixels > 128ull * 1024 * 1024 || pixels > sourcePixels * 4) return {};
		return size;
	}

	files::DecodedImage correct_perspective(const files::DecodedImage& source, const Quadrilateral& corners) try
	{
		if (!valid_image(source)) return {};
		const auto matrix = inverse_map(corners);
		const auto size = perspective_size({source.width, source.height}, corners);
		if (!matrix || size.width < 2 || size.height < 2) return {};
		if (corners == whole_picture) return source;
		const auto count = static_cast<std::uint64_t>(size.width) * size.height;
		if (count > source.pixels.max_size()) return {};
		files::DecodedImage result;
		result.width = result.originalWidth = size.width;
		result.height = result.originalHeight = size.height;
		result.pixels.resize(static_cast<size_t>(count));
		const auto& m = *matrix;
		for (int y = 0; y < size.height; ++y)
		{
			const double v = static_cast<double>(y) / (size.height - 1);
			for (int x = 0; x < size.width; ++x)
			{
				const double u = static_cast<double>(x) / (size.width - 1);
				const double denominator = m[6] * u + m[7] * v + 1;
				const double sx = (m[0] * u + m[1] * v + m[2]) / denominator * (source.width - 1);
				const double sy = (m[3] * u + m[4] * v + m[5]) / denominator * (source.height - 1);
				result.pixels[static_cast<size_t>(y) * size.width + x] = sample(source, sx, sy);
			}
		}
		return result;
	}
	catch (const std::bad_alloc&)
	{
		return {};
	}

	std::optional<Quadrilateral> detect_document(const files::DecodedImage& source)
	{
		if (!valid_image(source) || source.width < 32 || source.height < 32) return {};
		const double scale = (std::min)(1.0, 512.0 / (std::max)(source.width, source.height));
		const int width = (std::max)(2, static_cast<int>(source.width * scale));
		const int height = (std::max)(2, static_cast<int>(source.height * scale));
		const int count = width * height;
		std::vector<int> gray(static_cast<size_t>(count), -1);
		std::array<int, 256> histogram{};
		int opaque = 0;
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x)
			{
				// Area averaging retains document edges without aliasing text into connected noise.
				const int x0 = x * source.width / width, x1 = (x + 1) * source.width / width;
				const int y0 = y * source.height / height, y1 = (y + 1) * source.height / height;
				std::uint64_t sum = 0, samples = 0;
				for (int sy = y0; sy < y1; ++sy)
					for (int sx = x0; sx < x1; ++sx)
					{
						const auto pixel = source.pixels[static_cast<size_t>(sy) * source.width + sx];
						if ((pixel >> 24) < 240) continue;
						sum += ((pixel & 255) * 29 + (pixel >> 8 & 255) * 150 + (pixel >> 16 & 255) * 77) >> 8;
						++samples;
					}
				if (samples * 4 < static_cast<std::uint64_t>(x1 - x0) * (y1 - y0) * 3) continue;
				const int value = static_cast<int>(sum / samples);
				gray[static_cast<size_t>(y) * width + x] = value;
				++histogram[static_cast<size_t>(value)];
				++opaque;
			}
		if (opaque < count * 3 / 4) return {};
		double total = 0;
		for (size_t i = 0; i < histogram.size(); ++i) total += static_cast<double>(i) * histogram[i];
		int background = 0, threshold = -1;
		double sum = 0, bestVariance = 0;
		for (int t = 0; t < 255; ++t)
		{
			background += histogram[static_cast<size_t>(t)];
			sum += static_cast<double>(t) * histogram[static_cast<size_t>(t)];
			if (background == 0 || background == opaque) continue;
			const double separation = sum / background - (total - sum) / (opaque - background);
			const double variance = static_cast<double>(background) * (opaque - background) * separation * separation;
			if (std::abs(separation) >= 24 && variance > bestVariance)
			{ bestVariance = variance; threshold = t; }
		}
		if (threshold < 0) return {};

		std::optional<Quadrilateral> result;
		double bestScore = 0;
		for (const bool light : {true, false})
		{
			std::vector<bool> visited(static_cast<size_t>(count));
			const auto foreground = [&](const int i)
				{ return gray[static_cast<size_t>(i)] >= 0 && (gray[static_cast<size_t>(i)] > threshold) == light; };
			for (int seed = 0; seed < count; ++seed)
			{
				if (visited[static_cast<size_t>(seed)] || !foreground(seed)) continue;
				std::vector<int> component{seed};
				std::vector<Corner> boundary;
				visited[static_cast<size_t>(seed)] = true;
				bool touchesBorder = false;
				for (size_t cursor = 0; cursor < component.size(); ++cursor)
				{
					const int i = component[cursor], x = i % width, y = i / width;
					if (x == 0 || y == 0 || x == width - 1 || y == height - 1) touchesBorder = true;
					bool edge = false;
					for (const auto delta : std::array<pointi, 4>{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}})
					{
						const int nx = x + delta.x, ny = y + delta.y;
						if (nx < 0 || ny < 0 || nx >= width || ny >= height) { edge = true; continue; }
						const int neighbor = ny * width + nx;
						if (!foreground(neighbor)) { edge = true; continue; }
						if (!visited[static_cast<size_t>(neighbor)])
						{ visited[static_cast<size_t>(neighbor)] = true; component.push_back(neighbor); }
					}
					if (edge) boundary.push_back({static_cast<double>(x), static_cast<double>(y)});
				}
				if (touchesBorder || component.size() < static_cast<size_t>(count / 10) || boundary.size() < 4)
					continue;
				auto hull = convex_hull(std::move(boundary));
				const double hullArea = polygon_area(hull);
				if (hull.size() < 4 || hullArea < count * 0.12 || hullArea > count * 0.9 ||
					component.size() < hullArea * 0.7) continue;
				// Removing the smallest corner triangles greedily can cut several pixels off
				// a rasterized page corner. That moves a candidate side inside the page and
				// fails the independent edge test. Maximize the inscribed quadrilateral
				// instead: for each diagonal, the farthest vertex on either arc determines
				// the two maximal triangles. Convexity lets both cursors advance monotonically.
				std::vector<Corner> quadrilateral;
				double largestArea = 0;
				for (size_t a = 0; a + 3 < hull.size(); ++a)
				{
					size_t b = a + 1, d = a + 3;
					for (size_t c = a + 2; c + 1 < hull.size(); ++c)
					{
						while (b + 1 < c && cross(hull[a], hull[b + 1], hull[c]) >= cross(hull[a], hull[b], hull[c]))
							++b;
						d = (std::max)(d, c + 1);
						while (d + 1 < hull.size() && cross(hull[a], hull[c], hull[d + 1]) >= cross(hull[a], hull[c], hull[d]))
							++d;
						const double area = cross(hull[a], hull[b], hull[c]) + cross(hull[a], hull[c], hull[d]);
						if (area > largestArea)
						{
							largestArea = area;
							quadrilateral = {hull[a], hull[b], hull[c], hull[d]};
						}
					}
				}
				if (quadrilateral.size() != 4) continue;
				hull = std::move(quadrilateral);
				const double area = polygon_area(hull);
				if (area / hullArea < 0.90) continue;
				double minSide = (std::numeric_limits<double>::max)(), maxSide = 0;
				double support = 0;
				bool supported = true;
				for (size_t edge = 0; edge < 4 && supported; ++edge)
				{
					const auto a = hull[edge], b = hull[(edge + 1) % 4];
					const double length = distance(a, b);
					minSide = (std::min)(minSide, length);
					maxSide = (std::max)(maxSide, length);
					if (length < 12) { supported = false; break; }
					const double nx = -(b.y - a.y) / length, ny = (b.x - a.x) / length;
					int votes = 0;
					for (int step = 2; step <= 18; ++step)
					{
						const double t = step / 20.0;
						const double px = a.x + (b.x - a.x) * t, py = a.y + (b.y - a.y) * t;
						const auto at = [&](const double sign)
						{
							const int x = static_cast<int>(std::lround(px + sign * nx * 3));
							const int y = static_cast<int>(std::lround(py + sign * ny * 3));
							return x < 0 || y < 0 || x >= width || y >= height ? -1 :
								gray[static_cast<size_t>(y) * width + x];
						};
						const int inside = at(1), outside = at(-1);
						if (inside >= 0 && outside >= 0 && (light ? inside - outside : outside - inside) >= 18)
							++votes;
					}
					if (votes < 12) supported = false;
					support += votes / 17.0;
				}
				if (!supported || maxSide > minSide * 5) continue;
				Quadrilateral q;
				const auto first = static_cast<size_t>(std::min_element(hull.begin(), hull.end(),
					[](const Corner a, const Corner b) { return a.x + a.y < b.x + b.y; }) - hull.begin());
				for (size_t i = 0; i < 4; ++i)
				{
					const auto p = hull[(first + i) % 4];
					// Downsampling refers to pixel cells; map their centres back to source centres.
					q[i] = {((p.x + 0.5) * source.width / width - 0.5) / (source.width - 1),
						((p.y + 0.5) * source.height / height - 0.5) / (source.height - 1)};
				}
				const double score = area * support;
				if (valid_quadrilateral(q) && score > bestScore) { bestScore = score; result = q; }
			}
		}
		return result;
	}

	CropHandle crop_handle(const recti crop, const pointi point, const int radius)
	{
		if (crop.width <= 0 || crop.height <= 0 || radius < 0) return CropHandle::none;
		const bool l = std::abs(static_cast<long long>(point.x) - crop.x) <= radius;
		const bool r = std::abs(static_cast<long long>(point.x) - crop.right()) <= radius;
		const bool t = std::abs(static_cast<long long>(point.y) - crop.y) <= radius;
		const bool b = std::abs(static_cast<long long>(point.y) - crop.bottom()) <= radius;
		if (l && t) return CropHandle::topLeft;
		if (r && t) return CropHandle::topRight;
		if (r && b) return CropHandle::bottomRight;
		if (l && b) return CropHandle::bottomLeft;
		if (point.y >= crop.y && point.y <= crop.bottom())
		{ if (l) return CropHandle::left; if (r) return CropHandle::right; }
		if (point.x >= crop.x && point.x <= crop.right())
		{ if (t) return CropHandle::top; if (b) return CropHandle::bottom; }
		return crop.contains(point) ? CropHandle::move : CropHandle::none;
	}

	recti drag_crop(const recti initial, const recti bounds, const CropHandle handle, const pointi delta)
	{
		if (bounds.width <= 0 || bounds.height <= 0 || initial.width <= 0 || initial.height <= 0) return {};
		const int width = (std::min)(initial.width, bounds.width), height = (std::min)(initial.height, bounds.height);
		int l = std::clamp(initial.x, bounds.x, bounds.right() - width), r = l + width;
		int t = std::clamp(initial.y, bounds.y, bounds.bottom() - height), b = t + height;
		const auto shift = [](const int value, const int deltaValue, const int low, const int high)
			{ return static_cast<int>(std::clamp(static_cast<long long>(value) + deltaValue,
				static_cast<long long>(low), static_cast<long long>(high))); };
		if (handle == CropHandle::move)
			return {shift(l, delta.x, bounds.x, bounds.right() - width),
				shift(t, delta.y, bounds.y, bounds.bottom() - height), width, height};
		if (handle == CropHandle::left || handle == CropHandle::topLeft || handle == CropHandle::bottomLeft)
			l = shift(l, delta.x, bounds.x, r - 1);
		if (handle == CropHandle::right || handle == CropHandle::topRight || handle == CropHandle::bottomRight)
			r = shift(r, delta.x, l + 1, bounds.right());
		if (handle == CropHandle::top || handle == CropHandle::topLeft || handle == CropHandle::topRight)
			t = shift(t, delta.y, bounds.y, b - 1);
		if (handle == CropHandle::bottom || handle == CropHandle::bottomLeft || handle == CropHandle::bottomRight)
			b = shift(b, delta.y, t + 1, bounds.bottom());
		return {l, t, r - l, b - t};
	}
}
