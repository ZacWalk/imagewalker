// ImageWalker by Zac Walker
//
// Purpose: The tooltip window the panes show item details in.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once


template<class T>
class ToolTipWindowImpl
{
public:

	CToolTipCtrl _toolTip;

	// The notification only borrows the text, and szText is 80 chars - far short
	// of the dozen metadata lines an item tooltip carries. Hold it here instead.
	CStringA _strToolTipA;
	CStringW _strToolTipW;

	ToolTipWindowImpl()
	{		
	}

	void CreateTooltip()
	{
		T *pT = static_cast<T*>(this);

		// Tool tips
		if (_toolTip.Create(pT->m_hWnd, NULL, NULL, 0))
		{
			TOOLINFO ti = { 0 };

			/* INITIALIZE MEMBERS OF THE TOOLINFO STRUCTURE */
			ti.cbSize = sizeof(TOOLINFO);
			ti.uFlags = TTF_IDISHWND;
			ti.hwnd = pT->m_hWnd;
			ti.hinst = App.GetResourceInstance();
			ti.uId = reinterpret_cast<UINT_PTR>(pT->m_hWnd);
			ti.lpszText = LPSTR_TEXTCALLBACK ;
			// Tooltip control will cover the whole window
			ti.rect.left =     
				ti.rect.top = 
				ti.rect.right = 
				ti.rect.bottom = 0;

			_toolTip.AddTool(&ti);

			_toolTip.SetMaxTipWidth(400);
			_toolTip.SetDelayTime(TTDT_AUTOPOP, SHRT_MAX);
			_toolTip.SetDelayTime(TTDT_INITIAL, 2000);
			_toolTip.SetDelayTime(TTDT_RESHOW, 0);
		}
	}

	void ActivateTooltip(bool b)
	{
		_toolTip.Activate(b);
	}

	BEGIN_MSG_MAP(ToolTipWindowImpl<T>)

		MESSAGE_HANDLER(WM_CREATE, OnCreate)

		MESSAGE_RANGE_HANDLER(WM_MOUSEFIRST, WM_MOUSELAST, OnMouseMessage)		

		NOTIFY_CODE_HANDLER(TTN_GETDISPINFOA, OnToolTipTextA)
		NOTIFY_CODE_HANDLER(TTN_GETDISPINFOW, OnToolTipTextW)

	END_MSG_MAP()



	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		CreateTooltip();
		bHandled = false;
		return 0;
	}

	// Relay tooltip
	LRESULT OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		T *pT = static_cast<T*>(this);
		MSG msg = { pT->m_hWnd, uMsg, wParam, lParam };

		if (App.Settings.m_bShowToolTips && _toolTip.m_hWnd)
			_toolTip.RelayEvent(&msg);

		bHandled = FALSE;
		return 1;
	}	

	CString GetToolTipText(UINT_PTR idCtrl)
	{
		CString str = App.LoadString(static_cast<UINT>(idCtrl));

		int n = str.Find(_T('\n'));
		if (n != -1) str.Delete(0, n + 1);
		return str;
	}


	LRESULT OnToolTipTextA(int idCtrl, LPNMHDR pnmh, BOOL& /*bHandled*/)
	{
		LPNMTTDISPINFOA pDispInfo = (LPNMTTDISPINFOA)pnmh;
		pDispInfo->szText[0] = 0;
		pDispInfo->lpszText = 0;
		pDispInfo->hinst = NULL;

		if (idCtrl != 0)
		{
			T *pT = static_cast<T*>(this);

			try
			{
				_strToolTipA = pT->GetToolTipText(pnmh->idFrom);

				if (!_strToolTipA.IsEmpty())
				{
					pDispInfo->lpszText = const_cast<LPSTR>(_strToolTipA.GetString());
				}
			}
			catch(const std::exception &)
			{
			}
		}

		return 0;
	}

	LRESULT OnToolTipTextW(int idCtrl, LPNMHDR pnmh, BOOL& /*bHandled*/)
	{
		LPNMTTDISPINFOW pDispInfo = (LPNMTTDISPINFOW)pnmh;
		pDispInfo->szText[0] = 0;
		pDispInfo->lpszText = 0;
		pDispInfo->hinst = NULL;

		if(idCtrl != 0)
		{
			T *pT = static_cast<T*>(this);

			try
			{
				_strToolTipW = pT->GetToolTipText(pnmh->idFrom);

				if (!_strToolTipW.IsEmpty())
				{
					pDispInfo->lpszText = const_cast<LPWSTR>(_strToolTipW.GetString());
				}
			}
			catch(const std::exception &)
			{
			}
		}

		return 0;
	}
};
