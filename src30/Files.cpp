// ImageWalker by Zac Walker
// Implements non-UI file enumeration, metadata, and image decoding services.

#include "Platform.h"
#include "Files.h"
#include "DesktopServices.h"

#include "Media.h"
#include "Paths.h"
#include <core/file_publication.h>
#include <iw/imaging/Codec.h>
#include <iw/imaging/detail/PlatformFileStream.h>
#include <libfiles/reader.h>
#include <libfiles/scan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <format>
#include <fstream>
#include <numeric>
#include <set>

namespace iw::files
{
	namespace
	{
		template <class T> void release(T* value)
		{
			if (value) value->Release();
		}

		std::error_code win_error(const DWORD error)
		{
			if (error == ERROR_SUCCESS) return {};
			if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
				return std::make_error_code(std::errc::file_exists);
			return {static_cast<int>(error), std::system_category()};
		}

		bool same_identity(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b)
		{
			return (a.nFileIndexLow || a.nFileIndexHigh) && a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
			       a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
		}

		bool pin_folder(publication::File& file, const std::filesystem::path& folder)
		{
			file.reset(CreateFileW(native_path(folder).c_str(), FILE_READ_ATTRIBUTES,
			                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
			                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			if (!file) return false;
			BY_HANDLE_FILE_INFORMATION info{};
			if (!GetFileInformationByHandle(file.get(), &info) ||
			    (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) !=
			        FILE_ATTRIBUTE_DIRECTORY)
			{
				SetLastError(ERROR_DIRECTORY);
				return false;
			}
			return true;
		}

		std::error_code publish(const HANDLE staged, const std::filesystem::path& path,
		                        const std::optional<FileSnapshot>& expected, const std::function<bool()>& validate,
		                        std::filesystem::path* originalLocation)
		{
			const auto absolute = native_path(path);
			publication::AvailableName target(absolute);
			if (!expected || !expected->exists)
			{
				if (validate && !validate()) return std::make_error_code(std::errc::operation_canceled);
				return win_error(target.move(staged));
			}
			const publication::File original(CreateFileW(absolute.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ,
			                                             nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
			                                             nullptr));
			if (!original) return win_error(GetLastError());
			if (expected->directory || expected->readOnly || expected->reparse || !matches_snapshot(path, *expected) ||
			    (validate && !validate()) || !matches_snapshot(path, *expected))
				return std::make_error_code(std::errc::operation_canceled);
			const auto proposed = path.parent_path() /
			                      (path.stem().wstring() + L"-original" + path.extension().wstring());
			const auto backup = unique_destination(proposed);
			if (backup.empty()) return std::make_error_code(std::errc::file_exists);
			publication::AvailableName recovery(native_path(backup));
			if (originalLocation) *originalLocation = backup;
			if (const auto error = win_error(recovery.move(original.get())))
			{
				if (originalLocation) originalLocation->clear();
				return error;
			}
			if (const auto error = win_error(target.move(staged)))
			{
				if (target.move(original.get()) == ERROR_SUCCESS)
				{
					if (originalLocation) originalLocation->clear();
				}
				else platform::write_diagnostic(L"The original was retained at " + backup.wstring() + L"\n");
				return error;
			}
			return {};
		}

		class TemporaryOutput
		{
		public:
			TemporaryOutput() = default;
			TemporaryOutput(const TemporaryOutput&) = delete;
			TemporaryOutput& operator=(const TemporaryOutput&) = delete;

			~TemporaryOutput()
			{
				if (!file_ || published_) return;
				if (!publication::remove(file_.get()))
				{
					FILE_BASIC_INFO basic{};
					if (GetFileInformationByHandleEx(file_.get(), FileBasicInfo, &basic, sizeof(basic)) &&
					    (basic.FileAttributes & FILE_ATTRIBUTE_READONLY))
					{
						basic = {};
						basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
						SetFileInformationByHandle(file_.get(), FileBasicInfo, &basic, sizeof(basic));
					}
					if (!publication::remove(file_.get()))
						platform::write_diagnostic(L"Incomplete temporary output retained at " + path_.wstring() +
						                           L"\n");
				}
			}

			std::error_code create(const std::filesystem::path& destination)
			{
				if (const auto error = prepare(destination)) return error;
				file_.reset(CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr, CREATE_NEW,
				                        FILE_ATTRIBUTE_NORMAL, nullptr));
				return file_ ? std::error_code{} : win_error(GetLastError());
			}

			bool seek(const std::int64_t offset) const
			{
				LARGE_INTEGER position{};
				position.QuadPart = offset;
				return offset >= 0 && SetFilePointerEx(file_.get(), position, nullptr, FILE_BEGIN);
			}

			StagedStream stream() const
			{
				return {
					[this]
				{
					LARGE_INTEGER size{};
					return GetFileSizeEx(file_.get(), &size) ? size.QuadPart : std::int64_t{-1};
				},
					[this](const std::int64_t offset, const std::span<std::uint8_t> bytes)
				{
					DWORD read{};
					return bytes.size() <= MAXDWORD && seek(offset) &&
					       ReadFile(file_.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) &&
					       read == bytes.size();
				},
					[this](const std::int64_t offset, const std::span<const std::uint8_t> bytes)
				{
					DWORD written{};
					return bytes.size() <= MAXDWORD && seek(offset) &&
					       WriteFile(file_.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
					       written == bytes.size();
				}};
			}

			bool flush() const { return FlushFileBuffers(file_.get()) != FALSE; }

			bool write(std::span<const std::uint8_t> bytes) const
			{
				while (!bytes.empty())
				{
					const auto chunk = static_cast<DWORD>((std::min)(bytes.size(), size_t{1024 * 1024}));
					DWORD written = 0;
					if (!WriteFile(file_.get(), bytes.data(), chunk, &written, nullptr) || written != chunk)
						return false;
					bytes = bytes.subspan(written);
				}
				return flush();
			}

			std::error_code copy_from(const std::filesystem::path& source, const std::filesystem::path& destination)
			{
				if (const auto error = prepare(destination)) return error;
				// Let Windows copy alternate data streams and attributes. Claim only the exact
				// file that the OS created, never a later occupant of this temporary name.
				BY_HANDLE_FILE_INFORMATION created{};
				const bool copied = CopyFileExW(native_path(source).c_str(), path_.c_str(), capture_copy_identity,
				                                &created, nullptr, COPY_FILE_FAIL_IF_EXISTS) != FALSE;
				const auto copyError = copied ? std::error_code{} : win_error(GetLastError());
				if (created.nFileIndexHigh || created.nFileIndexLow)
				{
					file_.reset(CreateFileW(path_.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | DELETE,
					                        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
					                        nullptr));
					BY_HANDLE_FILE_INFORMATION observed{};
					if (!file_ || !GetFileInformationByHandle(file_.get(), &observed) ||
					    !same_identity(created, observed) ||
					    (observed.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
						file_.reset();
				}
				if (copyError) return copyError;
				return file_ ? std::error_code{} : std::make_error_code(std::errc::state_not_recoverable);
			}

			std::error_code commit(const std::filesystem::path& destination,
			                       const std::optional<FileSnapshot>& expected, const std::function<bool()>& validate,
			                       std::filesystem::path* originalLocation = nullptr)
			{
				const auto result = publish(file_.get(), destination, expected, validate, originalLocation);
				published_ = !result;
				return result;
			}

		private:
			std::error_code prepare(const std::filesystem::path& destination)
			{
				const auto target = native_path(destination);
				if (!pin_folder(parent_, target.parent_path())) return win_error(GetLastError());
				path_ = publication::temporary_sibling(target, L".iw-output-");
				return path_.empty() ? std::make_error_code(std::errc::io_error) : std::error_code{};
			}

			static DWORD CALLBACK capture_copy_identity(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER,
			                                            DWORD, DWORD, HANDLE, HANDLE destination, LPVOID context)
			{
				auto& info = *static_cast<BY_HANDLE_FILE_INFORMATION*>(context);
				if (info.nFileIndexHigh || info.nFileIndexLow) return PROGRESS_QUIET;
				return GetFileInformationByHandle(destination, &info) ? PROGRESS_QUIET : PROGRESS_CANCEL;
			}

			std::filesystem::path path_;
			publication::File parent_, file_;
			bool published_{};
		};

		ItemKind classify_extension(const std::wstring& extension)
		{
			if (imaging::supports_extension(extension) && !imaging::is_preview_only(extension)) return ItemKind::image;
			constexpr std::array documents{L".doc", L".docx", L".pdf", L".ppt",  L".pptx",
			                               L".rtf", L".txt",  L".xls", L".xlsx", L".csv"};
			constexpr std::array videos{L".avi",  L".m4v", L".mkv",  L".mov", L".mp4", L".mpeg", L".mpg", L".wmv",
			                            L".webm", L".mts", L".m2ts", L".asf", L".3gp",  L".3g2",  L".ts", L".flv"};
			constexpr std::array audio{L".aac", L".flac", L".m4a",  L".mp3", L".ogg",
			                           L".wav", L".wma",  L".opus", L".aif", L".aiff"};
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

	}

	void set_show_hidden(const bool value)
	{
		showHidden.store(value, std::memory_order_relaxed);
	}

	bool show_hidden()
	{
		return showHidden.load(std::memory_order_relaxed);
	}

	bool is_supported_image(const std::filesystem::path& path)
	{
		const auto extension = paths::lowercase_extension(path);
		return imaging::supports_extension(extension) && !imaging::is_preview_only(extension);
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
		std::error_code directoryError;
		const bool directory = std::filesystem::is_directory(native_path(path), directoryError);
		const auto attributes = directory ? ::platform::file_attributes(desktop::folder_path(path))
		                                  : ::platform::file_attributes(desktop::file_path(path));
		if (!attributes.exists())
		{
			if (attributes.confirmed_missing()) return FileSnapshot{};
			return std::nullopt;
		}
		if (!attributes.identity.valid) return std::nullopt;
		return FileSnapshot{true,
		                    directory,
		                    attributes.is_readonly,
		                    attributes.is_reparse,
		                    attributes.identity.volume,
		                    attributes.identity.index,
		                    attributes.size,
		                    attributes.modified};
	}

	bool matches_snapshot(const std::filesystem::path& path, const FileSnapshot& snapshot)
	{
		const auto current = snapshot_file(path);
		return current && *current == snapshot;
	}

	std::optional<std::array<unsigned char, 32>> file_digest(const std::filesystem::path& path,
	                                                         const std::stop_token& stop)
	{
		std::atomic_bool cancelled{false};
		const std::stop_callback cancel(stop, [&] { cancelled = true; });
		return ::platform::file_sha256(desktop::file_path(path), ::core::cancel_token(cancelled));
	}

	ItemKind classify(const std::filesystem::path& path, const bool directory)
	{
		if (directory) return ItemKind::folder;
		return classify_extension(paths::lowercase_extension(path));
	}

	std::vector<FolderItem> scan_folder_items(const std::filesystem::path& folder, const std::stop_token& stop,
	                                          const std::function<void(size_t, size_t)>& progress,
	                                          std::error_code* error)
	{
		if (error) error->clear();
		std::vector<FolderItem> result;
		try
		{
			std::error_code iterationError;
			std::filesystem::directory_iterator iterator(native_path(folder), iterationError);
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
					const bool directory = attributes.known ? attributes.directory : entry.is_directory(entryError);
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

	std::vector<std::filesystem::path> scan_folder(const std::filesystem::path& folder, const std::stop_token& stop,
	                                               const std::function<void(size_t, size_t)>& progress)
	{
		std::vector<std::filesystem::path> result;
		try
		{
			std::error_code iterationError;
			std::filesystem::directory_iterator iterator(native_path(folder),
			                                             std::filesystem::directory_options::skip_permission_denied,
			                                             iterationError);
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
			{ return paths::natural_compare(left.filename().wstring(), right.filename().wstring()) < 0; });
		}
		catch (const std::exception&)
		{
			return {};
		}
		return result;
	}

	std::filesystem::path next_image_path(const std::filesystem::path& folder, const ImageSaveFormat format)
	{
		constexpr size_t maximumAttempts = 4096;
		std::error_code error;
		for (size_t number = 1; number <= maximumAttempts; ++number)
		{
			auto candidate = folder / std::format(L"New {}{}", number, image_save_extension(format));
			const bool exists = std::filesystem::exists(native_path(candidate), error);
			if (error) return {};
			if (!exists) return candidate;
		}
		return {};
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
		const auto isSidecar = [](const std::filesystem::path& path)
		{
			const auto extension = paths::lowercase_extension(path);
			return std::ranges::find(companions, extension) != companions.end();
		};
		if (isSidecar(primary)) return result;
		const auto ownsStem = [&]
		{
			WIN32_FIND_DATAW entry{};
			const auto search = FindFirstFileW(native_path(folder / (stem + L".*")).c_str(), &entry);
			if (search == INVALID_HANDLE_VALUE) return false;
			bool unambiguous = true;
			do
			{
				const std::filesystem::path sibling(entry.cFileName);
				if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				    sibling.native() != primary.filename().native() && paths::iequals(sibling.stem().native(), stem) &&
				    !isSidecar(sibling) && !paths::iequals(sibling.filename().native(), name))
				{
					unambiguous = false;
					break;
				}
			} while (FindNextFileW(search, &entry));
			if (unambiguous && GetLastError() != ERROR_NO_MORE_FILES) unambiguous = false;
			FindClose(search);
			return unambiguous;
		};
		std::optional<bool> stemIsUnique;
		std::error_code error;
		for (const auto* extension : companions)
		{
			// Both conventions are in use: photo.xmp beside photo.jpg, and photo.jpg.xmp.
			const auto exact = folder / (name + extension);
			const auto generic = folder / (stem + extension);
			for (const auto& candidate : {generic, exact})
			{
				if (paths::equal(candidate, primary)) continue;
				error.clear();
				if (!std::filesystem::is_regular_file(native_path(candidate), error)) continue;
				if (candidate != exact)
				{
					if (!stemIsUnique) stemIsUnique = ownsStem();
					if (!*stemIsUnique) continue;
				}
				if (std::ranges::any_of(result, [&candidate](const std::filesystem::path& found)
				{ return paths::equal(found, candidate); }))
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
			auto candidate = folder / std::format(L"{} ({}){}", stem, number, extension);
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
			const auto destinationAttributes = ::platform::file_attributes(desktop::file_path(destination));
			if (destinationAttributes.exists() && !overwrite) return std::make_error_code(std::errc::file_exists);
			if (!destinationAttributes.exists() && !destinationAttributes.confirmed_missing())
				return desktop::attributes_error(destinationAttributes);
			const auto folder = destination.parent_path();
			if (!folder.empty())
			{
				const auto folderAttributes = ::platform::file_attributes(desktop::folder_path(folder));
				if (!folderAttributes.exists())
				{
					if (!folderAttributes.confirmed_missing()) return desktop::attributes_error(folderAttributes);
					if (const auto created = ::platform::create_folder(desktop::folder_path(folder)); created.failed())
						return desktop::operation_error(created);
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
		const auto expected = overwrite ? snapshot_file(destination) : std::optional<FileSnapshot>{FileSnapshot{}};
		if (!expected) return std::make_error_code(std::errc::io_error);
		const publication::File input(CreateFileW(native_path(source).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		                                          OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
		if (!input) return win_error(GetLastError());
		if (!publication::stamp(input.get())) return std::make_error_code(std::errc::not_supported);
		TemporaryOutput output;
		if (const auto error = output.copy_from(source, destination)) return error;
		return output.commit(destination, expected, beforeCommit);
	}

	std::error_code move_file_to(const std::filesystem::path& source, const std::filesystem::path& destination,
	                             const bool overwrite, const std::function<bool()>& beforeCommit)
	{
		std::filesystem::path nativeDestination;
		if (const auto prepared = prepare_destination(destination, overwrite, nativeDestination)) return prepared;
		if (paths::equal(source, destination)) return std::make_error_code(std::errc::invalid_argument);
		const auto expected = overwrite ? snapshot_file(destination) : std::optional<FileSnapshot>{FileSnapshot{}};
		if (!expected) return std::make_error_code(std::errc::io_error);
		const auto absolute = native_path(source);
		publication::File sourceParent, destinationParent;
		if (!pin_folder(sourceParent, absolute.parent_path()) ||
		    !pin_folder(destinationParent, nativeDestination.parent_path()))
			return win_error(GetLastError());
		const publication::File input(CreateFileW(absolute.c_str(), FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ,
		                                          nullptr, OPEN_EXISTING,
		                                          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
		if (!input) return win_error(GetLastError());
		BY_HANDLE_FILE_INFORMATION info{};
		if (!GetFileInformationByHandle(input.get(), &info)) return win_error(GetLastError());
		if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return std::make_error_code(std::errc::not_supported);
		return publish(input.get(), destination, expected, beforeCommit, nullptr);
	}

	std::filesystem::path create_original_backup(const std::filesystem::path& path)
	{
		std::error_code error;
		if (!std::filesystem::is_regular_file(native_path(path), error)) return {};
		const auto proposed = path.parent_path() / (path.stem().wstring() + L"-original" + path.extension().wstring());
		auto backup = unique_destination(proposed);
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

		std::wstring wide(const std::string& utf8)
		{
			return ::platform::utf8_to_utf16(utf8);
		}

		// As EXIF writes a date, "YYYY:MM:DD HH:MM:SS", by the camera's clock whatever zone the file states, as
		// Diffractor shows it; as much of it as the file gives.
		std::wstring exif_text(const libfiles::date& d)
		{
			if (!d.valid()) return {};
			if (d.has_time)
				return std::format(L"{:04}:{:02}:{:02} {:02}:{:02}:{:02}", d.year, d.month, d.day, d.hour, d.minute,
				                   d.second);
			if (d.day != 0) return std::format(L"{:04}:{:02}:{:02}", d.year, d.month, d.day);
			if (d.month != 0) return std::format(L"{:04}:{:02}", d.year, d.month);
			return std::format(L"{:04}", d.year);
		}

		std::wstring token_value(const std::wstring_view token, const Metadata& metadata,
		                         const std::filesystem::path& path, const NameDate& date)
		{
			const auto is = [token](const wchar_t* name)
			{
				return paths::iequals(token, name);
			};
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
		const auto photo = imaging::read_properties(path);
		const auto make = wide(photo.camera_make);
		const auto model = wide(photo.camera_model);
		result.camera = make.empty() ? model : model.empty() ? make : make + L" " + model;
		result.dateTaken = exif_text(photo.created);

		// Under a second as 1/n, as a camera shows it, and otherwise in seconds.
		if (photo.exposure_time > 0.0 && photo.exposure_time < 1.0)
			result.exposure = std::format(L"1/{:.0f} s", 1.0 / photo.exposure_time);
		else if (photo.exposure_time >= 1.0) result.exposure = std::format(L"{:.1f} s", photo.exposure_time);

		if (photo.f_number > 0.0) result.aperture = std::format(L"f/{:.1f}", photo.f_number);
		if (photo.iso_speed > 0) result.iso = std::format(L"ISO {}", photo.iso_speed);
		if (photo.focal_length > 0.0) result.focalLength = std::format(L"{:.1f} mm", photo.focal_length);
		return result;
	}
	std::wstring read_colour_profile(const std::filesystem::path& path)
	{
		if (classify(path) != ItemKind::image) return {};
		const auto profile = imaging::read_colour_profile(path);
		if (!profile) return {};

		// The name the profile gives itself, or failing that the gamut libfiles recognises in its colorants.
		if (!profile->name.empty()) return wide(profile->name);
		return profile->primaries == "custom" ? std::wstring{} : wide(profile->primaries);
	}
	bool read_dimensions(const std::filesystem::path& path, int& width, int& height)
	{
		if (classify(path) == ItemKind::video)
		{
			const auto info = media::probe(path);
			if (info.width <= 0 || info.height <= 0) return false;
			width = info.width;
			height = info.height;
			return true;
		}
		return imaging::read_dimensions(path, width, height);
	}

	namespace
	{
		DecodedImage decode_photo(const std::filesystem::path& path, const int width, const int height,
		                          const bool thumbnail)
		{
			imaging::DecodeOptions options;
			options.maximumWidth = width;
			options.maximumHeight = height;
			options.thumbnail = thumbnail;
			options.stop = platform::exit_token();
			auto result = imaging::decode(path, options);
			if (!result && result.error != imaging::ImageError::cancelled) platform::write_diagnostic(result.message);
			return std::move(result.image);
		}
	}

	DecodedImage load_image(const std::filesystem::path& path)
	{
		return decode_photo(path, 0, 0, false);
	}
	DecodedImage load_image_for_area(const std::filesystem::path& path, int width, int height)
	{
		return decode_photo(path, width, height, false);
	}
	DecodedImage load_thumbnail(const std::filesystem::path& path, int width, int height)
	{
		if (classify(path) == ItemKind::video) return media::poster(path, width, height);
		return decode_photo(path, width, height, true);
	}

	std::wstring in_place_edit_block_reason(const std::filesystem::path& path, const ImageSaveFormat outputFormat)
	{
		constexpr auto unreadable = L"The original could not be checked for features the edited copy cannot preserve. "
									L"Use Save as to keep the original.";
		imaging::PlatformFileStream input(path);
		if (!input.is_open() || input.size() < 16) return unreadable;

		try
		{
			libfiles::reader reader(input);
			const auto format = libfiles::detect_image_format(reader.view(0, 16));
			using Format = libfiles::image_format;
			const auto expected = [outputFormat]
			{
				switch (outputFormat)
				{
				case ImageSaveFormat::jpeg: return Format::jpeg;
				case ImageSaveFormat::png: return Format::png;
				case ImageSaveFormat::tiff: return Format::tiff;
				case ImageSaveFormat::bmp: return Format::bmp;
				case ImageSaveFormat::webp: return Format::webp;
				}
				return Format::unknown;
			}();
			if (format != expected)
				return L"The original's format does not match its filename. Use Save as to keep the original.";

			if (format == Format::tiff)
			{
				const auto probe = ::platform::probe_image_file(desktop::file_path(path));
				if (!probe) return unreadable;
				if (probe->frame_count > 1)
					return L"Saving over this TIFF would discard additional pages. Use Save as to keep the original.";
			}
			const auto scan = libfiles::scan(reader);
			if (!scan.success) return unreadable;
			// WIC does not expose all animation frames, and Photo Edit encodes only eight-bit SDR pixels.
			if (scan.animated)
				return L"Saving over this image would discard its animation. Use Save as to keep the original.";
			if (scan.hdr || scan.gain_map || scan.picture.bit_depth > 8)
				return L"Saving over this photo would discard HDR or higher bit depth. Use Save as to keep the original.";
		}
		catch (const libfiles::read_error&)
		{
			return unreadable;
		}
		return {};
	}

	bool save_image(const DecodedImage& image, const std::filesystem::path& path, ImageSaveFormat format,
	                const SaveOptions& options, const bool overwrite, const std::function<bool()>& beforeCommit,
	                const std::optional<FileSnapshot>& expected, std::filesystem::path* originalLocation)
	{
		const auto reviewed = overwrite ? (expected ? expected : snapshot_file(path))
		                                : std::optional<FileSnapshot>{FileSnapshot{}};
		TemporaryOutput output;
		if (path.empty() || !reviewed) return false;
		if (const auto error = output.create(path))
		{
			platform::write_diagnostic(std::format(L"Could not stage an image save (error {}).\n", error.value()));
			return false;
		}
		const auto result = imaging::encode(image, format, options,
		                                    [&output](const auto bytes) { return output.write(bytes); });
		for (const auto& warning : result.warnings) platform::write_diagnostic(warning);
		if (!result)
		{
			platform::write_diagnostic(result.message);
			return false;
		}
		const auto error = output.commit(path, reviewed, beforeCommit, originalLocation);
		if (error && error != std::errc::file_exists && error != std::errc::operation_canceled)
			platform::write_diagnostic(std::format(L"Could not publish an image save (error {}).\n", error.value()));
		return !error;
	}

	std::error_code write_staged_file(const std::filesystem::path& path, const FileSnapshot& expected,
	                                 const std::function<bool(const StagedStream&)>& writer,
	                                 const std::function<bool()>& beforeCommit, std::filesystem::path* originalLocation)
	{
		if (!matches_snapshot(path, expected)) return std::make_error_code(std::errc::state_not_recoverable);
		TemporaryOutput output;
		if (const auto error = output.create(path)) return error;
		if (!writer(output.stream()) || !output.flush()) return std::make_error_code(std::errc::io_error);
		return output.commit(path, expected, beforeCommit, originalLocation);
	}
}
