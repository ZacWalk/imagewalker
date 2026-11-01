#pragma once

// Preserve historical min/max macros around inline modern code.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <iw/imaging/Codec.h>

namespace iw::imaging
{
	enum class JpegTransform { none, clockwise, halfTurn, counterClockwise, flipHorizontal, flipVertical, transpose, transverse };
	enum class JpegTransformPolicy { perfect, partialBlocks };

	struct JpegTransformOptions
	{
		JpegTransform transform{JpegTransform::none};
		JpegTransformPolicy policy{JpegTransformPolicy::partialBlocks};
		bool trim{};
		bool forceGrayscale{};
		bool optimizeCoding{};
		bool progressive{};
		std::uint64_t maximumBytes{256ull * 1024 * 1024};
		std::stop_token stop;
		std::function<bool()> cancelled;
		std::function<void(int, int)> progress;
	};

	struct EncodedImageResult : OperationResult
	{
		std::vector<std::uint8_t> bytes;
	};

	EncodedImageResult transform_jpeg(std::span<const std::uint8_t> source,
	                                  const JpegTransformOptions& options = {});
}

#include <iw/imaging/detail/JpegTransform.h>

#pragma pop_macro("max")
#pragma pop_macro("min")
