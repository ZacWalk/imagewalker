#pragma once

#include <libiw/Pixel.h>

#include <libfiles/icc.h>
#include <libfiles/properties.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>

namespace iw::imaging
{
	using DecodedImage = PixelImage;

	enum class ImageSaveFormat { png = 0, jpeg = 1, bmp = 2, tiff = 3, webp = 4 };
	enum class CarryMetadata { none, profile, all };
	enum class ImageError { none, invalid_input, unsupported, read_failed, encode_failed, resource_limit, cancelled };

	struct SaveOptions
	{
		int quality{85};
		bool lossless{};
		std::filesystem::path metadataSource;
		CarryMetadata carry{CarryMetadata::none};
	};

	struct OperationResult
	{
		ImageError error{ImageError::none};
		std::wstring message;
		std::vector<std::wstring> warnings;
		explicit operator bool() const noexcept { return error == ImageError::none; }
	};

	struct DecodeOptions
	{
		int maximumWidth{};
		int maximumHeight{};
		bool thumbnail{};
		bool applyOrientation{true};
		std::uint64_t maximumBytes{256ull * 1024 * 1024};
		std::stop_token stop;
		std::function<bool()> cancelled;
		std::function<void(int, int)> progress;
	};

	struct DecodeResult : OperationResult
	{
		PixelImage image;
		bool reduced{};
		int orientation{1};
	};

	std::filesystem::path native_path(const std::filesystem::path& path);
	std::wstring lowercase_extension(const std::filesystem::path& path);
	bool supports_extension(std::wstring_view extension);
	bool is_preview_only(std::wstring_view extension);
	// What a photo states of itself - its camera, when it was taken, its exposure - as libfiles reads it,
	// for every format libfiles scans. Empty for a file that cannot be opened or that libfiles does not read.
	libfiles::properties read_properties(const std::filesystem::path& path);
	// The colour profile a photo carries, as libfiles summarises one: its name, and the gamut libfiles
	// recognises in its colorants. Nothing when it carries none.
	std::optional<libfiles::icc_summary> read_colour_profile(const std::filesystem::path& path);
	bool read_dimensions(const std::filesystem::path& path, int& width, int& height);
	int read_orientation(const std::filesystem::path& path);
	bool orient_upright(PixelImage& image, int orientation);
	DecodeResult decode(const std::filesystem::path& path, const DecodeOptions& options = {});
	DecodeResult decode(std::span<const std::uint8_t> bytes, std::wstring_view extension,
	                    const DecodeOptions& options = {});
	PixelImage downsample_bgra_2x(const PixelImage& source);
	std::vector<ImageSaveFormat> writable_image_formats();
	const wchar_t* image_save_format_name(ImageSaveFormat format);
	const wchar_t* image_save_extension(ImageSaveFormat format);

	// The caller owns this temporary file and decides whether and how to publish it.
	OperationResult encode_to_file(const PixelImage& image, const std::filesystem::path& temporary,
	                              ImageSaveFormat format, const SaveOptions& options = {});
}
