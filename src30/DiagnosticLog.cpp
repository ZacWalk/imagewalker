#include "DiagnosticLog.h"
#include "DesktopServices.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <mutex>
#include <string>

namespace iw::diagnostics
{
	struct LogFile::Impl
	{
		std::mutex mutex;
		::platform::file_ptr file;
	};

	LogFile::LogFile() : impl_(std::make_unique<Impl>()) {}
	LogFile::~LogFile() { close(); }

	std::error_code LogFile::open(const std::filesystem::path& path)
	{
		std::lock_guard lock(impl_->mutex);
		if (impl_->file) return std::make_error_code(std::errc::device_or_resource_busy);
		impl_->file = ::platform::open_file(desktop::file_path(path),
		                                    ::platform::file_open_mode::create_exclusive);
		if (!impl_->file) return std::make_error_code(std::errc::io_error);
		return {};
	}

	std::error_code LogFile::write(const std::wstring_view text)
	{
		auto bytes = ::platform::utf16_to_utf8(text);
		const auto now = std::chrono::system_clock::now();
		const auto milliseconds = std::chrono::floor<std::chrono::milliseconds>(now);
		const auto seconds = std::chrono::floor<std::chrono::seconds>(milliseconds);
		const auto days = std::chrono::floor<std::chrono::days>(seconds);
		const std::chrono::year_month_day date{days};
		const std::chrono::hh_mm_ss time{seconds - days};
		const auto millisecond = static_cast<int>(
			milliseconds.time_since_epoch().count() % std::chrono::milliseconds::period::den);
		bytes.insert(0, std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z ",
			static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
			static_cast<unsigned>(date.day()), time.hours().count(), time.minutes().count(),
			time.seconds().count(), millisecond));
		if (bytes.empty() || bytes.back() != '\n') bytes.push_back('\n');
		std::lock_guard lock(impl_->mutex);
		if (!impl_->file) return std::make_error_code(std::errc::bad_file_descriptor);
		if (impl_->file->write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()) != bytes.size() ||
			!impl_->file->flush())
			return std::make_error_code(std::errc::io_error);
		return {};
	}

	void LogFile::close()
	{
		std::lock_guard lock(impl_->mutex);
		impl_->file.reset();
	}

	std::filesystem::path log_path(std::error_code& error)
	{
		error.clear();
		const auto running = ::platform::running_app_path();
		if (running.is_empty())
		{
			error = std::make_error_code(std::errc::io_error);
			return {};
		}
		auto result = ::platform::to_stream_path(running);
		result.replace_extension(L".log");
		return result;
	}

	LogFile& run_log()
	{
		static LogFile log;
		return log;
	}

	std::error_code open_run_log(LogFile& log, const std::filesystem::path& path, const unsigned long processId)
	{
		const auto error = log.open(path);
		if (!error) return {};
		// A second instance would otherwise report an unwritable folder from a
		// modal box raised before its window exists. The first failure is the one
		// reported when the fallback cannot open either.
		auto own = path;
		own.replace_filename(path.stem().wstring() + L"-" + std::to_wstring(processId) + path.extension().wstring());
		return log.open(own) ? error : std::error_code{};
	}

	Session::Session()
	{
		const auto path = log_path(error_);
		if (!error_) error_ = open_run_log(run_log(), path, ::platform::process_id());
		if (!error_) error_ = run_log().write(L"ImageWalker 3.0 starting");
	}

	Session::~Session()
	{
		if (!error_)
		{
			if (run_log().write(L"ImageWalker 3.0 exiting"))
				::core::debug_output(std::string("ImageWalker: unable to finish the diagnostic log."));
		}
		run_log().close();
	}
}
