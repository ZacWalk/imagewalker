// ImageWalker by Zac Walker
// Declares the bounded BGRA image resampler used by the CPU rendering tier.

#pragma once

#include "util_geometry.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace iw::ui
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

	class ResampleCache
	{
	public:
		std::span<const std::uint32_t> resample(std::span<const std::uint32_t> sourcePixels,
		                                        sizei sourceSize, SampleRect sourceRect,
		                                        sizei destinationSize, std::uint64_t cacheToken = 0);
		size_t hit_count() const { return hitCount_; }

	private:
		struct Entry
		{
			const std::uint32_t* source{};
			size_t sourceCount{};
			sizei sourceSize{};
			SampleRect sourceRect{};
			sizei destinationSize{};
			std::uint64_t cacheToken{};
			std::uint64_t lastUse{};
			bool valid{};
			ResampleWorkspace workspace;
		};

		std::array<Entry, 2> entries_{};
		ResampleWorkspace transient_;
		std::uint64_t clock_{};
		size_t hitCount_{};
	};

	std::vector<std::uint32_t> resample_bgra(std::span<const std::uint32_t> sourcePixels,
	                                         sizei sourceSize, SampleRect sourceRect,
	                                         sizei destinationSize);
	std::span<const std::uint32_t> resample_bgra(std::span<const std::uint32_t> sourcePixels,
	                                             sizei sourceSize, SampleRect sourceRect,
	                                             sizei destinationSize, ResampleWorkspace& workspace);
}