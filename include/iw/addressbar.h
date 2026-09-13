///////////////////////////////////////////////////////////////////////
//
// This file is part of the ImageWalker code base.
// For more information on ImageWalker see www.ImageWalker.com
//
// Copyright (C) 1998-2001 Zac Walker.  All rights reserved.
//
///////////////////////////////////////////////////////////////////////
//
// The shell address combo, shared by 1.0 through 2.3.
//
// Each app used to carry its own: src10/BrowserAddressCombo.*, the AddressInsert*
// members on 2.0's and 2.2's CMainFrame, and the CAddressBar<T> mixin in 2.3.
// The behaviour below is 2.3's - repopulate on dropdown,
// known-folder roots, typed path navigation - which is what 1.0, 2.0 and 2.2
// gain by adopting it.
//
// The host supplies four things and nothing else:
//
//   enum { kAddressBarId = ... };                    // the id in its .rc
//   void AddressNavigate(const CShellItem &item);    // the one way it navigates
//   CShellItem AddressFolder();                      // where it is now
//   HIMAGELIST AddressImageList() const;             // the system image list
//
// "go to parent" and "resolve a relative path" are derived from AddressFolder
// rather than being hooks of their own, so an app cannot wire one of them up
// inconsistently with the others.
//
///////////////////////////////////////////////////////////////////////

#pragma once

#include "iw/shell.h"

namespace IW
{
	template <class T>
	class CAddressBar
	{
	public:
		// The frame lays this out and sets its font; there is no point hiding it.
		CComboBoxEx m_wndAddress;

		BEGIN_MSG_MAP(CAddressBar)
			COMMAND_HANDLER(T::kAddressBarId, CBN_DROPDOWN, OnAddressDropDown)
			COMMAND_HANDLER(T::kAddressBarId, CBN_SELCHANGE, OnAddressSelectionChange)
			NOTIFY_HANDLER(T::kAddressBarId, CBEN_DELETEITEM, OnAddressDeleteItem)
		END_MSG_MAP()

		HWND CreateAddressBar(HWND hWndParent)
		{
			T* pT = static_cast<T*>(this);

			m_wndAddress.Create(hWndParent, CRect(0, 0, 100, 300), NULL,
			                    CBS_DROPDOWN | WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
			                    0, T::kAddressBarId);

			m_wndAddress.SetImageList(pT->AddressImageList());
			m_wndAddress.SetExtendedStyle(CBES_EX_NOSIZELIMIT, CBES_EX_NOSIZELIMIT);

			return m_wndAddress;
		}

		HWND GetAddressBar() const { return m_wndAddress; }

		BOOL PreTranslateMessage(MSG* pMsg)
		{
			if (pMsg->message != WM_KEYDOWN)
				return FALSE;

			const int keyCode = static_cast<int>(pMsg->wParam);

			if (keyCode != VK_RETURN && keyCode != VK_ESCAPE)
				return FALSE;

			const HWND hWndFocus = ::GetFocus();
			CEdit wndEdit = m_wndAddress.GetEditCtrl();

			if (hWndFocus != m_wndAddress && wndEdit != hWndFocus)
				return FALSE;

			if (keyCode == VK_RETURN)
			{
				ParseAddress();
				return TRUE;
			}

			const int nSelection = m_wndAddress.GetCurSel();

			if (CB_ERR != nSelection)
			{
				m_wndAddress.SetCurSel(nSelection);
				return TRUE;
			}

			return FALSE;
		}

		// Show a folder without navigating to it - the frame calls this when the
		// folder changed for some other reason.
		void SetAddress(LPCTSTR szName, int nIcon)
		{
			COMBOBOXEXITEM cbi = {0};

			cbi.mask = CBEIF_IMAGE | CBEIF_SELECTEDIMAGE | CBEIF_TEXT;
			cbi.iItem = -1;
			cbi.pszText = (LPTSTR)szName;
			cbi.cchTextMax = static_cast<int>(_tcsclen(szName));
			cbi.iImage = nIcon;
			cbi.iSelectedImage = nIcon;

			m_wndAddress.SetItem(&cbi);

			// CBEM_SETITEM on the -1 (edit) item does not invalidate the
			// control. An app whose first navigation lands after the frame has
			// painted - 1.0 opens its folder from a posted message - then shows
			// an empty combo for the rest of the session.
			m_wndAddress.Invalidate();
		}

		int AddressInsertItem(const CShellItem& item, int nIndent)
		{
			CShellDesktop desktop;
			int nPos = -1; // -1 appends

			for (int i = 0; i < m_wndAddress.GetCount(); i++)
			{
				auto pItem = reinterpret_cast<CShellItem*>(m_wndAddress.GetItemData(i));

				if (pItem == nullptr)
					continue;

				const short n = item.Compare(desktop, *pItem);

				if (n == 0)
					return i; // already have it

				if (n < 0)
				{
					nPos = i;
					break;
				}
			}

			SHFILEINFO sfi;
			ZeroMemory(&sfi, sizeof(sfi));

			if (0 == SHGetFileInfo((LPCTSTR)item.GetItem(), 0, &sfi, sizeof(sfi),
			                       SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_DISPLAYNAME))
				return -1;

			sfi.szDisplayName[_countof(sfi.szDisplayName) - 1] = 0;

			return m_wndAddress.InsertItem(nPos, sfi.szDisplayName, sfi.iIcon, sfi.iIcon,
			                               nIndent, (LPARAM)new CShellItem(item));
		}

		void AddressInsertKnownFolder(int nCsidl)
		{
			T* pT = static_cast<T*>(this);

			CShellItem item;

			if (item.Open(pT->m_hWnd, nCsidl))
				AddressInsertItem(item, item.Depth());
		}

		void AddressInsertChildren(const CShellItem& item, int nIndent)
		{
			T* pT = static_cast<T*>(this);

			CShellFolder folder;

			if (FAILED(folder.Open(item)))
				return;

			CShellItemEnum shellenum;

			if (FAILED(shellenum.Create(pT->m_hWnd, folder, SHCONTF_FOLDERS)))
				return;

			LPITEMIDLIST pItemRaw = nullptr;
			ULONG ulFetched = 0;
			CShellItem itemEnum, itemChild;

			while (shellenum->Next(1, &pItemRaw, &ulFetched) == S_OK)
			{
				itemEnum.Attach(pItemRaw);
				itemChild.Cat(item, itemEnum);

				AddressInsertItem(itemChild, nIndent);
			}
		}

		LRESULT OnAddressDropDown(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
		{
			T* pT = static_cast<T*>(this);

			CWaitCursor wait;

			m_wndAddress.ResetContent();

			CShellDesktopItem itemDesktop;

			int nSelection = AddressInsertItem(itemDesktop, 0);
			AddressInsertChildren(itemDesktop, 1);

			AddressInsertKnownFolder(CSIDL_MYPICTURES);
			AddressInsertKnownFolder(CSIDL_MYVIDEO);
			AddressInsertKnownFolder(CSIDL_MYMUSIC);

			CShellItem itemDrives;

			if (itemDrives.Open(pT->m_hWnd, CSIDL_DRIVES))
				AddressInsertChildren(itemDrives, 2);

			const CShellItem item = pT->AddressFolder();

			if (!item.IsNull() && !item.IsDesktop())
			{
				CShellItem itemCopy(item);
				itemCopy.StripToParent();

				while (!itemCopy.IsDesktop())
				{
					AddressInsertItem(itemCopy, itemCopy.Depth());
					itemCopy.StripToParent();
				}

				nSelection = AddressInsertItem(item, item.Depth());
			}

			if (nSelection >= 0)
				m_wndAddress.SetCurSel(nSelection);

			return 0;
		}

		// Navigate to whatever the combo currently has selected. 2.2's frame also
		// calls this from its CBN_SELENDOK branch: ID_VIEW_ADDRESS is both the
		// menu item and the combo's control id there.
		void AddressSelectionChanged()
		{
			const int nSelection = m_wndAddress.GetCurSel();

			if (CB_ERR != nSelection)
			{
				if (auto pItem = reinterpret_cast<CShellItem*>(m_wndAddress.GetItemData(nSelection)))
					static_cast<T*>(this)->AddressNavigate(*pItem);
			}
		}

		LRESULT OnAddressSelectionChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
		{
			AddressSelectionChanged();
			return 0;
		}

		LRESULT OnAddressDeleteItem(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
		{
			auto pCBEx = (PNMCOMBOBOXEX)pnmh;

			delete reinterpret_cast<CShellItem*>(pCBEx->ceItem.lParam);

			return 0;
		}

		void ParseAddress()
		{
			T* pT = static_cast<T*>(this);

			// ::CString - 2.2 declares an IW::CString of its own, which shadows
			// ATL's for unqualified lookup inside this namespace.
			::CString strEntered;
			m_wndAddress.GetWindowText(strEntered);

			if (strEntered.IsEmpty())
				return;

			const CShellItem itemCurrent = pT->AddressFolder();

			if (strEntered == _T(".."))
			{
				if (itemCurrent.IsNull() || itemCurrent.IsDesktop())
					return;

				CShellItem itemParent(itemCurrent);
				itemParent.StripToParent();
				pT->AddressNavigate(itemParent);
				return;
			}

			// Relative to the current folder first, then as an absolute path. 2.3
			// had lost the relative attempt: it built the combined path and then
			// parsed the typed text twice.
			::CString strCurrent;

			if (!itemCurrent.IsNull() && itemCurrent.GetPath(strCurrent))
			{
				TCHAR szCombined[MAX_PATH] = {0};

				if (::PathCombine(szCombined, strCurrent, strEntered) != nullptr &&
					NavigateToPath(szCombined))
					return;
			}

			if (NavigateToPath(strEntered))
				return;

			MessageBeep(MB_OK);
		}

	private:
		bool NavigateToPath(LPCTSTR szPath)
		{
			CShellItem item;

			if (!item.Open(szPath))
				return false;

			static_cast<T*>(this)->AddressNavigate(item);
			return true;
		}
	};
}
