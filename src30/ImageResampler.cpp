#include "ImageResampler.h"
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

}
