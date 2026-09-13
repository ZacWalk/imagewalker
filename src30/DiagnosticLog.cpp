#include "DiagnosticLog.h"

#include <windows.h>
#include <algorithm>
#include <format>
#include <limits>
#include <mutex>
#include <string>

namespace iw::diagnostics
{
	struct LogFile::Impl
	{
		std::mutex mutex;
		HANDLE file = INVALID_HANDLE_VALUE;
	};

	LogFile::LogFile() : impl_(std::make_unique<Impl>()) {}
	LogFile::~LogFile() { close(); }

	std::error_code LogFile::open(const std::filesystem::path& path)
	{
		std::lock_guard lock(impl_->mutex);
		if (impl_->file != INVALID_HANDLE_VALUE) return std::make_error_code(std::errc::device_or_resource_busy);
		impl_->file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (impl_->file == INVALID_HANDLE_VALUE)
			return {static_cast<int>(GetLastError()), std::system_category()};
		return {};
	}

	std::error_code LogFile::write(const std::wstring_view text)
	{
		if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
			return std::make_error_code(std::errc::value_too_large);
		const int size = static_cast<int>(text.size());
		const int length = size ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size,
			nullptr, 0, nullptr, nullptr) : 0;
		if (size && length == 0) return {static_cast<int>(GetLastError()), std::system_category()};
		std::string bytes(static_cast<size_t>(length), '\0');
		if (length && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size,
			bytes.data(), length, nullptr, nullptr))
			return {static_cast<int>(GetLastError()), std::system_category()};

		SYSTEMTIME now{};
		GetSystemTime(&now);
		bytes.insert(0, std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z ",
			now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds));
		if (bytes.empty() || bytes.back() != '\n') bytes.push_back('\n');
		std::lock_guard lock(impl_->mutex);
		if (impl_->file == INVALID_HANDLE_VALUE) return std::make_error_code(std::errc::bad_file_descriptor);
		size_t offset = 0;
		while (offset < bytes.size())
		{
			DWORD written = 0;
			if (!WriteFile(impl_->file, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &written, nullptr))
				return {static_cast<int>(GetLastError()), std::system_category()};
			if (!written) return std::make_error_code(std::errc::io_error);
			offset += written;
		}
		if (!FlushFileBuffers(impl_->file)) return {static_cast<int>(GetLastError()), std::system_category()};
		return {};
	}

	void LogFile::close()
	{
		std::lock_guard lock(impl_->mutex);
		if (impl_->file == INVALID_HANDLE_VALUE) return;
		if (!CloseHandle(impl_->file)) OutputDebugStringW(L"ImageWalker: cannot close diagnostic log.\n");
		impl_->file = INVALID_HANDLE_VALUE;
	}

	std::filesystem::path log_path(std::error_code& error)
	{
		error.clear();
		std::wstring path(260, L'\0');
		while (path.size() <= 32768)
		{
			const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
			if (!length)
			{
				error = {static_cast<int>(GetLastError()), std::system_category()};
				return {};
			}
			if (length < path.size())
			{
				path.resize(length);
				auto result = std::filesystem::path(path);
				result.replace_extension(L".log");
				return result;
			}
			path.resize((std::min)(path.size() * 2, size_t{32769}));
		}
		error = std::make_error_code(std::errc::filename_too_long);
		return {};
	}

	LogFile& run_log()
	{
		static LogFile log;
		return log;
	}

	Session::Session()
	{
		const auto path = log_path(error_);
		if (!error_) error_ = run_log().open(path);
		if (!error_) error_ = run_log().write(L"ImageWalker 3.0 starting");
	}

	Session::~Session()
	{
		if (!error_)
		{
			if (run_log().write(L"ImageWalker 3.0 exiting"))
				OutputDebugStringW(L"ImageWalker: unable to finish the diagnostic log.\n");
		}
		run_log().close();
	}
}
