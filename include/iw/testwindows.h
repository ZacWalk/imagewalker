#pragma once

#include <windows.h>
#include <commctrl.h>

namespace IW::Test
{
// Native dialogs still run their real modal loops, but only this test thread's
// top-level windows are placed outside the desktop and refused activation.
class OffscreenWindows
{
	static constexpr const wchar_t* geometryProperty_ = L"ImageWalker.Test.HiddenGeometry";
	HHOOK creation_ = nullptr;
	HHOOK positioning_ = nullptr;

	static void Place(int& x, int& y, int width, int height)
	{
		x = GetSystemMetrics(SM_XVIRTUALSCREEN) - (width > 0 ? width : 640) - 64;
		y = GetSystemMetrics(SM_YVIRTUALSCREEN) - (height > 0 ? height : 480) - 64;
	}

	static LRESULT CALLBACK Creation(int code, WPARAM wParam, LPARAM lParam)
	{
		if (code == HCBT_ACTIVATE || code == HCBT_SETFOCUS) return 1;
		if (code == HCBT_CREATEWND)
		{
			auto& window = *reinterpret_cast<CBT_CREATEWND*>(lParam)->lpcs;
			if (!(window.style & WS_CHILD))
			{
				Place(window.x, window.y, window.cx, window.cy);
				window.dwExStyle = (window.dwExStyle & ~WS_EX_APPWINDOW) |
					WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
			}
		}
		return CallNextHookEx(nullptr, code, wParam, lParam);
	}

	static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
		UINT_PTR, DWORD_PTR)
	{
		if (message == WM_NCDESTROY)
		{
			RemovePropW(window, geometryProperty_);
			RemoveWindowSubclass(window, WindowProcedure, 1);
		}
		const auto result = DefSubclassProc(window, message, wParam, lParam);
		if (message == WM_WINDOWPOSCHANGING)
		{
			auto& position = *reinterpret_cast<WINDOWPOS*>(lParam);
			if (GetPropW(window, geometryProperty_))
				position.flags = (position.flags & ~SWP_SHOWWINDOW) | SWP_HIDEWINDOW | SWP_NOACTIVATE;
			else
			{
				RECT bounds{};
				GetWindowRect(window, &bounds);
				const int width = position.flags & SWP_NOSIZE ? bounds.right - bounds.left : position.cx;
				const int height = position.flags & SWP_NOSIZE ? bounds.bottom - bounds.top : position.cy;
				Place(position.x, position.y, width, height);
				position.flags = (position.flags & ~SWP_NOMOVE) | SWP_NOACTIVATE;
			}
		}
		return result;
	}

	static LRESULT CALLBACK Positioning(int code, WPARAM wParam, LPARAM lParam)
	{
		if (code >= 0)
		{
			const auto& message = *reinterpret_cast<const CWPSTRUCT*>(lParam);
			if ((message.message == WM_CREATE || message.message == WM_INITDIALOG) &&
				!(GetWindowLongPtr(message.hwnd, GWL_STYLE) & WS_CHILD))
			{
				SetWindowSubclass(message.hwnd, WindowProcedure, 1, 0);
				SetWindowLongPtr(message.hwnd, GWL_EXSTYLE,
					(GetWindowLongPtr(message.hwnd, GWL_EXSTYLE) & ~WS_EX_APPWINDOW) |
					WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
			}
		}
		return CallNextHookEx(nullptr, code, wParam, lParam);
	}

public:
	OffscreenWindows()
	{
		const auto thread = GetCurrentThreadId();
		creation_ = SetWindowsHookEx(WH_CBT, Creation, nullptr, thread);
		positioning_ = SetWindowsHookEx(WH_CALLWNDPROC, Positioning, nullptr, thread);
	}

	~OffscreenWindows()
	{
		if (positioning_) UnhookWindowsHookEx(positioning_);
		if (creation_) UnhookWindowsHookEx(creation_);
	}

	OffscreenWindows(const OffscreenWindows&) = delete;
	OffscreenWindows& operator=(const OffscreenWindows&) = delete;
	explicit operator bool() const { return creation_ && positioning_; }

	// Fullscreen checks need the real monitor rectangles, not displaced coordinates.
	static bool PreserveGeometry(HWND window)
	{
		ShowWindow(window, SW_HIDE);
		return SetPropW(window, geometryProperty_, reinterpret_cast<HANDLE>(1)) != FALSE;
	}

	static bool OutsideDesktop(HWND window)
	{
		RECT bounds{}, intersection{};
		if (!GetWindowRect(window, &bounds)) return false;
		const auto x = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const auto y = GetSystemMetrics(SM_YVIRTUALSCREEN);
		const RECT desktop{x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN),
			y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
		return !IntersectRect(&intersection, &bounds, &desktop);
	}
};
}
