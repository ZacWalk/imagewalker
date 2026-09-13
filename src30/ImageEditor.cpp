// ImageWalker by Zac Walker
// Implements the rotate, straighten, crop, and colour passes behind the photo edit stack.

#include "ImageEdits.h"
#include "ImageResampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace iw::edits
{
	void EditHistory::record(const ImageEdits& before, const ImageEdits& after, const int group)
	{
		if (before == after) return;
		if (group == 0 || group != group_ || undo_.empty())
		{
			if (undo_.size() == 100) undo_.erase(undo_.begin());
			undo_.push_back(before);
		}
		redo_.clear();
		group_ = group;
	}

	bool EditHistory::undo(ImageEdits& current)
	{
		if (undo_.empty()) return false;
		redo_.push_back(current);
		current = undo_.back();
		undo_.pop_back();
		group_ = 0;
		return true;
	}

	bool EditHistory::redo(ImageEdits& current)
	{
		if (redo_.empty()) return false;
		undo_.push_back(current);
		current = redo_.back();
		redo_.pop_back();
		group_ = 0;
		return true;
	}

	namespace
	{
		constexpr double pi = 3.14159265358979323846;

		double straighten_radians(const ImageEdits& value)
		{
			return std::clamp(value.straighten, -100, 100) * 0.1 * pi / 180.0;
		}

		bool valid_image(const files::DecodedImage& source)
		{
			return source.width > 0 && source.height > 0 &&
				static_cast<std::uint64_t>(source.width) * source.height <= source.pixels.size();
		}

		std::uint32_t pack(const int b, const int g, const int r, const std::uint32_t alpha)
		{
			return alpha | static_cast<std::uint32_t>(std::clamp(r, 0, 255)) << 16 |
				static_cast<std::uint32_t>(std::clamp(g, 0, 255)) << 8 |
				static_cast<std::uint32_t>(std::clamp(b, 0, 255));
		}

		files::DecodedImage rotate_quarters(const files::DecodedImage& source, const int quarters)
		{
			if (quarters == 0) return source;
			files::DecodedImage result;
			const bool swaps = quarters == 1 || quarters == 3;
			result.width = swaps ? source.height : source.width;
			result.height = swaps ? source.width : source.height;
			result.originalWidth = swaps ? source.originalHeight : source.originalWidth;
			result.originalHeight = swaps ? source.originalWidth : source.originalHeight;
			result.pixels.resize(static_cast<size_t>(result.width) * result.height);
			for (int y = 0; y < source.height; ++y)
			{
				const auto* row = source.pixels.data() + static_cast<size_t>(y) * source.width;
				for (int x = 0; x < source.width; ++x)
				{
					int targetX = x, targetY = y;
					switch (quarters)
					{
					case 1: targetX = source.height - 1 - y;
						targetY = x;
						break;
					case 2: targetX = source.width - 1 - x;
						targetY = source.height - 1 - y;
						break;
					default: targetX = y;
						targetY = source.width - 1 - x;
						break;
					}
					result.pixels[static_cast<size_t>(targetY) * result.width + targetX] = row[x];
				}
			}
			return result;
		}

		// Inverse bilinear warp inside the frame it was given. A tap that falls off the picture
		// contributes nothing, which leaves a ramp at the edge instead of a smeared fringe.
		files::DecodedImage straighten_image(const files::DecodedImage& source, const double radians)
		{
			files::DecodedImage result;
			result.width = source.width;
			result.height = source.height;
			result.originalWidth = source.originalWidth;
			result.originalHeight = source.originalHeight;
			result.pixels.assign(static_cast<size_t>(source.width) * source.height, 0u);

			const double centreX = (source.width - 1) * 0.5;
			const double centreY = (source.height - 1) * 0.5;
			const double cosine = std::cos(radians);
			const double sine = std::sin(radians);

			for (int y = 0; y < source.height; ++y)
			{
				const double offsetY = y - centreY;
				auto* target = result.pixels.data() + static_cast<size_t>(y) * result.width;
				for (int x = 0; x < source.width; ++x)
				{
					const double offsetX = x - centreX;
					const double sourceX = centreX + offsetX * cosine + offsetY * sine;
					const double sourceY = centreY - offsetX * sine + offsetY * cosine;
					const int left = static_cast<int>(std::floor(sourceX));
					const int top = static_cast<int>(std::floor(sourceY));
					if (left < -1 || top < -1 || left >= source.width || top >= source.height) continue;
					const double fractionX = sourceX - left;
					const double fractionY = sourceY - top;

					double weights[4]{
						(1 - fractionX) * (1 - fractionY), fractionX * (1 - fractionY),
						(1 - fractionX) * fractionY, fractionX * fractionY
					};
					const int columns[4]{left, left + 1, left, left + 1};
					const int rows[4]{top, top, top + 1, top + 1};
					double blue = 0, green = 0, red = 0, alpha = 0;
					for (int tap = 0; tap < 4; ++tap)
					{
						if (columns[tap] < 0 || rows[tap] < 0 || columns[tap] >= source.width ||
							rows[tap] >= source.height)
							continue;
						const std::uint32_t pixel = source.pixels[
							static_cast<size_t>(rows[tap]) * source.width + columns[tap]];
						const double weight = weights[tap] * (pixel >> 24);
						blue += (pixel & 0xff) * weight;
						green += (pixel >> 8 & 0xff) * weight;
						red += (pixel >> 16 & 0xff) * weight;
						alpha += weight;
					}
					if (alpha <= 0.0) continue;
					target[x] = pack(static_cast<int>(blue / alpha + 0.5),
					                 static_cast<int>(green / alpha + 0.5),
					                 static_cast<int>(red / alpha + 0.5),
					                 static_cast<std::uint32_t>(std::clamp(
						                 static_cast<int>(alpha + 0.5), 0, 255)) << 24);
				}
			}
			return result;
		}

	}

	bool PreviewSource::update(const files::DecodedImage& source, const sizei pane)
		{
			if (!valid_image(source) || pane.width <= 0 || pane.height <= 0)
			{
				reset();
				return false;
			}
			if (!image_.pixels.empty() && pane_ == pane) return false;
			const double scale = (std::min)(1.0, (std::min)(
				static_cast<double>(pane.width) / source.width,
				static_cast<double>(pane.height) / source.height));
			files::DecodedImage image;
			if (scale >= 1.0) image = source;
			else
			{
				image.width = (std::max)(1, static_cast<int>(source.width * scale));
				image.height = (std::max)(1, static_cast<int>(source.height * scale));
				image.originalWidth = source.originalWidth;
				image.originalHeight = source.originalHeight;
				const bool hasAlpha = std::ranges::any_of(source.pixels,
					[](const auto pixel) { return (pixel >> 24) != 255; });
				std::vector<std::uint32_t> premultiplied;
				if (hasAlpha)
				{
					premultiplied = source.pixels;
					for (auto& pixel : premultiplied)
					{
						const auto alpha = pixel >> 24;
						pixel = pack(((pixel & 0xff) * alpha + 127) / 255,
							((pixel >> 8 & 0xff) * alpha + 127) / 255,
							((pixel >> 16 & 0xff) * alpha + 127) / 255, alpha << 24);
					}
				}
				image.pixels = ui::resample_bgra(hasAlpha ? premultiplied : source.pixels, {source.width, source.height},
					{0.0, 0.0, static_cast<double>(source.width), static_cast<double>(source.height)},
					{image.width, image.height});
				if (image.pixels.empty()) return false;
				if (hasAlpha)
					for (auto& pixel : image.pixels)
					{
						const auto alpha = pixel >> 24;
						pixel = alpha == 0 ? 0u : pack(((pixel & 0xff) * 255 + alpha / 2) / alpha,
							((pixel >> 8 & 0xff) * 255 + alpha / 2) / alpha,
							((pixel >> 16 & 0xff) * 255 + alpha / 2) / alpha, alpha << 24);
					}
			}
			image_ = std::move(image);
			pane_ = pane;
			return true;
		}

		ImageEdits scaled_edits(const ImageEdits& value, const sizei source, const sizei preview)
		{
			auto result = value;
			if (!value.has_crop()) return result;
			const auto original = transformed_size(source, value);
			const auto scaled = transformed_size(preview, value);
			if (original.width <= 0 || original.height <= 0 || scaled.width <= 0 || scaled.height <= 0)
			{
				result.crop = {};
				return result;
			}
			const auto crop = effective_crop(source, value);
			const auto scaleX = static_cast<double>(scaled.width) / original.width;
			const auto scaleY = static_cast<double>(scaled.height) / original.height;
			const int left = static_cast<int>(crop.x * scaleX);
			const int top = static_cast<int>(crop.y * scaleY);
			result.crop = {left, top,
				(std::max)(1, static_cast<int>(crop.right() * scaleX) - left),
				(std::max)(1, static_cast<int>(crop.bottom() * scaleY) - top)};
			return result;
		}

	namespace
	{
		files::DecodedImage crop_image(const files::DecodedImage& source, const recti bounds)
		{
			if (bounds.x == 0 && bounds.y == 0 && bounds.width == source.width &&
				bounds.height == source.height)
				return source;
			files::DecodedImage result;
			result.width = bounds.width;
			result.height = bounds.height;
			result.originalWidth = result.width;
			result.originalHeight = result.height;
			result.pixels.resize(static_cast<size_t>(result.width) * result.height);
			for (int y = 0; y < result.height; ++y)
			{
				const auto* row = source.pixels.data() +
					static_cast<size_t>(bounds.y + y) * source.width + bounds.x;
				std::copy_n(row, static_cast<size_t>(result.width),
				            result.pixels.data() + static_cast<size_t>(y) * result.width);
			}
			return result;
		}

		// One triangular bump per tone zone, so a zone control leaves the other two alone.
		double zone_weight(const double tone, const double centre)
		{
			const double distance = std::abs(tone - centre);
			return distance >= 0.5 ? 0.0 : 1.0 - distance * 2.0;
		}

		std::array<std::array<int, 256>, 3> build_tone_tables(const ImageEdits& value)
		{
			std::array<std::array<int, 256>, 3> tables{};
			const double gain = 1.0 + value.contrast / 100.0;
			const double offset = value.brightness / 200.0;
			// Temperature warms by lifting red and dropping blue; tint trades green against both.
			const double warm = value.temperature / 400.0;
			const double green = value.tint / 400.0;
			const std::array channelOffset{-warm, green, warm};
			for (int channel = 0; channel < 3; ++channel)
			{
				for (int index = 0; index < 256; ++index)
				{
					double tone = index / 255.0;
					tone = 0.5 + (tone - 0.5) * gain + offset + channelOffset[static_cast<size_t>(channel)];
					tone += value.darks / 200.0 * zone_weight(tone, 0.0);
					tone += value.midtones / 200.0 * zone_weight(tone, 0.5);
					tone += value.lights / 200.0 * zone_weight(tone, 1.0);
					tables[static_cast<size_t>(channel)][static_cast<size_t>(index)] =
						std::clamp(static_cast<int>(tone * 255.0 + 0.5), 0, 255);
				}
			}
			return tables;
		}
	}

	sizei transformed_size(const sizei source, const ImageEdits& value)
	{
		if (source.width <= 0 || source.height <= 0) return {};
		const auto frame = value.perspective ? perspective_size(source, *value.perspective) : source;
		const int quarters = ((value.rotation % 4) + 4) % 4;
		return quarters == 1 || quarters == 3 ? sizei{frame.height, frame.width} : frame;
	}

	recti crop_bounds(const sizei source, const ImageEdits& value)
	{
		const auto frame = transformed_size(source, value);
		if (frame.width <= 0 || frame.height <= 0) return {};
		const double radians = std::abs(straighten_radians(value));
		const double sine = std::abs(std::sin(radians));
		const double cosine = std::abs(std::cos(radians));
		if (sine < 1e-9) return {0, 0, frame.width, frame.height};

		// Bilinear sampling is opaque only between the outermost pixel centres. Using the
		// outer pixel edges here admits partly transparent corner pixels after straightening.
		const double width = frame.width - 1;
		const double height = frame.height - 1;
		const double longSide = (std::max)(width, height);
		const double shortSide = (std::min)(width, height);
		double fittedWidth = 0, fittedHeight = 0;
		if (shortSide <= 2.0 * sine * cosine * longSide || std::abs(sine - cosine) < 1e-9)
		{
			const double half = 0.5 * shortSide;
			if (width >= height)
			{
				fittedWidth = half / sine;
				fittedHeight = half / cosine;
			}
			else
			{
				fittedWidth = half / cosine;
				fittedHeight = half / sine;
			}
		}
		else
		{
			const double cosDouble = cosine * cosine - sine * sine;
			fittedWidth = (width * cosine - height * sine) / cosDouble;
			fittedHeight = (height * cosine - width * sine) / cosDouble;
		}

		const int left = static_cast<int>(std::ceil((width - fittedWidth) * 0.5 - 1e-9));
		const int top = static_cast<int>(std::ceil((height - fittedHeight) * 0.5 - 1e-9));
		const int right = static_cast<int>(std::floor((width + fittedWidth) * 0.5 + 1e-9));
		const int bottom = static_cast<int>(std::floor((height + fittedHeight) * 0.5 + 1e-9));
		return {left, top, (std::max)(0, right - left + 1), (std::max)(0, bottom - top + 1)};
	}

	recti effective_crop(const sizei source, const ImageEdits& value)
	{
		const auto bounds = crop_bounds(source, value);
		if (!value.has_crop() || bounds.width <= 0) return bounds;
		const int width = (std::min)(value.crop.width, bounds.width);
		const int height = (std::min)(value.crop.height, bounds.height);
		return {
			std::clamp(value.crop.x, bounds.x, bounds.right() - width),
			std::clamp(value.crop.y, bounds.y, bounds.bottom() - height), width, height
		};
	}

	void apply_color(std::vector<std::uint32_t>& pixels, const ImageEdits& value)
	{
		if (!value.has_color()) return;
		const auto tables = build_tone_tables(value);
		const double saturation = 1.0 + value.saturation / 100.0;
		const double vibrance = value.vibrance / 100.0;
		for (auto& pixel : pixels)
		{
			const std::uint32_t alpha = pixel & 0xff000000;
			int blue = tables[0][pixel & 0xff];
			int green = tables[1][pixel >> 8 & 0xff];
			int red = tables[2][pixel >> 16 & 0xff];
			if (value.saturation || value.vibrance)
			{
				const double gray = 0.114 * blue + 0.587 * green + 0.299 * red;
				const int highest = (std::max)({blue, green, red});
				const int lowest = (std::min)({blue, green, red});
				// Vibrance lifts the least saturated pixels hardest and leaves saturated ones alone.
				const double current = highest > 0
					                       ? static_cast<double>(highest - lowest) / highest
					                       : 0.0;
				const double scale = saturation + vibrance * (1.0 - current);
				blue = static_cast<int>(gray + (blue - gray) * scale + 0.5);
				green = static_cast<int>(gray + (green - gray) * scale + 0.5);
				red = static_cast<int>(gray + (red - gray) * scale + 0.5);
			}
			pixel = pack(blue, green, red, alpha);
		}
	}

	files::DecodedImage apply(const files::DecodedImage& source, const ImageEdits& value,
	                          const bool applyCrop)
	{
		if (!valid_image(source)) return {};
		if (value.empty()) return source;
		auto result = value.perspective ? correct_perspective(source, *value.perspective) : source;
		if (!valid_image(result)) return {};
		result = rotate_quarters(result, ((value.rotation % 4) + 4) % 4);
		if (value.has_warp()) result = straighten_image(result, straighten_radians(value));
		if (applyCrop)
		{
			const auto bounds = effective_crop({source.width, source.height}, value);
			if (bounds.width <= 0 || bounds.height <= 0) return {};
			result = crop_image(result, bounds);
		}
		apply_color(result.pixels, value);
		return result;
	}

	namespace
	{
		constexpr int histogramLevels = 256;

		int histogram_percentile(const std::array<std::uint64_t, histogramLevels>& counts,
		                         const std::uint64_t total,
		                         const double fraction)
		{
			const auto target = (std::max<std::uint64_t>)(1,
				static_cast<std::uint64_t>(std::ceil(total * fraction)));
			std::uint64_t running = 0;
			for (int level = 0; level < histogramLevels; ++level)
			{
				running += counts[static_cast<size_t>(level)];
				if (running >= target) return level;
			}
			return 255;
		}

		// One strong edge pixel, and how strong.
		struct EdgePoint
		{
			float x;
			float y;
			float weight;
		};

		// How sharply the points fall into bands at this angle. Points on the same straight line
		// share a band once the angle matches the line, so the sum of squares peaks at the angle
		// that undoes the tilt. `horizontal` picks the family of lines being tested: bands stacked
		// up the picture, or across it.
		double band_sharpness(const std::vector<EdgePoint>& points, const double cosine,
		                      const double sine, const bool horizontal, std::vector<double>& bins,
		                      const int offset)
		{
			if (points.empty()) return 0.0;
			std::ranges::fill(bins, 0.0);
			const auto count = static_cast<int>(bins.size());
			for (const auto& point : points)
			{
				const double distance = horizontal
					                        ? point.y * cosine - point.x * sine
					                        : point.x * cosine + point.y * sine;
				const int bin = static_cast<int>(distance + 0.5) + offset;
				if (bin >= 0 && bin < count) bins[static_cast<size_t>(bin)] += point.weight;
			}
			double score = 0;
			for (const double weight : bins) score += weight * weight;
			return score;
		}
	}

	bool auto_color(const files::DecodedImage& source, ImageEdits& value)
	{
		if (!valid_image(source)) return false;
		std::array<std::uint64_t, histogramLevels> blues{}, luminance{}, reds{};
		std::uint64_t total = 0;
		for (size_t index = 0; index < static_cast<size_t>(source.width) * source.height; ++index)
		{
			const auto pixel = source.pixels[index];
			const auto alpha = pixel >> 24;
			const auto tone = ((pixel >> 16 & 0xff) * 77 + (pixel >> 8 & 0xff) * 150 +
				(pixel & 0xff) * 29) >> 8;
			blues[pixel & 0xff] += alpha;
			luminance[tone] += alpha;
			reds[pixel >> 16 & 0xff] += alpha;
			total += alpha;
		}
		if (total == 0) return false;

		// Stretch the middle 98% of the luminance range to fill the scale, which is a contrast
		// gain plus a brightness offset in this model.
		const int low = histogram_percentile(luminance, total, 0.01);
		const int high = histogram_percentile(luminance, total, 0.99);
		value.contrast = 0;
		value.brightness = 0;
		if (high > low)
		{
			const double gain = 255.0 / (high - low);
			const double middle = (low + high) / 2.0;
			value.contrast = std::clamp(static_cast<int>((gain - 1.0) * 100.0), -100, 100);
			// The curve stretches about mid grey and adds brightness after it, so the offset that
			// carries the midpoint up to mid grey is scaled by the gain that is actually applied --
			// the clamped one, not the one asked for. Without it an underexposed picture came back
			// underexposed by exactly the gain.
			const double applied = 1.0 + value.contrast / 100.0;
			value.brightness = std::clamp(
				static_cast<int>(applied * (127.5 - middle) / 127.5 * 100.0), -100, 100);
		}

		// Grey-world white balance across the red and blue means.
		double meanBlue = 0, meanRed = 0;
		for (int level = 0; level < histogramLevels; ++level)
		{
			meanBlue += static_cast<double>(level) * blues[static_cast<size_t>(level)];
			meanRed += static_cast<double>(level) * reds[static_cast<size_t>(level)];
		}
		meanBlue /= total;
		meanRed /= total;
		// apply_color moves red by +temperature/400 and blue by -temperature/400 of full scale, so
		// closing a cast of d counts needs temperature = d * 400 / 510.
		value.temperature = std::clamp(static_cast<int>((meanBlue - meanRed) * 400.0 / 510.0), -100, 100);
		return true;
	}

	bool auto_straighten(const files::DecodedImage& source, ImageEdits& value)
	{
		if (!valid_image(source) || source.width < 8 || source.height < 8) return false;

		// A gradient direction taken from a 3x3 neighbourhood cannot resolve a fraction of a
		// degree: on a quantised edge tilted by 1.5 degrees the staircase reads as exactly
		// horizontal for 38 pixels at a time, so a histogram of Sobel angles puts every vote in the
		// zero bucket. What does resolve it is the whole line.
		const int step = (std::max)(1, (std::max)(source.width, source.height) / 384);
		const int width = source.width / step;
		const int height = source.height / step;
		if (width < 3 || height < 3) return false;

		std::vector<int> luma(static_cast<size_t>(width) * height);
		for (int y = 0; y < height; ++y)
		{
			const auto* row = source.pixels.data() + static_cast<size_t>(y) * step * source.width;
			for (int x = 0; x < width; ++x)
			{
				const std::uint32_t pixel = row[static_cast<size_t>(x) * step];
				luma[static_cast<size_t>(y) * width + x] =
					(pixel >> 24) == 0 ? 0 :
					(static_cast<int>(pixel >> 16 & 0xff) * 77 + static_cast<int>(pixel >> 8 & 0xff) * 151 +
						static_cast<int>(pixel & 0xff) * 28) >> 8;
			}
		}

		// Edges running across the picture and edges running up it are two separate votes on the
		// same tilt, so they are scored against their own family of lines.
		std::vector<EdgePoint> across, upright;
		for (int y = 1; y < height - 1; ++y)
		{
			for (int x = 1; x < width - 1; ++x)
			{
				const int* point = luma.data() + static_cast<size_t>(y) * width + x;
				const int gradientX = point[-width + 1] + 2 * point[1] + point[width + 1] -
					point[-width - 1] - 2 * point[-1] - point[width - 1];
				const int gradientY = point[width - 1] + 2 * point[width] + point[width + 1] -
					point[-width - 1] - 2 * point[-width] - point[-width + 1];
				const int magnitude = std::abs(gradientX) + std::abs(gradientY);
				if (magnitude < 120) continue;
				const EdgePoint edge{
					static_cast<float>(x), static_cast<float>(y), static_cast<float>(magnitude)
				};
				// The gradient is perpendicular to the edge, so a mostly vertical gradient belongs
				// to a line running across the picture.
				if (std::abs(gradientY) > std::abs(gradientX)) across.push_back(edge);
				else upright.push_back(edge);
			}
		}
		if (across.size() + upright.size() < 64) return false;

		const int offset = width + height;
		std::vector<double> bins(static_cast<size_t>(offset) * 2 + 2, 0.0);
		int best = 0;
		double bestScore = 0;
		// Plus or minus five degrees in tenths. Anything larger is a deliberate rotation, which is
		// what the two quarter-turn buttons are for.
		for (int tenth = -50; tenth <= 50; ++tenth)
		{
			const double angle = tenth / 10.0 * pi / 180.0;
			const double cosine = std::cos(angle);
			const double sine = std::sin(angle);
			const double score = band_sharpness(across, cosine, sine, true, bins, offset) +
				band_sharpness(upright, cosine, sine, false, bins, offset);
			if (score <= bestScore) continue;
			bestScore = score;
			best = tenth;
		}
		value.straighten = -best;
		return true;
	}
}
