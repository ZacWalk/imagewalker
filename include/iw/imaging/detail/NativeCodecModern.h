#pragma once

// Preserve historical min/max macros around inline modern code.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include <filesystem>
#include <span>
#include <string_view>
#include <cstdint>

namespace iw::imaging
{
	struct DecodeOptions;
	struct DecodeResult;
}

namespace iw::imaging::detail
{
	bool modern_native_supports_extension(std::wstring_view extension);
	bool modern_is_preview_only(std::wstring_view extension);
	DecodeResult modern_decode_native(const std::filesystem::path& path, const DecodeOptions& options);
	DecodeResult modern_decode_native(std::span<const std::uint8_t> bytes, std::wstring_view extension,
	                                  const DecodeOptions& options);
	bool modern_native_dimensions(const std::filesystem::path& path, int& width, int& height);
}

#include <iw/imaging/Codec.h>

#include <iw/imaging/ImageResampler.h>
#include <iw/imaging/detail/PlatformFileStream.h>
#include <libfiles/decode.h>
#include <libfiles/document.h>
#include <libfiles/identify.h>
#include <libfiles/scan.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <limits>

namespace iw::imaging::detail
{
	namespace native_codec_detail
	{
		inline std::string narrow_extension(const std::wstring_view extension)
		{
			std::string result;
			result.reserve(extension.size());
			for (const auto value : extension)
			{
				if (value == L'.' && result.empty()) continue;
				if (value > 0x7f) return {};
				result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
			}
			return result;
		}

		inline std::optional<libfiles::image_format> format_from_extension(const std::wstring_view extension)
		{
			const auto narrow = narrow_extension(extension);
			if (narrow.empty()) return {};
			const auto type = libfiles::file_type_from_extension(narrow);
			const auto format = libfiles::info(type).image;
			return format == libfiles::image_format::unknown ? std::nullopt : std::optional{format};
		}

		inline libfiles::image_format identify_format(libfiles::reader& input, const std::wstring_view extension)
		{
			const auto head = input.view(0, static_cast<std::size_t>((std::min)(input.size(), std::uint64_t{16})));
			const auto format = libfiles::detect_image_format(head);
			if (format != libfiles::image_format::unknown) return format;
			// A .webp extension is not enough to send another RIFF file to the WebP decoder.
			const auto hint = format_from_extension(extension).value_or(libfiles::image_format::unknown);
			return hint == libfiles::image_format::webp ? libfiles::image_format::unknown : hint;
		}

		inline bool cancelled(const DecodeOptions& options)
		{
			return options.stop.stop_requested() || (options.cancelled && options.cancelled());
		}

		class BgraSink final : public libfiles::image_sink
		{
		public:
			explicit BgraSink(const DecodeOptions& options) : options_(options) {}

			PixelImage image;
			libfiles::frame_info frame;

			bool accept(const libfiles::frame_info& value) override
			{
				frame = value;
				return !native_codec_detail::cancelled(options_);
			}

			libfiles::pixel_buffer allocate(const libfiles::frame_info& value) override
			{
				frame = value;
				if (value.width == 0 || value.height == 0 || value.width > INT_MAX || value.height > INT_MAX ||
				    static_cast<std::uint64_t>(value.width) * value.height >
				        (std::min)(options_.maximumBytes, static_cast<std::uint64_t>(SIZE_MAX)) / 4)
					return {};
				try
				{
					image.width = static_cast<int>(value.width);
					image.height = static_cast<int>(value.height);
					image.originalWidth = static_cast<int>(value.source_width ? value.source_width : value.width);
					image.originalHeight = static_cast<int>(value.source_height ? value.source_height : value.height);
					image.pixels.resize(static_cast<std::size_t>(value.width) * value.height);
				}
				catch (const std::bad_alloc&)
				{
					return {};
				}
				return {reinterpret_cast<std::uint8_t*>(image.pixels.data()),
				        static_cast<std::size_t>(value.width) * sizeof(std::uint32_t)};
			}

			bool cancelled() override { return native_codec_detail::cancelled(options_); }

		private:
			const DecodeOptions& options_;
		};

		inline DecodeResult map_decode_result(const libfiles::decode_result& decoded, BgraSink& sink,
		                                      const DecodeOptions& options)
		{
			DecodeResult result;
			result.orientation = decoded.frame.orientation;
			result.image = std::move(sink.image);
			if (decoded.frame.format == libfiles::pixel_format::bgrx8)
			{
				for (auto& pixel : result.image.pixels) pixel |= 0xff000000u;
			}
			if (decoded.cancelled || cancelled(options))
			{
				result.image = {};
				result.error = ImageError::cancelled;
				result.message = L"Image decoding was cancelled.";
				return result;
			}
			if (decoded.refused)
			{
				result.image = {};
				result.error = ImageError::resource_limit;
				result.message = L"The image exceeds the decoded-memory limit.";
				return result;
			}
			if (!decoded.success)
			{
				result.image = {};
				result.error = decoded.truncated ? ImageError::read_failed : ImageError::invalid_input;
				result.message = std::wstring(decoded.error.begin(), decoded.error.end());
				if (result.message.empty()) result.message = L"The image could not be decoded.";
				return result;
			}
			for (const auto& warning : decoded.warnings) result.warnings.emplace_back(warning.begin(), warning.end());
			if (options.applyOrientation && !decoded.frame.transformed && result.orientation >= 2 &&
			    result.orientation <= 8 && !orient_upright(result.image, result.orientation))
			{
				result.image = {};
				result.error = ImageError::read_failed;
				result.message = L"The image orientation could not be applied.";
				return result;
			}
			result.reduced = result.image.width != result.image.originalWidth ||
			                 result.image.height != result.image.originalHeight;
			if (options.progress) options.progress(result.image.height, result.image.height);
			return result;
		}

		inline sizei preview_size(const sizei source, const DecodeOptions& options)
		{
			if (source.cx <= 0 || source.cy <= 0 || options.maximumWidth <= 0 || options.maximumHeight <= 0)
				return source;
			const auto fit = (std::min)(static_cast<double>(options.maximumWidth) / source.cx,
			                            static_cast<double>(options.maximumHeight) / source.cy);
			// Match the existing viewer's full/half/quarter tiers; thumbnails have a hard fit bound instead.
			if (!options.thumbnail)
			{
				const int divisor = fit <= .25 ? 4 : fit <= .5 ? 2 : 1;
				return {1 + (source.cx - 1) / divisor, 1 + (source.cy - 1) / divisor};
			}
			const auto scale = (std::min)(1.0, fit);
			return {std::clamp(static_cast<int>(std::lround(source.cx * scale)), 1, options.maximumWidth),
			        std::clamp(static_cast<int>(std::lround(source.cy * scale)), 1, options.maximumHeight)};
		}

		inline void bound_preview(DecodeResult& result, const DecodeOptions& options)
		{
			if (!result || options.maximumWidth <= 0 || options.maximumHeight <= 0) return;
			auto& image = result.image;
			const auto size = preview_size({image.originalWidth, image.originalHeight}, options);
			if (image.width <= size.cx && image.height <= size.cy) return;
			try
			{
				auto pixels = resample_bgra(image.pixels, {image.width, image.height},
				                            {0.0, 0.0, static_cast<double>(image.width),
				                             static_cast<double>(image.height)},
				                            size);
				if (!pixels.empty())
				{
					image.pixels = std::move(pixels);
					image.width = size.cx;
					image.height = size.cy;
					result.reduced = true;
					return;
				}
			}
			catch (const std::bad_alloc&)
			{
			}
			result.image = {};
			result.error = ImageError::resource_limit;
			result.message = L"The preview could not be resized within its memory limit.";
		}

		struct FirstFrame final : libfiles::document_sink
		{
			explicit FirstFrame(const DecodeOptions& options) : keeper(options.maximumBytes), options(options) {}
			bool begin_frame(const libfiles::document_frame& frame) override
			{
				if (complete) return false;
				return keeper.begin_frame(frame);
			}
			void row(std::uint32_t y, std::span<const std::uint8_t> bytes) override { keeper.row(y, bytes); }
			void end_frame(const libfiles::document_frame&) override { complete = true; }
			void progress(std::uint32_t current, std::uint32_t total) override
			{
				if (options.progress) options.progress(static_cast<int>(current), static_cast<int>(total));
			}
			libfiles::document_keeper keeper;
			const DecodeOptions& options;
			bool complete{};
		};

		inline DecodeResult decode_reader(libfiles::reader& input, const std::wstring_view extension,
		                                  const DecodeOptions& options)
		{
			DecodeResult result;
			if (cancelled(options))
			{
				result.error = ImageError::cancelled;
				result.message = L"Image decoding was cancelled.";
				return result;
			}
			if (options.maximumBytes == 0 || options.maximumWidth < 0 || options.maximumHeight < 0 ||
			    (options.thumbnail && (options.maximumWidth == 0 || options.maximumHeight == 0)))
			{
				result.error = ImageError::invalid_input;
				result.message = L"Invalid image decode request.";
				return result;
			}

			const auto format = identify_format(input, extension);
			if (format == libfiles::image_format::unknown ||
			    (!libfiles::can_decode(format) && !libfiles::can_decode_document(format)))
			{
				result.error = ImageError::unsupported;
				result.message = L"No native-storage decoder recognizes this image.";
				return result;
			}

			if (libfiles::can_decode(format))
			{
				BgraSink sink(options);
				libfiles::decode_options decode_options;
				const bool bounded = options.thumbnail || format == libfiles::image_format::webp;
				decode_options.target_width = bounded ? options.maximumWidth : 0;
				decode_options.target_height = bounded ? options.maximumHeight : 0;
				if (format == libfiles::image_format::jpeg && !bounded && options.maximumWidth > 0 &&
				    options.maximumHeight > 0)
				{
					const auto scanned = libfiles::scan(input);
					if (scanned.width > 0 && scanned.height > 0 && scanned.width <= INT_MAX &&
					    scanned.height <= INT_MAX)
					{
						const auto angle = scanned.orientation.value_or(1);
						const bool turn = options.applyOrientation && angle >= 5 && angle <= 8;
						sizei stored{static_cast<int>(scanned.width), static_cast<int>(scanned.height)};
						if (turn) std::swap(stored.cx, stored.cy);
						auto target = preview_size(stored, options);
						if (turn) std::swap(target.cx, target.cy);
						decode_options.target_width = target.cx;
						decode_options.target_height = target.cy;
					}
				}
				decode_options.round_webp_size_down = format == libfiles::image_format::webp;
				decode_options.source_format = format;
				const auto decoded = libfiles::decode(input, sink, decode_options);
				result = map_decode_result(decoded, sink, options);
				bound_preview(result, options);
				return result;
			}

			const auto scanned = libfiles::scan(input);
			// libtiff's RGBA path can already reflect the rows. WIC exposes stored rows,
			// letting the display layer apply each non-default TIFF orientation once.
			if (format == libfiles::image_format::tiff && scanned.orientation.value_or(1) != 1)
			{
				result.error = ImageError::unsupported;
				result.message = L"This TIFF orientation needs the Windows codec.";
				return result;
			}
			libfiles::document_options document_options;
			document_options.thumbnail_width = options.thumbnail ? static_cast<std::uint32_t>(options.maximumWidth) : 0;
			document_options.thumbnail_height = options.thumbnail ? static_cast<std::uint32_t>(options.maximumHeight)
			                                                      : 0;
			document_options.maximum_working_bytes = options.maximumBytes;
			document_options.maximum_retained_bytes = options.maximumBytes;
			document_options.cancelled = [&options]
			{
				return cancelled(options);
			};
			FirstFrame output(options);
			const auto decoded = libfiles::decode_document(input, format, output, document_options);
			if (decoded.cancelled || cancelled(options))
			{
				result.error = ImageError::cancelled;
				result.message = L"Image decoding was cancelled.";
				return result;
			}
			if (decoded.resource_limit || (decoded.refused && !output.complete))
			{
				result.error = ImageError::resource_limit;
				result.message = L"The image exceeds the decoded-memory limit.";
				return result;
			}
			const bool keepComplete = decoded.refused ||
			                          (decoded.truncated && (format == libfiles::image_format::gif ||
			                                                 format == libfiles::image_format::tiff));
			if (!output.complete || (!decoded.success && !keepComplete))
			{
				// WIC supports some TIFF sample layouts and compressions not in the native build.
				if (format == libfiles::image_format::tiff && scanned.success && !decoded.truncated)
				{
					result.error = ImageError::unsupported;
					result.message = L"This TIFF encoding needs the Windows codec.";
					return result;
				}
				result.error = decoded.truncated ? ImageError::read_failed : ImageError::invalid_input;
				result.message = std::wstring(decoded.error.begin(), decoded.error.end());
				if (result.message.empty()) result.message = L"The image could not be decoded.";
				return result;
			}
			const auto& frame = output.keeper.document.frames.front();
			if (static_cast<std::uint64_t>(frame.width) * frame.height > options.maximumBytes / 4)
			{
				result.error = ImageError::resource_limit;
				result.message = L"The image exceeds the decoded-memory limit.";
				return result;
			}
			result.image.width = static_cast<int>(frame.width);
			result.image.height = static_cast<int>(frame.height);
			result.orientation = scanned.orientation.value_or(1);
			result.image.originalWidth = scanned.width ? static_cast<int>(scanned.width) : result.image.width;
			result.image.originalHeight = scanned.height ? static_cast<int>(scanned.height) : result.image.height;
			result.image.pixels.resize(static_cast<std::size_t>(frame.width) * frame.height);
			libfiles::copy_bgra(frame.rows(), result.image.pixels, frame.width, false);
			for (const auto& warning : decoded.warnings) result.warnings.emplace_back(warning.begin(), warning.end());
			if (options.applyOrientation && !orient_upright(result.image, result.orientation))
			{
				result.image = {};
				result.error = ImageError::read_failed;
				result.message = L"The image orientation could not be applied.";
				return result;
			}
			result.reduced = result.image.width != result.image.originalWidth ||
			                 result.image.height != result.image.originalHeight;
			bound_preview(result, options);
			return result;
		}

		inline DecodeResult read_bounded(libfiles::reader& input, const std::wstring_view extension,
		                                 const DecodeOptions& options)
		{
			try
			{
				return decode_reader(input, extension, options);
			}
			catch (const libfiles::read_error& error)
			{
				const std::string_view message(error.what());
				DecodeResult result;
				result.error = ImageError::read_failed;
				result.message.assign(message.begin(), message.end());
				return result;
			}
			catch (const std::bad_alloc&)
			{
				DecodeResult result;
				result.error = ImageError::resource_limit;
				result.message = L"The image exceeds the decoded-memory limit.";
				return result;
			}
		}
	}

	inline bool modern_native_supports_extension(const std::wstring_view extension)
	{
		using namespace native_codec_detail;
		const auto format = format_from_extension(extension);
		return format && (libfiles::can_decode(*format) || libfiles::can_decode_document(*format));
	}

	inline bool modern_is_preview_only(const std::wstring_view extension)
	{
		using namespace native_codec_detail;
		std::wstring lower(extension);
		std::ranges::transform(lower, lower.begin(),
		                       [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
		return lower == L".ppt" || lower == L".pps" || lower == L".vsd";
	}

	inline DecodeResult modern_decode_native(const std::span<const std::uint8_t> bytes,
	                                         const std::wstring_view extension, const DecodeOptions& options)
	{
		using namespace native_codec_detail;
		if (bytes.empty() || bytes.size() > INT_MAX)
		{
			DecodeResult result;
			result.error = ImageError::invalid_input;
			result.message = L"The encoded image is empty or exceeds the input limit.";
			return result;
		}
		libfiles::reader input(bytes);
		return read_bounded(input, extension, options);
	}

	inline DecodeResult modern_decode_native(const std::filesystem::path& path, const DecodeOptions& options)
	{
		using namespace native_codec_detail;
		PlatformFileStream source(path);
		if (!source.is_open())
		{
			DecodeResult result;
			result.error = ImageError::read_failed;
			result.message = L"The image file could not be opened.";
			return result;
		}
		libfiles::reader input(source);
		return read_bounded(input, path.extension().wstring(), options);
	}

	inline bool modern_native_dimensions(const std::filesystem::path& path, int& width, int& height)
	{
		using namespace native_codec_detail;
		PlatformFileStream source(path);
		if (!source.is_open()) return false;
		libfiles::reader input(source);
		const auto scanned = libfiles::scan(input);
		if (scanned.width == 0 || scanned.height == 0) return false;
		width = static_cast<int>(scanned.width);
		height = static_cast<int>(scanned.height);
		if (scanned.orientation && *scanned.orientation >= 5 && *scanned.orientation <= 8) std::swap(width, height);
		return true;
	}
}

#pragma pop_macro("max")
#pragma pop_macro("min")
