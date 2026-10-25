///////////////////////////////////////////////////////////////////////
//
// This file is part of the ImageWalker code base.
// For more information on ImageWalker see www.ImageWalker.com
//
// Copyright (C) 1998-2001 Zac Walker.  All rights reserved.
//
///////////////////////////////////////////////////////////////////////
//
// The shell folder tree, shared by 2.0 through 2.3.
//
// 1.0 has no folder tree and is not meant to - see docs/features.md. 3.0 has its
// own in src30/PlatformShellTree.cpp, against IShellItem and std::filesystem.
//
// Merged from the three copies: 2.2's structure, 2.3's item helpers and naming,
// and the IContextMenu3 / WM_MENUCHAR path that 2.3 has but 2.0 and 2.2
// do not.
//
// The host supplies three things:
//
//   void TreeNavigate(const CShellItem &item);  // the user picked a folder
//   bool TreeShowHidden();                      // include hidden folders
//   HIMAGELIST TreeImageList();                 // the system image list
//
// IUnknown is implemented here by hand rather than through CComObjectRootEx.
// The tree is only ever handed out as the IDropTarget its own window is
// registered with, and its lifetime belongs to the host, so Release must not
// delete. That is three short functions instead of a COM map plus
// CreateInstance, and unlike 2.3's version - which returned S_OK from AddRef and
// handed *this* back for any IID without an AddRef - it is correct.
//
///////////////////////////////////////////////////////////////////////

#pragma once

#include "iw/shell.h"

namespace IW
{
	template <class T>
	class CShellTreeView : public CWindowImpl<CShellTreeView<T>>, public IDropTarget
	{
	public:
		typedef CShellTreeView<T> ThisClass;
		typedef CWindowImpl<CShellTreeView<T>> BaseClass;

		// TVI_ROOT and TVI_LAST are sentinels, so a plain NULL needs its own name.
		static HTREEITEM TreeItemNull() { return (HTREEITEM)(ULONG_PTR)0; }

		CTreeViewCtrl _tree;

		// The tree is its own window, so the host is held rather than recovered by
		// a downcast. Call this before Create.
		void SetTreeHost(T* pHost) { _pHost = pHost; }

		BEGIN_MSG_MAP(ThisClass)
			MESSAGE_HANDLER(WM_CREATE, OnCreate)
			MESSAGE_HANDLER(WM_SIZE, OnSize)
			MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
			MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)

			NOTIFY_CODE_HANDLER(TVN_ITEMEXPANDING, OnItemExpanding)
			NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
			NOTIFY_CODE_HANDLER(TVN_SELCHANGED, OnSelChanged)
			NOTIFY_CODE_HANDLER(TVN_DELETEITEM, OnDelItem)
			NOTIFY_CODE_HANDLER(TVN_GETDISPINFO, OnGetDispInfo)

			if (ProcessContextMenuMessage(uMsg, wParam, lParam, lResult))
				return TRUE;
		END_MSG_MAP()

		LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
		{
			const DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
				TVS_HASBUTTONS | TVS_HASLINES | TVS_SHOWSELALWAYS;

			_tree.Create(this->m_hWnd, CWindow::rcDefault, 0, dwStyle, 0, 106);
			_tree.SetImageList(Host()->TreeImageList(), TVSIL_NORMAL);

			HTREEITEM hItem = InsertItem(new CShellDesktopItem());
			_tree.Expand(hItem, TVE_EXPAND);

			RegisterDropTarget();

			return 1;
		}

		LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
		{
			_tree.SetWindowPos(NULL, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}

		LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
		{
			// Before the apartment goes: an abandoned drag still holds shell
			// interfaces, and OLE must not be left with a pointer to this window.
			DragLeave();
			RevokeDropTarget();
			_tree.SetImageList(NULL, TVSIL_NORMAL);

			return 0;
		}

		LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
		{
			return 1;
		}

		// --- population ----------------------------------------------------

		static CShellItem* CreateItem(const CShellItem& itemParent, CShellItem& itemChild)
		{
			auto pItem = new CShellItem;

			if (itemParent.IsDesktop())
				pItem->Attach(itemChild.Detach());
			else
				pItem->Cat(itemParent, itemChild);

			return pItem;
		}

		UINT EnumFlags()
		{
			UINT dwFlags = SHCONTF_FOLDERS;

			if (Host()->TreeShowHidden())
				dwFlags |= SHCONTF_INCLUDEHIDDEN;

			return dwFlags;
		}

		void FillTreeView(IShellFolder* pFolderRaw, CShellItem& itemParent, HTREEITEM hParent)
		{
			CShellItemEnum enumitem;
			CShellFolder pFolder = pFolderRaw;

			if (FAILED(enumitem.Create(this->m_hWnd, pFolder, EnumFlags())))
				return;

			LPITEMIDLIST pItemRaw = NULL;
			ULONG ulFetched = 0;
			CShellItem item;

			while (enumitem->Next(1, &pItemRaw, &ulFetched) == S_OK)
			{
				item.Attach(pItemRaw);
				CShellItem* pItem = CreateItem(itemParent, item);

				if (!InsertItem(pItem, hParent))
					delete pItem;
			}
		}

		HTREEITEM InsertItem(CShellItem* pItem, HTREEITEM hParent = TVI_ROOT, HTREEITEM hPrev = TVI_LAST)
		{
			TV_INSERTSTRUCT tvins;

			tvins.item.mask = TVIF_CHILDREN | TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
			tvins.item.cChildren = GetFolderItemSubFolders(*pItem);
			tvins.item.pszText = LPSTR_TEXTCALLBACK;
			tvins.item.cchTextMax = -1;
			tvins.item.lParam = (LPARAM)pItem;
			tvins.item.iImage = GetFolderItemImageIndex(*pItem, false);
			tvins.item.iSelectedImage = GetFolderItemImageIndex(*pItem, true);

			tvins.hInsertAfter = hPrev;
			tvins.hParent = hParent;

			return _tree.InsertItem(&tvins);
		}

		static int GetFolderItemImageIndex(const CShellItem& item, bool bOpen)
		{
			SHFILEINFO sfi;
			ZeroMemory(&sfi, sizeof(sfi));

			UINT uFlags = SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON;
			if (bOpen) uFlags |= SHGFI_OPENICON;

			SHGetFileInfo((LPCTSTR)item.GetItem(), 0, &sfi, sizeof(sfi), uFlags);
			return sfi.iIcon;
		}

		static ::CString GetFolderItemDisplayName(const CShellItem& item)
		{
			SHFILEINFO sfi;
			ZeroMemory(&sfi, sizeof(sfi));

			SHGetFileInfo((LPCTSTR)item.GetItem(), 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_DISPLAYNAME);
			return sfi.szDisplayName;
		}

		static int GetFolderItemSubFolders(const CShellItem& item)
		{
			SHFILEINFO sfi;
			ZeroMemory(&sfi, sizeof(sfi));
			sfi.dwAttributes = SFGAO_HASSUBFOLDER;

			SHGetFileInfo((LPCTSTR)item.GetItem(), 0, &sfi, sizeof(sfi),
			              SHGFI_PIDL | SHGFI_ATTRIBUTES | SHGFI_ATTR_SPECIFIED);

			return (sfi.dwAttributes & SFGAO_HASSUBFOLDER) ? 1 : 0;
		}

		// --- context menu --------------------------------------------------

		void ShowMenu(CShellItem* pItemParent, CShellItem* pItem, const CPoint& pt)
		{
			ATLASSERT(pItemParent && pItem);

			CShellFolder pFolder;

			if (FAILED(pFolder.Open(*pItemParent, true)))
				return;

			CComPtr<IContextMenu> spContextMenu;
			LPCITEMIDLIST pTailItem = pItem->GetTailItem();

			if (FAILED(pFolder->GetUIObjectOf(this->m_hWnd, 1, &pTailItem, IID_IContextMenu,
			                                  0, (LPVOID*)&spContextMenu)))
				return;

			CMenu menu;

			if (!menu.CreatePopupMenu())
				return;

			if (SUCCEEDED(spContextMenu->QueryContextMenu(menu, 0, 1, 0x7fff, CMF_EXPLORE)))
			{
				_pContextMenu2 = spContextMenu;

				const int idCmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
				                                      pt.x, pt.y, this->m_hWnd);

				if (idCmd)
				{
					CMINVOKECOMMANDINFO cmi = {0};
					cmi.cbSize = sizeof(cmi);
					cmi.hwnd = this->m_hWnd;
					cmi.lpVerb = (LPCSTR)MAKEINTRESOURCE(idCmd - 1);
					cmi.nShow = SW_SHOWNORMAL;

					spContextMenu->InvokeCommand(&cmi);
				}

				_pContextMenu2.Release();
			}

			_tree.Invalidate();
		}

		BOOL ProcessContextMenuMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& lResult)
		{
			if (_pContextMenu2 == NULL)
				return FALSE;

			if (WM_MENUCHAR == uMsg)
			{
				// WM_MENUCHAR needs the LRESULT, which only IContextMenu3 can return.
				CComQIPtr<IContextMenu3> pContextMenu3 = _pContextMenu2;

				if (pContextMenu3 == NULL)
					return FALSE;

				return SUCCEEDED(pContextMenu3->HandleMenuMsg2(uMsg, wParam, lParam, &lResult));
			}

			if (WM_DRAWITEM == uMsg || WM_MEASUREITEM == uMsg || WM_INITMENUPOPUP == uMsg)
				return SUCCEEDED(_pContextMenu2->HandleMenuMsg(uMsg, wParam, lParam));

			return FALSE;
		}

		void ShowMenu(NMHDR* /*pNMHDR*/)
		{
			CPoint pt;
			::GetCursorPos(&pt);
			this->ScreenToClient(&pt);

			TV_HITTESTINFO tvhti;
			tvhti.pt = pt;
			_tree.HitTest(&tvhti);
			_tree.SelectItem(tvhti.hItem);

			if ((tvhti.flags & (TVHT_ONITEMLABEL | TVHT_ONITEMICON)) == 0)
				return;

			this->ClientToScreen(&pt);

			TV_ITEM tvi;
			tvi.mask = TVIF_PARAM;
			tvi.hItem = tvhti.hItem;

			if (!_tree.GetItem(&tvi))
				return;

			auto pItem = (CShellItem*)tvi.lParam;

			// The parent folder is what owns the item's context menu.
			HTREEITEM htiParent = _tree.GetParentItem(tvhti.hItem);

			if (!htiParent)
				return;

			tvi.hItem = htiParent;

			if (!_tree.GetItem(&tvi))
				return;

			ShowMenu((CShellItem*)tvi.lParam, pItem, pt);
			Refresh();
		}

		// --- notifications -------------------------------------------------

		void FolderExpanding(NMHDR* pNMHDR)
		{
			auto pnmtv = (NM_TREEVIEW*)pNMHDR;

			if (pnmtv->itemNew.state & TVIS_EXPANDEDONCE)
				return;

			auto pItem = (CShellItem*)pnmtv->itemNew.lParam;

			if (pItem == NULL)
				return;

			CShellFolder pNewFolder;

			if (SUCCEEDED(pNewFolder.Open(*pItem, true)))
			{
				FillTreeView(pNewFolder, *pItem, pnmtv->itemNew.hItem);
				SortChildren(pnmtv->itemNew.hItem);
			}
		}

		LRESULT OnItemExpanding(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
		{
			FolderExpanding(pnmh);
			return 0;
		}

		LRESULT OnSelChanged(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
		{
			auto pNMTreeView = (NMTREEVIEW*)pnmh;

			if (pNMTreeView->action == TVC_BYMOUSE || pNMTreeView->action == TVC_BYKEYBOARD)
			{
				if (auto pItem = (CShellItem*)pNMTreeView->itemNew.lParam)
					Host()->TreeNavigate(*pItem);
			}

			return 0;
		}

		LRESULT OnRightClick(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
		{
			ShowMenu(pnmh);
			return 0;
		}

		LRESULT OnDelItem(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
		{
			auto pnmtv = (LPNMTREEVIEW)pnmh;
			delete (CShellItem*)pnmtv->itemOld.lParam;

			return 0;
		}

		LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
		{
			auto lptvdi = (LPNMTVDISPINFO)pnmh;
			auto pItem = (CShellItem*)lptvdi->item.lParam;

			if (pItem != NULL && (lptvdi->item.mask & TVIF_TEXT))
			{
				::CString str = GetFolderItemDisplayName(*pItem);
				_tcsncpy_s(lptvdi->item.pszText, lptvdi->item.cchTextMax, str, _TRUNCATE);
			}

			return 0;
		}

		// --- navigation ----------------------------------------------------

		void SetCurFolder(const CShellItem& item)
		{
			if (item.IsNull())
				return;

			if (HTREEITEM hItem = FindItemData(item))
				_tree.SelectItem(hItem);
		}

		HTREEITEM FindItemData(const CShellItem& item)
		{
			HTREEITEM hItem = TVI_ROOT;

			if (item.Depth() > 0)
			{
				CShellItem itemParent(item);
				itemParent.StripToParent();
				hItem = FindItemData(itemParent);
			}

			if (hItem == NULL)
				return NULL;

			_tree.Expand(hItem, TVE_EXPAND);

			for (HTREEITEM htiCur = _tree.GetChildItem(hItem); htiCur;
			     htiCur = _tree.GetNextItem(htiCur, TVGN_NEXT))
			{
				auto pItemChild = (CShellItem*)_tree.GetItemData(htiCur);

				if (pItemChild != NULL && item.Compare(_pDesktop, *pItemChild) == 0)
					return htiCur;
			}

			return NULL;
		}

		static int CALLBACK TreeViewCompareProc(LPARAM lparam1, LPARAM lparam2, LPARAM lparamSort)
		{
			auto pThis = (ThisClass*)lparamSort;
			auto pItem1 = (CShellItem*)lparam1;
			auto pItem2 = (CShellItem*)lparam2;

			return pItem1->Compare(pThis->_pDesktop, *pItem2);
		}

		void SortChildren(HTREEITEM hItem)
		{
			TV_SORTCB tvscb;

			tvscb.hParent = hItem;
			tvscb.lParam = (LPARAM)this;
			tvscb.lpfnCompare = TreeViewCompareProc;

			_tree.SortChildrenCB(&tvscb);
		}

		// --- refresh -------------------------------------------------------

		void Refresh()
		{
			_tree.SetRedraw(FALSE);
			RefreshNode(_tree.GetSelectedItem());
			_tree.SetRedraw(TRUE);
		}

		void RefreshNode(HTREEITEM htiFolder)
		{
			if (htiFolder == NULL)
				return;

			auto pItemFolder = (CShellItem*)_tree.GetItemData(htiFolder);

			if (pItemFolder == NULL)
				return;

			TVITEM item;
			item.hItem = htiFolder;
			item.mask = TVIF_CHILDREN | TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
			item.cChildren = GetFolderItemSubFolders(*pItemFolder);
			item.pszText = LPSTR_TEXTCALLBACK;
			item.cchTextMax = -1;
			item.lParam = (LPARAM)pItemFolder;
			item.iImage = GetFolderItemImageIndex(*pItemFolder, false);
			item.iSelectedImage = GetFolderItemImageIndex(*pItemFolder, true);

			_tree.SetItem(&item);

			if ((_tree.GetItemState(htiFolder, TVIS_EXPANDED) & TVIS_EXPANDED) == 0)
				return;

			CShellFolder spFolder;

			if (FAILED(spFolder.Open(*pItemFolder, true)))
				return;

			typedef std::map<CShellItem, HTREEITEM> MAPITEMS;
			MAPITEMS mapItems;

			for (HTREEITEM hti = _tree.GetChildItem(htiFolder); hti;
			     hti = _tree.GetNextItem(hti, TVGN_NEXT))
			{
				if (auto pItemChild = (CShellItem*)_tree.GetItemData(hti))
					mapItems[*pItemChild] = hti;
			}

			CShellItemEnum enumitem;

			if (FAILED(enumitem.Create(this->m_hWnd, spFolder, EnumFlags())))
				return;

			LPITEMIDLIST pItemRaw = NULL;
			ULONG ulFetched = 0;
			CShellItem itemEnum;

			while (enumitem->Next(1, &pItemRaw, &ulFetched) == S_OK)
			{
				itemEnum.Attach(pItemRaw);

				CShellItem* pItem = CreateItem(*pItemFolder, itemEnum);
				auto i = mapItems.find(*pItem);

				if (i == mapItems.end())
				{
					if (!InsertItem(pItem, htiFolder))
						delete pItem;
				}
				else
				{
					// Still there; leave the existing node alone.
					mapItems.erase(i);
					delete pItem;
				}
			}

			// Whatever the enumeration did not account for has gone.
			for (auto i = mapItems.begin(); i != mapItems.end(); ++i)
				_tree.DeleteItem(i->second);

			SortChildren(htiFolder);

			for (HTREEITEM hti = _tree.GetChildItem(htiFolder); hti;
			     hti = _tree.GetNextItem(hti, TVGN_NEXT))
			{
				RefreshNode(hti);
			}
		}

		// --- drop target ---------------------------------------------------

		void RegisterDropTarget()
		{
			::RegisterDragDrop(_tree, static_cast<IDropTarget*>(this));
		}

		void RevokeDropTarget()
		{
			::RevokeDragDrop(_tree);
		}

		STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
		{
			_hDragOverItem = TreeItemNull();
			_pDataObject = pDataObj;

			return DragOver(grfKeyState, pt, pdwEffect);
		}

		STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
		{
			CPoint pointClient(pt.x, pt.y);
			::ScreenToClient(this->m_hWnd, &pointClient);

			UINT flags = 0;
			HTREEITEM hitem = _tree.HitTest(pointClient, &flags);

			if (_hDragOverItem != hitem)
			{
				_hDragOverItem = hitem;
				_tree.SelectDropTarget(hitem);

				if (_pDropTarget != NULL)
				{
					_pDropTarget->DragLeave();
					_pDropTarget.Release();
				}

				if (_hDragOverItem != TreeItemNull())
				{
					if (auto pItemChild = (CShellItem*)_tree.GetItemData(hitem))
					{
						CShellFolder spFolder;

						if (SUCCEEDED(spFolder.Open(*pItemChild, true)))
							spFolder->CreateViewObject(_tree, IID_IDropTarget, (LPVOID*)&_pDropTarget);
					}
				}

				if (_pDropTarget != NULL)
				{
					HRESULT hr = _pDropTarget->DragEnter(_pDataObject, grfKeyState, pt, pdwEffect);

					if (SUCCEEDED(hr))
						return hr;
				}
			}

			if (_pDropTarget != NULL)
				return _pDropTarget->DragOver(grfKeyState, pt, pdwEffect);

			*pdwEffect = DROPEFFECT_NONE;
			return S_OK;
		}

		STDMETHODIMP DragLeave()
		{
			if (_pDropTarget != NULL)
			{
				_pDropTarget->DragLeave();
				_pDropTarget.Release();
			}

			_pDataObject.Release();

			if (_hDragOverItem != TreeItemNull())
			{
				_hDragOverItem = TreeItemNull();

				if (_tree.IsWindow())
					_tree.SelectDropTarget(NULL);
			}

			return S_OK;
		}

		STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
		{
			HRESULT hr = DROPEFFECT_NONE;

			if (_pDropTarget != NULL)
				hr = _pDropTarget->Drop(pDataObj, grfKeyState, pt, pdwEffect);
			else
				*pdwEffect = DROPEFFECT_NONE;

			DragLeave();

			return hr;
		}

		// The host owns this object, so Release must not delete. The reference
		// count exists only to balance RegisterDragDrop/RevokeDragDrop.
		STDMETHODIMP QueryInterface(REFIID iid, void** ppvObject)
		{
			if (ppvObject == NULL)
				return E_POINTER;

			*ppvObject = NULL;

			if (iid == IID_IUnknown || iid == IID_IDropTarget)
			{
				*ppvObject = static_cast<IDropTarget*>(this);
				AddRef();
				return S_OK;
			}

			return E_NOINTERFACE;
		}

		STDMETHODIMP_(ULONG) AddRef() { return ++_references; }
		STDMETHODIMP_(ULONG) Release() { return --_references; }

	private:
		T* Host()
		{
			ATLASSERT(_pHost != NULL);
			return _pHost;
		}

		T* _pHost = nullptr;
		CComPtr<IContextMenu2> _pContextMenu2;
		CComPtr<IDropTarget> _pDropTarget;
		CComPtr<IDataObject> _pDataObject;

		HTREEITEM _hDragOverItem = (HTREEITEM)(ULONG_PTR)0;
		CShellDesktop _pDesktop;
		ULONG _references = 1;
	};
}
