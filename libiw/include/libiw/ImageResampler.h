// ImageWalker by Zac Walker
// Declares the bounded BGRA image resampler used by the CPU rendering tier.

#pragma once

#include <core/util_geometry.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace iw::imaging
{
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
