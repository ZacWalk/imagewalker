// ImageWalker by Zac Walker
//
// Purpose: The image pane window - one picture, or a collage when several
//          are selected.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

class CDescriptionWindow;
class CImageLoad;

#include "ViewBase.h"
#include "ViewImageFrames.h"
#include "ViewDialogs.h"
#include "ViewModelItems.h"
#include "AppThreadImage.h"
#include "FileFormatAny.h"
#include "ViewFolderCtrl.h"  
#include "ImagingDataObject.h"
#include "ViewImageNavigation.h"
#include "ViewProgressDlg.h"
#include "util_layout.h"

// CBitmapButtonImpl draws the masked image and erases nothing at all, so every
// pixel the magenta key took out kept whatever was last on the pane. It also
// draws at the top left, and this button is taller than its glyph.
class CGutterButton : public CBitmapButtonImpl<CGutterButton>
{
public:

	void DoPaint(CDCHandle dc)
	{
		CRect rect;
		GetClientRect(rect);
		dc.FillSolidRect(rect, ::GetSysColor(COLOR_BTNFACE));

		if (m_ImageList.m_hImageList == nullptr)
			return;

		const bool bPressed = (m_fPressed == 1) || (IsCheckMode() && (m_fChecked == 1));

		int nImage = -1;

		if (!IsWindowEnabled())
			nImage = m_nImage[_nImageDisabled];
		else if (bPressed)
			nImage = m_nImage[_nImagePushed];

		if (nImage == -1)
			nImage = m_nImage[_nImageNormal];

		if (nImage == -1)
			return;

		int cx = 0, cy = 0;
		m_ImageList.GetIconSize(cx, cy);

		m_ImageList.Draw(dc, nImage,
		                 rect.left + IW::Half(rect.Width() - cx),
		                 rect.top + IW::Half(rect.Height() - cy), ILD_NORMAL);
	}
};

class CImageCtrl :
	public CWindowImpl<CImageCtrl>,
	public CView<CImageCtrl>,
	public CPaletteImpl<CImageCtrl>,
	public CDropTargetImpl<CImageCtrl>,	
	public CImageWindowImpl<CImageCtrl>
{
public:

	typedef CImageCtrl ThisClass;
	typedef CImageWindowImpl<ThisClass> ImageWindowBase;

	// Named so a harness can find the pane the way it finds the edit canvas.
	// The window erases its own background.
	DECLARE_WND_CLASS_EX(_T("IWImageCtrl"), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, NULL)

	State &_state;

	// 2.2 puts the zoom controls in a gutter along the bottom of the pane rather
	// than over the picture. Real controls, sized to a scroll bar.
	//
	// 2.0 split this pane in two -- an inner image window with its own non-client
	// scroll bars above a strip of controls. The 2.3 base has one window doing
	// both, so both bars are children here; the arrangement on screen is 2.0's.
	CEdit _scaleEdit;
	CGutterButton _modeButton;
	CScrollBar _scrollH;
	CScrollBar _scrollV;
	CToolBarCtrl _gutterBar;
	CImageList _gutterImages;
	CImageList _modeImages;
	bool _bInScaleUpdate = false;

	// What the readout was last given. A kill-focus that matches it is this
	// pane's own text coming back, not the user typing a scale.
	CString _strScaleShown;

	enum
	{
		kGutterId = 0x7f40,
		kScaleEditId = 0x7f42,
		kScaleEditWidth = 50,

		// Image indices into IDB_IMAGE_GUTTER_BUTTONS, a 56x8 strip of seven 8x8 frames.
		// Frames 0 and 1 are drawn on black rather than on the magenta key, so
		// they come out as solid blocks on a toolbar and are not used.
		kGlyphNavigate = 3,
		kGlyphActualSize = 4,
		kGlyphFit = 5,

		// IDB_FOLDER_VIEW_BUTTONS is appended after those seven; its frame 1 is the
		// thumbnail grid, which is what the film strip toggle shows or hides.
		kGlyphFolderOptions = 7,
		kGlyphFilmStrip = kGlyphFolderOptions + 1
	};

	// One tile of the multiple-selection collage.
	//
	// A cell draws the thumbnail the items pane has already decoded rather than
	// a decode of its own: this pane has one loader thread and it is busy with
	// the focused image, and the completion handler for that thread is the only
	// producer of the next request in the walk.
	struct CollageCell
	{
		CString strPath;
		IW::Image image;
		CRect rect;
		CSize sizeSource = CSize(0, 0);
		int nItem = -1;
		bool bFocus = false;
		bool bFull = false;		// image is a decode of the file, not the thumbnail
	};

	enum
	{
		// More cells than this cannot be told apart in one pane. Matches 3.0.
		kMaxCells = 24,
		kCellGap = 6,

		kCompareGrip = 32,			// height of the block drawn on the divider
		kCompareGrab = 12,			// how close the mouse has to be to take it
		kCompareAspect = 6,			// percent the two shapes may differ by
		kComparePreview = 1600,		// longest edge the two are decoded to
		kCompareSettle = 6			// 20 Hz ticks the selection must hold still
	};

	std::vector<CollageCell> _cells;
	CString _strCollageCaption;
	bool _bCollage = false;

	// Two selected images are a comparison rather than a collage: side by side
	// at the same scale, and -- when they are the same shape -- a grip between
	// them that lays one over the other with a divider to drag.
	bool _bCompare = false;
	bool _bSplit = false;
	int _nDivider = 50;			// percent across _rectSplit
	int _nSettle = 0;			// ticks left before the two are decoded
	CRect _rectSplit;			// the one frame both are drawn into while split

	// Both scaled to exactly that frame, so the two line up pixel for pixel.
	IW::Image _splitA;
	IW::Image _splitB;
	CSize _sizeSplitFor = CSize(0, 0);

	ImageToolCompare<CImageCtrl> _toolCompare;

	// What the pane says instead of a picture when the focused item is not one.
	// An empty pane that still holds the last photo looks stuck.
	CString _strNoPreview;
	CString _strNoPreviewDetail;

	FadeOverlay _fade;

	CImageCtrl(Coupling *pCoupling, State &state) :
		CImageWindowImpl<CImageCtrl>(pCoupling, state),
		_state(state),		
		_toolCompare(*this)
	{
	}

	~CImageCtrl()
	{
	}

	void OnNewImage(bool bScrollToCenter)
	{
		Refresh(bScrollToCenter);
		RefreshDescription();
		OnResetFrames();
	}

	void OnActivate()
	{		
		RefreshDescription();
		OnResetFrames();
	}

	void OnResetFrames()
	{
		SetScrollSizes(false);
		ShowFrames();
		SizeClients();
		ImageWindowBase::ShowMouseMoveFeedback();
		Invalidate();
	}

	void RefreshDescription()
	{
	}

	void LayoutFrames(int cx, int cy)
	{
		LayoutGutter();
		SizeClients();
		Invalidate();
	}	

	void OnCommand(WORD id)
	{
		_pCoupling->Command(id);
	}	

	BEGIN_MSG_MAP(CImageCtrl)

		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_HSCROLL, OnBarScroll)
		MESSAGE_HANDLER(WM_VSCROLL, OnBarScroll)
		COMMAND_HANDLER(kScaleEditId, EN_KILLFOCUS, OnScaleEditChange)
		COMMAND_ID_HANDLER(IDR_MODE, OnModeButton)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)

		CHAIN_MSG_MAP(CPaletteImpl<CImageCtrl>)
		CHAIN_MSG_MAP(ImageWindowBase)
		CHAIN_MSG_MAP(CView<CImageCtrl>)

	END_MSG_MAP()

public:

	void OnOptionsChanged()
	{
		Invalidate();
	}	

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CreateGutter();
		RegisterDropTarget();
		_state.ResetFrames.Bind(this, &ThisClass::OnResetFrames);
		ShowFrames();
		bHandled = false;
		return 0;
	}

	int GutterHeight() const
	{
		return IW::Max(::GetSystemMetrics(SM_CYHSCROLL), 18);
	}

	int GutterWidth() const
	{
		if (!CanShowControls() || !CanShowGutterItem(ID_IMAGE_SCROLLV))
			return 0;

		CRect rect;
		GetClientRect(rect);
		rect.bottom = IW::Max(rect.top, rect.bottom - GutterHeight());

		// Decide from the bar-free viewport, not last layout's reserved width.
		return CalcImageSize(rect.Size()).cy > rect.Height() ? ::GetSystemMetrics(SM_CXVSCROLL) : 0;
	}

	bool CanShowGutterItem(DWORD id) const
	{
		return _pCoupling == nullptr || _pCoupling->CanShowGutterItem(id);
	}

	void CreateGutter()
	{
		const int cy = GutterHeight();

		_scaleEdit.Create(m_hWnd, rcDefault, nullptr,
		                  WS_CHILD | WS_VISIBLE | ES_RIGHT | ES_AUTOHSCROLL,
		                  WS_EX_STATICEDGE, kScaleEditId);
		_scaleEdit.SetFont(AtlGetStockFont(DEFAULT_GUI_FONT));
		_scaleEdit.LimitText(19);

		// No LR_CREATEDIBSECTION: the 24-bit strip comes back as a 32-bit image
		// list and the magenta key is never applied, so the button draws its
		// background.
		_modeImages.CreateFromImage(IDB_IMAGE_MODE_BUTTON, 16, 1, RGB(255, 0, 255), IMAGE_BITMAP, 0);

		_modeButton.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		                   0, IDR_MODE);

		// Shared list only: BMPBTN_AUTO3D_* draws a raised border, and nothing
		// else in this strip has one.
		_modeButton.SetBitmapButtonExtendedStyle(BMPBTN_SHAREIMAGELISTS);
		_modeButton.SetImageList(_modeImages);
		_modeButton.SetImages(0, 1, 2);

		_scrollH.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | SBS_HORZ,
		                0, ID_IMAGE_SCROLL);
		_scrollV.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | SBS_VERT,
		                0, ID_IMAGE_SCROLLV);

		// iString is only ever an index into a string pool this toolbar does not
		// have; SetMaxTextRows(0) below is what stops comctl32 drawing from it.
		// The Options button 2.0 had is gone - the mode button beside the readout
		// raises the same menu.
		static const TBBUTTON tb[] =
		{
			{kGlyphFilmStrip, ID_VIEW_FILMSTRIP, TBSTATE_ENABLED | TBSTATE_HIDDEN, BTNS_CHECK, {0}, 0L, 0},
			{kGlyphFit, ID_SCALE_FIT, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0L, 0},
			{kGlyphActualSize, ID_SCALE_100, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0L, 0},
			{kGlyphNavigate, ID_NAVIGATE, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_DROPDOWN, {0}, 0L, 0}
		};

		// No TBSTYLE_FLAT, for the same reason as the items strip: flat means
		// transparent, and this pane clips its children.
		_gutterBar.Create(m_hWnd, rcDefault, nullptr,
		                  WS_CHILD | WS_VISIBLE | CCS_NORESIZE | CCS_NOPARENTALIGN |
		                  CCS_NODIVIDER | CCS_TOP | TBSTYLE_TOOLTIPS,
		                  0, kGutterId);

		_gutterBar.SetButtonStructSize(sizeof(TBBUTTON));
		_gutterBar.SetMaxTextRows(0);

		// 8x8 glyphs on scroll-bar-sized buttons: that is what makes the strip
		// exactly one scroll bar tall, which is the whole look of this gutter.
		_gutterImages.CreateFromImage(IDB_IMAGE_GUTTER_BUTTONS, 8, 1, RGB(255, 0, 255),
		                              IMAGE_BITMAP, LR_CREATEDIBSECTION);

		// No LR_CREATEDIBSECTION here: ImageList_AddMasked has to read the key
		// colour back out of the bitmap.
		CBitmap bitmapFolderOptions;

		if (bitmapFolderOptions.LoadBitmap(IDB_FOLDER_VIEW_BUTTONS))
			_gutterImages.Add(bitmapFolderOptions, RGB(255, 0, 255));

		_gutterBar.SetImageList(_gutterImages);
		_gutterBar.AddButtons(_countof(tb), const_cast<LPTBBUTTON>(tb));
		_gutterBar.SetBitmapSize(CSize(8, 8));
		_gutterBar.SetButtonSize(CSize(cy, cy));
	}

	// 2.0's order, left to right: the scale readout, the mode button, the
	// horizontal scroll bar taking what is left, then the toolbar flush right.
	// The vertical bar stands on the right edge, above the strip.
	void LayoutGutter()
	{
		if (_scaleEdit.m_hWnd == nullptr)
			return;

		CRect rectClient;
		GetClientRect(rectClient);

		const int cy = GutterHeight();
		const int cxScroll = GutterWidth();
		const int yTop = rectClient.bottom - cy;

		if (yTop <= rectClient.top)
			return;

		const bool bScroll = CanShowControls() && CanShowGutterItem(ID_IMAGE_SCROLL) &&
			GetCanvasSize().cx > GetClientSize().cx;
		const bool bStrip = CanShowGutterItem(ID_VIEW_FILMSTRIP);

		if ((_gutterBar.IsButtonHidden(ID_VIEW_FILMSTRIP) != FALSE) == bStrip)
			_gutterBar.HideButton(ID_VIEW_FILMSTRIP, !bStrip);

		const int cxBar = cy * (bStrip ? 4 : 3);

		int left = rectClient.left;
		int right = IW::Max(left, rectClient.right - cxBar);

		HDWP hdwp = ::BeginDeferWindowPos(5);

		auto place = [&](CWindow &wnd, int x, int y, int cx, int cyItem)
		{
			if (wnd.m_hWnd == nullptr)
				return;

			CRect rectCurrent;
			wnd.GetWindowRect(rectCurrent);
			ScreenToClient(rectCurrent);
			if (rectCurrent == CRect(x, y, x + cx, y + cyItem))
				return;

			if (hdwp != nullptr)
				hdwp = ::DeferWindowPos(hdwp, wnd, nullptr, x, y, cx, cyItem,
				                        SWP_NOZORDER | SWP_NOACTIVATE);
			else
				wnd.SetWindowPos(nullptr, x, y, cx, cyItem, SWP_NOZORDER | SWP_NOACTIVATE);
		};

		const int cxEdit = IW::Min(static_cast<int>(kScaleEditWidth), IW::Max(0, right - left));
		place(_scaleEdit, left, yTop, cxEdit, cy);
		left += cxEdit;

		const int cxMode = IW::Min(cy, IW::Max(0, right - left));
		place(_modeButton, left, yTop, cxMode, cy);
		left += cxMode;

		place(_scrollH, left, yTop, IW::Max(0, right - left), cy);
		place(_gutterBar, right, yTop, cxBar, cy);
		place(_scrollV, rectClient.right - cxScroll, rectClient.top, cxScroll, yTop - rectClient.top);

		if (hdwp != nullptr)
			::EndDeferWindowPos(hdwp);

		if (IW::HasVisibleStyle(_scrollH) != bScroll)
			_scrollH.ShowWindow(bScroll ? SW_SHOW : SW_HIDE);

		if (IW::HasVisibleStyle(_scrollV) != (cxScroll > 0))
			_scrollV.ShowWindow(cxScroll > 0 ? SW_SHOW : SW_HIDE);
	}

	// The toolbar gutter is permanent; the vertical bar only takes image space
	// when the scaled image is taller than the bar-free viewport.
	CRect GetImageClientRect() const
	{
		CRect rect;
		GetClientRect(rect);
		rect.bottom = IW::Max(rect.top, rect.bottom - GutterHeight());
		rect.right = IW::Max(rect.left, rect.right - GutterWidth());
		return rect;
	}

	CRect GetImageDisplayRect() const
	{
		return GetImageClientRect();
	}

	// Push the model's scroll range at the two bars, and let the Navigate button
	// go grey when there is nothing to navigate.
	void OnScrollRangeChanged()
	{
		if (_scrollH.m_hWnd == nullptr)
			return;

		LayoutGutter();

		const CSize sizeAll = GetCanvasSize();
		const CSize sizeClient = GetClientSize();
		const CPoint point = GetScrollOffset();

		SCROLLINFO si = {sizeof(si), SIF_PAGE | SIF_RANGE | SIF_POS, 0, 0, 0, 0, 0};

		si.nMax = IW::Max(0, sizeAll.cx - 1);
		si.nPage = sizeClient.cx;
		si.nPos = point.x;
		_scrollH.SetScrollInfo(&si, TRUE);

		si.nMax = IW::Max(0, sizeAll.cy - 1);
		si.nPage = sizeClient.cy;
		si.nPos = point.y;
		_scrollV.SetScrollInfo(&si, TRUE);

		const bool bNavigate = CanShowControls() && CanNavigate();
		if (_gutterBar.m_hWnd != nullptr &&
			(_gutterBar.IsButtonEnabled(ID_NAVIGATE) != FALSE) != bNavigate)
			_gutterBar.EnableButton(ID_NAVIGATE, bNavigate);
	}

	LRESULT OnBarScroll(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		const bool bVert = uMsg == WM_VSCROLL;
		CScrollBar &bar = bVert ? _scrollV : _scrollH;

		if (reinterpret_cast<HWND>(lParam) != bar.m_hWnd || bar.m_hWnd == nullptr)
		{
			bHandled = FALSE;
			return 0;
		}

		SCROLLINFO si = {sizeof(si), SIF_ALL, 0, 0, 0, 0, 0};
		bar.GetScrollInfo(&si);

		const int nLine = IW::Max(1, static_cast<int>(si.nPage) / 10);
		int nPos = si.nPos;

		switch (LOWORD(wParam))
		{
		case SB_LINEUP: nPos -= nLine; break;
		case SB_LINEDOWN: nPos += nLine; break;
		case SB_PAGEUP: nPos -= si.nPage; break;
		case SB_PAGEDOWN: nPos += si.nPage; break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: nPos = si.nTrackPos; break;
		case SB_TOP: nPos = si.nMin; break;
		case SB_BOTTOM: nPos = si.nMax; break;
		default: return 0;
		}

		CPoint point = GetScrollOffset();
		(bVert ? point.y : point.x) = nPos;
		ScrollTo(point);

		return 0;
	}

	// The bitmap button between the readout and the scroll bar: 2.0's way to the
	// scale list without going to the menu bar.
	LRESULT OnModeButton(WORD, WORD, HWND, BOOL&)
	{
		CMenu menu;

		if (!menu.LoadMenu(IDR_MODE))
			return 0;

		CRect rect;
		_modeButton.GetWindowRect(rect);
		TrackGutterMenu(menu.GetSubMenu(0), rect);

		return 0;
	}

	void TrackGutterMenu(CMenuHandle menuPopup, const CRect &rectExclude)
	{
		if (menuPopup.IsNull())
			return;

		menuPopup.CheckMenuItem(ID_SCALE_FIT,
		                        MF_BYCOMMAND | (GetScaleType() == ScaleMode::Fit ? MF_CHECKED : MF_UNCHECKED));
		menuPopup.CheckMenuItem(ID_SCALE_FILL,
		                        MF_BYCOMMAND | (GetScaleType() == ScaleMode::Fill ? MF_CHECKED : MF_UNCHECKED));

		// Through the coupling: the frame owns the command bar that routes what
		// the menu posts, and these ids are handled by the view, not by this pane.
		_pCoupling->TrackPopupMenu(menuPopup, TPM_LEFTALIGN | TPM_TOPALIGN,
		                           rectExclude.left, rectExclude.bottom);
	}

	// The box takes "Fit", "Up", "Down", "n%" and "n:m" -- SetScale parses it and
	// leaves the scale alone if it cannot.
	LRESULT OnScaleEditChange(WORD, WORD, HWND, BOOL&)
	{
		if (!_bInScaleUpdate)
		{
			CString str;
			_scaleEdit.GetWindowText(str);

			// The readout shows a percentage even while fitting, so parsing what
			// we wrote there would turn every trip through the box into a fixed
			// scale and quietly drop Fit.
			if (str != _strScaleShown)
				SetScale(str);
		}

		return 0;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		ATLTRACE(_T("Destroy CImageCtrl\n"));
		RevokeDropTarget();
		bHandled = false;
		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(wParam != SIZE_MINIMIZED)
		{
			SizeClients();
			ArrangeCollage();
		}

		bHandled = FALSE;
		return 1;
	}

	void LoadImageFromHGlobal(HGLOBAL hGlobal)
	{
		IW::Image image;
		image.Copy(hGlobal);
		_state.Image.SetImage(image, g_szEmptyString);
	}

	void OnPaint(CDCHandle dc)
	{
		IW::CRender render;

		if (render.Create(dc))
		{
			render.Fill(GetBackGroundColor());
			
			CRect rectClip;
			dc.GetClipBox(rectClip);

			if (_bCollage)
			{
				DrawCollage(render);
			}
			else if (!_state.Image.IsImageShown() && !_strNoPreview.IsEmpty())
			{
				DrawNoPreview(render);
			}
			else
			{
				DrawImage(render, rectClip);
			}

			Accept(FrameVisitorErase(render));
			Accept(FrameVisitorRender(render));	

			// Into the back buffer, before it goes out. Blending onto the window
			// after Flip meant every paint wrote the new picture to the screen at
			// full strength and then darkened it -- a strobe on each frame of the
			// fade, worst when stepping quickly because there are more of them.
			{
				IW::CDCRender dcRender(render);
				_fade.Draw(m_hWnd, dcRender);
			}

			CRect rectGutter;
			GetClientRect(rectGutter);
			rectGutter.top = GetImageClientRect().bottom;
			render.Fill(::GetSysColor(COLOR_BTNFACE), rectGutter);

			render.Flip();
		}

		DrawControls(dc);
	}

	// Called just before whatever this pane draws is replaced.
	void BeginFade(int nSteps)
	{
		_fade.Capture(m_hWnd, nSteps);
	}

	void OnTimer()
	{
		ImageWindowBase::OnTimer();

		// Deferred: a rubber band passing through two selected items would
		// otherwise decode a pair of files on the way past.
		if (_nSettle > 0 && --_nSettle == 0)
			LoadCompareImages();

		if (_fade.Animate())
			Invalidate(FALSE);
	}

	///////////////////////////////////////////////////////////////////////
	// The collage.
	//
	// More than one selected item is shown the way 1.0 and 3.0 show it: the pane
	// is split recursively into one cell per file. There is no other producer of
	// what this pane displays, so "what is selected" and "what is on screen"
	// cannot drift apart.


	// Set on every focus change: the text for an item that has no picture behind
	// it, or empty for one that has.
	void SetNoPreview(const CString &strTitle, const CString &strDetail = g_szEmptyString)
	{
		if (_strNoPreview == strTitle && _strNoPreviewDetail == strDetail)
			return;

		_strNoPreview = strTitle;
		_strNoPreviewDetail = strDetail;

		// Not just a repaint: the help panel stands down while this is up.
		OnResetFrames();
	}

	void DrawNoPreview(IW::CRender &render)
	{
		CRect rectClient;
		GetClientRect(rectClient);

		CRect rectAvailable(rectClient);
		rectAvailable.DeflateRect(IW::Max(8, rectClient.Width() / 10), 0);

		const CRect rectTitle = render.MeasureString(_strNoPreview, rectAvailable,
		                                             IW::Style::Font::Heading,
		                                             IW::Style::Text::NormalCentre);

		const CRect rectDetail = render.MeasureString(_strNoPreviewDetail, rectAvailable,
		                                              IW::Style::Font::Standard,
		                                              IW::Style::Text::NormalCentre);

		const int cy = rectTitle.Height() + Frame::paddingLarge + rectDetail.Height();

		CRect r(rectAvailable);
		r.top = rectClient.top + IW::Half(rectClient.Height() - cy);
		r.bottom = r.top + rectTitle.Height();

		render.DrawString(_strNoPreview, r, IW::Style::Font::Heading,
		                  IW::Style::Text::NormalCentre, IW::Style::Color::WindowText);

		r.top = r.bottom + Frame::paddingLarge;
		r.bottom = r.top + rectDetail.Height();

		render.DrawString(_strNoPreviewDetail, r, IW::Style::Font::Standard,
		                  IW::Style::Text::NormalCentre, IW::Style::Color::WindowText);
	}

	// The one entry point. Called on every selection change.
	void UpdateSelection()
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();

		int nSelected = 0, nImages = 0;
		IW::FileSize size;
		pFolder->GetSelectStatus(nSelected, nImages, size);

		if (nSelected < 2)
		{
			if (!_bCollage)
				return;

			_bCollage = false;
			_cells.clear();
			_strCollageCaption.Empty();
			ResetCompare();

			// Not just a repaint: the zoom strip and the description panel are
			// hidden while the collage is up and have to come back with it.
			OnResetFrames();
			return;
		}

		const int nCount = pFolder->GetItemCount();

		std::vector<int> wanted;
		wanted.reserve(kMaxCells);

		for (int i = 0; i < nCount && static_cast<int>(wanted.size()) < kMaxCells; i++)
		{
			if (pFolder->IsItemSelected(i))
				wanted.push_back(i);
		}

		if (wanted.empty())
			return;

		// Extending a selection must not throw away the cells it keeps: their
		// thumbnails are already resident and re-reading them flickers.
		bool bSame = _bCollage && _cells.size() == wanted.size();

		for (size_t i = 0; bSame && i < wanted.size(); i++)
			bSame = _cells[i].strPath.CompareNoCase(pFolder->GetItemPath(wanted[i])) == 0;

		if (!bSame)
		{
			_cells.clear();
			_cells.resize(wanted.size());
			ResetCompare();
		}

		const int nFocus = pFolder->GetFocusItem();

		for (size_t i = 0; i < wanted.size(); i++)
		{
			CollageCell &cell = _cells[i];

			cell.nItem = wanted[i];
			cell.strPath = pFolder->GetItemPath(wanted[i]);
			cell.bFocus = wanted[i] == nFocus;

			if (cell.bFull)
				continue;

			pFolder->GetItemImage(wanted[i], cell.image);
			cell.sizeSource = cell.image.IsEmpty() ? CSize(4, 3) : cell.image.GetBoundingRect().Size();
		}

		// Two pictures and nothing else is a comparison. Anything with a folder
		// or an unreadable file in it stays a collage.
		_bCompare = _cells.size() == 2 && nSelected == 2 && nImages == 2;

		if (_bCompare)
			_nSettle = kCompareSettle;

		CString str;
		str.Format(IDS_IMAGES_FILES_FOLDERS, nImages, nSelected, static_cast<LPCTSTR>(size.ToString()));

		// Folders, unreadable files and the cell cap all leave some of the
		// selection undrawn, and showing fewer tiles than were picked without
		// saying so looks like a bug.
		if (static_cast<int>(_cells.size()) < nSelected)
		{
			CString strShown;
			strShown.Format(_T("      showing %d"), static_cast<int>(_cells.size()));
			str += strShown;
		}

		_strCollageCaption = str;
		_bCollage = true;

		OnResetFrames();
		ArrangeCollage();
		Invalidate();
	}

	// The thumbnails arrive after the selection does, so the cells are refreshed
	// whenever the loader reports progress.
	void OnThumbsLoaded()
	{
		if (!_bCollage)
			return;

		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		const int nCount = pFolder->GetItemCount();

		bool bChanged = false;

		for (CollageCell &cell : _cells)
		{
			if (!cell.image.IsEmpty())
				continue;

			// By index, checked against the path, rather than a scan for the path:
			// GetItemPath is a shell round trip per item and this runs on every
			// thumbnail progress message. A cell whose index has moved is left
			// alone; the next selection change rebuilds it.
			if (cell.nItem < 0 || cell.nItem >= nCount ||
				cell.strPath.CompareNoCase(pFolder->GetItemPath(cell.nItem)) != 0)
				continue;

			pFolder->GetItemImage(cell.nItem, cell.image);

			if (!cell.image.IsEmpty())
			{
				// The packing is by aspect, so a cell whose real shape was not
				// known changes the whole layout.
				cell.sizeSource = cell.image.GetBoundingRect().Size();
				bChanged = true;
			}
		}

		if (bChanged)
		{
			DropSplitImages();
			ArrangeCollage();
			Invalidate();
		}
	}

	CRect CollageCaptionRect() const
	{
		CRect rc;
		GetClientRect(rc);

		const int cy = 22;

		if (_strCollageCaption.IsEmpty() || rc.Height() < cy * 3)
			return CRect(rc.left, rc.top, rc.right, rc.top);

		rc.bottom = rc.top + cy;
		return rc;
	}

	///////////////////////////////////////////////////////////////////////
	// Two selected images.

	void ResetCompare()
	{
		_bCompare = false;
		_bSplit = false;
		_nDivider = 50;
		_nSettle = 0;
		_rectSplit.SetRectEmpty();
		DropSplitImages();
	}

	void DropSplitImages()
	{
		_splitA.Free();
		_splitB.Free();
		_sizeSplitFor.cx = _sizeSplitFor.cy = 0;
	}

	// A comparison only makes sense between two frames of the same shape: laying
	// a portrait over a landscape shows the reader nothing about either.
	bool CompareAspectsMatch() const
	{
		if (_cells.size() != 2)
			return false;

		for (int i = 0; i < 2; i++)
			if (_cells[i].sizeSource.cx < 1 || _cells[i].sizeSource.cy < 1 || _cells[i].image.IsEmpty())
				return false;

		const int a0 = MulDiv(_cells[0].sizeSource.cx, 1000, _cells[0].sizeSource.cy);
		const int a1 = MulDiv(_cells[1].sizeSource.cx, 1000, _cells[1].sizeSource.cy);

		return abs(a0 - a1) * 100 <= ((a0 + a1) / 2) * kCompareAspect;
	}

	bool CanCompareSplit() const
	{
		return _bCompare && !_rectSplit.IsRectEmpty();
	}

	int CompareDividerX() const
	{
		return _rectSplit.left + MulDiv(_nDivider, _rectSplit.Width(), 100);
	}

	// Where the grip is drawn: in the gutter between the two while they are side
	// by side, on the divider once they are one frame.
	CRect CompareGripRect() const
	{
		if (!CanCompareSplit())
			return CRect(0, 0, 0, 0);

		const int x = _bSplit ? CompareDividerX() : _cells[1].rect.left;
		const CRect &rc = _bSplit ? _rectSplit : _cells[0].rect;
		const int yMid = (rc.top + rc.bottom) / 2;

		return CRect(x - 4, yMid - kCompareGrip / 2, x + 5, yMid + kCompareGrip / 2);
	}

	bool PointOnCompareHandle(const CPoint &pt) const
	{
		if (!CanCompareSplit())
			return false;

		// The whole divider is a target once it is up; before that only the grip
		// is, or a click anywhere down the middle of the pane would split.
		if (_bSplit)
			return abs(pt.x - CompareDividerX()) <= kCompareGrab &&
				pt.y >= _rectSplit.top && pt.y <= _rectSplit.bottom;

		CRect rc = CompareGripRect();
		rc.InflateRect(kCompareGrab, kCompareGrab);

		return rc.PtInRect(pt) != FALSE;
	}

	void BeginCompareSplit()
	{
		if (_bSplit || !CanCompareSplit())
			return;

		BuildSplitImages();

		// Nothing to lay over anything: leave the two side by side rather than
		// entering a mode the next paint would have to leave again.
		if (_splitA.IsEmpty() || _splitB.IsEmpty())
			return;

		_bSplit = true;
		Invalidate();
	}

	bool EndCompareSplit()
	{
		if (!_bSplit)
			return false;

		_bSplit = false;
		_nDivider = 50;
		Invalidate();

		return true;
	}

	void SetCompareDivider(int x)
	{
		if (!_bSplit || _rectSplit.Width() < 1)
			return;

		const int n = IW::Clamp(MulDiv(x - _rectSplit.left, 100, _rectSplit.Width()), 0, 100);

		if (n == _nDivider)
			return;

		_nDivider = n;
		Invalidate();
	}

	// The cells draw the items pane's thumbnails, which is all a collage needs.
	// Two pictures being compared are looked at closely, so those two -- and
	// only those two -- are decoded once the selection has stopped moving.
	void LoadCompareImages()
	{
		if (!_bCompare)
			return;

		CWaitCursor wait;
		CLoadAny loader(_state.Loaders);

		bool bChanged = false;

		for (CollageCell &cell : _cells)
		{
			if (cell.bFull || cell.strPath.IsEmpty())
				continue;

			CImageLoad info(cell.strPath, false, 0, 0, 0);

			if (!info.Load(loader, IW::CNullStatus::Instance) || info._image.IsEmpty())
			{
				// Nothing more to try, and retrying it every tick would decode
				// the file over and over.
				cell.bFull = true;
				continue;
			}

			const CSize sizeFile = info._image.GetBoundingRect().Size();

			// Bounded rather than full resolution: two photographs at their own
			// size are tens of megabytes and every repaint would rescale them.
			cell.image = (sizeFile.cx <= kComparePreview && sizeFile.cy <= kComparePreview)
				             ? info._image
				             : IW::CreatePreview(info._image, CSize(kComparePreview, kComparePreview), true);

			cell.sizeSource = sizeFile;
			cell.bFull = true;
			bChanged = true;
		}

		if (bChanged)
		{
			DropSplitImages();
			ArrangeCollage();
			Invalidate();
		}
	}

	// Both scaled to exactly the frame they share. Fitting each to its own
	// aspect instead would leave the two a pixel or two out of step, which is
	// the one thing a split comparison must not do.
	void BuildSplitImages()
	{
		const CSize size = _rectSplit.Size();

		if (size.cx < 1 || size.cy < 1 || _cells.size() != 2)
			return;

		// The size is the whole key: built, or tried at this size and could not.
		if (_sizeSplitFor == size)
			return;

		DropSplitImages();
		_sizeSplitFor = size;

		if (_cells[0].image.IsEmpty() || _cells[1].image.IsEmpty())
			return;

		if (!IW::Scale(_cells[0].image, _splitA, size, IW::CNullStatus::Instance) ||
			!IW::Scale(_cells[1].image, _splitB, size, IW::CNullStatus::Instance) ||
			!IsSplitPage(_splitA, size) || !IsSplitPage(_splitB, size))
		{
			// An animated GIF scales to one page per frame, and a frame is a
			// sub-rectangle -- the source rectangles below would read past it.
			_splitA.Free();
			_splitB.Free();
		}
	}

	static bool IsSplitPage(const IW::Image &image, CSize size)
	{
		return image.GetPageCount() == 1 &&
			image.GetFirstPage().GetPageRect() == CRect(0, 0, size.cx, size.cy);
	}

	void ArrangeCompare(const CRect &rectView)
	{
		_rectSplit.SetRectEmpty();

		const int nHalf = rectView.Width() / 2;

		_cells[0].rect = CRect(rectView.left, rectView.top, rectView.left + nHalf, rectView.bottom);
		_cells[1].rect = CRect(rectView.left + nHalf, rectView.top, rectView.right, rectView.bottom);

		if (!CompareAspectsMatch())
		{
			_bSplit = false;
			return;
		}

		// One frame for both: the size either of them would take on its own.
		const iw::recti fitted = iw::ui::fit_centered(
			iw::sizei(_cells[0].sizeSource.cx, _cells[0].sizeSource.cy),
			iw::recti(rectView.left + kCellGap, rectView.top + kCellGap,
			          rectView.Width() - kCellGap * 2, rectView.Height() - kCellGap * 2));

		_rectSplit = CRect(fitted.x, fitted.y, fitted.x + fitted.width, fitted.y + fitted.height);
	}

	void DrawCompareGrip(IW::CRender &render)
	{
		const CRect rc = CompareGripRect();

		if (rc.IsRectEmpty())
			return;

		render.DrawRect(rc, RGB(0, 0, 0), RGB(255, 255, 255));
	}

	// One frame of two pictures says nothing about which half is which file.
	void DrawCompareName(IW::CRender &render, const CString &strPath, bool bLeft)
	{
		const CString str = IW::Path::FindFileName(strPath);

		CRect rc = _rectSplit;
		rc.DeflateRect(6, 6);
		rc.top = rc.bottom - 20;

		rc = IW::Style::MeasureString(str, rc, IW::Style::Font::Standard, IW::Style::Text::SingleLine);
		rc.InflateRect(4, 2);

		if (bLeft)
			rc.OffsetRect(_rectSplit.left + 6 - rc.left, _rectSplit.bottom - 6 - rc.bottom);
		else
			rc.OffsetRect(_rectSplit.right - 6 - rc.right, _rectSplit.bottom - 6 - rc.bottom);

		render.DrawRect(rc, RGB(0, 0, 0), RGB(0, 0, 0));

		CRect rcText(rc);
		rcText.DeflateRect(4, 2);

		render.DrawString(str, rcText, IW::Style::Font::Standard, IW::Style::Text::SingleLine,
		                  RGB(255, 255, 255));
	}

	void DrawCompareSplit(IW::CRender &render)
	{
		const int x = IW::Clamp(CompareDividerX(), static_cast<int>(_rectSplit.left),
		                        static_cast<int>(_rectSplit.right));

		const CSize size = _rectSplit.Size();

		// Both pages are already exactly the size of the frame, so each half is
		// a straight copy -- the only path through DrawImage that honours a
		// source rectangle.
		if (x > _rectSplit.left)
			render.DrawImage(_splitA.GetFirstPage(),
			                 CRect(_rectSplit.left, _rectSplit.top, x, _rectSplit.bottom),
			                 CRect(0, 0, x - _rectSplit.left, size.cy));

		if (x < _rectSplit.right)
			render.DrawImage(_splitB.GetFirstPage(),
			                 CRect(x, _rectSplit.top, _rectSplit.right, _rectSplit.bottom),
			                 CRect(x - _rectSplit.left, 0, size.cx, size.cy));

		render.DrawLine(x, _rectSplit.top, x, _rectSplit.bottom, RGB(255, 255, 255), 2);

		DrawCompareName(render, _cells[0].strPath, true);
		DrawCompareName(render, _cells[1].strPath, false);

		DrawCompareGrip(render);
	}

	void ArrangeCollage()
	{
		if (_cells.empty())
			return;

		CRect rectView;
		GetClientRect(rectView);
		rectView.top = CollageCaptionRect().bottom;

		if (rectView.IsRectEmpty())
			return;

		if (_bCompare)
		{
			ArrangeCompare(rectView);
			return;
		}

		std::vector<iw::sizei> sizes;
		sizes.reserve(_cells.size());

		for (const CollageCell &cell : _cells)
			sizes.push_back(cell.sizeSource.cx > 0 && cell.sizeSource.cy > 0
				                ? iw::sizei(cell.sizeSource.cx, cell.sizeSource.cy)
				                : iw::sizei(4, 3));

		const iw::recti bounds(rectView.left, rectView.top, rectView.Width(), rectView.Height());
		const std::vector<iw::recti> packed = iw::ui::layout_collage(bounds, sizes);

		for (size_t i = 0; i < _cells.size(); i++)
		{
			_cells[i].rect = i < packed.size()
				                 ? CRect(packed[i].x, packed[i].y,
				                         packed[i].x + packed[i].width, packed[i].y + packed[i].height)
				                 : CRect(0, 0, 0, 0);
		}
	}

	void DrawCollage(IW::CRender &render)
	{
		const CRect rectCaption = CollageCaptionRect();

		if (!rectCaption.IsRectEmpty())
		{
			render.DrawString(_strCollageCaption, rectCaption, IW::Style::Font::Standard,
			                  IW::Style::Text::NormalCentre, IW::Style::Color::WindowText);
		}

		if (_bSplit && CanCompareSplit())
		{
			BuildSplitImages();

			if (!_splitA.IsEmpty() && !_splitB.IsEmpty())
			{
				DrawCompareSplit(render);
				return;
			}

			// Nothing to split with yet -- the thumbnails have not arrived.
			_bSplit = false;
		}

		for (CollageCell &cell : _cells)
		{
			CRect rect(cell.rect);
			rect.DeflateRect(kCellGap / 2, kCellGap / 2);

			if (rect.IsRectEmpty())
				continue;

			if (cell.image.IsEmpty())
			{
				// No thumbnail yet, or a file that is not a picture. The name is
				// the only thing the pane can say about it.
				render.DrawString(IW::Path::FindFileName(cell.strPath), rect, IW::Style::Font::Standard,
				                  IW::Style::Text::NormalCentre, IW::Style::Color::WindowText);
			}
			else
			{
				const iw::recti fitted = iw::ui::fit_centered(
					iw::sizei(cell.sizeSource.cx, cell.sizeSource.cy),
					iw::recti(rect.left, rect.top, rect.Width(), rect.Height()));

				IW::Page page = cell.image.GetFirstPage();
				render.DrawImage(page, CRect(fitted.x, fitted.y,
				                             fitted.x + fitted.width, fitted.y + fitted.height));
			}

			if (cell.bFocus)
				render.DrawFocusRect(rect);
		}

		if (_bCompare)
			DrawCompareGrip(render);
	}

	CString GetToolTipText(UINT_PTR idCtrl)
	{
		FrameVisitorTooltip visitor;
		Accept(visitor);
		if (visitor.Handled) idCtrl = visitor.Id;
		return CView<CImageCtrl>::GetToolTipText(idCtrl);
	}

	void OnKeyDown(int nChar)
	{
		switch(nChar)
		{
		case VK_RETURN:
			OnCommand(ID_VIEW_IMAGEFULLSCREEN);
			break;

		case VK_END:
			_state.Image.End();
			break;

		case VK_HOME:
			_state.Image.Home();
			break;

		// Through the command, not straight to ImageState: stepping has to move
		// the items pane with it, or the filmstrip stops following the picture.
		case VK_LEFT:
		case VK_UP:
		case VK_PRIOR:
			OnCommand(ID_IMAGE_PREVIOUS);
			break;

		case VK_RIGHT:
		case VK_DOWN:
		case VK_NEXT:
			OnCommand(ID_IMAGE_NEXT);
			break;

		case VK_PAUSE:
			SetAnimationPaused(!IsAnimationPaused());
			break;

		case VK_SUBTRACT:
		case '-':
		case '_':
			PreviousFrame();
			break;

		case VK_ADD:
		case '=':
		case '+':
			NextFrame();
			break;
		}
	}	

	void Redraw()
	{
		LayoutFrames(0, 0);
		DoSize();		
	}

	using ImageWindowBase::ShowMouseMoveFeedback;

	void ShowMouseMoveFeedback(const CPoint &point)
	{
		if (PointOnCompareHandle(point))
		{
			SetCursor(IW::Style::Cursor::LeftRight);
			return;
		}

		ImageWindowBase::ShowMouseMoveFeedback(point);
	}

	ImageTool *GetToolFromLButtonDown(const CPoint &point)
	{
		if (PointOnCompareHandle(point))
			return &_toolCompare;

		// A collage has no one picture to select in, pan or scroll.
		if (_bCollage)
			return nullptr;

		return ImageWindowBase::GetToolFromLButtonDown(point);
	}

	// Full screen is black; every windowed mode uses the system's window colour.
	// There is no option here -- one that only changed the picture's backdrop
	// read as a theme switch and was in the way of the one that matters.
	COLORREF GetBackGroundColor() const
	{
		return _pCoupling->IsFullScreen() ? RGB(0, 0, 0) : IW::Style::Color::Window;
	}

	void ShowFrames()
	{		
		Accept(FrameVisitorActivate(m_hWnd));

		ImageWindowBase::ShowFrames();
	}	


	// A collage has no one picture behind it, so everything that acts on one --
	// the zoom strip, the description panel, the navigator -- stands down.
	bool CanShowControls() const 
	{
		return _state.Image.IsImageShown() && !_bCollage;
	}

	// The pointer only auto-hides with the chrome gone.
	bool CanHideCursor() const
	{
		return _pCoupling->IsFullScreen();
	}

	void OnScaleChange()
	{
		_frameNavigation.SetVisible(CanShowNavigation());

		if (_scaleEdit.m_hWnd != nullptr)
		{
			// The percentage, not the mode: "Fit" does not say how big the picture
			// on screen actually is, which is the one thing the readout is for.
			CString str;
			str.Format(_T("%d%%"), IW::Max(1, GetScale()));

			if (str != _strScaleShown)
			{
				_strScaleShown = str;
				_bInScaleUpdate = true;
				_scaleEdit.SetWindowText(str);
				_bInScaleUpdate = false;
			}
		}
	}

	// Told by the frame's idle pass, so these buttons say the same thing as the
	// menu item behind them.
	void SetGutterState(WORD id, bool bEnabled, bool bChecked)
	{
		if (_gutterBar.m_hWnd == nullptr)
			return;

		switch (id)
		{
		case ID_VIEW_FILMSTRIP:
			_gutterBar.CheckButton(id, bChecked);
			_gutterBar.EnableButton(id, bEnabled);
			break;
		}
	}
};
