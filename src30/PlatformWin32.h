// ImageWalker by Zac Walker
// Pins the Windows SDK configuration and declares the Win32 backend helpers shared between platform files.

#pragma once

// Include this before any header that reaches windows.h, including COM and WIC headers, so that
// every translation unit sees the same SDK version and macro configuration.

#ifndef WINVER
#define WINVER 0x0601
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06010000
#endif

#ifndef STRICT
#define STRICT 1
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#include <windows.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace iw::platform
{
	class WindowFrame;
	using WindowFramePtr = std::shared_ptr<WindowFrame>;

	namespace win32
	{
		bool wait_for_message();
		HWND owner_window(const WindowFramePtr& frame);
		HANDLE shell_small_image_list();
		int shell_icon_index(const std::filesystem::path& path);
		std::vector<std::filesystem::path> dropped_files(HANDLE drop);
	}
}
