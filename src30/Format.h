// ImageWalker by Zac Walker
// Declares the shared display formatting used by the canvas, details columns, and status strip.

#pragma once

#include "Files.h"
#include <cstdint>
#include <filesystem>
#include <string>

namespace iw::format
{
	std::wstring size(std::uintmax_t bytes);
	std::wstring file_type(const std::filesystem::path& path);
	std::wstring item_type(const files::FolderItem& item);
	std::wstring modified(std::filesystem::file_time_type time);
	std::wstring dimensions(int width, int height);

	// "1 image" / "12 images" style counts that stay truthful when the collection holds folders.
	std::wstring item_count(size_t files, size_t folders);
}
