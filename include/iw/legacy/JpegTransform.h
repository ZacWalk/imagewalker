#pragma once

#include <iw/legacy/LegacyPch.h>
#include <iw/legacy/FileJpegTran.h>

#include <libfiles/encode.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

enum class JpegTransformResult { NotJpeg, Done, Failed };

namespace IW
{
	struct JpegTransformRequest
	{
		JXFORM_CODE transform = JXFORM_NONE;
		bool perfect = false;
		bool trim = false;
		bool forceGrayscale = false;
		bool optimizeCoding = false;
		bool progressive = false;
		std::uint64_t maximumBytes = kMaxPageBytes;
	};

	inline bool IsJpegStream(IStreamIn& input);
	inline JpegTransformResult TransformJpegStream(IStreamIn& input, IStreamOut& output,
		JXFORM_CODE transform, IStatus* status, std::uint64_t maximumBytes = kMaxPageBytes);
	inline JpegTransformResult TransformJpegStream(IStreamIn& input, IStreamOut& output,
		const JpegTransformRequest& request, IStatus* status);
}

namespace IW::legacy_detail
{
	inline std::vector<std::uint8_t> read_jpeg_stream(IW::IStreamIn& input)
	{
		const auto memory = input.GetMemory();
		if (!memory.empty()) return {memory.begin(), memory.end()};
		const auto size = input.GetFileSize().GetByteCount();
		if (size == 0 || size > INT_MAX) return {};
		std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
		if (input.Seek(IW::IStreamCommon::eBegin, 0) != 0) return {};
		DWORD offset = 0;
		while (offset < result.size())
		{
			DWORD read{};
			const auto wanted = static_cast<DWORD>(
				(std::min<std::size_t>)(result.size() - offset, MAXDWORD));
			if (!input.Read(result.data() + offset, wanted, &read) || read == 0) return {};
			offset += read;
		}
		return result;
	}

	inline bool write_jpeg_stream(IW::IStreamOut& output,
		const std::span<const std::uint8_t> bytes)
	{
		DWORD written{};
		return bytes.size() <= MAXDWORD &&
			output.Write(bytes.data(), static_cast<DWORD>(bytes.size()), &written) &&
			written == bytes.size();
	}

	inline std::optional<libfiles::jpeg_transform> jpeg_transform_code(
		const JXFORM_CODE code)
	{
		switch (code)
		{
		case JXFORM_NONE: return libfiles::jpeg_transform::none;
		case JXFORM_FLIP_H: return libfiles::jpeg_transform::flip_horizontal;
		case JXFORM_FLIP_V: return libfiles::jpeg_transform::flip_vertical;
		case JXFORM_TRANSPOSE: return libfiles::jpeg_transform::transpose;
		case JXFORM_TRANSVERSE: return libfiles::jpeg_transform::transverse;
		case JXFORM_ROT_90: return libfiles::jpeg_transform::rotate90;
		case JXFORM_ROT_180: return libfiles::jpeg_transform::rotate180;
		case JXFORM_ROT_270: return libfiles::jpeg_transform::rotate270;
		default: return {};
		}
	}
}

namespace IW
{
	inline bool IsJpegStream(IStreamIn& input)
	{
		const auto bytes = legacy_detail::read_jpeg_stream(input);
		return bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xd8;
	}

	inline JpegTransformResult TransformJpegStream(IStreamIn& input, IStreamOut& output,
		JXFORM_CODE transform, IStatus* status, const std::uint64_t maximumBytes)
	{
		IW::JpegTransformRequest request;
		request.transform = transform;
		request.maximumBytes = maximumBytes;
		return TransformJpegStream(input, output, request, status);
	}

	inline JpegTransformResult TransformJpegStream(IStreamIn& input, IStreamOut& output,
		const JpegTransformRequest& request, IStatus* status)
	{
		const auto bytes = legacy_detail::read_jpeg_stream(input);
		if (bytes.size() < 2 || bytes[0] != 0xff || bytes[1] != 0xd8)
			return JpegTransformResult::NotJpeg;
		if (status->QueryCancel())
		{
			status->SetError(L"JPEG transformation was cancelled.");
			return JpegTransformResult::Failed;
		}
		const bool advanced = request.trim || request.forceGrayscale ||
			request.optimizeCoding || request.progressive;
		if (request.transform == JXFORM_NONE && !advanced)
			return legacy_detail::write_jpeg_stream(output, bytes)
				? JpegTransformResult::Done : JpegTransformResult::Failed;
		const auto mapped = legacy_detail::jpeg_transform_code(request.transform);
		if (!mapped)
		{
			status->SetError(L"Unsupported JPEG coefficient transform.");
			return JpegTransformResult::Failed;
		}

		libfiles::jpeg_encoder encoder;
		libfiles::jpeg_transformer transformer;
		libfiles::jpeg_transform_options options;
		options.transform = *mapped;
		options.policy = request.perfect
			? libfiles::jpeg_transform_policy::perfect
			: libfiles::jpeg_transform_policy::partial_blocks;
		options.trim = request.trim;
		options.force_grayscale = request.forceGrayscale;
		options.optimize_coding = request.optimizeCoding;
		options.progressive = request.progressive;
		options.maximum_bytes = request.maximumBytes;
		options.cancelled = [status] { return status->QueryCancel() != FALSE; };
		options.progress = [status](const std::uint32_t current, const std::uint32_t total)
		{
			status->Progress(static_cast<int>((std::min)(current, static_cast<std::uint32_t>(INT_MAX))),
			                 static_cast<int>((std::min)(total, static_cast<std::uint32_t>(INT_MAX))));
		};
		const auto result = transformer.transform(bytes, encoder, options);
		if (result.data.empty() || !legacy_detail::write_jpeg_stream(output, result.data))
		{
			if (result.cancelled || status->QueryCancel())
				status->SetError(L"JPEG transformation was cancelled.");
			else if (result.over_budget || result.resource_limit)
				status->SetResourceError(L"The JPEG coefficient transform exceeds the memory limit.");
			else
				status->SetError(CString(CA2W(result.error.c_str(), CP_UTF8)));
			return JpegTransformResult::Failed;
		}
		return JpegTransformResult::Done;
	}
}
