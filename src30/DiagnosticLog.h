#pragma once

#include <filesystem>
#include <memory>
#include <string_view>
#include <system_error>

namespace iw::diagnostics
{
	class LogFile
	{
	public:
		LogFile();
		~LogFile();
		LogFile(const LogFile&) = delete;
		LogFile& operator=(const LogFile&) = delete;
		std::error_code open(const std::filesystem::path& path);
		std::error_code write(std::wstring_view text);
		void close();

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};

	std::filesystem::path log_path(std::error_code& error);
	LogFile& run_log();

	// Opens the latest-run log at path. When another live run holds it, which this
	// run must not truncate, opens <stem>-<processId><ext> beside it instead.
	std::error_code open_run_log(LogFile& log, const std::filesystem::path& path, unsigned long processId);

	class Session
	{
	public:
		Session();
		~Session();
		Session(const Session&) = delete;
		Session& operator=(const Session&) = delete;
		const std::error_code& error() const { return error_; }

	private:
		std::error_code error_;
	};
}
