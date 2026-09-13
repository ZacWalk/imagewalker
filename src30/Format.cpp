// ImageWalker by Zac Walker
// Implements shared size, type, date, and count formatting so every surface reads the same.

#include "Format.h"
#include "Paths.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <format>

namespace iw::format
{
	std::wstring size(const std::uintmax_t bytes)
	{
		constexpr std::array units{L"bytes", L"KB", L"MB", L"GB", L"TB"};
		double value = static_cast<double>(bytes);
		size_t unit = 0;
		while (value >= 1024.0 && unit + 1 < units.size())
		{
			value /= 1024.0;
			++unit;
		}
		return unit ? std::format(L"{:.1f} {}", value, units[unit]) : std::format(L"{} {}", bytes, units[unit]);
	}

	std::wstring file_type(const std::filesystem::path& path)
	{
		auto extension = path.extension().wstring();
		std::ranges::transform(extension, extension.begin(), towupper);
		return extension.empty() ? L"Image" : extension.substr(1) + L" image";
	}

	std::wstring item_type(const files::FolderItem& item)
	{
		if (item.kind == files::ItemKind::folder) return L"File folder";
		if (item.kind == files::ItemKind::image) return file_type(item.path);
		auto extension = item.path.extension().wstring();
		std::ranges::transform(extension, extension.begin(), towupper);
		return extension.empty() ? L"File" : extension.substr(1) + L" file";
	}

	std::wstring modified(const std::filesystem::file_time_type time)
	{
		// Formatting a sys_time prints UTC, so the zone has to be applied before formatting.
		const auto utc = std::chrono::clock_cast<std::chrono::system_clock>(time);
		const auto local = std::chrono::current_zone()->to_local(utc);
		return std::format(L"{:%Y-%m-%d %H:%M}", std::chrono::floor<std::chrono::minutes>(local));
	}

	std::wstring dimensions(const int width, const int height)
	{
		return width > 0 && height > 0 ? std::format(L"{} x {}", width, height) : std::wstring{};
	}

	std::wstring item_count(const size_t files, const size_t folders)
	{
		std::wstring result = std::format(L"{} {}", files, files == 1 ? L"image" : L"images");
		if (folders) result += std::format(L", {} {}", folders, folders == 1 ? L"folder" : L"folders");
		return result;
	}
}
