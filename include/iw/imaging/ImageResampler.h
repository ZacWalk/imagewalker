// ImageWalker by Zac Walker
// Declares the bounded BGRA image resampler used by the CPU rendering tier.

#pragma once

// Preserve historical min/max macros around inline modern code.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include "Geometry.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace iw::imaging
{
	using ::core::sizei;

	struct SampleRect
	{
		double x{};
		double y{};
		double width{};
		double height{};
	};

	struct ResampleWorkspace
	{
		std::vector<std::uint32_t> pixels;
		std::vector<int> sourceX;
		std::vector<int> sourceXRight;
		std::vector<std::uint16_t> fractionX;
		std::array<std::vector<std::uint32_t>, 2> horizontalRows;
		std::array<int, 2> horizontalSourceY{-1, -1};
	};

	std::vector<std::uint32_t> resample_bgra(std::span<const std::uint32_t> sourcePixels,
	                                         sizei sourceSize, SampleRect sourceRect,
	                                         sizei destinationSize);
	std::span<const std::uint32_t> resample_bgra(std::span<const std::uint32_t> sourcePixels,
	                                             sizei sourceSize, SampleRect sourceRect,
	                                             sizei destinationSize, ResampleWorkspace& workspace);
}

// ImageWalker by Zac Walker
// Implements exact, area, bilinear, and nearest-neighbour BGRA sampling for visible image regions.

#include <iw/imaging/PixelOps.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace iw::imaging
{
	namespace resampler_detail
	{
		inline std::uint32_t pixel_at(const std::span<const std::uint32_t> pixels, const sizei size,
		                       const int x, const int y)
		{
			return pixels[static_cast<size_t>(std::clamp(y, 0, size.cy - 1)) * size.cx +
				std::clamp(x, 0, size.cx - 1)];
		}

		inline std::uint32_t area_sample(const std::span<const std::uint32_t> pixels, const sizei size,
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

		inline std::uint32_t interpolate_pixel(const std::uint32_t first, const std::uint32_t second,
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

	inline std::span<const std::uint32_t> resample_bgra(const std::span<const std::uint32_t> sourcePixels,
	                                             const sizei sourceSize, SampleRect sourceRect,
	                                             const sizei destinationSize, ResampleWorkspace& workspace)
	{
		using namespace resampler_detail;
		workspace.pixels.clear();
		if (sourceSize.cx <= 0 || sourceSize.cy <= 0 || destinationSize.cx <= 0 ||
			destinationSize.cy <= 0)
			return {};
		const size_t sourceCount = static_cast<size_t>(sourceSize.cx) * sourceSize.cy;
		const size_t destinationCount = static_cast<size_t>(destinationSize.cx) * destinationSize.cy;
		if (sourcePixels.size() < sourceCount || destinationCount > (std::numeric_limits<size_t>::max)() / 4)
			return {};
		sourceRect.x = std::clamp(sourceRect.x, 0.0, static_cast<double>(sourceSize.cx));
		sourceRect.y = std::clamp(sourceRect.y, 0.0, static_cast<double>(sourceSize.cy));
		sourceRect.width = std::clamp(sourceRect.width, 0.0, sourceSize.cx - sourceRect.x);
		sourceRect.height = std::clamp(sourceRect.height, 0.0, sourceSize.cy - sourceRect.y);
		if (sourceRect.width <= 0.0 || sourceRect.height <= 0.0) return {};
		workspace.pixels.resize(destinationCount);
		auto& result = workspace.pixels;
		const double stepX = sourceRect.width / destinationSize.cx;
		const double stepY = sourceRect.height / destinationSize.cy;
		const bool exact = std::abs(stepX - 1.0) < 1e-9 && std::abs(stepY - 1.0) < 1e-9 &&
			std::abs(sourceRect.x - std::round(sourceRect.x)) < 1e-9 &&
			std::abs(sourceRect.y - std::round(sourceRect.y)) < 1e-9;
		const bool nearest = 1.0 / stepX >= 3.0 && 1.0 / stepY >= 3.0;
		const bool downscale = stepX > 1.0 || stepY > 1.0;
		if (exact)
		{
			const int sourceX = static_cast<int>(sourceRect.x);
			const int sourceY = static_cast<int>(sourceRect.y);
			for (int y = 0; y < destinationSize.cy; ++y)
				std::memcpy(result.data() + static_cast<size_t>(y) * destinationSize.cx,
				            sourcePixels.data() + static_cast<size_t>(sourceY + y) * sourceSize.cx + sourceX,
				            static_cast<size_t>(destinationSize.cx) * sizeof(std::uint32_t));
			return result;
		}

		workspace.sourceX.resize(static_cast<size_t>(destinationSize.cx));
		if (nearest)
		{
			for (int x = 0; x < destinationSize.cx; ++x)
				workspace.sourceX[x] = std::clamp(static_cast<int>(sourceRect.x + (x + 0.5) * stepX),
				                                      0, sourceSize.cx - 1);
			for (int y = 0; y < destinationSize.cy; ++y)
			{
				const int sourceY = std::clamp(static_cast<int>(sourceRect.y + (y + 0.5) * stepY),
				                               0, sourceSize.cy - 1);
				const auto* sourceRow = sourcePixels.data() + static_cast<size_t>(sourceY) * sourceSize.cx;
				auto* destinationRow = result.data() + static_cast<size_t>(y) * destinationSize.cx;
				for (int x = 0; x < destinationSize.cx; ++x)
					destinationRow[x] = sourceRow[workspace.sourceX[x]];
			}
			return result;
		}

		if (!downscale)
		{
			workspace.sourceXRight.resize(static_cast<size_t>(destinationSize.cx));
			workspace.fractionX.resize(static_cast<size_t>(destinationSize.cx));
			for (int x = 0; x < destinationSize.cx; ++x)
			{
				const double position = sourceRect.x + (x + 0.5) * stepX - 0.5;
				const int left = static_cast<int>(std::floor(position));
				workspace.sourceX[x] = std::clamp(left, 0, sourceSize.cx - 1);
				workspace.sourceXRight[x] = std::clamp(left + 1, 0, sourceSize.cx - 1);
				workspace.fractionX[x] = static_cast<std::uint16_t>(
					std::clamp(std::lround((position - left) * 256.0), 0l, 256l));
			}
			for (auto& row : workspace.horizontalRows)
				row.resize(static_cast<size_t>(destinationSize.cx));
			workspace.horizontalSourceY = {-1, -1};
			const auto horizontalRow = [&](const int sourceY) -> std::span<const std::uint32_t>
			{
				const size_t slot = static_cast<size_t>(sourceY & 1);
				auto& row = workspace.horizontalRows[slot];
				if (workspace.horizontalSourceY[slot] != sourceY)
				{
					const auto* sourceRow = sourcePixels.data() + static_cast<size_t>(sourceY) * sourceSize.cx;
					for (int x = 0; x < destinationSize.cx; ++x)
						row[x] = interpolate_pixel(sourceRow[workspace.sourceX[x]],
						                           sourceRow[workspace.sourceXRight[x]], workspace.fractionX[x]);
					workspace.horizontalSourceY[slot] = sourceY;
				}
				return row;
			};
			for (int y = 0; y < destinationSize.cy; ++y)
			{
				const double positionY = sourceRect.y + (y + 0.5) * stepY - 0.5;
				const int rawTop = static_cast<int>(std::floor(positionY));
				const int top = std::clamp(rawTop, 0, sourceSize.cy - 1);
				const int bottom = std::clamp(rawTop + 1, 0, sourceSize.cy - 1);
				const auto fractionY = static_cast<std::uint16_t>(
					std::clamp(std::lround((positionY - rawTop) * 256.0), 0l, 256l));
				interpolate_bgra(horizontalRow(top), horizontalRow(bottom),
				                 std::span(result).subspan(static_cast<size_t>(y) * destinationSize.cx,
				                                             destinationSize.cx), fractionY);
			}
			return result;
		}

		for (int y = 0; y < destinationSize.cy; ++y)
			for (int x = 0; x < destinationSize.cx; ++x)
				result[static_cast<size_t>(y) * destinationSize.cx + x] = area_sample(
					sourcePixels, sourceSize, sourceRect.x + x * stepX, sourceRect.y + y * stepY,
					sourceRect.x + (x + 1) * stepX, sourceRect.y + (y + 1) * stepY);
		return result;
	}

	inline std::vector<std::uint32_t> resample_bgra(const std::span<const std::uint32_t> sourcePixels,
	                                         const sizei sourceSize, const SampleRect sourceRect,
	                                         const sizei destinationSize)
	{
		using namespace resampler_detail;
		ResampleWorkspace workspace;
		resample_bgra(sourcePixels, sourceSize, sourceRect, destinationSize, workspace);
		return std::move(workspace.pixels);
	}
}

#pragma pop_macro("max")
#pragma pop_macro("min")
