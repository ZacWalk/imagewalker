#include <iw/imaging/ImageEdits.h>
#include <iw/imaging/JpegTransform.h>
#include <iw/imaging/Document.h>
#include <iw/legacy/LegacyPch.h>
#include <iw/legacy/Legacy.h>
#include <iw/legacy/FileModern.h>
#include <iw/legacy/OperationStatus.h>
#include <iw/legacy/NativeCodec.h>
#include <iw/imaging/WebPCodec.h>
#include <cstdio>
#include <cstring>
#include <iw/legacy/ImagingStreams.h>
#include <iw/legacy/FilePng.h>
#include <iw/legacy/FileBmp.h>
#include <iw/legacy/FileGif.h>
#include <iw/legacy/FileJpeg.h>
#include <iw/legacy/FileTiff.h>
#include <iw/legacy/FilePcx.h>
#include <iw/legacy/FilePsd.h>
#include <iw/legacy/FileCompoundDoc.h>
#include <iw/legacy/FileWpg.h>
#include <iw/legacy/ImagingEdits.h>
#include <iw/legacy/MetadataExif.h>
#include <iw/legacy/ImagingBlitter.h>
#include <iw/legacy/RenderImage.h>
#include "iw/testrunner.h"

namespace IW
{
	template<class Base, bool raw = true>
	using ImageStream = ImageStreamCore<Base, raw>;
}

#define IW_LIBIW_TESTS
#include "iw/imagingtests.h"

namespace
{
	struct NativeReaderRecord
	{
		std::uint64_t hash = 14695981039346656037ull;
		unsigned cases{};
		unsigned decoded{};

		void number(std::uint64_t value)
		{
			for (int i = 0; i < 8; ++i)
			{
				hash = (hash ^ (value & 255)) * 1099511628211ull;
				value >>= 8;
			}
		}

		void bytes(std::span<const std::uint8_t> value)
		{
			number(value.size());
			for (const auto byte : value) hash = (hash ^ byte) * 1099511628211ull;
		}

		void image(const IW::Image& value)
		{
			number(value.Pages.size());
			for (const auto& page : value.Pages)
			{
				const auto bounds = page.GetPageRect();
				number(bounds.left);
				number(bounds.top);
				number(page.GetWidth());
				number(page.GetHeight());
				number(page.GetPixelFormat()._pf);
				number(page.GetFlags());
				number(page.GetTimeDelay());
				number(page.GetDisposalMethod());
				number(page.GetTransparent());
				const auto pixels = iw::imaging::image_pixels(value, &page - value.Pages.data());
				for (const auto pixel : pixels.pixels) number(pixel);
			}
			const auto& settings = value.GetCameraSettings();
			number(settings.OriginalImageSize.cx);
			number(settings.OriginalImageSize.cy);
			number(settings.OriginalBpp._pf);
			number(settings.XPelsPerMeter);
			number(settings.YPelsPerMeter);
			number(settings.Orientation);
			number(value.Blobs.size());
			for (const auto& blob : value.Blobs)
			{
				number(blob.GetType());
				bytes({blob.GetData(), blob.GetDataSize()});
			}
		}
	};

	class RecordImageStream final : public IW::ImageStreamCore<IW::IImageStream>
	{
	public:
		explicit RecordImageStream(IW::Image& image) : ImageStreamCore(image, false) {}
		void CreatePage(const CRect& rect, const IW::PixelFormat& format, bool sequential) override
		{
			if (rect.Width() <= 0 || rect.Height() <= 0 ||
				static_cast<std::uint64_t>(rect.Width()) * rect.Height() * 4 > 1024 * 1024)
				throw std::bad_alloc();
			ImageStreamCore::CreatePage(rect, format, sequential);
		}
	};

	template<class Loader>
	void record_native_input(NativeReaderRecord& record, std::span<const std::uint8_t> bytes)
	{
		IW::Image image;
		RecordImageStream destination(image);
		IW::StreamConstBlob source(bytes.data(), IW::FileSize(bytes.size()));
		iw::imaging::DecodeOptions options;
		iw::imaging::OperationResult result;
		iw::imaging::detail::OperationStatus status(options, result);
		Loader loader;
		bool loaded{};
		try { loaded = loader.Read(loader.GetKey(), &source, &destination, &status); }
		catch (const std::exception&) { loaded = false; }
		++record.cases;
		record.number(loaded);
		if (loaded)
		{
			++record.decoded;
			record.image(image);
		}
	}

	template<class Loader>
	void record_native_fixture(NativeReaderRecord& record, const std::vector<std::uint8_t>& bytes,
		const bool includeCuts)
	{
		record_native_input<Loader>(record, bytes);
		const auto step = (std::max)(size_t{1}, bytes.size() / 32);
		if (includeCuts)
			for (size_t length = 0; length < bytes.size(); length += step)
				record_native_input<Loader>(record, {bytes.data(), length});
		for (size_t offset = bytes.size() > 32 ? bytes.size() - 32 : 0; offset < bytes.size(); ++offset)
		{
			auto damaged = bytes;
			damaged[offset] ^= 0x80;
			record_native_input<Loader>(record, damaged);
		}
	}

	template<class Loader>
	std::vector<std::uint8_t> native_record_fixture(const IW::Image& image)
	{
		Loader loader;
		IW::SimpleBlob encoded;
		IW::StreamBlob<IW::SimpleBlob> destination(encoded);
		IW_CHECK(loader.Write(loader.GetKey(), &destination, image, IW::CodecSettings(), IW::CNullStatus::Instance));
		IW_CHECK(destination.Close(IW::CNullStatus::Instance));
		return {encoded.GetData(), encoded.GetData() + encoded.GetDataSize()};
	}

	template<class Loader>
	void check_native_record(const char* name, const std::vector<std::vector<std::uint8_t>>& fixtures,
		const std::uint64_t expected, const bool includeCuts = true)
	{
		NativeReaderRecord record;
		for (const auto& fixture : fixtures) record_native_fixture<Loader>(record, fixture, includeCuts);
		std::printf("Native reader record %s: %u cases, %u decoded, %016llx\n", name,
			record.cases, record.decoded, static_cast<unsigned long long>(record.hash));
		IW_CHECK(record.decoded >= fixtures.size());
		IW_CHECK_EQ(record.hash, expected);
	}
}

IW_TEST(NativeReadersKeepTheirPreMigrationRecord)
{
	const auto rgb = MakeTrueColourImage(9, 5, IW::PixelFormat::PF24);
	const auto alpha = MakeAlphaImage(9, 5, 128);
	const auto indexed = MakePalettedImage(9, 5);
	check_native_record<CLoadBmp>("BMP", {native_record_fixture<CLoadBmp>(rgb),
		native_record_fixture<CLoadBmp>(indexed), native_record_fixture<CLoadBmp>(alpha)}, 0xe13cb887db79afddull);
	check_native_record<CLoadPng>("PNG", {native_record_fixture<CLoadPng>(rgb),
		native_record_fixture<CLoadPng>(indexed), native_record_fixture<CLoadPng>(alpha)}, 0x5281aa5ebd100ef5ull);
	check_native_record<CLoadGif>("GIF", {native_record_fixture<CLoadGif>(indexed)}, 0xd42799cb3f97ad19ull);
	check_native_record<CLoadJpg>("JPEG", {native_record_fixture<CLoadJpg>(rgb)}, 0xf9f2ea990e066ba6ull);
	check_native_record<CLoadTiff>("TIFF", {native_record_fixture<CLoadTiff>(rgb),
		native_record_fixture<CLoadTiff>(indexed)}, 0x40ed36de9363fcedull);
	check_native_record<CLoadPcx>("PCX", {MakeTestPcx()}, 0x6d4b7db47e2c1b8eull);
	// The old PSD reader accepted incomplete planes containing uninitialized pixels.
	// Only whole planes have a reproducible record; truncated planes must be rejected.
	check_native_record<CLoadPsd>("PSD", {MakePsdWithColourMap(1, 1, 9, 5, 0),
		MakePsdWithColourMap(2, 1, 9, 5, 768), MakePsdWithColourMap(3, 3, 9, 5, 0),
		MakePsdWithColourMap(3, 4, 9, 5, 0)}, 0x6d82576bf6a359e5ull, false);
	auto wpg = MakeWpgWithPaletteRecord(0, 0);
	wpg.resize(18);
	wpg.insert(wpg.end(), {0x0b, 14});
	for (const auto value : {3u, 1u, 8u, 0u, 0u}) WriteLE16(wpg, value);
	wpg.insert(wpg.end(), {3, 1, 2, 3});
	check_native_record<CLoadWpg>("WPG", {wpg}, 0xab96de3c8025e2fcull);
}

IW_TEST(NativeMemoryReadersBorrowTheirInputWithoutStreamCopies)
{
	struct Source final : IW::StreamConstBlob
	{
		using StreamConstBlob::StreamConstBlob;
		unsigned reads = 0;
		bool Read(LPVOID destination, DWORD count, LPDWORD read = nullptr) override
		{
			++reads;
			return StreamConstBlob::Read(destination, count, read);
		}
	};
	const auto file = MakeTestPcx();
	Source input(file.data(), IW::FileSize(file.size()));
	IW_CHECK(input.GetMemory().data() == file.data());
	IW_CHECK_EQ(input.GetMemory().size(), file.size());
	IW::Image image;
	IW::ImageStreamCore<IW::IImageStream> output(image, false);
	CLoadPcx decoder;
	IW_CHECK(decoder.Read(L"PCX", &input, &output, IW::CNullStatus::Instance));
	IW_CHECK_EQ(input.reads, 0u);
}

IW_TEST(NativePsdRejectsEveryIncompletePlane)
{
	const auto file = MakePsdWithColourMap(3, 3, 9, 5, 0);
	for (size_t length = 0; length < file.size(); ++length)
	{
		const auto decoded = iw::imaging::decode_document(
			std::span<const std::uint8_t>(file.data(), length), L".psd");
		IW_CHECK(!decoded);
		IW_CHECK_EQ(decoded.document.frames.size(), size_t{0});
	}
	IW_CHECK(static_cast<bool>(iw::imaging::decode_document(file, L".psd")));
}

IW_TEST(NativePsdColorModesKeepTheirPreMigrationRecord)
{
	check_native_record<CLoadPsd>("PSD colour modes", {
		MakePsdWithColourMap(4, 4, 9, 5, 0), MakePsdWithColourMap(4, 5, 9, 5, 0),
		MakePsdWithColourMap(9, 3, 9, 5, 0), MakePsdWithColourMap(9, 4, 9, 5, 0),
		MakePsdWithColourMap(7, 1, 9, 5, 0), MakePsdWithColourMap(8, 1, 9, 5, 0)}, 0xa9a171e16491f7bcull, false);
}

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

IW_TEST(NativeStorageStrideAndPageExtentsDoNotOverflow)
{
	IW_CHECK_EQ(IW::CalcStorageWidth(100000000, IW::PixelFormat::PF32), 400000000);
	IW_CHECK_EQ(IW::CalcStorageWidth(INT_MAX, IW::PixelFormat::PF1), 268435456);
	for (const auto rect : {CRect(0, 0, INT_MAX, INT_MAX), CRect(LONG_MIN, 0, LONG_MAX, 1)})
	{
		IW::Image image;
		bool rejected{};
		try { image.CreatePage(rect, IW::PixelFormat::PF32); }
		catch (const std::bad_alloc&) { rejected = true; }
		IW_CHECK(rejected);
		IW_CHECK(image.IsEmpty());
	}
}

IW_TEST(SixteenBitSubrowsDoNotReadPastTheLastPixel)
{
	SYSTEM_INFO info{};
	GetSystemInfo(&info);
	auto* allocation = static_cast<BYTE*>(VirtualAlloc(nullptr, static_cast<size_t>(info.dwPageSize) * 2,
		MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	IW_CHECK(allocation != nullptr);
	if (!allocation) return;
	struct Cleanup
	{
		void* allocation;
		~Cleanup() { IW_CHECK(VirtualFree(allocation, 0, MEM_RELEASE) != FALSE); }
	} cleanup{allocation};
	DWORD oldProtection{};
	IW_CHECK(VirtualProtect(allocation + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &oldProtection) != FALSE);
	auto* input = reinterpret_cast<unsigned short*>(allocation + info.dwPageSize) - 2;
	input[0] = 0x1234;
	input[1] = 0x5678;
	COLORREF pixel{};
	IW::Convert555to32(&pixel, reinterpret_cast<const BYTE*>(input), 1, 1);
	IW_CHECK_EQ(pixel, IW::RGB555to888(input[1]) | 0xff000000);
	IW::Convert565to32(&pixel, reinterpret_cast<const BYTE*>(input), 1, 1);
	IW_CHECK_EQ(pixel, IW::RGB565to888(input[1]) | 0xff000000);
}

IW_TEST(ThumbnailScratchUsesThePageRatherThanTheRequestedBox)
{
	const auto bytes = native_record_fixture<CLoadPng>(MakeTrueColourImage(9, 5, IW::PixelFormat::PF24));
	iw::imaging::DecodeOptions options;
	options.thumbnail = true;
	options.maximumWidth = INT_MAX;
	options.maximumHeight = 2;
	options.maximumBytes = 1024;
	const auto decoded = iw::imaging::decode_document(bytes, L".png", options);
	IW_CHECK(static_cast<bool>(decoded));
	if (!decoded) return;
	IW_CHECK_EQ(decoded.document.frames[0].width, 4u);
	IW_CHECK_EQ(decoded.document.frames[0].height, 2u);
}

IW_TEST(ThumbnailFlushNeverCompletesANewPageWithPreviousRows)
{
	IW::Image image;
	IW::ImageStreamThumbnailCore<IW::CNull, true> stream(image, CSize(4, 4), false);
	const std::uint32_t row[8]{0xff112233, 0xff112233, 0xff112233, 0xff112233,
		0xff112233, 0xff112233, 0xff112233, 0xff112233};
	stream.CreatePage(CRect(0, 0, 8, 8), IW::PixelFormat::PF32, true);
	for (int y = 0; y < 8; ++y) stream.SetBitmap(y, row);
	stream.Flush();
	IW_CHECK_EQ(image.GetPageCount(), 1u);
	stream.CreatePage(CRect(0, 0, 32, 1), IW::PixelFormat::PF32, true);
	stream.Flush();
	IW_CHECK_EQ(image.GetPageCount(), 2u);
	IW_CHECK(iw::imaging::image_pixels(image, 1).pixels ==
		std::vector<std::uint32_t>(4, 0xff000000));
	stream.CreatePage(CRect(0, 0, 2, 2), IW::PixelFormat::PF32, true);
	for (int y = 0; y < 2; ++y) stream.SetBitmap(y, row);
	stream.Flush();
	IW_CHECK_EQ(image.GetPageCount(), 3u);
	IW_CHECK(iw::imaging::image_pixels(image, 2).pixels ==
		std::vector<std::uint32_t>(4, 0xff112233));
}

IW_TEST(NativeDibAndBmpHeadersCarryTheirPaletteOrBitfields)
{
	for (const auto format : {IW::PixelFormat::PF8GrayScale, IW::PixelFormat::PF555,
		IW::PixelFormat::PF565, IW::PixelFormat::PF32})
	{
		const auto image = MakeTrueColourImage(9, 5, format);
		const auto handle = image.CopyToHandle();
		IW_CHECK(handle != nullptr);
		if (!handle) continue;
		const auto* info = static_cast<const BITMAPINFO*>(GlobalLock(handle));
		IW_CHECK(info != nullptr);
		if (info)
		{
			const size_t colours = format == IW::PixelFormat::PF8GrayScale ? 256 : 3;
			const size_t header = sizeof(BITMAPINFOHEADER) + colours * sizeof(RGBQUAD);
			IW_CHECK(GlobalSize(handle) >= header + info->bmiHeader.biSizeImage);
			if (format == IW::PixelFormat::PF8GrayScale)
			{
				IW_CHECK_EQ(info->bmiHeader.biClrUsed, 256u);
				IW_CHECK_EQ(reinterpret_cast<const DWORD*>(info->bmiColors)[127], RGB(127, 127, 127));
			}
			else
			{
				IW_CHECK_EQ(info->bmiHeader.biCompression, static_cast<DWORD>(BI_BITFIELDS));
				IW_CHECK_EQ(reinterpret_cast<const DWORD*>(info->bmiColors)[0],
					format == IW::PixelFormat::PF555 ? 0x7c00u :
					format == IW::PixelFormat::PF565 ? 0xf800u : 0xff0000u);
			}
			GlobalUnlock(handle);
		}
		IW_CHECK(GlobalFree(handle) == nullptr);
		const auto bmp = native_record_fixture<CLoadBmp>(image);
		IW_CHECK(bmp.size() >= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER));
		if (bmp.size() < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) continue;
		const auto* header = reinterpret_cast<const BITMAPFILEHEADER*>(bmp.data());
		const auto* bitmap = reinterpret_cast<const BITMAPINFOHEADER*>(bmp.data() + sizeof(BITMAPFILEHEADER));
		const size_t colours = format == IW::PixelFormat::PF8GrayScale ? 256 :
			format == IW::PixelFormat::PF32 ? 0 : 3;
		IW_CHECK_EQ(header->bfOffBits, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) +
			colours * sizeof(RGBQUAD));
		IW_CHECK_EQ(bitmap->biBitCount, format == IW::PixelFormat::PF8GrayScale ? 8 :
			format == IW::PixelFormat::PF32 ? 32 : 16);
	}
}

IW_TEST(NativeFiltersKeepAlphaAndSmallImagesAndReportCancellation)
{
	long identity[25]{};
	identity[12] = 1;
	for (const int size : {1, 7})
	{
		const auto source = MakeAlphaImage(size, size, 90);
		IW::Image result;
		IW_CHECK(IW::Filter(source, result, identity, 5, 1, 0, IW::CNullStatus::Instance));
		IW_CHECK(result.GetFirstPage().GetPixelFormat() == IW::PixelFormat::PF32Alpha);
		IW_CHECK(iw::imaging::image_pixels(source).pixels == iw::imaging::image_pixels(result).pixels);
	}
	auto source = MakeTrueColourImage(7, 7, IW::PixelFormat::PF24);
	source.Pages.push_back(source.GetFirstPage());
	IW::Image result;
	bool cancelled{};
	iw::imaging::DecodeOptions options;
	options.cancelled = [&] { return cancelled; };
	options.progress = [&](int, int) { cancelled = true; };
	iw::imaging::OperationResult operation;
	iw::imaging::detail::OperationStatus status(options, operation);
	IW_CHECK(!IW::Filter(source, result, identity, 5, 1, 0, &status));
	IW_CHECK_EQ(result.GetPageCount(), 1u);
}

IW_TEST(GeneralRotationUsesExactQuarterTurnsAndRejectsNonfiniteAngles)
{
	const auto source = MakeAlphaImage(9, 5, 90);
	const auto pixels = iw::imaging::image_pixels(source);
	for (const int quarters : {-4, -3, -2, -1, 0, 1, 2, 3, 4, 8})
	{
		IW::Image result;
		IW_CHECK(IW::Rotate(source, result, static_cast<float>(quarters * 90), IW::CNullStatus::Instance));
		const auto expected = iw::imaging::rotate_quarters(pixels, quarters);
		const auto actual = iw::imaging::image_pixels(result);
		IW_CHECK_EQ(actual.width, expected.width);
		IW_CHECK_EQ(actual.height, expected.height);
		IW_CHECK(actual.pixels == expected.pixels);
	}
	for (const auto angle : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		IW::Image result;
		IW_CHECK(!IW::Rotate(source, result, angle, IW::CNullStatus::Instance));
		IW_CHECK(result.IsEmpty());
	}
}

IW_TEST(NativeDisplayDecodesReportAnUnappliedOrientation)
{
	auto source = MakeTrueColourImage(16, 8, IW::PixelFormat::PF24);
	const auto exif = MakeExifBlob(false, 0x0112, 3, IW::Orientation::RightTop);
	IW::MetaData metadata(IW::MetaDataTypes::PROFILE_EXIF, exif.data(), static_cast<DWORD>(exif.size()));
	source.SetMetaData(metadata);
	auto settings = source.GetCameraSettings();
	settings.Orientation = IW::Orientation::RightTop;
	source.SetCameraSettings(settings);
	const auto encoded = native_record_fixture<CLoadJpg>(source);
	iw::imaging::DecodeOptions options;
	options.applyOrientation = false;
	const auto decoded = iw::imaging::decode(encoded, L".pcx", options);
	IW_CHECK(static_cast<bool>(decoded));
	IW_CHECK_EQ(decoded.orientation, 6);
	IW_CHECK_EQ(decoded.image.width, 16);
	IW_CHECK_EQ(decoded.image.height, 8);
}

IW_TEST(NativeDisplayDecodesBudgetTheExpandedBgraPixels)
{
	const auto encoded = native_record_fixture<CLoadBmp>(MakePalettedImage(9, 5));
	iw::imaging::DecodeOptions options;
	options.maximumBytes = 100;
	IW_CHECK(static_cast<bool>(iw::imaging::decode_document(encoded, L".bmp", options)));
	const auto decoded = iw::imaging::decode(encoded, L".pcx", options);
	IW_CHECK(decoded.error == iw::imaging::ImageError::resource_limit);
	IW_CHECK(decoded.image.pixels.empty());
}

IW_TEST(NativeEncodersReportShortAndFailedWrites)
{
	struct Output final : IW::StreamBlob<IW::SimpleBlob>
	{
		using StreamBlob::StreamBlob;
		int remaining = 1;
		bool shortWrite{};
		bool Write(LPCVOID bytes, DWORD count, LPDWORD written = nullptr) override
		{
			if (remaining-- > 0) return StreamBlob::Write(bytes, count, written);
			if (written) *written = count ? count - 1 : 0;
			return shortWrite;
		}
	};
	const auto image = MakeTrueColourImage(9, 5, IW::PixelFormat::PF24);
	for (const bool shortWrite : {false, true})
	{
		IW::SimpleBlob bytes;
		Output output(bytes);
		output.shortWrite = shortWrite;
		CLoadBmp encoder;
		IW_CHECK(!encoder.Write(L"BMP", &output, image, IW::CodecSettings(), IW::CNullStatus::Instance));
		output.remaining = 1;
		CLoadPng png;
		IW_CHECK(!png.Write(L"PNG", &output, image, IW::CodecSettings(), IW::CNullStatus::Instance));
		output.remaining = 1;
		CLoadTiff tiff;
		IW_CHECK(!tiff.Write(L"TIFF", &output, image, IW::CodecSettings(), IW::CNullStatus::Instance));
	}
}

IW_TEST(NativeTiffEncodesEveryStorageFormatWithoutLosingAlpha)
{
	for (const auto format : {IW::PixelFormat::PF1, IW::PixelFormat::PF2, IW::PixelFormat::PF4,
		IW::PixelFormat::PF8, IW::PixelFormat::PF8Alpha, IW::PixelFormat::PF8GrayScale,
		IW::PixelFormat::PF555, IW::PixelFormat::PF565, IW::PixelFormat::PF24,
		IW::PixelFormat::PF32, IW::PixelFormat::PF32Alpha})
	{
		IW::Image image;
		auto& page = image.CreatePage(9, 5, format);
		const auto pf = page.GetPixelFormat();
		const int count = pf.NumberOfPaletteEntries();
		for (int i = 0; i < count; ++i)
			page.GetPalette()[i] = IW::RGBA(i, 255 - i, (i * 13) & 255, pf.HasAlpha() ? 128 : 255);
		const auto lock = IW::IImageSurfaceLockPtr(page.GetSurfaceLock());
		for (int y = 0; y < 5; ++y)
		{
			COLORREF row[9]{};
			for (int x = 0; x < 9; ++x)
				row[x] = count ? (x + y) % count : IW::RGBA(40 + x, 80 + y, 120, pf.HasAlpha() ? 128 : 255);
			lock->SetLine(row, y, 0, 9);
		}
		const auto encoded = native_record_fixture<CLoadTiff>(image);
		const auto decoded = iw::imaging::decode(encoded, L".tif");
		IW_CHECK(static_cast<bool>(decoded));
		if (decoded) IW_CHECK(decoded.image.pixels == iw::imaging::image_pixels(image).pixels);
	}
}

IW_TEST(LegacyMetadataKindsSurviveSameFormatNativeRoundTrips)
{
	const auto text = std::vector<std::uint8_t>{
		't', 'E', 'X', 't', 'N', 'o', 't', 'e', 0, 'Q', 'A'};
	const auto text2 = std::vector<std::uint8_t>{
		'i', 'T', 'X', 't', 'L', 'a', 'b', 'e', 'l', 0, 0, 0, 0, 0,
		'E', 'u', 'r', 'o', ' ', 0xc3, 0xa9};
	const auto photoshop = std::vector<std::uint8_t>{
		'8', 'B', 'I', 'M', 0x12, 0x34, 0, 0, 0, 0, 0, 3, 'x', 'y', 'z', 0};

	IW::Image png = MakeTrueColourImage(9, 5, IW::PixelFormat::PF24);
	png.SetMetaData(IW::MetaData(IW::MetaDataTypes::PROFILE_AUTHOR,
		"old-author", 10));
	png.SetMetaData(IW::MetaData(IW::MetaDataTypes::PROFILE_AUTHOR,
		"QA", 2));
	png.AppendMetaData(IW::MetaData(IW::MetaDataTypes::PROFILE_PNG_TEXT,
		text.data(), static_cast<DWORD>(text.size())));
	png.AppendMetaData(IW::MetaData(IW::MetaDataTypes::PROFILE_PNG_TEXT,
		text2.data(), static_cast<DWORD>(text2.size())));
	const auto pngRoundTrip = ReloadThrough<CLoadPng>(_T("PNG"), png);
	const auto author = pngRoundTrip.GetMetaData(IW::MetaDataTypes::PROFILE_AUTHOR);
	IW_CHECK(author.GetDataSize() == 2 &&
		std::memcmp(author.GetData(), "QA", 2) == 0);
	std::vector<std::vector<std::uint8_t>> pngText;
	for (const auto& blob : pngRoundTrip.Blobs)
	{
		if (!blob.IsType(IW::MetaDataTypes::PROFILE_PNG_TEXT)) continue;
		pngText.emplace_back(blob.GetData(), blob.GetData() + blob.GetDataSize());
	}
	IW_CHECK_EQ(pngText.size(), size_t{2});
	if (pngText.size() == 2)
	{
		IW_CHECK(pngText[0] == text);
		IW_CHECK(pngText[1] == text2);
	}
	IW_CHECK(pngRoundTrip.Compare(pngRoundTrip));

	IW::Image propagated = pngRoundTrip;
	IW::IterateImageMetaData(pngRoundTrip, propagated, IW::CNullStatus::Instance);
	std::vector<std::vector<std::uint8_t>> propagatedText;
	for (const auto& blob : propagated.Blobs)
	{
		if (!blob.IsType(IW::MetaDataTypes::PROFILE_PNG_TEXT)) continue;
		propagatedText.emplace_back(blob.GetData(), blob.GetData() + blob.GetDataSize());
	}
	IW_CHECK_EQ(propagatedText.size(), size_t{2});
	if (propagatedText.size() == 2)
	{
		IW_CHECK(propagatedText[0] == text);
		IW_CHECK(propagatedText[1] == text2);
	}

	IW::Image edited;
	IW_CHECK(IW::Rotate180(pngRoundTrip, edited, IW::CNullStatus::Instance));
	IW::Image copied = edited;
	IW_CHECK(copied.Compare(edited));
	const auto editedRoundTrip = ReloadThrough<CLoadPng>(_T("PNG"), copied);
	std::vector<std::vector<std::uint8_t>> editedText;
	for (const auto& blob : editedRoundTrip.Blobs)
	{
		if (!blob.IsType(IW::MetaDataTypes::PROFILE_PNG_TEXT)) continue;
		editedText.emplace_back(blob.GetData(), blob.GetData() + blob.GetDataSize());
	}
	IW_CHECK_EQ(editedText.size(), size_t{2});
	if (editedText.size() == 2)
	{
		IW_CHECK(editedText[0] == text);
		IW_CHECK(editedText[1] == text2);
	}

	IW::Image tiff = MakeTrueColourImage(9, 5, IW::PixelFormat::PF24);
	tiff.SetMetaData(IW::MetaData(IW::MetaDataTypes::PROFILE_PHOTOSHOP,
		photoshop.data(), static_cast<DWORD>(photoshop.size())));
	const auto tiffRoundTrip = ReloadThrough<CLoadTiff>(_T("TIFF"), tiff);
	const auto photoshopRoundTrip =
		tiffRoundTrip.GetMetaData(IW::MetaDataTypes::PROFILE_PHOTOSHOP);
	IW_CHECK(photoshopRoundTrip.GetDataSize() == photoshop.size() &&
		std::equal(photoshop.begin(), photoshop.end(), photoshopRoundTrip.GetData()));
}

IW_TEST(NativePngAndTiffEncodingStopWhenCancelled)
{
	const auto image = MakeTrueColourImage(9, 5, IW::PixelFormat::PF24);
	for (const bool transcode : {false, true})
	{
		IW::Image source = image;
		if (transcode)
		{
			const auto encoded = native_record_fixture<CLoadPng>(source);
			IW::MetaData data(IW::MetaDataTypes::PNG_IMAGE, encoded.data(), static_cast<DWORD>(encoded.size()));
			source.SetMetaData(data);
		}
		bool cancelled{};
		iw::imaging::DecodeOptions options;
		options.cancelled = [&] { return cancelled; };
		options.progress = [&](int, int) { cancelled = true; };
		iw::imaging::OperationResult operation;
		iw::imaging::detail::OperationStatus status(options, operation);
		IW::SimpleBlob bytes;
		IW::StreamBlob<IW::SimpleBlob> output(bytes);
		CLoadPng png;
		IW_CHECK(!png.Write(L"PNG", &output, source, IW::CodecSettings(), &status));
		cancelled = false;
		CLoadTiff tiff;
		IW_CHECK(!tiff.Write(L"TIFF", &output, source, IW::CodecSettings(), &status));
	}
}

IW_TEST(NativeGifKeepsTwoBitPixelsAndLargerLaterPalettes)
{
	for (const auto format : {IW::PixelFormat::PF1, IW::PixelFormat::PF2})
	{
		IW::Image image;
		auto& first = image.CreatePage(9, 5, format);
		const int count = first.GetPixelFormat().NumberOfPaletteEntries();
		for (int i = 0; i < count; ++i) first.GetPalette()[i] = IW::RGBA(i * 63, 20, 200);
		{
			IW::IImageSurfaceLockPtr lock = first.GetSurfaceLock();
			COLORREF row[9]{};
			for (int x = 0; x < 9; ++x) row[x] = x % count;
			for (int y = 0; y < 5; ++y) lock->SetLine(row, y, 0, 9);
		}
		auto& later = image.CreatePage(CRect(0, 0, 9, 5), IW::PixelFormat::PF8);
		later.GetPalette()[200] = IW::RGBA(5, 120, 240);
		for (int y = 0; y < 5; ++y) std::memset(later.GetBitmapLine(y), 200, 9);
		IW::MetaData comment(IW::MetaDataTypes::PROFILE_COMMENT, "note", 4);
		image.SetMetaData(comment);
		const auto encoded = native_record_fixture<CLoadGif>(image);
		const BYTE extension[]{0x21, 0xfe, 4, 'n', 'o', 't', 'e', 0};
		IW_CHECK(std::search(encoded.begin(), encoded.end(), std::begin(extension), std::end(extension)) != encoded.end());
		const auto decoded = iw::imaging::decode_document(encoded, L".gif");
		IW_CHECK(static_cast<bool>(decoded));
		if (!decoded) continue;
		IW_CHECK_EQ(decoded.document.frames.size(), size_t{2});
		IW_CHECK(iw::imaging::frame_pixels(decoded, 0).pixels == iw::imaging::image_pixels(image, 0).pixels);
		IW_CHECK(iw::imaging::frame_pixels(decoded, 1).pixels == iw::imaging::image_pixels(image, 1).pixels);
	}
}

IW_TEST(TemporaryOutputCannotRecoverAfterAFailedWrite)
{
	struct ReadOnlyOutput final : IW::CFileTemp
	{
		bool opened{};
		ReadOnlyOutput()
		{
			_hFile = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			m_bCloseOnDelete = _hFile != INVALID_HANDLE_VALUE;
			opened = m_bCloseOnDelete;
		}
	};
	ReadOnlyOutput output;
	IW_CHECK(output.opened);
	if (!output.opened) return;
	const BYTE value = 42;
	DWORD written = 1;
	IW_CHECK(!output.Write(&value, 1, &written));
	IW_CHECK_EQ(written, 0u);
	IW_CHECK(!output.Write(nullptr, 0));
	IW_CHECK(!output.Close(IW::CNullStatus::Instance));
}

IW_TEST(IndexedTransparencySurvivesNativePngAndGifEncoding)
{
	for (const auto format : {IW::PixelFormat::PF8, IW::PixelFormat::PF8Alpha})
	{
		IW::Image image;
		auto& page = image.CreatePage(9, 5, format);
		for (int i = 0; i < 256; ++i) page.GetPalette()[i] = IW::RGBA(i, 100, 200);
		if (format == IW::PixelFormat::PF8Alpha) page.GetPalette()[2] &= 0x00ffffff;
		else page.SetTransparent(2);
		for (int y = 0; y < 5; ++y)
			for (int x = 0; x < 9; ++x) page.GetBitmapLine(y)[x] = static_cast<BYTE>(x % 3);
		const auto png = iw::imaging::decode_document(native_record_fixture<CLoadPng>(image), L".png");
		IW_CHECK(static_cast<bool>(png));
		if (png) IW_CHECK(iw::imaging::frame_pixels(png).pixels == iw::imaging::image_pixels(image).pixels);
		const auto gif = iw::imaging::decode_document(native_record_fixture<CLoadGif>(image), L".gif");
		IW_CHECK(static_cast<bool>(gif));
		if (gif) IW_CHECK(iw::imaging::frame_pixels(gif).pixels == iw::imaging::image_pixels(image).pixels);
	}
}

IW_TEST(QuantizationCancellationIsNotSuccessfulOutput)
{
	const auto image = MakeTrueColourImage(9, 5, IW::PixelFormat::PF24);
	IW::Image result;
	iw::imaging::DecodeOptions options;
	options.cancelled = [] { return true; };
	iw::imaging::OperationResult operation;
	iw::imaging::detail::OperationStatus status(options, operation);
	IW_CHECK(!IW::Quantize(image, result, &status));
	IW_CHECK(result.IsEmpty());
}

IW_TEST(XmpValuesStayUtf8AcrossLookupsAndPropertyListing)
{
	const char packet[] =
		"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
		"<rdf:Description xmlns:dc='http://purl.org/dc/elements/1.1/' xmlns:photoshop='http://ns.adobe.com/photoshop/1.0/'>"
		"<photoshop:Headline>caf\xc3\xa9</photoshop:Headline><dc:subject><rdf:Bag>"
		"<rdf:li>\xe6\x97\xa5\xe6\x9c\xac</rdf:li></rdf:Bag></dc:subject><dc:description><rdf:Alt>"
		"<rdf:li xml:lang='x-default'>na\xc3\xafve</rdf:li></rdf:Alt></dc:description>"
		"</rdf:Description></rdf:RDF></x:xmpmeta>";
	IW::MetaData data(IW::MetaDataTypes::PROFILE_XMP, packet, sizeof(packet) - 1);
	MetadataXMP metadata(data);
	CString value;
	IW_CHECK(metadata.Read(L"http://ns.adobe.com/photoshop/1.0/", L"Headline", value));
	IW_CHECK(value == L"caf\u00e9");
	IW_CHECK(metadata.ReadArray(L"http://purl.org/dc/elements/1.1/", L"subject", value));
	IW_CHECK(value == L"\u65e5\u672c");
	IW_CHECK(metadata.ReadAltText(L"http://purl.org/dc/elements/1.1/", L"description", value));
	IW_CHECK(value == L"na\u00efve");
	IW::MetadataProperties properties;
	metadata.Load(properties);
	const auto* headline = properties.Find(L"Headline");
	IW_CHECK(headline != nullptr);
	if (headline) IW_CHECK(headline->value == L"caf\u00e9");
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
			IW_CHECK_EQ(decoded.document.frames[0].width, 32u);
			IW_CHECK_EQ(decoded.document.frames[0].height, 48u);
		}
		auto turned = result;
		for (int i = 0; i < 3 && turned; ++i) turned = iw::imaging::transform_jpeg(turned.bytes, options);
		IW_CHECK(static_cast<bool>(turned));
		if (turned)
		{
			const auto before = iw::imaging::decode_document(source, L".jpg");
			const auto after = iw::imaging::decode_document(turned.bytes, L".jpg");
			IW_CHECK(static_cast<bool>(before) && static_cast<bool>(after));
			if (before && after) IW_CHECK(iw::imaging::frame_pixels(before).pixels == iw::imaging::frame_pixels(after).pixels);
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
	date.SetLocalClock(2026, 10, 17, 14, 32, 42);
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
	const auto actual = iw::imaging::frame_pixels(decoded);
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

// The 2.x property pages' EXIF listing: a section for each directory, each tag with
// libexif's name, title and description for the directory it is in and its value as
// text, and what libexif's defaults ignored - a tag it does not name - left out.
IW_TEST(ExifPropertiesListEachDirectoryAsLibexifNamedIt)
{
	std::vector<BYTE> block = {'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 0x2a, 0};
	const auto u16 = [&block](unsigned v) { block.push_back(static_cast<BYTE>(v)); block.push_back(static_cast<BYTE>(v >> 8)); };
	const auto u32 = [&](IW::UInt32 v) { u16(v & 0xffff); u16(v >> 16); };
	const auto entry = [&](unsigned tag, unsigned type, IW::UInt32 count, IW::UInt32 value) { u16(tag); u16(type); u32(count); u32(value); };

	// Offsets are from the TIFF header: IFD0 at 8, Make's text at 50, the Exif
	// directory at 56, its exposure at 86 and the GPS directory at 94.
	u32(8);
	u16(3);
	entry(0x010f, 2, 6, 50);
	entry(0x8769, 4, 1, 56);
	entry(0x8825, 4, 1, 94);
	u32(0);
	for (const char c : {'C', 'a', 'n', 'o', 'n', '\0'}) block.push_back(static_cast<BYTE>(c));
	u16(2);
	entry(0x829a, 5, 1, 86);
	entry(0xfde8, 3, 1, 1);
	u32(0);
	u32(1);
	u32(60);
	u16(1);
	entry(0x0001, 2, 2, 'N');
	u32(0);
	block.resize(block.size() + 16, 0);

	IW::MetadataProperties properties;
	MetadataExif(IW::MetaData(IW::MetaDataTypes::PROFILE_EXIF, block.data(), static_cast<DWORD>(block.size())))
		.Load(properties);

	IW_CHECK_EQ(properties.sections.size(), size_t{3});
	if (properties.sections.size() != 3) return;
	IW_CHECK(properties.sections[0].name == g_szIFD0);
	IW_CHECK(properties.sections[1].name == g_szIFD);
	IW_CHECK(properties.sections[2].name == g_szIFDGPS);

	const auto& make = properties.sections[0].properties;
	IW_CHECK(make.size() == 1 && make[0].key == _T("Make") && make[0].title == _T("Manufacturer") &&
		make[0].value == _T("Canon") && make[0].description.Left(44) == _T("The manufacturer of the recording equipment."));

	// The unknown tag beside the exposure is not listed.
	const auto& exposure = properties.sections[1].properties;
	IW_CHECK(exposure.size() == 1 && exposure[0].key == _T("ExposureTime") && exposure[0].title == _T("Exposure Time") &&
		exposure[0].value == _T("1/60 sec.") && exposure[0].description == _T("Exposure time, given in seconds (sec)."));

	// 0x0001 in the GPS directory is the latitude's hemisphere, not the Interoperability rule.
	const auto& gps = properties.sections[2].properties;
	IW_CHECK(gps.size() == 1 && gps[0].key == _T("GPSLatitudeRef") && gps[0].title == _T("North or South Latitude") &&
		gps[0].value == _T("N"));

	const auto* const found = properties.Find(_T("Exposure Time"));
	IW_CHECK(found && found->description == _T("Exposure time, given in seconds (sec)."));

	IW::MetadataProperties none;
	MetadataExif(IW::MetaData(IW::MetaDataTypes::PROFILE_EXIF)).Load(none);
	IW_CHECK(none.IsEmpty());
}

// The 2.x apps' XMP reads - a simple property, an array, alt-text - and the
// property view's listing, over libfiles' data model: whitespace around a value
// trimmed, English before the default, an alias read as its actual property, a
// struct shown as its fields, and a namespace the packet never declared absent.
IW_TEST(XmpReadsAndListsAPacketAsTheAppsUseIt)
{
	const char packet[] =
		"<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
		"<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
		" xmlns:photoshop=\"http://ns.adobe.com/photoshop/1.0/\" xmlns:tiff=\"http://ns.adobe.com/tiff/1.0/\""
		" xmlns:exif=\"http://ns.adobe.com/exif/1.0/\" photoshop:City=\" Paris \">"
		"<photoshop:Headline>Eiffel</photoshop:Headline>"
		"<dc:subject><rdf:Bag><rdf:li>tower</rdf:li><rdf:li>iron; steel</rdf:li></rdf:Bag></dc:subject>"
		"<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">Tower</rdf:li>"
		"<rdf:li xml:lang=\"en\">The tower</rdf:li><rdf:li xml:lang=\"fr\">La tour</rdf:li></rdf:Alt></dc:title>"
		"<dc:rights><rdf:Alt><rdf:li xml:lang=\"x-default\">Tower Ltd</rdf:li>"
		"<rdf:li xml:lang=\"en\">Other</rdf:li></rdf:Alt></dc:rights>"
		"<tiff:ImageDescription>At dusk</tiff:ImageDescription>"
		"<exif:Flash rdf:parseType=\"Resource\"><exif:Fired>True</exif:Fired><exif:Mode>1</exif:Mode></exif:Flash>"
		"</rdf:Description></rdf:RDF></x:xmpmeta>";
	const MetadataXMP xmp(IW::MetaData(IW::MetaDataTypes::PROFILE_XMP, reinterpret_cast<const BYTE*>(packet),
	                                   static_cast<DWORD>(sizeof packet - 1)));
	const CString dc = _T("http://purl.org/dc/elements/1.1/");
	const CString photoshop = _T("http://ns.adobe.com/photoshop/1.0/");

	CString value;
	IW_CHECK(xmp.Read(photoshop, _T("Headline"), value) && value == _T("Eiffel"));
	IW_CHECK(xmp.Read(photoshop, _T("City"), value) && value == _T("Paris"));
	IW_CHECK(xmp.ReadArray(dc, _T("subject"), value) && value == _T("tower; \"iron; steel\""));
	IW_CHECK(!xmp.Read(dc, _T("subject"), value));
	IW_CHECK(xmp.ReadAltText(dc, _T("title"), value) && value == _T("The tower"));
	// A default and one language are read alike, as Adobe's toolkit reads them: older
	// Adobe applications read only the default, so its text is the language's too.
	IW_CHECK(xmp.ReadAltText(dc, _T("rights"), value) && value == _T("Tower Ltd"));
	IW_CHECK(xmp.ReadAltText(dc, _T("description"), value) && value == _T("At dusk"));
	IW_CHECK(!xmp.Read(_T("http://example.com/never/declared/"), _T("Anything"), value));

	IW::MetadataProperties properties;
	xmp.Load(properties);
	const auto* const title = properties.Find(_T("title"));
	IW_CHECK(title && title->value == _T("The tower (en)"));
	const auto* const flash = properties.Find(_T("Flash"));
	IW_CHECK(flash && flash->value == _T("True; 1"));
	const auto* const subject = properties.Find(_T("subject"));
	IW_CHECK(subject && subject->value == _T("tower; \"iron; steel\""));

	bool dcSection = false, tiffSection = false;
	for (const auto& section : properties.sections)
	{
		dcSection |= section.name == _T("dc (http://purl.org/dc/elements/1.1/)");
		tiffSection |= section.name.Find(_T("tiff")) == 0;
	}
	IW_CHECK(dcSection);
	IW_CHECK(!tiffSection);

	IW::MetadataProperties none;
	MetadataXMP(IW::MetaData(IW::MetaDataTypes::PROFILE_XMP)).Load(none);
	IW_CHECK(none.IsEmpty());
}

IW_TEST(IptcReadsAndListsTheDatasetsAsTheAppsUseThem)
{
	std::vector<BYTE> iim(4, 0); // padding some exporters write before the first record
	const auto dataset = [&iim](const BYTE record, const BYTE number, const std::string_view value)
	{
		iim.insert(iim.end(), {0x1C, record, number, static_cast<BYTE>(value.size() >> 8), static_cast<BYTE>(value.size())});
		iim.insert(iim.end(), value.begin(), value.end());
	};

	dataset(1, 90, "\x1B%G");
	dataset(2, 0, std::string_view("\0\x04", 2));
	dataset(2, 105, "Eiffel");
	dataset(2, 25, "tower");
	dataset(2, 25, "Z\xC3\xBCrich");
	dataset(2, 80, "O\x92" "Brien"); // Windows-1252's apostrophe
	dataset(2, 116, std::string_view("Ann\0\0", 5));
	dataset(2, 90, std::string_view("\0\0", 2));
	dataset(2, 202, std::string_view("\xFF\xD8\xFF\xD9", 4)); // a preview, binary
	dataset(2, 250, "mystery");
	// IIM 4.1: with the size field's top bit set, its low bits count the octets that hold the size.
	iim.insert(iim.end(), {0x1C, 2, 120, 0x80, 0x02, 0x00, 0x05, 'H', 'e', 'l', 'l', 'o'});

	// As a JPEG carries it: the Photoshop segment, the records in its resource 0x0404.
	std::vector<BYTE> segment = {'P', 'h', 'o', 't', 'o', 's', 'h', 'o', 'p', ' ', '3', '.', '0', 0,
	                             '8', 'B', 'I', 'M', 0x04, 0x04, 0, 0};
	const auto size = static_cast<DWORD>(iim.size());
	segment.insert(segment.end(), {static_cast<BYTE>(size >> 24), static_cast<BYTE>(size >> 16),
	                               static_cast<BYTE>(size >> 8), static_cast<BYTE>(size)});
	segment.insert(segment.end(), iim.begin(), iim.end());
	if (size & 1) segment.push_back(0);

	const MetadataIPTC iptc(IW::MetaData(IW::MetaDataTypes::PROFILE_IPTC, segment.data(), static_cast<DWORD>(segment.size())));

	CString value;
	IW_CHECK(iptc.Read(2, 105, value) && value == _T("Eiffel"));
	IW_CHECK(iptc.Read(2, 25, value) && value == L"tower; Z\u00FCrich");
	IW_CHECK(iptc.Read(2, 80, value) && value == L"O\u2019Brien");
	IW_CHECK(iptc.Read(2, 116, value) && value == _T("Ann") && value.GetLength() == 3);
	IW_CHECK(iptc.Read(2, 120, value) && value == _T("Hello"));
	IW_CHECK(iptc.Read(2, 90, value) && value.IsEmpty());

	IW::MetadataProperties properties;
	iptc.Load(properties);
	IW_CHECK(!properties.sections.empty() && properties.sections.front().name == g_szCaption);
	IW_CHECK(!properties.sections.empty() && properties.sections.back().name == _T("Other"));
	const auto* const version = properties.Find(MetadataIPTC::GetKey(2, 0));
	IW_CHECK(version && version->value == _T("4"));
	const auto* const keywords = properties.Find(MetadataIPTC::GetKey(2, 25));
	IW_CHECK(keywords && keywords->value == L"tower; Z\u00FCrich");
	const auto* const unknown = properties.Find(MetadataIPTC::GetKey(2, 250));
	IW_CHECK(unknown && unknown->value == _T("mystery"));
	// A dataset of nothing but padding, and a binary one, are not shown.
	IW_CHECK(!properties.Find(MetadataIPTC::GetKey(2, 90)));
	IW_CHECK(!properties.Find(MetadataIPTC::GetKey(2, 202)));

	// As a TIFF carries it: the bare records.
	const MetadataIPTC bare(IW::MetaData(IW::MetaDataTypes::PROFILE_IPTC, iim.data() + 4, static_cast<DWORD>(iim.size() - 4)));
	IW_CHECK(bare.Read(2, 120, value) && value == _T("Hello"));

	IW::MetadataProperties none;
	MetadataIPTC(IW::MetaData(IW::MetaDataTypes::PROFILE_IPTC)).Load(none);
	IW_CHECK(none.IsEmpty());
}

IW_TEST(IptcIsFoundAmongPhotoshopResourcesAsLibfilesFindsIt)
{
	std::vector<BYTE> iim;
	const auto dataset = [&iim](const BYTE number, const std::string_view value)
	{
		iim.insert(iim.end(), {0x1C, 2, number, static_cast<BYTE>(value.size() >> 8), static_cast<BYTE>(value.size())});
		iim.insert(iim.end(), value.begin(), value.end());
	};

	dataset(5, "Eiffel");
	dataset(25, "tower");

	const auto resource = [](std::vector<BYTE>& chain, const WORD id, const std::vector<BYTE>& data)
	{
		const auto size = static_cast<DWORD>(data.size());
		chain.insert(chain.end(), {'8', 'B', 'I', 'M', static_cast<BYTE>(id >> 8), static_cast<BYTE>(id), 0, 0,
		                           static_cast<BYTE>(size >> 24), static_cast<BYTE>(size >> 16),
		                           static_cast<BYTE>(size >> 8), static_cast<BYTE>(size)});
		chain.insert(chain.end(), data.begin(), data.end());
		if (size & 1) chain.push_back(0);
	};

	// A thumbnail whose bytes hold a 1C 02, which a search for the records once
	// started at; an empty IPTC resource, which a walk once stopped at; then the
	// one that holds the records.
	std::vector<BYTE> chain;
	resource(chain, 0x0409, {0xFF, 0xD8, 0x1C, 0x02, 0x05, 0x00, 0x03, 'b', 'a', 'd', 0xFF, 0xD9});
	resource(chain, 0x0404, {});
	resource(chain, 0x0404, iim);
	resource(chain, 0x03ED, std::vector<BYTE>(16, 0));

	// As a JPEG's loader keeps it: the APP13 segment.
	std::vector<BYTE> segment = {'P', 'h', 'o', 't', 'o', 's', 'h', 'o', 'p', ' ', '3', '.', '0', 0};
	segment.insert(segment.end(), chain.begin(), chain.end());
	const IW::MetaData blob(IW::MetaDataTypes::PROFILE_IPTC, segment.data(), static_cast<DWORD>(segment.size()));
	IW_CHECK(MetadataIPTC::GetIim(blob) == iim);

	CString value;
	IW_CHECK(MetadataIPTC(blob).Read(2, 5, value) && value == _T("Eiffel"));

	const auto bytesOf = [](const IW::MetaData& data)
	{
		return data.IsEmpty() ? std::vector<BYTE>() : std::vector<BYTE>(data.GetData(), data.GetData() + data.GetDataSize());
	};

	// A TIFF saves the records in its Photoshop tag, and reads them back.
	IW::Image image = MakeTrueColourImage(8, 8, IW::PixelFormat::PF24);
	image.SetMetaData(blob);
	const IW::Image tiff = ReloadThrough<CLoadTiff>(_T("TIF"), image);
	IW_CHECK(!tiff.IsEmpty());
	IW_CHECK(bytesOf(tiff.GetMetaData(IW::MetaDataTypes::PROFILE_IPTC)) == iim);

	// A PSD's loader reads the records from its resources, and nothing after them.
	std::vector<BYTE> psd(26, 0);
	memcpy(psd.data(), "8BPS", 4);
	psd[5] = 1;   // PSD version.
	psd[13] = 3;  // RGB planes.
	psd[17] = 1;  // One row.
	psd[21] = 1;  // One column.
	psd[23] = 8;
	psd[25] = 3;  // RGB.
	const auto be32 = [&psd](const DWORD v)
	{
		psd.insert(psd.end(), {static_cast<BYTE>(v >> 24), static_cast<BYTE>(v >> 16), static_cast<BYTE>(v >> 8), static_cast<BYTE>(v)});
	};
	be32(0); // No colour map.
	be32(static_cast<DWORD>(chain.size()));
	psd.insert(psd.end(), chain.begin(), chain.end());
	be32(0); // No layers.
	psd.insert(psd.end(), {0, 0, 0x80, 0x40, 0x20}); // Uncompressed: one sample a plane.

	IW::StreamConstBlob stream(psd.data(), psd.size());
	IW::Image loaded;
	IW::ImageStream<IW::IImageStream> output(loaded);
	CLoadPsd loader;
	IW_CHECK(loader.Read(_T("PSD"), &stream, &output, IW::CNullStatus::Instance));
	IW_CHECK(bytesOf(loaded.GetMetaData(IW::MetaDataTypes::PROFILE_IPTC)) == iim);
}

IW_TEST(ExifSettingsAreReadWhereverTheDirectoriesLie)
{
	// IFD 0 after the Exif directory it points to, as some writers lay a block out. The
	// parser this replaced took the pointer back for a loop and read nothing past it.
	std::vector<BYTE> tiff = {'I', 'I', 42, 0, 0, 0, 0, 0};
	const auto u16 = [&tiff](const unsigned v)
	{
		tiff.push_back(static_cast<BYTE>(v));
		tiff.push_back(static_cast<BYTE>(v >> 8));
	};
	const auto u32 = [&u16](const unsigned v)
	{
		u16(v & 0xffff);
		u16(v >> 16);
	};
	const auto entry = [&u16, &u32](const unsigned tag, const unsigned type, const unsigned count, const unsigned value)
	{
		u16(tag);
		u16(type);
		u32(count);
		u32(value);
	};

	const unsigned exifDirectory = 8, entries = 7, values = exifDirectory + 2 + entries * 12 + 4;
	u16(entries);
	entry(0x829a, 5, 1, values);        // ExposureTime
	entry(0x829d, 5, 1, values + 8);    // FNumber, which ApertureValue's absence does not hide
	entry(0x8827, 3, 1, 400);           // ISOSpeedRatings
	entry(0x9003, 2, 20, values + 24);  // DateTimeOriginal
	entry(0x920a, 5, 1, values + 16);   // FocalLength
	entry(0xa002, 4, 1, 0);             // PixelXDimension
	entry(0xa003, 4, 1, 0);             // PixelYDimension
	u32(0);
	u32(1);
	u32(250);
	u32(28);
	u32(10);
	u32(50);
	u32(1);
	const char date[20] = "2024:05:06 14:30:15";
	tiff.insert(tiff.end(), date, date + 20);
	const auto ifd0 = static_cast<unsigned>(tiff.size());
	u16(2);
	entry(0x0112, 3, 1, 6);             // Orientation
	entry(0x8769, 4, 1, exifDirectory);
	u32(0);
	tiff[4] = static_cast<BYTE>(ifd0);
	tiff[5] = static_cast<BYTE>(ifd0 >> 8);

	std::vector<BYTE> exif = {'E', 'x', 'i', 'f', 0, 0};
	exif.insert(exif.end(), tiff.begin(), tiff.end());
	const auto size = static_cast<unsigned>(exif.size());

	IW::CameraSettings settings;
	Exif::Parse(&settings, exif.data(), size);
	IW_CHECK_EQ(static_cast<int>(settings.Orientation), 6);
	IW_CHECK(settings.FormatAperture() == _T("f/2.8"));
	IW_CHECK(settings.FormatExposureTime() == _T("1/250 s"));
	IW_CHECK_EQ(static_cast<int>(settings.IsoSpeed), 400);
	IW_CHECK(settings.FormatFocalLength() == _T("50.0 mm"));
	IW_CHECK_EQ(static_cast<int>(settings.DateTaken.ToSystemTime().wYear), 2024);
	IW_CHECK_EQ(Exif::ReadOrientation(exif.data(), size), 6);

	Exif::FixExif(exif.data(), size, 640, 480);
	Exif::SetOrientation(exif.data(), size, IW::Orientation::TopLeft);
	const auto value = [&exif](const unsigned at) { return exif[6 + at] | exif[6 + at + 1] << 8; };
	IW_CHECK_EQ(value(exifDirectory + 2 + 5 * 12 + 8), 640);
	IW_CHECK_EQ(value(exifDirectory + 2 + 6 * 12 + 8), 480);
	IW_CHECK_EQ(Exif::ReadOrientation(exif.data(), size), static_cast<int>(IW::Orientation::TopLeft));

	// A block that is not EXIF is left as it was, and says nothing.
	std::vector<BYTE> other = {'J', 'F', 'I', 'F', 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0, 0, 0};
	const auto before = other;
	Exif::FixExif(other.data(), static_cast<unsigned>(other.size()), 1, 1);
	IW_CHECK(other == before);
	IW_CHECK_EQ(Exif::ReadOrientation(other.data(), static_cast<unsigned>(other.size())),
	            static_cast<int>(IW::Orientation::TopLeft));
}

IW_TEST(ExifDateTakenFollowsLibfilesDateRule)
{
	// IFD 0 pointing to an Exif directory with DateTimeOriginal and, when given, OffsetTimeOriginal.
	const auto taken = [](const std::string& original, const std::string& offset)
	{
		std::vector<BYTE> tiff = {'I', 'I', 42, 0, 8, 0, 0, 0};
		const auto u16 = [&tiff](const unsigned v)
		{
			tiff.push_back(static_cast<BYTE>(v));
			tiff.push_back(static_cast<BYTE>(v >> 8));
		};
		const auto u32 = [&u16](const unsigned v)
		{
			u16(v & 0xffff);
			u16(v >> 16);
		};
		const auto entry = [&u16, &u32](const unsigned tag, const unsigned type, const unsigned count, const unsigned value)
		{
			u16(tag);
			u16(type);
			u32(count);
			u32(value);
		};

		const unsigned exifDirectory = 8 + 2 + 12 + 4;
		const unsigned entries = offset.empty() ? 1 : 2;
		const unsigned values = exifDirectory + 2 + entries * 12 + 4;
		const auto originalCount = static_cast<unsigned>(original.size() + 1);
		u16(1);
		entry(0x8769, 4, 1, exifDirectory);
		u32(0);
		u16(entries);
		entry(0x9003, 2, originalCount, values);                                                 // DateTimeOriginal
		if (!offset.empty())
			entry(0x9011, 2, static_cast<unsigned>(offset.size() + 1), values + originalCount); // OffsetTimeOriginal
		u32(0);
		tiff.insert(tiff.end(), original.begin(), original.end());
		tiff.push_back(0);
		if (!offset.empty())
		{
			tiff.insert(tiff.end(), offset.begin(), offset.end());
			tiff.push_back(0);
		}

		std::vector<BYTE> exif = {'E', 'x', 'i', 'f', 0, 0};
		exif.insert(exif.end(), tiff.begin(), tiff.end());
		IW::CameraSettings settings;
		Exif::Parse(&settings, exif.data(), static_cast<unsigned>(exif.size()));
		return settings.DateTaken;
	};

	// The camera's clock time, whatever offset the block states for it.
	const auto zoned = taken("2024:05:06 14:30:15", "+09:00").ToLocalTime().ToSystemTime();
	IW_CHECK(zoned.wYear == 2024 && zoned.wMonth == 5 && zoned.wDay == 6);
	IW_CHECK(zoned.wHour == 14 && zoned.wMinute == 30 && zoned.wSecond == 15);

	// The rule Diffractor reads by, which libfiles now holds: all six fields, the seconds a real number and
	// whatever follows them ignored, as the scanf it emulates reads them; and a camera's unset date is none.
	IW_CHECK(taken("2024:05:06 14:30", "").IsEmpty());
	const auto trailing = taken("2024:05:06 14:30:15 at noon", "").ToLocalTime().ToSystemTime();
	IW_CHECK(trailing.wYear == 2024 && trailing.wHour == 14 && trailing.wMinute == 30 && trailing.wSecond == 15);
	IW_CHECK(taken("0000:00:00 00:00:00", "").IsEmpty());
}

namespace
{
	IW::Image MakeIndexedAlphaFixture(bool paletteAlpha)
	{
		IW::Image image;
		auto& page = image.CreatePage(9, 7, paletteAlpha ? IW::PixelFormat::PF8Alpha : IW::PixelFormat::PF8);
		std::fill_n(page.GetPalette(), 256, IW::RGBA(30, 60, 90));
		page.GetPalette()[0] = IW::RGBA(240, 20, 200, paletteAlpha ? 0 : 255);
		page.GetPalette()[1] = IW::RGBA(40, 80, 120, paletteAlpha ? 128 : 255);
		if (!paletteAlpha) page.SetTransparent(0);
		for (int y = 0; y < 7; ++y)
			for (int x = 0; x < 9; ++x) page.GetBitmapLine(y)[x] = static_cast<BYTE>((x + y) % 3);
		return image;
	}

	IW::Image BgraFixture(const IW::Image& source)
	{
		const auto pixels = iw::imaging::image_pixels(source);
		IW::Image image;
		auto& page = image.CreatePage(pixels.width, pixels.height, IW::PixelFormat::PF32Alpha);
		IW::IImageSurfaceLockPtr lock = page.GetSurfaceLock();
		std::vector<COLORREF> line(static_cast<size_t>(pixels.width));
		for (int y = 0; y < pixels.height; ++y)
		{
			std::copy_n(pixels.pixels.data() + static_cast<size_t>(y) * pixels.width, pixels.width, line.begin());
			lock->SetLine(line.data(), y, 0, pixels.width);
		}
		return image;
	}
}

IW_TEST(FilteredScaleNormalizesNegativeIndicesAndRejectsOverflowingBuffers)
{
	const auto source = MakeTrueColourImage(9, 7, IW::PixelFormat::PF24);
	IW::Image reference;
	IW_CHECK(IW::Scale(CSize(5, 4), 0, source, reference, IW::CNullStatus::Instance));
	for (const int filter : {-1, INT_MIN})
	{
		IW::Image result;
		IW_CHECK(IW::Scale(CSize(5, 4), filter, source, result, IW::CNullStatus::Instance));
		IW_CHECK(iw::imaging::image_pixels(result).pixels == iw::imaging::image_pixels(reference).pixels);
	}
	for (const auto size : {CSize(0, 4), CSize(-1, 4), CSize(INT_MAX, INT_MAX), CSize(50000, 50000)})
	{
		IW::Image filtered, fast;
		IW_CHECK(!IW::Scale(size, 3, source, filtered, IW::CNullStatus::Instance));
		IW_CHECK(!IW::Scale(source, fast, size, IW::CNullStatus::Instance));
		IW_CHECK(filtered.IsEmpty() && fast.IsEmpty());
	}
	IW::Image narrow, result;
	narrow.CreatePage(1, 65536, IW::PixelFormat::PF1);
	IW_CHECK(!IW::Scale(CSize(65536, 1), 3, narrow, result, IW::CNullStatus::Instance));
	IW_CHECK(result.IsEmpty());
}

IW_TEST(IndexedAlphaScalingMatchesStraightBgraAndKeepsTransparency)
{
	for (const bool paletteAlpha : {false, true})
	{
		auto indexed = MakeIndexedAlphaFixture(paletteAlpha);
		indexed.GetFirstPage().SetPageOffset(3, 4);
		const auto bgra = BgraFixture(indexed);
		for (const auto size : {CSize(5, 4), CSize(18, 14)})
		{
			for (const bool filtered : {false, true})
			{
				IW::Image indexedResult, bgraResult;
				if (filtered)
				{
					IW_CHECK(IW::Scale(size, 3, indexed, indexedResult, IW::CNullStatus::Instance));
					IW_CHECK(IW::Scale(size, 3, bgra, bgraResult, IW::CNullStatus::Instance));
				}
				else
				{
					IW_CHECK(IW::Scale(indexed, indexedResult, size, IW::CNullStatus::Instance));
					IW_CHECK(IW::Scale(bgra, bgraResult, size, IW::CNullStatus::Instance));
				}
				const auto actual = iw::imaging::image_pixels(indexedResult);
				IW_CHECK(actual.pixels == iw::imaging::image_pixels(bgraResult).pixels);
				IW_CHECK(std::ranges::any_of(actual.pixels, [](auto pixel) { return (pixel >> 24) > 0; }));
				IW_CHECK(std::ranges::any_of(actual.pixels, [](auto pixel) { return (pixel >> 24) < 255; }));
			}
		}
	}
}

IW_TEST(IndexedAlphaWarpMatchesStraightBgra)
{
	for (const bool paletteAlpha : {false, true})
	{
		const auto indexed = MakeIndexedAlphaFixture(paletteAlpha);
		const auto bgra = BgraFixture(indexed);
		ImageEdits edits;
		edits._straighten = 30;
		edits._perspectiveH = 10;
		IW::Image indexedResult, bgraResult;
		IW_CHECK(IW::ApplyEditGeometry(indexed, indexedResult, edits, IW::CNullStatus::Instance));
		IW_CHECK(IW::ApplyEditGeometry(bgra, bgraResult, edits, IW::CNullStatus::Instance));
		const auto actual = iw::imaging::image_pixels(indexedResult);
		IW_CHECK(actual.pixels == iw::imaging::image_pixels(bgraResult).pixels);
		IW_CHECK(std::ranges::any_of(actual.pixels, [](auto pixel) { return (pixel >> 24) > 0; }));
		IW_CHECK(std::ranges::any_of(actual.pixels, [](auto pixel) { return (pixel >> 24) < 255; }));
	}
}

IW_TEST(RenderImageKeepsIndexedTransparencyAtEveryScale)
{
	for (const auto format : {IW::PixelFormat::PF8, IW::PixelFormat::PF8Alpha})
	{
		IW::Image image;
		auto& page = image.CreatePage(8, 8, format);
		std::fill_n(page.GetPalette(), 256, IW::RGBA(240, 0, 180));
		if (format == IW::PixelFormat::PF8Alpha) page.GetPalette()[0] &= 0x00ffffff;
		else page.SetTransparent(0);
		for (int y = 0; y < 8; ++y) std::memset(page.GetBitmapLine(y), 0, 8);
		for (const auto destination : {CRect(2, 3, 10, 11), CRect(0, 0, 16, 16), CRect(4, 4, 8, 8)})
		{
			TestCanvas canvas;
			const auto before = canvas.bits;
			CBlitter blitter;
			RenderImage<TestCanvas, CBlitter> render(canvas, blitter);
			render.DrawImage(page, destination, CRect(0, 0, 8, 8));
			IW_CHECK(canvas.bits == before);
		}
	}
}

IW_TEST(RenderImageCompositesUpscaledAlphaAgainstEachCanvasRow)
{
	IW::Image image;
	auto& page = image.CreatePage(8, 8, IW::PixelFormat::PF8Alpha);
	std::fill_n(page.GetPalette(), 256, IW::RGBA(100, 50, 150, 128));
	for (int y = 0; y < 8; ++y) std::memset(page.GetBitmapLine(y), 0, 8);
	for (const auto destination : {CRect(2, 3, 10, 11), CRect(0, 0, 16, 16), CRect(4, 4, 8, 8)})
	{
		TestCanvas canvas;
		for (int y = 0; y < TestCanvas::cy; ++y)
			for (int x = 0; x < TestCanvas::cx; ++x)
				canvas.bits[y * TestCanvas::cx + x] = IW::RGBA(10 + x, 20 + y, 60 + x + y);
		const auto before = canvas.bits;
		CBlitter blitter;
		RenderImage<TestCanvas, CBlitter> render(canvas, blitter);
		render.DrawImage(page, destination, CRect(0, 0, 8, 8));
		for (int y = 0; y < TestCanvas::cy; ++y)
			for (int x = 0; x < TestCanvas::cx; ++x)
			{
				const auto index = y * TestCanvas::cx + x;
				if (!destination.PtInRect(CPoint(x, y))) IW_CHECK_EQ(canvas.bits[index], before[index]);
				else
				{
					const auto blend = [](int foreground, int background) { return (foreground * 128 + background * 127 + 127) / 255; };
					const auto expected = IW::RGBA(blend(100, IW::GetR(before[index])),
						blend(50, IW::GetG(before[index])), blend(150, IW::GetB(before[index])));
					const int tolerance = destination.Width() == 8 && destination.Height() == 8 ? 0 : 1;
					IW_CHECK(std::abs(IW::GetR(canvas.bits[index]) - IW::GetR(expected)) <= tolerance);
					IW_CHECK(std::abs(IW::GetG(canvas.bits[index]) - IW::GetG(expected)) <= tolerance);
					IW_CHECK(std::abs(IW::GetB(canvas.bits[index]) - IW::GetB(expected)) <= tolerance);
					IW_CHECK_EQ(IW::GetA(canvas.bits[index]), 255);
				}
			}
	}
	for (const auto size : {CSize(8, 8), CSize(16, 16), CSize(4, 4)})
	{
		IW::Image scaled;
		IW_CHECK(IW::Scale(image, scaled, size, IW::CNullStatus::Instance));
		const auto pixels = iw::imaging::image_pixels(scaled);
		IW_CHECK(std::ranges::all_of(pixels.pixels, [](auto pixel) { return (pixel >> 24) == 128; }));
	}
}

IW_TEST(NonQuarterRotationsPreserveIndexedAndStraightAlpha)
{
	for (const bool paletteAlpha : {false, true})
	{
		const auto indexed = MakeIndexedAlphaFixture(paletteAlpha);
		const auto bgra = BgraFixture(indexed);
		for (const float angle : {-17.0f, 17.0f, 45.0f, 95.0f, 355.0f})
		{
			IW::Image indexedResult, bgraResult;
			IW_CHECK(IW::Rotate(indexed, indexedResult, angle, IW::CNullStatus::Instance));
			IW_CHECK(IW::Rotate(bgra, bgraResult, angle, IW::CNullStatus::Instance));
			const auto actual = iw::imaging::image_pixels(indexedResult);
			IW_CHECK(indexedResult.GetFirstPage().GetPixelFormat() == IW::PixelFormat::PF32Alpha);
			IW_CHECK(actual.pixels == iw::imaging::image_pixels(bgraResult).pixels);
			IW_CHECK(std::ranges::any_of(actual.pixels, [](auto pixel) { return (pixel >> 24) > 0; }));
			IW_CHECK(std::ranges::any_of(actual.pixels, [](auto pixel) { return (pixel >> 24) < 255; }));
		}
	}
	for (const int alpha : {0, 128})
	{
		const auto image = MakeAlphaImage(9, 7, alpha);
		IW::Image rotated;
		IW_CHECK(IW::Rotate(image, rotated, 17.0f, IW::CNullStatus::Instance));
		const auto pixels = iw::imaging::image_pixels(rotated);
		IW_CHECK(std::ranges::all_of(pixels.pixels,
			[alpha](auto pixel) { return (pixel >> 24) <= static_cast<std::uint32_t>(alpha); }));
		if (alpha) IW_CHECK(std::ranges::any_of(pixels.pixels, [](auto pixel) { return (pixel >> 24) > 0; }));
	}
}

IW_TEST(CompoundDocumentsRejectIncompleteInputAndKeepCompletePreviews)
{
	const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	IW_CHECK(SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE);
	if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return;
	struct Apartment
	{
		HRESULT initialized;
		~Apartment() { if (SUCCEEDED(initialized)) CoUninitialize(); }
	} apartment{initialized};
	CComPtr<ILockBytes> bytes;
	IW_CHECK(SUCCEEDED(CreateILockBytesOnHGlobal(nullptr, TRUE, &bytes)));
	if (!bytes) return;
	CComPtr<IStorage> storage;
	IW_CHECK(SUCCEEDED(StgCreateDocfileOnILockBytes(bytes,
		STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &storage)));
	if (!storage) return;
	CComPtr<IPropertySetStorage> sets;
	IW_CHECK(SUCCEEDED(storage->QueryInterface(IID_PPV_ARGS(&sets))));
	if (!sets) return;
	CComPtr<IPropertyStorage> properties;
	IW_CHECK(SUCCEEDED(sets->Create(FMTID_SummaryInformation, nullptr, PROPSETFLAG_DEFAULT,
		STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, &properties)));
	if (!properties) return;
	const HDC dc = CreateMetaFileW(nullptr);
	IW_CHECK(dc != nullptr);
	if (!dc) return;
	const RECT rectangle{0, 0, 640, 480};
	FillRect(dc, &rectangle, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
	const HMETAFILE metafile = CloseMetaFile(dc);
	IW_CHECK(metafile != nullptr);
	if (!metafile) return;
	struct Metafile
	{
		HMETAFILE handle;
		~Metafile() { DeleteMetaFile(handle); }
	} cleanup{metafile};
	const auto metafileSize = GetMetaFileBitsEx(metafile, 0, nullptr);
	std::vector<BYTE> thumbnail(12 + metafileSize);
	IW_CHECK(GetMetaFileBitsEx(metafile, metafileSize, thumbnail.data() + 12) == metafileSize);
	const DWORD format = CF_METAFILEPICT;
	std::memcpy(thumbnail.data(), &format, sizeof(format));
	CLIPDATA clip{static_cast<ULONG>(thumbnail.size() + sizeof(LONG)), -1, thumbnail.data()};
	PROPVARIANT value{};
	value.vt = VT_CF;
	// WriteMultiple borrows these stack-owned clipboard bytes.
	value.pclipdata = &clip;
	PROPSPEC spec{};
	spec.ulKind = PRSPEC_PROPID;
	spec.propid = PIDSI_THUMBNAIL;
	IW_CHECK(SUCCEEDED(properties->WriteMultiple(1, &spec, &value, PID_FIRST_USABLE)));
	IW_CHECK(SUCCEEDED(properties->Commit(STGC_DEFAULT)));
	IW_CHECK(SUCCEEDED(storage->Commit(STGC_DEFAULT)));
	properties.Release();
	sets.Release();
	storage.Release();
	STATSTG stat{};
	IW_CHECK(SUCCEEDED(bytes->Stat(&stat, STATFLAG_NONAME)));
	HGLOBAL memory{};
	IW_CHECK(SUCCEEDED(GetHGlobalFromILockBytes(bytes, &memory)));
	const auto* input = static_cast<const BYTE*>(GlobalLock(memory));
	IW_CHECK(input != nullptr);
	if (!input) return;
	const std::vector<BYTE> encoded(input, input + static_cast<size_t>(stat.cbSize.QuadPart));
	GlobalUnlock(memory);
	const auto decoded = iw::imaging::decode_document(encoded, L".ppt");
	IW_CHECK(static_cast<bool>(decoded));
	for (const bool zero_budget : {false, true})
	{
		iw::imaging::DecodeOptions invalid;
		if (zero_budget) invalid.maximumBytes = 0;
		else invalid.maximumWidth = -1;
		const auto refused = iw::imaging::decode_document(encoded, L".ppt", invalid);
		IW_CHECK(refused.error == iw::imaging::ImageError::invalid_input);
		IW_CHECK(refused.document.frames.empty());
	}
	if (decoded)
	{
		IW_CHECK_EQ(decoded.document.frames.size(), size_t{1});
		IW_CHECK_EQ(decoded.document.frames[0].width, 640u);
		IW_CHECK(std::ranges::any_of(iw::imaging::frame_pixels(decoded).pixels,
			[](auto pixel) { return pixel == 0xff000000; }));
	}
	{
		IW::StreamConstBlob source(encoded.data(), IW::FileSize(encoded.size()));
		IW::Image image;
		IW::ImageStreamCore<IW::IImageStream> output(image, false);
		CLoadCompoundDoc loader;
		IW_CHECK(loader.Read(L"PPT", &source, &output, IW::CNullStatus::Instance));
		IW_CHECK(!image.IsEmpty() && image.GetFirstPage().GetPixelFormat() == IW::PixelFormat::PF32);
	}
	struct Source final : IW::StreamConstBlob
	{
		int mode{};
		using StreamConstBlob::StreamConstBlob;
		IW::FileSize GetFileSize() override
		{
			const auto actual = GetMemory().size();
			return IW::FileSize(mode == 0 ? actual + 16 : mode == 3 ? (1ull << 32) + actual : actual);
		}
		bool Read(LPVOID target, DWORD count, LPDWORD read = nullptr) override
		{
			const bool success = StreamConstBlob::Read(target, count, read);
			return mode != 1 && success;
		}
		DWORD Seek(ePosition position, LONG offset) override
		{
			return mode == 2 ? INVALID_SET_FILE_POINTER : StreamConstBlob::Seek(position, offset);
		}
	};
	for (int mode = 0; mode < 4; ++mode)
	{
		Source source(encoded.data(), IW::FileSize(encoded.size()));
		source.mode = mode;
		IW::Image image;
		IW::ImageStreamCore<IW::IImageStream> output(image, false);
		iw::imaging::DecodeOptions options;
		iw::imaging::OperationResult result;
		iw::imaging::detail::OperationStatus status(options, result);
		CLoadCompoundDoc loader;
		IW_CHECK(!loader.Read(L"PPT", &source, &output, &status));
		IW_CHECK(result.message == (mode == 3 ? L"The compound document is empty or exceeds the input limit." :
			L"The compound document could not be read completely."));
		IW_CHECK(result.error == (mode == 3 ? iw::imaging::ImageError::resource_limit :
			iw::imaging::ImageError::read_failed));
		IW_CHECK(image.IsEmpty());
	}
}
