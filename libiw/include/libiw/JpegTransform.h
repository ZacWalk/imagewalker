#pragma once
#include <libiw/Codec.h>

namespace iw::imaging
{
	enum class JpegTransform { none, clockwise, halfTurn, counterClockwise, flipHorizontal, flipVertical, transpose, transverse };

	struct JpegTransformOptions
	{
		JpegTransform transform{JpegTransform::none};
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
