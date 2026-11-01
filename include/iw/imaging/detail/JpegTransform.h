#pragma once

// Preserve historical min/max macros around inline modern code.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include <iw/imaging/JpegTransform.h>
#include <libfiles/encode.h>
#include <algorithm>

namespace iw::imaging
{
	inline EncodedImageResult transform_jpeg(const std::span<const std::uint8_t> source,
		const JpegTransformOptions& options)
	{
		EncodedImageResult result;
		if (source.empty() || options.maximumBytes == 0)
		{
			result.error = ImageError::invalid_input;
			result.message = L"Invalid JPEG transform request.";
			return result;
		}
		constexpr libfiles::jpeg_transform transforms[] = {
			libfiles::jpeg_transform::none, libfiles::jpeg_transform::rotate90,
			libfiles::jpeg_transform::rotate180, libfiles::jpeg_transform::rotate270,
			libfiles::jpeg_transform::flip_horizontal, libfiles::jpeg_transform::flip_vertical,
			libfiles::jpeg_transform::transpose, libfiles::jpeg_transform::transverse};
		const auto index = static_cast<std::size_t>(options.transform);
		if (index >= std::size(transforms))
		{
			result.error = ImageError::invalid_input;
			result.message = L"Invalid JPEG transform request.";
			return result;
		}
		const auto cancelled = [&options]
		{
			return options.stop.stop_requested() || (options.cancelled && options.cancelled());
		};
		if (cancelled())
		{
			result.error = ImageError::cancelled;
			result.message = L"JPEG transformation was cancelled.";
			return result;
		}
		if (source.size() < 2 || source[0] != 0xff || source[1] != 0xd8)
		{
			result.error = ImageError::unsupported;
			result.message = L"The input is not a JPEG image.";
			return result;
		}
		if (index == 0 && !options.trim && !options.forceGrayscale &&
			!options.optimizeCoding && !options.progressive)
		{
			result.bytes.assign(source.begin(), source.end());
			return result;
		}
		libfiles::jpeg_transform_options request;
		request.transform = transforms[index];
		request.policy = options.policy == JpegTransformPolicy::perfect
			? libfiles::jpeg_transform_policy::perfect : libfiles::jpeg_transform_policy::partial_blocks;
		request.trim = options.trim;
		request.force_grayscale = options.forceGrayscale;
		request.optimize_coding = options.optimizeCoding;
		request.progressive = options.progressive;
		request.maximum_bytes = options.maximumBytes;
		request.cancelled = cancelled;
		request.progress = [&options](const std::uint32_t current, const std::uint32_t total)
		{
			if (options.progress) options.progress(
				static_cast<int>((std::min)(current, static_cast<std::uint32_t>(INT_MAX))),
				static_cast<int>((std::min)(total, static_cast<std::uint32_t>(INT_MAX))));
		};
		libfiles::jpeg_encoder encoder;
		libfiles::jpeg_transformer transformer;
		auto transformed = transformer.transform(source, encoder, request);
		if (!transformed.data.empty()) result.bytes = std::move(transformed.data);
		else
		{
			result.error = transformed.cancelled ? ImageError::cancelled :
				(transformed.over_budget || transformed.resource_limit) ? ImageError::resource_limit :
				transformed.unsupported ? ImageError::unsupported : ImageError::encode_failed;
			result.message = transformed.error.empty() ? L"The lossless JPEG transformation failed." :
				std::wstring(transformed.error.begin(), transformed.error.end());
		}
		return result;
	}
}

#pragma pop_macro("max")
#pragma pop_macro("min")
