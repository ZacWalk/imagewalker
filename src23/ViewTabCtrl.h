// ImageWalker by Zac Walker
//
// Purpose: A tab control whose pages are child dialogs, used by the
//          description window. The pages are children of the tab control and
//          only the selected one is visible.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

// The owner must have REFLECT_NOTIFICATIONS() in its message map, or the tab
// control never hears about its own selection changes.
class CDialogTabCtrl : public CWindowImpl<CDialogTabCtrl, CTabCtrl>
{
public:

	DECLARE_WND_SUPERCLASS(NULL, CTabCtrl::GetWndClassName())

	BEGIN_MSG_MAP(CDialogTabCtrl)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		REFLECTED_NOTIFY_CODE_HANDLER(TCN_SELCHANGE, OnSelChange)
	END_MSG_MAP()

	BOOL SubclassWindow(HWND hWnd)
	{
		const BOOL bRet = CWindowImpl<CDialogTabCtrl, CTabCtrl>::SubclassWindow(hWnd);

		// The dialog manager will not tab into the pages without this.
		if (bRet)
			ModifyStyleEx(0, WS_EX_CONTROLPARENT);

		return bRet;
	}

	BOOL InsertItem(int nItem, LPTCITEM pItem, HWND hWndPage)
	{
		ATLASSERT(::IsWindow(hWndPage));

		if (nItem < 0 || nItem > static_cast<int>(_pages.size()))
			return FALSE;

		::SetParent(hWndPage, m_hWnd);
		::ShowWindow(hWndPage, SW_HIDE);

		CWindow page(hWndPage);
		page.ModifyStyleEx(0, WS_EX_CONTROLPARENT);
		::EnableThemeDialogTexture(hWndPage, ETDT_ENABLETAB);

		_pages.insert(_pages.begin() + nItem, hWndPage);

		return CTabCtrl::InsertItem(nItem, pItem) != -1;
	}

	int SetCurSel(int nItem)
	{
		if (_pages.empty())
			return -1;

		if (nItem < 0)
			nItem = 0;

		if (nItem >= static_cast<int>(_pages.size()))
			nItem = static_cast<int>(_pages.size()) - 1;

		const int nOld = CTabCtrl::SetCurSel(nItem);
		ShowPage(nItem);
		return nOld;
	}

private:

	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL& bHandled)
	{
		bHandled = FALSE;
		ShowPage(CTabCtrl::GetCurSel());
		return 0;
	}

	LRESULT OnSelChange(int, LPNMHDR, BOOL& bHandled)
	{
		bHandled = FALSE;
		ShowPage(CTabCtrl::GetCurSel());
		return 0;
	}

	void ShowPage(int nItem)
	{
		CRect rect;
		GetClientRect(rect);
		AdjustRect(FALSE, rect);

		for (int i = 0; i < static_cast<int>(_pages.size()); i++)
		{
			if (i == nItem)
			{
				::SetWindowPos(_pages[i], HWND_TOP, rect.left, rect.top, rect.Width(), rect.Height(),
					SWP_NOACTIVATE | SWP_SHOWWINDOW);
			}
			else
			{
				::ShowWindow(_pages[i], SW_HIDE);
			}
		}
	}

	std::vector<HWND> _pages;
};
