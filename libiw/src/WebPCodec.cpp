// ImageWalker by Zac Walker
// libfiles decoding and bundled libwebp encoding, independent of installed WIC extensions.

#include <libiw/WebPCodec.h>
#include <libiw/Diagnostics.h>
#include <libfiles/decode.h>

#include <webp/encode.h>

#include <algorithm>
#include <array>
#include <fstream>

namespace iw::webp
{
	namespace
	{
		constexpr std::uint64_t maximumFileBytes = 512ull * 1024 * 1024;
		constexpr std::uint64_t maximumPixels = 128ull * 1024 * 1024;

		bool dimensions_ok(const int width, const int height)
		{
			return width > 0 && height > 0 && width <= WEBP_MAX_DIMENSION && height <= WEBP_MAX_DIMENSION &&
				static_cast<std::uint64_t>(width) * height <= maximumPixels;
		}

		bool features(const std::span<const std::uint8_t> bytes, libfiles::webp_header& value)
		{
			if (bytes.empty() || bytes.size() > maximumFileBytes) return false;
			libfiles::reader input(bytes);
			const auto header = libfiles::probe_webp_header(input);
			if (!header || header->has_animation || !dimensions_ok(header->width, header->height)) return false;
			value = *header;
			return true;
		}

		std::vector<std::uint8_t> read(const std::filesystem::path& path)
		{
			std::ifstream file(imaging::native_path(path), std::ios::binary | std::ios::ate);
			if (!file) return {};
			const auto end = file.tellg();
			if (end <= 0 || static_cast<std::uint64_t>(end) > maximumFileBytes) return {};
			std::vector<std::uint8_t> bytes(static_cast<size_t>(end));
			file.seekg(0);
			if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
				return {};
			return bytes;
		}
	}

	bool probe(const std::span<const std::uint8_t> bytes, int& width, int& height)
	{
		width = height = 0;
		libfiles::webp_header value;
		if (!features(bytes, value)) return false;
		width = value.width;
		height = value.height;
		return true;
	}

	bool probe(const std::filesystem::path& path, int& width, int& height)
	{
		width = height = 0;
		// Features precede the pixel bitstream; probing must not read an entire large photo.
		std::ifstream file(imaging::native_path(path), std::ios::binary);
		if (!file) return false;
		std::array<std::uint8_t, 64> header{};
		file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
		const auto size = file.gcount();
		return size > 0 && probe(std::span<const std::uint8_t>(header.data(), static_cast<size_t>(size)), width, height);
	}

	imaging::PixelImage decode(const std::span<const std::uint8_t> bytes, const int maximumWidth,
		const int maximumHeight)
	{
		libfiles::webp_header value;
		if (maximumWidth < 0 || maximumHeight < 0 || !features(bytes, value)) return {};
		struct Sink final : libfiles::image_sink
		{
			imaging::PixelImage image;
			libfiles::pixel_buffer allocate(const libfiles::frame_info& frame) override
			{
				if (!dimensions_ok(static_cast<int>(frame.width), static_cast<int>(frame.height))) return {};
				image.width = static_cast<int>(frame.width);
				image.height = static_cast<int>(frame.height);
				image.originalWidth = static_cast<int>(frame.source_width);
				image.originalHeight = static_cast<int>(frame.source_height);
				image.pixels.resize(static_cast<size_t>(frame.width) * frame.height);
				return {reinterpret_cast<std::uint8_t*>(image.pixels.data()), static_cast<size_t>(frame.width) * 4};
			}
		} sink;
		libfiles::reader input(bytes);
		libfiles::decode_options options;
		options.source_format = libfiles::image_format::webp;
		options.target_width = maximumWidth;
		options.target_height = maximumHeight;
		options.round_webp_size_down = true;
		const auto result = libfiles::decode(input, sink, options);
		if (!result.success)
		{
			imaging::report_diagnostic(result.refused ? L"The WebP exceeds the decoded-image limit." :
				std::wstring(result.error.begin(), result.error.end()));
			return {};
		}
		return std::move(sink.image);
	}

	imaging::PixelImage load(const std::filesystem::path& path, const int maximumWidth, const int maximumHeight)
	{
		return decode(read(path), maximumWidth, maximumHeight);
	}

	std::vector<std::uint8_t> encode(const imaging::PixelImage& image, const imaging::SaveOptions& options)
	{
		if (!dimensions_ok(image.width, image.height) ||
			static_cast<std::uint64_t>(image.width) * image.height > image.pixels.size()) return {};
		WebPConfig config;
		if (!WebPConfigInit(&config)) return {};
		config.lossless = options.lossless ? 1 : 0;
		config.quality = static_cast<float>(std::clamp(options.quality, 1, 100));
		config.method = 4;
		config.exact = 1; // Lossless includes RGB under zero alpha, not just the visible composite.
		if (!WebPValidateConfig(&config)) return {};
		WebPPicture picture;
		if (!WebPPictureInit(&picture)) return {};
		WebPMemoryWriter writer;
		WebPMemoryWriterInit(&writer);
		struct Cleanup
		{
			WebPPicture& picture;
			WebPMemoryWriter& writer;
			~Cleanup() { WebPPictureFree(&picture); WebPMemoryWriterClear(&writer); }
		} cleanup{picture, writer};
		picture.width = image.width;
		picture.height = image.height;
		picture.use_argb = 1;
		picture.writer = WebPMemoryWrite;
		picture.custom_ptr = &writer;
		if (!WebPPictureImportBGRA(&picture, reinterpret_cast<const std::uint8_t*>(image.pixels.data()),
			image.width * 4) || !WebPEncode(&config, &picture) || writer.size == 0 ||
			writer.size > maximumFileBytes) return {};
		return {writer.mem, writer.mem + writer.size};
	}
}
