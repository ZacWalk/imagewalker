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

	FrameImageCtrl<CImageCtrl> _frameControls;
	FrameImageInfo _frameImageInfo;
	FrameShowImageInfo _frameShowImageInfo;

	FrameImageWalker _frameImageWalker;

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
		_frameImageInfo(_frameParent, state),
		_frameShowImageInfo(_frameParent),
		_frameControls(_frameParent, this, state),
		_frameImageWalker(_frameParent),
		_toolCompare(*this)
	{
		_frames.push_back(&_frameImageWalker);
		_frames.push_back(&_frameImageInfo);
		_frames.push_back(&_frameShowImageInfo);
		_frames.push_back(&_frameControls);		
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
		_frameImageInfo.Refresh();	
		_frameImageWalker.Refresh();

		ShowFrames();
		SizeClients();
		ImageWindowBase::ShowMouseMoveFeedback();
		Invalidate();
	}

	void RefreshDescription()
	{
		_frameImageInfo.Refresh();
	}

	void LayoutFrames(int cx, int cy)
	{
		SizeClients();
		Invalidate();
	}	

	void OnCommand(WORD id)
	{
		_pCoupling->Command(id);
	}	

	BEGIN_MSG_MAP(CImageCtrl)

		MESSAGE_HANDLER(WM_SIZE, OnSize)
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
		RegisterDropTarget();
		_state.ResetFrames.Bind(this, &ThisClass::OnResetFrames);
		ShowFrames();
		bHandled = false;
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

	COLORREF GetBackGroundColor() const
	{
		return IW::Style::Color::Window;
	}

	void ShowFrames()
	{		
		_frameControls.SetVisible(CanShowControls());
		_frameImageInfo.SetVisible(CanShowImageInfo());
		_frameShowImageInfo.SetVisible(CanShowImageInfoButton());
		_frameImageWalker.SetVisible(CanShowHelp());

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
	
	bool CanShowHelp() const
	{
		return !_state.Image.IsImageShown() && !_bCollage && _strNoPreview.IsEmpty();
	}

	bool CanShowImageInfo() const
	{
		return App.Settings.ShowDescription &&
			_state.Image.IsImageShown() &&
			!_bCollage &&
			!_state.Image.IsImageEditMode();
	}

	// Same conditions, with the panel closed: this is what opens it again.
	bool CanShowImageInfoButton() const
	{
		return !App.Settings.ShowDescription &&
			_state.Image.IsImageShown() &&
			!_bCollage &&
			!_state.Image.IsImageEditMode();
	}

	void OnScaleChange()
	{
		_frameNavigation.SetVisible(CanShowNavigation());
		_frameControls.OnScaleChange();
	}
};
