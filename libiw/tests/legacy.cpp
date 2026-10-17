#include <libiw/ImageEdits.h>
#include <libiw/JpegTransform.h>
#include "../src/NativeCodec.h"
#include "../src/legacy/LegacyPch.h"
#include "../src/legacy/FileModern.h"
#include "ImagingStreams.h"
#include "FilePng.h"
#include "FileBmp.h"
#include "FileGif.h"
#include "FileJpeg.h"
#include "FileTiff.h"
#include "FilePcx.h"
#include "FilePsd.h"
#include "FileWpg.h"
#include "ImagingEdits.h"
#include "MetadataExif.h"
#include "ImagingBlitter.h"
#include "RenderImage.h"
#include "iw/testrunner.h"

namespace IW
{
	template<class Base, bool raw = true>
	using ImageStream = ImageStreamCore<Base, raw>;
}

#define IW_LIBIW_TESTS
#include "iw/imagingtests.h"

IW_TEST(PackedAndBgraImagesUseTheSameColorAdjustments)
{
	const IW::Image source = MakeAlphaImage(19, 13, 128);
	ImageEdits legacy;
	legacy._brightness = 10;
	legacy._contrast = 12;
	legacy._darks = 9;
	legacy._midtones = -7;
	legacy._lights = 5;
	legacy._saturation = 13;
	legacy._vibrance = 21;
	legacy._temperature = 30;
	legacy._tint = -11;
	IW::Image adjusted;
	IW_CHECK(IW::ApplyEditColor(source, adjusted, legacy, IW::CNullStatus::Instance));
	iw::edits::ImageEdits modern;
	modern.brightness = legacy._brightness;
	modern.contrast = legacy._contrast;
	modern.darks = legacy._darks;
	modern.midtones = legacy._midtones;
	modern.lights = legacy._lights;
	modern.saturation = legacy._saturation;
	modern.vibrance = legacy._vibrance;
	modern.temperature = legacy._temperature;
	modern.tint = legacy._tint;
	auto pixels = iw::imaging::image_pixels(source);
	iw::edits::apply_color(pixels.pixels, modern);
	IW_CHECK(pixels.pixels == iw::imaging::image_pixels(adjusted).pixels);
}

IW_TEST(AllEightOrientationsAgreeAcrossPixelRepresentations)
{
	const auto source = MakeAlphaImage(9, 5, 120);
	for (int orientation = 1; orientation <= 8; ++orientation)
	{
		auto native = source.Clone();
		auto settings = native.GetCameraSettings();
		settings.Orientation = orientation;
		settings.OriginalImageSize = CSize(9, 5);
		native.SetCameraSettings(settings);
		native.FixOrientation(IW::CNullStatus::Instance);
		auto modern = iw::imaging::image_pixels(source);
		IW_CHECK(iw::imaging::orient_upright(modern, orientation));
		const auto converted = iw::imaging::image_pixels(native);
		IW_CHECK_EQ(converted.width, modern.width);
		IW_CHECK_EQ(converted.height, modern.height);
		IW_CHECK(converted.pixels == modern.pixels);
		IW_CHECK_EQ(native.GetCameraSettings().Orientation, IW::Orientation::TopLeft);
	}
}

IW_TEST(LosslessJpegApiPreservesMarkersAndReportsCancellation)
{
	auto image = MakeTrueColourImage(48, 32, IW::PixelFormat::PF24);
	CLoadJpg encoder;
	IW::SimpleBlob encoded;
	IW::StreamBlob<IW::SimpleBlob> output(encoded);
	IW_CHECK(encoder.Write(L"JPG", &output, image, IW::CodecSettings(), IW::CNullStatus::Instance));
	IW_CHECK(output.Close(IW::CNullStatus::Instance));
	const BYTE segment[] = {0xff, 0xed, 0, 11, 'i', 'w', '-', 'o', 'p', 'a', 'q', 'u', 'e'};
	std::vector<std::uint8_t> source(encoded.GetData(), encoded.GetData() + encoded.GetDataSize());
	IW_CHECK(source.size() >= 2);
	if (source.size() < 2) return;
	source.insert(source.begin() + 2, std::begin(segment), std::end(segment));
	iw::imaging::JpegTransformOptions options;
	options.transform = iw::imaging::JpegTransform::clockwise;
	auto result = iw::imaging::transform_jpeg(source, options);
	IW_CHECK(static_cast<bool>(result));
	if (result)
	{
		IW_CHECK(std::search(result.bytes.begin(), result.bytes.end(), std::begin(segment), std::end(segment)) != result.bytes.end());
		auto decoded = iw::imaging::decode_document(result.bytes, L".jpg");
		IW_CHECK(static_cast<bool>(decoded));
		if (decoded)
		{
			IW_CHECK_EQ(decoded.document.frame(0).width, 32);
			IW_CHECK_EQ(decoded.document.frame(0).height, 48);
		}
		auto turned = result;
		for (int i = 0; i < 3 && turned; ++i) turned = iw::imaging::transform_jpeg(turned.bytes, options);
		IW_CHECK(static_cast<bool>(turned));
		if (turned)
		{
			const auto before = iw::imaging::decode_document(source, L".jpg");
			const auto after = iw::imaging::decode_document(turned.bytes, L".jpg");
			IW_CHECK(static_cast<bool>(before) && static_cast<bool>(after));
			if (before && after) IW_CHECK(before.document.frame_pixels().pixels == after.document.frame_pixels().pixels);
		}
	}

	options.cancelled = [] { return true; };
	result = iw::imaging::transform_jpeg(source, options);
	IW_CHECK(result.error == iw::imaging::ImageError::cancelled);
	IW_CHECK(result.bytes.empty());
	options.cancelled = {};
	options.maximumBytes = 1;
	result = iw::imaging::transform_jpeg(source, options);
	IW_CHECK(result.error == iw::imaging::ImageError::resource_limit);
	IW_CHECK(result.bytes.empty());
}

IW_TEST(MemoryStreamsUseExactEofAndSignedSeekOffsets)
{
	const BYTE bytes[] = {10, 20, 30, 40};
	IW::StreamConstBlob input(bytes, IW::FileSize(sizeof(bytes)));
	IW_CHECK_EQ(input.Seek(IW::IStreamCommon::eEnd, 0), 4u);
	BYTE value{};
	DWORD read = 1;
	IW_CHECK(!input.Read(&value, 1, &read));
	IW_CHECK_EQ(read, 0u);
	IW_CHECK_EQ(input.Seek(IW::IStreamCommon::eEnd, -1), 3u);
	IW_CHECK(input.Read(&value, 1, &read));
	IW_CHECK_EQ(value, 40);
	IW_CHECK_EQ(input.Seek(IW::IStreamCommon::eBegin, 5), INVALID_SET_FILE_POINTER);
}

IW_TEST(ExifDatesUseWordSizedSystemTimeFields)
{
	IW::FileTime date;
	date.ParseExifDate("2026:10:17 14:32:42");
	const auto local = date.ToLocalTime().ToSystemTime();
	IW_CHECK_EQ(local.wYear, 2026);
	IW_CHECK_EQ(local.wMonth, 10);
	IW_CHECK_EQ(local.wDay, 17);
	IW_CHECK_EQ(local.wHour, 14);
	IW_CHECK_EQ(local.wMinute, 32);
	IW_CHECK_EQ(local.wSecond, 42);
}

IW_TEST(WebPLoaderWritesWhatTheNativeRegistryReadsBack)
{
	IW::Image image;
	IW::Page& page = image.CreatePage(16, 16, IW::PixelFormat::PF24);
	{
		IW::IImageSurfaceLockPtr lock = page.GetSurfaceLock();
		std::vector<COLORREF> line(16, IW::RGBA(101, 61, 31));
		for (int y = 0; y < 16; ++y) lock->SetLine(line.data(), y, 0, 16);
	}
	CLoadWebP encoder;
	IW::SimpleBlob encoded;
	IW::StreamBlob<IW::SimpleBlob> output(encoded);
	IW::CodecSettings settings;
	settings.JpegQuality = 100;
	IW_CHECK(encoder.Write(L"WEBP", &output, image, settings, IW::CNullStatus::Instance));
	IW_CHECK(output.Close(IW::CNullStatus::Instance));
	const std::vector<std::uint8_t> bytes(encoded.GetData(), encoded.GetData() + encoded.GetDataSize());
	const auto decoded = iw::imaging::decode_document(bytes, L".webp");
	IW_CHECK(static_cast<bool>(decoded));
	if (!decoded) return;
	const auto expected = iw::imaging::image_pixels(image);
	const auto actual = decoded.document.frame_pixels();
	IW_CHECK_EQ(actual.width, 16);
	IW_CHECK_EQ(actual.height, 16);
	IW_CHECK_EQ(actual.pixels.size(), expected.pixels.size());
	if (actual.pixels.size() != expected.pixels.size()) return;
	// Lossy WebP keeps a flat colour within its YUV rounding.
	for (size_t i = 0; i < expected.pixels.size(); ++i)
		for (const int shift : {0, 8, 16, 24})
			IW_CHECK(std::abs(static_cast<int>((expected.pixels[i] >> shift) & 0xff) -
				static_cast<int>((actual.pixels[i] >> shift) & 0xff)) <= 6);
}
