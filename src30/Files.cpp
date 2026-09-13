// ImageWalker by Zac Walker
// Implements non-UI file enumeration, metadata, and image decoding services.

#include "Platform.h"
#include "PlatformWin32.h"
#include "Files.h"
#include "Media.h"
#include "Paths.h"
#include "WebPCodec.h"

#include <objbase.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <format>
#include <fstream>
#include <intrin.h>
#include <numeric>
#include <set>

namespace iw::files
{
	namespace
	{
		template <class T>
		void release(T* value) { if (value) value->Release(); }

		class TemporaryOutput
		{
		public:
			TemporaryOutput() = default;
			TemporaryOutput(const TemporaryOutput&) = delete;
			TemporaryOutput& operator=(const TemporaryOutput&) = delete;

			~TemporaryOutput()
			{
				if (path_.empty()) return;
				std::error_code error;
				std::filesystem::remove(path_, error);
				if (error == std::errc::permission_denied &&
					SetFileAttributesW(path_.c_str(), FILE_ATTRIBUTE_NORMAL))
				{
					error.clear();
					std::filesystem::remove(path_, error);
				}
				if (error) platform::write_diagnostic(L"Unable to remove an incomplete temporary output.\n");
			}

			std::error_code create(const std::filesystem::path& destination)
			{
				for (int attempt = 0; attempt < 16; ++attempt)
				{
					GUID id{};
					if (FAILED(CoCreateGuid(&id))) return std::make_error_code(std::errc::io_error);
					wchar_t text[40]{};
					if (!StringFromGUID2(id, text, static_cast<int>(std::size(text))))
						return std::make_error_code(std::errc::io_error);
					auto candidate = native_path(destination.parent_path() /
						(std::wstring(L".iw30-") + text + L".tmp"));
					const HANDLE file = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr,
						CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
					if (file != INVALID_HANDLE_VALUE)
					{
						CloseHandle(file);
						path_ = std::move(candidate);
						return {};
					}
					const auto error = GetLastError();
					if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
						return {static_cast<int>(error), std::system_category()};
				}
				return std::make_error_code(std::errc::file_exists);
			}

			const std::filesystem::path& path() const { return path_; }

			std::error_code commit(const std::filesystem::path& destination, const bool overwrite)
			{
				// Publish only a complete output; an unreviewed destination wins a racing create.
				if (!MoveFileExW(path_.c_str(), native_path(destination).c_str(),
					MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)))
					return {static_cast<int>(GetLastError()), std::system_category()};
				path_.clear();
				return {};
			}

		private:
			std::filesystem::path path_;
		};

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

		bool is_supported_extension(const std::wstring& extension)
		{
			return extension == L".webp" || (!extension.empty() && wic_image_extensions().contains(extension));
		}

		ItemKind classify_extension(const std::wstring& extension)
		{
			if (is_supported_extension(extension)) return ItemKind::image;
			constexpr std::array documents{
				L".doc", L".docx", L".pdf", L".ppt", L".pptx", L".rtf", L".txt",
				L".xls", L".xlsx", L".csv"
			};
			constexpr std::array videos{L".avi", L".m4v", L".mkv", L".mov", L".mp4", L".mpeg", L".mpg",
				L".wmv", L".webm", L".mts", L".m2ts", L".asf", L".3gp", L".3g2", L".ts"};
			constexpr std::array audio{L".aac", L".flac", L".m4a", L".mp3", L".ogg", L".wav", L".wma",
				L".opus", L".aif", L".aiff"};
			if (std::ranges::find(documents, extension) != documents.end()) return ItemKind::document;
			if (std::ranges::find(videos, extension) != videos.end()) return ItemKind::video;
			if (std::ranges::find(audio, extension) != audio.end()) return ItemKind::audio;
			return ItemKind::other;
		}

		std::atomic<bool> showHidden{false};

		// Reparse-point directories are neither ordinary folders nor safe to walk through.
		bool skip_entry(const platform::FileAttributes& attributes, const bool includeHidden)
		{
			if (!attributes.known) return false;
			if (!includeHidden && attributes.hidden) return true;
			return attributes.directory && attributes.reparse;
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

	void set_show_hidden(const bool value) { showHidden.store(value, std::memory_order_relaxed); }

	bool show_hidden() { return showHidden.load(std::memory_order_relaxed); }

	bool is_supported_image(const std::filesystem::path& path)
	{
		return is_supported_extension(paths::lowercase_extension(path));
	}

	bool is_editable_image(const std::filesystem::path& path)
	{
		if (!is_supported_image(path)) return false;
		std::error_code error;
		const auto status = std::filesystem::status(native_path(path), error);
		return !error && std::filesystem::is_regular_file(status) &&
			(status.permissions() & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
	}

	std::optional<FileSnapshot> snapshot_file(const std::filesystem::path& path)
	{
		if (path.empty()) return std::nullopt;
		const HANDLE file = CreateFileW(native_path(path).c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			const DWORD error = GetLastError();
			if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return FileSnapshot{};
			return std::nullopt;
		}
		BY_HANDLE_FILE_INFORMATION information{};
		const bool read = GetFileInformationByHandle(file, &information) != FALSE;
		CloseHandle(file);
		if (!read) return std::nullopt;
		const auto combine = [](const DWORD high, const DWORD low)
		{
			return (static_cast<std::uint64_t>(high) << 32) | low;
		};
		return FileSnapshot{
			true, (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
			(information.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0, information.dwVolumeSerialNumber,
			combine(information.nFileIndexHigh, information.nFileIndexLow),
			combine(information.nFileSizeHigh, information.nFileSizeLow),
			combine(information.ftLastWriteTime.dwHighDateTime, information.ftLastWriteTime.dwLowDateTime)
		};
	}

	bool matches_snapshot(const std::filesystem::path& path, const FileSnapshot& snapshot)
	{
		const auto current = snapshot_file(path);
		return current && *current == snapshot;
	}

	ItemKind classify(const std::filesystem::path& path, const bool directory)
	{
		if (directory) return ItemKind::folder;
		return classify_extension(paths::lowercase_extension(path));
	}

	std::vector<FolderItem> scan_folder_items(const std::filesystem::path& folder,
	                                          const std::stop_token& stop,
	                                          const std::function<void(size_t, size_t)>& progress,
	                                          std::error_code* error)
	{
		if (error) error->clear();
		std::vector<FolderItem> result;
		try
		{
			std::error_code iterationError;
			std::filesystem::directory_iterator iterator(
				native_path(folder), std::filesystem::directory_options::skip_permission_denied, iterationError);
			if (iterationError)
			{
				if (error) *error = iterationError;
				return result;
			}
			const std::filesystem::directory_iterator end;
			const bool includeHidden = show_hidden();
			size_t entries = 0;
			size_t images = 0;
			while (iterator != end)
			{
				if (stop.stop_requested()) return {};
				const auto& entry = *iterator;
				++entries;
				const auto attributes = platform::read_file_attributes(entry.path());
				if (!skip_entry(attributes, includeHidden))
				{
					std::error_code entryError;
					const bool directory = attributes.known
						                       ? attributes.directory
						                       : entry.is_directory(entryError);
					entryError.clear();
					const bool regular = directory || entry.is_regular_file(entryError);
					entryError.clear();
					if (regular)
					{
						FolderItem item;
						item.path = folder / entry.path().filename();
						item.kind = classify(item.path, directory);
						if (item.kind == ItemKind::image) ++images;
						if (!directory)
						{
							const auto size = entry.file_size(entryError);
							item.hasSize = !entryError;
							item.size = entryError ? 0 : size;
							entryError.clear();
						}
						const auto modified = entry.last_write_time(entryError);
						item.hasModified = !entryError;
						if (!entryError) item.modified = modified;
						entryError.clear();
						if (media::is_media(item.kind))
						{
							if (progress) progress(entries, images);
							if (stop.stop_requested()) return {};
							try
							{
								const auto info = media::probe(item.path, stop);
								item.width = info.width;
								item.height = info.height;
								item.duration = info.duration;
								item.hasDuration = info.hasDuration;
							}
							catch (...)
							{
								platform::write_diagnostic(L"Unable to read a media item's information.\n");
							}
						}
						result.push_back(std::move(item));
					}
				}
				if (progress && entries % 32 == 0) progress(entries, images);
				iterator.increment(iterationError);
				if (iterationError)
				{
					if (error) *error = iterationError;
					break;
				}
			}
			if (progress) progress(entries, images);
			sort_items(result, SortField::name);
		}
		catch (const std::exception&)
		{
			if (error) *error = std::make_error_code(std::errc::not_enough_memory);
			return {};
		}
		return result;
	}

	void sort_items(std::vector<FolderItem>& items, const SortField field, const bool ascending)
	{
		if (items.size() < 2) return;
		try
		{
			struct SortKey
			{
				bool folder{};
				std::wstring extension;
				std::wstring name;
			};
			std::vector<SortKey> keys(items.size());
			for (size_t index = 0; index < items.size(); ++index)
			{
				auto& key = keys[index];
				key.folder = items[index].kind == ItemKind::folder;
				if (!key.folder) key.extension = paths::lowercase_extension(items[index].path);
				key.name = items[index].path.filename().wstring();
			}
			std::vector<size_t> order(items.size());
			std::iota(order.begin(), order.end(), size_t{0});
			std::stable_sort(order.begin(), order.end(), [&](const size_t leftIndex, const size_t rightIndex)
			{
				const auto& left = items[leftIndex];
				const auto& right = items[rightIndex];
				// Folders lead in both directions, matching Explorer.
				if (keys[leftIndex].folder != keys[rightIndex].folder) return keys[leftIndex].folder;
				int comparison = 0;
				switch (field)
				{
				case SortField::modified:
					if (left.hasModified != right.hasModified) comparison = left.hasModified ? -1 : 1;
					else if (left.hasModified && left.modified != right.modified)
						comparison = left.modified < right.modified ? -1 : 1;
					break;
				case SortField::type:
					comparison = paths::icompare(keys[leftIndex].extension, keys[rightIndex].extension);
					break;
				case SortField::size:
					if (left.hasSize != right.hasSize) comparison = left.hasSize ? -1 : 1;
					else if (left.hasSize && left.size != right.size) comparison = left.size < right.size ? -1 : 1;
					break;
				case SortField::dimensions:
					if (left.width != right.width) comparison = left.width < right.width ? -1 : 1;
					else if (left.height != right.height) comparison = left.height < right.height ? -1 : 1;
					break;
				case SortField::duration:
					// Unknown values stay last in either direction; equal durations use natural names.
					if (left.hasDuration != right.hasDuration) return left.hasDuration;
					if (left.hasDuration && left.duration != right.duration)
						comparison = left.duration < right.duration ? -1 : 1;
					break;
				case SortField::name: break;
				}
				if (comparison == 0) comparison = paths::natural_compare(keys[leftIndex].name, keys[rightIndex].name);
				if (comparison == 0) return false;
				return ascending ? comparison < 0 : comparison > 0;
			});
			std::vector<FolderItem> sorted;
			sorted.reserve(items.size());
			for (const size_t index : order) sorted.push_back(std::move(items[index]));
			items = std::move(sorted);
		}
		catch (const std::exception&)
		{
		}
	}

	std::vector<std::filesystem::path> scan_folder(const std::filesystem::path& folder,
	                                               const std::stop_token& stop,
	                                               const std::function<void(size_t, size_t)>& progress)
	{
		std::vector<std::filesystem::path> result;
		try
		{
			std::error_code iterationError;
			std::filesystem::directory_iterator iterator(
				native_path(folder), std::filesystem::directory_options::skip_permission_denied, iterationError);
			if (iterationError) return result;
			const std::filesystem::directory_iterator end;
			const bool includeHidden = show_hidden();
			size_t entries = 0;
			while (iterator != end)
			{
				if (stop.stop_requested()) return {};
				const auto& entry = *iterator;
				++entries;
				std::error_code entryError;
				if (!skip_entry(platform::read_file_attributes(entry.path()), includeHidden) &&
					entry.is_regular_file(entryError) && is_supported_image(entry.path()))
					result.push_back(folder / entry.path().filename());
				if (progress && entries % 32 == 0) progress(entries, result.size());
				iterator.increment(iterationError);
				if (iterationError) break;
			}
			if (progress) progress(entries, result.size());
			std::ranges::stable_sort(result, [](const std::filesystem::path& left, const std::filesystem::path& right)
			{
				return paths::natural_compare(left.filename().wstring(), right.filename().wstring()) < 0;
			});
		}
		catch (const std::exception&)
		{
			return {};
		}
		return result;
	}

	Metadata read_metadata(const std::filesystem::path& path)
	{
		Metadata result;
		std::error_code error;
		result.size = std::filesystem::file_size(native_path(path), error);
		result.hasSize = !error;
		error.clear();
		result.modified = std::filesystem::last_write_time(native_path(path), error);
		result.hasModified = !error;
		if (media::is_media(classify(path)))
		{
			const auto info = media::probe(path);
			result.duration = info.duration;
			result.hasDuration = info.hasDuration;
			result.videoWidth = info.width;
			result.videoHeight = info.height;
			result.frameRate = info.frameRate;
			result.codec = info.codec;
			result.mediaError = info.error;
			return result;
		}
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

	bool read_dimensions(const std::filesystem::path& path, int& width, int& height)
	{
		if (paths::lowercase_extension(path) == L".webp") return webp::probe(path, width, height);
		if (classify(path) == ItemKind::video)
		{
			const auto info = media::probe(path);
			if (info.width <= 0 || info.height <= 0) return false;
			width = info.width;
			height = info.height;
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
			width = static_cast<int>(frameWidth);
			height = static_cast<int>(frameHeight);
			result = true;
		}
		release(frame);
		release(decoder);
		release(factory);
		return result;
	}

	DecodedImage load_image(const std::filesystem::path& path)
	{
		return load_image_for_area(path, 0, 0);
	}

	DecodedImage load_image_for_area(const std::filesystem::path& path, const int maximumWidth,
	                                 const int maximumHeight)
	{
		if (paths::lowercase_extension(path) == L".webp") return webp::load(path, maximumWidth, maximumHeight);
		constexpr std::uint64_t maximumDecodedBytes = 256ull * 1024 * 1024;
		DecodedImage result;
		IWICImagingFactory* factory = nullptr;
		IWICBitmapDecoder* decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		IWICBitmapScaler* scaler = nullptr;
		IWICFormatConverter* converter = nullptr;
		const auto nativePath = native_path(path);
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
			&&
			SUCCEEDED(factory->CreateDecoderFromFilename(nativePath.c_str(), nullptr, GENERIC_READ,
				WICDecodeMetadataCacheOnDemand, &decoder)) && SUCCEEDED(decoder->GetFrame(0, &frame)) &&
			SUCCEEDED(factory->CreateFormatConverter(&converter)))
		{
			UINT width = 0, height = 0;
			if (SUCCEEDED(frame->GetSize(&width, &height)) && width && height && width <= 100000 && height <= 100000)
			{
				result.originalWidth = static_cast<int>(width);
				result.originalHeight = static_cast<int>(height);
				int divisor = 1;
				if (maximumWidth > 0 && maximumHeight > 0)
				{
					const double fitScale = (std::min)(static_cast<double>(maximumWidth) / width,
					                                   static_cast<double>(maximumHeight) / height);
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
			}
		}
		release(converter);
		release(scaler);
		release(frame);
		release(decoder);
		release(factory);
		return result;
	}

	DecodedImage load_thumbnail(const std::filesystem::path& path, const int maximumWidth, const int maximumHeight)
	{
		if (paths::lowercase_extension(path) == L".webp") return webp::load(path, maximumWidth, maximumHeight);
		if (classify(path) == ItemKind::video) return media::poster(path, maximumWidth, maximumHeight);
		constexpr std::uint64_t maximumDecodedBytes = 256ull * 1024 * 1024;
		DecodedImage result;
		IWICImagingFactory* factory = nullptr;
		IWICBitmapDecoder* decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		IWICBitmapScaler* scaler = nullptr;
		IWICFormatConverter* converter = nullptr;
		const auto nativePath = native_path(path);
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
			&&
			SUCCEEDED(factory->CreateDecoderFromFilename(nativePath.c_str(), nullptr, GENERIC_READ,
				WICDecodeMetadataCacheOnDemand, &decoder)) && SUCCEEDED(decoder->GetFrame(0, &frame)))
		{
			UINT sourceWidth = 0, sourceHeight = 0;
			if (SUCCEEDED(frame->GetSize(&sourceWidth, &sourceHeight)) && sourceWidth && sourceHeight)
			{
				const double scale = (std::min)(1.0, (std::min)(static_cast<double>(maximumWidth) / sourceWidth,
				                                                static_cast<double>(maximumHeight) / sourceHeight));
				result.width = (std::max)(1, static_cast<int>(sourceWidth * scale));
				result.height = (std::max)(1, static_cast<int>(sourceHeight * scale));
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
		const bool useSse2 = platform::has_sse2();
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

	std::filesystem::path next_image_path(const std::filesystem::path& folder, const ImageSaveFormat format)
	{
		constexpr size_t maximumAttempts = 4096;
		std::error_code error;
		for (size_t number = 1; number <= maximumAttempts; ++number)
		{
			const auto candidate = folder / std::format(L"New {}{}", number, image_save_extension(format));
			const bool exists = std::filesystem::exists(native_path(candidate), error);
			if (error) return {};
			if (!exists) return candidate;
		}
		return {};
	}

	bool save_image(const DecodedImage& image, const std::filesystem::path& path,
	                const ImageSaveFormat format, const SaveOptions& options, const bool overwrite,
	                const std::function<bool()>& beforeCommit)
	{
		if (image.width <= 0 || image.height <= 0) return false;
		const auto strideBytes = static_cast<std::uint64_t>(image.width) * 4;
		const auto totalBytes = strideBytes * static_cast<std::uint64_t>(image.height);
		if (strideBytes > UINT_MAX || totalBytes > UINT_MAX ||
			image.pixels.size() < totalBytes / 4)
			return false;

		IWICImagingFactory* factory = nullptr;
		IWICBitmap* bitmap = nullptr;
		IWICStream* stream = nullptr;
		IWICBitmapEncoder* encoder = nullptr;
		IWICBitmapFrameEncode* frame = nullptr;
		IPropertyBag2* properties = nullptr;
		bool saved = false;
		const UINT stride = static_cast<UINT>(strideBytes);
		const UINT bytes = static_cast<UINT>(totalBytes);
		TemporaryOutput output;
		if (path.empty() || output.create(path)) return false;
		if (format == ImageSaveFormat::webp)
		{
			const auto encoded = webp::encode(image, options);
			if (encoded.empty()) return false;
			std::ofstream encodedFile(output.path(), std::ios::binary | std::ios::trunc);
			if (!encodedFile.write(reinterpret_cast<const char*>(encoded.data()),
				static_cast<std::streamsize>(encoded.size()))) return false;
			encodedFile.close();
			if (!encodedFile) return false;
			return (!beforeCommit || beforeCommit()) && !output.commit(path, overwrite);
		}
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&factory))) &&
			SUCCEEDED(factory->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppBGRA,
				stride, bytes,
				reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(
					image.pixels.data())), &bitmap)) &&
			SUCCEEDED(factory->CreateStream(&stream)) &&
			SUCCEEDED(stream->InitializeFromFilename(output.path().c_str(), GENERIC_WRITE)) &&
			SUCCEEDED(factory->CreateEncoder(container_format(format), nullptr, &encoder)) &&
			SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
			SUCCEEDED(encoder->CreateNewFrame(&frame, &properties)) &&
			apply_save_options(properties, format, options) && SUCCEEDED(frame->Initialize(properties)) &&
			SUCCEEDED(frame->SetSize(image.width, image.height)))
		{
			WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
			if (SUCCEEDED(frame->SetPixelFormat(&pixelFormat)) && SUCCEEDED(frame->WriteSource(bitmap, nullptr)) &&
				SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()))
				saved = true;
		}
		release(properties);
		release(frame);
		release(encoder);
		release(stream);
		release(bitmap);
		release(factory);
		return saved && (!beforeCommit || beforeCommit()) && !output.commit(path, overwrite);
	}

	std::vector<std::filesystem::path> sidecar_paths(const std::filesystem::path& primary)
	{
		static constexpr std::array companions{L".xmp", L".thm", L".aae"};
		std::vector<std::filesystem::path> result;
		if (primary.empty()) return result;
		const auto folder = primary.parent_path();
		const auto stem = primary.stem().wstring();
		const auto name = primary.filename().wstring();
		if (stem.empty()) return result;
		std::error_code error;
		for (const auto* extension : companions)
		{
			// Both conventions are in use: photo.xmp beside photo.jpg, and photo.jpg.xmp.
			for (const auto& candidate : {folder / (stem + extension), folder / (name + extension)})
			{
				if (paths::equal(candidate, primary)) continue;
				error.clear();
				if (!std::filesystem::is_regular_file(native_path(candidate), error)) continue;
				if (std::ranges::any_of(result, [&candidate](const std::filesystem::path& found)
				{
					return paths::equal(found, candidate);
				}))
					continue;
				result.push_back(candidate);
			}
		}
		return result;
	}

	std::filesystem::path unique_destination(const std::filesystem::path& proposed,
	                                         const std::function<bool(const std::filesystem::path&)>& alsoTaken)
	{
		constexpr int maximumAttempts = 4096;
		if (proposed.empty()) return {};
		std::error_code error;
		const auto taken = [&alsoTaken, &error](const std::filesystem::path& candidate)
		{
			error.clear();
			if (std::filesystem::exists(native_path(candidate), error) || error) return true;
			return alsoTaken && alsoTaken(candidate);
		};
		if (!taken(proposed)) return proposed;
		if (error) return {};

		const auto folder = proposed.parent_path();
		const auto stem = proposed.stem().wstring();
		const auto extension = proposed.extension().wstring();
		for (int number = 2; number <= maximumAttempts; ++number)
		{
			const auto candidate = folder / std::format(L"{} ({}){}", stem, number, extension);
			if (!taken(candidate)) return candidate;
			if (error) return {};
		}
		return {};
	}

	namespace
	{
		std::error_code prepare_destination(const std::filesystem::path& destination, const bool overwrite,
		                                    std::filesystem::path& nativeDestination)
		{
			nativeDestination = native_path(destination);
			std::error_code error;
			if (std::filesystem::exists(nativeDestination, error) && !overwrite)
				return std::make_error_code(std::errc::file_exists);
			if (error) return error;
			const auto folder = nativeDestination.parent_path();
			error.clear();
			if (!folder.empty())
			{
				const bool exists = std::filesystem::exists(folder, error);
				if (error) return error;
				if (!exists)
				{
					std::filesystem::create_directories(folder, error);
					if (error) return error;
				}
			}
			return {};
		}
	}

	std::error_code copy_file_to(const std::filesystem::path& source, const std::filesystem::path& destination,
	                             const bool overwrite, const std::function<bool()>& beforeCommit)
	{
		std::filesystem::path nativeDestination;
		if (const auto prepared = prepare_destination(destination, overwrite, nativeDestination)) return prepared;
		if (paths::equal(source, destination)) return std::make_error_code(std::errc::invalid_argument);
		TemporaryOutput output;
		if (const auto error = output.create(destination)) return error;
		std::error_code error;
		std::filesystem::copy_file(native_path(source), output.path(),
			std::filesystem::copy_options::overwrite_existing, error);
		if (error) return error;
		if (beforeCommit && !beforeCommit()) return std::make_error_code(std::errc::operation_canceled);
		return output.commit(destination, overwrite);
	}

	std::error_code move_file_to(const std::filesystem::path& source, const std::filesystem::path& destination,
	                             const bool overwrite, const std::function<bool()>& beforeCommit)
	{
		std::filesystem::path nativeDestination;
		if (const auto prepared = prepare_destination(destination, overwrite, nativeDestination)) return prepared;
		const auto nativeSource = native_path(source);
		if (paths::equal(source, destination)) return std::make_error_code(std::errc::invalid_argument);
		if (beforeCommit && !beforeCommit()) return std::make_error_code(std::errc::operation_canceled);
		if (MoveFileExW(nativeSource.c_str(), nativeDestination.c_str(),
			MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)))
			return {};
		const auto moveError = GetLastError();
		if (moveError != ERROR_NOT_SAME_DEVICE)
			return {static_cast<int>(moveError), std::system_category()};
		auto error = copy_file_to(source, destination, overwrite, beforeCommit);
		if (error) return error;
		std::filesystem::remove(nativeSource, error);
		return error;
	}

	std::filesystem::path create_original_backup(const std::filesystem::path& path)
	{
		std::error_code error;
		if (!std::filesystem::is_regular_file(native_path(path), error)) return {};
		const auto proposed = path.parent_path() /
			(path.stem().wstring() + L"-original" + path.extension().wstring());
		const auto backup = unique_destination(proposed);
		if (backup.empty() || copy_file_to(path, backup, false)) return {};
		return backup;
	}

	namespace
	{
		struct NameDate
		{
			int year{};
			int month{};
			int day{};
			int hour{};
			int minute{};
			int second{};
			bool known{};
		};

		// EXIF DateTimeOriginal is "YYYY:MM:DD HH:MM:SS"; anything shorter is not trusted.
		NameDate parse_exif_date(const std::wstring& text)
		{
			NameDate result;
			if (text.size() < 19) return result;
			const auto number = [&text](const size_t offset, const size_t length)
			{
				int value = 0;
				for (size_t index = 0; index < length; ++index)
				{
					const wchar_t character = text[offset + index];
					if (character < L'0' || character > L'9') return -1;
					value = value * 10 + (character - L'0');
				}
				return value;
			};
			result.year = number(0, 4);
			result.month = number(5, 2);
			result.day = number(8, 2);
			result.hour = number(11, 2);
			result.minute = number(14, 2);
			result.second = number(17, 2);
			result.known = result.year > 0 && result.month > 0 && result.day > 0 && result.hour >= 0 &&
				result.minute >= 0 && result.second >= 0;
			return result;
		}

		NameDate local_date(const std::filesystem::file_time_type time)
		{
			NameDate result;
			const auto utc = std::chrono::clock_cast<std::chrono::system_clock>(time);
			const auto local = std::chrono::current_zone()->to_local(utc);
			const auto days = std::chrono::floor<std::chrono::days>(local);
			const std::chrono::year_month_day date{days};
			const std::chrono::hh_mm_ss clock{std::chrono::floor<std::chrono::seconds>(local - days)};
			result.year = static_cast<int>(date.year());
			result.month = static_cast<int>(static_cast<unsigned>(date.month()));
			result.day = static_cast<int>(static_cast<unsigned>(date.day()));
			result.hour = static_cast<int>(clock.hours().count());
			result.minute = static_cast<int>(clock.minutes().count());
			result.second = static_cast<int>(clock.seconds().count());
			result.known = true;
			return result;
		}

		std::wstring token_value(const std::wstring_view token, const Metadata& metadata,
		                         const std::filesystem::path& path, const NameDate& date)
		{
			const auto is = [token](const wchar_t* name) { return paths::iequals(token, name); };
			if (is(L"name")) return path.stem().wstring();
			if (is(L"ext")) return paths::lowercase_extension(path);
			if (is(L"folder")) return path.parent_path().filename().wstring();
			if (is(L"camera")) return metadata.camera;
			if (is(L"iso")) return metadata.iso;
			if (is(L"aperture")) return metadata.aperture;
			if (is(L"exposure")) return metadata.exposure;
			if (is(L"focal-length")) return metadata.focalLength;
			if (!date.known) return {};
			if (is(L"created")) return std::format(L"{:04}-{:02}-{:02}", date.year, date.month, date.day);
			if (is(L"year")) return std::format(L"{:04}", date.year);
			if (is(L"month")) return std::format(L"{:02}", date.month);
			if (is(L"day")) return std::format(L"{:02}", date.day);
			if (is(L"time")) return std::format(L"{:02}{:02}{:02}", date.hour, date.minute, date.second);
			return {};
		}
	}

	std::wstring expand_tokens(const std::wstring_view templateText, const Metadata& metadata,
	                           const std::filesystem::path& path, const int sequence)
	{
		auto date = parse_exif_date(metadata.dateTaken);
		if (!date.known && metadata.hasModified) date = local_date(metadata.modified);

		std::wstring result;
		result.reserve(templateText.size() + 8);
		for (size_t index = 0; index < templateText.size();)
		{
			const wchar_t character = templateText[index];
			if (character == L'#')
			{
				size_t width = 0;
				while (index + width < templateText.size() && templateText[index + width] == L'#') ++width;
				result += std::format(L"{:0{}}", sequence, width);
				index += width;
				continue;
			}
			if (character == L'{')
			{
				const auto close = templateText.find(L'}', index + 1);
				if (close != std::wstring_view::npos)
				{
					result += token_value(templateText.substr(index + 1, close - index - 1), metadata, path, date);
					index = close + 1;
					continue;
				}
			}
			result.push_back(character);
			++index;
		}
		return result;
	}
}
