#pragma once

// The startup splash. The one window that appears before the frame.

#include "Dib.h"

class CSplashWnd : public CWindowImpl<CSplashWnd>
{
public:
	DECLARE_WND_CLASS_EX(_T("IWSplash"), CS_HREDRAW | CS_VREDRAW, -1)

	BEGIN_MSG_MAP(CSplashWnd)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_TIMER(OnTimer)
	END_MSG_MAP()

	BOOL Show()
	{
		if (!m_dib.LoadResource(IDR_ICONMATE8, CSize(0, 0)))
			return FALSE;

		CRect r(0, 0, m_dib.Width(), m_dib.Height());

		r.OffsetRect((::GetSystemMetrics(SM_CXSCREEN) - r.Width()) / 2,
		             (::GetSystemMetrics(SM_CYSCREEN) - r.Height()) / 2);

		if (Create(nullptr, r, nullptr, WS_VISIBLE | WS_POPUP | WS_BORDER, WS_EX_TOPMOST) == nullptr)
			return FALSE;

		SetTimer(1, 3000);
		UpdateWindow();

		return TRUE;
	}

private:
	void OnPaint(CDCHandle)
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
