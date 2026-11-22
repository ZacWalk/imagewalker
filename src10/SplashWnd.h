#pragma once

// The startup splash, shown over the frame while it comes up.

#include "Dib.h"

class CSplashWnd : public SdkWindow
{
public:
	// Owned by the frame. Unowned, the splash had a blank taskbar button of its
	// own and was the window found first by anything closing the app's main
	// window, so the close dismissed the splash and the app ran on.
	BOOL Show(HWND hWndOwner)
	{
		if (!m_dib.LoadResource(IDR_SPLASH, CSize(0, 0)))
			return FALSE;

		CRect r(0, 0, m_dib.Width(), m_dib.Height());
		MONITORINFO monitor{sizeof(monitor)};
		if (!::GetMonitorInfo(::MonitorFromWindow(hWndOwner, MONITOR_DEFAULTTONEAREST), &monitor))
			return FALSE;
		const CRect work(monitor.rcWork);
		r.OffsetRect(CPoint(work.left + (work.Width() - r.Width()) / 2, work.top + (work.Height() - r.Height()) / 2));

		if (!CreateNative("IWSplash", hWndOwner, r, nullptr, WS_VISIBLE | WS_POPUP,
		                  WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW))
			return FALSE;

		if (!SetTimer(1, 3000))
		{
			DestroyWindow();
			return FALSE;
		}
		UpdateWindow();

		return TRUE;
	}

private:
	LRESULT WindowMessage(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (message == WM_PAINT) { OnPaint(); return 0; }
		if (message == WM_TIMER && wParam == 1) { OnTimer(wParam); return 0; }
		return SdkWindow::WindowMessage(message, wParam, lParam);
	}

	void OnPaint()
	{
		CPaintDC dc(m_hWnd);

		CRect r;
		dc.GetClipBox(&r);

		if (!r.IsRectEmpty())
			m_dib.Draw(dc.m_hDC, r.TopLeft(), r);
	}

	void OnTimer(UINT_PTR nIDEvent)
	{
		KillTimer(nIDEvent);
		DestroyWindow();
	}

	CDib m_dib;
};
