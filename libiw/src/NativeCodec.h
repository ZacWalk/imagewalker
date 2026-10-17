#pragma once

#include <libiw/Document.h>

namespace IW { class Image; }

namespace iw::imaging
{
	// Display (BGRA) decodes use exactly one backend: bundled libwebp for WebP, an
	// installed Windows codec where one claims the extension, otherwise the native
	// decoders. Native-storage decodes (decode_document and the 2.x and ArtMate
	// loaders) start with the native decoders and reach libwebp and WIC only
	// through the WebP and Windows-codec loaders, which call decode_with().
	enum class PixelDecoder { webp, wic, native };
	PixelDecoder pixel_decoder(std::wstring_view extension, std::span<const std::uint8_t> header = {});
	// Never re-enters selection, so a loader can use it as a fallback without recursion.
	DecodeResult decode_with(PixelDecoder decoder, std::span<const std::uint8_t> bytes,
	                         std::wstring_view extension, const DecodeOptions& options);
	bool has_webp_signature(std::span<const std::uint8_t> header);

	std::vector<std::wstring> windows_image_extensions();
	bool native_supports_extension(std::wstring_view extension);
	DecodeResult decode_native(const std::filesystem::path& path, const DecodeOptions& options);
	PixelImage image_pixels(const IW::Image& image, std::size_t index = 0);
	FrameView image_frame(const IW::Image& image, std::size_t index = 0);
}
