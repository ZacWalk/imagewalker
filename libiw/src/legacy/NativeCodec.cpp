#include <libiw/Document.h>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>
#include "../NativeCodec.h"
#include "LegacyPch.h"
#include "FileFormatAny.h"
#include "ImagingLock.h"
#include "OperationStatus.h"

namespace iw::imaging
{
	namespace
	{
		// A page's rows as libfiles describes stored pixels: bottom-up, as a page holds them.
		libfiles::pixel_rows page_rows(const IW::Page& page)
		{
			const auto format = page.GetPixelFormat();
			libfiles::pixel_rows rows;
			rows.width = static_cast<std::uint32_t>(page.GetWidth());
			rows.height = static_cast<std::uint32_t>(page.GetHeight());
			rows.storage = static_cast<libfiles::document_storage>(format._pf);
			rows.stride = static_cast<std::size_t>(page.GetStorageWidth());
			rows.bottom_up = true;
			rows.pixels = {page.GetBitmap(), rows.stride * rows.height};
			rows.palette = {reinterpret_cast<const std::uint32_t*>(page.GetPalette()),
				static_cast<std::size_t>(format.NumberOfPaletteEntries())};
			if (page.GetFlags() & IW::PageFlags::HasTransparent)
				rows.transparent = static_cast<std::uint32_t>(page.GetTransparent());
			return rows;
		}

		// The page as libfiles holds a frame: its rows top-down, its palette and its timing, copied.
		libfiles::decoded_frame decoded_page(const IW::Page& page)
		{
			const auto rows = page_rows(page);
			const auto rect = page.GetPageRect();
			libfiles::decoded_frame frame;
			frame.left = rect.left;
			frame.top = rect.top;
			frame.width = rows.width;
			frame.height = rows.height;
			frame.storage = rows.storage;
			frame.stride = rows.stride;
			frame.pixels.resize(rows.pixels.size());
			for (std::size_t y = 0; y < rows.height; ++y)
				std::memcpy(frame.pixels.data() + y * rows.stride,
					rows.pixels.data() + (rows.height - 1 - y) * rows.stride, rows.stride);
			frame.palette.assign(rows.palette.begin(), rows.palette.end());
			frame.transparent = rows.transparent;
			const auto flags = page.GetFlags();
			if (flags & IW::PageFlags::HasBackGround) frame.background = static_cast<std::uint32_t>(page.GetBackGround());
			if (flags & IW::PageFlags::HasTimeDelay) frame.delay_ms = static_cast<std::uint32_t>(page.GetTimeDelay());
			if (const auto disposal = page.GetDisposalMethod(); disposal != 0)
				frame.disposal = static_cast<std::uint32_t>(disposal);
			return frame;
		}

		// A block libfiles names; a thumbnail's encoded image and the loaders' private blocks have no name there.
		std::optional<libfiles::document_metadata> metadata_kind(const IW::MetaData& blob)
		{
			if (blob.IsImageData()) return {};
			switch (blob.GetType())
			{
			case IW::MetaDataTypes::PROFILE_EXIF: return libfiles::document_metadata::exif;
			case IW::MetaDataTypes::PROFILE_IPTC: return libfiles::document_metadata::iptc;
			case IW::MetaDataTypes::PROFILE_XMP: return libfiles::document_metadata::xmp;
			case IW::MetaDataTypes::PROFILE_ICC: return libfiles::document_metadata::icc;
			case IW::MetaDataTypes::PROFILE_SOURCE_ICC: return libfiles::document_metadata::source_icc;
			case IW::MetaDataTypes::PROFILE_COMMENT: return libfiles::document_metadata::comment;
			default: return {};
			}
		}

		// What ImageWalker's loaders read, as libfiles holds a document, and what they add to it.
		void to_document(const IW::Image& image, DocumentResult& result)
		{
			auto& document = result.document;
			for (const auto& page : image.Pages) document.frames.push_back(decoded_page(page));
			for (const auto& blob : image.Blobs)
			{
				const auto kind = metadata_kind(blob);
				if (!kind) continue;
				const auto* bytes = blob.IsEmpty() ? nullptr : blob.GetData();
				document.metadata.push_back({*kind, bytes ? std::vector<std::uint8_t>(bytes, bytes + blob.GetDataSize())
					: std::vector<std::uint8_t>{}});
			}
			if (!document.frames.empty())
			{
				const auto& first = document.frames.front();
				document.info.width = first.width;
				document.info.height = first.height;
				document.info.storage = first.storage;
				document.info.bits_per_pixel = libfiles::storage_bits(first.storage);
			}
			document.info.frames = static_cast<std::uint32_t>(document.frames.size());
			const auto& camera = image.GetCameraSettings();
			result.orientation = camera.Orientation;
			result.originalWidth = camera.OriginalImageSize.cx;
			result.originalHeight = camera.OriginalImageSize.cy;
			result.description = image.GetStatistics().GetString();
			result.formatKey = image.GetLoaderName().GetString();
		}

		// Kept as libfiles' document: an image too large to copy is refused as one too large to decode.
		void keep_document(const IW::Image& image, DocumentResult& result)
		{
			try
			{
				to_document(image, result);
			}
			catch (const std::bad_alloc&)
			{
				result.document = {};
				result.error = ImageError::resource_limit;
				result.message = L"Not enough memory to decode the image.";
			}
		}
	}

	libfiles::pixel_rows image_rows(const IW::Image& image, const std::size_t index)
	{
		if (index >= image.Pages.size()) throw std::out_of_range("Image frame index.");
		return page_rows(image.Pages[index]);
	}

	PixelImage image_pixels(const IW::Image& image, const std::size_t index)
	{
		if (index >= image.Pages.size()) throw std::out_of_range("Image frame index.");
		const auto& page = image.Pages[index];
		PixelImage result;
		result.width = page.GetWidth();
		result.height = page.GetHeight();
		const auto original = image.GetCameraSettings().OriginalImageSize;
		result.originalWidth = original.cx > 0 ? original.cx : result.width;
		result.originalHeight = original.cy > 0 ? original.cy : result.height;
		result.pixels.resize(static_cast<size_t>(result.width) * result.height);
		libfiles::copy_bgra(page_rows(page), result.pixels, static_cast<size_t>(result.width), false);
		return result;
	}

	PixelImage pixels_from_rows(const libfiles::pixel_rows& rows)
	{
		if (rows.width == 0 || rows.height == 0 ||
			static_cast<std::uint64_t>(rows.width) * rows.height > IW::kMaxPageBytes / 4)
			throw std::invalid_argument("Invalid image frame dimensions.");
		PixelImage result;
		result.width = result.originalWidth = static_cast<int>(rows.width);
		result.height = result.originalHeight = static_cast<int>(rows.height);
		result.pixels.resize(static_cast<size_t>(rows.width) * rows.height);
		libfiles::copy_bgra(rows, result.pixels, rows.width, false);
		return result;
	}

	PixelImage frame_pixels(const DocumentResult& result, const std::size_t index)
	{
		if (index >= result.document.frames.size()) throw std::out_of_range("Image frame index.");
		auto pixels = pixels_from_rows(result.document.frames[index].rows());
		if (result.originalWidth > 0) pixels.originalWidth = result.originalWidth;
		if (result.originalHeight > 0) pixels.originalHeight = result.originalHeight;
		return pixels;
	}
	namespace
	{
		const ImageLoaders& loaders()
		{
			static const ImageLoaders registry;
			return registry;
		}

		// An unclaimed extension needs a whole signature: a RIFF audio file or an OLE
		// container is not an image just because a loader shares its first word.
		CString signature_type(IW::IStreamIn& input)
		{
			using namespace std::string_view_literals;
			static constexpr std::pair<std::string_view, const wchar_t*> signatures[]{
				{"\xFF\xD8\xFF"sv, L"jpg"}, {"BM"sv, L"bmp"}, {"GIF87a"sv, L"gif"}, {"GIF89a"sv, L"gif"},
				{"\x89PNG\r\n\x1A\n"sv, L"png"}, {"II*\0"sv, L"tif"}, {"MM\0*"sv, L"tif"},
				{"II+\0"sv, L"tif"}, {"MM\0+"sv, L"tif"}, {"\x0A\x05\x01"sv, L"pcx"}, {"8BPS"sv, L"psd"}};
			std::uint8_t header[12]{};
			DWORD read{};
			input.Seek(IW::IStreamCommon::eBegin, 0);
			if (!input.Read(header, sizeof(header), &read)) read = 0;
			input.Seek(IW::IStreamCommon::eBegin, 0);
			if (has_webp_signature({header, read})) return L"webp";
			const std::string_view bytes(reinterpret_cast<const char*>(header), read);
			for (const auto& [magic, type] : signatures)
				if (bytes.starts_with(magic)) return type;
			return {};
		}

		// Selects by extension or, failing that, by a complete signature.
		bool claim(CString& type, IW::IStreamIn& input)
		{
			if (loaders().Find(type)) return true;
			type = signature_type(input);
			return !type.IsEmpty() && loaders().Find(type);
		}

		bool cancelled(const DecodeOptions& options)
		{
			return options.stop.stop_requested() || (options.cancelled && options.cancelled());
		}

		template<class T>
		class BoundedStream final : public T
		{
		public:
			template<class... Args>
			BoundedStream(const DecodeOptions& options, OperationResult& result, Args&&... args)
				: T(std::forward<Args>(args)...), options_(options), result_(result) {}

			void CreatePage(const CRect& rect, const IW::PixelFormat& format, bool sequential) override
			{
				check_cancel();
				if (rect.Width() <= 0 || rect.Height() <= 0) throw IW::invalid_file("Invalid image dimensions.");
				const auto width = options_.thumbnail ? (std::min)(rect.Width(), options_.maximumWidth) : rect.Width();
				const auto height = options_.thumbnail ? (std::min)(rect.Height(), options_.maximumHeight) : rect.Height();
				const auto bits = options_.thumbnail ? 32 : format.ToStorageBpp();
				const auto stride = ((static_cast<std::uint64_t>(width) * bits + 31) / 32) * 4;
				const auto bytes = stride * height;
				if (bytes > options_.maximumBytes || used_ > options_.maximumBytes - bytes)
				{
					result_.error = ImageError::resource_limit;
					throw IW::invalid_file("The image exceeds the decoded-memory limit.");
				}
				used_ += bytes;
				T::CreatePage(rect, format, sequential);
			}

			void SetBitmap(int y, LPCVOID pixels) override
			{
				check_cancel();
				T::SetBitmap(y, pixels);
			}

		private:
			void check_cancel()
			{
				if (!cancelled(options_)) return;
				result_.error = ImageError::cancelled;
				throw IW::invalid_file("Image decoding was cancelled.");
			}
			const DecodeOptions& options_;
			OperationResult& result_;
			std::uint64_t used_{};
		};

		bool read_native(IW::IStreamIn& input, std::wstring_view extension, IW::Image& image,
		                 const DecodeOptions& options, OperationResult& result)
		{
			if (cancelled(options))
			{
				result.error = ImageError::cancelled;
				result.message = L"Image decoding was cancelled.";
				return false;
			}
			if (extension.size() > INT_MAX || options.maximumWidth < 0 || options.maximumHeight < 0 ||
				(options.thumbnail && (options.maximumWidth == 0 || options.maximumHeight == 0)))
			{
				result.error = ImageError::invalid_input;
				result.message = L"Invalid image decode options.";
				return false;
			}
			thread_local CLoadAnyCore loader(loaders(), false);
			detail::OperationStatus status(options, result);
			CString type(extension.data(), static_cast<int>(extension.size()));
			if (!claim(type, input))
			{
				result.error = ImageError::unsupported;
				result.message = L"No native-storage decoder recognizes this image.";
				return false;
			}
			bool success{};
			const auto read = [&](IW::IImageStream& output)
			{
				success = loader.Read(type, &input, &output, &status);
				// The extension's decoder failed before the signature's decoder succeeded.
				if (success && result.error == ImageError::read_failed)
				{
					result.error = ImageError::none;
					result.message.clear();
				}
			};
			try
			{
				if (options.thumbnail)
				{
					BoundedStream<IW::ImageStreamThumbnailCore<IW::IImageStream, true>> output(
						options, result, image, CSize(options.maximumWidth, options.maximumHeight), options.applyOrientation);
					read(output);
					output.Flush();
				}
				else
				{
					BoundedStream<IW::ImageStreamCore<IW::IImageStream>> output(options, result, image, options.applyOrientation);
					read(output);
					output.Flush();
				}
			}
			catch (const std::bad_alloc&)
			{
				result.error = ImageError::resource_limit;
				result.message = L"Not enough memory to decode the image.";
			}
			catch (const std::exception& error)
			{
				if (result.error == ImageError::none) result.error = ImageError::read_failed;
				result.message = CString(error.what()).GetString();
			}
			if (cancelled(options)) result.error = ImageError::cancelled;
			if ((!success || image.IsEmpty()) && result.error == ImageError::none)
				result.error = ImageError::read_failed;
			if (!result && result.message.empty()) result.message = L"The image could not be decoded.";
			return success && static_cast<bool>(result);
		}
	}

	DocumentResult decode_document(const std::span<const std::uint8_t> bytes,
	                               const std::wstring_view extension, const DecodeOptions& options)
	{
		DocumentResult result;
		if (bytes.empty() || bytes.size() > INT_MAX)
		{
			result.error = ImageError::invalid_input;
			result.message = L"The encoded image is empty or exceeds the input limit.";
			return result;
		}
		IW::Image image;
		IW::StreamConstBlob input(bytes.data(), IW::FileSize(bytes.size()));
		if (read_native(input, extension, image, options, result)) keep_document(image, result);
		return result;
	}

	namespace
	{
		// The file as the native loaders read it into image: false, with the result saying why, when they do not.
		bool read_native_file(const std::filesystem::path& path, const DecodeOptions& options, OperationResult& result,
		                      IW::Image& image)
		{
			std::error_code error;
			const auto size = std::filesystem::file_size(native_path(path), error);
			IW::CFile input;
			if (error || !input.OpenForRead(native_path(path).c_str()))
			{
				result.error = ImageError::read_failed;
				result.message = L"The image file could not be opened.";
				return false;
			}
			if (size > INT_MAX)
			{
				// A large file no native decoder claims is not an image over the input limit.
				CString type(lowercase_extension(path).c_str());
				const bool claimed = claim(type, input);
				result.error = claimed ? ImageError::resource_limit : ImageError::unsupported;
				result.message = claimed ? L"The image file exceeds the input limit."
					: L"No native-storage decoder recognizes this image.";
				return false;
			}
			return read_native(input, lowercase_extension(path), image, options, result);
		}
	}

	DocumentResult decode_document(const std::filesystem::path& path, const DecodeOptions& options)
	{
		DocumentResult result;
		IW::Image image;
		if (read_native_file(path, options, result, image)) keep_document(image, result);
		return result;
	}
	bool native_supports_extension(const std::wstring_view extension)
	{
		if (extension.size() > INT_MAX) return false;
		const auto* format = loaders().Find(CString(extension.data(), static_cast<int>(extension.size())));
		return format != nullptr && format->GetKey() != L"WindowsImagingComponent" && format->GetKey() != L"WebP";
	}

	bool is_preview_only(const std::wstring_view extension)
	{
		if (extension.size() > INT_MAX) return false;
		const auto* format = loaders().Find(CString(extension.data(), static_cast<int>(extension.size())));
		return format && (format->GetFlags() & IW::ImageLoaderFlags::THUMBONLY) != 0;
	}

	namespace
	{
		// The first frame as ImageWalker shows it, straight from the image the loaders read: a display
		// decode has no use for a document's own copy of the rows.
		bool show_first_frame(const IW::Image& image, const DecodeOptions& options, DecodeResult& result)
		{
			const auto& page = image.Pages.front();
			if (static_cast<std::uint64_t>(page.GetWidth()) * page.GetHeight() > options.maximumBytes / 4)
			{
				result.error = ImageError::resource_limit;
				result.message = L"The image exceeds the decoded-memory limit.";
				return false;
			}
			result.image = image_pixels(image);
			result.orientation = image.GetCameraSettings().Orientation;
			return true;
		}
	}

	DecodeResult decode_native(const std::span<const std::uint8_t> bytes, const std::wstring_view extension,
	                           const DecodeOptions& options)
	{
		DecodeResult result;
		if (bytes.empty() || bytes.size() > INT_MAX)
		{
			result.error = ImageError::invalid_input;
			result.message = L"The encoded image is empty or exceeds the input limit.";
			return result;
		}
		IW::Image image;
		IW::StreamConstBlob input(bytes.data(), IW::FileSize(bytes.size()));
		if (read_native(input, extension, image, options, result)) show_first_frame(image, options, result);
		return result;
	}

	DecodeResult decode_native(const std::filesystem::path& path, const DecodeOptions& options)
	{
		DecodeResult result;
		IW::Image image;
		if (read_native_file(path, options, result, image) && show_first_frame(image, options, result))
			result.reduced = result.image.width != result.image.originalWidth || result.image.height != result.image.originalHeight;
		return result;
	}

	bool native_dimensions(const std::filesystem::path& path, int& width, int& height)
	{
		DecodeResult result;
		IW::Image image;
		if (!read_native_file(path, {}, result, image)) return false;
		width = image.Pages.front().GetWidth();
		height = image.Pages.front().GetHeight();
		return true;
	}
}
