// ImageWalker by Zac Walker
// Converts application paths and operation results at the libdesktop boundary.

#pragma once

#include <libdesktop/platform.h>

#include <filesystem>
#include <string>
#include <system_error>

namespace iw::desktop
{
	inline std::string utf8_path(const std::filesystem::path& path)
	{
		const auto text = path.u8string();
		return {reinterpret_cast<const char*>(text.data()), text.size()};
	}

	inline ::core::file_path file_path(const std::filesystem::path& path)
	{
		return ::core::file_path(utf8_path(path));
	}

	inline ::core::folder_path folder_path(const std::filesystem::path& path)
	{
		return ::core::folder_path(utf8_path(path));
	}

	inline std::filesystem::path stream_path(const std::filesystem::path& path)
	{
		return ::platform::to_stream_path(file_path(path));
	}

	inline std::error_code operation_error(const ::platform::file_op_result& result,
	                                       const std::error_code fallback = std::make_error_code(std::errc::io_error))
	{
		if (result.success()) return {};
		if (result.error) return result.error;
		if (result.code == ::platform::file_op_result_code::ALREADY_EXISTS)
			return std::make_error_code(std::errc::file_exists);
		return fallback;
	}

	inline std::error_code attributes_error(const ::platform::file_attributes_t& attributes)
	{
		if (attributes.exists()) return {};
		if (attributes.confirmed_missing()) return std::make_error_code(std::errc::no_such_file_or_directory);
		return std::make_error_code(std::errc::io_error);
	}
}
