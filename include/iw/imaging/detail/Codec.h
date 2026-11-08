#pragma once

#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include <iw/imaging/detail/NativeCodecModern.h>
#include "iw/imaging/Codec.h"
#include "iw/imaging/Geometry.h"
#include "iw/imaging/WebPCodec.h"
#include <iw/imaging/detail/PlatformFileStream.h>

#include <libdesktop/platform.h>
#include <libfiles/document.h>
#include <libfiles/encode.h>
#include <libfiles/icc.h>
#include <libfiles/identify.h>
#include <libfiles/scan.h>


#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace iw::imaging
{
	namespace codec_detail
	{
		inline bool cancelled(const DecodeOptions& options)
		{
			return options.stop.stop_requested() ||
				(options.cancelled && options.cancelled());
		}



		inline DecodeResult failed_decode(const ImageError error,
		                           const wchar_t* message)
		{
			DecodeResult result;
			result.error = error;
			result.message = message;
			return result;
		}

		inline ::core::file_path desktop_path(const std::filesystem::path& path)
		{
			const auto utf8 = path.u8string();
			return ::core::file_path(std::string(
				reinterpret_cast<const char*>(utf8.data()), utf8.size()));
		}

		inline ::core::file_path core_path(const std::filesystem::path& path)
		{
			return desktop_path(path);
		}

		inline std::wstring lower_extension(const std::filesystem::path& path)
		{
			auto result = path.extension().wstring();
			std::ranges::transform(result, result.begin(), [](const wchar_t value)
			{
				return static_cast<wchar_t>(std::towlower(value));
			});
			return result;
		}

		inline std::wstring lower_extension(const std::wstring_view extension)
		{
			std::wstring result(extension);
			std::ranges::transform(result, result.begin(), [](const wchar_t value)
			{
				return static_cast<wchar_t>(std::towlower(value));
			});
			return result;
		}

		inline int progress_value(const std::uint64_t value)
		{
			return static_cast<int>((std::min)(value,
				static_cast<std::uint64_t>(INT_MAX)));
		}

		inline ::platform::image_load_options bitmap_options(const DecodeOptions& options)
		{
			::platform::image_load_options result;
			result.max_decoded_bytes = options.maximumBytes;
			result.target_width = options.maximumWidth > 0
				? static_cast<std::uint32_t>(options.maximumWidth) : 0;
			result.target_height = options.maximumHeight > 0
				? static_cast<std::uint32_t>(options.maximumHeight) : 0;
			result.thumbnail = options.thumbnail;
			result.apply_orientation = options.applyOrientation;
			result.cancelled = [&options] { return cancelled(options); };
			result.progress = [&options](const std::uint64_t decoded,
			                             const std::uint64_t source)
			{
				if (options.progress)
					options.progress(progress_value(decoded), progress_value(source));
			};
			return result;
		}

		inline DecodeResult map_bitmap_result(::platform::image_load_result loaded,
		                              const DecodeOptions& options)
		{
			DecodeResult result;
			result.orientation = static_cast<int>(loaded.stored_orientation);
			if (loaded.error == ::platform::image_load_error::cancelled ||
				cancelled(options))
			{
				result.error = ImageError::cancelled;
				result.message = L"Image decoding was cancelled.";
				return result;
			}
			if (loaded.error == ::platform::image_load_error::too_large)
			{
				result.error = ImageError::resource_limit;
				result.message = L"The image exceeds the decoded-memory limit.";
				return result;
			}
			if (!loaded.image)
			{
				result.error = loaded.error == ::platform::image_load_error::unavailable
					? ImageError::unsupported : ImageError::read_failed;
				result.message = loaded.diagnostic.empty()
					? L"The Windows image codec could not decode the image."
					: std::wstring(loaded.diagnostic.begin(), loaded.diagnostic.end());
				return result;
			}
			const auto& surface = *loaded.image;
			const auto width = surface.width();
			const auto height = surface.height();
			const auto max_size = (std::numeric_limits<std::size_t>::max)();
			if (width == 0 || height == 0 ||
				width > static_cast<std::uint32_t>(INT_MAX) ||
				height > static_cast<std::uint32_t>(INT_MAX) ||
				static_cast<std::uint64_t>(width) * height >
					static_cast<std::uint64_t>(max_size / sizeof(std::uint32_t)) ||
				static_cast<std::size_t>(width) > max_size / sizeof(std::uint32_t))
			{
				result.error = ImageError::resource_limit;
				result.message = L"The decoded image dimensions are too large.";
				return result;
			}
			const auto row_bytes = static_cast<std::size_t>(width) * sizeof(std::uint32_t);
			if (surface.stride() < row_bytes || surface.stride() > max_size / height ||
				surface.size() < surface.stride() * height)
			{
				result.error = ImageError::resource_limit;
				result.message = L"The decoded image dimensions are too large.";
				return result;
			}
			const auto original_width = loaded.source_width ? loaded.source_width : width;
			const auto original_height = loaded.source_height ? loaded.source_height : height;
			if (original_width > static_cast<std::uint32_t>(INT_MAX) ||
				original_height > static_cast<std::uint32_t>(INT_MAX))
			{
				result.error = ImageError::resource_limit;
				result.message = L"The decoded image dimensions are too large.";
				return result;
			}
			try
			{
				result.image.width = static_cast<int>(width);
				result.image.height = static_cast<int>(height);
				result.image.pixels.resize(static_cast<std::size_t>(width) * height);
				for (std::uint32_t y = 0; y < height; ++y)
					std::memcpy(result.image.pixels.data() +
						static_cast<std::size_t>(y) * width,
						surface.pixels_line(static_cast<int>(y)), row_bytes);
			}
			catch (const std::bad_alloc&)
			{
				result.image = {};
				result.error = ImageError::resource_limit;
				result.message = L"The decoded image dimensions are too large.";
				return result;
			}
			result.image.originalWidth = static_cast<int>(original_width);
			result.image.originalHeight = static_cast<int>(original_height);
			result.reduced = result.image.width != result.image.originalWidth ||
				result.image.height != result.image.originalHeight;
			return result;
		}
		inline std::optional<libfiles::image_format> image_format(
			const ImageSaveFormat format)
		{
			switch (format)
			{
			case ImageSaveFormat::jpeg: return libfiles::image_format::jpeg;
			case ImageSaveFormat::bmp: return libfiles::image_format::bmp;
			case ImageSaveFormat::tiff: return libfiles::image_format::tiff;
			case ImageSaveFormat::webp: return libfiles::image_format::webp;
			case ImageSaveFormat::png: return libfiles::image_format::png;
			default: return {};
			}
		}

		inline void add_source_metadata(libfiles::decoded_document& destination,
		                         const std::filesystem::path& source,
		                         const CarryMetadata carry,
		                         std::vector<std::wstring>& warnings,
		                         std::vector<std::uint8_t>* jpeg_header)
		{
			if (carry == CarryMetadata::none || source.empty()) return;
			PlatformFileStream input(source);
			if (!input.is_open())
			{
				warnings.emplace_back(L"The metadata source could not be opened; saved pixels only.");
				return;
			}
			libfiles::reader reader(input);
			const auto scanned = libfiles::scan(reader);
			if (jpeg_header && carry == CarryMetadata::all &&
				scanned.format == libfiles::image_format::jpeg)
			{
				*jpeg_header = libfiles::read_jpeg_header(reader);
				if (jpeg_header->empty())
					warnings.emplace_back(L"The JPEG marker header could not be read; saved decoded metadata only.");
			}
			const auto append_metadata = [&](const libfiles::document_metadata kind,
			                                const std::vector<std::uint8_t>& bytes)
			{
				if (bytes.empty()) return;
				// The output is always RGB/BGRA. An opaque source ICC profile
				// must not be attached as though it described those pixels.
				if (kind == libfiles::document_metadata::source_icc ||
					(kind == libfiles::document_metadata::icc && !libfiles::icc_declares_rgb(bytes)))
				{
					warnings.emplace_back(L"A non-RGB source profile was not attached to the saved RGB pixels.");
					return;
				}
				if (carry == CarryMetadata::profile &&
					kind != libfiles::document_metadata::icc)
					return;
				// JPEG-to-JPEG saves carry the original markers after encoding, not reconstructed blocks.
				if (jpeg_header && !jpeg_header->empty()) return;
				destination.metadata.push_back({kind, bytes});
			};
			const auto append_scanned_metadata = [&]
			{
				append_metadata(libfiles::document_metadata::exif, scanned.metadata.exif);
				append_metadata(libfiles::document_metadata::xmp, scanned.metadata.xmp);
				append_metadata(libfiles::document_metadata::iptc, scanned.metadata.iptc);
				append_metadata(libfiles::document_metadata::icc, scanned.metadata.icc);
				append_metadata(libfiles::document_metadata::photoshop, scanned.metadata.photoshop);
			};
			if (!libfiles::can_decode_document(scanned.format))
			{
				append_scanned_metadata();
				warnings.emplace_back(
					L"The metadata source has no native document decoder; used any metadata found by scanning.");
				return;
			}
			libfiles::decoded_document loaded;
			libfiles::document_options decode_options;
			decode_options.pixels = false;
			decode_options.maximum_working_bytes = 1ull * 1024 * 1024 * 1024;
			decode_options.maximum_retained_bytes = 1ull * 1024 * 1024 * 1024;
			const auto decoded = libfiles::decode_document(reader, scanned.format,
				loaded, decode_options);
			if (!decoded.success)
			{
				// A scanner can still provide safe opaque blocks when a native
				// metadata decoder refuses the whole source.
				append_scanned_metadata();
				warnings.emplace_back(L"The metadata source could not be fully decoded; saved scanned metadata only.");
				return;
			}
			for (const auto& metadata : loaded.metadata)
				append_metadata(metadata.kind, metadata.bytes);
			if (loaded.info.has_resolution)
			{
				destination.info.has_resolution = true;
				destination.info.pixels_per_meter_x =
					loaded.info.pixels_per_meter_x;
				destination.info.pixels_per_meter_y =
					loaded.info.pixels_per_meter_y;
			}
		}

		inline std::optional<libfiles::decoded_document> document_from_pixels(
			const PixelImage& image, const SaveOptions& options,
			std::vector<std::wstring>& warnings, std::vector<std::uint8_t>* jpeg_header)
		{
			if (!valid_pixels(image.width, image.height, image.pixels.size()) ||
				image.width <= 0 || image.height <= 0)
				return {};
			const auto width = static_cast<std::uint32_t>(image.width);
			const auto height = static_cast<std::uint32_t>(image.height);
			const auto stride = static_cast<std::size_t>(width) * sizeof(std::uint32_t);
			if (height > std::numeric_limits<std::size_t>::max() / stride)
				return {};

			libfiles::decoded_document result;
			result.info.width = width;
			result.info.height = height;
			result.info.storage = libfiles::document_storage::bgra32;
			result.info.bits_per_pixel = 32;
			result.info.frames = 1;
			libfiles::decoded_frame frame;
			frame.width = width;
			frame.height = height;
			frame.storage = libfiles::document_storage::bgra32;
			frame.stride = stride;
			frame.pixels.resize(stride * height);
			std::memcpy(frame.pixels.data(), image.pixels.data(), frame.pixels.size());
			result.frames.push_back(std::move(frame));
			add_source_metadata(result, options.metadataSource, options.carry, warnings, jpeg_header);
			return result;
		}

		inline bool write_bytes(const std::filesystem::path& path,
		                const std::span<const std::uint8_t> bytes)
		{
			auto file = ::platform::open_file(desktop_path(path),
				::platform::file_open_mode::create);
			if (!file || file->write(bytes.data(), bytes.size()) != bytes.size())
				return false;
			return file->flush();
		}

		inline std::uint32_t average_2x2(const std::uint32_t top_left,
		                          const std::uint32_t top_right,
		                          const std::uint32_t bottom_left,
		                          const std::uint32_t bottom_right)
		{
			const std::uint32_t low = (top_left & 0x00ff00ff) +
				(top_right & 0x00ff00ff) + (bottom_left & 0x00ff00ff) +
				(bottom_right & 0x00ff00ff);
			const std::uint32_t high = ((top_left >> 8) & 0x00ff00ff) +
				((top_right >> 8) & 0x00ff00ff) +
				((bottom_left >> 8) & 0x00ff00ff) +
				((bottom_right >> 8) & 0x00ff00ff);
			return (((low + 0x00020002) >> 2) & 0x00ff00ff) |
				((((high + 0x00020002) >> 2) & 0x00ff00ff) << 8);
		}

		inline bool sideways(const std::uint16_t orientation)
		{
			return orientation >= 5 && orientation <= 8;
		}
	}

	inline std::filesystem::path native_path(const std::filesystem::path& path)
	{
		using namespace codec_detail;
		return std::filesystem::path(
			::platform::to_file_system_path(desktop_path(path)));
	}

	inline std::wstring lowercase_extension(const std::filesystem::path& path)
	{
		using namespace codec_detail;
		return lower_extension(path);
	}

	inline bool has_webp_signature(const std::span<const std::uint8_t> header)
	{
		using namespace codec_detail;
		return header.size() >= 12 &&
			std::memcmp(header.data(), "RIFF", 4) == 0 &&
			std::memcmp(header.data() + 8, "WEBP", 4) == 0;
	}

	inline PixelDecoder pixel_decoder(const std::wstring_view extension,
	                           const std::span<const std::uint8_t> header)
	{
		using namespace codec_detail;
		if (header.empty() ? lower_extension(extension) == L".webp" :
			has_webp_signature(header))
			return PixelDecoder::webp;
		const auto wic_extensions = windows_image_extensions();
		const auto lower = lower_extension(extension);
		if (lower == L".webp" || lower == L".png" ||
			lower == L".tif" || lower == L".tiff" ||
			std::ranges::find(wic_extensions, lower) != wic_extensions.end() ||
			!detail::modern_native_supports_extension(extension))
			return PixelDecoder::wic;
		return PixelDecoder::native;
	}

	inline bool read_dimensions(const std::filesystem::path& path, int& width, int& height)
	{
		using namespace codec_detail;
		const auto backend = pixel_decoder(lower_extension(path));
		if (backend == PixelDecoder::webp)
			return webp::probe(path, width, height);
		if (backend == PixelDecoder::native)
			return detail::modern_native_dimensions(path, width, height);
		const auto probe = ::platform::probe_image_file(core_path(path));
		if (!probe || probe->width > INT_MAX || probe->height > INT_MAX)
			return false;
		width = static_cast<int>(probe->width);
		height = static_cast<int>(probe->height);
		if (sideways(probe->orientation))
			std::swap(width, height);
		return true;
	}

	inline int read_orientation(const std::filesystem::path& path)
	{
		using namespace codec_detail;
		if (lowercase_extension(path) == L".webp") return 1;
		const auto probe = ::platform::probe_image_file(core_path(path));
		return probe && probe->orientation ? probe->orientation : 1;
	}

	inline bool orient_upright(PixelImage& image, const int orientation)
	{
		using namespace codec_detail;
		if (orientation < 2 || orientation > 8) return true;
		const int width = image.width;
		const int height = image.height;
		if (width <= 0 || height <= 0 ||
			image.pixels.size() < static_cast<std::size_t>(width) * height)
			return false;
		auto* const pixels = image.pixels.data();
		const auto row = [pixels, width](const int y)
		{
			return pixels + static_cast<std::size_t>(y) * width;
		};
		switch (orientation)
		{
		case 2:
			for (int y = 0; y < height; ++y)
				std::reverse(row(y), row(y) + width);
			return true;
		case 3:
			std::reverse(pixels, pixels + static_cast<std::size_t>(width) * height);
			return true;
		case 4:
			for (int y = 0; y < height / 2; ++y)
				std::swap_ranges(row(y), row(y) + width, row(height - 1 - y));
			return true;
		default:
			break;
		}
		std::vector<std::uint32_t> turned;
		try
		{
			turned.resize(static_cast<std::size_t>(width) * height);
		}
		catch (const std::bad_alloc&)
		{
			return false;
		}
		const int upright_width = height;
		const int upright_height = width;
		const bool from_right = orientation >= 7;
		const bool from_bottom = orientation == 6 || orientation == 7;
		constexpr int tile = 64;
		for (int top = 0; top < upright_height; top += tile)
			for (int left = 0; left < upright_width; left += tile)
				for (int y = top; y < (std::min)(top + tile, upright_height); ++y)
				{
					const auto* stored = pixels + (from_right ? width - 1 - y : y);
					auto* destination = turned.data() +
						static_cast<std::size_t>(y) * upright_width;
					for (int x = left; x < (std::min)(left + tile, upright_width); ++x)
						destination[x] = stored[static_cast<std::size_t>(
							from_bottom ? height - 1 - x : x) * width];
				}
		image.pixels = std::move(turned);
		image.width = upright_width;
		image.height = upright_height;
		std::swap(image.originalWidth, image.originalHeight);
		return true;
	}

	inline bool supports_extension(const std::wstring_view extension)
	{
		using namespace codec_detail;
		const auto lower = lower_extension(extension);
		const auto extensions = windows_image_extensions();
		return detail::modern_native_supports_extension(lower) ||
			is_preview_only(lower) ||
			std::ranges::find(extensions, lower) != extensions.end();
	}

	inline bool is_preview_only(const std::wstring_view extension)
	{
		using namespace codec_detail;
		return detail::modern_is_preview_only(extension);
	}

	inline std::vector<std::wstring> windows_image_extensions()
	{
		using namespace codec_detail;
		std::vector<std::wstring> result;
		for (const auto& extension : ::platform::installed_image_extensions())
		{
			if (extension.empty()) continue;
			std::wstring value(extension.begin(), extension.end());
			if (value.front() != L'.') value.insert(value.begin(), L'.');
			std::ranges::transform(value, value.begin(), [](const wchar_t c)
			{
				return static_cast<wchar_t>(std::towlower(c));
			});
			result.push_back(std::move(value));
		}
		std::ranges::sort(result);
		result.erase(std::ranges::unique(result).begin(), result.end());
		return result;
	}

	inline DecodeResult decode(const std::filesystem::path& path,
	                    const DecodeOptions& options)
	{
		using namespace codec_detail;
		if (cancelled(options))
			return failed_decode(ImageError::cancelled,
				L"Image decoding was cancelled.");
		if (path.empty() || options.maximumBytes == 0 ||
			options.maximumWidth < 0 || options.maximumHeight < 0 ||
			(options.thumbnail && (options.maximumWidth == 0 ||
				options.maximumHeight == 0)))
			return failed_decode(ImageError::invalid_input,
				L"Invalid image decode request.");

		const auto backend = pixel_decoder(lower_extension(path));
		if (backend == PixelDecoder::native || backend == PixelDecoder::webp)
			return detail::modern_decode_native(path, options);

		const auto loaded = ::platform::load_image_file(core_path(path),
			bitmap_options(options));
		return map_bitmap_result(std::move(loaded), options);
	}

	inline DecodeResult decode(const std::span<const std::uint8_t> bytes,
	                    const std::wstring_view extension,
	                    const DecodeOptions& options)
	{
		using namespace codec_detail;
		if (cancelled(options))
			return failed_decode(ImageError::cancelled,
				L"Image decoding was cancelled.");
		if (bytes.empty() || options.maximumBytes == 0 ||
			options.maximumWidth < 0 || options.maximumHeight < 0 ||
			(options.thumbnail && (options.maximumWidth == 0 ||
				options.maximumHeight == 0)))
			return failed_decode(ImageError::invalid_input,
				L"Invalid image decode request.");

		const auto backend = pixel_decoder(extension, bytes);
		if (backend == PixelDecoder::native || backend == PixelDecoder::webp)
			return detail::modern_decode_native(bytes, extension, options);

		return map_bitmap_result(::platform::load_image_memory(bytes,
			bitmap_options(options)), options);
	}

	inline DecodeResult decode_with(const PixelDecoder decoder,
	                         const std::span<const std::uint8_t> bytes,
	                         const std::wstring_view extension,
	                         const DecodeOptions& options)
	{
		using namespace codec_detail;
		if (decoder == PixelDecoder::native)
			return detail::modern_decode_native(bytes, extension, options);
		if (decoder == PixelDecoder::webp)
			return decode(bytes, L".webp", options);
		return map_bitmap_result(::platform::load_image_memory(bytes,
			bitmap_options(options)), options);
	}

	inline PixelImage downsample_bgra_2x(const PixelImage& source)
	{
		using namespace codec_detail;
		PixelImage result;
		if (!valid_pixels(source.width, source.height, source.pixels.size()) ||
			source.width <= 0 || source.height <= 0)
			return result;
		result.width = (source.width + 1) / 2;
		result.height = (source.height + 1) / 2;
		result.originalWidth = source.originalWidth ? source.originalWidth : source.width;
		result.originalHeight = source.originalHeight ? source.originalHeight : source.height;
		result.pixels.resize(static_cast<std::size_t>(result.width) * result.height);
		for (int y = 0; y < result.height; ++y)
		{
			const int source_y = y * 2;
			const auto* top = source.pixels.data() +
				static_cast<std::size_t>(source_y) * source.width;
			const auto* bottom = source.pixels.data() +
				static_cast<std::size_t>((std::min)(source_y + 1,
					source.height - 1)) * source.width;
			auto* destination = result.pixels.data() +
				static_cast<std::size_t>(y) * result.width;
			for (int x = 0; x < result.width; ++x)
			{
				const int source_x = (std::min)(x * 2, source.width - 1);
				const int next_x = (std::min)(source_x + 1, source.width - 1);
				destination[x] = average_2x2(top[source_x], top[next_x],
					bottom[source_x], bottom[next_x]);
			}
		}
		return result;
	}

	inline std::vector<ImageSaveFormat> writable_image_formats()
	{
		using namespace codec_detail;
		std::vector<ImageSaveFormat> result;
		for (const auto format : {ImageSaveFormat::png, ImageSaveFormat::jpeg,
			                      ImageSaveFormat::bmp, ImageSaveFormat::tiff,
			                      ImageSaveFormat::webp})
		{
			if (const auto native = image_format(format);
				native && libfiles::can_encode_document(*native))
				result.push_back(format);
		}
		return result;
	}

	inline const wchar_t* image_save_format_name(const ImageSaveFormat format)
	{
		using namespace codec_detail;
		switch (format)
		{
		case ImageSaveFormat::jpeg: return L"JPEG image (*.jpg)";
		case ImageSaveFormat::bmp: return L"Bitmap image (*.bmp)";
		case ImageSaveFormat::tiff: return L"TIFF image (*.tif)";
		case ImageSaveFormat::webp: return L"WebP image (*.webp)";
		default: return L"PNG image (*.png)";
		}
	}

	inline const wchar_t* image_save_extension(const ImageSaveFormat format)
	{
		using namespace codec_detail;
		switch (format)
		{
		case ImageSaveFormat::jpeg: return L".jpg";
		case ImageSaveFormat::bmp: return L".bmp";
		case ImageSaveFormat::tiff: return L".tif";
		case ImageSaveFormat::webp: return L".webp";
		default: return L".png";
		}
	}

	inline OperationResult encode_to_file(const PixelImage& image,
	                              const std::filesystem::path& temporary,
	                              const ImageSaveFormat format,
	                              const SaveOptions& options)
	{
		using namespace codec_detail;
		OperationResult result;
		const auto native = image_format(format);
		if (temporary.empty() || !native)
		{
			result.error = ImageError::invalid_input;
			result.message = L"Invalid or unsupported image encode request.";
			return result;
		}
		try
		{
			if (format == ImageSaveFormat::webp)
			{
				const auto encoded = ::iw::webp::encode(image, options);
				if (encoded.empty())
				{
					result.error = ImageError::encode_failed;
					result.message = L"The WebP encoder returned no data.";
				}
				else if (!write_bytes(temporary, encoded))
				{
					result.error = ImageError::encode_failed;
					result.message = L"The WebP temporary output could not be written.";
				}
				return result;
			}
			if (!libfiles::can_encode_document(*native))
			{
				result.error = ImageError::unsupported;
				result.message = L"The requested native encoder is unavailable.";
				return result;
			}
			std::vector<std::uint8_t> jpeg_header;
			auto document = document_from_pixels(image, options, result.warnings,
				*native == libfiles::image_format::jpeg ? &jpeg_header : nullptr);
			if (!document)
			{
				result.error = ImageError::invalid_input;
				result.message = L"Invalid image pixels.";
				return result;
			}
			libfiles::document_encode_options encode_options;
			encode_options.quality = options.quality;
			encode_options.reset_orientation_metadata = true;
			encode_options.legacy_compatibility =
				*native == libfiles::image_format::jpeg ||
				*native == libfiles::image_format::tiff;
			encode_options.maximum_working_bytes = 256ull * 1024 * 1024;
			auto encoded = libfiles::encode_document(*document, *native,
				encode_options);
			for (const auto& warning : encoded.warnings)
				result.warnings.emplace_back(warning.begin(), warning.end());
			if (!encoded.success)
			{
				result.error = encoded.resource_limit
					? ImageError::resource_limit
					: encoded.unsupported ? ImageError::unsupported
					: ImageError::encode_failed;
				result.message = encoded.error.empty()
					? L"The image could not be encoded."
					: std::wstring(encoded.error.begin(), encoded.error.end());
				return result;
			}
			if (!jpeg_header.empty())
			{
				auto carried = libfiles::carry_jpeg_metadata(jpeg_header, encoded.data, image.width, image.height);
				if (carried.empty())
					result.warnings.emplace_back(L"The photo's marker metadata could not be carried into the saved JPEG.");
				else
					encoded.data = std::move(carried);
			}
			if (!write_bytes(temporary, encoded.data))
			{
				result.error = ImageError::encode_failed;
				result.message = L"The encoded image could not be written.";
			}
		}
		catch (const std::bad_alloc&)
		{
			result.error = ImageError::resource_limit;
			result.message = L"Not enough memory to encode the image.";
		}
		catch (const std::exception&)
		{
			result.error = ImageError::encode_failed;
			result.message = L"The image encoder failed.";
		}
		return result;
	}
}

#pragma pop_macro("max")
#pragma pop_macro("min")
