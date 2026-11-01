#pragma once
#include "Geometry.h"
#include <iw/imaging/ImageResampler.h>
namespace iw::ui
{
	using imaging::SampleRect;
	using imaging::ResampleWorkspace;
	using imaging::resample_bgra;
	class ResampleCache
	{
	public:
		std::span<const std::uint32_t> resample(std::span<const std::uint32_t> sourcePixels,
		                                        app::sizei sourceSize, SampleRect sourceRect,
		                                        app::sizei destinationSize, std::uint64_t cacheToken = 0);
		size_t hit_count() const { return hitCount_; }

	private:
		struct Entry
		{
			const std::uint32_t* source{};
			size_t sourceCount{};
			app::sizei sourceSize{};
			SampleRect sourceRect{};
			app::sizei destinationSize{};
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

}
