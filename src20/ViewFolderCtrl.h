// ImageWalker by Zac Walker
//
// Purpose: The items pane window - painting, hit testing, selection, drag
//          and drop and its context menu.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewFolderWindow.h"
#include "ViewBase.h"

namespace IW
{
	class IW::Folder;
}

class CFolderCtrl : 
	public CWindowImpl<CFolderCtrl>,
	public CView<CFolderCtrl>,
	public CScrollImpl<CFolderCtrl>,
	public CPaletteImpl<CFolderCtrl>,
	public FolderWindowImpl<CFolderCtrl>
{
public:

	typedef CFolderCtrl ThisClass;
	typedef CWindowImpl<ThisClass> WindowType;
	typedef ToolTipWindowImpl<ThisClass> ToolTipType;
	typedef FolderLayout<ThisClass> LayoutType;
	typedef CScrollImpl<ThisClass> ScrollType;

	Coupling *_pCoupling;
	State &_state;	
	CPoint _pointScreenOrigin;

	FolderLayoutNormal<ThisClass> _layoutNormal;
	FolderLayoutDetail<ThisClass> _layoutDetail;
	FolderLayoutMatrix<ThisClass> _layoutMatrix;
	FolderLayoutStrip<ThisClass> _layoutStrip;

	LayoutType *_pLayout;	
	FolderDisplayMode _displayMode;

	// 2.2's detail view is a real header control - drag to reorder, track to
	// resize, click to sort - where 2.3 draws "label: value" pairs per row.
	CHeaderCtrl _header;

	// The right-hand gutter: a real scroll bar with the view-mode strip stacked
	// under it, exactly one scroll bar wide. 2.3 put that strip in the status
	// bar; in this generation each pane carries its own.
	CScrollBar _scrollV;
	CToolBarCtrl _toolbarViews;
	CImageList _viewImages;

	enum { kHeaderId = 0x7f30 };

	// Full screen borrows the pane for a filmstrip. It is not a view mode: no
	// command selects it, and _displayMode keeps whatever the user chose.
	bool _bFilmStrip;

	FadeOverlay _fade;

	
	CFolderCtrl(Coupling *pCoupling, State &state) : 
		FolderWindowImpl<CFolderCtrl>(state),
		_state(state),
		_pCoupling(pCoupling),
		_displayMode(eViewMatrix),
		_bFilmStrip(false),
		_pLayout(&_layoutMatrix),
		_layoutNormal(*this),
		_layoutDetail(*this),
		_layoutMatrix(*this),
		_layoutStrip(*this),
		_pointScreenOrigin(0,0)
	{	
		GetLayout()->Init();
		SetScrollExtendedStyle(0);
	}

	~CFolderCtrl()
	{
	}	

	LayoutType *GetLayout()
	{
		return _pLayout;
	}

	const LayoutType *GetLayout() const
	{
		return _pLayout;
	}

	Coupling *GetCoupling()
	{
		return _pCoupling;
	}

	CSize GetThumbnailSize() const
	{
		return _pCoupling->GetThumbnailSize();
	}

	const Coupling *GetCoupling() const
	{
		return _pCoupling;
	}

	void OnFolderChanged()
	{
		ScrollTop();
		SetScrollSizeList(true);
		UpdateBars();
		ResetCounters();
		UpdateScrollSize();
		Invalidate();
	}

	void OnFolderRefresh() 
	{
		SetScrollSizeList(false);

		IW::FolderPtr pFolder = GetFolder();
		int nNewFocus = pFolder->GetFocusItem();

		if (nNewFocus != -1)
		{
			MakeItemVisible(nNewFocus);
		}

		if (!_strNewCreation.IsEmpty())
		{	
			if (_state.Folder.Select(_strNewCreation))
			{
				_strNewCreation.Empty();
			}
		}


		ResetCounters();
		Invalidate();
		UpdateBars();
	}

	void OnResetFrames()
	{
		ShowFrames();
		SizeClients();
		GetLayout()->DoSize();
		UpdateBars();
		Invalidate();
	}

	void ShowFrames()
	{
		Invalidate();
	}

	void OnActivate()
	{
	}


	void OnCommand(WORD id)
	{
		_pCoupling->Command(id);
	}	

	// Anything the ini or a caller offers that is not one of the three the user
	// can pick becomes the default -- a run that ended in full screen used to
	// write the filmstrip out and reopen as a single row that could not wrap.
	static FolderDisplayMode ValidateViewMode(int nMode)
	{
		switch (nMode)
		{
		case eViewNormal:
		case eViewDetail:
		case eViewMatrix:
			return static_cast<FolderDisplayMode>(nMode);
		}

		return eViewMatrix;
	}

	void SetViewMode(FolderDisplayMode eMode)
	{
		_displayMode = ValidateViewMode(eMode);
		ApplyLayout(true);
	}

	void SetFilmStrip(bool bFilmStrip)
	{
		if (_bFilmStrip == bFilmStrip)
			return;

		_bFilmStrip = bFilmStrip;
		ApplyLayout(true);
	}

	bool IsFilmStrip() const
	{
		return _bFilmStrip;
	}

	// The cross-fade says "the same items, arranged differently". A sort, a
	// thumbnail-size change or an options change is not that, so only the two
	// callers that switch layout ask for it.
	void ApplyLayout(bool bCaptureFade = false)
	{
		_pLayout = _bFilmStrip ? static_cast<LayoutType*>(&_layoutStrip) : ModeEnumToObject(_displayMode);
		
		if (bCaptureFade && App.Settings.m_bUseEffects) _fade.Capture(m_hWnd);

		m_ptOffset = CPoint(0,0);

		// Every layout, hit test and scroll size is measured from the origin, so
		// reserving the header's height here is all it takes to make room for it.
		UpdateHeader();

		GetLayout()->Init();

		SetScrollSizeList(true);

		IW::FolderPtr pFolder = GetFolder();
		int nFocusItem = pFolder->GetFocusItem();

		if (nFocusItem != -1)
		{
			MakeItemVisible(nFocusItem);
		}

		ResetCounters();
		Invalidate();
	}

	LayoutType *ModeEnumToObject(FolderDisplayMode eMode)
	{
		switch(eMode)
		{
		case eViewNormal:
			return &_layoutNormal;
		case eViewDetail:
			return &_layoutDetail;
		case eViewMatrix:
			return &_layoutMatrix;
		}

		return &_layoutNormal; // Default
	}

	void UpdateBars()
	{
		// Update the scroll bar
		SCROLLINFO si;
		IW::MemZero(&si, sizeof(SCROLLINFO));
		si.cbSize = sizeof(si);
		si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
		si.nMin = 0;

		si.nMax = m_sizeAll.cx - 1;
		si.nPage = m_sizeClient.cx;
		si.nPos = m_ptOffset.x;
		SetScrollInfo(SB_HORZ, &si, true);

		si.nMax = m_sizeAll.cy - 1;
		si.nPage = m_sizeClient.cy;
		si.nPos = m_ptOffset.y;
		SetScrollInfo(SB_VERT, &si, true);
	}

	bool AdjustScrollOffset(int& x, int& y)
	{
		const CSize sizeThumb = GetLayout()->_sizeThumb;

		int xOld = x;
		int yOld = y;

		int cxMax = m_sizeAll.cx - m_sizeClient.cx;
		int cyMax = m_sizeAll.cy - m_sizeClient.cy;
		int cxMin = IW::UpperLimit<0>(((_nThumbsX * sizeThumb.cx) - m_sizeClient.cx) / 2);
		int cyMin = 0;

		if(x > cxMax)
			x = (cxMax >= cxMin) ? cxMax : cxMin;
		else if(x < cxMin)
			x = cxMin;

		if(y > cyMax)
			y = (cyMax >= cyMin) ? cyMax : cyMin;
		else if(y < cyMin)
			y = cyMin;

		return (x != xOld || y != yOld);
	}

	void UpdateScrollSize()
	{
		IW::FolderPtr pFolder = GetFolder();
		const CSize sizeThumb = GetLayout()->_sizeThumb;

		int y = ((pFolder->GetSize() - 1) / _nThumbsX)*sizeThumb.cy + sizeThumb.cy;

		y = IW::LowerLimit<1>(y);

		if (y != m_sizeAll.cy)
		{
			CRect rectClient(m_ptOffset, m_sizeClient);
			CRect rectAdded(0, m_sizeAll.cy, m_sizeAll.cx, y);

			CRect rectIntersect;
			if (rectIntersect.IntersectRect(rectClient, rectAdded))
				Invalidate();

			m_sizeAll.cy = y;

			UpdateBars();
		}
	}

	void InvalidateThumb(int nThumb)
	{
		IW::FolderPtr pFolder = GetFolder();
		InvalidateThumb(pFolder, nThumb);
	}


	void InvalidateThumb(IW::Folder *pFolder, int nThumb)
	{
		if (IsThumbVisible(pFolder, nThumb))
		{			
			CRect rectThumb = GetLayout()->GetThumbRect(nThumb);
			rectThumb.OffsetRect(-m_ptOffset.x, -m_ptOffset.y);
			rectThumb.InflateRect(3,3);
			InvalidateRect(rectThumb);
		}
	}

	CString GetToolTipText(UINT_PTR idCtrl)
	{
		if(App.Settings.m_bShowToolTips &&  
			(_nHoverItem != -1) &&
			(m_hWnd == reinterpret_cast<HWND>(idCtrl)))
		{
			CString str;
			IW::FolderPtr pFolder = GetFolder();
			return pFolder->GetToolTip(_nHoverItem);
		}

		return ToolTipType::GetToolTipText(idCtrl);
	}

	CPoint GetScreenOrigin() const
	{
		return _pointScreenOrigin;
	}

	// The header exists only in detail mode. Its columns are the thumbnail plus
	// whatever the user's column list holds, which is the same list the rows
	// draw from, so the two cannot drift.
	void UpdateHeader()
	{
		const bool bShow = !_bFilmStrip && _displayMode == eViewDetail;

		_pointScreenOrigin.y = 0;

		if (_header.m_hWnd == nullptr)
			return;

		if (!bShow)
		{
			_header.ShowWindow(SW_HIDE);
			return;
		}

		while (_header.GetItemCount() > 0)
			_header.DeleteItem(0);

		InsertColumn(_T("Image"), GetThumbnailSize().cx + (THUMB_PADDING * 4), HDF_LEFT);

		IW::CArrayDWORD &columns = App.Settings.m_columns;

		for (int i = 0; i < columns.GetSize(); i++)
			InsertColumn(App.GetMetaDataShortTitle(columns[i]), 140, HDF_LEFT);

		CRect rectClient;
		GetClientRect(rectClient);
		rectClient.right = IW::Max(rectClient.left, rectClient.right - GutterWidth());

		WINDOWPOS wp = {};
		HDLAYOUT hdl = {&rectClient, &wp};

		if (_header.Layout(&hdl))
		{
			_header.SetWindowPos(HWND_TOP, wp.x, wp.y, wp.cx, wp.cy, wp.flags | SWP_SHOWWINDOW);
			_pointScreenOrigin.y = wp.cy;
		}
	}

	void InsertColumn(LPCTSTR szTitle, int cx, int nFormat)
	{
		HDITEM hdi = {};
		hdi.mask = HDI_TEXT | HDI_FORMAT | HDI_WIDTH;
		hdi.pszText = const_cast<LPTSTR>(szTitle);
		hdi.cchTextMax = lstrlen(szTitle);
		hdi.fmt = nFormat | HDF_STRING;
		hdi.cxy = cx;

		_header.InsertItem(_header.GetItemCount(), &hdi);
	}

	// Column left edge and width in client coordinates, for the row painter.
	bool GetColumnRect(int nColumn, CRect &rect) const
	{
		if (_header.m_hWnd == nullptr || nColumn >= _header.GetItemCount())
			return false;

		return const_cast<CHeaderCtrl&>(_header).GetItemRect(nColumn, rect) != FALSE;
	}

	int GetColumnCount() const
	{
		return _header.m_hWnd == nullptr ? 0 : const_cast<CHeaderCtrl&>(_header).GetItemCount();
	}

	// Column 0 is the thumbnail and has no sort order of its own.
	LRESULT OnHeaderClick(int, LPNMHDR pnmh, BOOL&)
	{
		auto pHeader = reinterpret_cast<LPNMHEADER>(pnmh);
		IW::CArrayDWORD &columns = App.Settings.m_columns;

		const int nColumn = pHeader->iItem - 1;

		if (nColumn >= 0 && nColumn < columns.GetSize())
			SetSortOrder(columns[nColumn]);

		return 0;
	}

	LRESULT OnHeaderChanged(int, LPNMHDR, BOOL&)
	{
		Invalidate();
		return 0;
	}

	CPoint GetScrollOffset() const
	{
		return m_ptOffset;
	}

	HWND GetHWnd()
	{
		return m_hWnd;
	}

	const CSize GetClientSize() const 
	{ 
		return m_sizeClient; 
	}	

	const CSize GetSizeAll() const 
	{ 
		return m_sizeAll; 
	}

	BEGIN_MSG_MAP(CFolderCtrl)
		NOTIFY_HANDLER(kHeaderId, HDN_ITEMCLICK, OnHeaderClick)
		NOTIFY_HANDLER(kHeaderId, HDN_ITEMCHANGED, OnHeaderChanged)
		NOTIFY_HANDLER(kHeaderId, HDN_ENDDRAG, OnHeaderChanged)
		MESSAGE_HANDLER(WM_COMMAND, OnGutterForward)
		MESSAGE_HANDLER(WM_NOTIFY, OnGutterForward)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)

		CHAIN_MSG_MAP(CView<ThisClass>)
		CHAIN_MSG_MAP(FolderWindowImpl<ThisClass>)
		CHAIN_MSG_MAP(ScrollType)
		CHAIN_MSG_MAP(CPaletteImpl<ThisClass>)

	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		_header.Create(m_hWnd, rcDefault, nullptr,
		               WS_CHILD | HDS_BUTTONS | HDS_DRAGDROP | HDS_FULLDRAG | HDS_HORZ,
		               0, kHeaderId);
		_header.SetFont(AtlGetStockFont(DEFAULT_GUI_FONT));

		CreateGutter();

		ApplyLayout();
		ShowFrames();

		_state.Folder.ChangedDelegates.Bind(this, &ThisClass::OnFolderChanged);
		_state.Folder.RefreshDelegates.Bind(this, &ThisClass::OnFolderRefresh);		
		_state.Folder.SelectionDelegates.Bind(this, &ThisClass::OnSelectionChanged);
		_state.Folder.FocusDelegates.Bind(this, &ThisClass::OnFocusChanged);
		_state.ResetFrames.Bind(this, &ThisClass::OnResetFrames);

		bHandled = false;
		return 0;
	}	

	void CreateGutter()
	{
		_scrollV.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | SBS_VERT,
		                0, ID_FOLDER_SCROLL);

		// iString is only ever an index into a string pool this toolbar does not
		// have. SetMaxTextRows(0) below is what actually stops comctl32 drawing
		// from it -- without it a stray glyph appeared beside the detail button.
		static const TBBUTTON tb[] =
		{
			{0, ID_THUMBNAILS_MATRIX, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0L, 0},
			{1, ID_THUMBNAILS_THUMBNAIL, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0L, 0},
			{2, ID_THUMBNAILS_DETAIL, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0L, 0},
			{3, ID_VIEW_ARRANGEICONS, TBSTATE_ENABLED, BTNS_DROPDOWN, {0}, 0L, 0}
		};

		// No TBSTYLE_FLAT: comctl32 draws a flat toolbar transparently, and with
		// the pane clipping its children nothing ever paints behind it -- the
		// strip kept whatever pixels were last there. 2.0's strip is not flat
		// either, so this is also what the tree is meant to look like.
		_toolbarViews.Create(m_hWnd, rcDefault, nullptr,
		                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		                     CCS_VERT | CCS_RIGHT | CCS_NOMOVEX | CCS_NOMOVEY |
		                     CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN |
		                     TBSTYLE_WRAPABLE | TBSTYLE_TOOLTIPS,
		                     0, ID_FOLDER_VIEWBAR);

		_toolbarViews.SetButtonStructSize(sizeof(TBBUTTON));
		_toolbarViews.SetMaxTextRows(0);

		_viewImages.Create(IDB_FOLDER_VIEW_BUTTONS, 8, 1, RGB(255, 0, 255));
		_toolbarViews.SetImageList(_viewImages);
		_toolbarViews.AddButtons(_countof(tb), const_cast<LPTBBUTTON>(tb));

		const int cx = ::GetSystemMetrics(SM_CXVSCROLL);
		_toolbarViews.SetBitmapSize(CSize(8, 8));
		_toolbarViews.SetButtonSize(CSize(cx, cx));
	}

	// Zero while the pane is a film strip -- that scrolls sideways and has no
	// room for a view-mode choice it does not offer -- and in full screen.
	int GutterWidth() const
	{
		if (_bFilmStrip)
			return 0;

		if (_pCoupling != nullptr && !_pCoupling->CanShowGutterItem(ID_FOLDER_SCROLL))
			return 0;

		return ::GetSystemMetrics(SM_CXVSCROLL);
	}

	// The strip is a real scroll bar, so everything CScrollImpl says about the
	// vertical bar is said to it instead of to the window's own non-client one.
	BOOL SetScrollInfo(int nBar, LPSCROLLINFO lpsi, BOOL bRedraw = TRUE)
	{
		if (nBar == SB_VERT && _scrollV.m_hWnd != nullptr)
			return _scrollV.SetScrollInfo(lpsi, bRedraw);

		return CWindow::SetScrollInfo(nBar, lpsi, bRedraw);
	}

	BOOL GetScrollInfo(int nBar, LPSCROLLINFO lpsi)
	{
		if (nBar == SB_VERT && _scrollV.m_hWnd != nullptr)
			return _scrollV.GetScrollInfo(lpsi);

		return CWindow::GetScrollInfo(nBar, lpsi);
	}

	int SetScrollPos(int nBar, int nPos, BOOL bRedraw = TRUE)
	{
		if (nBar == SB_VERT && _scrollV.m_hWnd != nullptr)
			return _scrollV.SetScrollPos(nPos, bRedraw);

		return CWindow::SetScrollPos(nBar, nPos, bRedraw);
	}

	// The strip posts to its own parent, which is this pane; the frame owns
	// these commands and the arrange-icons drop-down. ::SendMessage, never the
	// frame's ProcessWindowMessage -- an _EX handler over there writes through
	// m_pCurrentMsg, which is only live inside ATL's own WindowProc.
	LRESULT OnGutterForward(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		const HWND hWndFrom = uMsg == WM_COMMAND
			                      ? reinterpret_cast<HWND>(lParam)
			                      : reinterpret_cast<LPNMHDR>(lParam)->hwndFrom;

		if (hWndFrom != _toolbarViews.m_hWnd || _toolbarViews.m_hWnd == nullptr)
		{
			bHandled = FALSE;
			return 0;
		}

		bHandled = TRUE;
		return ::SendMessage(IW::GetMainWindow(), uMsg, wParam, lParam);
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		ATLTRACE(_T("Destroy CFolderCtrl\n")); 		
		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		if(wParam != SIZE_MINIMIZED)
		{
			SizeClients();
			GetLayout()->DoSize();
			UpdateBars();
		}

		return 1;
	}

	void LayoutFrames(int cx, int cy)
	{
		SizeClients();
		GetLayout()->DoSize();
		UpdateBars();
		Invalidate();
	}

	// The right gutter: the scroll bar down from the top, the view-mode strip
	// stacked under it. m_sizeClient is what everything else measures against,
	// so the strip is reserved here and nowhere else.
	void SizeClients()
	{	
		CRect rect;
		GetClientRect(rect);

		const int cx = GutterWidth();

		m_sizeClient.cx = IW::Max(0, rect.Width() - cx);
		m_sizeClient.cy = rect.Height();

		if (_scrollV.m_hWnd == nullptr)
			return;

		// One button per row, each as square as the bar is wide. Measuring the
		// toolbar instead reports its unwrapped width until it has been sized,
		// which is a chicken and egg with the size we are working out.
		const int cyBar = cx > 0 ? _toolbarViews.GetButtonCount() * cx : 0;
		const int cyScroll = IW::Max(0, rect.Height() - cyBar);

		HDWP hdwp = ::BeginDeferWindowPos(2);

		auto place = [&](CWindow &wnd, int y, int cy)
		{
			if (hdwp != nullptr)
				hdwp = ::DeferWindowPos(hdwp, wnd, nullptr, rect.right - cx, y, cx, cy,
				                        SWP_NOZORDER | SWP_NOACTIVATE);
			else
				wnd.SetWindowPos(nullptr, rect.right - cx, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
		};

		place(_scrollV, rect.top, cyScroll);
		place(_toolbarViews, rect.top + cyScroll, cyBar);

		if (hdwp != nullptr)
			::EndDeferWindowPos(hdwp);

		if (IW::HasVisibleStyle(_scrollV) != (cx > 0))
		{
			_scrollV.ShowWindow(cx > 0 ? SW_SHOW : SW_HIDE);
			_toolbarViews.ShowWindow(cx > 0 ? SW_SHOW : SW_HIDE);
		}
	}	

	void Accept(FrameVisitor &visitor) 
	{ 
	}

	// Full screen puts the strip under the picture on the same black; every
	// windowed mode is the system's window colour.
	COLORREF GetBackGroundColor() const
	{
		return IsBlackBackground() ? RGB(0, 0, 0) : IW::Style::Color::Window;
	}

	COLORREF GetTextColor() const
	{
		return IsBlackBackground() ? RGB(255, 255, 255) : IW::Style::Color::WindowText;
	}

	bool IsBlackBackground() const
	{
		return _bFilmStrip && _pCoupling != nullptr && _pCoupling->IsFullScreen();
	}

	void OnPaint(CDCHandle dc)
	{
		IW::CRender render;

		if (render.Create(dc))
		{
			render.Fill(GetBackGroundColor());

			CRect rectClip;
			dc.GetClipBox(rectClip);
			DrawFolder(render, rectClip);			

			Accept(FrameVisitorErase(render));
			Accept(FrameVisitorRender(render));

			// The view strip is a flat toolbar and comctl32 draws those without
			// erasing, so whatever the pane painted shows through it. Give the
			// whole gutter the face colour the buttons are drawn for.
			const int cxGutter = GutterWidth();

			if (cxGutter > 0)
			{
				CRect rectClient;
				GetClientRect(rectClient);
				rectClient.left = IW::Max(rectClient.left, rectClient.right - cxGutter);
				render.Fill(IW::Style::Color::Face, rectClient);
			}

			{
				IW::CDCRender dcRender(render);
				_fade.Draw(m_hWnd, dcRender);
			}

			render.Flip();
		}
	}	

	void OnTimer()
	{
		FolderWindowImpl<ThisClass>::OnTimer();

		if (_fade.Animate())
			Invalidate(FALSE);
	}

	void  Cut()
	{
		//Invoke("cut", false);
		SetClipboard(true);	
	}

	void  Copy()
	{
		//Invoke("copy", false);  
		SetClipboard(false);
	}

	void  Paste()
	{
		if (!::IsClipboardFormatAvailable(CF_HDROP) &&
			::IsClipboardFormatAvailable(CF_DIB))
		{
			OpenClipboard();

			HGLOBAL hglb = ::GetClipboardData(CF_DIB); 

			if (hglb)
			{			  
				IW::Image dib;
				dib.Copy(hglb);

				SaveNewImage(dib);
			}

			::CloseClipboard();
		}
		else
		{
			Invoke("paste", TRUE);
		}
	}

	void ShowProperties()
	{
		Invoke("Properties", false);
	}

	void OnAfterDelete()
	{
		const IW::FolderPtr pFolder = GetFolder();
		const int nFocusItem = pFolder->GetFocusItem();
		const int nSize = pFolder->GetSize();

		// Next thumb after selection
		for(int i = nFocusItem + 1; i < nSize; i++)
		{
			if (!pFolder->IsItemSelected(i))
			{
				_state.Folder.Select(i, 0, true);
				return;
			}
		}

		_state.Folder.Select(-1, 0, true);
	}


	IW::FolderPtr GetFolder()
	{
		return _state.Folder.GetFolder();
	}

	int GetItemCount() const
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		return pFolder->GetSize();
	}

	bool IsItemSelected(long nItem) const
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		return pFolder->IsItemSelected(nItem);
	}

	bool IsItemImage(long nItem) const
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		return pFolder->IsItemImage(nItem);
	}

	int GetSelectedItemCount() const
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		return pFolder->GetSelectedItemCount();
	}	

	bool IsItemFolder(long nItem) const
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		return pFolder->IsItemFolder(nItem);
	}

	int GetImageCount() const
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		return pFolder->GetImageCount();
	}

	void OnSelectionChanged()
	{
		Invalidate();
	}

	void OnFocusChanged() 
	{
		MakeFocusItemVisible();
	}

	void OnStartSearching()
	{
		Invalidate();
		ScrollTop();
		SetScrollSizeList(true);
		ResetCounters();	
	}

	void  MakeFocusItemVisible()
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		int nFocusItem = pFolder->GetFocusItem();

		if (nFocusItem != -1)
		{
			MakeItemVisible(nFocusItem);
		}
	}	


	void SetSortOrder(int nSortOrder)
	{
		_state.Folder.SetSortOrder(nSortOrder);
	}

	void SetSortOrder(int nSortOrder, bool bAscending)
	{
		_state.Folder.SetSortOrder(nSortOrder, bAscending);
	}

	void OnOptionsChanged()
	{
		ResetThumbnailSize();
		ApplyLayout();
		Invalidate();
	}

	void OnThumbnailSizeChanged()
	{
		ApplyLayout(false);
		SizeClients();
	}
};

