// ImageWalker by Zac Walker
//
// Purpose: The splitter window holding the side pane and the view. WTL's
//          splitter, drawn as a flat hot-tracked bar and positioned on the
//          0..10000 scale the ini stores.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com


#ifndef __IW_SPLITTER_H__
#define __IW_SPLITTER_H__

#pragma once

#ifndef __ATLAPP_H__
	#error ViewSplitter.h requires atlapp.h to be included first
#endif

#ifndef __ATLWIN_H__
	#error ViewSplitter.h requires atlwin.h to be included first
#endif

#include <atlsplit.h>


/////////////////////////////////////////////////////////////////////////////
// Classes in this file
//
// CSplitter2Impl<T>
// CSplitter2WindowImpl<T, TBase, TWinTraits>
// CSplitter2Window


namespace WTL
{

/////////////////////////////////////////////////////////////////////////////
// CSplitter2Impl - WTL's splitter with the skin's bar and the ini's scale

template <class T>
class CSplitter2Impl : public CSplitterImpl<T>
{
public:
	// The scale the ini stores splitter positions on. CSplitterImpl keeps the
	// proportional position on its own much larger scale; these convert.
	enum { m_nPropMax = 10000 };

	bool _bHot = false;

// Attributes
	int GetProportionalPos() const
	{
		const int cxyTotal = m_bVertical
			? (m_rcSplitter.right - m_rcSplitter.left - m_cxySplitBar - m_cxyBarEdge)
			: (m_rcSplitter.bottom - m_rcSplitter.top - m_cxySplitBar - m_cxyBarEdge);

		const int nPos = ((cxyTotal > 0) && (m_xySplitterPos >= 0))
			? ::MulDiv(m_xySplitterPos, m_nPropMax, cxyTotal)
			: ::MulDiv(m_nProportionalPos, m_nPropMax, CSplitterImpl<T>::m_nPropMax);

		return (nPos != 0) ? nPos : (m_nPropMax / 2);
	}

	void SetProportionalPos(int nPos)
	{
		m_nProportionalPos = ::MulDiv(nPos, CSplitterImpl<T>::m_nPropMax, m_nPropMax);
		UpdateProportionalPos();

		T* pT = static_cast<T*>(this);
		if(pT->m_hWnd != NULL)
			UpdateSplitterLayout();
	}

	// Unlike SetOrientation, this keeps the proportional position rather than
	// rescaling the pixel one: full screen turns the same splitter on its side.
	void SetAspectAspect(bool bVertical)
	{
		if(m_bVertical == bVertical)
			return;

		StoreProportionalPos();
		m_bVertical = bVertical;
		m_hCursor = ::LoadCursor(NULL, m_bVertical ? IDC_SIZEWE : IDC_SIZENS);

		T* pT = static_cast<T*>(this);
		pT->GetSystemSettings(false);

		UpdateProportionalPos();
		UpdateSplitterLayout();
	}

// Overrideables
	// A flat bar drawn edge to edge, and no minimum pane size: the film strip
	// and the side pane are both sized by dragging a pane shut.
	void GetSystemSettings(bool bUpdate)
	{
		CSplitterImpl<T>::GetSystemSettings(false);

		m_cxySplitBar = ::GetSystemMetrics(m_bVertical ? SM_CXSIZEFRAME : SM_CYSIZEFRAME);
		m_cxyBarEdge = 0;
		m_cxyMin = 0;

		if(bUpdate)
			UpdateSplitterLayout();
	}

	void DrawSplitterBar(CDCHandle dc)
	{
		RECT rect = {};
		if(GetSplitterBarRect(&rect))
		{
			T* pT = static_cast<T*>(this);

			// System face, not a darkened window colour: the panes carry their own
			// client edges in 2.2, so the bar between them is ordinary background.
			if(_bHot || (::GetCapture() == pT->m_hWnd))
			{
				dc.FillSolidRect(&rect, IW::Style::Color::Highlight);
			}
			else
			{
				dc.FillSolidRect(&rect, IW::Style::Color::Face);
			}
		}
	}

	// called only if pane is empty
	void DrawSplitterPane(CDCHandle dc, int nPane)
	{
		RECT rect = {};
		if(GetSplitterPaneRect(nPane, &rect))
			dc.FillRect(&rect, COLOR_APPWORKSPACE);
	}

// Message map and handlers
	BEGIN_MSG_MAP(CSplitter2Impl)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMoveHotTrack)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeaveHotTrack)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChangedHotTrack)
		CHAIN_MSG_MAP(CSplitterImpl<T>)
	END_MSG_MAP()

	LRESULT OnMouseMoveHotTrack(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		T* pT = static_cast<T*>(this);

		// While the bar has capture it is being dragged, and it stays hot.
		if(IsInteractive() && (::GetCapture() != pT->m_hWnd))
		{
			const bool bOver = IsOverSplitterBar(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

			if(bOver && !_bHot)
				TrackLeave();

			SetHot(bOver);
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnMouseLeaveHotTrack(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		SetHot(false);

		bHandled = FALSE;
		return 0;
	}

	// The drag swallowed the leave, so ask for another one: if the pointer is
	// already outside, this posts WM_MOUSELEAVE straight back.
	LRESULT OnCaptureChangedHotTrack(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(_bHot)
			TrackLeave();

		bHandled = FALSE;
		return 0;
	}

// Implementation - internal helpers
	void SetHot(bool bHot)
	{
		if(_bHot == bHot)
			return;

		_bHot = bHot;

		RECT rect = {};
		if(GetSplitterBarRect(&rect))
			static_cast<T*>(this)->InvalidateRect(&rect);
	}

	void TrackLeave()
	{
		T* pT = static_cast<T*>(this);

		TRACKMOUSEEVENT tme = {};
		tme.cbSize = sizeof(TRACKMOUSEEVENT);
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = pT->m_hWnd;
		::TrackMouseEvent(&tme);
	}
};


/////////////////////////////////////////////////////////////////////////////
// CSplitter2WindowImpl - Implements a splitter window

template <class T, class TBase = ATL::CWindow, class TWinTraits = ATL::CControlWinTraits>
class ATL_NO_VTABLE CSplitter2WindowImpl : public ATL::CWindowImpl< T, TBase, TWinTraits >, public CSplitter2Impl< T >
{
public:
	DECLARE_WND_CLASS_EX(NULL, CS_DBLCLKS, COLOR_WINDOW)

	typedef CSplitter2WindowImpl< T , TBase, TWinTraits >	thisClass;
	typedef CSplitter2Impl< T >				baseClass;
	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		CHAIN_MSG_MAP(baseClass)
		FORWARD_NOTIFICATIONS()
	END_MSG_MAP()

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		// handled, no background painting needed
		return 1;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(wParam != SIZE_MINIMIZED)
			SetSplitterRect();

		bHandled = FALSE;
		return 1;
	}
};


/////////////////////////////////////////////////////////////////////////////
// CSplitter2Window - Implements a splitter window to be used as is

class CSplitter2Window : public CSplitter2WindowImpl<CSplitter2Window>
{
public:
	DECLARE_WND_CLASS_EX(_T("IWSplitterWindow"), CS_DBLCLKS, COLOR_WINDOW)
};


}; //namespace WTL

#endif // __IW_SPLITTER_H__
