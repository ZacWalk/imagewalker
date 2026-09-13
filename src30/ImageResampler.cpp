// ImageWalker by Zac Walker
// Implements exact, area, bilinear, and nearest-neighbour BGRA sampling for visible image regions.

#include "ImageResampler.h"
#include "PixelOps.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace iw::ui
{
	namespace
	{
		bool same_size(const sizei left, const sizei right)
		{
			return left.width == right.width && left.height == right.height;
		}

		bool same_rect(const SampleRect& left, const SampleRect& right)
		{
			return left.x == right.x && left.y == right.y && left.width == right.width &&
				left.height == right.height;
		}

		std::uint32_t pixel_at(const std::span<const std::uint32_t> pixels, const sizei size,
		                       const int x, const int y)
		{
			return pixels[static_cast<size_t>(std::clamp(y, 0, size.height - 1)) * size.width +
				std::clamp(x, 0, size.width - 1)];
		}

		std::uint32_t area_sample(const std::span<const std::uint32_t> pixels, const sizei size,
		                          const double left, const double top, const double right, const double bottom)
		{
			std::array<double, 4> total{};
			double totalWeight = 0.0;
			const int firstX = static_cast<int>(std::floor(left));
			const int lastX = static_cast<int>(std::ceil(right));
			const int firstY = static_cast<int>(std::floor(top));
			const int lastY = static_cast<int>(std::ceil(bottom));
			for (int y = firstY; y < lastY; ++y)
			{
				const double yWeight = (std::max)(0.0, (std::min)(bottom, y + 1.0) - (std::max)(top, static_cast<double>(y)));
				for (int x = firstX; x < lastX; ++x)
				{
					const double xWeight = (std::max)(0.0, (std::min)(right, x + 1.0) -
						(std::max)(left, static_cast<double>(x)));
					const double weight = xWeight * yWeight;
					const std::uint32_t pixel = pixel_at(pixels, size, x, y);
					for (int channel = 0; channel < 4; ++channel)
						total[channel] += ((pixel >> (channel * 8)) & 0xff) * weight;
					totalWeight += weight;
				}
			}
			if (totalWeight <= 0.0) return pixel_at(pixels, size, firstX, firstY);
			std::uint32_t result = 0;
			for (int channel = 0; channel < 4; ++channel)
				result |= static_cast<std::uint32_t>(std::clamp(std::lround(total[channel] / totalWeight), 0l, 255l))
					<< (channel * 8);
			return result;
		}

		std::uint32_t interpolate_pixel(const std::uint32_t first, const std::uint32_t second,
		                                const std::uint16_t fraction)
		{
			const std::uint32_t inverse = 256 - fraction;
			std::uint32_t result{};
			for (int shift = 0; shift < 32; shift += 8)
				result |= ((((first >> shift) & 0xff) * inverse + ((second >> shift) & 0xff) * fraction + 128) >> 8)
					<< shift;
			return result;
		}
	}

	std::span<const std::uint32_t> ResampleCache::resample(const std::span<const std::uint32_t> sourcePixels,
	                                                      const sizei sourceSize,
	                                                      const SampleRect sourceRect,
	                                                      const sizei destinationSize,
	                                                      const std::uint64_t cacheToken)
	{
		++clock_;
		for (auto& entry : entries_)
		{
			if (cacheToken != 0 && entry.valid && entry.cacheToken == cacheToken &&
				entry.source == sourcePixels.data() && entry.sourceCount == sourcePixels.size() &&
				same_size(entry.sourceSize, sourceSize) && same_rect(entry.sourceRect, sourceRect) &&
				same_size(entry.destinationSize, destinationSize))
			{
				entry.lastUse = clock_;
				++hitCount_;
				return entry.workspace.pixels;
			}
		}

		constexpr size_t minimumCachedPixels = 128 * 128;
		const size_t destinationCount = destinationSize.width > 0 && destinationSize.height > 0
			? static_cast<size_t>(destinationSize.width) * destinationSize.height
			: 0;
		if (cacheToken == 0 || destinationCount < minimumCachedPixels)
			return resample_bgra(sourcePixels, sourceSize, sourceRect, destinationSize, transient_);

		auto* target = &entries_.front();
		for (auto& entry : entries_)
			if (!entry.valid || entry.lastUse < target->lastUse) target = &entry;
		target->source = sourcePixels.data();
		target->sourceCount = sourcePixels.size();
		target->sourceSize = sourceSize;
		target->sourceRect = sourceRect;
		target->destinationSize = destinationSize;
		target->cacheToken = cacheToken;
		target->lastUse = clock_;
		target->valid = true;
		return resample_bgra(sourcePixels, sourceSize, sourceRect, destinationSize, target->workspace);
	}

	std::span<const std::uint32_t> resample_bgra(const std::span<const std::uint32_t> sourcePixels,
	                                             const sizei sourceSize, SampleRect sourceRect,
	                                             const sizei destinationSize, ResampleWorkspace& workspace)
	{
		workspace.pixels.clear();
		if (sourceSize.width <= 0 || sourceSize.height <= 0 || destinationSize.width <= 0 ||
			destinationSize.height <= 0)
			return {};
		const size_t sourceCount = static_cast<size_t>(sourceSize.width) * sourceSize.height;
		const size_t destinationCount = static_cast<size_t>(destinationSize.width) * destinationSize.height;
		if (sourcePixels.size() < sourceCount || destinationCount > (std::numeric_limits<size_t>::max)() / 4)
			return {};
		sourceRect.x = std::clamp(sourceRect.x, 0.0, static_cast<double>(sourceSize.width));
		sourceRect.y = std::clamp(sourceRect.y, 0.0, static_cast<double>(sourceSize.height));
		sourceRect.width = std::clamp(sourceRect.width, 0.0, sourceSize.width - sourceRect.x);
		sourceRect.height = std::clamp(sourceRect.height, 0.0, sourceSize.height - sourceRect.y);
		if (sourceRect.width <= 0.0 || sourceRect.height <= 0.0) return {};
		workspace.pixels.resize(destinationCount);
		auto& result = workspace.pixels;
		const double stepX = sourceRect.width / destinationSize.width;
		const double stepY = sourceRect.height / destinationSize.height;
		const bool exact = std::abs(stepX - 1.0) < 1e-9 && std::abs(stepY - 1.0) < 1e-9 &&
			std::abs(sourceRect.x - std::round(sourceRect.x)) < 1e-9 &&
			std::abs(sourceRect.y - std::round(sourceRect.y)) < 1e-9;
		const bool nearest = 1.0 / stepX >= 3.0 && 1.0 / stepY >= 3.0;
		const bool downscale = stepX > 1.0 || stepY > 1.0;
		if (exact)
		{
			const int sourceX = static_cast<int>(sourceRect.x);
			const int sourceY = static_cast<int>(sourceRect.y);
			for (int y = 0; y < destinationSize.height; ++y)
				std::memcpy(result.data() + static_cast<size_t>(y) * destinationSize.width,
				            sourcePixels.data() + static_cast<size_t>(sourceY + y) * sourceSize.width + sourceX,
				            static_cast<size_t>(destinationSize.width) * sizeof(std::uint32_t));
			return result;
		}

		workspace.sourceX.resize(static_cast<size_t>(destinationSize.width));
		if (nearest)
		{
			for (int x = 0; x < destinationSize.width; ++x)
				workspace.sourceX[x] = std::clamp(static_cast<int>(sourceRect.x + (x + 0.5) * stepX),
				                                      0, sourceSize.width - 1);
			for (int y = 0; y < destinationSize.height; ++y)
			{
				const int sourceY = std::clamp(static_cast<int>(sourceRect.y + (y + 0.5) * stepY),
				                               0, sourceSize.height - 1);
				const auto* sourceRow = sourcePixels.data() + static_cast<size_t>(sourceY) * sourceSize.width;
				auto* destinationRow = result.data() + static_cast<size_t>(y) * destinationSize.width;
				for (int x = 0; x < destinationSize.width; ++x)
					destinationRow[x] = sourceRow[workspace.sourceX[x]];
			}
			return result;
		}

		if (!downscale)
		{
			workspace.sourceXRight.resize(static_cast<size_t>(destinationSize.width));
			workspace.fractionX.resize(static_cast<size_t>(destinationSize.width));
			for (int x = 0; x < destinationSize.width; ++x)
			{
				const double position = sourceRect.x + (x + 0.5) * stepX - 0.5;
				const int left = static_cast<int>(std::floor(position));
				workspace.sourceX[x] = std::clamp(left, 0, sourceSize.width - 1);
				workspace.sourceXRight[x] = std::clamp(left + 1, 0, sourceSize.width - 1);
				workspace.fractionX[x] = static_cast<std::uint16_t>(
					std::clamp(std::lround((position - left) * 256.0), 0l, 256l));
			}
			for (auto& row : workspace.horizontalRows)
				row.resize(static_cast<size_t>(destinationSize.width));
			workspace.horizontalSourceY = {-1, -1};
			const auto horizontalRow = [&](const int sourceY) -> std::span<const std::uint32_t>
			{
				const size_t slot = static_cast<size_t>(sourceY & 1);
				auto& row = workspace.horizontalRows[slot];
				if (workspace.horizontalSourceY[slot] != sourceY)
				{
					const auto* sourceRow = sourcePixels.data() + static_cast<size_t>(sourceY) * sourceSize.width;
					for (int x = 0; x < destinationSize.width; ++x)
						row[x] = interpolate_pixel(sourceRow[workspace.sourceX[x]],
						                           sourceRow[workspace.sourceXRight[x]], workspace.fractionX[x]);
					workspace.horizontalSourceY[slot] = sourceY;
				}
				return row;
			};
			for (int y = 0; y < destinationSize.height; ++y)
			{
				const double positionY = sourceRect.y + (y + 0.5) * stepY - 0.5;
				const int rawTop = static_cast<int>(std::floor(positionY));
				const int top = std::clamp(rawTop, 0, sourceSize.height - 1);
				const int bottom = std::clamp(rawTop + 1, 0, sourceSize.height - 1);
				const auto fractionY = static_cast<std::uint16_t>(
					std::clamp(std::lround((positionY - rawTop) * 256.0), 0l, 256l));
				interpolate_bgra(horizontalRow(top), horizontalRow(bottom),
				                 std::span(result).subspan(static_cast<size_t>(y) * destinationSize.width,
				                                             destinationSize.width), fractionY);
			}
			return result;
		}

		for (int y = 0; y < destinationSize.height; ++y)
			for (int x = 0; x < destinationSize.width; ++x)
				result[static_cast<size_t>(y) * destinationSize.width + x] = area_sample(
					sourcePixels, sourceSize, sourceRect.x + x * stepX, sourceRect.y + y * stepY,
					sourceRect.x + (x + 1) * stepX, sourceRect.y + (y + 1) * stepY);
		return result;
	}

	std::vector<std::uint32_t> resample_bgra(const std::span<const std::uint32_t> sourcePixels,
	                                         const sizei sourceSize, const SampleRect sourceRect,
	                                         const sizei destinationSize)
	{
		ResampleWorkspace workspace;
		resample_bgra(sourcePixels, sourceSize, sourceRect, destinationSize, workspace);
		return std::move(workspace.pixels);
	}
}