// ImageWalker by Zac Walker
//
// Purpose: ImageWalker 2.0's hand-drawn mode tabs.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

class CModeTabs : public CWindowImpl<CModeTabs>
{
public:
	DECLARE_WND_CLASS_EX(_T("IWModeTabs"), CS_HREDRAW | CS_VREDRAW, COLOR_BTNFACE)

	enum { kHeight = 26 };

	BEGIN_MSG_MAP(CModeTabs)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
	END_MSG_MAP()

	void SetMode(int idActive)
	{
		const int nActive = FindCommand(idActive);
		if (_nActive != nActive)
		{
			_nActive = nActive;
			Invalidate();
		}
	}

	void EnableModeFromState(int id, bool bEnable)
	{
		const int nTab = FindCommand(id);
		if (nTab == -1 || _tabs[nTab]._enabled == bEnable)
			return;

		_tabs[nTab]._enabled = bEnable;
		Invalidate();
	}

private:
	struct Tab
	{
		LPCTSTR _caption;
		int _command;
		int _image;
		bool _enabled;
	};

	Tab _tabs[6] =
	{
		{_T("Image"), ID_VIEW_NORMAL, ImageIndex::Normal, true},
		{_T("Description"), ID_VIEW_DESCRIPTION, ImageIndex::Description, true},
		{_T("Folders"), ID_VIEW_FOLDERS, ImageIndex::Folders, true},
		{_T("Search"), ID_VIEW_SEARCHADVANCED, ImageIndex::SearchAdvanced, true},
		{_T("Edit"), ID_VIEW_EDIT, ImageIndex::FilterColor, true},
		{_T("Print"), ID_VIEW_PRINT, ImageIndex::PrintPreview, true}
	};

	CBitmap _highlight;
	CBrush _highlightBrush;
	int _nActive = 0;
	int _nHover = -1;
	int _nPressed = -1;

	int FindCommand(int id) const
	{
		for (int i = 0; i < countof(_tabs); i++)
		{
			if (_tabs[i]._command == id)
				return i;
		}

		return -1;
	}

	int TabFromPoint(POINT point) const
	{
		CRect rectClient;
		GetClientRect(rectClient);

		if (!rectClient.PtInRect(point))
			return -1;

		const int cx = rectClient.Width() / countof(_tabs);
		return IW::Min(countof(_tabs) - 1, point.x / IW::Max(1, cx));
	}

	CRect TabRect(int nTab) const
	{
		CRect rectClient;
		GetClientRect(rectClient);

		const int cx = rectClient.Width() / countof(_tabs);
		return CRect(nTab * cx, 0,
		             nTab == countof(_tabs) - 1 ? rectClient.right : (nTab + 1) * cx,
		             rectClient.bottom);
	}

	void DrawTab(CDCHandle dc, int nTab)
	{
		CRect rect = TabRect(nTab);
		const bool bActive = nTab == _nActive;
		const bool bHover = nTab == _nHover && nTab != _nActive;

		if (bActive)
		{
			dc.DrawEdge(rect, EDGE_ETCHED, BF_TOP | BF_MIDDLE);
			rect.top++;
			rect.bottom--;

			UINT flags = BF_ADJUST | BF_LEFT | BF_RIGHT;
			if (nTab == 0) flags &= ~BF_LEFT;
			if (nTab == countof(_tabs) - 1) flags &= ~BF_RIGHT;
			dc.DrawEdge(rect, EDGE_RAISED, flags);
			rect.top += 2;
		}
		else if (bHover)
		{
			UINT flags = BF_ADJUST | BF_LEFT | BF_RIGHT;
			if (_nPressed != nTab)
				flags = BF_TOP | BF_BOTTOM | BF_ADJUST |
				        (nTab == 0 ? BF_LEFT : 0) |
				        (nTab == countof(_tabs) - 1 ? BF_RIGHT : 0);
			dc.DrawEdge(rect, _nPressed == nTab ? EDGE_RAISED : EDGE_SUNKEN, flags);

			if (!_highlightBrush.IsNull())
				dc.FillRect(rect, _highlightBrush);
			else
				dc.FillSolidRect(rect, GetSysColor(COLOR_BTNFACE));
		}
		else
		{
			UINT flags = BF_ADJUST | BF_TOP | BF_BOTTOM;
			if (nTab == 0) flags |= BF_LEFT;
			if (nTab == countof(_tabs) - 1) flags |= BF_RIGHT;
			dc.DrawEdge(rect, EDGE_SUNKEN, flags);
			dc.FillSolidRect(rect, GetSysColor(COLOR_APPWORKSPACE));
		}

		rect.DeflateRect(1, 1);
		if (_nPressed == nTab)
			rect.OffsetRect(1, 1);

		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(!_tabs[nTab]._enabled ? GetSysColor(COLOR_GRAYTEXT) :
		                (bActive || bHover ? GetSysColor(COLOR_BTNTEXT) : RGB(255, 255, 255)));

		CSize sizeText;
		dc.GetTextExtent(_tabs[nTab]._caption, -1, &sizeText);

		constexpr int cxIcon = 16;
		constexpr int cxGap = 4;
		const int cxGroup = cxIcon + cxGap + sizeText.cx;
		const int xGroup = rect.left + IW::Max(0, (rect.Width() - cxGroup) / 2);
		const int yIcon = rect.top + IW::Max(0, (rect.Height() - cxIcon) / 2);

		ImageList_Draw(App.GetGlobalBitmap(), _tabs[nTab]._image, dc,
		               xGroup, yIcon, ILD_TRANSPARENT);

		CRect rectText(xGroup + cxIcon + cxGap, rect.top, rect.right - 1, rect.bottom);
		dc.DrawText(_tabs[nTab]._caption, -1, rectText,
		            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL &bHandled)
	{
		bHandled = FALSE;
		_highlight.LoadBitmap(IDB_TAB_HIGHLIGHT);
		if (!_highlight.IsNull())
			_highlightBrush.CreatePatternBrush(_highlight);

		return 0;
	}

	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL &bHandled)
	{
		bHandled = FALSE;
		_highlightBrush.DeleteObject();
		_highlight.DeleteObject();
		return 0;
	}

	LRESULT OnEraseBackground(UINT, WPARAM, LPARAM, BOOL &)
	{
		return 1;
	}

	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL &)
	{
		CPaintDC dc(m_hWnd);
		CDCHandle dcHandle(dc.m_hDC);
		const HFONT hFontOld = dcHandle.SelectFont(IW::Style::GetFont(IW::Style::Font::Standard));
		for (int i = 0; i < countof(_tabs); i++)
			DrawTab(dcHandle, i);
		dcHandle.SelectFont(hFontOld);
		return 0;
	}

	LRESULT OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL &)
	{
		const POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		const int nHover = TabFromPoint(point);

		if (_nHover != nHover)
		{
			_nHover = nHover;
			Invalidate();
		}

		if (_nPressed != -1 || nHover != -1)
			SetCapture();
		else if (GetCapture() == m_hWnd)
			ReleaseCapture();

		return 0;
	}

	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL &bHandled)
	{
		const POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		_nPressed = TabFromPoint(point);

		if (_nPressed == -1 || !_tabs[_nPressed]._enabled)
		{
			_nPressed = -1;
			bHandled = FALSE;
			return 0;
		}

		SetCapture();
		Invalidate();
		return 0;
	}

	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL &bHandled)
	{
		if (_nPressed == -1)
		{
			bHandled = FALSE;
			return 0;
		}

		const POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		const int nPressed = _nPressed;
		_nPressed = -1;
		ReleaseCapture();
		Invalidate();

		if (TabFromPoint(point) == nPressed && _tabs[nPressed]._enabled)
			::PostMessage(GetParent(), WM_COMMAND, MAKEWPARAM(_tabs[nPressed]._command, 0), 0);

		return 0;
	}
};