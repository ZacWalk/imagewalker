#pragma once

// Preserve historical min/max macros around inline modern code.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include <iw/imaging/Document.h>
#include <iw/imaging/detail/PlatformFileStream.h>
#include <libfiles/identify.h>
#include <libfiles/scan.h>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstring>
#include <memory>

namespace iw::imaging
{
	namespace document_detail
	{
		inline bool cancelled(const DecodeOptions& options)
		{
			return options.stop.stop_requested() ||
				(options.cancelled && options.cancelled());
		}

		inline std::string narrow_extension(const std::wstring_view extension)
		{
			std::string result;
			result.reserve(extension.size());
			for (const auto value : extension)
			{
				if (value == L'.' && result.empty()) continue;
				if (value > 0x7f) return {};
				result.push_back(static_cast<char>(
					std::tolower(static_cast<unsigned char>(value))));
			}
			return result;
		}

		inline std::optional<libfiles::image_format> native_format_from_extension(
			const std::wstring_view extension)
		{
			const auto narrow = narrow_extension(extension);
			if (narrow.empty()) return {};
			const auto type = libfiles::file_type_from_extension(narrow);
			const auto format = libfiles::info(type).image;
			return libfiles::can_decode_document(format) ? std::optional{format} : std::nullopt;
		}

		class ThumbnailDocumentSink final : public libfiles::document_sink
		{
		public:
			explicit ThumbnailDocumentSink(const DecodeOptions& options) : options_(options) {}

			libfiles::decoded_document document;
			bool limited = false;

			bool begin_frame(const libfiles::document_frame& frame) override
			{
				if (document_detail::cancelled(options_)) return false;
				if (frame.width == 0 || frame.height == 0) return false;
				const auto width_limit = options_.maximumWidth > 0
					? static_cast<double>(options_.maximumWidth) / frame.width : 1.0;
				const auto height_limit = options_.maximumHeight > 0
					? static_cast<double>(options_.maximumHeight) / frame.height : 1.0;
				const auto scale = (std::min)({1.0, width_limit, height_limit});
				const auto width = static_cast<std::uint32_t>((std::max)(
					1.0, std::round(frame.width * scale)));
				const auto height = static_cast<std::uint32_t>((std::max)(
					1.0, std::round(frame.height * scale)));
				const auto bytes = static_cast<std::uint64_t>(width) * height * sizeof(std::uint32_t);
				const auto palette = static_cast<std::uint64_t>(frame.palette.size()) * sizeof(std::uint32_t);
				if (bytes > options_.maximumBytes ||
					palette > options_.maximumBytes - bytes ||
					retained_ > options_.maximumBytes - bytes - palette)
				{
					limited = true;
					return false;
				}

				libfiles::decoded_frame output;
				output.left = static_cast<std::int32_t>(std::lround(frame.left * scale));
				output.top = static_cast<std::int32_t>(std::lround(frame.top * scale));
				output.width = width;
				output.height = height;
				output.storage = libfiles::document_storage::bgra32;
				output.stride = static_cast<std::size_t>(width) * sizeof(std::uint32_t);
				output.pixels.resize(static_cast<std::size_t>(output.stride) * height);
				output.background = frame.background;
				output.delay_ms = frame.delay_ms;
				output.disposal = frame.disposal;
				document.frames.push_back(std::move(output));
				source_ = frame;
				scale_ = scale;
				row_pixels_.resize(frame.width);
				retained_ += bytes;
				return true;
			}

			void row(const std::uint32_t y, const std::span<const std::uint8_t> bytes) override
			{
				if (document.frames.empty() || y >= source_.height) return;
				libfiles::pixel_rows source{
					source_.width, 1, source_.storage, bytes.size(), false, bytes,
					source_.palette, source_.transparent};
				libfiles::copy_bgra(source, row_pixels_, source_.width, false);
				auto& output = document.frames.back();
				const auto output_y = (std::min)(
					output.height - 1,
					static_cast<std::uint32_t>(static_cast<double>(y) * scale_));
				auto* destination = reinterpret_cast<std::uint32_t*>(
					output.pixels.data() + static_cast<std::size_t>(output_y) * output.stride);
				for (std::uint32_t x = 0; x < output.width; ++x)
				{
					const auto source_x = (std::min)(source_.width - 1,
						static_cast<std::uint32_t>(static_cast<double>(x) / scale_));
					destination[x] = row_pixels_[source_x];
				}
			}

			void metadata(const libfiles::document_metadata kind,
			              const std::span<const std::uint8_t> bytes) override
			{
				if (bytes.size() > options_.maximumBytes ||
					retained_ > options_.maximumBytes - bytes.size())
				{
					limited = true;
					return;
				}
				document.metadata.push_back({kind, {bytes.begin(), bytes.end()}});
				retained_ += bytes.size();
			}

			bool cancelled() override
			{
				return document_detail::cancelled(options_);
			}

			void progress(const std::uint32_t current, const std::uint32_t total) override
			{
				if (options_.progress) options_.progress(
					static_cast<int>((std::min)(current, static_cast<std::uint32_t>(INT_MAX))),
					static_cast<int>((std::min)(total, static_cast<std::uint32_t>(INT_MAX))));
			}

		private:
			const DecodeOptions& options_;
			libfiles::document_frame source_;
			std::vector<std::uint32_t> row_pixels_;
			double scale_ = 1.0;
			std::uint64_t retained_ = 0;
		};

		inline std::wstring format_name(const libfiles::image_format format)
		{
			for (const auto& row : libfiles::file_types())
				if (row.image == format) return {row.name.begin(), row.name.end()};
			return {};
		}

		inline void append_document_warnings(DocumentResult& result,
		                              const std::vector<std::string>& warnings)
		{
			for (const auto& warning : warnings)
				result.warnings.emplace_back(warning.begin(), warning.end());
		}

		inline bool decode_result_to_document(const DecodeResult& decoded, DocumentResult& result)
		{
			result.error = decoded.error;
			result.message = decoded.message;
			result.warnings = decoded.warnings;
			result.orientation = decoded.orientation;
			result.originalWidth = decoded.image.originalWidth;
			result.originalHeight = decoded.image.originalHeight;
			if (!decoded) return false;
			libfiles::decoded_frame frame;
			frame.width = static_cast<std::uint32_t>(decoded.image.width);
			frame.height = static_cast<std::uint32_t>(decoded.image.height);
			frame.storage = libfiles::document_storage::bgra32;
			frame.stride = static_cast<std::size_t>(frame.width) * sizeof(std::uint32_t);
			frame.pixels.assign(reinterpret_cast<const std::uint8_t*>(decoded.image.pixels.data()),
				reinterpret_cast<const std::uint8_t*>(decoded.image.pixels.data()) + frame.stride * frame.height);
			result.document.info.width = frame.width;
			result.document.info.height = frame.height;
			result.document.info.storage = frame.storage;
			result.document.info.bits_per_pixel = 32;
			result.document.info.frames = 1;
			result.document.frames.push_back(std::move(frame));
			result.formatKey = L"WindowsImageComponent";
			result.description = result.formatKey;
			return true;
		}

		inline bool decode_libfiles_document(libfiles::reader& input, const std::wstring_view extension,
		                              const DecodeOptions& options, DocumentResult& result)
		{
			if (cancelled(options))
			{
				result.error = ImageError::cancelled;
				result.message = L"Image decoding was cancelled.";
				return false;
			}
			if (options.maximumBytes == 0 || options.maximumWidth < 0 || options.maximumHeight < 0 ||
				(options.thumbnail && (options.maximumWidth == 0 || options.maximumHeight == 0)))
			{
				result.error = ImageError::invalid_input;
				result.message = L"Invalid image decode options.";
				return false;
			}
			auto scanned = libfiles::scan(input);
			auto format = scanned.format;
			if (format == libfiles::image_format::unknown)
				if (const auto by_extension = native_format_from_extension(extension)) format = *by_extension;
			if (format == libfiles::image_format::unknown || !libfiles::can_decode_document(format))
			{
				result.error = ImageError::unsupported;
				result.message = L"No native-storage decoder recognizes this image.";
				return false;
			}

			const auto cancelled_callback = [&options] { return cancelled(options); };
			const auto progress_callback = [&options](const std::uint32_t current, const std::uint32_t total)
			{
				if (!options.progress) return;
				options.progress(static_cast<int>((std::min)(current, static_cast<std::uint32_t>(INT_MAX))),
					static_cast<int>((std::min)(total, static_cast<std::uint32_t>(INT_MAX))));
			};
			std::unique_ptr<ThumbnailDocumentSink> thumbnail_sink;
			std::unique_ptr<libfiles::document_keeper> keeper;
			libfiles::document_sink* sink = nullptr;
			if (options.thumbnail && format != libfiles::image_format::jpeg)
			{
				thumbnail_sink = std::make_unique<ThumbnailDocumentSink>(options);
				sink = thumbnail_sink.get();
			}
			else
			{
				keeper = std::make_unique<libfiles::document_keeper>(
					options.maximumBytes, cancelled_callback, progress_callback, false);
				sink = keeper.get();
			}
			libfiles::document_options document_options;
			document_options.thumbnail_width = options.thumbnail ? static_cast<std::uint32_t>(options.maximumWidth) : 0;
			document_options.thumbnail_height = options.thumbnail ? static_cast<std::uint32_t>(options.maximumHeight) : 0;
			document_options.maximum_working_bytes = options.maximumBytes;
			document_options.maximum_retained_bytes = options.maximumBytes;
			document_options.count_palette_in_retained_bytes = false;
			document_options.legacy_tiff_storage =
				options.legacyTiffStorage && format == libfiles::image_format::tiff;
			document_options.cancelled = cancelled_callback;
			document_options.progress = [progress_callback](const std::uint32_t current, const std::uint32_t total)
			{
				progress_callback(current, total);
			};
			const auto decoded = libfiles::decode_document(input, format, *sink, document_options);
			const bool thumbnail_limited = thumbnail_sink && thumbnail_sink->limited;
			result.document = thumbnail_sink ? std::move(thumbnail_sink->document) :
				std::move(keeper->document);
			result.document.info = decoded.info;
			result.orientation = scanned.orientation.value_or(1);
			result.originalWidth = static_cast<int>(scanned.width);
			result.originalHeight = static_cast<int>(scanned.height);
			result.description = format_name(format);
			result.formatKey = result.description;
			append_document_warnings(result, decoded.warnings);
			if (thumbnail_limited || decoded.refused || decoded.resource_limit)
			{
				result.document = {};
				result.error = ImageError::resource_limit;
				result.message = L"The image exceeds the decoded-memory limit.";
				return false;
			}
			if (decoded.cancelled || cancelled(options))
			{
				result.document = {};
				result.error = ImageError::cancelled;
				result.message = L"Image decoding was cancelled.";
				return false;
			}
			if (!decoded.success)
			{
				const bool keep_complete = format == libfiles::image_format::gif ||
					format == libfiles::image_format::tiff;
				if (!decoded.truncated || !keep_complete) result.document = {};
				result.error = ImageError::read_failed;
				result.message = std::wstring(decoded.error.begin(), decoded.error.end());
				if (result.message.empty()) result.message = L"The image could not be decoded.";
				return false;
			}
			if (result.document.frames.empty())
			{
				result.error = ImageError::read_failed;
				result.message = L"The image did not contain a complete frame.";
				return false;
			}
			result.error = ImageError::none;
			return true;
		}
	}

	inline PixelImage pixels_from_rows(const libfiles::pixel_rows& rows)
	{
		using namespace document_detail;
		if (rows.width == 0 || rows.height == 0 ||
			static_cast<std::uint64_t>(rows.width) * rows.height > (std::uint64_t{1} << 30) / 4)
			throw std::invalid_argument("Invalid image frame dimensions.");
		PixelImage result;
		result.width = result.originalWidth = static_cast<int>(rows.width);
		result.height = result.originalHeight = static_cast<int>(rows.height);
		result.pixels.resize(static_cast<std::size_t>(rows.width) * rows.height);
		libfiles::copy_bgra(rows, result.pixels, rows.width, false);
		return result;
	}

	inline PixelImage frame_pixels(const DocumentResult& result, const std::size_t index)
	{
		using namespace document_detail;
		if (index >= result.document.frames.size()) throw std::out_of_range("Image frame index.");
		auto pixels = pixels_from_rows(result.document.frames[index].rows());
		if (result.originalWidth > 0) pixels.originalWidth = result.originalWidth;
		if (result.originalHeight > 0) pixels.originalHeight = result.originalHeight;
		return pixels;
	}

	inline bool native_supports_extension(const std::wstring_view extension)
	{
		using namespace document_detail;
		return native_format_from_extension(extension).has_value();
	}

	inline DocumentResult decode_document(const std::span<const std::uint8_t> bytes,
	                               const std::wstring_view extension, const DecodeOptions& options)
	{
		using namespace document_detail;
		DocumentResult result;
		if (bytes.empty() || bytes.size() > INT_MAX)
		{
			result.error = ImageError::invalid_input;
			result.message = L"The encoded image is empty or exceeds the input limit.";
			return result;
		}
		libfiles::reader input(bytes);
		if (decode_libfiles_document(input, extension, options, result)) return result;
		if (result.error != ImageError::unsupported) return result;
		if (extension == L".ppt" || extension == L".pps" || extension == L".vsd")
		{
			result = {};
			const auto loaded = ::platform::load_compound_preview(bytes, codec_detail::bitmap_options(options));
			const auto decoded = codec_detail::map_bitmap_result(loaded, options);
			decode_result_to_document(decoded, result);
			return result;
		}
		const bool signature_webp = extension.empty() &&
			bytes.size() >= 12 && has_webp_signature(bytes.first(12));
		if (!supports_extension(extension) && !signature_webp)
		{
			result.error = ImageError::unsupported;
			result.message = L"No image decoder recognizes this input.";
			return result;
		}
		result = {};
		DecodeResult fallback = decode(bytes, extension, options);
		decode_result_to_document(fallback, result);
		return result;
	}

	inline DocumentResult decode_document(const std::filesystem::path& path, const DecodeOptions& options)
	{
		using namespace document_detail;
		DocumentResult result;
		PlatformFileStream source(path);
		if (!source.is_open())
		{
			result.error = ImageError::read_failed;
			result.message = L"The image file could not be opened.";
			return result;
		}
		libfiles::reader input(source);
		if (decode_libfiles_document(input, lowercase_extension(path), options, result)) return result;
		if (result.error != ImageError::unsupported) return result;
		if (!supports_extension(lowercase_extension(path)))
		{
			result.error = ImageError::unsupported;
			result.message = L"No image decoder recognizes this input.";
			return result;
		}
		result = {};
		DecodeResult fallback = decode(path, options);
		decode_result_to_document(fallback, result);
		return result;
	}
}

#pragma pop_macro("max")
#pragma pop_macro("min")
