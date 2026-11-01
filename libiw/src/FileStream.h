// ImageWalker by Zac Walker
// A file libfiles reads where it asks, a few blocks of it, through Win32.

#pragma once

#include "ImagingWin32.h"
#include <libiw/Codec.h>

#include <libfiles/stream.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>

namespace iw::imaging
{
	class FileStream final : public libfiles::stream
	{
	public:
		explicit FileStream(const std::filesystem::path& path) :
			file_(CreateFileW(native_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
		{
			LARGE_INTEGER size{};
			if (file_ != INVALID_HANDLE_VALUE && GetFileSizeEx(file_, &size)) size_ = static_cast<std::uint64_t>(size.QuadPart);
		}

		~FileStream() override
		{
			if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
		}

		FileStream(const FileStream&) = delete;
		FileStream& operator=(const FileStream&) = delete;

		bool is_open() const { return file_ != INVALID_HANDLE_VALUE; }
		std::uint64_t size() override { return size_; }

		std::size_t read(const std::uint64_t offset, void* data, const std::size_t size) override
		{
			OVERLAPPED at{};
			at.Offset = static_cast<DWORD>(offset);
			at.OffsetHigh = static_cast<DWORD>(offset >> 32);
			DWORD got = 0;
			const auto wanted = static_cast<DWORD>(std::min<std::size_t>(size, 0x7FFFFFFF));
			return ReadFile(file_, data, wanted, &got, &at) ? got : 0;
		}

	private:
		HANDLE file_;
		std::uint64_t size_ = 0;
	};
}
