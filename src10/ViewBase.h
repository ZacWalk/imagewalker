#pragma once

// The shared pane: a tiled background, an offscreen DIB and the scroll bars.
//
// The bars are real controls in a gutter along the right and bottom edges
// rather than the window's own non-client bars, because the view-mode strips
// sit in that gutter beside them.

#include "ArtMate.h"
#include "Dib.h"

// The powder tile behind both panes. One copy for the whole app.
inline CDib& BackgroundTile()
{
	static CDib dib;

	if (!dib.IsOpen())
		dib.LoadResource(IDR_BACKGROUND, CSize(0, 0));

	return dib;
}

template <class T>
class CViewBase : public CWindowImpl<T>
{
public:
	enum JUST { j_none, j_center, j_centerx, j_top };

	enum { kScrollBarV = 0x7f01, kScrollBarH = 0x7f02, kBarRange = 10000 };

	DECLARE_WND_CLASS_EX(nullptr, CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, -1)

	BEGIN_MSG_MAP(CViewBase)
		MSG_WM_PAINT(OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPrintClient)
		MSG_WM_SIZE(OnSize)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_VSCROLL(OnVScroll)
		MSG_WM_HSCROLL(OnHScroll)
		MSG_WM_MOUSEWHEEL(OnMouseWheel)
	END_MSG_MAP()

	// The drawable area: the client rect less whatever the gutters take.
	CRect ViewRect() const
	{
		CRect r;
		const_cast<CViewBase*>(this)->GetClientRect(&r);
		r.right = max(r.right - m_sizeGutter.cx, 0L);
		r.bottom = max(r.bottom - m_sizeGutter.cy, 0L);
		return r;
	}

	void ScrollTo(int x, int y)
	{
		const CRect r = ViewRect();

		y = Clamp(y, m_sizeScroll.cy - r.Height());
		x = Clamp(x, m_sizeScroll.cx - r.Width());

		if (y != m_pointScroll.y)
		{
			const int yo = m_pointScroll.y - y;
			m_pointScroll.y = y;

			if (m_barV.IsWindowVisible())
				m_barV.SetScrollPos(Fraction(y, m_sizeScroll.cy), TRUE);

			this->ScrollWindowEx(0, yo, &r, &r, nullptr, nullptr, SW_INVALIDATE);
		}

		if (x != m_pointScroll.x)
		{
			const int xo = m_pointScroll.x - x;
			m_pointScroll.x = x;

			if (m_barH.IsWindowVisible())
				m_barH.SetScrollPos(Fraction(x, m_sizeScroll.cx), TRUE);

			this->ScrollWindowEx(xo, 0, &r, &r, nullptr, nullptr, SW_INVALIDATE);
		}
	}

	// Publishes the content size and re-lays the gutters. A bar appearing takes
	// room from the view, which can make the other bar necessary, so two passes
	// settle it; a third could not change anything.
	void SetScrollSize(int cx, int cy, JUST eJust = j_none)
	{
		m_sizeScroll.cx = max(cx, 0);
		m_sizeScroll.cy = max(cy, 0);

		CRect rectClient;
		this->GetClientRect(&rectClient);

		bool bBarV = false;
		bool bBarH = false;

		for (int nPass = 0; nPass < 2; nPass++)
		{
			const int cxView = rectClient.Width() - max(m_sizeGutterMin.cx, bBarV ? m_cxBar : 0);
			const int cyView = rectClient.Height() - max(m_sizeGutterMin.cy, bBarH ? m_cyBar : 0);

			bBarV = m_sizeScroll.cy > cyView;
			bBarH = m_sizeScroll.cx > cxView;
		}

		m_sizeGutter.cx = max(m_sizeGutterMin.cx, bBarV ? m_cxBar : 0);
		m_sizeGutter.cy = max(m_sizeGutterMin.cy, bBarH ? m_cyBar : 0);

		LayoutGutters(bBarV, bBarH);

		const CRect rectView = ViewRect();
		const CPoint pointCenter((m_sizeScroll.cx - rectView.Width()) / 2,
		                         (m_sizeScroll.cy - rectView.Height()) / 2);

		switch (eJust)
		{
		case j_center:
			m_pointScroll = pointCenter;
			break;
		case j_centerx:
			m_pointScroll.x = pointCenter.x;
			break;
		case j_top:
			m_pointScroll.x = pointCenter.x;
			m_pointScroll.y = 0;
			break;
		case j_none:
			break;
		}

		m_pointScroll.x = Clamp(m_pointScroll.x, m_sizeScroll.cx - rectView.Width());
		m_pointScroll.y = Clamp(m_pointScroll.y, m_sizeScroll.cy - rectView.Height());

		SetBarInfo(m_barV, bBarV, rectView.Height(), m_sizeScroll.cy, m_pointScroll.y);
		SetBarInfo(m_barH, bBarH, rectView.Width(), m_sizeScroll.cx, m_pointScroll.x);
	}

	CDibDC m_ddc;
	CPoint m_pointScroll{0, 0};
	CSize m_sizeScroll{0, 0};

	// One "line" for the wheel and the bar arrows; a grid sets it to a row.
	int m_nScrollLineY = 16;

protected:
	// Called from the derived OnCreate.
	void InitViewBase()
	{
		m_barV.Create(this->m_hWnd, CRect(0, 0, 0, 0), nullptr, WS_CHILD | SBS_VERT, 0, kScrollBarV);
		m_barH.Create(this->m_hWnd, CRect(0, 0, 0, 0), nullptr, WS_CHILD | SBS_HORZ, 0, kScrollBarH);
	}

	// The gutter a derived view always wants, whether or not a bar is showing.
	CSize m_sizeGutterMin{0, 0};
	CSize m_sizeGutter{0, 0};

	int m_cxBar = ::GetSystemMetrics(SM_CXVSCROLL);
	int m_cyBar = ::GetSystemMetrics(SM_CYHSCROLL);

	CScrollBar m_barV;
	CScrollBar m_barH;

	// What a derived view's own gutter controls take, at the far end of each
	// gutter. The bars start at the near end and get the rest.
	int m_nReservedBottom = 0;
	int m_nReservedRight = 0;

	void OnSize(UINT, CSize size)
	{
		// Sized to the whole client, so a gutter appearing does not force a
		// reallocation part way through a layout pass.
		if (size.cx > 0 && size.cy > 0)
			m_ddc.Create(size.cx, size.cy, 24);

		this->SetMsgHandled(FALSE);
	}

	BOOL OnEraseBkgnd(CDCHandle dc)
	{
		CRect rc;
		this->GetClientRect(&rc);

		const CRect rcView = ViewRect();
		const auto brush = reinterpret_cast<HBRUSH>(LongToHandle(COLOR_BTNFACE + 1));

		// The view area repaints itself from the offscreen DIB. The gutters have
		// nothing behind the strip and the hidden bar, so they are filled here.
		if (rcView.right < rc.right)
		{
			CRect rcRight(rcView.right, rc.top, rc.right, rc.bottom);
			dc.FillRect(&rcRight, brush);
		}

		if (rcView.bottom < rc.bottom)
		{
			CRect rcBottom(rc.left, rcView.bottom, rc.right, rc.bottom);
			dc.FillRect(&rcBottom, brush);
		}

		return TRUE;
	}

	void OnPaint(CDCHandle)
	{
		CPaintDC dc(this->m_hWnd);

		CRect r;
		dc.GetClipBox(&r);
		r &= ViewRect();

		if (!r.IsRectEmpty())
			DrawTo(dc.m_hDC, r);
	}

	// PrintWindow, which is how the screenshot harness captures the app.
	LRESULT OnPrintClient(UINT, WPARAM wParam, LPARAM, BOOL&)
	{
		if (wParam != 0)
			DrawTo(reinterpret_cast<HDC>(wParam), ViewRect());

		return 0;
	}

	void OnVScroll(int nSBCode, short nPos, CScrollBar bar)
	{
		if (bar.m_hWnd != m_barV.m_hWnd)
			return;

		const CRect r = ViewRect();
		int n = m_pointScroll.y;

		switch (nSBCode)
		{
		case SB_TOP: n = 0;
			break;
		case SB_BOTTOM: n = m_sizeScroll.cy;
			break;
		case SB_LINEUP: n -= m_nScrollLineY;
			break;
		case SB_LINEDOWN: n += m_nScrollLineY;
			break;
		case SB_PAGEUP: n -= r.Height();
			break;
		case SB_PAGEDOWN: n += r.Height();
			break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION:
			n = MulDiv(nPos, m_sizeScroll.cy, kBarRange);
			break;
		default:
			return;
		}

		ScrollTo(m_pointScroll.x, n);
	}

	void OnHScroll(int nSBCode, short nPos, CScrollBar bar)
	{
		if (bar.m_hWnd != m_barH.m_hWnd)
			return;

		const CRect r = ViewRect();
		int n = m_pointScroll.x;

		switch (nSBCode)
		{
		case SB_LEFT: n = 0;
			break;
		case SB_RIGHT: n = m_sizeScroll.cx;
			break;
		case SB_LINELEFT: n -= 16;
			break;
		case SB_LINERIGHT: n += 16;
			break;
		case SB_PAGELEFT: n -= r.Width();
			break;
		case SB_PAGERIGHT: n += r.Width();
			break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION:
			n = MulDiv(nPos, m_sizeScroll.cx, kBarRange);
			break;
		default:
			return;
		}

		ScrollTo(n, m_pointScroll.y);
	}

	BOOL OnMouseWheel(UINT, short zDelta, CPoint)
	{
		UINT nLines = 3;

		if (!::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &nLines, 0) || nLines == 0)
			nLines = 3;

		const CRect r = ViewRect();
		const int nAmount = (nLines == WHEEL_PAGESCROLL)
			                    ? r.Height()
			                    : static_cast<int>(nLines) * m_nScrollLineY;

		ScrollTo(m_pointScroll.x, m_pointScroll.y - MulDiv(zDelta, nAmount, WHEEL_DELTA));
		return TRUE;
	}

	// Nothing extra by default; a derived view places its own strip here.
	void LayoutControls(const CRect&)
	{
	}

	// SetWindowPos repaints whether or not anything moved, and SetScrollSize
	// runs a whole layout on every scale change - which is every frame of a
	// zoom slider drag. Placing a control that is already there is the flicker.
	static void PlaceControl(CWindow wnd, const CRect& rect)
	{
		if (!wnd.IsWindow())
			return;

		CRect current;
		wnd.GetWindowRect(&current);
		wnd.GetParent().ScreenToClient(&current);

		if (current == rect && wnd.IsWindowVisible())
			return;

		wnd.SetWindowPos(nullptr, rect.left, rect.top, rect.Width(), rect.Height(),
		                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}

	static void HideControl(CWindow wnd)
	{
		if (wnd.IsWindow() && wnd.IsWindowVisible())
			wnd.ShowWindow(SW_HIDE);
	}

private:
	static int Clamp(int n, int nMax)
	{
		nMax = max(nMax, 0);
		return n < 0 ? 0 : (n > nMax ? nMax : n);
	}

	static int Fraction(int n, int nTotal)
	{
		return nTotal > 0 ? MulDiv(n, kBarRange, nTotal) : 0;
	}

	static void SetBarInfo(CScrollBar& bar, bool bShow, int nPage, int nTotal, int nPos)
	{
		if (!bShow || !bar.IsWindow())
			return;

		SCROLLINFO info = {sizeof(SCROLLINFO), SIF_PAGE | SIF_RANGE | SIF_POS};
		info.nMin = 0;
		info.nMax = kBarRange;
		info.nPage = static_cast<UINT>(Fraction(nPage, nTotal));
		info.nPos = Fraction(nPos, nTotal);

		bar.SetScrollInfo(&info, TRUE);
	}

	void LayoutGutters(bool bBarV, bool bBarH)
	{
		CRect rectClient;
		this->GetClientRect(&rectClient);

		static_cast<T*>(this)->LayoutControls(rectClient);

		const int cx = m_sizeGutter.cx;
		const int cy = m_sizeGutter.cy;

		if (bBarV && cx > 0)
			PlaceControl(m_barV, CRect(rectClient.right - cx, 0, rectClient.right,
			                           max(rectClient.Height() - cy - m_nReservedBottom, 0L)));
		else
			HideControl(m_barV);

		if (bBarH && cy > 0)
			PlaceControl(m_barH, CRect(0, rectClient.bottom - cy,
			                           max(rectClient.Width() - cx - m_nReservedRight, 0L),
			                           rectClient.bottom));
		else
			HideControl(m_barH);
	}

	void DrawTo(HDC hDC, const CRect& rect)
	{
		if (m_ddc.m_hDC == nullptr || rect.IsRectEmpty())
			return;

		m_ddc.SetMapping(m_pointScroll, rect);
		m_ddc.Tile(BackgroundTile(), m_pointScroll);

		static_cast<T*>(this)->OnDraw(m_ddc);

		// GDI has to finish with the DIB section before the CPU reads its bits.
		::GdiFlush();

		ImageList_DragShowNolock(FALSE);
		m_ddc.GetDib().Draw(hDC, rect.TopLeft(), rect);
		ImageList_DragShowNolock(TRUE);
	}
};
