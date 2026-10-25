#include <libiw/Document.h>
#include <cstring>
#include <string_view>
#include <utility>
#include "../NativeCodec.h"
#include "LegacyPch.h"
#include "FileFormatAny.h"
#include "ImagingLock.h"
#include "OperationStatus.h"

namespace iw::imaging
{
	static void copy_page_bgra(const IW::Page& page, std::span<std::uint32_t> destination,
	                          std::size_t stride, bool bottomUp);
	static FrameView frame_view(const IW::Page& page);

	struct Document::Impl
	{
		IW::Image image;
	};

	Document::Document() = default;
	Document::~Document() = default;
	Document::Document(Document&&) noexcept = default;
	Document& Document::operator=(Document&&) noexcept = default;

	std::size_t Document::frame_count() const noexcept { return impl_ ? impl_->image.Pages.size() : 0; }
	std::size_t Document::metadata_count() const noexcept { return impl_ ? impl_->image.Blobs.size() : 0; }
	int Document::orientation() const { return impl_ ? impl_->image.GetCameraSettings().Orientation : 1; }
	int Document::original_width() const
	{
		return impl_ ? impl_->image.GetCameraSettings().OriginalImageSize.cx : 0;
	}
	int Document::original_height() const
	{
		return impl_ ? impl_->image.GetCameraSettings().OriginalImageSize.cy : 0;
	}
	std::wstring Document::description() const
	{
		return impl_ ? std::wstring(impl_->image.GetStatistics().GetString()) : std::wstring{};
	}
	std::wstring Document::format_key() const
	{
		return impl_ ? std::wstring(impl_->image.GetLoaderName().GetString()) : std::wstring{};
	}

	FrameView Document::frame(const std::size_t index) const
	{
		if (index >= frame_count()) throw std::out_of_range("Image frame index.");
		return frame_view(impl_->image.Pages[index]);
	}

	FrameView image_frame(const IW::Image& image, const std::size_t index)
	{
		if (index >= image.Pages.size()) throw std::out_of_range("Image frame index.");
		return frame_view(image.Pages[index]);
	}

	static FrameView frame_view(const IW::Page& page)
	{
		const auto rect = page.GetPageRect();
		const auto format = page.GetPixelFormat();
		const auto paletteSize = static_cast<size_t>(format.NumberOfPaletteEntries()) * sizeof(COLORREF);
		return {rect.left, rect.top, page.GetWidth(), page.GetHeight(), page.GetStorageWidth(),
			static_cast<PixelStorage>(format._pf), true,
			{page.GetBitmap(), static_cast<size_t>(page.GetStorageWidth()) * page.GetHeight()},
			{reinterpret_cast<const std::uint8_t*>(page.GetPalette()), paletteSize},
			static_cast<std::uint32_t>(page.GetFlags()), static_cast<std::uint32_t>(page.GetTimeDelay()),
			static_cast<std::uint32_t>(page.GetDisposalMethod()), static_cast<std::uint32_t>(page.GetTransparent())};
	}

	MetadataView Document::metadata(const std::size_t index) const
	{
		if (index >= metadata_count()) throw std::out_of_range("Image metadata index.");
		const auto& blob = impl_->image.Blobs[index];
		auto kind = blob.IsImageData() ? MetadataKind::encodedImage : MetadataKind::opaque;
		switch (blob.GetType())
		{
		case IW::MetaDataTypes::PROFILE_EXIF: kind = MetadataKind::exif; break;
		case IW::MetaDataTypes::PROFILE_IPTC: kind = MetadataKind::iptc; break;
		case IW::MetaDataTypes::PROFILE_XMP: kind = MetadataKind::xmp; break;
		case IW::MetaDataTypes::PROFILE_ICC: kind = MetadataKind::icc; break;
		case IW::MetaDataTypes::PROFILE_SOURCE_ICC: kind = MetadataKind::sourceProfile; break;
		default: break;
		}
		return {blob.GetType(), {blob.IsEmpty() ? nullptr : blob.GetData(), blob.GetDataSize()}, kind};
	}

	PixelImage Document::frame_pixels(const std::size_t index) const
	{
		if (index >= frame_count()) throw std::out_of_range("Image frame index.");
		return image_pixels(impl_->image, index);
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
		copy_page_bgra(page, result.pixels, result.width, false);
		return result;
	}

	void Document::copy_bgra(const std::span<std::uint32_t> destination, const std::size_t stride,
	                         const bool bottomUp, const std::size_t index) const
	{
		if (index >= frame_count()) throw std::out_of_range("Image frame index.");
		copy_page_bgra(impl_->image.Pages[index], destination, stride, bottomUp);
	}

	static void copy_page_bgra(const IW::Page& page, const std::span<std::uint32_t> destination,
	                          const std::size_t stride, const bool bottomUp)
	{
		copy_frame_bgra(frame_view(page), destination, stride, bottomUp);
	}

	void copy_frame_bgra(const FrameView& frame, const std::span<std::uint32_t> destination,
	                     const std::size_t stride, const bool bottomUp)
	{
		using PF = IW::PixelFormat;
		using Converter = void (*)(COLORREF*, const BYTE*, int, int);
		constexpr Converter converters[] = {
			&IW::PixelTraits<PF::PF1>::GetLine, &IW::PixelTraits<PF::PF2>::GetLine,
			&IW::PixelTraits<PF::PF4>::GetLine, &IW::PixelTraits<PF::PF8>::GetLine,
			&IW::PixelTraits<PF::PF8Alpha>::GetLine, &IW::PixelTraits<PF::PF8GrayScale>::GetLine,
			&IW::PixelTraits<PF::PF555>::GetLine, &IW::PixelTraits<PF::PF565>::GetLine,
			&IW::PixelTraits<PF::PF24>::GetLine, &IW::PixelTraits<PF::PF32>::GetLine,
			&IW::PixelTraits<PF::PF32Alpha>::GetLine};
		constexpr int bits[] = {1, 2, 4, 8, 8, 8, 16, 16, 24, 32, 32};
		const auto format = static_cast<size_t>(frame.format);
		const int width = frame.width;
		const int height = frame.height;
		if (width <= 0 || height <= 0 || format >= std::size(converters) || frame.stride <= 0 ||
			frame.stride % 4 != 0 ||
			static_cast<std::uint64_t>(frame.stride) < ((static_cast<std::uint64_t>(width) * bits[format] + 31) / 32) * 4 ||
			static_cast<std::uint64_t>(frame.stride) * height > frame.pixels.size())
			throw std::invalid_argument("Invalid source pixel storage.");
		if (stride < static_cast<size_t>(width) ||
			stride > (std::numeric_limits<size_t>::max)() / static_cast<size_t>(height) ||
			destination.size() < stride * static_cast<size_t>(height))
			throw std::invalid_argument("Invalid destination pixel storage.");
		const auto inputAddress = reinterpret_cast<std::uintptr_t>(frame.pixels.data());
		const auto outputAddress = reinterpret_cast<std::uintptr_t>(destination.data());
		if (inputAddress < outputAddress + destination.size_bytes() &&
			outputAddress < inputAddress + frame.pixels.size())
			throw std::invalid_argument("Source and destination pixel storage overlap.");
		const bool indexed = format <= static_cast<size_t>(PixelStorage::indexed8Alpha);
		const size_t paletteSize = frame.paletteBgra.size() / 4;
		IW::CBuffer<COLORREF> row(width);
		for (int y = 0; y < height; ++y)
		{
			const auto* input = frame.pixels.data() +
				static_cast<size_t>(frame.bottomUp ? height - 1 - y : y) * frame.stride;
			converters[format](row, input, 0, width);
			auto* output = destination.data() + static_cast<size_t>(bottomUp ? height - 1 - y : y) * stride;
			for (int x = 0; x < width; ++x)
			{
				COLORREF value = row[x];
				if (indexed)
				{
					const auto paletteIndex = value & 0xff;
					if (paletteIndex >= paletteSize)
						throw std::invalid_argument("Image palette index is out of range.");
					std::memcpy(&value, frame.paletteBgra.data() + paletteIndex * 4, 4);
					if ((frame.flags & IW::PageFlags::HasTransparent) && paletteIndex == frame.transparent)
						value &= 0x00ffffff;
				}
				else if (frame.format != PixelStorage::bgra32) value |= 0xff000000;
				output[x] = value;
			}
		}
	}

	PixelImage pixels_from_frame(const FrameView& frame)
	{
		if (frame.width <= 0 || frame.height <= 0 ||
			static_cast<std::uint64_t>(frame.width) * frame.height > IW::kMaxPageBytes / 4)
			throw std::invalid_argument("Invalid image frame dimensions.");
		PixelImage result;
		result.width = result.originalWidth = frame.width;
		result.height = result.originalHeight = frame.height;
		result.pixels.resize(static_cast<size_t>(frame.width) * frame.height);
		copy_frame_bgra(frame, result.pixels, frame.width, false);
		return result;
	}

	Document Document::clone() const
	{
		Document result;
		if (impl_)
		{
			result.impl_ = std::make_unique<Impl>();
			result.impl_->image = impl_->image.Clone();
		}
		return result;
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
		auto image = std::make_unique<Document::Impl>();
		IW::StreamConstBlob input(bytes.data(), IW::FileSize(bytes.size()));
		if (read_native(input, extension, image->image, options, result)) result.document.impl_ = std::move(image);
		return result;
	}

	DocumentResult decode_document(const std::filesystem::path& path, const DecodeOptions& options)
	{
		DocumentResult result;
		std::error_code error;
		const auto size = std::filesystem::file_size(native_path(path), error);
		IW::CFile input;
		if (error || !input.OpenForRead(native_path(path).c_str()))
		{
			result.error = ImageError::read_failed;
			result.message = L"The image file could not be opened.";
			return result;
		}
		if (size > INT_MAX)
		{
			// A large file no native decoder claims is not an image over the input limit.
			CString type(lowercase_extension(path).c_str());
			const bool claimed = claim(type, input);
			result.error = claimed ? ImageError::resource_limit : ImageError::unsupported;
			result.message = claimed ? L"The image file exceeds the input limit."
				: L"No native-storage decoder recognizes this image.";
			return result;
		}
		auto image = std::make_unique<Document::Impl>();
		if (read_native(input, lowercase_extension(path), image->image, options, result))
			result.document.impl_ = std::move(image);
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

	DecodeResult decode_native(const std::filesystem::path& path, const DecodeOptions& options)
	{
		auto document = decode_document(path, options);
		DecodeResult result;
		static_cast<OperationResult&>(result) = std::move(static_cast<OperationResult&>(document));
		if (result)
		{
			result.image = document.document.frame_pixels();
			result.reduced = result.image.width != result.image.originalWidth || result.image.height != result.image.originalHeight;
		}
		return result;
	}
}
