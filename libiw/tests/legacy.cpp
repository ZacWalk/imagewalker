#include <libiw/ImageEdits.h>
#include <libiw/JpegTransform.h>
#include "../src/NativeCodec.h"
#include "../src/legacy/LegacyPch.h"
#include "../src/legacy/FileModern.h"
#include "../src/legacy/OperationStatus.h"
#include <libiw/WebPCodec.h>
#include <cstdio>
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
		IW_CHECK_EQ(decoded.document.frame_count(), size_t{0});
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
