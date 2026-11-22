#pragma once

#include <windows.h>
#include <objbase.h>

#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace iw::publication
{
	struct Stamp
	{
		DWORD volume{};
		DWORD indexHigh{};
		DWORD indexLow{};
		DWORD sizeHigh{};
		DWORD sizeLow{};
		LONGLONG modified{};
		LONGLONG changed{};
		bool operator==(const Stamp&) const = default;
	};

	inline std::optional<Stamp> stamp(const HANDLE file)
	{
		BY_HANDLE_FILE_INFORMATION id{};
		FILE_BASIC_INFO basic{};
		if (!GetFileInformationByHandle(file, &id) ||
		    !GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic)) ||
		    (id.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
		    (!id.nFileIndexHigh && !id.nFileIndexLow))
			return {};
		return Stamp{id.dwVolumeSerialNumber,  id.nFileIndexHigh, id.nFileIndexLow,
		             id.nFileSizeHigh,         id.nFileSizeLow,   basic.LastWriteTime.QuadPart,
		             basic.ChangeTime.QuadPart};
	}

	class File
	{
	public:
		explicit File(const HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}
		~File() { reset(); }
		File(const File&) = delete;
		File& operator=(const File&) = delete;
		void reset(const HANDLE handle = INVALID_HANDLE_VALUE)
		{
			if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
			handle_ = handle;
		}
		HANDLE get() const { return handle_; }
		explicit operator bool() const { return handle_ != INVALID_HANDLE_VALUE; }

	private:
		HANDLE handle_;
	};

	inline std::optional<Stamp> stamp(const std::filesystem::path& path)
	{
		const File file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
		                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		                            FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
		return file ? stamp(file.get()) : std::nullopt;
	}

	inline std::filesystem::path temporary_sibling(const std::filesystem::path& path, const wchar_t* prefix)
	{
		GUID id{};
		wchar_t text[40]{};
		if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, text, ARRAYSIZE(text))) return {};
		return path.parent_path() / (std::wstring(prefix) + text + L".tmp");
	}

	inline std::wstring extended(const std::filesystem::path& path)
	{
		const auto& value = path.native();
		if (value.starts_with(LR"(\\?\)")) return value;
		return value.starts_with(LR"(\\)") ? LR"(\\?\UNC\)" + value.substr(2) : LR"(\\?\)" + value;
	}

	class AvailableName
	{
	public:
		explicit AvailableName(const std::filesystem::path& absolute)
		{
			const auto name = extended(absolute);
			const auto bytes = name.size() * sizeof(wchar_t);
			buffer_.resize(sizeof(FILE_RENAME_INFO) + bytes);
			auto* const info = reinterpret_cast<FILE_RENAME_INFO*>(buffer_.data());
			info->ReplaceIfExists = FALSE;
			info->RootDirectory = nullptr;
			info->FileNameLength = static_cast<DWORD>(bytes);
			std::memcpy(info->FileName, name.c_str(), bytes);
		}

		DWORD move(const HANDLE file)
		{
			return SetFileInformationByHandle(file, FileRenameInfo, buffer_.data(), static_cast<DWORD>(buffer_.size()))
			           ? ERROR_SUCCESS
			           : GetLastError();
		}

	private:
		std::vector<unsigned char> buffer_;
	};

	inline bool remove(const HANDLE file)
	{
		FILE_DISPOSITION_INFO disposition{TRUE};
		return SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
	}
}
