// ImageWalker by Zac Walker
//
// Purpose: ViewBase, CommandFrameBase and the skinned command bar: how a
//          command reaches a view and how its enabled/checked state is asked
//          for.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

// UpdateUI.h: interface for the CommandFrameBase class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ViewShellMenu.h"
#include "ViewToolTip.h"

#define FILE_MENU_POSITION	0
#define COPYTO_MENU_POSITION	11
#define MOVETO_MENU_POSITION	10

struct CommandInfo
{
	int _nImage;
	int _id;
	int _wType;
	BYTE _nToolbarStyle;
	int _dwStringId;
};

extern CommandInfo s_commands[];
extern int s_toolbarMain[];
extern int s_toolbarEdit[];
extern int s_toolbarPrint[];

typedef std::map<int,int> MAPIDTOIMAGE;


class ViewBase : public IW::Referenced
{
public:
	virtual ~ViewBase() {};	

	virtual HWND Activate(HWND hWndParent) = 0;
	virtual void Deactivate() = 0;
	virtual bool CanEditImages() const = 0;	
	virtual void OnTimer() = 0;
	virtual BOOL PreTranslateMessage(MSG* pMsg) { return FALSE; };
	virtual bool CanShowToolbar(DWORD id) = 0;
	virtual void OnOptionsChanged() = 0;
	virtual void OnNewImage(bool bScrollToCenter) {};
	virtual HWND GetImageWindow() = 0;
	virtual void OnAfterCopy(bool bMove) {};
	virtual CString GetSelectedFileList() const { return _T("\n\n"); };

	// One rung of the frame's Escape ladder. Return true having undone
	// something the view owns; false lets the frame take the next rung, which
	// ends in leaving the mode.
	virtual bool OnEscape() { return false; };

	// Asked before the frame leaves this mode or closes. Returning false
	// cancels the departure.
	virtual bool QueryLeave() { return true; };

	// Return false for an id this view does not own. The frame is asked next,
	// and an id neither of them claims is greyed out.
	virtual bool InvokeCommand(DWORD id) { return false; };
	virtual bool GetCommandState(DWORD id, bool &bEnabled, bool &bChecked) { return false; };
};

class CFlatMenuWindow : 
	public CWindowImpl<CFlatMenuWindow>
{
public:
	DECLARE_WND_SUPERCLASS(_T("WTL_XpMenu"), GetWndClassName())

	BEGIN_MSG_MAP(CFlatMenuWindow)
		MESSAGE_HANDLER(WM_NCPAINT, OnNcPaint)
		MESSAGE_HANDLER(WM_PRINT, OnPrint)
	END_MSG_MAP()

	COLORREF m_clrBorder;
	COLORREF m_clrMenu;

	SIZE m_sizeBorder;
	int m_cxMenuButton;

	CFlatMenuWindow(int cxMenuButton, COLORREF clrBorder, COLORREF clrMenu)
		: m_cxMenuButton(cxMenuButton), 
		m_clrBorder(clrBorder), 
		m_clrMenu(clrMenu)
	{
		m_sizeBorder.cx = ::GetSystemMetrics(SM_CXDLGFRAME);
		m_sizeBorder.cy = ::GetSystemMetrics(SM_CYDLGFRAME);
	}

	virtual void OnFinalMessage(HWND /*hWnd*/)
	{
		delete this;
	}

	LRESULT OnNcPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		CDC dc = ::GetDCEx(m_hWnd, (HRGN) wParam, DCX_WINDOW | DCX_INTERSECTRGN | 0x10000);
		if( dc.IsNull() ) dc = ::GetWindowDC(m_hWnd);

		CRect rcWin;
		GetWindowRect(&rcWin);
		::OffsetRect(&rcWin, -rcWin.left, -rcWin.top);
		DrawMenu((HDC)dc, rcWin);

		return 1;
	}

	void DrawMenu(CDCHandle dc, CRect rcWin)
	{
		dc.FillSolidRect(rcWin, m_clrBorder);
		rcWin.DeflateRect(m_sizeBorder);
		dc.FillSolidRect(rcWin, m_clrMenu);
	}

	LRESULT OnPrint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CDCHandle dc = (HDC) wParam;
		CRect rcWin;
		GetWindowRect(&rcWin);
		::OffsetRect(&rcWin, -rcWin.left, -rcWin.top);

		// Do the same as in OnNcPaint, but draw to provided DC.
		// Should there be a common method?
		if( (lParam & PRF_NONCLIENT) != 0 )
		{
			DrawMenu(dc, rcWin);
		}

		// Get the system to draw all the items to a memory DC and then whack it
		// on top of the background we just drew above
		if( (lParam & PRF_CLIENT) != 0 )
		{
			RECT rcClient;
			GetClientRect(&rcClient);
			int cxClient = rcClient.right - rcClient.left;
			int cyClient = rcClient.bottom - rcClient.top;
			int offsetX = (rcWin.right - rcWin.left - cxClient) / 2;
			int offsetY = (rcWin.bottom - rcWin.top - cyClient) / 2;
			CDC memDC;
			CBitmap memBM;
			memDC.CreateCompatibleDC(dc);
			memBM.CreateCompatibleBitmap(dc, cxClient, cyClient);
			HBITMAP hOldBmp = memDC.SelectBitmap(memBM);
			DefWindowProc(WM_PRINTCLIENT, (WPARAM) memDC.m_hDC, PRF_CLIENT);
			dc.BitBlt(offsetX, offsetY, cxClient, cyClient, memDC, 0, 0, SRCCOPY);
			memDC.SelectBitmap(hOldBmp);
		}

		bHandled = TRUE;
		return 0;
	}
};

class ImageWalkerCommandBarCtrl : public CCommandBarCtrlImpl<ImageWalkerCommandBarCtrl>
{
	typedef CCommandBarCtrlImpl<ImageWalkerCommandBarCtrl> BaseClass;
public:
	DECLARE_WND_SUPERCLASS(_T("CommandBar"), GetWndClassName())
	MAPIDTOIMAGE m_mapCommand;
	static CRect m_rcButton;

	ImageWalkerCommandBarCtrl()
	{		
		SetImageSize(20, 20);	// default
		m_hImageList = App.GetGlobalBitmap();
	}

	~ImageWalkerCommandBarCtrl()
	{		
		// We dont want this deleted
		m_hImageList = 0;
	}

	void UpdateCommands()
	{
		m_arrCommand.RemoveAll();

		// find max object
		for(MAPIDTOIMAGE::iterator it = m_mapCommand.begin(); it != m_mapCommand.end(); ++it)
		{
			int nImage = it->second;
			int nId = it->first;
			if (nImage < 0 || nId <= 0 || nId > 0xffff)
				continue;

			while(m_arrCommand.GetSize() <= nImage)
			{
				m_arrCommand.Add(0);
			}

			// Check for duplicates
			//ATLASSERT(m_arrCommand[nImage] == 0);
			m_arrCommand[nImage] = static_cast<WORD>(nId);
		}

		#if _WTL_CMDBAR_VISTA_MENUS
		if(RunTimeHelper::IsVista())
		{
			_AddVistaBitmapsFromImageList(0, m_arrCommand.GetSize());
			ATLASSERT(m_arrCommand.GetSize() == m_arrVistaBitmap.GetSize());
		}
#endif // _WTL_CMDBAR_VISTA_MENUS
	}		

	BOOL DoTrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, LPTPMPARAMS lpParams = NULL)
	{
		// Figure out the size of the pressed button
		if( !m_bContextMenu ) GetItemRect(m_nPopBtn, &m_rcButton);

		CMenuHandle menuPopup = hMenu;

		BOOL bTrackRet = FALSE;

		{
			// TrackPopupMenuEx runs a nested modal loop, so anything it dispatches
			// can throw straight through this
			IW::CAutoLockRawCS lock(_Module.m_csWindowCreate);

			ATLASSERT(s_hCreateHook == NULL);

			s_pCurrentBar = static_cast<ImageWalkerCommandBarCtrl*>(this);

			s_hCreateHook = ::SetWindowsHookEx(WH_CBT, MyCreateHookProc, _Module.GetModuleInstance(), GetCurrentThreadId());
			ATLASSERT(s_hCreateHook != NULL);

			m_bPopupItem = false;
			m_bMenuActive = true;

			bTrackRet = menuPopup.TrackPopupMenuEx(uFlags, x, y, m_hWnd, lpParams);
			m_bMenuActive = false;

			::UnhookWindowsHookEx(s_hCreateHook);

			s_hCreateHook = NULL;
			s_pCurrentBar = NULL;
		}

		// Cleanup - convert menus back to original state
		ATLASSERT(m_stackMenuWnd.GetSize() == 0);

		// These updates are also from the original WTL CommandBar control
		// but they actually solves some paint problems with submenus in this
		// control as well.
		UpdateWindow();
		IW::GetMainWindow().UpdateWindow();

		// Restore the menu items to the previous state for all menus that were converted
		if( m_bImagesVisible )
		{
			HMENU hMenuSav;
			while( (hMenuSav = m_stackMenuHandle.Pop()) != NULL ) {
				menuPopup = hMenuSav;
				BOOL bRet = FALSE;
				// Restore state and delete menu item data
				for( int i = 0; i < menuPopup.GetMenuItemCount(); i++ ) {
					CMenuItemInfo mii;
					mii.fMask = MIIM_DATA | MIIM_TYPE | MIIM_ID;
					bRet = menuPopup.GetMenuItemInfo(i, TRUE, &mii);
					ATLASSERT(bRet);

					_MenuItemData* pMI = (_MenuItemData*)mii.dwItemData;
					if( pMI != NULL && pMI->IsCmdBarMenuItem() )
					{
						mii.fMask = MIIM_DATA | MIIM_TYPE | MIIM_STATE;
						mii.fType = pMI->fType;
						mii.fState = pMI->fState;
						mii.dwTypeData = pMI->lpstrText;
						mii.cch = lstrlen(pMI->lpstrText);
						mii.dwItemData = NULL;

						bRet = menuPopup.SetMenuItemInfo(i, TRUE, &mii);
						// This one triggers WM_MEASUREITEM
						menuPopup.ModifyMenu(i, MF_BYPOSITION | mii.fType | mii.fState, mii.wID, pMI->lpstrText);
						ATLASSERT(bRet);

						delete [] pMI->lpstrText;
						delete pMI;
					}
				}
			}
		}
		return bTrackRet;
	}

	// Implementation - Hook procs

	static LRESULT CALLBACK MyCreateHookProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		LRESULT lRet = 0;
		TCHAR szClassName[7];

		if( nCode == HCBT_CREATEWND )
		{
			HWND hWndMenu = (HWND)wParam;
#ifdef _CMDBAR_EXTRA_TRACE
			ATLTRACE2(atlTraceUI, 0, "CmdBar - HCBT_CREATEWND (HWND = %8.8X)\n", hWndMenu);
#endif
			::GetClassName(hWndMenu, szClassName, 7);
			if( ::lstrcmp(_T("#32768"), szClassName) == 0 ) 
			{
				s_pCurrentBar->m_stackMenuWnd.Push(hWndMenu);

				// Subclass to a flat-looking menu
				CFlatMenuWindow* wnd = new CFlatMenuWindow(m_rcButton.right - m_rcButton.left, IW::Emphasize(IW::Style::Color::MenuBackground), IW::Style::Color::MenuBackground);
				wnd->SubclassWindow(hWndMenu);
				wnd->SetWindowPos(HWND_TOP, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_DRAWFRAME);
				::SetRectEmpty(&m_rcButton);
			}
		}
		else if( nCode == HCBT_DESTROYWND )
		{
			HWND hWndMenu = (HWND)wParam;
#ifdef _CMDBAR_EXTRA_TRACE
			ATLTRACE2(atlTraceUI, 0, "CmdBar - HCBT_DESTROYWND (HWND = %8.8X)\n", hWndMenu);
#endif
			::GetClassName(hWndMenu, szClassName, 7);
			if( ::lstrcmp(_T("#32768"), szClassName) == 0 )
			{
				ATLASSERT(hWndMenu == s_pCurrentBar->m_stackMenuWnd.GetCurrent());
				s_pCurrentBar->m_stackMenuWnd.Pop();
			}
		}
		else if( nCode < 0 )
		{
			lRet = ::CallNextHookEx(s_hCreateHook, nCode, wParam, lParam);
		}
		return lRet;
	}

	BEGIN_MSG_MAP(ImageWalkerCommandBarCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		//MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_INITMENU, OnInitMenu)
		MESSAGE_HANDLER(WM_INITMENUPOPUP, OnInitMenuPopup)
		MESSAGE_HANDLER(WM_MENUSELECT, OnMenuSelect)
		MESSAGE_HANDLER(GetAutoPopupMessage(), OnInternalAutoPopup)
		MESSAGE_HANDLER(GetGetBarMessage(), OnInternalGetBar)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_MENUCHAR, OnMenuChar)

		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_KEYUP, OnKeyUp)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
		MESSAGE_HANDLER(WM_SYSKEYDOWN, OnSysKeyDown)
		MESSAGE_HANDLER(WM_SYSKEYUP, OnSysKeyUp)
		MESSAGE_HANDLER(WM_SYSCHAR, OnSysChar)
		// public API handlers - these stay to support chevrons in atlframe.h
		MESSAGE_HANDLER(CBRM_GETMENU, OnAPIGetMenu)
		MESSAGE_HANDLER(CBRM_TRACKPOPUPMENU, OnAPITrackPopupMenu)
		MESSAGE_HANDLER(CBRM_GETCMDBAR, OnAPIGetCmdBar)

		MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
		MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)

		MESSAGE_HANDLER(WM_FORWARDMSG, OnForwardMsg)
		ALT_MSG_MAP(1)		// Parent window messages
		NOTIFY_CODE_HANDLER(TBN_HOTITEMCHANGE, OnParentHotItemChange)
		NOTIFY_CODE_HANDLER(TBN_DROPDOWN, OnParentDropDown)
		MESSAGE_HANDLER(WM_INITMENUPOPUP, OnParentInitMenuPopup)
		MESSAGE_HANDLER(GetGetBarMessage(), OnParentInternalGetBar)
		MESSAGE_HANDLER(WM_SYSCOMMAND, OnParentSysCommand)
		MESSAGE_HANDLER(CBRM_GETMENU, OnParentAPIGetMenu)
		MESSAGE_HANDLER(WM_MENUCHAR, OnParentMenuChar)
		MESSAGE_HANDLER(CBRM_TRACKPOPUPMENU, OnParentAPITrackPopupMenu)
		MESSAGE_HANDLER(CBRM_GETCMDBAR, OnParentAPIGetCmdBar)

		MESSAGE_HANDLER(WM_DRAWITEM, OnParentDrawItem)
		MESSAGE_HANDLER(WM_MEASUREITEM, OnParentMeasureItem)

		MESSAGE_HANDLER(WM_ACTIVATE, OnParentActivate)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnParentCustomDraw)
		ALT_MSG_MAP(2)		// MDI client window messages
		// Use CMDICommandBarCtrl for MDI support
		ALT_MSG_MAP(3)		// Message hook messages
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnHookMouseMove)
		MESSAGE_HANDLER(WM_SYSKEYDOWN, OnHookSysKeyDown)
		MESSAGE_HANDLER(WM_SYSKEYUP, OnHookSysKeyUp)
		MESSAGE_HANDLER(WM_SYSCHAR, OnHookSysChar)
		MESSAGE_HANDLER(WM_KEYDOWN, OnHookKeyDown)
		MESSAGE_HANDLER(WM_NEXTMENU, OnHookNextMenu)
		MESSAGE_HANDLER(WM_CHAR, OnHookChar)
	END_MSG_MAP()

	LRESULT OnHookKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		bHandled = FALSE;

		if(wParam == VK_ESCAPE)
		{
			if(m_bMenuActive && !m_bContextMenu)
			{
				int nHot = GetHotItem();
				if(nHot == -1)
					nHot = m_nPopBtn;
				if(nHot == -1)
					nHot = 0;
				SetHotItem(nHot);
				bHandled = TRUE;
				TakeFocus();
				m_bEscapePressed = true; // To keep focus
			}
			else if(::GetFocus() == m_hWnd && m_wndParent.IsWindow())
			{
				SetHotItem(-1);
				GiveFocusBack();
				bHandled = TRUE;
			}
		}
		else if(wParam == VK_RETURN || wParam == VK_UP || wParam == VK_DOWN)
		{
			if(!m_bMenuActive && ::GetFocus() == m_hWnd && m_wndParent.IsWindow())
			{
				int nHot = GetHotItem();
				if(nHot != -1)
				{
					if(wParam != VK_RETURN)
					{
						// IE4 only: WM_KEYDOWN doesn't generate TBN_DROPDOWN, we need to simulate a mouse click
#if (_WIN32_IE < 0x0500)
						DWORD dwMajor = 0, dwMinor = 0;
						AtlGetCommCtrlVersion(&dwMajor, &dwMinor);
						if(dwMajor <= 4 || (dwMajor == 5 && dwMinor < 80))
						{
							CRect rect;
							GetItemRect(nHot, &rect);
							PostMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(rect.left, rect.top));
						}
#endif //(_WIN32_IE < 0x0500)
						//PostMessage(WM_KEYDOWN, VK_DOWN, 0L);
						bHandled = TRUE;
					}
				}				
			}
			if(wParam == VK_RETURN && m_bMenuActive)
			{
				PostMessage(TB_SETHOTITEM, (WPARAM)-1, 0L);
				m_nNextPopBtn = -1;
				GiveFocusBack();
			}
		}
		else if(wParam == VK_LEFT || wParam == VK_RIGHT)
		{
#if (WINVER >= 0x0500)
			bool bRTL = ((GetExStyle() & WS_EX_LAYOUTRTL) != 0);
			WPARAM wpNext = bRTL ? VK_LEFT : VK_RIGHT;
			WPARAM wpPrev = bRTL ? VK_RIGHT : VK_LEFT;
#else // !(WINVER >= 0x0500)
			WPARAM wpNext = VK_RIGHT;
			WPARAM wpPrev = VK_LEFT;
#endif // !(WINVER >= 0x0500)

			if(m_bMenuActive && !m_bContextMenu && !(wParam == wpNext && m_bPopupItem))
			{
				bool bAction = false;
				if(wParam == wpPrev && s_pCurrentBar->m_stackMenuWnd.GetSize() == 1)
				{
					m_nNextPopBtn = GetPreviousMenuItem(m_nPopBtn);
					if(m_nNextPopBtn != -1)
						bAction = true;
				}
				else if(wParam == wpNext)
				{
					m_nNextPopBtn = GetNextMenuItem(m_nPopBtn);
					if(m_nNextPopBtn != -1)
						bAction = true;
				}
				HWND hWndMenu = m_stackMenuWnd.GetCurrent();
				ATLASSERT(hWndMenu != NULL);

				// Close the popup menu
				if(bAction)
				{
					::PostMessage(hWndMenu, WM_KEYDOWN, VK_ESCAPE, 0L);
					if(wParam == wpNext)
					{
						int cItem = m_stackMenuWnd.GetSize() - 1;
						while(cItem >= 0)
						{
							hWndMenu = m_stackMenuWnd[cItem];
							if(hWndMenu != NULL)
								::PostMessage(hWndMenu, WM_KEYDOWN, VK_ESCAPE, 0L);
							cItem--;
						}
					}
#if (_WIN32_IE >= 0x0500)
					if(m_nNextPopBtn == -2)
					{
						m_nNextPopBtn = -1;
						DisplayChevronMenu();
					}
#endif //(_WIN32_IE >= 0x0500)
					bHandled = TRUE;
				}
			}
		}
		return 0;
	}

	LRESULT OnParentMeasureItem(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		// Skip it if its for the status bar
		if (wParam != 0)
		{
			bHandled = false;
			return 0;
		}

		return OnMeasureItem(uMsg, wParam, lParam, bHandled);
	}

	LRESULT OnParentDrawItem(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		// Skip it if its for the status bar
		if (wParam != 0)
		{
			bHandled = false;
			return 0;
		}

		return OnDrawItem(uMsg, wParam, lParam, bHandled);
	}

	LRESULT OnDrawItem(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		LPDRAWITEMSTRUCT lpDrawItemStruct = (LPDRAWITEMSTRUCT)lParam;
		_MenuItemData* pmd = (_MenuItemData*)lpDrawItemStruct->itemData;
		IW::ShellMenuItem* pMI = (IW::ShellMenuItem*)lpDrawItemStruct->itemData;

		if(lpDrawItemStruct->CtlType == ODT_MENU && pmd != NULL && 
			(pmd->IsCmdBarMenuItem() || pMI->IsValid()))
		{
			DrawItem(lpDrawItemStruct);
		}
		else
		{
			bHandled = FALSE;
		}
		return (LRESULT)TRUE;
	}

	LRESULT OnMeasureItem(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		LPMEASUREITEMSTRUCT lpMeasureItemStruct = (LPMEASUREITEMSTRUCT)lParam;
		_MenuItemData* pmd = (_MenuItemData*)lpMeasureItemStruct->itemData;
		IW::ShellMenuItem* pMI = (IW::ShellMenuItem*)lpMeasureItemStruct->itemData;

		if(lpMeasureItemStruct->CtlType == ODT_MENU && pmd != NULL &&
			(pmd->IsCmdBarMenuItem() || pMI->IsValid()))
		{
			MeasureItem(lpMeasureItemStruct);
		}
		else
		{
			bHandled = FALSE;
		}
		return (LRESULT)TRUE;
	}

	void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
	{
		IW::Skin::DrawMenuItem(lpDrawItemStruct);
	}

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(m_bAttachedMenu || (m_dwExtendedStyle & CBR_EX_TRANSPARENT))
		{
			bHandled = FALSE;
			return 0;
		}

		CDCHandle dc = (HDC)wParam;
		RECT rect = { 0 };
		GetClientRect(&rect);
		dc.FillSolidRect(&rect, IW::Style::Color::Window);

		return 1;   // don't do the default erase
	}

	LRESULT OnParentCustomDraw(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled)
	{
		LRESULT lRet = CDRF_DODEFAULT;
		bHandled = FALSE;
		if(pnmh->hwndFrom == m_hWnd)
		{
			LPNMTBCUSTOMDRAW lpTBCustomDraw = (LPNMTBCUSTOMDRAW)pnmh;
			if(lpTBCustomDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
			{
				lRet = CDRF_NOTIFYITEMDRAW;
				bHandled = TRUE;
			}
			else if(lpTBCustomDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
			{
				IW::Skin::DrawButton((LPNMTBCUSTOMDRAW)pnmh, NULL, CSize(16, 16), false);
				lRet = CDRF_SKIPDEFAULT;
				bHandled = TRUE;
			}
		}
		return lRet;
	}
};

template<class T>
class CommandFrameBase : 
	public CUpdateUIBase,
	//public CCustomDraw<T>,
	public ToolTipWindowImpl<T>	
{
public:	

	BEGIN_MSG_MAP(CommandFrameBase)

		MESSAGE_HANDLER(WM_COMMAND, OnCommand)		

		CHAIN_MSG_MAP(ToolTipWindowImpl<T>)
		CHAIN_MSG_MAP(CUpdateUIBase)
		//CHAIN_MSG_MAP(CCustomDraw<T>)

	END_MSG_MAP()

	// UpdateUIMap
	CSimpleArray<_AtlUpdateUIMap> m_arrayUpdateUIMap;

	ImageWalkerCommandBarCtrl m_CmdBar;


	//////////////////////////////////////////////////////////////////////
	// Construction/Destruction
	//////////////////////////////////////////////////////////////////////

	CommandFrameBase()
	{
		m_pUIMap = 0;
	}



	~CommandFrameBase()
	{
		ATLTRACE(_T("Delete CommandFrameBase\n"));

		if(m_pUIMap != NULL && m_pUIData != NULL)
		{
			const _AtlUpdateUIMap* pUIMap = m_pUIMap;
			_AtlUpdateUIData* pUIData = m_pUIData;

			while(pUIMap->m_nID != (WORD)-1)
			{
				if(pUIData->m_wState & UPDUI_TEXT)
				{
					free(pUIData->m_lpData);
					pUIData->m_lpData = 0;
				}

				pUIMap++;
				pUIData++;
			}

			delete [] m_pUIData;
			m_pUIData = 0;
			m_pUIMap = 0;
		}
	}

	HWND CreateToolbar(HWND hwndParent, int nId, int toolbarButtons[])
	{
		typedef std::map<int, CommandInfo*> MAPIDTOINFO;
		MAPIDTOINFO mapCommandInfo;	

		for(int j = 0; s_commands[j]._id != -1; j++)
		{
			mapCommandInfo[s_commands[j]._id] = &s_commands[j];
		}	

		CSimpleArray<TBBUTTON> arrayButtons;
		int nString = 0;
		CString strings;

		// Build up the list
		for(int i = 0; -1 != toolbarButtons[i]; ++i)
		{
			TBBUTTON tbb;
			IW::MemZero(&tbb, sizeof(tbb));
			tbb.iString = -1;

			// A zero id is a separator, and there is no CommandInfo for one --
			// looking it up first left the separator branch unreachable and every
			// toolbar ran together as one undivided row of buttons.
			MAPIDTOINFO::iterator it = mapCommandInfo.find(toolbarButtons[i]);

			if (it == mapCommandInfo.end())
			{
				if (toolbarButtons[i] != 0)
					continue;

				tbb.fsStyle = BTNS_SEP;
			}
			else
			{
				const CommandInfo* pCommandInfo = it->second;

				tbb.iBitmap = pCommandInfo->_nImage;
				tbb.idCommand = pCommandInfo->_id;
				tbb.fsState = TBSTATE_ENABLED;
				tbb.fsStyle = static_cast<BYTE>(pCommandInfo->_nToolbarStyle | BTNS_AUTOSIZE);

				// Check for a string
				if (-1 != pCommandInfo->_dwStringId)
				{
					CString str;
					str.LoadString(pCommandInfo->_dwStringId);
					int nFound = str.Find(_T('\n'));

					if (nFound != -1)
					{
						str.Truncate(nFound);
					}

					strings += str;
					strings += _T('\n');

					tbb.iString = nString;
					nString++;
				}
			}

			arrayButtons.Add(tbb);
		}

		// AddStrings wants one buffer of NUL-separated captions. The separator has
		// to be a character a caption cannot contain: a full stop can, and "Print..."
		// arrived as "Print" plus three empty strings, which shifted the index of
		// every caption after it.
		if (nString > 0) strings += _T('\n');
		HWND hWndToolbar = CreateToolbar(hwndParent, nId, arrayButtons.GetData(), arrayButtons.GetSize(), strings);
		UIAddToolBar(hWndToolbar);
		return hWndToolbar;
	}

	HWND CreateToolbar(HWND hwndParent, int nId, LPTBBUTTON lpButtons, int nCount, CString strings = CString())
	{
		DWORD dwStyle = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | 
			CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN | 
			TBSTYLE_TOOLTIPS | TBSTYLE_LIST | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT;

		/*HWND hwndTB = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, 
		dwStyle, 
		0, 0, 0, 0, hwndParent, 
		(HMENU) nId, App.GetResourceInstance(), NULL); */


		CToolBarCtrl tb;
		tb.Create(hwndParent, NULL, NULL, dwStyle, 0, nId);

		if (!strings.IsEmpty()) 
		{
			strings.Replace(_T('\n'), 0);
			tb.AddStrings(strings); 
		}

		tb.SetExtendedStyle(TBSTYLE_EX_DRAWDDARROWS);
		tb.SetFont(IW::Style::GetFont(IW::Style::Font::Standard));
		tb.SetButtonStructSize();		
		tb.SetImageList(App.GetGlobalBitmap());	
		tb.SetButtonSize(22,22);
		tb.AddButtons(nCount, lpButtons);		
		tb.AutoSize();

		//CSize size, padding;
		//tb.GetButtonSize(size);
		//tb.GetPadding(&padding);
		//if (size.cy > 22) 
		//	tb.SetPadding(padding.cx, padding.cy - ((size.cy - 22) / 2));
		//tb.ModifyStyle(0, TBSTYLE_LIST);
		return tb.Detach();
	}


	//////////////////////////////////////////////////////////////////////////////////////////
	typedef std::map<TCHAR, int> MAPSHORTCUTS;
	typedef CSimpleArray<ACCEL> ACCELLIST;
	typedef std::map<int, ACCELLIST > MAPIDTOACCEL;

	static void LoadAccel(HACCEL hAccel, MAPIDTOACCEL &mapAccel)
	{
		int nAccel;
		if (hAccel && (nAccel = CopyAcceleratorTable(hAccel, NULL, 0)) > 0) 
		{
			IW::CBuffer<ACCEL> pAccel(nAccel);
			CopyAcceleratorTable(hAccel, pAccel, nAccel);

			for (int i=0; i<nAccel; i++) 
			{
				ACCEL& ac = pAccel[i];
				mapAccel[ac.cmd].Add(ac);
			}
		}
	}


	static void ConstructMenuString(LPTSTR szOut, int szOutSize, LPTSTR szIn, ACCELLIST *pal, MAPSHORTCUTS &mapShortcuts)
	{
		bool bInsertedShortcut = false;
		TCHAR *pch = szIn, *pch2 = szOut;

		// Rest shortcut
		while (*pch != '\0') 
		{ 
			if (*pch != '&') 
			{ 
				if (*pch == '\t') 
				{ 
					*pch = '\0'; 
					*pch2 = '\0'; 
				} 
				else 
				{
					TCHAR ch = *pch++;

					if (!bInsertedShortcut && ch != ' ')
					{
						MAPSHORTCUTS::iterator it = mapShortcuts.find(ch);

						if (it == mapShortcuts.end())
						{
							*pch2++ = '&';
							mapShortcuts[ch] = 1;
							bInsertedShortcut = true;
						}
					}

					*pch2++ = ch;
				}
			} 
			else pch++; 
		} 

		*pch2 = 0;

		// Add Accelerator
		if (pal)
		{
			bool bFirst = true;

			for(int i = 0; i < pal->GetSize(); ++i)
			{
				ACCEL *pa = &((*pal)[i]);

				if (pa)
				{
					if (bFirst)
					{
						_tcscat_s(szOut, szOutSize, _T("\t"));					
					}
					else
					{
						_tcscat_s(szOut, szOutSize, g_szSpace);
						_tcscat_s(szOut, szOutSize, App.LoadString(IDS_OR));
						_tcscat_s(szOut, szOutSize, g_szSpace);
					}

					if (pa->fVirt & FALT)
					{
						_tcscat_s(szOut, szOutSize, App.LoadString(IDS_ALT_PLUS));
					}
					if (pa->fVirt & FCONTROL)
					{
						_tcscat_s(szOut, szOutSize, App.LoadString(IDS_CTRL_PLUS));
					}
					if (pa->fVirt & FSHIFT)
					{
						_tcscat_s(szOut, szOutSize, App.LoadString(IDS_SHIFT_PLUS));
					}


					if (pa->fVirt & FVIRTKEY) 
					{
						TCHAR keyname[64];
						UINT vkey = MapVirtualKey(pa->key, 0)<<16;

						if (0x30 >= pa->key && VK_SPACE != pa->key)
						{
							vkey |= (1 << 24);
						}

						GetKeyNameText(vkey, keyname, countof(keyname));
						_tcscat_s(szOut, szOutSize, keyname);
					} 
					else
					{
						TCHAR szTemp[2] = { (TCHAR)pa->key, 0 };
						_tcscat_s(szOut, szOutSize, szTemp);
					}

					bFirst = false;
				}
			}
		}



		return;
	}



	static void UpdateMenuShortcuts(HMENU hMenu, MAPIDTOACCEL &mapAccel, MAPSHORTCUTS &mapShortcuts)
	{
		TCHAR szIn[MAX_PATH + 1];        // temporary buffer 
		TCHAR szOut[MAX_PATH + 1]; // buffer for menu-item text 

		DWORD cItems = GetMenuItemCount(hMenu); 
		for (DWORD i = 0; i < cItems; i++) 
		{
			MENUITEMINFO mii; 
			mii.cbSize = sizeof(MENUITEMINFO);
			mii.fMask = MIIM_SUBMENU | MIIM_ID | MIIM_TYPE;

			TCHAR szTypeData[MAX_PATH];
			mii.dwTypeData = szTypeData;
			mii.cch = MAX_PATH;

			if (GetMenuItemInfo(hMenu, i, TRUE, &mii) &&
				!(mii.fType & MFT_SEPARATOR) &&
				GetMenuString(hMenu, i, szIn, MAX_PATH, MF_BYPOSITION))
			{
				ACCELLIST *pal = 0;

				MAPIDTOACCEL::iterator it = mapAccel.find(mii.wID);

				if (it != mapAccel.end())
				{
					pal = &(it->second);
				}

				ConstructMenuString(szOut, MAX_PATH, szIn, pal, mapShortcuts);
				ModifyMenu(hMenu, i, MF_BYPOSITION | MF_STRING, mii.wID, szOut); 

				if (mii.hSubMenu)
				{
					UpdateMenuShortcuts(mii.hSubMenu, mapAccel, mapShortcuts);
				}
			}
		} 
	}


	void UpdateMenuStrings(HACCEL hAccel, HMENU hMenu)
	{
		// Routine to update menu strings and make them have 
		// strings etc to match the accelerators etc
		MAPSHORTCUTS mapShortcuts;
		MAPIDTOACCEL mapAccel;

		LoadAccel(hAccel, mapAccel);

		TCHAR szIn[MAX_PATH + 1];        // temporary buffer 
		TCHAR szOut[MAX_PATH + 1]; // buffer for menu-item text 

		DWORD cItems = GetMenuItemCount(hMenu); 
		for (DWORD i = 0; i < cItems; i++) 
		{
			MENUITEMINFO mii; 
			mii.cbSize = sizeof(MENUITEMINFO);
			mii.fMask = MIIM_SUBMENU | MIIM_ID; 
			mii.dwTypeData = szIn; 
			mii.cch = MAX_PATH; 

			if (GetMenuItemInfo(hMenu, i, TRUE, &mii) &&
				GetMenuString(hMenu, i, szIn, MAX_PATH, MF_BYPOSITION))
			{
				ConstructMenuString(szOut, MAX_PATH, szIn, 0, mapShortcuts);
				ModifyMenu(hMenu, i, MF_BYPOSITION | MF_STRING, mii.wID, szOut); 

				if (mii.hSubMenu)
				{
					MAPSHORTCUTS mapShortcutsNew;
					UpdateMenuShortcuts(mii.hSubMenu, mapAccel, mapShortcutsNew);
				}
			}
		} 
	}

	// The frame's own commands. T hides both of these to add its own and
	// chains back here from its default label.
	bool InvokeCommand(DWORD id)
	{
		T *pT = static_cast<T*>(this);

		switch (id)
		{
		case ID_APP_EXIT:
			pT->PostMessage(WM_CLOSE);
			return true;
		}

		return false;
	}

	bool GetCommandState(DWORD id, bool &bEnabled, bool &bChecked)
	{
		switch (id)
		{
		case ID_APP_EXIT:
			return true;
		}

		return false;
	}

	bool OnCommand(WORD id)
	{
		T *pT = static_cast<T*>(this);

		if (pT->_pView != 0 && pT->_pView->InvokeCommand(id))
			return true;

		return pT->InvokeCommand(id);
	};

	LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		bHandled = OnCommand(LOWORD(wParam));
		return 0;
	}

	bool InitMenu()
	{
		T *pT = static_cast<T*>(this);

		// Create Command bar
		HWND hWndCmdBar = m_CmdBar.Create(pT->m_hWnd, pT->rcDefault, NULL, ATL_SIMPLE_CMDBAR_PANE_STYLE);

		if (!hWndCmdBar)
		{
			CString str;
			str.LoadString(IDS_FAILEDTOCREATETOOLBAR);
			App.Log(str);

			return false;
		}

		m_CmdBar.AttachMenu(pT->GetMenu());
		pT->SetMenu(NULL);

		for(int i = 0; s_commands[i]._id != -1; i++)
		{
			int nImage = s_commands[i]._nImage;

			if (nImage != -1)
			{
				m_CmdBar.m_mapCommand[s_commands[i]._id] = nImage;
			}
		}

		LoadAccelerators();
		ResetUIMap();

		return true;
	}

	void LoadAccelerators()
	{
		T *pT = static_cast<T*>(this);

		pT->m_hAccel = ::LoadAccelerators(App.GetResourceInstance(), MAKEINTRESOURCE(pT->GetWndClassInfo().m_uCommonResourceID));

		//Update Menu Strings
		UpdateMenuStrings(pT->m_hAccel, m_CmdBar.m_hMenu);
	}

	void ResetUIMap()
	{
		// Remove old map
		m_arrayUpdateUIMap.RemoveAll();

		// Normal Commands
		int i = 0;
		for(; s_commands[i]._id != -1; i++)
		{
			_AtlUpdateUIMap uuime = { static_cast<WORD>(s_commands[i]._id), static_cast<WORD>(s_commands[i]._wType) };
			m_arrayUpdateUIMap.Add(uuime);
		}

		// Terminate
		_AtlUpdateUIMap uuime = { (WORD)-1, 0 };

		m_arrayUpdateUIMap.Add(uuime);
		m_pUIMap = m_arrayUpdateUIMap.GetData();

		int nCount = m_arrayUpdateUIMap.GetSize();

		// check for duplicates (debug only)
#ifdef _DEBUG
		for(i = 0; i < nCount; i++)
		{
			for(int j = 0; j < nCount; j++)
			{
				// shouldn't have duplicates in the update UI map
				if(i != j)
				{
					int id1 = m_arrayUpdateUIMap[j].m_nID;
					int id2 = m_arrayUpdateUIMap[i].m_nID;
					ATLASSERT(id1 != id2);
				}
			}
		}
#endif //_DEBUG

		if (m_pUIData) delete [] m_pUIData;
		ATLTRY(m_pUIData = new _AtlUpdateUIData[nCount]);
		ATLASSERT(m_pUIData != NULL);

		if(m_pUIData != NULL)
		{
			IW::MemZero(m_pUIData, sizeof(_AtlUpdateUIData) * nCount);
		}

		// Also reset commands
		m_CmdBar.UpdateCommands();
	}

	LRESULT OnMenuSelect(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		if(m_hWndStatusBar == NULL)
			return 1;

		CStatusBarCtrl sb(m_hWndStatusBar);
		WORD wFlags = HIWORD(wParam);

		if(wFlags == 0xFFFF && lParam == NULL)	// menu closing
		{
			sb.SetSimple(FALSE);
		}
		else
		{
			if(!(wFlags & MF_POPUP))
			{
				WORD wID = LOWORD(wParam);

				// check for special cases
				if(wID >= 0xF000 && wID < 0xF1F0)				// system menu IDs
					wID = (WORD)(((wID - 0xF000) >> 4) + ATL_IDS_SCFIRST);
				else if(wID >= ID_FILE_MRU_FIRST && wID <= ID_FILE_MRU_LAST)	// MRU items
					wID = ATL_IDS_MRU_FILE;
				else if(wID >= ATL_IDM_FIRST_MDICHILD)				// MDI child windows
					wID = ATL_IDS_MDICHILD;

				CString strMenuText = GetCommandTitle(wID, false);

				sb.SetSimple(TRUE);
				sb.SetText(0, strMenuText, SMetaDataTypes::NOBORDERS);
			}
		}

		return 1;
	}

	CString GetToolTipText(UINT_PTR idCtrl)
	{
		return GetCommandTitle(static_cast<WORD>(idCtrl));
	}

	// The strings are "<status bar hint>\n<short name>". A tooltip and a toolbar
	// caption want the second half, the status bar the first.
	CString GetCommandTitle(WORD wID, bool bTooltip = true)
	{
		CString str = App.LoadString(wID);
		const int n = str.Find(_T('\n'));

		if (n != -1)
			str = bTooltip ? str.Mid(n + 1) : str.Left(n);

		return str;
	}

	void EnableMenuItems()
	{
		T *pT = static_cast<T*>(this);
		const _AtlUpdateUIMap* pMap = m_pUIMap;
		_AtlUpdateUIData* pUIData = m_pUIData;
		if(pUIData != NULL)
		{
			for( ; pMap->m_nID != (WORD)-1; pMap++, pUIData++)
			{
				bool bEnable = true;
				bool bChecked = false;

				if (!(pT->_pView != 0 && pT->_pView->GetCommandState(pMap->m_nID, bEnable, bChecked)) &&
					!pT->GetCommandState(pMap->m_nID, bEnable, bChecked))
				{
					bEnable = false;
					bChecked = false;
				}

				int nCheck = bChecked ? 1 : 0;

				if(bEnable)
				{
					if(pUIData->m_wState & UPDUI_DISABLED)
					{
						pUIData->m_wState |= pMap->m_wType;
						pUIData->m_wState &= ~UPDUI_DISABLED;
					}
				}
				else
				{
					if(!(pUIData->m_wState & UPDUI_DISABLED))
					{
						pUIData->m_wState |= pMap->m_wType;
						pUIData->m_wState |= UPDUI_DISABLED;
					}
				}

				switch(nCheck)
				{
				case 0:
					if((pUIData->m_wState & UPDUI_CHECKED) || (pUIData->m_wState & UPDUI_CHECKED2))
					{
						pUIData->m_wState |= pMap->m_wType;
						pUIData->m_wState &= ~(UPDUI_CHECKED | UPDUI_CHECKED2);
					}
					break;
				case 1:
					if(!(pUIData->m_wState & UPDUI_CHECKED))
					{
						pUIData->m_wState |= pMap->m_wType;
						pUIData->m_wState &= ~UPDUI_CHECKED2;
						pUIData->m_wState |= UPDUI_CHECKED;
					}
					break;
				case 2:
					if(!(pUIData->m_wState & UPDUI_CHECKED2))
					{
						pUIData->m_wState |= pMap->m_wType;
						pUIData->m_wState &= ~UPDUI_CHECKED;
						pUIData->m_wState |= UPDUI_CHECKED2;
					}
					break;
				}

				if(pUIData->m_wState & pMap->m_wType)
				{
					m_wDirtyType |= pMap->m_wType;
				}
			}
		}

		UIUpdateToolBar();
	}
};
