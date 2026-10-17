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

	// "1 image" / "12 images" style counts that stay truthful when the collection holds folders
	// and other files: each kind is counted under its own name.
	std::wstring item_count(size_t images, size_t folders, size_t otherFiles = 0);

	// "2 of 30 items selected (27 images, 3 other files)": a selection can hold folders and other
	// files as well as images, so it is counted against every item, never against the images alone.
	std::wstring selection_count(size_t selected, size_t images, size_t folders, size_t otherFiles = 0);
}
