#pragma once

#include "file_publication.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <libfiles/encode.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace iw::screenshot
{
	struct Request
	{
		bool enabled{};
		std::filesystem::path output;
		std::filesystem::path input;
	};

	inline Request parse(int count, wchar_t* const* args)
	{
		if (count < 2 || std::wstring_view(args[1]) != L"--screenshot") return {};
		Request request;
		request.enabled = true;
		if ((count != 3 && count != 4) || !*args[2]) return request;
		std::error_code error;
		const auto output = std::filesystem::absolute(args[2], error);
		if (error || _wcsicmp(output.extension().c_str(), L".png") != 0) return request;
		if (count == 4)
		{
			if (!*args[3]) return request;
			request.input = std::filesystem::absolute(args[3], error);
			if (error || (!std::filesystem::is_directory(request.input, error) &&
			              !std::filesystem::is_regular_file(request.input, error)))
				return request;
		}
		request.output = output;
		return request;
	}

	inline Request command_line()
	{
		int count = 0;
		const auto args = std::unique_ptr<wchar_t*, decltype(&LocalFree)>(CommandLineToArgvW(GetCommandLineW(), &count),
		                                                                  LocalFree);
		return args ? parse(count, args.get()) : Request{};
	}

	inline void report(const wchar_t* text, const std::filesystem::path& output = {}) noexcept
	{
		std::fwprintf(stderr, L"ImageWalker screenshot: %ls%ls%ls\n", text, output.empty() ? L"" : L" ",
		              output.c_str());
		OutputDebugStringW(text);
	}

	inline bool write_new(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
	{
		if (!path.is_absolute() || bytes.empty() || bytes.size() > MAXDWORD) return false;
		const auto temporary = publication::temporary_sibling(path, L".iw-capture-");
		if (temporary.empty()) return false;
		publication::AvailableName target(path);
		const publication::File file(CreateFileW(publication::extended(temporary).c_str(), GENERIC_WRITE | DELETE, 0,
		                                        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (!file) return false;

		DWORD written = 0;
		const auto size = static_cast<DWORD>(bytes.size());
		if (WriteFile(file.get(), bytes.data(), size, &written, nullptr) && written == size &&
		    FlushFileBuffers(file.get()) && target.move(file.get()) == ERROR_SUCCESS)
			return true;
		if (!publication::remove(file.get())) report(L"Could not remove incomplete capture.", temporary);
		return false;
	}

	inline bool capture(HWND window, const std::filesystem::path& output)
	{
		DWORD owner = 0;
		RECT bounds{};
		if (!GetWindowThreadProcessId(window, &owner) || owner != GetCurrentProcessId() || IsIconic(window) ||
		    !GetWindowRect(window, &bounds))
			return false;
		const auto width = bounds.right - bounds.left;
		const auto height = bounds.bottom - bounds.top;
		if (width <= 0 || height <= 0 || static_cast<std::uint64_t>(width) * height > 40'000'000) return false;

		struct Bitmap
		{
			HDC dc{CreateCompatibleDC(nullptr)};
			HBITMAP bitmap{};
			HGDIOBJ previous{};
			~Bitmap()
			{
				if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
				if (bitmap) DeleteObject(bitmap);
				if (dc) DeleteDC(dc);
			}
		} surface;
		if (!surface.dc) return false;
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(info.bmiHeader);
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		void* pixels = nullptr;
		surface.bitmap = CreateDIBSection(surface.dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
		if (!surface.bitmap || !pixels) return false;
		surface.previous = SelectObject(surface.dc, surface.bitmap);
		// The Win7 SDK declarations used by the preserved editions omit this Windows 8.1 flag.
		constexpr UINT renderFullContent = 2;
		if (!surface.previous || surface.previous == HGDI_ERROR || !PrintWindow(window, surface.dc, renderFullContent))
			return false;
		GdiFlush();
		const libfiles::image_view image{static_cast<const std::uint8_t*>(pixels), static_cast<std::size_t>(width) * 4,
		                                 static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
		return write_new(output, libfiles::encode_png(image, 1, {}).data);
	}

	class Timer
	{
	public:
		explicit Timer(const Request& request) : request_(request) {}
		Timer(const Timer&) = delete;
		Timer& operator=(const Timer&) = delete;
		~Timer()
		{
			if (!window_) return;
			KillTimer(window_, id());
			RemoveWindowSubclass(window_, dispatch, id());
		}

		bool start(HWND window)
		{
			if (!request_.enabled) return true;
			if (!SetWindowSubclass(window, dispatch, id(), reinterpret_cast<DWORD_PTR>(this))) return false;
			if (!SetTimer(window, id(), 10'000, nullptr))
			{
				RemoveWindowSubclass(window, dispatch, id());
				return false;
			}
			window_ = window;
			return true;
		}

		int result(int code) const
		{
			if (!request_.enabled || code != 0 || saved_) return code;
			if (!attempted_) report(L"the window closed before a screenshot was saved.");
			return 1;
		}

	private:
		UINT_PTR id() const { return reinterpret_cast<UINT_PTR>(this); }

		static LRESULT CALLBACK dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id,
		                                 DWORD_PTR context) noexcept
		{
			auto& timer = *reinterpret_cast<Timer*>(context);
			if (message == WM_TIMER && wParam == id)
			{
				KillTimer(window, id);
				timer.attempted_ = true;
				try
				{
					timer.saved_ = capture(window, timer.request_.output);
				}
				catch (const std::exception&)
				{
					// Encoding failures must not cross the window procedure.
					timer.saved_ = false;
				}
				report(timer.saved_ ? L"saved:" : L"could not capture or write a new PNG:", timer.request_.output);
				PostMessageW(window, WM_CLOSE, 0, 0);
				return 0;
			}
			if (message == WM_NCDESTROY)
			{
				KillTimer(window, id);
				RemoveWindowSubclass(window, dispatch, id);
				timer.window_ = nullptr;
			}
			return DefSubclassProc(window, message, wParam, lParam);
		}

		const Request& request_;
		HWND window_{};
		bool attempted_{};
		bool saved_{};
	};

	inline void report_usage()
	{
		report(L"usage: --screenshot <new PNG file> [existing folder or image]");
	}
}
