#include <libiw/Pixel.h>
#include <libiw/Codec.h>
#include <libiw/Document.h>
#include <libiw/ImageEdits.h>
#include <libiw/PixelOps.h>
#include <libiw/WebPCodec.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <tiffio.h>
#include "../src/NativeCodec.h"
#include "iw/testrunner.h"

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
	IW_CHECK_EQ(native.document.frame_count(), size_t{1});
	IW_CHECK(native.document.frame_pixels().pixels == source.pixels);
	const auto copy = native.document.clone();
	IW_CHECK(copy.frame_pixels().pixels == source.pixels);
	IW_CHECK(copy.frame(0).pixels.data() != native.document.frame(0).pixels.data());
	for (size_t i = 0; i < native.document.metadata_count(); ++i)
	{
		const auto original = native.document.metadata(i);
		const auto cloned = copy.metadata(i);
		IW_CHECK_EQ(original.type, cloned.type);
		IW_CHECK(std::ranges::equal(original.bytes, cloned.bytes));
		if (!original.bytes.empty()) IW_CHECK(original.bytes.data() != cloned.bytes.data());
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
	IW_CHECK_EQ(nativeTooLarge.document.frame_count(), size_t{0});
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

IW_TEST(NativeFrameViewsValidateStrideAndPreservePaletteAlpha)
{
	const std::uint8_t bytes[] = {0x90, 0, 0, 0, 0x18, 0, 0, 0};
	const std::uint32_t palette[] = {0xff112233, 0x80445566, 0x00778899, 0xffaabbcc};
	iw::imaging::FrameView frame;
	frame.width = 3;
	frame.height = 2;
	frame.stride = 4;
	frame.format = iw::imaging::PixelStorage::indexed2;
	frame.pixels = bytes;
	frame.paletteBgra = {reinterpret_cast<const std::uint8_t*>(palette), sizeof(palette)};
	const auto decoded = iw::imaging::pixels_from_frame(frame);
	IW_CHECK(decoded.pixels == std::vector<std::uint32_t>({palette[0], palette[1], palette[2],
		palette[2], palette[1], palette[0]}));
	frame.stride = 3;
	bool rejected{};
	try { (void)iw::imaging::pixels_from_frame(frame); }
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
	const auto pixels = decoded.document.frame_pixels();
	IW_CHECK_EQ(pixels.width, 19);
	IW_CHECK_EQ(pixels.height, 17);
	IW_CHECK(std::ranges::all_of(pixels.pixels, [](std::uint32_t pixel) { return pixel == 0xff1f3d65; }));
	bool retainedProfile{};
	for (size_t i = 0; i < decoded.document.metadata_count(); ++i)
	{
		const auto metadata = decoded.document.metadata(i);
		IW_CHECK(metadata.kind != iw::imaging::MetadataKind::icc);
		if (metadata.kind == iw::imaging::MetadataKind::sourceProfile)
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

	bool solid(const iw::imaging::Document& document, const std::size_t frame, const std::uint32_t colour)
	{
		const auto pixels = document.frame_pixels(frame);
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
		if (decoded) IW_CHECK(decoded.document.frame_pixels().pixels == source.pixels);
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
	if (decoded) IW_CHECK(decoded.document.frame_pixels().pixels == source.pixels);
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
		IW_CHECK_EQ(decoded.document.frame_count(), frames);
		IW_CHECK(solid(decoded.document, 0, red));
		if (frames > 1) IW_CHECK(solid(decoded.document, 1, green));
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
	IW_CHECK_EQ(decoded.document.frame_count(), size_t{1});
	IW_CHECK(solid(decoded.document, 0, 0xff1f3d65));
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
		return decoded && solid(decoded.document, 0, 0xff1f3d65);
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
	IW_CHECK_EQ(decoded.document.frame_count(), size_t{0});
	IW_CHECK(polls > 8);
}

int main()
{
	return IW::Test::RunAll("libiw");
}
