// ImageWalker by Zac Walker
// Bundled libwebp encode/decode, independent of machine-installed WIC extensions.

#include "WebPCodec.h"

#include <webp/decode.h>
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

		bool features(const std::span<const std::uint8_t> bytes, WebPBitstreamFeatures& value)
		{
			return !bytes.empty() && bytes.size() <= maximumFileBytes &&
				WebPGetFeatures(bytes.data(), bytes.size(), &value) == VP8_STATUS_OK &&
				!value.has_animation && dimensions_ok(value.width, value.height);
		}

		std::vector<std::uint8_t> read(const std::filesystem::path& path)
		{
			std::ifstream file(files::native_path(path), std::ios::binary | std::ios::ate);
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
		WebPBitstreamFeatures value{};
		if (!features(bytes, value)) return false;
		width = value.width;
		height = value.height;
		return true;
	}

	bool probe(const std::filesystem::path& path, int& width, int& height)
	{
		width = height = 0;
		// Features precede the pixel bitstream; probing must not read an entire large photo.
		std::ifstream file(files::native_path(path), std::ios::binary);
		if (!file) return false;
		std::array<std::uint8_t, 64> header{};
		file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
		const auto size = file.gcount();
		return size > 0 && probe(std::span<const std::uint8_t>(header.data(), static_cast<size_t>(size)), width, height);
	}

	files::DecodedImage decode(const std::span<const std::uint8_t> bytes, const int maximumWidth,
		const int maximumHeight)
	{
		WebPBitstreamFeatures value{};
		if (maximumWidth < 0 || maximumHeight < 0 || !features(bytes, value)) return {};
		const double scale = maximumWidth > 0 && maximumHeight > 0 ? (std::min)({1.0,
			static_cast<double>(maximumWidth) / value.width, static_cast<double>(maximumHeight) / value.height}) : 1.0;
		files::DecodedImage result;
		result.width = (std::max)(1, static_cast<int>(value.width * scale));
		result.height = (std::max)(1, static_cast<int>(value.height * scale));
		result.originalWidth = value.width;
		result.originalHeight = value.height;
		result.pixels.resize(static_cast<size_t>(result.width) * result.height);

		WebPDecoderConfig config;
		if (!WebPInitDecoderConfig(&config)) return {};
		struct Cleanup
		{
			WebPDecoderConfig& config;
			~Cleanup() { WebPFreeDecBuffer(&config.output); }
		} cleanup{config};
		config.output.colorspace = MODE_BGRA;
		config.output.is_external_memory = 1;
		config.output.u.RGBA.rgba = reinterpret_cast<std::uint8_t*>(result.pixels.data());
		config.output.u.RGBA.stride = result.width * 4;
		config.output.u.RGBA.size = result.pixels.size() * sizeof(std::uint32_t);
		if (scale < 1)
		{
			config.options.use_scaling = 1;
			config.options.scaled_width = result.width;
			config.options.scaled_height = result.height;
		}
		if (WebPDecode(bytes.data(), bytes.size(), &config) != VP8_STATUS_OK) return {};
		return result;
	}

	files::DecodedImage load(const std::filesystem::path& path, const int maximumWidth, const int maximumHeight)
	{
		return decode(read(path), maximumWidth, maximumHeight);
	}

	std::vector<std::uint8_t> encode(const files::DecodedImage& image, const files::SaveOptions& options)
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
