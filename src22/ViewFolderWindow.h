// ImageWalker by Zac Walker
//
// Purpose: The behaviour the items pane and the film strip share: scrolling,
//          keyboard navigation, and which item is being drawn.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewModelItems.h"
#include "iw/winver.h"
#include "iw/palettewindow.h"
#include "ViewModelSearch.h"
#include "FileFormatAny.h"
#include "ImagingDataObject.h"
#include "ViewFolderDrop.h"
#include "ViewFolderLayout.h"
#include "AppThreadFolder.h"
#include "ViewRenameDlg.h"
#include "ViewBackBuffer.h"
#include "ViewToolTip.h"

typedef enum tagFolderDisplayMode
{
	eViewNormal,
	eViewDetail,
	eViewMatrix

} FolderDisplayMode;

template<class T>
class FolderWindowImpl
{
public:

	typedef FolderWindowImpl<T> ThisClass;
	typedef CFolderWindowDropAdapter<T> DragDropType;

	State &_state;
		
	FolderWindowImpl(State &state) :		
		_nDragOverItem(-1),
		_nThumbsX(1),
		_nThumbsY(1),
		_nHoverItem(-1),	
		_sizeThumbImageApplied(0, 0),
		_bDraging(false),
		_state(state)
	{	
		T *pT = static_cast<T*>(this);		

		CComObject<DragDropType> *p; 
		HRESULT hr = CComObject<DragDropType>::CreateInstance(&p);

		CComPtr<DragDropType> pReleaser = p;
		if (FAILED(hr)) throw IW::startup_exception();

		_pDragDrop = p;
		_pDragDrop->_pWindow = pT;
	}

	~FolderWindowImpl()
	{
	}

	int GetMaxTextLengt() const
	{
		return MAX_PATH;
	}

	IW::FolderPtr GetFolder()
	{
		return _state.Folder.GetFolder();
	}	

	const IW::FolderPtr GetFolder() const
	{
		return _state.Folder.GetFolder();
	}

	int GetItemCount() const
	{
		return GetFolder()->GetSize();
	}

	bool HasDescriptions() const
	{
		IW::FolderPtr pFolder = GetFolder();
		int nSize = pFolder->GetSize();

		for(int nItem = 0; nItem < nSize; nItem++)
		{
			if (!pFolder->GetItemDescription(nItem).IsEmpty())
				return true;
		}

		return false;
	}

	BEGIN_MSG_MAP(ThisClass)	

		MESSAGE_HANDLER(WM_CREATE, OnCreate)				
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)	
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)				
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)				
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)				
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)		
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
		MESSAGE_HANDLER(WM_XBUTTONUP, OnXButtonUp)			

		if(ProcessFolderWindowMessage(hWnd, uMsg, wParam, lParam, lResult))
			return TRUE;

	END_MSG_MAP()

public:

	CComPtr<IContextMenu> _pContextMenu;
	CComPtr<IContextMenu2> _pContextMenu2;
	IW::RefPtr<DragDropType> _pDragDrop;
	
	bool _bDraging;						

	int _nDragOverItem;
	int _nThumbsX;	
	int _nThumbsY;	
	int _nHoverItem;	
	CSize _sizeThumbImageApplied;

	CRect _rectStartDrag;
	CString _strNewCreation;

public:

	bool IsMessageWantedForContextMenu(UINT uMsg)
	{
		return (WM_DRAWITEM == uMsg ||
			WM_MEASUREITEM == uMsg ||
			WM_INITMENUPOPUP  == uMsg ||
			WM_MENUCHAR == uMsg);
	}

	BOOL ProcessFolderWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &lResult, DWORD dwMsgMapID = 0)
	{
		// Context menu reflect?
		if (_pContextMenu2 != NULL)
		{
			if (WM_MENUCHAR == uMsg)
			{
				// WM_MENUCHAR needs the LRESULT, which only IContextMenu3 can return
				CComQIPtr<IContextMenu3> pContextMenu3 = _pContextMenu2;

				if (pContextMenu3 != NULL && SUCCEEDED(pContextMenu3->HandleMenuMsg2(uMsg, wParam, lParam, &lResult)))
					return TRUE;

				return FALSE;
			}

			if (IsMessageWantedForContextMenu(uMsg))
			{
				HRESULT hr = _pContextMenu2->HandleMenuMsg(uMsg, wParam, lParam);

				if (SUCCEEDED(hr))
					return TRUE;
			}
		}

		return FALSE;
	}

	IW::FolderItemPtr GetItem(int nItem)
	{
		return GetFolder()->GetItem(nItem);
	}

	void GetItemAttributes(int nItem, IW::FolderItemAttributes &attributes, bool bGetImage = false) const
	{
		GetFolder()->GetItemAttributes(nItem, attributes, bGetImage);
	}

	bool IsItemSelected(int nItem) const
	{
		return GetFolder()->IsItemSelected(nItem);
	}

	int GetFocusItem() const
	{
		IW::FolderPtr pFolder = GetFolder();
		return pFolder->GetFocusItem();
	}

	int GetHoverItem() const
	{
		return _nHoverItem;
	}

	void NextImage()
	{
		IW::FolderPtr pFolder = GetFolder();
		int nFocusItem = pFolder->GetFocusItem();
		int nSize = pFolder->GetSize();

		// Next thumb after selection
		for(int i = nFocusItem + 1; i < nSize; i++)
		{
			if (pFolder->IsItemImage(i))
			{
				Select(pFolder, i, 0, true);
				return;
			}
		}

		// Next thumb before selection. The bound is nFocusItem, not one short of
		// it, or wrapping round skips the image directly before the focus.
		for(int i = 0; i < nFocusItem; i++)
		{
			if (pFolder->IsItemImage(i))
			{
				Select(pFolder, i, 0, true);
				return;
			}
		}


		return;
	}

	void PreviousImage()
	{
		IW::FolderPtr pFolder = GetFolder();
		int nFocusItem = pFolder->GetFocusItem();
		int nSize = pFolder->GetSize();

		if (nFocusItem > 0)
		{
			// Next thumb before selection
			for(int i = nFocusItem - 1; i >= 0; i--)
			{
				if (pFolder->IsItemImage(i))
				{
					Select(pFolder, i, 0, true);
					return;
				}
			}
		}

		// Next thumb after selection
		for(int i = nSize - 1; i > nFocusItem; i--)
		{
			if (pFolder->IsItemImage(i))
			{
				Select(pFolder, i, 0, true);
				return;
			}
		}

		return;
	}

	bool CreateFolder()
	{
		IW::FolderPtr pFolder = GetFolder();

		int i = 1;

		CString strFolderPath = pFolder->GetFolderPath();

		// A virtual namespace folder has no path, and Path::Combine then answers
		// the bare name -- creating "New Folder" beside the executable.
		if (strFolderPath.IsEmpty())
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(IDS_FAILEDTO_CREATE_FOLDER);
			return false;
		}

		CString strNewFolderName = App.LoadString(IDS_NEW_FOLDER);
		CString strNewFolderPath = IW::Path::Combine(strFolderPath, strNewFolderName);		

		while(!CreateDirectory(strNewFolderPath, NULL) && (i < 100))
		{		
			strNewFolderName.Format(IDS_NEW_FOLDER_NUM, i++);
			strNewFolderPath = IW::Path::Combine(strFolderPath, strNewFolderName);
		}

		if (i >= 100)
		{
			IW::CMessageBoxIndirect mb;
			mb.ShowOsErrorWithFile(strNewFolderPath, IDS_FAILEDTO_CREATE_FOLDER);
		}
		else
		{
			_strNewCreation = strNewFolderName;
			SHChangeNotify(SHCNE_MKDIR, SHCNF_PATH, strNewFolderPath, NULL);
		}

		return true;
	}

	bool SaveNewImage(const IW::Image &image)
	{
		T *pT = static_cast<T*>(this);

		IW::FolderPtr pFolder = GetFolder();
		CString strFolderPath = pFolder->GetFolderPath();

		// Just in case?
		if (image.IsEmpty())
			return false;

		IW::ImageLoaderInfoPtr pFactory = pT->_state.Loaders.Find(App.Settings.CaptureFormat);

		// The ini names this format and can name one that is not here.
		if (pFactory == nullptr)
			pFactory = pT->_state.Loaders.Find(g_szJPG);

		if (pFactory == nullptr)
			return false;

		IW::RefPtr<IW::IImageLoader> pLoader = pFactory->Create();

		CString strDot = _T(".");
		CString strNewImageName;
		strNewImageName.LoadString(IDS_NEW_IMAGE);
		strNewImageName += strDot + pFactory->GetExtensionDefault();
		CString strNewImagePath = IW::Path::Combine(strFolderPath, strNewImageName);		

		int nImageNum = 1;

		while(IW::Path::FileExists(strNewImagePath))
		{
			strNewImageName.Format(IDS_NEW_IMAGE_NUM, ++nImageNum);
			strNewImageName += strDot + pFactory->GetExtensionDefault();
			strNewImagePath = IW::Path::Combine(strFolderPath, strNewImageName);
		}		
		
		IW::CFileTemp f;
		if (!f.OpenForWrite(strNewImagePath))
		{
			IW::CMessageBoxIndirect mb;
			mb.ShowOsErrorWithFile(_strNewCreation, IDS_FAILEDTO_CREATE_FILE);
			return false;
		}

		if (!pLoader->Write(g_szEmptyString, &f, image, App.Settings.Codec, IW::CNullStatus::Instance) ||
			!f.Close(IW::CNullStatus::Instance))
		{
			IW::CMessageBoxIndirect mb;
			mb.ShowOsErrorWithFile(_strNewCreation, IDS_FAILEDTO_CREATE_FILE);
			return false;
		}

		_strNewCreation = strNewImageName;
		return true;
	}

	void ResetThumbnailSize()
	{
		T *pT = static_cast<T*>(this);

		const CSize sizeImage = App.Settings._sizeThumbImage;

		if (_sizeThumbImageApplied != sizeImage)
		{
			_sizeThumbImageApplied = sizeImage;
			pT->GetLayout()->Init();
			_state.Folder.RefreshList(true);
		}
	}


	HRESULT SetClipboard(bool bMove)
	{
		IW::FolderPtr pFolder = GetFolder();

		CComPtr<IDataObject> spDataObject;

		HRESULT	hr = pFolder->GetSelectedUIObjectOf(
			IW::GetMainWindow(),
			IID_IDataObject,
			0,
			(LPVOID *)&spDataObject);

		if (SUCCEEDED(hr))
		{
			CComPtr<CImageDataObject> p = IW::CreateComObject<CImageDataObject>();

			p->AggregateDataObject(spDataObject);
			p->SetForMove(bMove);

			int nFocusItem = pFolder->GetFocusItem();

			if (nFocusItem != -1)
			{				
				bool bIsImage = pFolder->IsItemImage(nFocusItem);

				if (bIsImage)
				{
					IW::FolderItemPtr pItem = pFolder->GetItem(nFocusItem);
					if (pItem) p->Cache(pItem->GetFullShellItem());
				}
			}

			OleSetClipboard(p);
		}

		return hr;
	}	
	

	

	void UpdateInvalidatedThumbs()
	{
		T *pT = static_cast<T*>(this);

		IW::FolderPtr pFolder = GetFolder();

		int nSize = pFolder->GetSize();

		for(int i = 0; i < nSize; i++)
		{
			DWORD dw = pFolder->GetItemFlags(i);

			if (dw & THUMB_INVALIDATE)
			{
				// Only invalidate if on screen
				if (pT->IsThumbVisible(pFolder, i))
				{
					pT->InvalidateThumb(pFolder, i);
				}			

				pFolder->ModifyItemFlags(i, THUMB_INVALIDATE, 0);
			}
		}
	}

	void OnThumbsLoaded()
	{
		T *pT = static_cast<T*>(this);

		UpdateInvalidatedThumbs();

		// We should update the scroll sizes
		// has been more than a second since the last update
		if (_state.Folder.IsSearchMode)
		{
			pT->GetLayout()->DoSize();
			pT->UpdateBars();
		}

		return;
	}
	

	void OnTimer()
	{
		T *pT = static_cast<T*>(this);

		InvalidateThumbsMarkedAsInvalidByWorkerThread();
		_pDragDrop->OnTimer();		  
		pT->GetLayout()->OnTimer();

		// May be time to refresh the list
		if (_state.Folder.GetRefreshFolder() < GetTickCount())
		{
			_state.Folder.RefreshList(false);
		}
	}

	void InvalidateThumbsMarkedAsInvalidByWorkerThread()
	{
		T *pT = static_cast<T*>(this);

		IW::FolderPtr pFolder = GetFolder();
		int nCount = pFolder->GetSize();

		// Only invalidate if on screen
		for(int i = 0; i < nCount; i++)
		{
			if (pFolder->IsItemAnimatedImage(i))
			{
				if (pT->IsThumbVisible(pFolder, i))
				{
					if (pFolder->AnimationStep(i))
					{
						pT->InvalidateThumb(pFolder, i);
					}
				}
			}
		}
	}

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		Init();
		bHandled = FALSE;
		return TRUE;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		T *pT = static_cast<T*>(this);
		if (wParam != 0)
		{
			pT->OnPaint((HDC)wParam);		
		}
		else
		{
			CPaintDC dc(pT->m_hWnd);
			pT->OnPaint((HDC)dc);
		}

		return 0;
	}

	void OnPaint(CDCHandle dc)
	{
		T *pT = static_cast<T*>(this);
	}	


	void Init()
	{
		// Register for drop and drag
		_pDragDrop->RegisterDropTarget();
	}

	void DrawFolder(IW::CRender &render, const CRect &rectClip)
	{
		T *pT = static_cast<T*>(this);

		
		IW::FolderPtr pFolder = GetFolder();

		if (pFolder->GetSize())
		{
			const CPoint pointOffset = pT->GetScrollOffset();
			int nFocusItem = pFolder->GetFocusItem();
			T::LayoutType *pLayout = pT->GetLayout();

			CRect rectClipLogical(rectClip);
			rectClipLogical.OffsetRect(pointOffset);

			int nFirstItem = pLayout->ClosestThumbFromPoint(rectClipLogical.TopLeft());
			int nLastItem =  pLayout->ClosestThumbFromPoint(rectClipLogical.BottomRight());

			pFolder->SetLoadItem(nFirstItem);

			for(int i = nFirstItem; i <= nLastItem; i++)
			{
				if (i != nFocusItem && i != _nHoverItem)
				{					
					pLayout->DrawThumb(render, *pFolder, i, rectClip);
				}
			}

			if (nFocusItem <= nLastItem && nFocusItem != -1)
			{
				pLayout->DrawThumb(render, *pFolder, nFocusItem, rectClip);
			}

			if (_nHoverItem <= nLastItem && _nHoverItem != -1)
			{
				pLayout->DrawThumb(render, *pFolder, _nHoverItem, rectClip);
			}
		}
		else
		{
			CRect rectClient;
			pT->GetClientRect(&rectClient);

			CString str;
			str.LoadString(_state.Folder.IsSearchMode ? IDS_NO_MATCH : IDS_EMPTYFOLDER);

			render.DrawString(str, rectClient, IW::Style::Font::Standard, IW::Style::Text::CenterInRect, IW::Style::Color::WindowText);
		}
	}

	int GetPageCount(bool bImagesOnly, bool bSelectedImagesOnly)
	{
		IW::FolderPtr pFolder = GetFolder();

		if (bSelectedImagesOnly)
		{
			int nCount = 0;
			int nSize = pFolder->GetSize();

			for(int i = 0; i < nSize; i++)
			{
				UINT u = pFolder->GetItemFlags(i);

				if (u & THUMB_IMAGE && 
					u & THUMB_SELECTED)
				{
					nCount++;

				}
			}

			return IW::iceil(nCount, App.Settings._sizeRowsColumns.cy * App.Settings._sizeRowsColumns.cx);
		}

		if (bImagesOnly)
		{
			return IW::iceil(pFolder->GetImageCount(), App.Settings._sizeRowsColumns.cy * App.Settings._sizeRowsColumns.cx);
		}

		return IW::iceil(pFolder->GetSize(), App.Settings._sizeRowsColumns.cy * App.Settings._sizeRowsColumns.cx);
	}


	


	void ResetCounters()
	{
		IW::FolderPtr pFolder = GetFolder();

		pFolder->ResetCounters();

		_nDragOverItem = -1;
		_nHoverItem = -1;	
	}


	LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		T *pT = static_cast<T*>(this);

		CPoint point( GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		bool bHandled = false;
		pT->Accept(FrameVisitorMouseLeftButtonDown(point, bHandled));

		if (!bHandled)
		{
			IW::FolderPtr pFolder = GetFolder();
			pT->SetFocus();

			// Work out the drag rect
			SetRect(&_rectStartDrag, point.x - 5, point.y - 5, point.x + 5, point.y + 5);
			pT->ClientToScreen(&_rectStartDrag);

			const CPoint pointOffset = pT->GetScrollOffset();
			point += pointOffset;

			int i = pT->GetLayout()->ThumbFromPoint(point);

			if (i < 0)
				return 0;

			bool bDrag = false;
			bool bMayStillNeedSelect = true;

			if (!pFolder->IsItemSelected(i))
			{
				_state.Folder.Select(i, static_cast<UINT>(wParam), true);
				bMayStillNeedSelect = false;
			}

			// Do we want to do a drag and drop?
			const CPoint pointThumb = pT->GetLayout()->GetThumbPoint(i);
			const CRect  rectThumb(pointThumb, pT->GetLayout()->_sizeThumb);	

			if (_pDragDrop->OnBeginDrag() && pFolder->HasSelection())
			{
				CComPtr<IDataObject> spDataObject;

				HRESULT	hr = pFolder->GetSelectedUIObjectOf(
					IW::GetMainWindow(),
					IID_IDataObject,
					0,
					(LPVOID *)&spDataObject);

				if (SUCCEEDED(hr))
				{
					// call global OLE api to do the drag drop
					CComQIPtr<IDropSource> spDropSource(_pDragDrop);

					DWORD dwResultEffect = DROPEFFECT_NONE;
					DWORD dwEffects = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;

					_bDraging = true;

					CComPtr<CImageDataObject> p = IW::CreateComObject<CImageDataObject>();
					p->AggregateDataObject(spDataObject);

					hr = ::DoDragDrop(p, spDropSource, dwEffects, &dwResultEffect);

					_bDraging = false;

					// Failure in drag drop?
					ATLASSERT(SUCCEEDED(hr));

					bDrag = (DRAGDROP_S_DROP == hr) && (dwResultEffect != DROPEFFECT_NONE);

				}
			}

			if (!bDrag && bMayStillNeedSelect)
			{
				_state.Folder.Select(i, static_cast<UINT>(wParam), true);
			}
		}

		return 0;
	}

	LRESULT OnXButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		int fwButton = GET_XBUTTON_WPARAM (wParam); 

		switch(fwButton)
		{
		case XBUTTON1:
			Left();
			break;
		case XBUTTON2:
			Right();
			break;
		}

		return 0;
	}

	LRESULT OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		OnLButtonDblClk(point, static_cast<UINT>(wParam));
		return 0;
	}

	void OnLButtonDblClk(const CPoint &point, UINT nFlags)
	{		
		T *pT = static_cast<T*>(this);

		const CPoint pointOffset = pT->GetScrollOffset();
		const CPoint pointThumb(point.x + pointOffset.x, point.y + pointOffset.y);

		int i = pT->GetLayout()->ThumbFromPoint(pointThumb);

		if (i >= 0)
		{				
			_state.Folder.Select(i, nFlags, true);
			OpenFocusItem(true);
		}
	}

	void OpenFocusItem(bool bCanOpenFullScreen, bool bAlwaysFullScreen = false)
	{
		T *pT = static_cast<T*>(this);

		IW::FolderPtr pFolder = GetFolder();

		int nSelected = pFolder->GetFocusItem();
		if (nSelected == -1)
			return;

		ULONG ulAttribs = pFolder->GetItemAttribs(nSelected);

		bool bIsLink = (ulAttribs & SFGAO_LINK) != 0;
		bool bIsImage = pFolder->IsItemImage(nSelected);

		if (bCanOpenFullScreen &&
			(bAlwaysFullScreen || App.Settings.m_bDoubleClickShowsFullScreen) &&
			bIsImage &&
			!bIsLink)
		{
			pT->GetCoupling()->ViewFullScreen(true);
		}
		else
		{
			_state.Folder.OnFileDefault();
		}
	}

	void GetThumbText(CString &strOut, int nItem, bool bFormat)
	{
		GetFolder()->GetItemFormatText(nItem, strOut, App.Settings.m_annotations, bFormat);
	}

	void GetThumbText(CSimpleArray<CString> &arrayStrOut, int nItem, bool bFormat)
	{
		GetFolder()->GetItemFormatText(nItem, arrayStrOut, App.Settings.m_columns, bFormat);
	}

	CString GetItemDescription(int nItem) const
	{
		return GetFolder()->GetItemDescription(nItem);
	}

	void SetScrollSizeList(bool bChangeOffset)
	{
		T *pT = static_cast<T*>(this);
		pT->GetLayout()->SetScrollSizeList(bChangeOffset);
		pT->Invalidate();
	}

	void MakeItemVisible(int nThumb)
	{
		T *pT = static_cast<T*>(this);
		pT->GetLayout()->MakeItemVisible(nThumb);
	}

	CRect GetImageRect(int i)
	{
		T *pT = static_cast<T*>(this);

		IW::FolderPtr pFolder = GetFolder();

		const CPoint pointThumb = pT->GetLayout()->GetThumbPoint(i);	

		CRect rectThumb(pointThumb, pT->GetLayout()->_sizeThumb);
		const CSize sizeCenter = pT->GetLayout()->_sizeThumbCenter;
		CRect rectImage;

		if (pFolder->IsItemImage(i))
		{
			const CRect rc = pFolder->GetItemThumbRect(i);

			int cx = rc.Width();
			int cy = rc.Height();

			rectImage.left = rectThumb.left + sizeCenter.cx - (cx / 2);
			rectImage.top = rectThumb.top + sizeCenter.cy - (cy / 2);
			rectImage.right = rectImage.left + cx;
			rectImage.bottom = rectImage.top + cy;
		}
		else
		{
			rectImage.left = rectThumb.left + sizeCenter.cx - (32 / 2);
			rectImage.top = rectThumb.top + sizeCenter.cy - (32 / 2);
			rectImage.right = rectImage.left + 32;
			rectImage.bottom = rectImage.top + 32;
		}

		return rectImage;
	}

	void Exit()
	{		
		_pDragDrop->RevokeDropTarget();		
	}


	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		ATLTRACE(_T("Destroy CFolderWindow\n")); 
		Exit();
		bHandled = false;
		return 0;
	}	

	LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		T *pT = static_cast<T*>(this);
		CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		SetHoverItem(pT->GetLayout()->ThumbFromPoint(point + pT->GetScrollOffset()));			
		bHandled = FALSE;
		return 0;    
	}

	LRESULT OnMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		SetHoverItem(-1);
		bHandled = FALSE;
		return 0;
	}

	void SetHoverItem(int i)
	{
		T *pT = static_cast<T*>(this);

		if (_nHoverItem != i)
		{
			IW::FolderPtr pFolder = GetFolder();

			if (_nHoverItem != -1) 
			{				
				pT->InvalidateThumb(pFolder, _nHoverItem);
			}

			// Use Activate() to hide the tooltip.
			pT->ActivateTooltip(false);
			_nHoverItem = i;

			pT->GetLayout()->HoverChanged(_nHoverItem);

			if (i != -1)
			{
				pT->InvalidateThumb(pFolder, i);
				pT->ActivateTooltip(true);
			}
		}
	}

	LRESULT OnRButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		T *pT = static_cast<T*>(this);
		CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		IW::FolderPtr pFolder = GetFolder();		
		pT->SetFocus();

		const CPoint pointOffset = pT->GetScrollOffset();
		const CPoint pointThumb(point.x + pointOffset.x, point.y + pointOffset.y);

		int i = pT->GetLayout()->ThumbFromPoint(pointThumb);

		if (i != -1)
		{
			if (!(pFolder->IsItemSelected(i)))
			{
				_state.Folder.Select(i, static_cast<UINT>(wParam), true);
			}
		}

		pT->MapWindowPoints(HWND_DESKTOP, &point, 1);

		CMenu menuContext;
		menuContext.LoadMenu(IDR_POPUP_FOLDER);
		CMenuHandle menuPopup(menuContext.GetSubMenu(0));
		pT->GetCoupling()->TrackPopupMenu(menuPopup, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y);

		return 0;
	}
	
	static int GetKeyFlags()
	{
		int nFlags = 0;		

		if (GetAsyncKeyState(VK_CONTROL) < 0) nFlags |= MK_CONTROL;
		if (GetAsyncKeyState(VK_SHIFT) < 0) nFlags |= MK_SHIFT;

		return nFlags;
	}

	void Home()
	{
		T *pT = static_cast<T*>(this);
		_state.Folder.Select(pT->GetLayout()->NearestItem(0), GetKeyFlags(), true);
	}

	void End()
	{
		T *pT = static_cast<T*>(this);
		IW::FolderPtr pFolder = GetFolder();
		int nSel = pT->GetLayout()->NearestItem(pFolder->GetSize() - 1);
		_state.Folder.Select(nSel, GetKeyFlags(), true);
	}

	void PageUp()
	{
		T *pT = static_cast<T*>(this);
		IW::FolderPtr pFolder = GetFolder();

		if (pT->GetLayout()->IsSingleRow())
		{
			StepPage(-1);
			return;
		}

		const int nRows = IW::LowerLimit<1>(pT->GetClientSize().cy / IW::LowerLimit<1>(pT->GetLayout()->_sizeThumb.cy));
		int nFocusItem = pFolder->GetFocusItem();
		int nSel = nFocusItem - (_nThumbsX * nRows);

		if (nSel < 0)
		{
			nSel = 0;
		}

		_state.Folder.Select(nSel, GetKeyFlags(), true);

	}

	void PageDown()
	{
		T *pT = static_cast<T*>(this);
		IW::FolderPtr pFolder = GetFolder();

		if (pT->GetLayout()->IsSingleRow())
		{
			StepPage(1);
			return;
		}

		const int nRows = IW::LowerLimit<1>(pT->GetClientSize().cy / IW::LowerLimit<1>(pT->GetLayout()->_sizeThumb.cy));
		int nFocusItem = pFolder->GetFocusItem();
		int nSel = nFocusItem + (_nThumbsX * nRows);

		if (nSel > (pFolder->GetSize() -1))
		{
			nSel = pFolder->GetSize() - 1;
		}

		_state.Folder.Select(nSel, GetKeyFlags(), true);
	}

	// A screenful along a single row of thumbnails.
	void StepPage(int nDirection)
	{
		T *pT = static_cast<T*>(this);
		IW::FolderPtr pFolder = GetFolder();

		const int nStep = IW::LowerLimit<1>(
			pT->GetClientSize().cx / IW::LowerLimit<1>(pT->GetLayout()->_sizeThumb.cx));

		int nSel = pFolder->GetFocusItem();

		for (int i = 0; i < nStep; i++)
			nSel = pT->GetLayout()->StepItem(nSel, nDirection);

		_state.Folder.Select(nSel, GetKeyFlags(), true);
	}

	void Left()
	{
		T *pT = static_cast<T*>(this);
		IW::FolderPtr pFolder = GetFolder();
		int nSel = pT->GetLayout()->StepItem(pFolder->GetFocusItem(), -1);
		_state.Folder.Select(nSel, GetKeyFlags(), true);
	}

	void Right()
	{
		T *pT = static_cast<T*>(this);
		IW::FolderPtr pFolder = GetFolder();
		int nSel = pT->GetLayout()->StepItem(pFolder->GetFocusItem(), 1);
		_state.Folder.Select(nSel, GetKeyFlags(), true);
	}

	void Up()
	{
		T *pT = static_cast<T*>(this);

		// One row of thumbnails has no row above it, so up and down step along it.
		if (pT->GetLayout()->IsSingleRow())
		{
			Left();
			return;
		}

		IW::FolderPtr pFolder = GetFolder();
		int nSelected = pFolder->GetFocusItem();
		int nSel = nSelected - _nThumbsX;
		_state.Folder.Select(nSel, GetKeyFlags(), true);
	}

	void Down()
	{
		T *pT = static_cast<T*>(this);

		if (pT->GetLayout()->IsSingleRow())
		{
			Right();
			return;
		}

		IW::FolderPtr pFolder = GetFolder();
		int nSelected = pFolder->GetFocusItem();
		const int nLast = pFolder->GetSize() - 1;
		int nSel = nSelected + _nThumbsX;
		if (nSel > nLast && nSelected < nLast) nSel = nLast;
		_state.Folder.Select(nSel, GetKeyFlags(), true);
	}



	LRESULT OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		int nChar = (int) wParam;
		OnKeyDown(nChar);
		return 0;
	}



	void Invoke(LPCSTR szVerb, BOOL bFolder)
	{
		IW::FolderPtr pFolder = GetFolder();
		CComPtr<IContextMenu> spContextMenu;

		HRESULT hr;

		if (bFolder || !pFolder->HasSelection())
		{
			hr = pFolder->CreateViewObject(
				IW::GetMainWindow(),
				IID_IContextMenu, 
				(LPVOID *)&spContextMenu);
		}
		else 
		{
			hr = pFolder->GetSelectedUIObjectOf(
				IW::GetMainWindow(),
				IID_IContextMenu,
				0,
				(LPVOID*)&spContextMenu);
		}


		if (SUCCEEDED(hr))
		{

			CMINVOKECOMMANDINFO cmi;

			cmi.cbSize = sizeof(CMINVOKECOMMANDINFO);
			cmi.fMask  = 0;
			cmi.hwnd   = IW::GetMainWindow();
			cmi.lpVerb = szVerb;
			cmi.lpParameters = NULL;
			cmi.lpDirectory  = NULL;
			cmi.nShow        = SW_SHOWNORMAL;
			cmi.dwHotKey     = 0;
			cmi.hIcon        = NULL;

			hr = spContextMenu->InvokeCommand(&cmi);

			// Did the menu invoke occur correctly?
			ATLASSERT(SUCCEEDED(hr));

		}

	}


	bool IsThumbVisible(IW::Folder *pFolder, int nThumb) const
	{
		const T *pT = static_cast<const T*>(this);

		const CRect rectThumb = pT->GetLayout()->GetThumbRect(nThumb);
		const CRect rectScreen(pT->GetScrollOffset(), pT->GetClientSize());

		CRect rectIntersect;
		return rectIntersect.IntersectRect(rectThumb, rectScreen) != FALSE;
	}

private:


	

	void OnKeyDown(int nChar)
	{
		T *pT = static_cast<T*>(this);
		
		switch(nChar)
		{
		case VK_RETURN:
			// Enter is "show it": a picture goes full screen, a folder opens.
			pT->OpenFocusItem(GetAsyncKeyState(VK_CONTROL) >= 0, true);
			return;
		case VK_PRIOR:
			pT->PageUp();
			return;
		case VK_NEXT:
			pT->PageDown();
			return;
		case VK_END:
			pT->End();
			return;
		case VK_HOME:
			pT->Home();
			return;
		case VK_LEFT:
			pT->Left();
			return;
		case VK_UP:
			pT->Up();
			return;
		case VK_RIGHT:
			pT->Right();
			return;
		case VK_DOWN:
			pT->Down();
			return;
		default:
			break;
		}

		// Screen out any control chars as they are probably
		// accelerators
		if (GetAsyncKeyState(VK_CONTROL) >= 0 && 
			(('0' <= nChar &&  '9' >= nChar) ||
			('A' <= nChar &&  'Z' >= nChar)))
		{
			IW::FolderPtr pFolder = GetFolder();

			const int nSelected = pFolder->GetFocusItem();
			const int nFirst = (nSelected == -1) ? 0 : nSelected + 1;
			const int nSize = pFolder->GetSize();

			for(int i = nFirst; i < nSize; i++)
			{
				CString str = pFolder->GetItemName(i);

				if (_totupper(str[0]) == nChar)
				{
					_state.Folder.Select(i, 0, true);
					return;
				}
			}

			for(int i = 0; i < nSize; i++)
			{
				CString str = pFolder->GetItemName(i);

				if (_totupper(str[0]) == nChar)
				{
					_state.Folder.Select(i, 0, true);
					return;
				}
			}
		}
	}	
};
