// ImageWalker by Zac Walker
// Prepares the compiled help that every executable carries as a resource.
//
// HTML Help shows blank pages for a .chm marked as downloaded - Windows marks every
// file extracted from a downloaded zip - and for a .chm on a network share. So each
// executable embeds ImageWalker.chm and, before showing it, writes it to the user's
// temporary folder: a local copy the application wrote itself carries neither
// restriction. The copy is named after its contents, so executables from different
// releases never share one, and an existing copy is used only when its bytes match.
//
// Header-only and wide-character throughout, so MBCS ArtMate 1.0, the Unicode 2.x
// trees and ImageWalker 3.0 share it.

#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace iw::help
{
	// The RCDATA resource each executable's resource script embeds the help file as.
	inline constexpr wchar_t resource_name[] = L"IMAGEWALKER_HELP";

	namespace detail
	{
		class File
		{
		public:
			explicit File(const HANDLE handle) : handle_(handle) {}
			File(const File&) = delete;
			File& operator=(const File&) = delete;
			~File() { close(); }

			bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
			HANDLE get() const { return handle_; }

			bool close()
			{
				const bool closed = valid() && CloseHandle(handle_);
				handle_ = INVALID_HANDLE_VALUE;
				return closed;
			}

		private:
			HANDLE handle_;
		};

		constexpr size_t chunk = 64 * 1024;

		// True when `path` holds exactly these bytes and carries no mark of the web.
		inline bool holds(const std::wstring& path, const unsigned char* bytes, const size_t size)
		{
			if (GetFileAttributesW((path + L":Zone.Identifier").c_str()) != INVALID_FILE_ATTRIBUTES) return false;
			File file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
			LARGE_INTEGER length{};
			if (!file.valid() || !GetFileSizeEx(file.get(), &length) ||
				static_cast<unsigned long long>(length.QuadPart) != size)
				return false;
			std::string buffer(chunk, '\0');
			for (size_t offset = 0; offset < size;)
			{
				const DWORD wanted = static_cast<DWORD>(size - offset < chunk ? size - offset : chunk);
				DWORD read = 0;
				if (!ReadFile(file.get(), buffer.data(), wanted, &read, nullptr) || read != wanted ||
					std::memcmp(buffer.data(), bytes + offset, wanted) != 0)
					return false;
				offset += read;
			}
			return true;
		}

		inline bool write_all(const HANDLE file, const unsigned char* bytes, const size_t size)
		{
			for (size_t offset = 0; offset < size;)
			{
				const DWORD wanted = static_cast<DWORD>(size - offset < chunk ? size - offset : chunk);
				DWORD written = 0;
				if (!WriteFile(file, bytes + offset, wanted, &written, nullptr) || written != wanted) return false;
				offset += written;
			}
			return true;
		}
	}

	// The path of an unmarked copy of these help bytes in `folder`, written there unless an
	// identical copy already is. Empty when no copy can be written.
	inline std::wstring prepare(const void* data, const size_t size, std::wstring folder)
	{
		if (!data || !size || folder.empty()) return {};
		if (folder.back() != L'\\' && folder.back() != L'/') folder += L'\\';
		const auto* bytes = static_cast<const unsigned char*>(data);
		std::uint64_t hash = 14695981039346656037ull; // 64-bit FNV-1a
		for (size_t index = 0; index < size; ++index)
		{
			hash ^= bytes[index];
			hash *= 1099511628211ull;
		}
		wchar_t name[40]{};
		swprintf_s(name, L"ImageWalker-%016llx.chm", static_cast<unsigned long long>(hash));
		const std::wstring path = folder + name;
		if (detail::holds(path, bytes, size)) return path;

		wchar_t suffix[48]{};
		swprintf_s(suffix, L".%lx-%lx-%lx.tmp", GetCurrentProcessId(), GetCurrentThreadId(), GetTickCount());
		const std::wstring temporary = path + suffix;
		detail::File file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL, nullptr));
		if (!file.valid()) return {};
		const bool written = detail::write_all(file.get(), bytes, size);
		if (!file.close() || !written)
		{
			DeleteFileW(temporary.c_str());
			return {};
		}
		if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return path;
		DeleteFileW(temporary.c_str());
		// Another instance may have written the same copy first.
		return detail::holds(path, bytes, size) ? path : std::wstring{};
	}

	// Prepares the help file the executable embeds, in the user's temporary folder. Empty
	// when the executable carries none or no copy can be written.
	inline std::wstring prepare_embedded(const HMODULE module = nullptr)
	{
		const HRSRC resource = FindResourceW(module, resource_name, MAKEINTRESOURCEW(10)); // RT_RCDATA
		const HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
		const void* data = loaded ? LockResource(loaded) : nullptr;
		const DWORD size = resource ? SizeofResource(module, resource) : 0;
		wchar_t folder[MAX_PATH + 1]{};
		const DWORD length = GetTempPathW(MAX_PATH + 1, folder);
		if (!data || !size || !length || length > MAX_PATH) return {};
		return prepare(data, size, folder);
	}
}
