#include "ImagingWin32.h"
#include "NativeCodec.h"
#include <libiw/Codec.h>
#include <libiw/JpegMetadata.h>
#include <libiw/WebPCodec.h>
#include <libiw/PixelOps.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cwctype>
#include <format>
#include <fstream>
#include <intrin.h>
#include <set>
#include <limits>
#include <stdexcept>

namespace iw::imaging
{
namespace
{
		template<class T>
		void release(T* value) { if (value) value->Release(); }

		HRESULT open_decoder(IWICImagingFactory* factory, const std::filesystem::path& path,
		                     std::span<const std::uint8_t> bytes, IWICBitmapDecoder** decoder)
		{
			if (bytes.empty())
				return factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
					WICDecodeMetadataCacheOnDemand, decoder);
			if (bytes.size() > UINT_MAX) return E_INVALIDARG;
			IWICStream* stream{};
			HRESULT result = factory->CreateStream(&stream);
			if (SUCCEEDED(result))
				result = stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()));
			if (SUCCEEDED(result))
				result = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, decoder);
			release(stream);
			return result;
		}

		const GUID& container_format(const ImageSaveFormat format)
		{
			switch (format)
			{
			case ImageSaveFormat::jpeg: return GUID_ContainerFormatJpeg;
			case ImageSaveFormat::bmp: return GUID_ContainerFormatBmp;
			case ImageSaveFormat::tiff: return GUID_ContainerFormatTiff;
			case ImageSaveFormat::webp: return GUID_ContainerFormatWebp;
			default: return GUID_ContainerFormatPng;
			}
		}

		bool apply_save_options(IPropertyBag2* properties, const ImageSaveFormat format, const SaveOptions& options)
		{
			if (format != ImageSaveFormat::jpeg && format != ImageSaveFormat::webp) return true;
			if (!properties) return false;
			const int clamped = options.quality < 1 ? 1 : options.quality > 100 ? 100 : options.quality;
			PROPBAG2 option{};
			option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
			VARIANT value;
			VariantInit(&value);
			value.vt = VT_R4;
			value.fltVal = static_cast<float>(clamped) / 100.0f;
			HRESULT result = properties->Write(1, &option, &value);
			VariantClear(&value);
			if (SUCCEEDED(result) && format == ImageSaveFormat::webp)
			{
				option.pstrName = const_cast<LPOLESTR>(L"Lossless");
				value.vt = VT_BOOL;
				value.boolVal = options.lossless ? VARIANT_TRUE : VARIANT_FALSE;
				result = properties->Write(1, &option, &value);
				VariantClear(&value);
			}
			return SUCCEEDED(result);
		}

		std::set<std::wstring> enumerate_wic_extensions()
		{
			std::set<std::wstring> result;
			IWICImagingFactory* factory = nullptr;
			IEnumUnknown* enumerator = nullptr;
			if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
					IID_PPV_ARGS(&factory))) &&
				SUCCEEDED(factory->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault,
					&enumerator)))
			{
				IUnknown* component = nullptr;
				while (enumerator->Next(1, &component, nullptr) == S_OK)
				{
					IWICBitmapCodecInfo* codec = nullptr;
					if (SUCCEEDED(component->QueryInterface(IID_PPV_ARGS(&codec))))
					{
						UINT length = 0;
						if (SUCCEEDED(codec->GetFileExtensions(0, nullptr, &length)) && length > 1)
						{
							std::wstring advertised(length, L'\0');
							if (SUCCEEDED(codec->GetFileExtensions(length, advertised.data(), &length)))
							{
								if (!advertised.empty() && advertised.back() == L'\0') advertised.pop_back();
								size_t start = 0;
								while (start < advertised.size())
								{
									const size_t end = advertised.find_first_of(L",;\0", start);
									auto extension = advertised.substr(start, end - start);
									std::erase_if(extension, [](const wchar_t value)
									{
										return value == L'*' || std::iswspace(value);
									});
									std::ranges::transform(extension, extension.begin(), [](const wchar_t value)
									{
										return static_cast<wchar_t>(std::towlower(value));
									});
									if (!extension.empty()) result.insert(std::move(extension));
									if (end == std::wstring::npos || advertised[end] == L'\0') break;
									start = end + 1;
								}
							}
						}
					}
					release(codec);
					release(component);
				}
			}
			release(enumerator);
			release(factory);
			return result;
		}

		// A failed initialisation must not be cached; without a COM apartment every image would
		// stay unsupported for the life of the process.
		const std::set<std::wstring>& wic_image_extensions()
		{
			static const std::set<std::wstring> empty;
			static std::atomic<const std::set<std::wstring>*> cached{nullptr};
			if (const auto* ready = cached.load(std::memory_order_acquire)) return *ready;
			try
			{
				auto discovered = enumerate_wic_extensions();
				if (discovered.empty()) return empty;
				auto* published = new std::set<std::wstring>(std::move(discovered));
				const std::set<std::wstring>* expected = nullptr;
				if (cached.compare_exchange_strong(expected, published, std::memory_order_acq_rel)) return *published;
				delete published;
				return *expected;
			}
			catch (const std::exception&)
			{
				return empty;
			}
		}

		bool wic_supports_extension(const std::wstring& extension)
		{
			return extension == L".webp" || (!extension.empty() && wic_image_extensions().contains(extension));
		}

		std::wstring metadata_text(IWICMetadataQueryReader* reader, const wchar_t* query)
		{
			if (!reader) return {};
			PROPVARIANT value{};
			PropVariantInit(&value);
			std::wstring result;
			if (SUCCEEDED(reader->GetMetadataByName(query, &value)))
			{
				if (value.vt == VT_LPWSTR && value.pwszVal) result = value.pwszVal;
				else if (value.vt == VT_BSTR && value.bstrVal) result = value.bstrVal;
				else if (value.vt == VT_LPSTR && value.pszVal)
				{
					// EXIF ASCII fields - Make, Model, DateTimeOriginal - arrive as VT_LPSTR. Cameras
					// write UTF-8 or a local code page, and pad fixed-width fields with spaces.
					const auto convert = [&value, &result](const UINT codePage, const DWORD flags)
					{
						const int length = MultiByteToWideChar(codePage, flags, value.pszVal, -1, nullptr, 0);
						if (length <= 1) return length == 1;
						result.resize(static_cast<size_t>(length));
						if (!MultiByteToWideChar(codePage, flags, value.pszVal, -1, result.data(), length))
							return false;
						result.resize(static_cast<size_t>(length) - 1);
						return true;
					};
					if (!convert(CP_UTF8, MB_ERR_INVALID_CHARS) && !convert(CP_ACP, 0)) result.clear();
					while (!result.empty() && (result.back() == L' ' || result.back() == L'\0')) result.pop_back();
				}
				else if (value.vt == VT_UI2) result = std::to_wstring(value.uiVal);
				else if (value.vt == VT_UI4) result = std::to_wstring(value.ulVal);
			}
			PropVariantClear(&value);
			return result;
		}

		std::wstring metadata_rational(IWICMetadataQueryReader* reader, const wchar_t* query,
		                               const wchar_t* suffix, const int precision)
		{
			if (!reader) return {};
			PROPVARIANT value{};
			PropVariantInit(&value);
			std::wstring result;
			if (SUCCEEDED(reader->GetMetadataByName(query, &value)) && value.vt == VT_UI8)
			{
				// WIC packs an unsigned rational into VT_UI8 with the numerator in the low 32 bits.
				const auto numerator = value.uhVal.LowPart;
				const auto denominator = value.uhVal.HighPart;
				if (denominator)
				{
					const double number = static_cast<double>(numerator) / denominator;
					if (number > 0.0 && number < 1.0 && std::wstring_view(suffix) == L" s")
						result = std::format(L"1/{:.0f} s", 1.0 / number);
					else
						result = precision
							         ? std::format(L"{:.1f}{}", number, suffix)
							         : std::format(L"{}{}", number, suffix);
				}
			}
			PropVariantClear(&value);
			return result;
		}

		// System.Photo.Orientation names the right tag for each container. JPEG, TIFF and camera
		// RAW decoders return the stored pixels and report the orientation for the reader to
		// apply; the HEIF decoder applies the container's rotation itself and reports 1, so
		// honouring this value never turns a photo twice.
		int frame_orientation(IWICBitmapFrameDecode* frame)
		{
			IWICMetadataQueryReader* reader = nullptr;
			int result = 1;
			if (frame && SUCCEEDED(frame->GetMetadataQueryReader(&reader)))
			{
				PROPVARIANT value;
				PropVariantInit(&value);
				if (SUCCEEDED(reader->GetMetadataByName(L"System.Photo.Orientation", &value)) &&
					value.vt == VT_UI2 && value.uiVal >= 1 && value.uiVal <= 8)
					result = value.uiVal;
				PropVariantClear(&value);
			}
			release(reader);
			return result;
		}

		bool turns_sideways(const int orientation) { return orientation >= 5 && orientation <= 8; }

		// A refused or failed decode keeps no pixels, but still reports upright dimensions.
		void make_upright(DecodedImage& image, const int orientation)
		{
			if (!image.pixels.empty() && orient_upright(image, orientation)) return;
			image.pixels.clear();
			if (!turns_sideways(orientation)) return;
			std::swap(image.width, image.height);
			std::swap(image.originalWidth, image.originalHeight);
		}

		// The photo a save carries metadata from, held open only while the output is encoded.
		struct MetadataSource
		{
			IWICBitmapDecoder* decoder{};
			IWICBitmapFrameDecode* frame{};
			GUID container{};

			MetadataSource() = default;
			MetadataSource(const MetadataSource&) = delete;
			MetadataSource& operator=(const MetadataSource&) = delete;
			~MetadataSource() { close(); }

			bool open(IWICImagingFactory* factory, const std::filesystem::path& path)
			{
				return !path.empty() &&
					SUCCEEDED(factory->CreateDecoderFromFilename(native_path(path).c_str(), nullptr, GENERIC_READ,
						WICDecodeMetadataCacheOnDemand, &decoder)) &&
					SUCCEEDED(decoder->GetContainerFormat(&container)) && SUCCEEDED(decoder->GetFrame(0, &frame));
			}

			// The source may be the destination; it must not be open when the output replaces it.
			void close()
			{
				release(frame);
				release(decoder);
				frame = nullptr;
				decoder = nullptr;
			}
		};

		// Saved pixels are always RGB, so only an RGB profile still describes them.
		bool rgb_profile(IWICColorContext* context)
		{
			WICColorContextType type{};
			UINT size = 0;
			if (FAILED(context->GetType(&type)) || type != WICColorContextProfile ||
				FAILED(context->GetProfileBytes(0, nullptr, &size)) || size < 20)
				return false;
			std::vector<BYTE> bytes(size);
			return SUCCEEDED(context->GetProfileBytes(size, bytes.data(), &size)) && size >= 20 &&
				std::memcmp(bytes.data() + 16, "RGB ", 4) == 0;
		}

		class ColorContexts
		{
		public:
			ColorContexts() = default;
			ColorContexts(const ColorContexts&) = delete;
			ColorContexts& operator=(const ColorContexts&) = delete;
			~ColorContexts() { for (auto* context : contexts_) release(context); }

			// Reads the frame's ICC profiles; `foreign` reports one that does not describe RGB.
			void read(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame)
			{
				UINT count = 0;
				if (FAILED(frame->GetColorContexts(0, nullptr, &count)) || !count || count > 16) return;
				std::vector<IWICColorContext*> all(count, nullptr);
				bool created = true;
				for (auto*& context : all) created = created && SUCCEEDED(factory->CreateColorContext(&context));
				if (created && SUCCEEDED(frame->GetColorContexts(count, all.data(), &count)))
					for (auto*& context : all)
					{
						if (rgb_profile(context))
						{
							contexts_.push_back(context);
							context = nullptr;
							continue;
						}
						WICColorContextType type{};
						if (SUCCEEDED(context->GetType(&type)) && type == WICColorContextProfile) foreign_ = true;
					}
				for (auto* context : all) release(context);
			}

			bool foreign() const { return foreign_; }
			bool empty() const { return contexts_.empty(); }
			UINT size() const { return static_cast<UINT>(contexts_.size()); }
			IWICColorContext* const* data() const { return contexts_.data(); }

		private:
			std::vector<IWICColorContext*> contexts_;
			bool foreign_{};
		};

		void set_metadata_text(IWICMetadataQueryWriter* writer, const wchar_t* query, const wchar_t* text)
		{
			PROPVARIANT value;
			PropVariantInit(&value);
			if (SUCCEEDED(writer->GetMetadataByName(query, &value)))
			{
				PropVariantClear(&value);
				value.vt = VT_LPWSTR;
				value.pwszVal = const_cast<wchar_t*>(text);
				writer->SetMetadataByName(query, &value);
				return;
			}
			PropVariantClear(&value);
		}

		// TIFF and PNG saves into their own format copy the source's metadata blocks; the encoder
		// still writes its own structural tags. The orientation is reset for upright pixels, and a
		// profile that does not describe RGB is removed.
		bool copy_metadata_blocks(IWICBitmapFrameDecode* source, IWICBitmapFrameEncode* destination,
		                          const ImageSaveFormat format, const bool removeProfile)
		{
			IWICMetadataBlockReader* reader = nullptr;
			IWICMetadataBlockWriter* writer = nullptr;
			IWICMetadataQueryWriter* query = nullptr;
			const bool copied = SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&reader))) &&
				SUCCEEDED(destination->QueryInterface(IID_PPV_ARGS(&writer))) &&
				SUCCEEDED(writer->InitializeFromBlockReader(reader)) &&
				SUCCEEDED(destination->GetMetadataQueryWriter(&query));
			if (copied && format == ImageSaveFormat::tiff)
			{
				PROPVARIANT value;
				PropVariantInit(&value);
				if (SUCCEEDED(query->GetMetadataByName(L"/ifd/{ushort=274}", &value)))
				{
					PropVariantClear(&value);
					value.vt = VT_UI2;
					value.uiVal = 1;
					query->SetMetadataByName(L"/ifd/{ushort=274}", &value);
				}
				PropVariantClear(&value);
				set_metadata_text(query, L"/ifd/xmp/tiff:Orientation", L"1");
				if (removeProfile) query->RemoveMetadataByName(L"/ifd/{ushort=34675}");
			}
			if (copied && format == ImageSaveFormat::png && removeProfile) query->RemoveMetadataByName(L"/iCCP");
			release(query);
			release(writer);
			release(reader);
			return copied;
		}

		// Encodes into the temporary output, carrying what `source` allows. Metadata is never a
		// reason for a save to fail: the caller retries without it.
		bool encode_wic(IWICImagingFactory* factory, const DecodedImage& image, const std::filesystem::path& file,
		                const ImageSaveFormat format, const SaveOptions& options, MetadataSource* source)
		{
			const UINT stride = static_cast<UINT>(image.width) * 4;
			const UINT bytes = stride * static_cast<UINT>(image.height);
			IWICBitmap* bitmap = nullptr;
			IWICStream* stream = nullptr;
			IWICBitmapEncoder* encoder = nullptr;
			IWICBitmapFrameEncode* frame = nullptr;
			IPropertyBag2* properties = nullptr;
			bool saved = false;
			if (SUCCEEDED(factory->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppBGRA,
					stride, bytes, reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.pixels.data())), &bitmap)) &&
				SUCCEEDED(factory->CreateStream(&stream)) &&
				SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
				SUCCEEDED(factory->CreateEncoder(container_format(format), nullptr, &encoder)) &&
				SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
				SUCCEEDED(encoder->CreateNewFrame(&frame, &properties)) &&
				apply_save_options(properties, format, options) && SUCCEEDED(frame->Initialize(properties)) &&
				SUCCEEDED(frame->SetSize(image.width, image.height)))
			{
				WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
				bool ready = SUCCEEDED(frame->SetPixelFormat(&pixelFormat));
				if (ready && source)
				{
					ColorContexts profiles;
					profiles.read(factory, source->frame);
					const bool sameFormat = IsEqualGUID(source->container, container_format(format)) != FALSE;
					if (options.carry == CarryMetadata::all && sameFormat &&
						(format == ImageSaveFormat::tiff || format == ImageSaveFormat::png))
						ready = copy_metadata_blocks(source->frame, frame, format, profiles.foreign());
					else if (!profiles.empty())
						// An encoder without colour profiles, such as BMP, still saves the pixels.
						frame->SetColorContexts(profiles.size(), const_cast<IWICColorContext**>(profiles.data()));
				}
				if (ready && SUCCEEDED(frame->WriteSource(bitmap, nullptr)) && SUCCEEDED(frame->Commit()) &&
					SUCCEEDED(encoder->Commit()))
					saved = true;
			}
			release(properties);
			release(frame);
			release(encoder);
			release(stream);
			release(bitmap);
			return saved;
		}

		std::vector<std::uint8_t> read_file(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file) return {};
			const auto end = file.tellg();
			if (end <= 0) return {};
			std::vector<std::uint8_t> bytes(static_cast<size_t>(end));
			file.seekg(0);
			if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
			return bytes;
		}

		// Replaces the encoder's own segments with the source's, byte for byte. False only when
		// the output could not be rewritten, which leaves it unusable.
		bool carry_jpeg_segments(const std::filesystem::path& output, const std::filesystem::path& source,
		                         const int width, const int height, const std::function<void(std::wstring_view)>& diagnostic)
		{
			const auto header = jpeg::read_header(source);
			const auto encoded = read_file(output);
			const auto carried = header.empty() || encoded.empty()
				                     ? std::vector<std::uint8_t>{}
				                     : jpeg::carry_metadata(header, encoded, width, height);
			if (carried.empty())
			{
				diagnostic(L"The photo's metadata could not be carried into the saved JPEG.\n");
				return true;
			}
			std::ofstream file(output, std::ios::binary | std::ios::trunc);
			file.write(reinterpret_cast<const char*>(carried.data()), static_cast<std::streamsize>(carried.size()));
			file.close();
			return static_cast<bool>(file);
		}

		std::uint32_t average_2x2(const std::uint32_t topLeft, const std::uint32_t topRight,
		                          const std::uint32_t bottomLeft, const std::uint32_t bottomRight)
		{
			const std::uint32_t low = (topLeft & 0x00ff00ff) + (topRight & 0x00ff00ff) +
				(bottomLeft & 0x00ff00ff) + (bottomRight & 0x00ff00ff);
			const std::uint32_t high = ((topLeft >> 8) & 0x00ff00ff) + ((topRight >> 8) & 0x00ff00ff) +
				((bottomLeft >> 8) & 0x00ff00ff) + ((bottomRight >> 8) & 0x00ff00ff);
			return ((low + 0x00020002) >> 2 & 0x00ff00ff) |
				(((high + 0x00020002) >> 2 & 0x00ff00ff) << 8);
		}

		void downsample_row_sse2(const std::uint32_t* top, const std::uint32_t* bottom,
		                         std::uint32_t* destination, const int outputCount)
		{
			const __m128i zero = _mm_setzero_si128();
			const __m128i rounding = _mm_set1_epi16(2);
			int x = 0;
			for (; x + 1 < outputCount; x += 2)
			{
				const __m128i topPixels = _mm_loadu_si128(reinterpret_cast<const __m128i*>(top + x * 2));
				const __m128i bottomPixels = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bottom + x * 2));
				const auto averagePair = [&](const __m128i topWords, const __m128i bottomWords)
				{
					const __m128i vertical = _mm_add_epi16(topWords, bottomWords);
					const __m128i horizontal = _mm_add_epi16(vertical, _mm_srli_si128(vertical, 8));
					return _mm_packus_epi16(_mm_srli_epi16(_mm_add_epi16(horizontal, rounding), 2), zero);
				};
				const __m128i first = averagePair(_mm_unpacklo_epi8(topPixels, zero),
				                                  _mm_unpacklo_epi8(bottomPixels, zero));
				const __m128i second = averagePair(_mm_unpackhi_epi8(topPixels, zero),
				                                   _mm_unpackhi_epi8(bottomPixels, zero));
				destination[x] = static_cast<std::uint32_t>(_mm_cvtsi128_si32(first));
				destination[x + 1] = static_cast<std::uint32_t>(_mm_cvtsi128_si32(second));
			}
			for (; x < outputCount; ++x)
				destination[x] = average_2x2(top[x * 2], top[x * 2 + 1], bottom[x * 2], bottom[x * 2 + 1]);
		}
}
	std::filesystem::path native_path(const std::filesystem::path& path)
	{
		const auto& original = path.native();
		if (original.starts_with(LR"(\\?\)") || original.starts_with(LR"(\\.\)")) return path;
		std::error_code error;
		const auto full = std::filesystem::absolute(path, error);
		if (error || full.empty()) return path;
		// The \\?\ prefix disables Win32 normalisation, so the path must already be normalised.
		auto value = full.lexically_normal().wstring();
		std::ranges::replace(value, L'/', L'\\');
		const auto rootLength = full.root_path().wstring().size();
		while (value.size() > rootLength && value.back() == L'\\') value.pop_back();
		if (value.empty()) return path;
		if (value.starts_with(LR"(\\)")) return LR"(\\?\UNC\)" + value.substr(2);
		return LR"(\\?\)" + value;
	}

	PhotoMetadata read_photo_metadata(const std::filesystem::path& path)
	{
		ComApartment apartment;
		PhotoMetadata result;
		IWICImagingFactory* factory = nullptr;
		IWICBitmapDecoder* decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		IWICMetadataQueryReader* reader = nullptr;
		const auto nativePath = native_path(path);
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&factory))) &&
			SUCCEEDED(factory->CreateDecoderFromFilename(nativePath.c_str(), nullptr, GENERIC_READ,
				WICDecodeMetadataCacheOnDemand, &decoder)) &&
			SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetMetadataQueryReader(&reader)))
		{
			const auto make = metadata_text(reader, L"/app1/ifd/{ushort=271}");
			const auto model = metadata_text(reader, L"/app1/ifd/{ushort=272}");
			result.camera = make.empty() ? model : model.empty() ? make : make + L" " + model;
			result.dateTaken = metadata_text(reader, L"/app1/ifd/exif/{ushort=36867}");
			result.exposure = metadata_rational(reader, L"/app1/ifd/exif/{ushort=33434}", L" s", 1);
			result.aperture = metadata_rational(reader, L"/app1/ifd/exif/{ushort=33437}", L"", 1);
			if (!result.aperture.empty()) result.aperture = L"f/" + result.aperture;
			result.iso = metadata_text(reader, L"/app1/ifd/exif/{ushort=34855}");
			if (!result.iso.empty()) result.iso = L"ISO " + result.iso;
			result.focalLength = metadata_rational(reader, L"/app1/ifd/exif/{ushort=37386}", L" mm", 1);
		}
		release(reader);
		release(frame);
		release(decoder);
		release(factory);
		return result;
	}

	bool has_webp_signature(const std::span<const std::uint8_t> header)
	{
		return header.size() >= 12 && std::memcmp(header.data(), "RIFF", 4) == 0 &&
			std::memcmp(header.data() + 8, "WEBP", 4) == 0;
	}

	PixelDecoder pixel_decoder(const std::wstring_view extension, const std::span<const std::uint8_t> header)
	{
		// Encoded bytes are identified by content; a path is identified by its extension.
		if (header.empty() ? extension == L".webp" : has_webp_signature(header)) return PixelDecoder::webp;
		ComApartment apartment;
		if (wic_supports_extension(std::wstring(extension)) || !native_supports_extension(extension))
			return PixelDecoder::wic;
		return PixelDecoder::native;
	}

	bool read_dimensions(const std::filesystem::path& path, int& width, int& height)
	{
		ComApartment apartment;
		const auto backend = pixel_decoder(lowercase_extension(path));
		if (backend == PixelDecoder::webp) return webp::probe(path, width, height);
		if (backend == PixelDecoder::native)
		{
			auto result = decode_document(path);
			if (!result) return false;
			const auto frame = result.document.frame(0);
			width = frame.width;
			height = frame.height;
			return true;
		}
		IWICImagingFactory* factory = nullptr;
		IWICBitmapDecoder* decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		bool result = false;
		const auto nativePath = native_path(path);
		UINT frameWidth = 0, frameHeight = 0;
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&factory))) &&
			SUCCEEDED(factory->CreateDecoderFromFilename(nativePath.c_str(), nullptr, GENERIC_READ,
				WICDecodeMetadataCacheOnDemand, &decoder)) &&
			SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&frameWidth, &frameHeight)) &&
			frameWidth && frameHeight && frameWidth <= 100000 && frameHeight <= 100000)
		{
			const bool sideways = turns_sideways(frame_orientation(frame));
			width = static_cast<int>(sideways ? frameHeight : frameWidth);
			height = static_cast<int>(sideways ? frameWidth : frameHeight);
			result = true;
		}
		release(frame);
		release(decoder);
		release(factory);
		return result;
	}

	int read_orientation(const std::filesystem::path& path)
	{
		ComApartment apartment;
		if (lowercase_extension(path) == L".webp") return 1;
		IWICImagingFactory* factory = nullptr;
		IWICBitmapDecoder* decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		int result = 1;
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&factory))) &&
			SUCCEEDED(factory->CreateDecoderFromFilename(native_path(path).c_str(), nullptr, GENERIC_READ,
				WICDecodeMetadataCacheOnDemand, &decoder)) &&
			SUCCEEDED(decoder->GetFrame(0, &frame)))
			result = frame_orientation(frame);
		release(frame);
		release(decoder);
		release(factory);
		return result;
	}

	bool orient_upright(DecodedImage& image, const int orientation)
	{
		if (orientation < 2 || orientation > 8) return true;
		const int width = image.width;
		const int height = image.height;
		if (width <= 0 || height <= 0 || image.pixels.size() < static_cast<size_t>(width) * height) return false;
		auto* const pixels = image.pixels.data();
		const auto row = [pixels, width](const int y) { return pixels + static_cast<size_t>(y) * width; };
		switch (orientation)
		{
		case 2:
			for (int y = 0; y < height; ++y) std::reverse(row(y), row(y) + width);
			return true;
		case 3:
			std::reverse(pixels, pixels + static_cast<size_t>(width) * height);
			return true;
		case 4:
			for (int y = 0; y < height / 2; ++y) std::swap_ranges(row(y), row(y) + width, row(height - 1 - y));
			return true;
		default:
			break;
		}
		std::vector<std::uint32_t> turned;
		try { turned.resize(static_cast<size_t>(width) * height); }
		catch (const std::bad_alloc&) { return false; }
		// Stored rows become upright columns. Working in tiles keeps both buffers in cache.
		const int uprightWidth = height;
		const int uprightHeight = width;
		const bool fromRight = orientation >= 7;
		const bool fromBottom = orientation == 6 || orientation == 7;
		constexpr int tile = 64;
		for (int top = 0; top < uprightHeight; top += tile)
			for (int left = 0; left < uprightWidth; left += tile)
				for (int y = top; y < (std::min)(top + tile, uprightHeight); ++y)
				{
					const auto* stored = pixels + (fromRight ? width - 1 - y : y);
					auto* destination = turned.data() + static_cast<size_t>(y) * uprightWidth;
					for (int x = left; x < (std::min)(left + tile, uprightWidth); ++x)
						destination[x] = stored[static_cast<size_t>(fromBottom ? height - 1 - x : x) * width];
				}
		image.pixels = std::move(turned);
		image.width = uprightWidth;
		image.height = uprightHeight;
		std::swap(image.originalWidth, image.originalHeight);
		return true;
	}

	DecodedImage load_wic_for_area(const std::filesystem::path& path, const int maximumWidth,
	                                 const int maximumHeight, const bool applyOrientation, const std::uint64_t maximumDecodedBytes,
	                                 std::span<const std::uint8_t> encodedBytes = {}, int* storedOrientation = nullptr)
	{
		DecodedImage result;
		IWICImagingFactory* factory = nullptr;
		IWICBitmapDecoder* decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		IWICBitmapScaler* scaler = nullptr;
		IWICFormatConverter* converter = nullptr;
		const auto nativePath = native_path(path);
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
			&&
			SUCCEEDED(open_decoder(factory, nativePath, encodedBytes, &decoder)) && SUCCEEDED(decoder->GetFrame(0, &frame)) &&
			SUCCEEDED(factory->CreateFormatConverter(&converter)))
		{
			UINT width = 0, height = 0;
			if (SUCCEEDED(frame->GetSize(&width, &height)) && width && height && width <= 100000 && height <= 100000)
			{
				// Decoded as stored, then turned upright; the area fits the upright photo.
				const int orientation = frame_orientation(frame);
				if (storedOrientation) *storedOrientation = applyOrientation ? 1 : orientation;
				const bool sideways = applyOrientation && turns_sideways(orientation);
				result.originalWidth = static_cast<int>(width);
				result.originalHeight = static_cast<int>(height);
				int divisor = 1;
				if (maximumWidth > 0 && maximumHeight > 0)
				{
					const double fitScale = (std::min)(static_cast<double>(maximumWidth) / (sideways ? height : width),
					                                   static_cast<double>(maximumHeight) / (sideways ? width : height));
					if (fitScale <= 0.25) divisor = 4;
					else if (fitScale <= 0.5) divisor = 2;
				}
				result.width = (static_cast<int>(width) + divisor - 1) / divisor;
				result.height = (static_cast<int>(height) + divisor - 1) / divisor;
				IWICBitmapSource* source = frame;
				if (divisor > 1 && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
					SUCCEEDED(scaler->Initialize(frame, result.width, result.height, WICBitmapInterpolationModeFant)))
					source = scaler;
				else if (divisor > 1)
				{
					result.width = static_cast<int>(width);
					result.height = static_cast<int>(height);
				}
				if (SUCCEEDED(converter->Initialize(source, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
					nullptr, 0.0, WICBitmapPaletteTypeCustom)))
				{
					const auto stride = static_cast<std::uint64_t>(result.width) * 4;
					const auto bytes = stride * static_cast<std::uint64_t>(result.height);
					if (bytes <= maximumDecodedBytes && bytes <= UINT_MAX)
					{
						try
						{
							result.pixels.resize(bytes / 4);
							if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(stride),
								static_cast<UINT>(bytes),
								reinterpret_cast<BYTE*>(result.pixels.data()))))
								result.pixels.clear();
						}
						catch (const std::bad_alloc&)
						{
							result.pixels.clear();
						}
					}
				}
				if (applyOrientation) make_upright(result, orientation);
			}
		}
		release(converter);
		release(scaler);
		release(frame);
		release(decoder);
		release(factory);
		return result;
	}

	DecodedImage load_wic_thumbnail(const std::filesystem::path& path, const int maximumWidth, const int maximumHeight,
	                               const bool applyOrientation, const std::uint64_t maximumDecodedBytes,
	                               std::span<const std::uint8_t> encodedBytes = {}, int* storedOrientation = nullptr)
	{
		DecodedImage result;
		IWICImagingFactory* factory = nullptr;
		IWICBitmapDecoder* decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		IWICBitmapScaler* scaler = nullptr;
		IWICFormatConverter* converter = nullptr;
		const auto nativePath = native_path(path);
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
			&&
			SUCCEEDED(open_decoder(factory, nativePath, encodedBytes, &decoder)) && SUCCEEDED(decoder->GetFrame(0, &frame)))
		{
			UINT sourceWidth = 0, sourceHeight = 0;
			if (SUCCEEDED(frame->GetSize(&sourceWidth, &sourceHeight)) && sourceWidth && sourceHeight)
			{
				// Scaled as stored to the size that is the bound once turned upright.
				const int orientation = frame_orientation(frame);
				if (storedOrientation) *storedOrientation = applyOrientation ? 1 : orientation;
				const bool sideways = applyOrientation && turns_sideways(orientation);
				const double uprightWidth = sideways ? sourceHeight : sourceWidth;
				const double uprightHeight = sideways ? sourceWidth : sourceHeight;
				const double scale = (std::min)(1.0, (std::min)(maximumWidth / uprightWidth,
				                                                maximumHeight / uprightHeight));
				const int thumbnailWidth = (std::max)(1, static_cast<int>(uprightWidth * scale));
				const int thumbnailHeight = (std::max)(1, static_cast<int>(uprightHeight * scale));
				result.width = sideways ? thumbnailHeight : thumbnailWidth;
				result.height = sideways ? thumbnailWidth : thumbnailHeight;
				result.originalWidth = static_cast<int>(sourceWidth);
				result.originalHeight = static_cast<int>(sourceHeight);
				if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
					SUCCEEDED(scaler->Initialize(frame, result.width, result.height, WICBitmapInterpolationModeFant)) &&
					SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
					SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
						nullptr, 0.0, WICBitmapPaletteTypeCustom)))
				{
					const auto stride = static_cast<std::uint64_t>(result.width) * 4;
					const auto bytes = stride * static_cast<std::uint64_t>(result.height);
					if (bytes <= maximumDecodedBytes && bytes <= UINT_MAX)
					{
						try
						{
							result.pixels.resize(bytes / 4);
							if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(stride),
								static_cast<UINT>(bytes),
								reinterpret_cast<BYTE*>(result.pixels.data()))))
								result.pixels.clear();
						}
						catch (const std::bad_alloc&)
						{
							result.pixels.clear();
						}
					}
				}
				if (applyOrientation) make_upright(result, orientation);
			}
		}
		release(converter);
		release(scaler);
		release(frame);
		release(decoder);
		release(factory);
		return result;
	}

	DecodedImage downsample_bgra_2x(const DecodedImage& source)
	{
		DecodedImage result;
		if (source.width <= 0 || source.height <= 0 ||
			source.pixels.size() < static_cast<size_t>(source.width) * source.height)
			return result;
		result.width = (source.width + 1) / 2;
		result.height = (source.height + 1) / 2;
		result.originalWidth = source.originalWidth ? source.originalWidth : source.width;
		result.originalHeight = source.originalHeight ? source.originalHeight : source.height;
		result.pixels.resize(static_cast<size_t>(result.width) * result.height);
		const bool useSse2 = has_sse2();
		for (int y = 0; y < result.height; ++y)
		{
			const int sourceY = y * 2;
			const auto* top = source.pixels.data() + static_cast<size_t>(sourceY) * source.width;
			const auto* bottom = source.pixels.data() + static_cast<size_t>((std::min)(sourceY + 1, source.height - 1))
				*
				source.width;
			auto* destination = result.pixels.data() + static_cast<size_t>(y) * result.width;
			const int pairedWidth = source.width / 2;
			if (useSse2) downsample_row_sse2(top, bottom, destination, pairedWidth);
			else
				for (int x = 0; x < pairedWidth; ++x)
					destination[x] = average_2x2(top[x * 2], top[x * 2 + 1], bottom[x * 2], bottom[x * 2 + 1]);
			if (source.width & 1)
			{
				const auto x = source.width - 1;
				destination[result.width - 1] = average_2x2(top[x], top[x], bottom[x], bottom[x]);
			}
		}
		return result;
	}

	std::vector<ImageSaveFormat> writable_image_formats()
	{
		ComApartment apartment;
		std::vector<ImageSaveFormat> result;
		IWICImagingFactory* factory = nullptr;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&factory))))
			return {ImageSaveFormat::webp};
		for (const auto format : {
			     ImageSaveFormat::png, ImageSaveFormat::jpeg,
			     ImageSaveFormat::bmp, ImageSaveFormat::tiff
		     })
		{
			IWICBitmapEncoder* encoder = nullptr;
			if (SUCCEEDED(factory->CreateEncoder(container_format(format), nullptr, &encoder)))
				result.
					push_back(format);
			release(encoder);
		}
		release(factory);
		result.push_back(ImageSaveFormat::webp);
		return result;
	}

	const wchar_t* image_save_format_name(const ImageSaveFormat format)
	{
		switch (format)
		{
		case ImageSaveFormat::jpeg: return L"JPEG image (*.jpg)";
		case ImageSaveFormat::bmp: return L"Bitmap image (*.bmp)";
		case ImageSaveFormat::tiff: return L"TIFF image (*.tif)";
		case ImageSaveFormat::webp: return L"WebP image (*.webp)";
		default: return L"PNG image (*.png)";
		}
	}

	const wchar_t* image_save_extension(const ImageSaveFormat format)
	{
		switch (format)
		{
		case ImageSaveFormat::jpeg: return L".jpg";
		case ImageSaveFormat::bmp: return L".bmp";
		case ImageSaveFormat::tiff: return L".tif";
		case ImageSaveFormat::webp: return L".webp";
		default: return L".png";
		}
	}

	bool encode_pixels(const DecodedImage& image, const std::filesystem::path& path,
	                const ImageSaveFormat format, const SaveOptions& options, const std::function<void(std::wstring_view)>& diagnostic)
	{
		if (image.width <= 0 || image.height <= 0) return false;
		const auto strideBytes = static_cast<std::uint64_t>(image.width) * 4;
		const auto totalBytes = strideBytes * static_cast<std::uint64_t>(image.height);
		if (strideBytes > UINT_MAX || totalBytes > UINT_MAX ||
			image.pixels.size() < totalBytes / 4)
			return false;

		if (format == ImageSaveFormat::webp)
		{
			const auto encoded = webp::encode(image, options);
			if (encoded.empty()) return false;
			std::ofstream encodedFile(native_path(path), std::ios::binary | std::ios::trunc);
			if (!encodedFile.write(reinterpret_cast<const char*>(encoded.data()),
				static_cast<std::streamsize>(encoded.size()))) return false;
			encodedFile.close();
			if (!encodedFile) return false;
			return true;
		}
		IWICImagingFactory* factory = nullptr;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
			return false;
		bool saved = false;
		{
			MetadataSource source;
			const bool carrying = options.carry != CarryMetadata::none && source.open(factory, options.metadataSource);
			if (options.carry != CarryMetadata::none && !carrying)
				diagnostic(L"The source photo's metadata could not be read for the save.\n");
			saved = encode_wic(factory, image, native_path(path), format, options, carrying ? &source : nullptr);
			if (!saved && carrying)
			{
				diagnostic(L"Saving with the source photo's metadata failed; saving the pixels alone.\n");
				saved = encode_wic(factory, image, native_path(path), format, options, nullptr);
			}
			const bool jpegToJpeg = carrying && format == ImageSaveFormat::jpeg &&
				IsEqualGUID(source.container, GUID_ContainerFormatJpeg);
			source.close();
			if (saved && jpegToJpeg && options.carry == CarryMetadata::all)
				saved = carry_jpeg_segments(native_path(path), options.metadataSource, image.width, image.height, diagnostic);
		}
		release(factory);
		return saved;
	}

	std::wstring lowercase_extension(const std::filesystem::path& path)
	{
		auto extension = path.extension().wstring();
		std::ranges::transform(extension, extension.begin(),
			[](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
		return extension;
	}

	bool supports_extension(const std::wstring_view extension)
	{
		ComApartment apartment;
		return native_supports_extension(extension) || wic_supports_extension(std::wstring(extension));
	}

	std::vector<std::wstring> windows_image_extensions()
	{
		ComApartment apartment;
		const auto& extensions = wic_image_extensions();
		return {extensions.begin(), extensions.end()};
	}

	DecodeResult decode(const std::filesystem::path& path, const DecodeOptions& options)
	{
		DecodeResult result;
		const auto cancelled = [&] { return options.stop.stop_requested() || (options.cancelled && options.cancelled()); };
		if (cancelled())
		{
			result.error = ImageError::cancelled;
			result.message = L"Image decoding was cancelled.";
			return result;
		}
		if (path.empty() || options.maximumWidth < 0 || options.maximumHeight < 0 ||
			(options.thumbnail && (options.maximumWidth == 0 || options.maximumHeight == 0)) ||
			options.maximumBytes == 0)
		{
			result.error = ImageError::invalid_input;
			result.message = L"Invalid image decode request.";
			return result;
		}
		try
		{
			ComApartment apartment;
			const auto backend = pixel_decoder(lowercase_extension(path));
			if (backend == PixelDecoder::native) return decode_native(path, options);
			if (backend == PixelDecoder::webp)
			{
				int width{}, height{};
				if (webp::probe(path, width, height))
				{
					const double scale = options.maximumWidth > 0 && options.maximumHeight > 0
						? (std::min)({1.0, static_cast<double>(options.maximumWidth) / width,
							static_cast<double>(options.maximumHeight) / height}) : 1.0;
					const auto pixels = static_cast<std::uint64_t>((std::max)(1, static_cast<int>(width * scale))) *
						(std::max)(1, static_cast<int>(height * scale));
					if (pixels > options.maximumBytes / 4)
					{
						result.error = ImageError::resource_limit;
						result.message = L"The image exceeds the decoded-memory limit.";
						return result;
					}
				}
				result.image = webp::load(path, options.maximumWidth, options.maximumHeight);
			}
			else
				result.image = options.thumbnail
					? load_wic_thumbnail(path, options.maximumWidth, options.maximumHeight,
						options.applyOrientation, options.maximumBytes, {}, &result.orientation)
					: load_wic_for_area(path, options.maximumWidth, options.maximumHeight,
						options.applyOrientation, options.maximumBytes, {}, &result.orientation);
			if (cancelled())
			{
				result.image = {};
				result.error = ImageError::cancelled;
				result.message = L"Image decoding was cancelled.";
			}
			else if (!valid_pixels(result.image.width, result.image.height, result.image.pixels.size()))
			{
				const auto bytes = result.image.width > 0 && result.image.height > 0
					? static_cast<std::uint64_t>(result.image.width) * result.image.height * 4 : 0;
				result.error = bytes > options.maximumBytes ? ImageError::resource_limit : ImageError::read_failed;
				result.message = result.error == ImageError::resource_limit
					? L"The image exceeds the decoded-memory limit."
					: L"The image could not be decoded; it may be damaged or require a codec that is not installed.";
			}
			else
			{
				result.reduced = result.image.width != result.image.originalWidth ||
					result.image.height != result.image.originalHeight;
				if (options.progress) options.progress(result.image.height, result.image.height);
			}
		}
		catch (const std::bad_alloc&)
		{
			result.image = {};
			result.error = ImageError::resource_limit;
			result.message = L"Not enough memory to decode the image.";
		}
		catch (const std::filesystem::filesystem_error& error)
		{
			result.image = {};
			result.error = ImageError::read_failed;
			result.message = error.code().message().empty() ? L"Image input failed." : L"The image file could not be read.";
		}
		return result;
	}

	OperationResult encode_to_file(const PixelImage& image, const std::filesystem::path& temporary,
	                              const ImageSaveFormat format, const SaveOptions& options)
	{
		OperationResult result;
		if (temporary.empty() || !valid_pixels(image.width, image.height, image.pixels.size()) ||
			format < ImageSaveFormat::png || format > ImageSaveFormat::webp)
		{
			result.error = ImageError::invalid_input;
			result.message = L"Invalid image encode request.";
			return result;
		}
		ComApartment apartment;
		try
		{
			if (!encode_pixels(image, temporary, format, options,
				[&](std::wstring_view warning) { result.warnings.emplace_back(warning); }))
			{
				result.error = ImageError::encode_failed;
				result.message = L"The image could not be encoded to the temporary output.";
			}
		}
		catch (const std::bad_alloc&)
		{
			result.error = ImageError::resource_limit;
			result.message = L"Not enough memory to encode the image.";
		}
		return result;
	}

	DecodeResult decode(const std::span<const std::uint8_t> bytes, const std::wstring_view extension,
	                    const DecodeOptions& options)
	{
		return decode_with(pixel_decoder(extension, bytes), bytes, extension, options);
	}

	DecodeResult decode_with(const PixelDecoder decoder, const std::span<const std::uint8_t> bytes,
	                         const std::wstring_view extension, const DecodeOptions& options)
	{
		DecodeResult result;
		const auto cancelled = [&] { return options.stop.stop_requested() || (options.cancelled && options.cancelled()); };
		if (cancelled())
		{
			result.error = ImageError::cancelled;
			result.message = L"Image decoding was cancelled.";
			return result;
		}
		if (bytes.empty() || bytes.size() > UINT_MAX || options.maximumBytes == 0 ||
			options.maximumWidth < 0 || options.maximumHeight < 0 ||
			(options.thumbnail && (options.maximumWidth == 0 || options.maximumHeight == 0)))
		{
			result.error = ImageError::invalid_input;
			result.message = L"Invalid image decode request.";
			return result;
		}
		try
		{
			ComApartment apartment;
			if (decoder == PixelDecoder::webp)
			{
				int width{}, height{};
				if (!webp::probe(bytes, width, height))
				{
					result.error = ImageError::unsupported;
					result.message = L"This WebP image is damaged or animated; only static WebP is supported.";
					return result;
				}
				const double scale = options.maximumWidth > 0 && options.maximumHeight > 0
					? (std::min)({1.0, static_cast<double>(options.maximumWidth) / width,
						static_cast<double>(options.maximumHeight) / height}) : 1.0;
				if (static_cast<std::uint64_t>((std::max)(1, static_cast<int>(width * scale))) *
					(std::max)(1, static_cast<int>(height * scale)) > options.maximumBytes / 4)
				{
					result.error = ImageError::resource_limit;
					result.message = L"The image exceeds the decoded-memory limit.";
					return result;
				}
				result.image = webp::decode(bytes, options.maximumWidth, options.maximumHeight);
			}
			else if (decoder == PixelDecoder::native)
			{
				auto document = decode_document(bytes, extension, options);
				static_cast<OperationResult&>(result) = std::move(static_cast<OperationResult&>(document));
				if (result) result.image = document.document.frame_pixels();
			}
			else
			{
				result.image = options.thumbnail
					? load_wic_thumbnail({}, options.maximumWidth, options.maximumHeight,
						options.applyOrientation, options.maximumBytes, bytes, &result.orientation)
					: load_wic_for_area({}, options.maximumWidth, options.maximumHeight,
						options.applyOrientation, options.maximumBytes, bytes, &result.orientation);
			}
			if (cancelled())
			{
				result.image = {};
				result.error = ImageError::cancelled;
				result.message = L"Image decoding was cancelled.";
			}
			else if (result && !valid_pixels(result.image.width, result.image.height, result.image.pixels.size()))
			{
				const auto decodedBytes = result.image.width > 0 && result.image.height > 0
					? static_cast<std::uint64_t>(result.image.width) * result.image.height * 4 : 0;
				result.error = decodedBytes > options.maximumBytes ? ImageError::resource_limit : ImageError::read_failed;
				result.message = result.error == ImageError::resource_limit
					? L"The image exceeds the decoded-memory limit." : L"The image could not be decoded.";
			}
			if (result)
			{
				result.reduced = result.image.width != result.image.originalWidth ||
					result.image.height != result.image.originalHeight;
				if (options.progress) options.progress(result.image.height, result.image.height);
			}
		}
		catch (const std::bad_alloc&)
		{
			result.image = {};
			result.error = ImageError::resource_limit;
			result.message = L"Not enough memory to decode the image.";
		}
		return result;
	}

}
