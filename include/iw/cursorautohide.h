#pragma once

// Hiding the mouse pointer while the user is idle, without hiding it for the
// rest of the desktop.
//
// The historical implementations used ShowCursor(FALSE), not a boolean: it
// decrements a display counter that is not scoped to any window, so while that
// counter is negative the pointer is invisible wherever the user moves it,
// including over other applications. Those implementations then restored it
// only from a WM_MOUSEMOVE delivered to one particular window, so Alt-Tab, a
// second monitor, a modal dialog or a view switch left the counter negative
// with nothing still running that would put it back. Some could not restore it
// once HideCursor was turned off; others took mouse capture as well, stopping
// every other window on the desktop from setting its own cursor.
//
// WM_SETCURSOR has none of that. Returning TRUE after SetCursor(nullptr) hides
// the pointer only while it is over our own client area; every other window, in
// this process or any other, sets its own cursor from its own WM_SETCURSOR, so
// the hidden state physically cannot escape. There is no counter to balance,
// no capture to hold, and no restore path to forget.
//
// Idle is measured with GetLastInputInfo, which sees keyboard and mouse across
// the whole session. That replaces the per-window WM_MOUSEMOVE bookkeeping the
// originals used, including the compare-the-last-point hack they needed to tell
// a real mouse move from one synthesised under a stationary pointer.

#include <windows.h>

namespace IW
{
	class CCursorAutoHide
	{
	public:
		bool IsCursorHidden() const { return _bHidden; }

		// Drive from the owning window's timer. Returns true if the state changed.
		bool Update(HWND hwnd, bool bEnabled, DWORD nIdleDelayMS)
		{
			return SetHidden(hwnd, bEnabled && IdleTimeMS() >= nIdleDelayMS);
		}

		bool Show(HWND hwnd)
		{
			return SetHidden(hwnd, false);
		}

		// WM_SETCURSOR. True when the message was consumed, in which case the
		// window's handler must return TRUE without calling DefWindowProc.
		bool OnSetCursor(HWND hwnd, WPARAM wParam, LPARAM lParam) const
		{
			if (!_bHidden || reinterpret_cast<HWND>(wParam) != hwnd || LOWORD(lParam) != HTCLIENT)
				return false;

			::SetCursor(nullptr);
			return true;
		}

	private:
		bool SetHidden(HWND hwnd, bool bHidden)
		{
			if (_bHidden == bHidden)
				return false;

			_bHidden = bHidden;

			// The pointer is stationary by definition when we hide, so nothing
			// would ask for a WM_SETCURSOR until it next moved.
			if (::IsWindow(hwnd) && IsCursorOver(hwnd))
			{
				::SendMessage(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd),
				              MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
			}

			return true;
		}

		static bool IsCursorOver(HWND hwnd)
		{
			POINT pt;

			if (!::GetCursorPos(&pt))
				return false;

			HWND hwndPoint = ::WindowFromPoint(pt);
			return hwndPoint == hwnd || ::IsChild(hwnd, hwndPoint);
		}

		static DWORD IdleTimeMS()
		{
			LASTINPUTINFO lii = {sizeof(LASTINPUTINFO), 0};

			if (!::GetLastInputInfo(&lii))
				return 0;

			return ::GetTickCount() - lii.dwTime; // unsigned, so tick wrap is fine
		}

		bool _bHidden = false;
	};
}
