// ImageWalker by Zac Walker
//
// Purpose: Building a menu from shell items, with their icons and display
//          names.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewSkin.h"

namespace IW {

class ShellMenuItem
{
public:
	DWORD dwMagic;
	LPCTSTR lpstrText;
	UINT fType;
	UINT fState;
	int iButton;
	CString     strDisplayName;

	ShellMenuItem() { dwMagic = 0x1919; fType = 0; fState = 0; }
	bool IsValid() { return (dwMagic == 0x1919); }
} ;

class CShellMenu : public CWindowImpl<CShellMenu>
{
private:
	CMenu _menu;
	CSimpleArray<ShellMenuItem*> _items;
	bool m_bSelectFirst;
	

public:
	DECLARE_WND_SUPERCLASS(NULL, GetWndClassName())

	CShellMenu() : m_bSelectFirst(false)
	{
		
	}

	~CShellMenu()
	{
	}

	operator HMENU()
	{
		return _menu;
	}


	bool LoadMenu(UINT nId)
	{
		if (!_menu.m_hMenu)
			if (!_menu.CreatePopupMenu())
				return false;

		_menu.LoadMenu(nId);

		return true;
	}

	bool AddSeparator()
	{
		if (!_menu.m_hMenu)
			if (!_menu.CreatePopupMenu())
				return false;
			
		if (!_menu.AppendMenu(MF_SEPARATOR))
			return false;		
		
		return true;
	}

	UINT EnableMenuItem(UINT nIDEnableItem, UINT nEnable)
	{
		return _menu.EnableMenuItem(nIDEnableItem, nEnable);
	}

	int GetMenuItemCount() const
	{
		return _menu.GetMenuItemCount();
	}

	bool AddItem(UINT nId, UINT nIdString)
	{
		if (!_menu.m_hMenu)
			if (!_menu.CreatePopupMenu())
				return false;

		CString str;
		str.LoadString(nIdString);
			
		return _menu.AppendMenu(MF_ENABLED|MF_STRING, nId, str) != 0;
	}

	bool AddItem(UINT nId, const CShellItem &item, UINT nIdStringPrefix = UINT_MAX)
	{
		if (!_menu.m_hMenu)
			if (!_menu.CreatePopupMenu())
				return false;

		SHFILEINFO sfi;
		ZeroMemory(&sfi, sizeof(sfi));
		
		SHGetFileInfo((LPCTSTR)(LPCITEMIDLIST)item,
			0,
			&sfi, 
			sizeof(SHFILEINFO), 
			SHGFI_PIDL |
			SHGFI_SMALLICON |
			SHGFI_SYSICONINDEX |
			SHGFI_DISPLAYNAME);		

		ShellMenuItem* pMI = new ShellMenuItem;

		if (pMI == NULL)
			return false;

		pMI->iButton = sfi.iIcon;
		if (nIdStringPrefix != UINT_MAX) pMI->strDisplayName.LoadString(nIdStringPrefix);

		CString str;

		if (!item.GetPath(str))
		{
			str = sfi.szDisplayName;
		}
		
		pMI->strDisplayName += str;
		pMI->lpstrText = pMI->strDisplayName;

		_items.Add(pMI);

		CMenuItemInfo mii;

		mii.fMask = MIIM_ID | MIIM_TYPE | MIIM_DATA;
		mii.wID = nId;
		mii.fType = MFT_OWNERDRAW;
		mii.dwItemData = (ULONG_PTR)pMI;

		return _menu.InsertMenuItem(UINT_MAX, TRUE, &mii) != 0;
		
		//return _menu.InsertMenu(-1, MF_BYPOSITION|MF_ENABLED|MF_OWNERDRAW, nId, (LPTSTR)pMI) != 0;
	}

	BOOL TrackPopupMenu(HWND hWnd, UINT nFlags, int x, int y, LPCRECT lpRect = NULL)
	{
		HWND hwnd = CWindowImpl<CShellMenu>::Create(hWnd, CRect(0,0,0,0), NULL, 0);

		if (!hwnd)
			return false;

		ATLASSERT(::IsMenu(_menu));
		BOOL bRet = ::TrackPopupMenu(_menu, nFlags | TPM_RETURNCMD, 
			x, y, 0, m_hWnd, lpRect);

		if (bRet)
		{
			// Reflect command to the parent
			::PostMessage(GetParent(), WM_COMMAND, bRet, 0);			
		}

		// restore state and delete menu item data
		for(int i = 0; i < _items.GetSize(); i++)
		{
			delete _items[i];
		}

		_items.RemoveAll();
		_menu.DestroyMenu();

		DestroyWindow();

		return bRet;
	}

	BEGIN_MSG_MAP(CShellMenu)

		MESSAGE_HANDLER(WM_INITMENUPOPUP, OnInit)		
		MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
		MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)

		
	END_MSG_MAP()

	LRESULT OnInit(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		if (m_bSelectFirst) PostMessage(WM_KEYDOWN, VK_DOWN);
		return 0;
	}

	LRESULT OnDrawItem(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		// Image Walker Fix
		// Should only handel its own draw item
		// messages
		LPDRAWITEMSTRUCT lpDrawItemStruct = (LPDRAWITEMSTRUCT)lParam;
		ShellMenuItem* pMI = (ShellMenuItem*)lpDrawItemStruct->itemData;

		if (lpDrawItemStruct->CtlType == ODT_MENU && pMI != NULL && pMI->IsValid())
		{
			IW::Skin::DrawMenuItem(lpDrawItemStruct);
		}
		
		return 0;
	}
	
	LRESULT OnMeasureItem(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		LPMEASUREITEMSTRUCT lpMeasureItemStruct = (LPMEASUREITEMSTRUCT)lParam;
		ShellMenuItem* pMI = (ShellMenuItem*)lpMeasureItemStruct->itemData;

		if (lpMeasureItemStruct->CtlType == ODT_MENU && pMI != NULL && pMI->IsValid())
		{
			CDC dc;
			dc.Attach(CreateIC(_T("DISPLAY"), NULL, NULL, NULL));

			HFONT hfontOld = dc.SelectFont((HFONT)GetStockObject(DEFAULT_GUI_FONT));

			CSize sizeText;
			dc.GetTextExtent(pMI->strDisplayName, -1, &sizeText);
			//ATLASSERT(sizeText.cx < 500);
			lpMeasureItemStruct->itemWidth = sizeText.cx + 20;
			lpMeasureItemStruct->itemHeight = IW::Max(sizeText.cy, IW::Max(24,sizeText.cy));

			dc.SelectFont(hfontOld);
		}
		
		return (LRESULT)TRUE;
	}
};

}; //namespace IW
