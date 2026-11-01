#include <iw/imaging/Pixel.h>
#include <iw/imaging/Codec.h>
#include <iw/imaging/Document.h>
#include <iw/imaging/ImageEdits.h>
#include <iw/imaging/PixelOps.h>
#include <iw/imaging/WebPCodec.h>
#include <libfiles/encode.h>
#include <libfiles/exif.h>
#include <libfiles/scan.h>
#include <libfiles/xmp.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <tiffio.h>
#include <iw/imaging/detail/PlatformFileStream.h>
#include "iw/testrunner.h"

extern const std::string_view s_app_name = "ImageWalker-Imaging-Tests";

IW_TEST(QuarterTurnsPreserveEveryPixelAndAlpha)
{
	const iw::imaging::PixelImage source{3, 2,
		{0x00112233, 0x40445566, 0x80778899, 0xffaabbcc, 0xff102030, 0xff405060}, 30, 20};
	auto turned = iw::imaging::rotate_quarters(source, 1);
	IW_CHECK_EQ(turned.width, 2);
	IW_CHECK_EQ(turned.height, 3);
	IW_CHECK_EQ(turned.originalWidth, 20);
	IW_CHECK_EQ(turned.pixels[0], source.pixels[3]);
	IW_CHECK_EQ(turned.pixels[1], source.pixels[0]);
	for (int i = 0; i < 3; ++i) turned = iw::imaging::rotate_quarters(turned, 1);
	IW_CHECK(turned.pixels == source.pixels);
	IW_CHECK(iw::imaging::rotate_quarters(source, -1).pixels ==
		iw::imaging::rotate_quarters(source, 3).pixels);
}

IW_TEST(PixelBoundsRejectTruncatedAndOverflowingDimensions)
{
	IW_CHECK(!iw::imaging::valid_pixels(-1, 2, 4));
	IW_CHECK(!iw::imaging::valid_pixels(3, 2, 5));
	IW_CHECK(!iw::imaging::valid_pixels(INT_MAX, INT_MAX, 32));
	bool rejected = false;
	try { (void)iw::imaging::rotate_quarters({2, 2, {0}}, 1); }
	catch (const std::invalid_argument&) { rejected = true; }
	IW_CHECK(rejected);
}

namespace
{
	struct TemporaryImage
	{
		std::filesystem::path path;
		TemporaryImage()
		{
			wchar_t directory[MAX_PATH]{}, name[MAX_PATH]{};
			if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"iwl", 0, name))
				throw std::runtime_error("Could not create the test image.");
			path = name;
		}
		~TemporaryImage() { IW_CHECK(DeleteFileW(path.c_str()) != FALSE); }
	};
}

IW_TEST(StandaloneCodecsRetainAlphaAndNativeMetadataWithoutAnApplication)
{
	const iw::imaging::PixelImage source{3, 2,
		{0x00112233, 0x40445566, 0x80778899, 0xffaabbcc, 0xff102030, 0xff405060}, 3, 2};
	TemporaryImage file;
	const auto encoded = iw::imaging::encode_to_file(source, file.path, iw::imaging::ImageSaveFormat::png);
	IW_CHECK(static_cast<bool>(encoded));
	const auto decoded = iw::imaging::decode(file.path);
	IW_CHECK(static_cast<bool>(decoded));
	IW_CHECK(decoded.image.pixels == source.pixels);
	auto native = iw::imaging::decode_document(file.path);
	IW_CHECK(static_cast<bool>(native));
	if (!native) return;
	IW_CHECK_EQ(native.document.frames.size(), size_t{1});
	IW_CHECK(iw::imaging::frame_pixels(native).pixels == source.pixels);
	const auto copy = native;
	IW_CHECK(iw::imaging::frame_pixels(copy).pixels == source.pixels);
	IW_CHECK(copy.document.frames[0].pixels.data() != native.document.frames[0].pixels.data());
	IW_CHECK(copy.document.metadata == native.document.metadata);
	for (size_t i = 0; i < native.document.metadata.size(); ++i)
	{
		const auto& original = native.document.metadata[i].bytes;
		if (!original.empty()) IW_CHECK(original.data() != copy.document.metadata[i].bytes.data());
	}
}

IW_TEST(ScannedMetadataSurvivesWithoutANativeDocumentDecoder)
{
	const iw::imaging::PixelImage image{2, 2,
		{0xff112233, 0xff445566, 0xff778899, 0xffabcdef}, 2, 2};
	libfiles::image_view view;
	view.pixels = reinterpret_cast<const std::uint8_t*>(image.pixels.data());
	view.stride = 2 * sizeof(std::uint32_t);
	view.width = view.height = 2;
	view.format = libfiles::pixel_format::bgra8;
	libfiles::exif_fields fields;
	fields.orientation = 6;
	fields.description = "source caption";
	const auto exif = libfiles::make_exif(fields);
	libfiles::xmp::meta packet;
	packet.set_property_int(libfiles::xmp::ns::tiff, "Orientation", 6);
	const auto xmp = packet.serialize();
	std::vector<std::uint8_t> profile(128);
	profile[3] = 128;
	std::copy_n("CMYK", 4, profile.begin() + 16);
	libfiles::embedded_metadata metadata;
	metadata.exif = exif;
	metadata.xmp = {reinterpret_cast<const std::uint8_t*>(xmp.data()), xmp.size()};
	metadata.icc = profile;
	libfiles::webp_options webp;
	webp.lossless = true;
	const auto encoded = libfiles::encode_webp(view, 1, metadata, webp);
	IW_CHECK(!encoded.data.empty());
	TemporaryImage source;
	{
		std::ofstream output(source.path, std::ios::binary);
		output.write(reinterpret_cast<const char*>(encoded.data.data()),
			static_cast<std::streamsize>(encoded.data.size()));
		output.close();
		IW_CHECK(static_cast<bool>(output));
	}
	IW_CHECK(!libfiles::can_decode_document(libfiles::image_format::webp));
	for (const auto carry : {iw::imaging::CarryMetadata::all, iw::imaging::CarryMetadata::profile})
	{
		TemporaryImage destination;
		iw::imaging::SaveOptions options;
		options.metadataSource = source.path;
		options.carry = carry;
		const auto saved = iw::imaging::encode_to_file(image, destination.path,
			iw::imaging::ImageSaveFormat::png, options);
		IW_CHECK(static_cast<bool>(saved));
		IW_CHECK(!saved.warnings.empty());
		iw::imaging::PlatformFileStream stream(destination.path);
		libfiles::reader input(stream);
		const auto scanned = libfiles::scan(input);
		IW_CHECK(scanned.metadata.icc.empty());
		if (carry == iw::imaging::CarryMetadata::profile)
		{
			IW_CHECK(scanned.metadata.exif.empty());
			IW_CHECK(scanned.metadata.xmp.empty());
		}
		else
		{
			const auto info = libfiles::read_exif(scanned.metadata.exif);
			IW_CHECK(info.orientation == 1);
			IW_CHECK(info.description == "source caption");
			libfiles::xmp::meta carried;
			carried.parse(std::string_view(reinterpret_cast<const char*>(scanned.metadata.xmp.data()),
				scanned.metadata.xmp.size()));
			std::int32_t orientation = 0;
			IW_CHECK(carried.get_property_int(libfiles::xmp::ns::tiff, "Orientation", &orientation));
			IW_CHECK_EQ(orientation, 1);
		}
	}
}

IW_TEST(RasterJpegSavesCarryOpaqueSourceMarkersOnlyWhenRequested)
{
	const iw::imaging::PixelImage image{2, 2,
		{0xff112233, 0xff445566, 0xff778899, 0xffabcdef}, 2, 2};
	libfiles::jpeg_encoder encoder;
	auto bytes = encoder.encode(2, 2, reinterpret_cast<const std::uint8_t*>(image.pixels.data()),
		2 * sizeof(std::uint32_t), 1, {}, {});
	IW_CHECK(bytes.size() > 2);
	if (bytes.size() <= 2) return;
	const std::vector<std::uint8_t> opaque{0xff, 0xef, 0, 8, 'o', 'p', 'a', 'q', 'u', 'e'};
	bytes.insert(bytes.begin() + 2, opaque.begin(), opaque.end());
	TemporaryImage source;
	{
		std::ofstream output(source.path, std::ios::binary);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		output.close();
		IW_CHECK(static_cast<bool>(output));
	}
	for (const auto carry : {iw::imaging::CarryMetadata::all, iw::imaging::CarryMetadata::profile})
	{
		TemporaryImage destination;
		iw::imaging::SaveOptions options;
		options.metadataSource = source.path;
		options.carry = carry;
		const auto saved = iw::imaging::encode_to_file(image, destination.path,
			iw::imaging::ImageSaveFormat::jpeg, options);
		IW_CHECK(static_cast<bool>(saved));
		iw::imaging::PlatformFileStream stream(destination.path);
		libfiles::reader input(stream);
		const auto header = libfiles::read_jpeg_header(input);
		const auto marker = std::search(header.begin(), header.end(), opaque.begin(), opaque.end());
		IW_CHECK((marker != header.end()) == (carry == iw::imaging::CarryMetadata::all));
		IW_CHECK(static_cast<bool>(iw::imaging::decode(destination.path)));
	}
}

IW_TEST(DecodeCancellationAndMemoryLimitsAreDistinctFailures)
{
	TemporaryImage file;
	const iw::imaging::PixelImage source{2, 2, {0xff112233, 0xff445566, 0xff778899, 0xffabcdef}, 2, 2};
	IW_CHECK(static_cast<bool>(iw::imaging::encode_to_file(source, file.path, iw::imaging::ImageSaveFormat::png)));
	iw::imaging::DecodeOptions limit;
	limit.maximumBytes = 15;
	const auto tooLarge = iw::imaging::decode(file.path, limit);
	IW_CHECK(tooLarge.error == iw::imaging::ImageError::resource_limit);
	const auto nativeTooLarge = iw::imaging::decode_document(file.path, limit);
	IW_CHECK(nativeTooLarge.error == iw::imaging::ImageError::resource_limit);
	IW_CHECK(nativeTooLarge.document.frames.empty());
	iw::imaging::DecodeOptions stopped;
	stopped.cancelled = [] { return true; };
	IW_CHECK(iw::imaging::decode(file.path, stopped).error == iw::imaging::ImageError::cancelled);
	IW_CHECK(iw::imaging::decode_document(file.path, stopped).error == iw::imaging::ImageError::cancelled);
	IW_CHECK(static_cast<bool>(iw::imaging::decode(file.path)));
}

IW_TEST(CommonEditsAndSimdRetainTheirNeutralResults)
{
	const iw::imaging::PixelImage source{2, 2, {0x00112233, 0x80445566, 0xff778899, 0xffaabbcc}, 2, 2};
	IW_CHECK(iw::edits::apply(source, {}).pixels == source.pixels);
	iw::edits::ImageEdits edits;
	edits.rotation = 1;
	IW_CHECK(iw::edits::apply(source, edits).pixels == iw::imaging::rotate_quarters(source, 1).pixels);
	auto scalar = source.pixels;
	auto dispatched = source.pixels;
	iw::imaging::blend_constant_bgra(scalar, {30, 70, 130, 100}, iw::imaging::PixelBackend::scalar);
	iw::imaging::blend_constant_bgra(dispatched, {30, 70, 130, 100});
	IW_CHECK(scalar == dispatched);
}

IW_TEST(PixelRowsValidateStrideAndPreservePaletteAlpha)
{
	const std::uint8_t bytes[] = {0x90, 0, 0, 0, 0x18, 0, 0, 0};
	const std::uint32_t palette[] = {0xff112233, 0x80445566, 0x00778899, 0xffaabbcc};
	libfiles::pixel_rows frame;
	frame.width = 3;
	frame.height = 2;
	frame.stride = 4;
	frame.storage = libfiles::document_storage::indexed2;
	frame.bottom_up = true;
	frame.pixels = bytes;
	frame.palette = palette;
	const auto decoded = iw::imaging::pixels_from_rows(frame);
	IW_CHECK(decoded.pixels == std::vector<std::uint32_t>({palette[0], palette[1], palette[2],
		palette[2], palette[1], palette[0]}));
	frame.stride = 3;
	bool rejected{};
	try { (void)iw::imaging::pixels_from_rows(frame); }
	catch (const std::invalid_argument&) { rejected = true; }
	IW_CHECK(rejected);
}

IW_TEST(DocumentPreviewSupportDoesNotAdvertiseAnEditablePhoto)
{
	IW_CHECK(iw::imaging::supports_extension(L".ppt"));
	IW_CHECK(iw::imaging::is_preview_only(L".ppt"));
	IW_CHECK(!iw::imaging::is_preview_only(L".png"));
	IW_CHECK(iw::imaging::supports_extension(L".webp"));
}

IW_TEST(TiledTiffDecodingRetainsTheRasterRatherThanReturningABlankPage)
{
	TemporaryImage file;
	std::unique_ptr<TIFF, decltype(&TIFFClose)> encoded(TIFFOpenW(file.path.c_str(), "w"), TIFFClose);
	IW_CHECK(encoded != nullptr);
	if (!encoded) return;
	IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_IMAGEWIDTH, 19));
	IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_IMAGELENGTH, 17));
	IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_SAMPLESPERPIXEL, 3));
	IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_BITSPERSAMPLE, 8));
	IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB));
	IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG));
	IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_TILEWIDTH, 16));
	IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_TILELENGTH, 16));
	std::vector<std::uint8_t> sourceProfile(128);
	sourceProfile[3] = 128;
	std::copy_n("CMYK", 4, sourceProfile.begin() + 16);
	IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_ICCPROFILE,
		static_cast<std::uint32_t>(sourceProfile.size()), sourceProfile.data()));
	std::vector<std::uint8_t> tile(16 * 16 * 3);
	for (size_t i = 0; i < tile.size(); i += 3)
	{
		tile[i] = 31;
		tile[i + 1] = 61;
		tile[i + 2] = 101;
	}
	for (std::uint32_t y : {0u, 16u})
		for (std::uint32_t x : {0u, 16u})
			IW_CHECK(TIFFWriteEncodedTile(encoded.get(), TIFFComputeTile(encoded.get(), x, y, 0, 0),
				tile.data(), static_cast<tmsize_t>(tile.size())) == static_cast<tmsize_t>(tile.size()));
	encoded.reset();
	const auto decoded = iw::imaging::decode_document(file.path);
	IW_CHECK(static_cast<bool>(decoded));
	if (!decoded) return;
	const auto pixels = iw::imaging::frame_pixels(decoded);
	IW_CHECK_EQ(pixels.width, 19);
	IW_CHECK_EQ(pixels.height, 17);
	IW_CHECK(std::ranges::all_of(pixels.pixels, [](std::uint32_t pixel) { return pixel == 0xff1f3d65; }));
	bool retainedProfile{};
	for (const auto& metadata : decoded.document.metadata)
	{
		IW_CHECK(metadata.kind != libfiles::document_metadata::icc);
		if (metadata.kind == libfiles::document_metadata::source_icc)
			retainedProfile = std::ranges::equal(metadata.bytes, sourceProfile);
	}
	IW_CHECK(retainedProfile);
	IW_CHECK(!decoded.warnings.empty());
}

namespace
{
	std::vector<std::uint8_t> read_file(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
	}

	void put16(std::vector<std::uint8_t>& bytes, const unsigned value)
	{
		bytes.push_back(static_cast<std::uint8_t>(value));
		bytes.push_back(static_cast<std::uint8_t>(value >> 8));
	}

	void put32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
	{
		put16(bytes, value & 0xffff);
		put16(bytes, value >> 16);
	}

	// Three-by-two frames of one palette index each: black, red, green, blue. A clear
	// code before every literal keeps each LZW code three bits wide without an encoder.
	std::vector<std::uint8_t> make_gif(const std::initializer_list<std::uint8_t> frames)
	{
		std::vector<std::uint8_t> bytes{'G', 'I', 'F', '8', '9', 'a', 3, 0, 2, 0, 0x81, 0, 0,
			0, 0, 0, 0xff, 0, 0, 0, 0xff, 0, 0, 0, 0xff};
		for (const auto index : frames)
		{
			const std::uint8_t descriptor[] = {0x2c, 0, 0, 0, 0, 3, 0, 2, 0, 0, 2};
			bytes.insert(bytes.end(), std::begin(descriptor), std::end(descriptor));
			std::vector<std::uint8_t> raster;
			unsigned bits{};
			const auto emit = [&](const unsigned code)
			{
				for (unsigned bit = 0; bit < 3; ++bit, ++bits)
				{
					if (bits % 8 == 0) raster.push_back(0);
					raster.back() |= static_cast<std::uint8_t>(((code >> bit) & 1) << (bits % 8));
				}
			};
			for (int pixel = 0; pixel < 6; ++pixel)
			{
				emit(4);
				emit(index);
			}
			emit(5);
			bytes.push_back(static_cast<std::uint8_t>(raster.size()));
			bytes.insert(bytes.end(), raster.begin(), raster.end());
			bytes.push_back(0);
		}
		bytes.push_back(0x3b);
		return bytes;
	}

	bool solid(const iw::imaging::DocumentResult& document, const std::size_t frame, const std::uint32_t colour)
	{
		const auto pixels = iw::imaging::frame_pixels(document, frame);
		return !pixels.pixels.empty() &&
			std::ranges::all_of(pixels.pixels, [colour](const std::uint32_t pixel) { return pixel == colour; });
	}

	constexpr std::uint8_t kAviHeader[] = {'R', 'I', 'F', 'F', 4, 0, 0, 0, 'A', 'V', 'I', ' ', 0, 0, 0, 0};
}

IW_TEST(DisplayDecodesFollowOneBackendRule)
{
	using iw::imaging::PixelDecoder;
	using iw::imaging::pixel_decoder;
	const std::uint8_t webp[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'};
	IW_CHECK(pixel_decoder(L".webp") == PixelDecoder::webp);
	IW_CHECK(pixel_decoder(L".pcx", webp) == PixelDecoder::webp);
	IW_CHECK(pixel_decoder(L".webp", kAviHeader) == PixelDecoder::wic);
	IW_CHECK(pixel_decoder(L".jpg") == PixelDecoder::wic);
	IW_CHECK(pixel_decoder(L".png") == PixelDecoder::wic);
	IW_CHECK(pixel_decoder(L".ico") == PixelDecoder::wic);
	IW_CHECK(pixel_decoder(L".pcx") == PixelDecoder::native);
	IW_CHECK(pixel_decoder(L".wpg") == PixelDecoder::native);
	IW_CHECK(pixel_decoder(L".unknown") == PixelDecoder::wic);
}

IW_TEST(UnclaimedFilesAreUnsupportedAndFallbackLoadersNeverRecurse)
{
	const std::uint8_t ole[] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1, 0, 0, 0, 0, 0, 0, 0, 0};
	IW_CHECK(iw::imaging::decode_document(kAviHeader).error == iw::imaging::ImageError::unsupported);
	IW_CHECK(iw::imaging::decode_document(kAviHeader, L".avi").error == iw::imaging::ImageError::unsupported);
	IW_CHECK(iw::imaging::decode_document(ole, L".doc").error == iw::imaging::ImageError::unsupported);
	// A failed native loader retries by header word; RIFF selects the WebP loader,
	// which must not hand the bytes back to the native decoders.
	for (const auto* extension : {L".pcx", L".psd", L".wpg", L".wmf", L".ppt", L".webp"})
	{
		IW_CHECK(!iw::imaging::decode_document(kAviHeader, extension));
		IW_CHECK(!iw::imaging::decode(kAviHeader, extension));
	}
}

IW_TEST(WebPAndWindowsCodecLoadersServeNativeDocuments)
{
	const iw::imaging::PixelImage source{3, 2,
		{0xff112233, 0xff445566, 0xff778899, 0xffaabbcc, 0xff102030, 0xff405060}, 3, 2};
	iw::imaging::SaveOptions lossless;
	lossless.lossless = true;
	const auto webp = iw::webp::encode(source, lossless);
	IW_CHECK(!webp.empty());
	// Misnamed and extension-less WebP reach the WebP loader by signature.
	for (const auto* extension : {L".webp", L".jpg", L""})
	{
		const auto decoded = iw::imaging::decode_document(webp, extension);
		if (!decoded) IW::Test::Detail::Print(" extension %ls: %ls (error %d)\n",
			extension, decoded.message.c_str(), static_cast<int>(decoded.error));
		IW_CHECK(static_cast<bool>(decoded));
		if (decoded) IW_CHECK(iw::imaging::frame_pixels(decoded).pixels == source.pixels);
	}

	TemporaryImage png;
	IW_CHECK(static_cast<bool>(iw::imaging::encode_to_file(source, png.path, iw::imaging::ImageSaveFormat::png)));
	const auto encoded = read_file(png.path);
	std::vector<std::uint8_t> icon;
	put16(icon, 0);
	put16(icon, 1);
	put16(icon, 1);
	icon.insert(icon.end(), {3, 2, 0, 0});
	put16(icon, 1);
	put16(icon, 32);
	put32(icon, static_cast<std::uint32_t>(encoded.size()));
	put32(icon, 22);
	icon.insert(icon.end(), encoded.begin(), encoded.end());
	IW_CHECK(!iw::imaging::native_supports_extension(L".ico"));
	const auto decoded = iw::imaging::decode_document(icon, L".ico");
	IW_CHECK(static_cast<bool>(decoded));
	if (decoded) IW_CHECK(iw::imaging::frame_pixels(decoded).pixels == source.pixels);
}

IW_TEST(WebPDecodeBudgetsAndFailuresAreReportedBeforePublication)
{
	const iw::imaging::PixelImage source{32, 32,
		std::vector<std::uint32_t>(1024, 0xff336699), 32, 32};
	iw::imaging::SaveOptions save;
	save.lossless = true;
	const auto bytes = iw::webp::encode(source, save);
	IW_CHECK(!bytes.empty());
	if (bytes.empty()) return;
	TemporaryImage file;
	auto webp_path = file.path;
	webp_path.replace_extension(L".webp");
	std::filesystem::rename(file.path, webp_path);
	file.path = std::move(webp_path);
	{
		std::ofstream output(file.path, std::ios::binary);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		output.close();
		IW_CHECK(static_cast<bool>(output));
	}
	for (const auto budget : {std::uint64_t{1}, std::uint64_t{4095}, std::uint64_t{4096}})
	{
		iw::imaging::DecodeOptions options;
		options.maximumBytes = budget;
		const auto memory = iw::imaging::decode(bytes, L".webp", options);
		const auto path = iw::imaging::decode(file.path, options);
		for (const auto* decoded : {&memory, &path})
		{
			IW_CHECK(static_cast<bool>(*decoded) == (budget == 4096));
			if (budget < 4096)
			{
				IW_CHECK(decoded->error == iw::imaging::ImageError::resource_limit);
				IW_CHECK(decoded->image.pixels.empty());
			}
			else IW_CHECK_EQ(decoded->image.pixels.size(), size_t{1024});
		}
	}
	bool checked_truncation = false;
	for (size_t length = 20; length < bytes.size(); ++length)
	{
		const auto cut = std::span(bytes).first(length);
		int width = 0, height = 0;
		if (!iw::webp::probe(cut, width, height)) continue;
		const auto decoded = iw::imaging::decode(cut, L".webp");
		IW_CHECK(!decoded);
		IW_CHECK(decoded.error != iw::imaging::ImageError::none && !decoded.message.empty());
		IW_CHECK(decoded.image.pixels.empty());
		checked_truncation = true;
		break;
	}
	IW_CHECK(checked_truncation);
}

IW_TEST(WebPThumbnailsKeepTheNativeRoundingAndSourceDimensions)
{
	const iw::imaging::PixelImage source{9, 8, std::vector<std::uint32_t>(72, 0xff112233), 9, 8};
	iw::imaging::SaveOptions options;
	options.lossless = true;
	const auto encoded = iw::webp::encode(source, options);
	IW_CHECK(!encoded.empty());
	int width{}, height{};
	IW_CHECK(iw::webp::probe(std::span(encoded).first((std::min)(encoded.size(), size_t{64})), width, height));
	IW_CHECK_EQ(width, 9);
	IW_CHECK_EQ(height, 8);
	const auto decoded = iw::webp::decode(encoded, 4, 4);
	IW_CHECK_EQ(decoded.width, 4);
	IW_CHECK_EQ(decoded.height, 3);
	IW_CHECK_EQ(decoded.originalWidth, 9);
	IW_CHECK_EQ(decoded.originalHeight, 8);
	IW_CHECK(decoded.pixels == std::vector<std::uint32_t>(12, 0xff112233));
}

IW_TEST(WindowsBitmapAdapterPreservesSourceDimensionsAndBounds)
{
	const iw::imaging::PixelImage source{7, 5,
		std::vector<std::uint32_t>(35, 0xff336699), 7, 5};
	TemporaryImage file;
	IW_CHECK(static_cast<bool>(iw::imaging::encode_to_file(
		source, file.path, iw::imaging::ImageSaveFormat::png)));
	if (iw::imaging::pixel_decoder(L".png") != iw::imaging::PixelDecoder::wic)
		return;

	const auto full = iw::imaging::decode(file.path);
	IW_CHECK(static_cast<bool>(full));
	if (full)
	{
		IW_CHECK_EQ(full.image.width, 7);
		IW_CHECK_EQ(full.image.height, 5);
		IW_CHECK_EQ(full.image.originalWidth, 7);
		IW_CHECK_EQ(full.image.originalHeight, 5);
		IW_CHECK(!full.reduced);
	}

	iw::imaging::DecodeOptions thumbnail_options;
	thumbnail_options.thumbnail = true;
	thumbnail_options.maximumWidth = 3;
	thumbnail_options.maximumHeight = 3;
	const auto thumbnail = iw::imaging::decode(file.path, thumbnail_options);
	IW_CHECK(static_cast<bool>(thumbnail));
	if (thumbnail)
	{
		IW_CHECK(thumbnail.image.width <= 3);
		IW_CHECK(thumbnail.image.height <= 3);
		IW_CHECK_EQ(thumbnail.image.originalWidth, 7);
		IW_CHECK_EQ(thumbnail.image.originalHeight, 5);
		IW_CHECK(thumbnail.reduced);
	}

	thumbnail_options.cancelled = [] { return true; };
	IW_CHECK(iw::imaging::decode(file.path, thumbnail_options).error ==
		iw::imaging::ImageError::cancelled);
}

IW_TEST(PlatformFileStreamKeepsReadingAfterSharedRename)
{
	TemporaryImage source;
	const std::array<std::uint8_t, 4> original{10, 20, 30, 40};
	auto file = platform::open_file(iw::imaging::desktop_file_path(source.path),
		platform::file_open_mode::create);
	IW_CHECK(file != nullptr);
	if (!file) return;
	IW_CHECK_EQ(file->write(original.data(), original.size()), original.size());
	IW_CHECK(file->flush());
	file.reset();

	iw::imaging::PlatformFileStream reader(source.path);
	IW_CHECK(reader.is_open());
	if (!reader.is_open()) return;
	auto renamed = source.path;
	renamed += L".shared";
	const auto moved = platform::move_file(
		iw::imaging::desktop_file_path(source.path),
		iw::imaging::desktop_file_path(renamed), false);
	IW_CHECK(!moved.failed());
	std::array<std::uint8_t, 4> read{};
	IW_CHECK_EQ(reader.read(0, read.data(), read.size()), read.size());
	IW_CHECK(read == original);
	IW_CHECK(!platform::move_file(iw::imaging::desktop_file_path(renamed),
		iw::imaging::desktop_file_path(source.path), false).failed());
}

IW_TEST(DamageAfterACompleteGifFrameKeepsTheCompleteFrames)
{
	constexpr std::uint32_t red = 0xffff0000, green = 0xff00ff00;
	constexpr std::size_t header = 25, frame = 18, partialFrame = 14;
	const auto gif = make_gif({1, 2});
	const auto check = [&](const std::vector<std::uint8_t>& bytes, const std::size_t frames, const bool warned)
	{
		const auto decoded = iw::imaging::decode_document(bytes, L".gif");
		IW_CHECK(static_cast<bool>(decoded));
		if (!decoded) return;
		IW_CHECK_EQ(decoded.document.frames.size(), frames);
		IW_CHECK(solid(decoded, 0, red));
		if (frames > 1) IW_CHECK(solid(decoded, 1, green));
		IW_CHECK_EQ(decoded.warnings.empty(), !warned);
	};
	check(gif, 2, false);
	check({gif.begin(), gif.end() - 1}, 2, false);
	auto unknownRecord = gif;
	unknownRecord.back() = 0x99;
	check(unknownRecord, 2, true);
	check({gif.begin(), gif.begin() + header + frame + partialFrame}, 1, true);
	const std::vector<std::uint8_t> incompleteFirst(gif.begin(), gif.begin() + header + partialFrame);
	IW_CHECK(iw::imaging::decode_document(incompleteFirst, L".gif").error == iw::imaging::ImageError::read_failed);
}

IW_TEST(DamagedLaterTiffDirectoryKeepsTheCompletePages)
{
	TemporaryImage file;
	{
		std::unique_ptr<TIFF, decltype(&TIFFClose)> encoded(TIFFOpenW(file.path.c_str(), "w"), TIFFClose);
		IW_CHECK(encoded != nullptr);
		if (!encoded) return;
		IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_IMAGEWIDTH, 3));
		IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_IMAGELENGTH, 2));
		IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_SAMPLESPERPIXEL, 3));
		IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_BITSPERSAMPLE, 8));
		IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB));
		IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG));
		IW_CHECK(TIFFSetField(encoded.get(), TIFFTAG_ROWSPERSTRIP, 2));
		std::uint8_t row[] = {31, 61, 101, 31, 61, 101, 31, 61, 101};
		for (std::uint32_t y = 0; y < 2; ++y) IW_CHECK(TIFFWriteScanline(encoded.get(), row, y, 0) == 1);
	}
	auto tiff = read_file(file.path);
	std::uint32_t directory{};
	if (tiff.size() >= 8) std::memcpy(&directory, tiff.data() + 4, 4);
	IW_CHECK(tiff.size() >= 8 && tiff[0] == 'I' && directory + 2 <= tiff.size());
	if (tiff.size() < 8 || tiff[0] != 'I' || directory + 2 > tiff.size()) return;
	std::uint16_t entries{};
	std::memcpy(&entries, tiff.data() + directory, 2);
	const auto link = static_cast<std::size_t>(directory) + 2 + 12 * entries;
	IW_CHECK(link + 4 <= tiff.size());
	if (link + 4 > tiff.size()) return;
	// Link the complete first page to a directory past the end of the file.
	const auto beyond = static_cast<std::uint32_t>(tiff.size() + 4096);
	std::memcpy(tiff.data() + link, &beyond, 4);
	const auto decoded = iw::imaging::decode_document(tiff, L".tif");
	IW_CHECK(static_cast<bool>(decoded));
	if (!decoded) return;
	IW_CHECK_EQ(decoded.document.frames.size(), size_t{1});
	IW_CHECK(solid(decoded, 0, 0xff1f3d65));
	IW_CHECK(!decoded.warnings.empty());
}

IW_TEST(BmpHeadersNeedOnlyAValidPixelOffset)
{
	std::vector<std::uint8_t> bmp{'B', 'M'};
	put32(bmp, 70);
	put32(bmp, 0);
	put32(bmp, 54);
	put32(bmp, 40);
	put32(bmp, 2);
	put32(bmp, 2);
	put16(bmp, 1);
	put16(bmp, 24);
	put32(bmp, 0);
	put32(bmp, 16);
	bmp.resize(54, 0);
	for (int row = 0; row < 2; ++row)
	{
		for (int x = 0; x < 2; ++x) bmp.insert(bmp.end(), {101, 61, 31});
		bmp.insert(bmp.end(), {0, 0});
	}
	const auto decodes = [](const std::vector<std::uint8_t>& bytes)
	{
		const auto decoded = iw::imaging::decode_document(bytes, L".bmp");
		return decoded && solid(decoded, 0, 0xff1f3d65);
	};
	IW_CHECK(decodes(bmp));
	auto oversized = bmp;
	std::fill_n(oversized.begin() + 2, 4, std::uint8_t{0xff});
	IW_CHECK(decodes(oversized));
	auto implicitOffset = bmp;
	std::fill_n(implicitOffset.begin() + 10, 4, std::uint8_t{0});
	IW_CHECK(decodes(implicitOffset));
	auto outsideOffset = bmp;
	std::fill_n(outsideOffset.begin() + 10, 4, std::uint8_t{0xff});
	IW_CHECK(!decodes(outsideOffset));
	IW_CHECK(!decodes({bmp.begin(), bmp.end() - 1}));
}

IW_TEST(NativeDecodesStopWhenCancelledPartWayThroughThePixels)
{
	const iw::imaging::PixelImage source{64, 64, std::vector<std::uint32_t>(64 * 64, 0xff336699), 64, 64};
	TemporaryImage file;
	IW_CHECK(static_cast<bool>(iw::imaging::encode_to_file(source, file.path, iw::imaging::ImageSaveFormat::png)));
	int polls{};
	iw::imaging::DecodeOptions options;
	options.cancelled = [&polls] { return ++polls > 8; };
	const auto decoded = iw::imaging::decode_document(file.path, options);
	IW_CHECK(decoded.error == iw::imaging::ImageError::cancelled);
	IW_CHECK_EQ(decoded.document.frames.size(), size_t{0});
	IW_CHECK(polls > 8);
}

int main()
{
	return IW::Test::RunAll("imagewalker-imaging");
}
