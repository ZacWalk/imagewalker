// ImageWalker by Zac Walker
// WebP through libfiles, for ImageWalker's pixels. Publication stays with the caller.

#pragma once

// Preserve historical min/max macros around inline modern code.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include <span>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace iw::imaging { struct PixelImage; struct SaveOptions; }
namespace iw::webp
{
	bool probe(std::span<const std::uint8_t> bytes, int& width, int& height);
	bool probe(const std::filesystem::path& path, int& width, int& height);
	std::vector<std::uint8_t> encode(const imaging::PixelImage& image, const imaging::SaveOptions& options);
}

#include <iw/imaging/Codec.h>

namespace iw::webp
{
	// Static WebP only: animation is refused instead of silently saving its first frame.
	// A decode retains originalWidth/Height when maximum bounds request a thumbnail.
	bool probe(std::span<const std::uint8_t> bytes, int& width, int& height);
	bool probe(const std::filesystem::path& path, int& width, int& height);
	imaging::PixelImage decode(std::span<const std::uint8_t> bytes, int maximumWidth = 0, int maximumHeight = 0);
	imaging::PixelImage load(const std::filesystem::path& path, int maximumWidth = 0, int maximumHeight = 0);

	// Empty on invalid input or codec failure. No filesystem writes occur here, even on success.
	// The caller writes these bytes to its owned temporary sibling and commits explicitly.
	std::vector<std::uint8_t> encode(const imaging::PixelImage& image, const imaging::SaveOptions& options = {});
}

// ImageWalker by Zac Walker
// WebP decoded and encoded by libfiles, independent of installed WIC extensions.

#include <iw/imaging/Diagnostics.h>
#include <iw/imaging/detail/PlatformFileStream.h>
#include <libfiles/decode.h>
#include <libfiles/encode.h>

#include <algorithm>
#include <array>

namespace iw::webp
{
	namespace webp_detail
	{
		constexpr std::uint64_t maximumFileBytes = 512ull * 1024 * 1024;
		constexpr std::uint64_t maximumPixels = 128ull * 1024 * 1024;
		// WebP's own limit on a side.
		constexpr int maximumDimension = 16383;

		inline bool dimensions_ok(const int width, const int height)
		{
			return width > 0 && height > 0 && width <= maximumDimension && height <= maximumDimension &&
				static_cast<std::uint64_t>(width) * height <= maximumPixels;
		}

		inline bool features(const std::span<const std::uint8_t> bytes, libfiles::webp_header& value)
		{
			if (bytes.empty() || bytes.size() > maximumFileBytes) return false;
			libfiles::reader input(bytes);
			const auto header = libfiles::probe_webp_header(input);
			if (!header || header->has_animation || !dimensions_ok(header->width, header->height)) return false;
			value = *header;
			return true;
		}

		inline std::vector<std::uint8_t> read(const std::filesystem::path& path)
		{
			imaging::PlatformFileStream file(path);
			if (!file.is_open() || file.size() == 0 || file.size() > maximumFileBytes)
				return {};
			std::vector<std::uint8_t> bytes(static_cast<size_t>(file.size()));
			std::size_t offset = 0;
			while (offset < bytes.size())
			{
				const auto count = file.read(offset, bytes.data() + offset,
					bytes.size() - offset);
				if (count == 0) return {};
				offset += count;
			}
			return bytes;
		}
	}

	inline bool probe(const std::span<const std::uint8_t> bytes, int& width, int& height)
	{
		using namespace webp_detail;
		width = height = 0;
		libfiles::webp_header value;
		if (!features(bytes, value)) return false;
		width = value.width;
		height = value.height;
		return true;
	}

	inline bool probe(const std::filesystem::path& path, int& width, int& height)
	{
		using namespace webp_detail;
		width = height = 0;
		// Features precede the pixel bitstream; probing must not read an entire large photo.
		imaging::PlatformFileStream file(path);
		if (!file.is_open()) return false;
		std::array<std::uint8_t, 64> header{};
		const auto size = file.read(0, header.data(), header.size());
		return size > 0 && probe(std::span<const std::uint8_t>(header.data(), size),
			width, height);
	}

	inline imaging::PixelImage decode(const std::span<const std::uint8_t> bytes, const int maximumWidth,
		const int maximumHeight)
	{
		using namespace webp_detail;
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

	inline imaging::PixelImage load(const std::filesystem::path& path, const int maximumWidth, const int maximumHeight)
	{
		using namespace webp_detail;
		return decode(read(path), maximumWidth, maximumHeight);
	}

	inline std::vector<std::uint8_t> encode(const imaging::PixelImage& image, const imaging::SaveOptions& options)
	{
		using namespace webp_detail;
		if (!dimensions_ok(image.width, image.height) ||
			static_cast<std::uint64_t>(image.width) * image.height > image.pixels.size())
			return {};
		libfiles::image_view view;
		view.pixels = reinterpret_cast<const std::uint8_t*>(image.pixels.data());
		view.stride = static_cast<std::size_t>(image.width) * 4;
		view.width = static_cast<std::uint32_t>(image.width);
		view.height = static_cast<std::uint32_t>(image.height);
		view.format = libfiles::pixel_format::bgra8;
		libfiles::webp_options webp;
		webp.lossless = options.lossless;
		webp.quality = std::clamp(options.quality, 1, 100);
		webp.method = 4;
		webp.exact = true; // Lossless includes RGB under zero alpha, not just the visible composite.
		try
		{
			auto encoded = libfiles::encode_webp(view, 1, {}, webp);
			if (encoded.data.empty() || encoded.data.size() > maximumFileBytes) return {};
			return std::move(encoded.data);
		}
		catch (const std::exception&)
		{
			return {};
		}
	}
}

#pragma pop_macro("max")
#pragma pop_macro("min")
