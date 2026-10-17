#include <libiw/JpegTransform.h>
#include "LegacyPch.h"
#include "JpegTransform.h"
#include "OperationStatus.h"

namespace
{
	void jpeg_failure(j_common_ptr codec)
	{
		char message[JMSG_LENGTH_MAX]{};
		codec->err->format_message(codec, message);
		throw IW::invalid_file(message);
	}

	struct JpegPair
	{
		jpeg_decompress_struct source{};
		jpeg_compress_struct destination{};
		jpeg_error_mgr sourceError{}, destinationError{};
		JpegPair()
		{
			source.err = jpeg_std_error(&sourceError);
			destination.err = jpeg_std_error(&destinationError);
			sourceError.error_exit = destinationError.error_exit = jpeg_failure;
		}
		~JpegPair()
		{
			if (destination.mem) jpeg_destroy_compress(&destination);
			if (source.mem) jpeg_destroy_decompress(&source);
		}
	};
}

bool IW::IsJpegStream(IStreamIn& input)
{
	BYTE signature[2]{};
	DWORD read{};
	input.Seek(IStreamCommon::eBegin, 0);
	const bool jpeg = input.Read(signature, sizeof(signature), &read) && read == sizeof(signature) &&
		signature[0] == 0xff && signature[1] == 0xd8;
	input.Seek(IStreamCommon::eBegin, 0);
	return jpeg;
}

JpegTransformResult IW::TransformJpegStream(IStreamIn& input, IStreamOut& output,
	JXFORM_CODE transform, IStatus* status, std::uint64_t maximumBytes)
{
	if (!IsJpegStream(input)) return JpegTransformResult::NotJpeg;
	JpegPair codec;
	try
	{
		if (status->QueryCancel())
		{
			status->SetError(L"JPEG transformation was cancelled.");
			return JpegTransformResult::Failed;
		}
		jpeg_create_decompress(&codec.source);
		jpeg_create_compress(&codec.destination);
		jpeg_iw_src(&codec.source, &input, 0);
		jcopy_markers_setup(&codec.source, JCOPYOPT_ALL);
		if (jpeg_read_header(&codec.source, TRUE) != JPEG_HEADER_OK)
			throw invalid_file("The JPEG header could not be read.");
		status->SetStatusMessage(Text::JPEGTRANSFORM);
		std::uint64_t coefficients{};
		for (int i = 0; i < codec.source.num_components; ++i)
		{
			const auto& component = codec.source.comp_info[i];
			const auto width = (static_cast<std::uint64_t>(codec.source.image_width) * component.h_samp_factor +
				codec.source.max_h_samp_factor * DCTSIZE - 1) / (codec.source.max_h_samp_factor * DCTSIZE);
			const auto height = (static_cast<std::uint64_t>(codec.source.image_height) * component.v_samp_factor +
				codec.source.max_v_samp_factor * DCTSIZE - 1) / (codec.source.max_v_samp_factor * DCTSIZE);
			coefficients += width * height * DCTSIZE2 * sizeof(JCOEF) * 2;
		}
		if (coefficients > maximumBytes)
		{
			status->SetResourceError(L"The JPEG coefficient transform exceeds the memory limit.");
			return JpegTransformResult::Failed;
		}
		jpeg_transform_info options{};
		options.transform = transform;
		options.trim = FALSE;
		options.crop = FALSE;
		jtransform_normalise_exif_orientation(&codec.source, &options);
		jtransform_request_workspace(&codec.source, &options);
		auto source = jpeg_read_coefficients(&codec.source);
		status->Progress(2, 8);
		jpeg_copy_critical_parameters(&codec.source, &codec.destination);
		auto destination = jtransform_adjust_parameters(&codec.source, &codec.destination, source, &options);
		jpeg_iw_dest(&codec.destination, &output);
		jpeg_write_coefficients(&codec.destination, destination);
		jcopy_markers_execute(&codec.source, &codec.destination, JCOPYOPT_ALL);
		status->Progress(6, 8);
		jtransform_execute_transform(&codec.source, &codec.destination, source, &options, status);
		if (status->QueryCancel())
		{
			status->SetError(L"JPEG transformation was cancelled.");
			return JpegTransformResult::Failed;
		}
		jpeg_finish_compress(&codec.destination);
		jpeg_finish_decompress(&codec.source);
		status->Progress(8, 8);
		return JpegTransformResult::Done;
	}
	catch (const std::bad_alloc&)
	{
		status->SetResourceError(L"Not enough memory for the JPEG transformation.");
	}
	catch (const std::exception& error)
	{
		status->SetError(CString(error.what()));
	}
	return JpegTransformResult::Failed;
}

iw::imaging::EncodedImageResult iw::imaging::transform_jpeg(const std::span<const std::uint8_t> source,
	const JpegTransformOptions& options)
{
	EncodedImageResult result;
	constexpr JXFORM_CODE transforms[] = {JXFORM_NONE, JXFORM_ROT_90, JXFORM_ROT_180, JXFORM_ROT_270,
		JXFORM_FLIP_H, JXFORM_FLIP_V, JXFORM_TRANSPOSE, JXFORM_TRANSVERSE};
	const auto transform = static_cast<size_t>(options.transform);
	if (source.empty() || source.size() > INT_MAX || transform >= std::size(transforms) || options.maximumBytes == 0)
	{
		result.error = ImageError::invalid_input;
		result.message = L"Invalid JPEG transform request.";
		return result;
	}
	DecodeOptions context;
	context.stop = options.stop;
	context.cancelled = options.cancelled;
	context.progress = options.progress;
	detail::OperationStatus status(context, result);
	IW::StreamConstBlob input(source.data(), IW::FileSize(source.size()));
	IW::SimpleBlob encoded;
	IW::StreamBlob<IW::SimpleBlob> output(encoded);
	const auto outcome = IW::TransformJpegStream(input, output, transforms[transform], &status, options.maximumBytes);
	if (status.QueryCancel())
	{
		result.error = ImageError::cancelled;
		result.message = L"JPEG transformation was cancelled.";
	}
	else if (outcome == JpegTransformResult::NotJpeg)
	{
		result.error = ImageError::unsupported;
		result.message = L"The input is not a JPEG image.";
	}
	else if (outcome == JpegTransformResult::Done && output.Close(&status))
		result.bytes.assign(encoded.GetData(), encoded.GetData() + encoded.GetDataSize());
	else if (result.error == ImageError::none)
	{
		result.error = ImageError::encode_failed;
		result.message = L"The lossless JPEG transformation failed.";
	}
	return result;
}
